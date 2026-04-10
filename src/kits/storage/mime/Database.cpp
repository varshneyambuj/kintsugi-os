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
 *   Copyright 2002-2014, Haiku.
 *   Authors: Tyler Dauwalder, Axel Dörfler, Rene Gollent
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file Database.cpp
 * @brief Master controller for the MIME type database.
 *
 * Database is the central class that coordinates all read and write access to
 * the MIME database. It delegates storage I/O to DatabaseLocation, type-list
 * management to InstalledTypes, supporting-application tracking to
 * SupportingApps, and content-sniffing rules to SnifferRules. Change
 * notifications are broadcast to registered BMessenger subscribers via the
 * pluggable NotificationListener interface.
 *
 * @see DatabaseLocation
 */

#include <mime/Database.h>

#include <stdio.h>
#include <string>

#include <new>

#include <Application.h>
#include <Bitmap.h>
#include <DataIO.h>
#include <Directory.h>
#include <Entry.h>
#include <fs_attr.h>
#include <Message.h>
#include <MimeType.h>
#include <Node.h>
#include <Path.h>
#include <String.h>
#include <TypeConstants.h>

#include <AutoLocker.h>
#include <mime/database_support.h>
#include <mime/DatabaseLocation.h>
#include <storage_support.h>


//#define DBG(x) x
#define DBG(x)
#define OUT printf


namespace BPrivate {
namespace Storage {
namespace Mime {


/**
 * @brief Destroys the NotificationListener base object.
 */
Database::NotificationListener::~NotificationListener()
{
}


/*!
	\class Database
	\brief Mime::Database is the master of the MIME data base.

	All write and non-atomic read accesses are carried out by this class.

	\note No error checking (other than checks for NULL pointers) is performed
	      by this class on the mime type strings passed to it. It's assumed
	      that this sort of checking has been done beforehand.
*/

/**
 * @brief Creates and initialises a Mime::Database object.
 *
 * Ensures the writable MIME database directory exists and initialises all
 * internal subsystems (InstalledTypes, SupportingApps, SnifferRules).
 *
 * @param databaseLocation    Pointer to the DatabaseLocation that resolves paths.
 * @param mimeSniffer         Optional pointer to a MimeSniffer add-on manager.
 * @param notificationListener Optional pointer to a notification delivery sink.
 */
Database::Database(DatabaseLocation* databaseLocation, MimeSniffer* mimeSniffer,
	NotificationListener* notificationListener)
	:
	fStatus(B_NO_INIT),
	fLocation(databaseLocation),
	fNotificationListener(notificationListener),
	fAssociatedTypes(databaseLocation, mimeSniffer),
	fInstalledTypes(databaseLocation),
	fSnifferRules(databaseLocation, mimeSniffer),
	fSupportingApps(databaseLocation),
	fDeferredInstallNotificationsLocker("deferred install notifications"),
	fDeferredInstallNotifications()
{
	// make sure the user's MIME DB directory exists
	fStatus = create_directory(fLocation->WritableDirectory(),
		S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
}

/**
 * @brief Frees all resources associated with this object.
 */
Database::~Database()
{
}

/**
 * @brief Returns the initialisation status of the object.
 *
 * @return B_OK on success, or an error code if initialisation failed.
 */
status_t
Database::InitCheck() const
{
	return fStatus;
}

/**
 * @brief Installs the given MIME type in the database.
 *
 * If the type is already installed, B_FILE_EXISTS is returned. On success,
 * a B_MIME_TYPE_CREATED notification is sent (unless deferred).
 *
 * @param type Pointer to a NULL-terminated MIME type string.
 * @return B_OK on success, B_FILE_EXISTS if already installed, or an error code.
 */
status_t
Database::Install(const char *type)
{
	if (type == NULL)
		return B_BAD_VALUE;

	BEntry entry;
	status_t err = entry.SetTo(fLocation->WritablePathForType(type));
	if (err == B_OK || err == B_ENTRY_NOT_FOUND) {
		if (entry.Exists())
			err = B_FILE_EXISTS;
		else {
			bool didCreate = false;
			BNode node;
			err = fLocation->OpenWritableType(type, node, true, &didCreate);
			if (!err && didCreate) {
				fInstalledTypes.AddType(type);
				_SendInstallNotification(type);
			}
		}
	}
	return err;
}

/**
 * @brief Removes the given MIME type (and any subtypes) from the database.
 *
 * Recursively removes subtypes if the type is a supertype directory. Sends
 * a B_MIME_TYPE_DELETED notification on success.
 *
 * @param type Pointer to a NULL-terminated MIME type string.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::Delete(const char *type)
{
	if (type == NULL)
		return B_BAD_VALUE;

	// Open the type
	BEntry entry;
	status_t status = entry.SetTo(fLocation->WritablePathForType(type));
	if (status != B_OK)
		return status;

	// Remove it
	if (entry.IsDirectory()) {
		// We need to remove all files in this directory
		BDirectory directory(&entry);
		if (directory.InitCheck() == B_OK) {
			size_t length = strlen(type);
			char subType[B_PATH_NAME_LENGTH];
			memcpy(subType, type, length);
			subType[length++] = '/';

			BEntry subEntry;
			while (directory.GetNextEntry(&subEntry) == B_OK) {
				// Construct MIME type and remove it
				if (subEntry.GetName(subType + length) == B_OK) {
					status = Delete(subType);
					if (status != B_OK)
						return status;
				}
			}
		}
	}

	status = entry.Remove();

	if (status == B_OK) {
		// Notify the installed types database
		fInstalledTypes.RemoveType(type);
		// Notify the supporting apps database
		fSupportingApps.DeleteSupportedTypes(type, true);
		// Notify the monitor service
		_SendDeleteNotification(type);
	}

	return status;
}


/**
 * @brief Internal helper that writes a string attribute and sends a change notification.
 *
 * Reads the current value first; if the value is unchanged the write is skipped.
 * Sends B_MIME_TYPE_CREATED if the type node had to be created, otherwise sends
 * the supplied @a what change notification.
 *
 * @param type          The MIME type string.
 * @param what          Monitor notification code to send on modification.
 * @param attribute     Attribute name to write.
 * @param attributeType Attribute type code.
 * @param maxLength     Maximum allowed string length (including NUL).
 * @param value         The string value to write.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::_SetStringValue(const char *type, int32 what, const char* attribute,
	type_code attributeType, size_t maxLength, const char *value)
{
	size_t length = value != NULL ? strlen(value) : 0;
	if (type == NULL || value == NULL || length >= maxLength)
		return B_BAD_VALUE;

	char oldValue[maxLength];
	status_t status = fLocation->ReadAttribute(type, attribute, oldValue,
		maxLength, attributeType);
	if (status >= B_OK && !strcmp(value, oldValue)) {
		// nothing has changed, no need to write back the data
		return B_OK;
	}

	bool didCreate = false;
	status = fLocation->WriteAttribute(type, attribute, value, length + 1,
		attributeType, &didCreate);

	if (status == B_OK) {
		if (didCreate)
			_SendInstallNotification(type);
		else
			_SendMonitorUpdate(what, type, B_META_MIME_MODIFIED);
	}

	return status;
}


/**
 * @brief Sets the application hint for the given MIME type.
 *
 * @param type Pointer to a NULL-terminated MIME type string.
 * @param ref  Pointer to an entry_ref identifying the hint application.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::SetAppHint(const char *type, const entry_ref *ref)
{
	DBG(OUT("Database::SetAppHint()\n"));

	if (type == NULL || ref == NULL)
		return B_BAD_VALUE;

	BPath path;
	status_t status = path.SetTo(ref);
	if (status < B_OK)
		return status;

	return _SetStringValue(type, B_APP_HINT_CHANGED, kAppHintAttr,
		kAppHintType, B_PATH_NAME_LENGTH, path.Path());
}

/**
 * @brief Stores a BMessage describing the format of attributes typically
 *        associated with files of the given MIME type.
 *
 * See BMimeType::SetAttrInfo() for the expected message format. The
 * BMessage::what value is ignored.
 *
 * @param type The MIME type string.
 * @param info Pointer to a properly formatted BMessage with attribute info.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::SetAttrInfo(const char *type, const BMessage *info)
{
	DBG(OUT("Database::SetAttrInfo()\n"));

	if (type == NULL || info == NULL)
		return B_BAD_VALUE;

	bool didCreate = false;
	status_t status = fLocation->WriteMessageAttribute(type, kAttrInfoAttr,
		*info, &didCreate);
	if (status == B_OK) {
		if (didCreate)
			_SendInstallNotification(type);
		else
			_SendMonitorUpdate(B_ATTR_INFO_CHANGED, type, B_META_MIME_MODIFIED);
	}

	return status;
}


/**
 * @brief Sets the short description for the given MIME type.
 *
 * @param type        The MIME type string.
 * @param description NULL-terminated string containing the new short description.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::SetShortDescription(const char *type, const char *description)
{
	DBG(OUT("Database::SetShortDescription()\n"));

	return _SetStringValue(type, B_SHORT_DESCRIPTION_CHANGED, kShortDescriptionAttr,
		kShortDescriptionType, B_MIME_TYPE_LENGTH, description);
}

/**
 * @brief Sets the long description for the given MIME type.
 *
 * @param type        The MIME type string.
 * @param description NULL-terminated string containing the new long description.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::SetLongDescription(const char *type, const char *description)
{
	DBG(OUT("Database::SetLongDescription()\n"));

	size_t length = description != NULL ? strlen(description) : 0;
	if (type == NULL || description == NULL || length >= B_MIME_TYPE_LENGTH)
		return B_BAD_VALUE;

	return _SetStringValue(type, B_LONG_DESCRIPTION_CHANGED, kLongDescriptionAttr,
		kLongDescriptionType, B_MIME_TYPE_LENGTH, description);
}


/**
 * @brief Sets the list of filename extensions associated with the MIME type.
 *
 * The @a extensions message format is described by BMimeType::SetFileExtensions().
 *
 * @param type       The MIME type string.
 * @param extensions Pointer to a properly formatted BMessage with extensions.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::SetFileExtensions(const char *type, const BMessage *extensions)
{
	DBG(OUT("Database::SetFileExtensions()\n"));

	if (type == NULL || extensions == NULL)
		return B_BAD_VALUE;

	bool didCreate = false;
	status_t status = fLocation->WriteMessageAttribute(type,
		kFileExtensionsAttr, *extensions, &didCreate);

	if (status == B_OK) {
		if (didCreate) {
			_SendInstallNotification(type);
		} else {
			_SendMonitorUpdate(B_FILE_EXTENSIONS_CHANGED, type,
				B_META_MIME_MODIFIED);
		}
	}

	return status;
}


/**
 * @brief Sets the bitmap icon (from a BBitmap) for the given MIME type.
 *
 * @param type  The MIME type string.
 * @param icon  Pointer to a BBitmap (NULL to clear the icon).
 * @param which B_LARGE_ICON or B_MINI_ICON.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::SetIcon(const char* type, const BBitmap* icon, icon_size which)
{
	if (icon != NULL)
		return SetIcon(type, icon->Bits(), icon->BitsLength(), which);
	return SetIcon(type, NULL, 0, which);
}


/**
 * @brief Sets a raw bitmap icon for the given MIME type.
 *
 * @param type     The MIME type string.
 * @param data     Pointer to the bitmap data.
 * @param dataSize Size of the bitmap data in bytes.
 * @param which    B_LARGE_ICON (32x32) or B_MINI_ICON (16x16).
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::SetIcon(const char *type, const void *data, size_t dataSize,
	icon_size which)
{
	return SetIconForType(type, NULL, data, dataSize, which);
}


/**
 * @brief Sets the vector icon for the given MIME type.
 *
 * @param type     The MIME type string.
 * @param data     Pointer to the vector icon data.
 * @param dataSize Size of the icon data in bytes.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::SetIcon(const char *type, const void *data, size_t dataSize)
{
	return SetIconForType(type, NULL, data, dataSize);
}


/**
 * @brief Sets the per-file-type bitmap icon for an application type (BBitmap variant).
 *
 * @param type     The application MIME type string.
 * @param fileType The file MIME type for which the custom icon is set.
 * @param icon     Pointer to a BBitmap (NULL to clear).
 * @param which    B_LARGE_ICON or B_MINI_ICON.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::SetIconForType(const char* type, const char* fileType,
	const BBitmap* icon, icon_size which)
{
	if (icon != NULL) {
		return SetIconForType(type, fileType, icon->Bits(),
			(size_t)icon->BitsLength(), which);
	}
	return SetIconForType(type, fileType, NULL, 0, which);
}


/**
 * @brief Sets the large or mini bitmap icon an application uses for files of
 *        the given type.
 *
 * The bitmap data must be of the correct size (32x32 for B_LARGE_ICON,
 * 16x16 for B_MINI_ICON) in B_CMAP8 colour space.
 *
 * @param type     The application MIME type string.
 * @param fileType The file MIME type whose custom icon is set. Pass NULL to
 *                 set the application's own icon.
 * @param data     Pointer to the bitmap data.
 * @param dataSize Size of the bitmap data in bytes.
 * @param which    B_LARGE_ICON or B_MINI_ICON.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::SetIconForType(const char *type, const char *fileType,
	const void *data, size_t dataSize, icon_size which)
{
	DBG(OUT("Database::SetIconForType()\n"));

	if (type == NULL || data == NULL)
		return B_BAD_VALUE;

	int32 attrType = 0;

	// Figure out what kind of data we *should* have
	switch (which) {
		case B_MINI_ICON:
			attrType = kMiniIconType;
			break;
		case B_LARGE_ICON:
			attrType = kLargeIconType;
			break;

		default:
			return B_BAD_VALUE;
	}

	size_t attrSize = (size_t)which * (size_t)which;
	// Double check the data we've been given
	if (dataSize != attrSize)
		return B_BAD_VALUE;

	// Construct our attribute name
	std::string attr;
	if (fileType) {
		attr = (which == B_MINI_ICON
			? kMiniIconAttrPrefix : kLargeIconAttrPrefix)
			+ BPrivate::Storage::to_lower(fileType);
	} else
		attr = which == B_MINI_ICON ? kMiniIconAttr : kLargeIconAttr;

	// Write the icon data
	BNode node;
	bool didCreate = false;

	status_t err = fLocation->OpenWritableType(type, node, true, &didCreate);
	if (err != B_OK)
		return err;

	if (!err)
		err = node.WriteAttr(attr.c_str(), attrType, 0, data, attrSize);
	if (err >= 0)
		err = err == (ssize_t)attrSize ? (status_t)B_OK : (status_t)B_FILE_ERROR;
	if (didCreate) {
		_SendInstallNotification(type);
	} else if (!err) {
		if (fileType) {
			_SendMonitorUpdate(B_ICON_FOR_TYPE_CHANGED, type, fileType,
				which == B_LARGE_ICON, B_META_MIME_MODIFIED);
		} else {
			_SendMonitorUpdate(B_ICON_CHANGED, type,
				which == B_LARGE_ICON, B_META_MIME_MODIFIED);
		}
	}
	return err;
}

/**
 * @brief Sets the vector icon an application uses for files of the given type.
 *
 * @param type     The application MIME type string.
 * @param fileType The file MIME type whose custom vector icon is set. Pass NULL
 *                 to set the application's own vector icon.
 * @param data     Pointer to the vector icon data.
 * @param dataSize Size of the icon data in bytes.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::SetIconForType(const char *type, const char *fileType,
	const void *data, size_t dataSize)
{
	DBG(OUT("Database::SetIconForType()\n"));

	if (type == NULL || data == NULL)
		return B_BAD_VALUE;

	int32 attrType = B_VECTOR_ICON_TYPE;

	// Construct our attribute name
	std::string attr;
	if (fileType) {
		attr = kIconAttrPrefix + BPrivate::Storage::to_lower(fileType);
	} else
		attr = kIconAttr;

	// Write the icon data
	BNode node;
	bool didCreate = false;

	status_t err = fLocation->OpenWritableType(type, node, true, &didCreate);
	if (err != B_OK)
		return err;

	if (!err)
		err = node.WriteAttr(attr.c_str(), attrType, 0, data, dataSize);
	if (err >= 0)
		err = err == (ssize_t)dataSize ? (status_t)B_OK : (status_t)B_FILE_ERROR;
	if (didCreate) {
		_SendInstallNotification(type);
	} else if (!err) {
		// TODO: extra notification for vector icons (currently
		// passing "true" for B_LARGE_ICON)?
		if (fileType) {
			_SendMonitorUpdate(B_ICON_FOR_TYPE_CHANGED, type, fileType,
				true, B_META_MIME_MODIFIED);
		} else {
			_SendMonitorUpdate(B_ICON_CHANGED, type, true,
				B_META_MIME_MODIFIED);
		}
	}
	return err;
}

/**
 * @brief Sets the preferred application signature for the given app verb.
 *
 * Currently only B_OPEN is supported as an app_verb.
 *
 * @param type      The MIME type string.
 * @param signature NULL-terminated MIME signature of the preferred application.
 * @param verb      The app verb (currently only B_OPEN is used).
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::SetPreferredApp(const char *type, const char *signature, app_verb verb)
{
	DBG(OUT("Database::SetPreferredApp()\n"));

	// TODO: use "verb" some day!

	return _SetStringValue(type, B_PREFERRED_APP_CHANGED, kPreferredAppAttr,
		kPreferredAppType, B_MIME_TYPE_LENGTH, signature);
}

/**
 * @brief Sets the sniffer rule for the given MIME type.
 *
 * The rule is stored persistently in the database and also loaded into the
 * in-memory SnifferRules table.
 *
 * @param type The MIME type string.
 * @param rule The sniffer rule string.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::SetSnifferRule(const char *type, const char *rule)
{
	DBG(OUT("Database::SetSnifferRule()\n"));

	if (type == NULL || rule == NULL)
		return B_BAD_VALUE;

	bool didCreate = false;
	status_t status = fLocation->WriteAttribute(type, kSnifferRuleAttr, rule,
		strlen(rule) + 1, kSnifferRuleType, &didCreate);

	if (status == B_OK)
		status = fSnifferRules.SetSnifferRule(type, rule);

	if (didCreate) {
		_SendInstallNotification(type);
	} else if (status == B_OK) {
		_SendMonitorUpdate(B_SNIFFER_RULE_CHANGED, type,
			B_META_MIME_MODIFIED);
	}

	return status;
}

/**
 * @brief Sets the list of MIME types supported by the given type and synchronises
 *        the internal supporting-apps database.
 *
 * Any newly referenced types that are not yet installed are automatically
 * installed and set to prefer the given type as their handler. See
 * BMimeType::SetSupportedTypes() for details.
 *
 * @param type     The MIME type string.
 * @param types    Pointer to a BMessage whose "types" array lists supported types.
 * @param fullSync If true, also remove the type as a supporting app for previously
 *                 supported types that no longer appear in @a types.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::SetSupportedTypes(const char *type, const BMessage *types, bool fullSync)
{
	DBG(OUT("Database::SetSupportedTypes()\n"));

	if (type == NULL || types == NULL)
		return B_BAD_VALUE;

	// Install the types
	const char *supportedType;
	for (int32 i = 0; types->FindString("types", i, &supportedType) == B_OK; i++) {
		if (!fLocation->IsInstalled(supportedType)) {
			if (Install(supportedType) != B_OK)
				break;

			// Since the type has been introduced by this application
			// we take the liberty and make it the preferred handler
			// for them, too.
			SetPreferredApp(supportedType, type, B_OPEN);
		}
	}

	// Write the attr
	bool didCreate = false;
	status_t status = fLocation->WriteMessageAttribute(type,
		kSupportedTypesAttr, *types, &didCreate);

	// Notify the monitor if we created the type when we opened it
	if (status != B_OK)
		return status;

	// Update the supporting apps map
	if (status == B_OK)
		status = fSupportingApps.SetSupportedTypes(type, types, fullSync);

	// Notify the monitor
	if (didCreate) {
		_SendInstallNotification(type);
	} else if (status == B_OK) {
		_SendMonitorUpdate(B_SUPPORTED_TYPES_CHANGED, type,
			B_META_MIME_MODIFIED);
	}

	return status;
}


/**
 * @brief Retrieves a BMessage listing all installed MIME supertypes.
 *
 * @param supertypes Pointer to a pre-allocated BMessage to receive the list.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::GetInstalledSupertypes(BMessage *supertypes)
{
	return fInstalledTypes.GetInstalledSupertypes(supertypes);
}

/**
 * @brief Retrieves a BMessage listing all installed MIME types.
 *
 * @param types Pointer to a pre-allocated BMessage to receive the list.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::GetInstalledTypes(BMessage *types)
{
	return fInstalledTypes.GetInstalledTypes(types);
}

/**
 * @brief Retrieves a BMessage listing all installed subtypes of a supertype.
 *
 * @param supertype  The supertype string (e.g. "text").
 * @param subtypes   Pointer to a pre-allocated BMessage to receive subtypes.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::GetInstalledTypes(const char *supertype, BMessage *subtypes)
{
	return fInstalledTypes.GetInstalledTypes(supertype, subtypes);
}

/**
 * @brief Retrieves the list of applications that support the given MIME type.
 *
 * See BMimeType::GetSupportingApps() for the message format.
 *
 * @param type       The MIME type string.
 * @param signatures Pointer to a pre-allocated BMessage to receive the list.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::GetSupportingApps(const char *type, BMessage *signatures)
{
	return fSupportingApps.GetSupportingApps(type, signatures);
}

/**
 * @brief Returns a list of MIME types associated with the given file extension.
 *
 * @param extension The filename extension string.
 * @param types     Pointer to a pre-allocated BMessage to receive the list.
 * @return B_ERROR (not yet implemented).
 */
status_t
Database::GetAssociatedTypes(const char *extension, BMessage *types)
{
	return B_ERROR;
}

/**
 * @brief Guesses the MIME type for the entry referred to by an entry_ref.
 *
 * Combines multiple detection strategies: META:TYPE attribute check,
 * special-node type detection (directory, symlink), content sniffing, and
 * filename-extension lookup. Falls back to "application/octet-stream".
 *
 * @param ref    Pointer to an entry_ref identifying the file to examine.
 * @param result Pointer to a pre-allocated BString that receives the type.
 * @return B_OK on success (even when returning the generic type), or an error code.
 */
status_t
Database::GuessMimeType(const entry_ref *ref, BString *result)
{
	if (ref == NULL || result == NULL)
		return B_BAD_VALUE;

	BNode node;
	struct stat statData;
	status_t status = node.SetTo(ref);
	if (status < B_OK)
		return status;

	attr_info info;
	if (node.GetAttrInfo(kTypeAttr, &info) == B_OK) {
		// Check for a META:TYPE attribute
		result->SetTo(kMetaMimeType);
		return B_OK;
	}

	// See if we have a directory, a symlink, or a vanilla file
	status = node.GetStat(&statData);
	if (status < B_OK)
		return status;

	if (S_ISDIR(statData.st_mode)) {
		// Directory
		result->SetTo(kDirectoryType);
	} else if (S_ISLNK(statData.st_mode)) {
		// Symlink
		result->SetTo(kSymlinkType);
	} else if (S_ISREG(statData.st_mode)) {
		// Vanilla file: sniff first
		status = fSnifferRules.GuessMimeType(ref, result);

		// If that fails, check extensions
		if (status == kMimeGuessFailureError)
			status = fAssociatedTypes.GuessMimeType(ref, result);

		// If that fails, return the generic file type
		if (status == kMimeGuessFailureError) {
			result->SetTo(kGenericFileType);
			status = B_OK;
		}
	} else {
		// TODO: we could filter out devices, ...
		return B_BAD_TYPE;
	}

	return status;
}

/**
 * @brief Guesses the MIME type for a raw data buffer.
 *
 * Searches the installed sniffer rules. Falls back to
 * "application/octet-stream" when no rule matches.
 *
 * @param buffer Pointer to the data buffer to sniff.
 * @param length Size of the buffer in bytes.
 * @param result Pointer to a pre-allocated BString that receives the type.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::GuessMimeType(const void *buffer, int32 length, BString *result)
{
	if (buffer == NULL || result == NULL)
		return B_BAD_VALUE;

	status_t status = fSnifferRules.GuessMimeType(buffer, length, result);
	if (status == kMimeGuessFailureError) {
		result->SetTo(kGenericFileType);
		return B_OK;
	}

	return status;
}

/**
 * @brief Guesses the MIME type for a filename based on its extension.
 *
 * Only the filename string is examined; no filesystem access is performed.
 * Falls back to "application/octet-stream" when no extension match is found.
 *
 * @param filename The filename string (need not refer to an existing file).
 * @param result   Pointer to a pre-allocated BString that receives the type.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::GuessMimeType(const char *filename, BString *result)
{
	if (filename == NULL || result == NULL)
		return B_BAD_VALUE;

	status_t status = fAssociatedTypes.GuessMimeType(filename, result);
	if (status == kMimeGuessFailureError) {
		result->SetTo(kGenericFileType);
		return B_OK;
	}

	return status;
}


/**
 * @brief Subscribes a BMessenger to the MIME monitor service.
 *
 * Subscribed messengers receive B_META_MIME_CHANGED messages whenever a MIME
 * database attribute is modified. Message fields include "be:type" (string),
 * "be:which" (int32 bitmask), "be:extra_type" (string, optional), and
 * "be:large_icon" (bool, optional).
 *
 * @param target The BMessenger to subscribe.
 * @return B_OK on success, B_BAD_VALUE if the messenger is invalid.
 */
status_t
Database::StartWatching(BMessenger target)
{
	DBG(OUT("Database::StartWatching()\n"));

	if (!target.IsValid())
		return B_BAD_VALUE;

	fMonitorMessengers.insert(target);
	return B_OK;
}


/**
 * @brief Unsubscribes a BMessenger from the MIME monitor service.
 *
 * @param target The BMessenger to unsubscribe.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if the messenger was not subscribed.
 */
status_t
Database::StopWatching(BMessenger target)
{
	DBG(OUT("Database::StopWatching()\n"));

	if (!target.IsValid())
		return B_BAD_VALUE;

	status_t status = fMonitorMessengers.find(target) != fMonitorMessengers.end()
		? (status_t)B_OK : (status_t)B_ENTRY_NOT_FOUND;
	if (status == B_OK)
		fMonitorMessengers.erase(target);

	return status;
}


/**
 * @brief Deletes the app hint attribute for the given type.
 *
 * Sends a B_APP_HINT_CHANGED notification to the MIME monitor service.
 *
 * @param type The MIME type string.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::DeleteAppHint(const char *type)
{
	status_t status = fLocation->DeleteAttribute(type, kAppHintAttr);
	if (status == B_OK)
		_SendMonitorUpdate(B_APP_HINT_CHANGED, type, B_META_MIME_DELETED);
	else if (status == B_ENTRY_NOT_FOUND)
		status = B_OK;

	return status;
}


/**
 * @brief Deletes the attribute-info attribute for the given type.
 *
 * Sends a B_ATTR_INFO_CHANGED notification to the MIME monitor service.
 *
 * @param type The MIME type string.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::DeleteAttrInfo(const char *type)
{
	status_t status = fLocation->DeleteAttribute(type, kAttrInfoAttr);
	if (status == B_OK)
		_SendMonitorUpdate(B_ATTR_INFO_CHANGED, type, B_META_MIME_DELETED);
	else if (status == B_ENTRY_NOT_FOUND)
		status = B_OK;

	return status;
}


/**
 * @brief Deletes the short description attribute for the given type.
 *
 * Sends a B_SHORT_DESCRIPTION_CHANGED notification to the MIME monitor service.
 *
 * @param type The MIME type string.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::DeleteShortDescription(const char *type)
{
	status_t status = fLocation->DeleteAttribute(type, kShortDescriptionAttr);
	if (status == B_OK)
		_SendMonitorUpdate(B_SHORT_DESCRIPTION_CHANGED, type, B_META_MIME_DELETED);
	else if (status == B_ENTRY_NOT_FOUND)
		status = B_OK;

	return status;
}


/**
 * @brief Deletes the long description attribute for the given type.
 *
 * Sends a B_LONG_DESCRIPTION_CHANGED notification to the MIME monitor service.
 *
 * @param type The MIME type string.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::DeleteLongDescription(const char *type)
{
	status_t status = fLocation->DeleteAttribute(type, kLongDescriptionAttr);
	if (status == B_OK)
		_SendMonitorUpdate(B_LONG_DESCRIPTION_CHANGED, type, B_META_MIME_DELETED);
	else if (status == B_ENTRY_NOT_FOUND)
		status = B_OK;

	return status;
}


/**
 * @brief Deletes the file-extensions attribute for the given type.
 *
 * Sends a B_FILE_EXTENSIONS_CHANGED notification to the MIME monitor service.
 *
 * @param type The MIME type string.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::DeleteFileExtensions(const char *type)
{
	status_t status = fLocation->DeleteAttribute(type, kFileExtensionsAttr);
	if (status == B_OK)
		_SendMonitorUpdate(B_FILE_EXTENSIONS_CHANGED, type, B_META_MIME_DELETED);
	else if (status == B_ENTRY_NOT_FOUND)
		status = B_OK;

	return status;
}


/**
 * @brief Deletes the icon of the given size for the given type.
 *
 * Sends a B_ICON_CHANGED notification to the MIME monitor service.
 *
 * @param type  The MIME type string.
 * @param which B_LARGE_ICON or B_MINI_ICON.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::DeleteIcon(const char *type, icon_size which)
{
	const char *attr = which == B_MINI_ICON ? kMiniIconAttr : kLargeIconAttr;
	status_t status = fLocation->DeleteAttribute(type, attr);
	if (status == B_OK) {
		_SendMonitorUpdate(B_ICON_CHANGED, type, which == B_LARGE_ICON,
			B_META_MIME_DELETED);
	} else if (status == B_ENTRY_NOT_FOUND)
		status = B_OK;

	return status;
}


/**
 * @brief Deletes the vector icon for the given type.
 *
 * Sends a B_ICON_CHANGED notification to the MIME monitor service.
 *
 * @param type The MIME type string.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::DeleteIcon(const char *type)
{
	// TODO: extra notification for vector icon (for now we notify a "large"
	// icon)
	status_t status = fLocation->DeleteAttribute(type, kIconAttr);
	if (status == B_OK) {
		_SendMonitorUpdate(B_ICON_CHANGED, type, true,
						   B_META_MIME_DELETED);
	} else if (status == B_ENTRY_NOT_FOUND)
		status = B_OK;

	return status;
}


/**
 * @brief Deletes the bitmap icon an application uses for files of the given type.
 *
 * Sends a B_ICON_FOR_TYPE_CHANGED notification to the MIME monitor service.
 *
 * @param type     The application MIME type string.
 * @param fileType The file MIME type whose custom icon should be removed.
 * @param which    B_LARGE_ICON or B_MINI_ICON.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::DeleteIconForType(const char *type, const char *fileType, icon_size which)
{
	if (fileType == NULL)
		return B_BAD_VALUE;

	std::string attr = (which == B_MINI_ICON
		? kMiniIconAttrPrefix : kLargeIconAttrPrefix) + BPrivate::Storage::to_lower(fileType);

	status_t status = fLocation->DeleteAttribute(type, attr.c_str());
	if (status == B_OK) {
		_SendMonitorUpdate(B_ICON_FOR_TYPE_CHANGED, type, fileType,
			which == B_LARGE_ICON, B_META_MIME_DELETED);
	} else if (status == B_ENTRY_NOT_FOUND)
		status = B_OK;

	return status;
}


/**
 * @brief Deletes the vector icon an application uses for files of the given type.
 *
 * Sends a B_ICON_FOR_TYPE_CHANGED notification to the MIME monitor service.
 *
 * @param type     The application MIME type string.
 * @param fileType The file MIME type whose custom vector icon should be removed.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::DeleteIconForType(const char *type, const char *fileType)
{
	if (fileType == NULL)
		return B_BAD_VALUE;

	std::string attr = kIconAttrPrefix + BPrivate::Storage::to_lower(fileType);

	// TODO: introduce extra notification for vector icons?
	// (uses B_LARGE_ICON now)
	status_t status = fLocation->DeleteAttribute(type, attr.c_str());
	if (status == B_OK) {
		_SendMonitorUpdate(B_ICON_FOR_TYPE_CHANGED, type, fileType,
			true, B_META_MIME_DELETED);
	} else if (status == B_ENTRY_NOT_FOUND)
		status = B_OK;

	return status;
}


/**
 * @brief Deletes the preferred application for the given app verb.
 *
 * Sends a B_PREFERRED_APP_CHANGED notification to the MIME monitor service.
 *
 * @param type The MIME type string.
 * @param verb The app verb (currently only B_OPEN is supported).
 * @return B_OK on success, B_BAD_VALUE for an unsupported verb,
 *         or another error code on failure.
 */
status_t
Database::DeletePreferredApp(const char *type, app_verb verb)
{
	status_t status;

	switch (verb) {
		case B_OPEN:
			status = fLocation->DeleteAttribute(type, kPreferredAppAttr);
			break;

		default:
			return B_BAD_VALUE;
	}

	/*! \todo The R5 monitor makes no note of which app_verb value was updated. If
		additional app_verb values besides \c B_OPEN are someday added, the format
		of the MIME monitor messages will need to be augmented.
	*/
	if (status == B_OK)
		_SendMonitorUpdate(B_PREFERRED_APP_CHANGED, type, B_META_MIME_DELETED);
	else if (status == B_ENTRY_NOT_FOUND)
		status = B_OK;

	return status;
}

/**
 * @brief Deletes the sniffer rule for the given type.
 *
 * Also removes the corresponding rule from the in-memory SnifferRules table
 * and sends a B_SNIFFER_RULE_CHANGED notification to the MIME monitor service.
 *
 * @param type The MIME type string.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::DeleteSnifferRule(const char *type)
{
	status_t status = fLocation->DeleteAttribute(type, kSnifferRuleAttr);
	if (status == B_OK) {
		status = fSnifferRules.DeleteSnifferRule(type);
		if (status == B_OK) {
			_SendMonitorUpdate(B_SNIFFER_RULE_CHANGED, type,
				B_META_MIME_DELETED);
		}
	} else if (status == B_ENTRY_NOT_FOUND)
		status = B_OK;

	return status;
}

/**
 * @brief Deletes the supported-types list for the given type.
 *
 * Sends a B_SUPPORTED_TYPES_CHANGED notification to the MIME monitor service.
 * If @a fullSync is true, the type is also removed from the supporting-apps
 * entries for all previously supported types.
 *
 * @param type     The MIME type string.
 * @param fullSync Whether to perform a full synchronisation of supporting apps.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::DeleteSupportedTypes(const char *type, bool fullSync)
{
	status_t status = fLocation->DeleteAttribute(type, kSupportedTypesAttr);

	// Update the supporting apps database. If fullSync is specified,
	// do so even if the supported types attribute didn't exist, as
	// stranded types *may* exist in the database due to previous
	// calls to {Set,Delete}SupportedTypes() with fullSync == false.
	bool sendUpdate = true;
	if (status == B_OK)
		status = fSupportingApps.DeleteSupportedTypes(type, fullSync);
	else if (status == B_ENTRY_NOT_FOUND) {
		status = B_OK;
		if (fullSync)
			fSupportingApps.DeleteSupportedTypes(type, fullSync);
		else
			sendUpdate = false;
	}

	// Send a monitor notification
	if (status == B_OK && sendUpdate)
		_SendMonitorUpdate(B_SUPPORTED_TYPES_CHANGED, type, B_META_MIME_DELETED);

	return status;
}


/**
 * @brief Defers the B_MIME_TYPE_CREATED notification for the given type.
 *
 * While deferred, any install notification triggered for this type is held
 * until UndeferInstallNotification() is called.
 *
 * @param type The MIME type string whose install notification to defer.
 */
void
Database::DeferInstallNotification(const char* type)
{
	AutoLocker<BLocker> _(fDeferredInstallNotificationsLocker);

	// check, if already deferred
	if (_FindDeferredInstallNotification(type))
		return;

	// add new
	DeferredInstallNotification* notification
		= new(std::nothrow) DeferredInstallNotification;
	if (notification == NULL)
		return;

	strlcpy(notification->type, type, sizeof(notification->type));
	notification->notify = false;

	if (!fDeferredInstallNotifications.AddItem(notification))
		delete notification;
}


/**
 * @brief Releases a deferred install notification and sends it if warranted.
 *
 * If the notification was marked for delivery (i.e. an install event occurred
 * while deferred), the B_MIME_TYPE_CREATED notification is sent now.
 *
 * @param type The MIME type string whose deferred notification to release.
 */
void
Database::UndeferInstallNotification(const char* type)
{
	AutoLocker<BLocker> locker(fDeferredInstallNotificationsLocker);

	// check, if deferred at all
	DeferredInstallNotification* notification
		= _FindDeferredInstallNotification(type, true);

	locker.Unlock();

	if (notification == NULL)
		return;

	// notify, if requested
	if (notification->notify)
		_SendInstallNotification(notification->type);

	delete notification;
}


/**
 * @brief Sends a B_MIME_TYPE_CREATED notification to the MIME monitor service.
 *
 * @param type The MIME type that was created.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::_SendInstallNotification(const char *type)
{
	return _SendMonitorUpdate(B_MIME_TYPE_CREATED, type, B_META_MIME_MODIFIED);
}


/**
 * @brief Sends a B_MIME_TYPE_DELETED notification to the MIME monitor service.
 *
 * @param type The MIME type that was deleted.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::_SendDeleteNotification(const char *type)
{
	// Tell the backend first
	return _SendMonitorUpdate(B_MIME_TYPE_DELETED, type, B_META_MIME_MODIFIED);
}

/**
 * @brief Sends a MIME monitor update including a file-type string and icon-size flag.
 *
 * @param which     Bitmask describing which attribute changed.
 * @param type      The MIME type that was updated.
 * @param extraType Additional MIME type involved in the change.
 * @param largeIcon true if the large icon was affected, false for the small icon.
 * @param action    B_META_MIME_MODIFIED or B_META_MIME_DELETED.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::_SendMonitorUpdate(int32 which, const char *type, const char *extraType,
	bool largeIcon, int32 action)
{
	BMessage msg(B_META_MIME_CHANGED);
	status_t err;

	if (_CheckDeferredInstallNotification(which, type))
		return B_OK;

	err = msg.AddInt32("be:which", which);
	if (!err)
		err = msg.AddString("be:type", type);
	if (!err)
		err = msg.AddString("be:extra_type", extraType);
	if (!err)
		err = msg.AddBool("be:large_icon", largeIcon);
	if (!err)
		err = msg.AddInt32("be:action", action);
	if (!err)
		err = _SendMonitorUpdate(msg);
	return err;
}

/**
 * @brief Sends a MIME monitor update including a file-type string.
 *
 * @param which     Bitmask describing which attribute changed.
 * @param type      The MIME type that was updated.
 * @param extraType Additional MIME type involved in the change.
 * @param action    B_META_MIME_MODIFIED or B_META_MIME_DELETED.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::_SendMonitorUpdate(int32 which, const char *type, const char *extraType,
	int32 action)
{
	if (_CheckDeferredInstallNotification(which, type))
		return B_OK;

	BMessage msg(B_META_MIME_CHANGED);

	status_t err = msg.AddInt32("be:which", which);
	if (!err)
		err = msg.AddString("be:type", type);
	if (!err)
		err = msg.AddString("be:extra_type", extraType);
	if (!err)
		err = msg.AddInt32("be:action", action);
	if (!err)
		err = _SendMonitorUpdate(msg);
	return err;
}

/**
 * @brief Sends a MIME monitor update including an icon-size flag.
 *
 * @param which     Bitmask describing which attribute changed.
 * @param type      The MIME type that was updated.
 * @param largeIcon true if the large icon was affected, false for small icon.
 * @param action    B_META_MIME_MODIFIED or B_META_MIME_DELETED.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::_SendMonitorUpdate(int32 which, const char *type, bool largeIcon, int32 action)
{
	if (_CheckDeferredInstallNotification(which, type))
		return B_OK;

	BMessage msg(B_META_MIME_CHANGED);

	status_t err = msg.AddInt32("be:which", which);
	if (!err)
		err = msg.AddString("be:type", type);
	if (!err)
		err = msg.AddBool("be:large_icon", largeIcon);
	if (!err)
		err = msg.AddInt32("be:action", action);
	if (!err)
		err = _SendMonitorUpdate(msg);
	return err;
}

/**
 * @brief Sends a basic MIME monitor update (type and action only).
 *
 * @param which  Bitmask describing which attribute changed.
 * @param type   The MIME type that was updated.
 * @param action B_META_MIME_MODIFIED or B_META_MIME_DELETED.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Database::_SendMonitorUpdate(int32 which, const char *type, int32 action)
{
	if (_CheckDeferredInstallNotification(which, type))
		return B_OK;

	BMessage msg(B_META_MIME_CHANGED);

	status_t err = msg.AddInt32("be:which", which);
	if (!err)
		err = msg.AddString("be:type", type);
	if (!err)
		err = msg.AddInt32("be:action", action);
	if (!err)
		err = _SendMonitorUpdate(msg);
	return err;
}

/**
 * @brief Delivers a pre-built B_META_MIME_CHANGED message to all subscribers.
 *
 * @param msg A pre-populated MIME monitor BMessage.
 * @return B_OK (delivery failures to individual messengers are logged but ignored).
 */
status_t
Database::_SendMonitorUpdate(BMessage &msg)
{
	if (fNotificationListener == NULL)
		return B_OK;

	status_t err;
	std::set<BMessenger>::const_iterator i;
	for (i = fMonitorMessengers.begin(); i != fMonitorMessengers.end(); i++) {
		status_t err = fNotificationListener->Notify(&msg, *i);
		if (err) {
			DBG(OUT("Database::_SendMonitorUpdate(BMessage&): DeliverMessage failed, 0x%lx\n", err));
		}
	}
	err = B_OK;
	return err;
}


/**
 * @brief Searches the deferred-install-notification list for the given type.
 *
 * @param type   The MIME type to search for.
 * @param remove If true, remove the entry from the list when found.
 * @return Pointer to the matching DeferredInstallNotification, or NULL.
 */
Database::DeferredInstallNotification*
Database::_FindDeferredInstallNotification(const char* type, bool remove)
{
	for (int32 i = 0;
		DeferredInstallNotification* notification
			= (DeferredInstallNotification*)fDeferredInstallNotifications
				.ItemAt(i); i++) {
		if (strcmp(type, notification->type) == 0) {
			if (remove)
				fDeferredInstallNotifications.RemoveItem(i);
			return notification;
		}
	}

	return NULL;
}


/**
 * @brief Checks whether the given monitor event should be suppressed due to
 *        a pending deferred install notification.
 *
 * Handles three cases: if the type is being deleted and its install was
 * deferred, the install notification is discarded; if a create event arrives
 * while deferred, it is recorded for later delivery; if any other update
 * arrives while the install is deferred, it is suppressed.
 *
 * @param which The monitor event code.
 * @param type  The MIME type being updated.
 * @return true if the event should be suppressed, false otherwise.
 */
bool
Database::_CheckDeferredInstallNotification(int32 which, const char* type)
{
	AutoLocker<BLocker> locker(fDeferredInstallNotificationsLocker);

	// check, if deferred at all
	DeferredInstallNotification* notification
		= _FindDeferredInstallNotification(type);
	if (notification == NULL)
		return false;

	if (which == B_MIME_TYPE_DELETED) {
		// MIME type deleted -- if the install notification had been
		// deferred, we don't send anything
		if (notification->notify) {
			fDeferredInstallNotifications.RemoveItem(notification);
			delete notification;
			return true;
		}
	} else if (which == B_MIME_TYPE_CREATED) {
		// MIME type created -- defer notification
		notification->notify = true;
		return true;
	} else {
		// MIME type update -- don't send update, if deferred
		if (notification->notify)
			return true;
	}

	return false;
}


} // namespace Mime
} // namespace Storage
} // namespace BPrivate
