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
 *   Copyright 2002-2014, Haiku, Inc. All Rights Reserved.
 *   Authors:
 *       Tyler Dauwalder
 *       Rene Gollent, rene@gollent.com.
 *       Michael Lotz, mmlr@mlotz.ch
 *       Jonas Sundström, jonas@kirilla.com
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file MimeInfoUpdater.cpp
 * @brief Updates MIME type and application metadata attributes on filesystem entries.
 *
 * MimeInfoUpdater walks a filesystem entry, guesses its MIME type via the
 * MIME database, and writes the result to the BEOS:TYPE attribute.  For shared
 * object files it additionally mirrors all application metadata (signature,
 * catalog entry, app flags, icons, version info, and supported-type icons)
 * from the file's resources into its extended attributes so that the rest of
 * the system can find them without loading the file.
 *
 * @see MimeInfoUpdater
 */


#include <mime/MimeInfoUpdater.h>

#include <stdlib.h>

#include <AppFileInfo.h>
#include <Bitmap.h>
#include <File.h>
#include <fs_attr.h>
#include <MimeType.h>
#include <String.h>

#include <AutoLocker.h>
#include <mime/Database.h>
#include <mime/database_support.h>


static const char *kAppFlagsAttribute = "BEOS:APP_FLAGS";


/**
 * @brief Copies a bitmap icon for a supported type from one BAppFileInfo to another.
 *
 * Reads the icon for \a type at \a iconSize from \a appFileInfoRead and writes it
 * to \a appFileInfoWrite.  If the source has no icon for the type, the destination's
 * icon is cleared.
 *
 * @param appFileInfoRead  Source BAppFileInfo (reads from resources).
 * @param appFileInfoWrite Destination BAppFileInfo (writes to attributes).
 * @param type             MIME type string for which the icon applies.
 * @param icon             Pre-allocated BBitmap sized for \a iconSize.
 * @param iconSize         Either B_MINI_ICON or B_LARGE_ICON.
 * @return B_OK on success, or an error code on failure.
 */
static status_t
update_icon(BAppFileInfo &appFileInfoRead, BAppFileInfo &appFileInfoWrite,
	const char *type, BBitmap &icon, icon_size iconSize)
{
	status_t err = appFileInfoRead.GetIconForType(type, &icon, iconSize);
	if (err == B_OK)
		err = appFileInfoWrite.SetIconForType(type, &icon, iconSize, false);
	else if (err == B_ENTRY_NOT_FOUND)
		err = appFileInfoWrite.SetIconForType(type, NULL, iconSize, false);
	return err;
}


/**
 * @brief Copies a vector icon for a supported type from one BAppFileInfo to another.
 *
 * Reads the raw vector icon data for \a type from \a appFileInfoRead and writes it
 * to \a appFileInfoWrite.  If the source has no icon for the type, the destination's
 * icon is cleared.
 *
 * @param appFileInfoRead  Source BAppFileInfo (reads from resources).
 * @param appFileInfoWrite Destination BAppFileInfo (writes to attributes).
 * @param type             MIME type string for which the icon applies, or NULL for
 *                         the application's own icon.
 * @return B_OK on success, or an error code on failure.
 */
static status_t
update_icon(BAppFileInfo &appFileInfoRead, BAppFileInfo &appFileInfoWrite,
	const char *type)
{
	uint8* data = NULL;
	size_t size = 0;

	status_t err = appFileInfoRead.GetIconForType(type, &data, &size);
	if (err == B_OK)
		err = appFileInfoWrite.SetIconForType(type, data, size, false);
	else if (err == B_ENTRY_NOT_FOUND)
		err = appFileInfoWrite.SetIconForType(type, NULL, size, false);

	free(data);

	return err;
}


/**
 * @brief Returns true when the given MIME type string denotes a shared object.
 *
 * Compares \a type case-insensitively against B_APP_MIME_TYPE to determine
 * whether the associated file should have its application metadata synchronised.
 *
 * @param type The MIME type string to test.
 * @return true if \a type represents a shared-object / application MIME type.
 */
static bool
is_shared_object_mime_type(const BString &type)
{
	return type.ICompare(B_APP_MIME_TYPE) == 0;
}


namespace BPrivate {
namespace Storage {
namespace Mime {


/**
 * @brief Constructs a MimeInfoUpdater.
 *
 * @param database       Pointer to the MIME Database instance used for type guessing.
 * @param databaseLocker Pointer to the locker guarding the database.
 * @param force          Force-update flag forwarded to MimeEntryProcessor.
 */
MimeInfoUpdater::MimeInfoUpdater(Database* database,
	DatabaseLocker* databaseLocker, int32 force)
	:
	MimeEntryProcessor(database, databaseLocker, force)
{
}


/**
 * @brief Destroys the MimeInfoUpdater.
 */
MimeInfoUpdater::~MimeInfoUpdater()
{
}


/**
 * @brief Updates MIME and application metadata attributes for the given entry.
 *
 * Determines whether the BEOS:TYPE attribute needs updating (based on the force
 * flag or its absence), guesses the MIME type via the database, and writes it.
 * For shared-object files it then mirrors all application-level metadata from
 * the file's resource fork into its extended attributes, including the signature,
 * catalog entry, app flags, icons (vector, mini, large), version information, and
 * per-supported-type icons.
 *
 * @param entry       Reference to the filesystem entry to process.
 * @param _entryIsDir Optional output parameter set to true when the entry is
 *                    a directory.
 * @return B_OK on success, or an error code on failure.
 */
status_t
MimeInfoUpdater::Do(const entry_ref& entry, bool* _entryIsDir)
{
	bool updateType = false;
	bool updateAppInfo = false;
	BNode node;

	status_t err = node.SetTo(&entry);
	if (!err && _entryIsDir)
		*_entryIsDir = node.IsDirectory();
	if (!err) {
		// If not forced, only update if the entry has no file type attribute
		attr_info info;
		if (fForce == B_UPDATE_MIME_INFO_FORCE_UPDATE_ALL
			|| node.GetAttrInfo(kFileTypeAttr, &info) == B_ENTRY_NOT_FOUND) {
			updateType = true;
		}
		updateAppInfo = (updateType
			|| fForce == B_UPDATE_MIME_INFO_FORCE_KEEP_TYPE);
	}

	// guess the MIME type
	BString type;
	if (!err && (updateType || updateAppInfo)) {
		AutoLocker<DatabaseLocker> databaseLocker(fDatabaseLocker);
		err = fDatabase->GuessMimeType(&entry, &type);
	}

	// update the MIME type
	if (!err && updateType) {
		ssize_t len = type.Length() + 1;
		ssize_t bytes = node.WriteAttr(kFileTypeAttr, kFileTypeType, 0, type,
			len);
		if (bytes < B_OK)
			err = bytes;
		else
			err = (bytes != len ? (status_t)B_FILE_ERROR : (status_t)B_OK);
	}

	// update the app file info attributes, if this is a shared object
	BFile file;
	BAppFileInfo appFileInfoRead;
	BAppFileInfo appFileInfoWrite;
	if (!err && updateAppInfo && node.IsFile()
		&& is_shared_object_mime_type(type)
		&& (err = file.SetTo(&entry, B_READ_WRITE)) == B_OK
		&& (err = appFileInfoRead.SetTo(&file)) == B_OK
		&& (err = appFileInfoWrite.SetTo(&file)) == B_OK) {

		// we read from resources and write to attributes
		appFileInfoRead.SetInfoLocation(B_USE_RESOURCES);
		appFileInfoWrite.SetInfoLocation(B_USE_ATTRIBUTES);

		// signature
		char signature[B_MIME_TYPE_LENGTH];
		err = appFileInfoRead.GetSignature(signature);
		if (err == B_OK)
			err = appFileInfoWrite.SetSignature(signature);
		else if (err == B_ENTRY_NOT_FOUND)
			err = appFileInfoWrite.SetSignature(NULL);
		if (err != B_OK)
			return err;

		// catalog entry
		char catalogEntry[B_MIME_TYPE_LENGTH * 3];
		err = appFileInfoRead.GetCatalogEntry(catalogEntry);
		if (err == B_OK)
			err = appFileInfoWrite.SetCatalogEntry(catalogEntry);
		else if (err == B_ENTRY_NOT_FOUND)
			err = appFileInfoWrite.SetCatalogEntry(NULL);
		if (err != B_OK)
			return err;

		// app flags
		uint32 appFlags;
		err = appFileInfoRead.GetAppFlags(&appFlags);
		if (err == B_OK) {
			err = appFileInfoWrite.SetAppFlags(appFlags);
		} else if (err == B_ENTRY_NOT_FOUND) {
			file.RemoveAttr(kAppFlagsAttribute);
			err = B_OK;
		}
		if (err != B_OK)
			return err;

		// supported types
		BMessage supportedTypes;
		bool hasSupportedTypes = false;
		err = appFileInfoRead.GetSupportedTypes(&supportedTypes);
		if (err == B_OK) {
			err = appFileInfoWrite.SetSupportedTypes(&supportedTypes, false,
				false);
			hasSupportedTypes = true;
		} else if (err == B_ENTRY_NOT_FOUND)
			err = appFileInfoWrite.SetSupportedTypes(NULL, false, false);
		if (err != B_OK)
			return err;

		// vector icon
		err = update_icon(appFileInfoRead, appFileInfoWrite, NULL);
		if (err != B_OK)
			return err;

		// small icon
		BBitmap smallIcon(BRect(0, 0, 15, 15), B_BITMAP_NO_SERVER_LINK,
			B_CMAP8);
		if (smallIcon.InitCheck() != B_OK)
			return smallIcon.InitCheck();
		err = update_icon(appFileInfoRead, appFileInfoWrite, NULL, smallIcon,
			B_MINI_ICON);
		if (err != B_OK)
			return err;

		// large icon
		BBitmap largeIcon(BRect(0, 0, 31, 31), B_BITMAP_NO_SERVER_LINK,
			B_CMAP8);
		if (largeIcon.InitCheck() != B_OK)
			return largeIcon.InitCheck();
		err = update_icon(appFileInfoRead, appFileInfoWrite, NULL, largeIcon,
			B_LARGE_ICON);
		if (err != B_OK)
			return err;

		// version infos
		const version_kind versionKinds[]
			= {B_APP_VERSION_KIND, B_SYSTEM_VERSION_KIND};
		for (int i = 0; i < 2; i++) {
			version_kind kind = versionKinds[i];
			version_info versionInfo;
			err = appFileInfoRead.GetVersionInfo(&versionInfo, kind);
			if (err == B_OK)
				err = appFileInfoWrite.SetVersionInfo(&versionInfo, kind);
			else if (err == B_ENTRY_NOT_FOUND)
				err = appFileInfoWrite.SetVersionInfo(NULL, kind);
			if (err != B_OK)
				return err;
		}

		// icons for supported types
		if (hasSupportedTypes) {
			const char *supportedType;
			for (int32 i = 0;
				 supportedTypes.FindString("types", i, &supportedType) == B_OK;
				 i++) {
				// vector icon
				err = update_icon(appFileInfoRead, appFileInfoWrite,
					supportedType);
				if (err != B_OK)
					return err;

				// small icon
				err = update_icon(appFileInfoRead, appFileInfoWrite,
					supportedType, smallIcon, B_MINI_ICON);
				if (err != B_OK)
					return err;

				// large icon
				err = update_icon(appFileInfoRead, appFileInfoWrite,
					supportedType, largeIcon, B_LARGE_ICON);
				if (err != B_OK)
					return err;
			}
		}
	}

	return err;
}


} // namespace Mime
} // namespace Storage
} // namespace BPrivate
