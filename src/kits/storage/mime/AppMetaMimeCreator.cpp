/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors:
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2002-2013, Haiku, Inc. All Rights Reserved.
 *   Authors:
 *       Tyler Dauwalder
 *       Axel Dörfler, axeld@pinc-software.de
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file AppMetaMimeCreator.cpp
 * @brief Creates and populates MIME database entries from application metadata.
 *
 * This file implements AppMetaMimeCreator, which inspects application files for
 * embedded metadata (signature, icons, supported types, etc.) and registers
 * those details into the MIME database. It reads BEOS:APP_SIG and related
 * resource/attribute data, installing or updating the corresponding MIME type
 * entry under the database location. Deferred notifications are used so that
 * observers receive a single batched update after all fields are written.
 *
 * @see AppMetaMimeCreator
 */


#include <mime/AppMetaMimeCreator.h>

#include <stdlib.h>

#include <AppFileInfo.h>
#include <Bitmap.h>
#include <File.h>
#include <fs_attr.h>
#include <Message.h>
#include <MimeType.h>
#include <String.h>

#include <AutoLocker.h>
#include <mime/Database.h>
#include <mime/database_support.h>
#include <mime/DatabaseLocation.h>


namespace BPrivate {
namespace Storage {
namespace Mime {


/**
 * @brief Constructs an AppMetaMimeCreator.
 *
 * @param database       Pointer to the MIME Database instance to update.
 * @param databaseLocker Pointer to the locker guarding the database.
 * @param force          Force-update flag forwarded to MimeEntryProcessor.
 */
AppMetaMimeCreator::AppMetaMimeCreator(Database* database,
	DatabaseLocker* databaseLocker, int32 force)
	:
	MimeEntryProcessor(database, databaseLocker, force)
{
}


/**
 * @brief Destroys the AppMetaMimeCreator.
 */
AppMetaMimeCreator::~AppMetaMimeCreator()
{
}


/**
 * @brief Processes a filesystem entry and creates or updates its MIME database record.
 *
 * Opens the entry as a file and reads its application signature from the
 * BEOS:APP_SIG attribute.  If the signature is valid the method installs the
 * type (if absent) and then conditionally writes the preferred app, short
 * description, app hint, icons, and supported-types attributes — respecting
 * the force flag.  Deferred install notifications are used to batch observer
 * updates for every supported type touched during the operation.
 *
 * @param entry       Reference to the filesystem entry to process.
 * @param _entryIsDir Optional output parameter set to true when the entry is
 *                    a directory.
 * @return B_OK on success, B_BAD_TYPE if the entry has no valid app signature,
 *         or another error code on failure.
 */
status_t
AppMetaMimeCreator::Do(const entry_ref& entry, bool* _entryIsDir)
{
	BFile file;
	status_t status = file.SetTo(&entry, B_READ_ONLY | O_NOTRAVERSE);
	if (status < B_OK)
		return status;

	bool isDir = file.IsDirectory();
	if (_entryIsDir != NULL)
		*_entryIsDir = isDir;

	if (isDir || !file.IsFile())
		return B_OK;

	BAppFileInfo appInfo(&file);
	status = appInfo.InitCheck();
	if (status < B_OK)
		return status;

	// Read the app sig (which consequently keeps us from updating
	// non-applications, since we get an error if the file has no
	// app sig)
	BString signature;
	status = file.ReadAttrString("BEOS:APP_SIG", &signature);
	if (status != B_OK)
		return B_BAD_TYPE;

	if (!BMimeType::IsValid(signature))
		return B_BAD_TYPE;

	InstallNotificationDeferrer _(fDatabase, signature.String());

	if (!fDatabase->Location()->IsInstalled(signature)) {
		AutoLocker<DatabaseLocker> databaseLocker(fDatabaseLocker);
		fDatabase->Install(signature);
	}

	BNode typeNode;
	status = fDatabase->Location()->OpenType(signature, typeNode);
	if (status != B_OK)
		return status;

	// Preferred App
	attr_info info;
	if (status == B_OK
		&& (fForce || typeNode.GetAttrInfo(kPreferredAppAttr, &info) != B_OK)) {
		AutoLocker<DatabaseLocker> databaseLocker(fDatabaseLocker);
		status = fDatabase->SetPreferredApp(signature, signature);
	}

	// Short Description (name of the application)
	if (status == B_OK
		&& (fForce
			|| typeNode.GetAttrInfo(kShortDescriptionAttr, &info) != B_OK)) {
		AutoLocker<DatabaseLocker> databaseLocker(fDatabaseLocker);
		status = fDatabase->SetShortDescription(signature, entry.name);
	}

	// App Hint
	if (status == B_OK
		&& (fForce || typeNode.GetAttrInfo(kAppHintAttr, &info) != B_OK)) {
		AutoLocker<DatabaseLocker> databaseLocker(fDatabaseLocker);
		status = fDatabase->SetAppHint(signature, &entry);
	}

	// Vector Icon
	if (status == B_OK
		&& (fForce || typeNode.GetAttrInfo(kIconAttr, &info) != B_OK)) {
		uint8* data = NULL;
		size_t size = 0;
		if (appInfo.GetIcon(&data, &size) == B_OK) {
			AutoLocker<DatabaseLocker> databaseLocker(fDatabaseLocker);
			status = fDatabase->SetIcon(signature, data, size);
			free(data);
		}
	}
	// Mini Icon
	BBitmap miniIcon(BRect(0, 0, 15, 15), B_BITMAP_NO_SERVER_LINK, B_CMAP8);
	if (status == B_OK
		&& (fForce || typeNode.GetAttrInfo(kMiniIconAttr, &info) != B_OK)) {
		if (appInfo.GetIcon(&miniIcon, B_MINI_ICON) == B_OK) {
			AutoLocker<DatabaseLocker> databaseLocker(fDatabaseLocker);
			status = fDatabase->SetIcon(signature, &miniIcon, B_MINI_ICON);
		}
	}
	// Large Icon
	BBitmap largeIcon(BRect(0, 0, 31, 31), B_BITMAP_NO_SERVER_LINK, B_CMAP8);
	if (status == B_OK
		&& (fForce || typeNode.GetAttrInfo(kLargeIconAttr, &info) != B_OK)) {
		if (appInfo.GetIcon(&largeIcon, B_LARGE_ICON) == B_OK) {
			AutoLocker<DatabaseLocker> databaseLocker(fDatabaseLocker);
			status = fDatabase->SetIcon(signature, &largeIcon, B_LARGE_ICON);
		}
	}

	// Supported Types
	bool setSupportedTypes = false;
	BMessage supportedTypes;
	if (status == B_OK
		&& (fForce
			|| typeNode.GetAttrInfo(kSupportedTypesAttr, &info) != B_OK)) {
		if (appInfo.GetSupportedTypes(&supportedTypes) == B_OK)
			setSupportedTypes = true;
	}

	// defer notifications for supported types
	const char* type;
	for (int32 i = 0; supportedTypes.FindString("types", i, &type) == B_OK; i++)
		fDatabase->DeferInstallNotification(type);

	// set supported types
	if (setSupportedTypes) {
		AutoLocker<DatabaseLocker> databaseLocker(fDatabaseLocker);
		status = fDatabase->SetSupportedTypes(signature, &supportedTypes, true);
	}

	// Icons for supported types
	for (int32 i = 0; supportedTypes.FindString("types", i, &type) == B_OK;
		 i++) {
		// vector icon
		uint8* data = NULL;
		size_t size = 0;
		if (status == B_OK
			&& appInfo.GetIconForType(type, &data, &size) == B_OK) {
			AutoLocker<DatabaseLocker> databaseLocker(fDatabaseLocker);
			status = fDatabase->SetIconForType(signature, type, data, size);
			free(data);
		}
		// mini icon
		if (status == B_OK
			&& appInfo.GetIconForType(type, &miniIcon, B_MINI_ICON) == B_OK) {
			AutoLocker<DatabaseLocker> databaseLocker(fDatabaseLocker);
			status = fDatabase->SetIconForType(signature, type, &miniIcon,
				B_MINI_ICON);
		}
		// large icon
		if (status == B_OK
			&& appInfo.GetIconForType(type, &largeIcon, B_LARGE_ICON) == B_OK) {
			AutoLocker<DatabaseLocker> databaseLocker(fDatabaseLocker);
			status = fDatabase->SetIconForType(signature, type, &largeIcon,
				B_LARGE_ICON);
		}
	}

	// undefer notifications for supported types
	for (int32 i = 0; supportedTypes.FindString("types", i, &type) == B_OK; i++)
		fDatabase->UndeferInstallNotification(type);

	return status;
}


} // namespace Mime
} // namespace Storage
} // namespace BPrivate
