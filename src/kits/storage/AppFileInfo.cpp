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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file AppFileInfo.cpp
 * @brief Implementation of BAppFileInfo, providing access to application
 *        metadata stored in file attributes and resources.
 *
 * BAppFileInfo extends BNodeInfo to expose application-specific metadata
 * such as MIME type, application signature, launch flags, supported file
 * types, version information, and icons.  Data may be stored in extended
 * file-system attributes, embedded BResources, or both, depending on the
 * active info_location setting.
 *
 * @see BNodeInfo
 */


#include <new>
#include <set>
#include <stdlib.h>
#include <string>

#include <AppFileInfo.h>
#include <Bitmap.h>
#include <File.h>
#include <fs_attr.h>
#include <IconUtils.h>
#include <MimeType.h>
#include <RegistrarDefs.h>
#include <Resources.h>
#include <Roster.h>
#include <String.h>


// debugging
//#define DBG(x) x
#define DBG(x)
#define OUT	printf


// type codes
enum {
	B_APP_FLAGS_TYPE	= 'APPF',
	B_VERSION_INFO_TYPE	= 'APPV',
};


// attributes
static const char* kTypeAttribute				= "BEOS:TYPE";
static const char* kSignatureAttribute			= "BEOS:APP_SIG";
static const char* kAppFlagsAttribute			= "BEOS:APP_FLAGS";
static const char* kSupportedTypesAttribute		= "BEOS:FILE_TYPES";
static const char* kVersionInfoAttribute		= "BEOS:APP_VERSION";
static const char* kMiniIconAttribute			= "BEOS:M:";
static const char* kLargeIconAttribute			= "BEOS:L:";
static const char* kIconAttribute				= "BEOS:";
static const char* kStandardIconType			= "STD_ICON";
static const char* kIconType					= "ICON";
static const char* kCatalogEntryAttribute		= "SYS:NAME";

// resource IDs
static const int32 kTypeResourceID				= 2;
static const int32 kSignatureResourceID			= 1;
static const int32 kAppFlagsResourceID			= 1;
static const int32 kSupportedTypesResourceID	= 1;
static const int32 kMiniIconResourceID			= 101;
static const int32 kLargeIconResourceID			= 101;
static const int32 kIconResourceID				= 101;
static const int32 kVersionInfoResourceID		= 1;
static const int32 kMiniIconForTypeResourceID	= 0;
static const int32 kLargeIconForTypeResourceID	= 0;
static const int32 kIconForTypeResourceID		= 0;
static const int32 kCatalogEntryResourceID		= 1;

// R5 also exports these (Tracker is using them):
// (maybe we better want to drop them silently and declare
// the above in a public Haiku header - and use that one in
// Tracker when compiled for Haiku)
extern const uint32 MINI_ICON_TYPE, LARGE_ICON_TYPE;
const uint32 MINI_ICON_TYPE = 'MICN';
const uint32 LARGE_ICON_TYPE = 'ICON';


/**
 * @brief Default constructor. Creates an uninitialized BAppFileInfo object.
 *
 * The object must be associated with a BFile by calling SetTo() before any
 * other methods are used.
 */
BAppFileInfo::BAppFileInfo()
	:
	fResources(NULL),
	fWhere(B_USE_BOTH_LOCATIONS)
{
}


/**
 * @brief Constructs a BAppFileInfo object and associates it with the given
 *        file.
 *
 * Equivalent to default-constructing and then calling SetTo(file).
 *
 * @param file Pointer to the BFile to associate with this object.
 */
BAppFileInfo::BAppFileInfo(BFile* file)
	:
	fResources(NULL),
	fWhere(B_USE_BOTH_LOCATIONS)
{
	SetTo(file);
}


/**
 * @brief Destructor. Releases any BResources object held internally.
 */
BAppFileInfo::~BAppFileInfo()
{
	delete fResources;
}


/**
 * @brief Associates the BAppFileInfo object with a file.
 *
 * Any previously associated file is detached first.  A BResources object is
 * created for the new file; if the file has no resource fork the object falls
 * back to attribute-only mode.  The info location is adjusted accordingly.
 *
 * @param file Pointer to an open, initialised BFile.
 * @return A status code.
 * @retval B_OK The object was successfully associated with the file.
 * @retval B_BAD_VALUE \a file is \c NULL or not properly initialised.
 * @retval B_NO_MEMORY Could not allocate the internal BResources object.
 */
status_t
BAppFileInfo::SetTo(BFile* file)
{
	// unset the old file
	BNodeInfo::SetTo(NULL);
	if (fResources) {
		delete fResources;
		fResources = NULL;
	}

	// check param
	status_t error
		= file != NULL && file->InitCheck() == B_OK ? B_OK : B_BAD_VALUE;

	info_location where = B_USE_BOTH_LOCATIONS;

	// create resources
	if (error == B_OK) {
		fResources = new(std::nothrow) BResources();
		if (fResources) {
			error = fResources->SetTo(file);
			if (error != B_OK) {
				// no resources - this is no critical error, we'll just use
				// attributes only, then
				where = B_USE_ATTRIBUTES;
				error = B_OK;
			}
		} else
			error = B_NO_MEMORY;
	}

	// set node info
	if (error == B_OK)
		error = BNodeInfo::SetTo(file);

	if (error != B_OK || (where & B_USE_RESOURCES) == 0) {
		delete fResources;
		fResources = NULL;
	}

	// clean up on error
	if (error != B_OK) {
		if (InitCheck() == B_OK)
			BNodeInfo::SetTo(NULL);
	}

	// set data location
	if (error == B_OK)
		SetInfoLocation(where);

	// set error
	fCStatus = error;
	return error;
}


/**
 * @brief Retrieves the MIME type of the associated file.
 *
 * @param type A pre-allocated character buffer of at least
 *             \c B_MIME_TYPE_LENGTH bytes that will receive the
 *             null-terminated MIME type string.
 * @return A status code.
 * @retval B_OK The type was successfully read into \a type.
 * @retval B_BAD_VALUE \a type is \c NULL.
 * @retval B_NO_INIT The object is not associated with a file.
 * @retval B_ENTRY_NOT_FOUND No MIME type attribute/resource exists.
 */
status_t
BAppFileInfo::GetType(char* type) const
{
	// check param and initialization
	status_t error = type != NULL ? B_OK : B_BAD_VALUE;
	if (error == B_OK && InitCheck() != B_OK)
		error = B_NO_INIT;
	// read the data
	size_t read = 0;
	if (error == B_OK) {
		error = _ReadData(kTypeAttribute, kTypeResourceID, B_MIME_STRING_TYPE,
			type, B_MIME_TYPE_LENGTH, read);
	}
	// check the read data -- null terminate the string
	if (error == B_OK && type[read - 1] != '\0') {
		if (read == B_MIME_TYPE_LENGTH)
			error = B_ERROR;
		else
			type[read] = '\0';
	}
	return error;
}


/**
 * @brief Sets the MIME type of the associated file.
 *
 * Passing \c NULL removes any existing MIME type attribute/resource.
 *
 * @param type A null-terminated MIME type string, or \c NULL to remove.
 * @return A status code.
 * @retval B_OK The type was successfully written.
 * @retval B_BAD_VALUE \a type is longer than \c B_MIME_TYPE_LENGTH - 1.
 * @retval B_NO_INIT The object is not associated with a file.
 */
status_t
BAppFileInfo::SetType(const char* type)
{
	// check initialization
	status_t error = B_OK;
	if (InitCheck() != B_OK)
		error = B_NO_INIT;
	if (error == B_OK) {
		if (type != NULL) {
			// check param
			size_t typeLen = strlen(type);
			if (typeLen >= B_MIME_TYPE_LENGTH)
				error = B_BAD_VALUE;
			// write the data
			if (error == B_OK) {
				error = _WriteData(kTypeAttribute, kTypeResourceID,
					B_MIME_STRING_TYPE, type, typeLen + 1);
			}
		} else
			error = _RemoveData(kTypeAttribute, B_MIME_STRING_TYPE);
	}
	return error;
}


/**
 * @brief Retrieves the application signature of the associated file.
 *
 * @param signature A pre-allocated buffer of at least
 *                  \c B_MIME_TYPE_LENGTH bytes that will receive the
 *                  null-terminated application signature string.
 * @return A status code.
 * @retval B_OK The signature was successfully read into \a signature.
 * @retval B_BAD_VALUE \a signature is \c NULL.
 * @retval B_NO_INIT The object is not associated with a file.
 * @retval B_ENTRY_NOT_FOUND No signature attribute/resource exists.
 */
status_t
BAppFileInfo::GetSignature(char* signature) const
{
	// check param and initialization
	status_t error = (signature ? B_OK : B_BAD_VALUE);
	if (error == B_OK && InitCheck() != B_OK)
		error = B_NO_INIT;
	// read the data
	size_t read = 0;
	if (error == B_OK) {
		error = _ReadData(kSignatureAttribute, kSignatureResourceID,
			B_MIME_STRING_TYPE, signature, B_MIME_TYPE_LENGTH, read);
	}
	// check the read data -- null terminate the string
	if (error == B_OK && signature[read - 1] != '\0') {
		if (read == B_MIME_TYPE_LENGTH)
			error = B_ERROR;
		else
			signature[read] = '\0';
	}
	return error;
}


/**
 * @brief Sets the application signature of the associated file.
 *
 * Passing \c NULL removes any existing signature attribute/resource.
 *
 * @param signature A null-terminated MIME-format application signature
 *                  string, or \c NULL to remove.
 * @return A status code.
 * @retval B_OK The signature was successfully written.
 * @retval B_BAD_VALUE \a signature is longer than \c B_MIME_TYPE_LENGTH - 1.
 * @retval B_NO_INIT The object is not associated with a file.
 */
status_t
BAppFileInfo::SetSignature(const char* signature)
{
	// check initialization
	status_t error = B_OK;
	if (InitCheck() != B_OK)
		error = B_NO_INIT;
	if (error == B_OK) {
		if (signature) {
			// check param
			size_t signatureLen = strlen(signature);
			if (signatureLen >= B_MIME_TYPE_LENGTH)
				error = B_BAD_VALUE;
			// write the data
			if (error == B_OK) {
				error = _WriteData(kSignatureAttribute, kSignatureResourceID,
					B_MIME_STRING_TYPE, signature, signatureLen + 1);
			}
		} else
			error = _RemoveData(kSignatureAttribute, B_MIME_STRING_TYPE);
	}
	return error;
}


/**
 * @brief Retrieves the catalog entry (localisation key) stored in the file.
 *
 * The catalog entry is a string of the form "appName:context:string" used
 * by the locale kit to identify the application's human-readable name.
 *
 * @param catalogEntry A pre-allocated buffer of at least
 *                     \c B_MIME_TYPE_LENGTH * 3 bytes.
 * @return A status code.
 * @retval B_OK The catalog entry was successfully read.
 * @retval B_BAD_VALUE \a catalogEntry is \c NULL or the stored value is too
 *         long.
 * @retval B_NO_INIT The object is not associated with a file.
 */
status_t
BAppFileInfo::GetCatalogEntry(char* catalogEntry) const
{
	if (catalogEntry == NULL)
		return B_BAD_VALUE;

	if (InitCheck() != B_OK)
		return B_NO_INIT;

	size_t read = 0;
	status_t error = _ReadData(kCatalogEntryAttribute, kCatalogEntryResourceID,
		B_STRING_TYPE, catalogEntry, B_MIME_TYPE_LENGTH * 3, read);

	if (error != B_OK)
		return error;

	if (read >= B_MIME_TYPE_LENGTH * 3)
		return B_ERROR;

	catalogEntry[read] = '\0';

	return B_OK;
}


/**
 * @brief Sets the catalog entry (localisation key) stored in the file.
 *
 * Passing \c NULL removes any existing catalog entry attribute/resource.
 *
 * @param catalogEntry A null-terminated string of the form
 *                     "appName:context:string", or \c NULL to remove.
 * @return A status code.
 * @retval B_OK The catalog entry was successfully written.
 * @retval B_BAD_VALUE \a catalogEntry exceeds \c B_MIME_TYPE_LENGTH * 3 bytes.
 * @retval B_NO_INIT The object is not associated with a file.
 */
status_t
BAppFileInfo::SetCatalogEntry(const char* catalogEntry)
{
	if (InitCheck() != B_OK)
		return B_NO_INIT;

	if (catalogEntry == NULL)
		return _RemoveData(kCatalogEntryAttribute, B_STRING_TYPE);

	size_t nameLength = strlen(catalogEntry);
	if (nameLength > B_MIME_TYPE_LENGTH * 3)
		return B_BAD_VALUE;

	return _WriteData(kCatalogEntryAttribute, kCatalogEntryResourceID,
		B_STRING_TYPE, catalogEntry, nameLength + 1);
}


/**
 * @brief Retrieves the application launch flags from the associated file.
 *
 * @param flags Pointer to a \c uint32 that will receive the flags value.
 * @return A status code.
 * @retval B_OK The flags were successfully read into \a flags.
 * @retval B_BAD_VALUE \a flags is \c NULL or the stored data is malformed.
 * @retval B_NO_INIT The object is not associated with a file.
 */
status_t
BAppFileInfo::GetAppFlags(uint32* flags) const
{
	// check param and initialization
	status_t error = flags != NULL ? B_OK : B_BAD_VALUE;
	if (error == B_OK && InitCheck() != B_OK)
		error = B_NO_INIT;
	// read the data
	size_t read = 0;
	if (error == B_OK) {
		error = _ReadData(kAppFlagsAttribute, kAppFlagsResourceID,
			B_APP_FLAGS_TYPE, flags, sizeof(uint32), read);
	}
	// check the read data
	if (error == B_OK && read != sizeof(uint32))
		error = B_ERROR;
	return error;
}


/**
 * @brief Sets the application launch flags on the associated file.
 *
 * @param flags The application flags to store (e.g. \c B_SINGLE_LAUNCH,
 *              \c B_MULTIPLE_LAUNCH, \c B_EXCLUSIVE_LAUNCH).
 * @return A status code.
 * @retval B_OK The flags were successfully written.
 * @retval B_NO_INIT The object is not associated with a file.
 */
status_t
BAppFileInfo::SetAppFlags(uint32 flags)
{
	// check initialization
	status_t error = B_OK;
	if (InitCheck() != B_OK)
		error = B_NO_INIT;
	if (error == B_OK) {
		// write the data
		error = _WriteData(kAppFlagsAttribute, kAppFlagsResourceID,
			B_APP_FLAGS_TYPE, &flags, sizeof(uint32));
	}
	return error;
}


/**
 * @brief Removes the application flags attribute/resource from the
 *        associated file.
 *
 * @return A status code.
 * @retval B_OK The flags were successfully removed (or did not exist).
 * @retval B_NO_INIT The object is not associated with a file.
 */
status_t
BAppFileInfo::RemoveAppFlags()
{
	// check initialization
	status_t error = B_OK;
	if (InitCheck() != B_OK)
		error = B_NO_INIT;
	if (error == B_OK) {
		// remove the data
		error = _RemoveData(kAppFlagsAttribute, B_APP_FLAGS_TYPE);
	}
	return error;
}


/**
 * @brief Retrieves the list of MIME types supported by this application.
 *
 * The supported types are stored as a flattened BMessage containing a
 * string array field named "types".
 *
 * @param types Pointer to a BMessage that will be populated with the
 *              supported type strings.
 * @return A status code.
 * @retval B_OK The supported types were successfully read into \a types.
 * @retval B_BAD_VALUE \a types is \c NULL.
 * @retval B_NO_INIT The object is not associated with a file.
 * @retval B_ENTRY_NOT_FOUND No supported-types attribute/resource exists.
 */
status_t
BAppFileInfo::GetSupportedTypes(BMessage* types) const
{
	// check param and initialization
	status_t error = types != NULL ? B_OK : B_BAD_VALUE;
	if (error == B_OK && InitCheck() != B_OK)
		error = B_NO_INIT;
	// read the data
	size_t read = 0;
	void* buffer = NULL;
	if (error == B_OK) {
		error = _ReadData(kSupportedTypesAttribute, kSupportedTypesResourceID,
			B_MESSAGE_TYPE, NULL, 0, read, &buffer);
	}
	// unflatten the buffer
	if (error == B_OK)
		error = types->Unflatten((const char*)buffer);
	// clean up
	free(buffer);
	return error;
}


/**
 * @brief Sets the list of MIME types supported by this application, with
 *        full control over MIME database synchronisation.
 *
 * @param types A BMessage whose "types" string array lists the supported
 *              MIME types, or \c NULL to remove the supported-types data.
 * @param updateMimeDB If \c true, the application's MIME database entry is
 *                     updated to reflect the new supported types.
 * @param syncAll If \c true, the MIME database sync removes types that are
 *                no longer listed; if \c false it only adds new ones.
 * @return A status code.
 * @retval B_OK The supported types were successfully written.
 * @retval B_BAD_VALUE One or more type strings in \a types are not valid
 *         MIME strings.
 * @retval B_NO_INIT The object is not associated with a file.
 * @retval B_NO_MEMORY Memory allocation failure.
 */
status_t
BAppFileInfo::SetSupportedTypes(const BMessage* types, bool updateMimeDB,
	bool syncAll)
{
	// check initialization
	status_t error = B_OK;
	if (InitCheck() != B_OK)
		error = B_NO_INIT;

	BMimeType mimeType;
	if (error == B_OK)
		error = GetMetaMime(&mimeType);

	if (error == B_OK || error == B_ENTRY_NOT_FOUND) {
		error = B_OK;
		if (types) {
			// check param -- supported types must be valid
			const char* type;
			for (int32 i = 0;
				 error == B_OK && types->FindString("types", i, &type) == B_OK;
				 i++) {
				if (!BMimeType::IsValid(type))
					error = B_BAD_VALUE;
			}

			// get flattened size
			ssize_t size = 0;
			if (error == B_OK) {
				size = types->FlattenedSize();
				if (size < 0)
					error = size;
			}

			// allocate a buffer for the flattened data
			char* buffer = NULL;
			if (error == B_OK) {
				buffer = new(std::nothrow) char[size];
				if (!buffer)
					error = B_NO_MEMORY;
			}

			// flatten the message
			if (error == B_OK)
				error = types->Flatten(buffer, size);

			// write the data
			if (error == B_OK) {
				error = _WriteData(kSupportedTypesAttribute,
					kSupportedTypesResourceID, B_MESSAGE_TYPE, buffer, size);
			}

			delete[] buffer;
		} else
			error = _RemoveData(kSupportedTypesAttribute, B_MESSAGE_TYPE);

		// update the MIME database, if the app signature is installed
		if (updateMimeDB && error == B_OK && mimeType.IsInstalled())
			error = mimeType.SetSupportedTypes(types, syncAll);
	}
	return error;
}


/**
 * @brief Sets the list of MIME types supported by this application, updating
 *        the MIME database.
 *
 * Delegates to SetSupportedTypes(types, true, syncAll).
 *
 * @param types A BMessage whose "types" string array lists the supported
 *              MIME types, or \c NULL to remove.
 * @param syncAll If \c true, stale entries are removed from the MIME
 *                database.
 * @return A status code as described in
 *         SetSupportedTypes(const BMessage*, bool, bool).
 */
status_t
BAppFileInfo::SetSupportedTypes(const BMessage* types, bool syncAll)
{
	return SetSupportedTypes(types, true, syncAll);
}


/**
 * @brief Sets the list of MIME types supported by this application.
 *
 * Delegates to SetSupportedTypes(types, true, false).
 *
 * @param types A BMessage whose "types" string array lists the supported
 *              MIME types, or \c NULL to remove.
 * @return A status code as described in
 *         SetSupportedTypes(const BMessage*, bool, bool).
 */
status_t
BAppFileInfo::SetSupportedTypes(const BMessage* types)
{
	return SetSupportedTypes(types, true, false);
}


/**
 * @brief Tests whether a given MIME type is in the application's supported
 *        types list.
 *
 * The check honours MIME super-type containment: a supported type of
 * "application/octet-stream" matches any queried type.
 *
 * @param type A null-terminated MIME type string to look up.
 * @return \c true if \a type is supported, \c false otherwise (including on
 *         error).
 */
bool
BAppFileInfo::IsSupportedType(const char* type) const
{
	status_t error = type != NULL ? B_OK : B_BAD_VALUE;
	// get the supported types
	BMessage types;
	if (error == B_OK)
		error = GetSupportedTypes(&types);
	// turn type into a BMimeType
	BMimeType mimeType;
	if (error == B_OK)
		error = mimeType.SetTo(type);
	// iterate through the supported types
	bool found = false;
	if (error == B_OK) {
		const char* supportedType;
		for (int32 i = 0;
			 !found && types.FindString("types", i, &supportedType) == B_OK;
			 i++) {
			found = strcmp(supportedType, "application/octet-stream") == 0
				|| BMimeType(supportedType).Contains(&mimeType);
		}
	}
	return found;
}


/**
 * @brief Tests whether a given BMimeType is supported by this application.
 *
 * Uses BMimeType::Contains() for each supported type, so super-type matching
 * applies.
 *
 * @param type Pointer to an initialised BMimeType to look up.
 * @return \c true if \a type is supported, \c false otherwise (including on
 *         error or if \a type is \c NULL / uninitialised).
 */
bool
BAppFileInfo::Supports(BMimeType* type) const
{
	status_t error
		= type != NULL && type->InitCheck() == B_OK ? B_OK : B_BAD_VALUE;
	// get the supported types
	BMessage types;
	if (error == B_OK)
		error = GetSupportedTypes(&types);
	// iterate through the supported types
	bool found = false;
	if (error == B_OK) {
		const char* supportedType;
		for (int32 i = 0;
			 !found && types.FindString("types", i, &supportedType) == B_OK;
			 i++) {
			found = BMimeType(supportedType).Contains(type);
		}
	}
	return found;
}


/**
 * @brief Retrieves the application icon as a BBitmap.
 *
 * Delegates to GetIconForType(NULL, icon, which).
 *
 * @param icon A pre-allocated BBitmap that will receive the icon data.
 * @param which The desired icon size (\c B_MINI_ICON or \c B_LARGE_ICON).
 * @return A status code as described in GetIconForType().
 */
status_t
BAppFileInfo::GetIcon(BBitmap* icon, icon_size which) const
{
	return GetIconForType(NULL, icon, which);
}


/**
 * @brief Retrieves the application vector icon as a raw byte buffer.
 *
 * Delegates to GetIconForType(NULL, data, size).
 *
 * @param data Pointer to a \c uint8* that will be set to a newly allocated
 *             buffer containing the raw icon data.  The caller is
 *             responsible for freeing this buffer.
 * @param size Pointer to a \c size_t that will receive the buffer size.
 * @return A status code as described in GetIconForType().
 */
status_t
BAppFileInfo::GetIcon(uint8** data, size_t* size) const
{
	return GetIconForType(NULL, data, size);
}


/**
 * @brief Sets the application icon from a BBitmap, with optional MIME
 *        database update.
 *
 * Delegates to SetIconForType(NULL, icon, which, updateMimeDB).
 *
 * @param icon The BBitmap to use as the icon, or \c NULL to remove.
 * @param which The icon size (\c B_MINI_ICON or \c B_LARGE_ICON).
 * @param updateMimeDB If \c true the MIME database entry is updated.
 * @return A status code as described in SetIconForType().
 */
status_t
BAppFileInfo::SetIcon(const BBitmap* icon, icon_size which, bool updateMimeDB)
{
	return SetIconForType(NULL, icon, which, updateMimeDB);
}


/**
 * @brief Sets the application icon from a BBitmap, updating the MIME
 *        database.
 *
 * Delegates to SetIconForType(NULL, icon, which, true).
 *
 * @param icon The BBitmap to use as the icon, or \c NULL to remove.
 * @param which The icon size (\c B_MINI_ICON or \c B_LARGE_ICON).
 * @return A status code as described in SetIconForType().
 */
status_t
BAppFileInfo::SetIcon(const BBitmap* icon, icon_size which)
{
	return SetIconForType(NULL, icon, which, true);
}


/**
 * @brief Sets the application vector icon from a raw byte buffer, with
 *        optional MIME database update.
 *
 * Delegates to SetIconForType(NULL, data, size, updateMimeDB).
 *
 * @param data Pointer to the raw vector icon data.
 * @param size Size in bytes of the icon data.
 * @param updateMimeDB If \c true the MIME database entry is updated.
 * @return A status code as described in SetIconForType().
 */
status_t
BAppFileInfo::SetIcon(const uint8* data, size_t size, bool updateMimeDB)
{
	return SetIconForType(NULL, data, size, updateMimeDB);
}


/**
 * @brief Sets the application vector icon from a raw byte buffer, updating
 *        the MIME database.
 *
 * Delegates to SetIconForType(NULL, data, size, true).
 *
 * @param data Pointer to the raw vector icon data.
 * @param size Size in bytes of the icon data.
 * @return A status code as described in SetIconForType().
 */
status_t
BAppFileInfo::SetIcon(const uint8* data, size_t size)
{
	return SetIconForType(NULL, data, size, true);
}


/**
 * @brief Retrieves version information from the associated file.
 *
 * Two version_info records may be stored: one for the application version
 * (index 0) and one for the system version (index 1).  If only one record
 * is present and \a kind is \c B_SYSTEM_VERSION_KIND, a zeroed record is
 * returned.
 *
 * @param info Pointer to a \c version_info structure that will receive the
 *             version data.
 * @param kind Selects which version to read: \c B_APP_VERSION_KIND or
 *             \c B_SYSTEM_VERSION_KIND.
 * @return A status code.
 * @retval B_OK The version info was successfully read.
 * @retval B_BAD_VALUE \a info is \c NULL or \a kind is invalid.
 * @retval B_NO_INIT The object is not associated with a file.
 */
status_t
BAppFileInfo::GetVersionInfo(version_info* info, version_kind kind) const
{
	// check params and initialization
	if (info == NULL)
		return B_BAD_VALUE;

	int32 index = 0;
	switch (kind) {
		case B_APP_VERSION_KIND:
			index = 0;
			break;
		case B_SYSTEM_VERSION_KIND:
			index = 1;
			break;
		default:
			return B_BAD_VALUE;
	}

	if (InitCheck() != B_OK)
		return B_NO_INIT;

	// read the data
	size_t read = 0;
	version_info infos[2];
	status_t error = _ReadData(kVersionInfoAttribute, kVersionInfoResourceID,
		B_VERSION_INFO_TYPE, infos, 2 * sizeof(version_info), read);
	if (error != B_OK)
		return error;

	// check the read data
	if (read == sizeof(version_info)) {
		// only the app version info is there -- return a cleared system info
		if (index == 0)
			*info = infos[index];
		else if (index == 1)
			memset(info, 0, sizeof(version_info));
	} else if (read == 2 * sizeof(version_info)) {
		*info = infos[index];
	} else
		return B_ERROR;

	// return result
	return B_OK;
}


/**
 * @brief Writes version information to the associated file.
 *
 * Both application and system version records are stored together in one
 * attribute/resource.  If only the other record has been written before,
 * it is preserved; if neither has been written, the missing record is
 * stored as zeroes.  Passing \c NULL for \a info removes the entire
 * version-info attribute/resource.
 *
 * @param info Pointer to the \c version_info to store, or \c NULL to remove.
 * @param kind Selects which version slot to write: \c B_APP_VERSION_KIND or
 *             \c B_SYSTEM_VERSION_KIND.
 * @return A status code.
 * @retval B_OK The version info was successfully written.
 * @retval B_BAD_VALUE \a kind is not a valid \c version_kind value.
 * @retval B_NO_INIT The object is not associated with a file.
 */
status_t
BAppFileInfo::SetVersionInfo(const version_info* info, version_kind kind)
{
	// check initialization
	status_t error = B_OK;
	if (InitCheck() != B_OK)
		error = B_NO_INIT;
	if (error == B_OK) {
		if (info != NULL) {
			// check param
			int32 index = 0;
			if (error == B_OK) {
				switch (kind) {
					case B_APP_VERSION_KIND:
						index = 0;
						break;
					case B_SYSTEM_VERSION_KIND:
						index = 1;
						break;
					default:
						error = B_BAD_VALUE;
						break;
				}
			}
			// read both infos
			version_info infos[2];
			if (error == B_OK) {
				size_t read;
				if (_ReadData(kVersionInfoAttribute, kVersionInfoResourceID,
						B_VERSION_INFO_TYPE, infos, 2 * sizeof(version_info),
						read) == B_OK) {
					// clear the part that hasn't been read
					if (read < sizeof(infos))
						memset((char*)infos + read, 0, sizeof(infos) - read);
				} else {
					// failed to read -- clear
					memset(infos, 0, sizeof(infos));
				}
			}
			infos[index] = *info;
			// write the data
			if (error == B_OK) {
				error = _WriteData(kVersionInfoAttribute,
					kVersionInfoResourceID, B_VERSION_INFO_TYPE, infos,
					2 * sizeof(version_info));
			}
		} else
			error = _RemoveData(kVersionInfoAttribute, B_VERSION_INFO_TYPE);
	}
	return error;
}


/**
 * @brief Retrieves the icon associated with a specific MIME type supported
 *        by this application, rendering it into a BBitmap.
 *
 * Vector icons are preferred; if none is found the method falls back to
 * the legacy B_CMAP8 mini or large icon.  Passing \c NULL for \a type
 * retrieves the application's own icon.
 *
 * @param type The MIME type whose icon should be retrieved, or \c NULL for
 *             the application icon.
 * @param icon A pre-allocated BBitmap that will receive the icon.
 * @param size The desired icon size (\c B_MINI_ICON or \c B_LARGE_ICON).
 * @return A status code.
 * @retval B_OK The icon was successfully read.
 * @retval B_BAD_VALUE \a icon is \c NULL / uninitialised, \a type is not a
 *         valid MIME string, or size/color-space constraints are violated.
 * @retval B_NO_INIT The object is not associated with a file.
 * @retval B_ENTRY_NOT_FOUND No icon data exists.
 */
status_t
BAppFileInfo::GetIconForType(const char* type, BBitmap* icon, icon_size size)
	const
{
	if (InitCheck() != B_OK)
		return B_NO_INIT;

	if (icon == NULL || icon->InitCheck() != B_OK)
		return B_BAD_VALUE;

	// TODO: for consistency with attribute based icon reading, we
	// could also prefer B_CMAP8 icons here if the provided bitmap
	// is in that format. Right now, an existing B_CMAP8 icon resource
	// would be ignored as soon as a vector icon is present. On the other
	// hand, maybe this still results in a more consistent user interface,
	// since Tracker/Deskbar would surely show the vector icon.

	// try vector icon first
	BString vectorAttributeName(kIconAttribute);

	// check type param
	if (type != NULL) {
		if (BMimeType::IsValid(type))
			vectorAttributeName += type;
		else
			return B_BAD_VALUE;
	} else {
		vectorAttributeName += kIconType;
	}
	const char* attribute = vectorAttributeName.String();

	size_t bytesRead;
	void* allocatedBuffer;
	status_t error = _ReadData(attribute, -1, B_VECTOR_ICON_TYPE, NULL, 0,
		bytesRead, &allocatedBuffer);
	if (error == B_OK) {
		error = BIconUtils::GetVectorIcon((uint8*)allocatedBuffer,
										  bytesRead, icon);
		free(allocatedBuffer);
		return error;
	}

	// no vector icon if we got this far,
	// align size argument just in case
	if (size < B_LARGE_ICON)
		size = B_MINI_ICON;
	else
		size = B_LARGE_ICON;

	error = B_OK;
	// set some icon size related variables
	BString attributeString;
	BRect bounds;
	uint32 attrType = 0;
	size_t attrSize = 0;
	switch (size) {
		case B_MINI_ICON:
			attributeString = kMiniIconAttribute;
			bounds.Set(0, 0, 15, 15);
			attrType = B_MINI_ICON_TYPE;
			attrSize = 16 * 16;
			break;
		case B_LARGE_ICON:
			attributeString = kLargeIconAttribute;
			bounds.Set(0, 0, 31, 31);
			attrType = B_LARGE_ICON_TYPE;
			attrSize = 32 * 32;
			break;
		default:
			return B_BAD_VALUE;
	}

	// compose attribute name
	attributeString += type != NULL ? type : kStandardIconType;
	attribute = attributeString.String();

	// check parameters
	// currently, scaling B_CMAP8 icons is not supported
	if (icon->ColorSpace() == B_CMAP8 && icon->Bounds() != bounds)
		return B_BAD_VALUE;

	// read the data
	if (error == B_OK) {
		bool tempBuffer
			= icon->ColorSpace() != B_CMAP8 || icon->Bounds() != bounds;
		uint8* buffer = NULL;
		size_t read;
		if (tempBuffer) {
			// other color space or bitmap size than stored in attribute
			buffer = new(std::nothrow) uint8[attrSize];
			if (!buffer) {
				error = B_NO_MEMORY;
			} else {
				error = _ReadData(attribute, -1, attrType, buffer, attrSize,
					read);
			}
		} else {
			error = _ReadData(attribute, -1, attrType, icon->Bits(), attrSize,
				read);
		}
		if (error == B_OK && read != attrSize)
			error = B_ERROR;
		if (tempBuffer) {
			// other color space than stored in attribute
			if (error == B_OK) {
				error = BIconUtils::ConvertFromCMAP8(buffer, (uint32)size,
					(uint32)size, (uint32)size, icon);
			}
			delete[] buffer;
		}
	}
	return error;
}


/**
 * @brief Retrieves the vector icon associated with a specific MIME type as
 *        a raw byte buffer.
 *
 * Passing \c NULL for \a type retrieves the application's own vector icon.
 *
 * @param type The MIME type whose icon should be retrieved, or \c NULL for
 *             the application icon.
 * @param data Pointer to a \c uint8* that will be set to a newly allocated
 *             buffer.  The caller is responsible for freeing this buffer.
 * @param size Pointer to a \c size_t that will receive the buffer size.
 * @return A status code.
 * @retval B_OK The icon data was successfully read.
 * @retval B_BAD_VALUE \a data or \a size is \c NULL, or \a type is not a
 *         valid MIME string.
 * @retval B_NO_INIT The object is not associated with a file.
 */
status_t
BAppFileInfo::GetIconForType(const char* type, uint8** data, size_t* size) const
{
	if (InitCheck() != B_OK)
		return B_NO_INIT;

	if (data == NULL || size == NULL)
		return B_BAD_VALUE;

	// get vector icon
	BString attributeName(kIconAttribute);

	// check type param
	if (type != NULL) {
		if (BMimeType::IsValid(type))
			attributeName += type;
		else
			return B_BAD_VALUE;
	} else
		attributeName += kIconType;

	void* allocatedBuffer = NULL;
	status_t ret = _ReadData(attributeName.String(), -1, B_VECTOR_ICON_TYPE,
		NULL, 0, *size, &allocatedBuffer);

	if (ret < B_OK)
		return ret;

	*data = (uint8*)allocatedBuffer;
	return B_OK;
}


/**
 * @brief Sets the legacy (B_CMAP8) icon for a given MIME type, with optional
 *        MIME database update.
 *
 * If \a icon is \c NULL the icon attribute/resource for \a type is removed.
 * Passing \c NULL for \a type sets the application's own icon.  The bitmap
 * is automatically converted to \c B_CMAP8 if it uses a different colour
 * space.
 *
 * @param type The MIME type to associate the icon with, or \c NULL for the
 *             application icon.
 * @param icon The BBitmap to store, or \c NULL to remove.
 * @param which The icon size (\c B_MINI_ICON or \c B_LARGE_ICON).
 * @param updateMimeDB If \c true the MIME database entry is updated.
 * @return A status code.
 * @retval B_OK The icon was successfully written.
 * @retval B_BAD_VALUE Invalid \a which value, invalid \a type string, or
 *         \a icon has wrong bounds.
 * @retval B_NO_INIT The object is not associated with a file.
 */
status_t
BAppFileInfo::SetIconForType(const char* type, const BBitmap* icon,
	icon_size which, bool updateMimeDB)
{
	status_t error = B_OK;

	// set some icon size related variables
	BString attributeString;
	BRect bounds;
	uint32 attrType = 0;
	size_t attrSize = 0;
	int32 resourceID = 0;
	switch (which) {
		case B_MINI_ICON:
			attributeString = kMiniIconAttribute;
			bounds.Set(0, 0, 15, 15);
			attrType = B_MINI_ICON_TYPE;
			attrSize = 16 * 16;
			resourceID = type != NULL
				? kMiniIconForTypeResourceID : kMiniIconResourceID;
			break;
		case B_LARGE_ICON:
			attributeString = kLargeIconAttribute;
			bounds.Set(0, 0, 31, 31);
			attrType = B_LARGE_ICON_TYPE;
			attrSize = 32 * 32;
			resourceID = type != NULL
				? kLargeIconForTypeResourceID : kLargeIconResourceID;
			break;
		default:
			error = B_BAD_VALUE;
			break;
	}

	// check type param
	if (error == B_OK) {
		if (type != NULL) {
			if (BMimeType::IsValid(type))
				attributeString += type;
			else
				error = B_BAD_VALUE;
		} else
			attributeString += kStandardIconType;
	}
	const char* attribute = attributeString.String();

	// check parameter and initialization
	if (error == B_OK && icon != NULL
		&& (icon->InitCheck() != B_OK || icon->Bounds() != bounds)) {
		error = B_BAD_VALUE;
	}
	if (error == B_OK && InitCheck() != B_OK)
		error = B_NO_INIT;

	// write/remove the attribute
	if (error == B_OK) {
		if (icon != NULL) {
			bool otherColorSpace = (icon->ColorSpace() != B_CMAP8);
			if (otherColorSpace) {
				BBitmap bitmap(bounds, B_BITMAP_NO_SERVER_LINK, B_CMAP8);
				error = bitmap.InitCheck();
				if (error == B_OK)
					error = bitmap.ImportBits(icon);
				if (error == B_OK) {
					error = _WriteData(attribute, resourceID, attrType,
						bitmap.Bits(), attrSize, true);
				}
			} else {
				error = _WriteData(attribute, resourceID, attrType,
					icon->Bits(), attrSize, true);
			}
		} else	// no icon given => remove
			error = _RemoveData(attribute, attrType);
	}

	// set the attribute on the MIME type, if the file has a signature
	BMimeType mimeType;
	if (updateMimeDB && error == B_OK && GetMetaMime(&mimeType) == B_OK) {
		if (!mimeType.IsInstalled())
			error = mimeType.Install();
		if (error == B_OK)
			error = mimeType.SetIconForType(type, icon, which);
	}
	return error;
}


/**
 * @brief Sets the legacy (B_CMAP8) icon for a given MIME type, updating the
 *        MIME database.
 *
 * Delegates to SetIconForType(type, icon, which, true).
 *
 * @param type The MIME type to associate the icon with, or \c NULL.
 * @param icon The BBitmap to store, or \c NULL to remove.
 * @param which The icon size (\c B_MINI_ICON or \c B_LARGE_ICON).
 * @return A status code as described in
 *         SetIconForType(const char*, const BBitmap*, icon_size, bool).
 */
status_t
BAppFileInfo::SetIconForType(const char* type, const BBitmap* icon,
	icon_size which)
{
	return SetIconForType(type, icon, which, true);
}


/**
 * @brief Sets the vector icon for a given MIME type from a raw byte buffer,
 *        with optional MIME database update.
 *
 * If \a data is \c NULL the vector icon attribute/resource for \a type is
 * removed.  Passing \c NULL for \a type sets the application's own vector
 * icon.
 *
 * @param type The MIME type to associate the icon with, or \c NULL for the
 *             application icon.
 * @param data Pointer to the raw vector icon data, or \c NULL to remove.
 * @param size Size in bytes of the icon data.
 * @param updateMimeDB If \c true the MIME database entry is updated.
 * @return A status code.
 * @retval B_OK The icon was successfully written.
 * @retval B_BAD_VALUE \a type is not a valid MIME string.
 * @retval B_NO_INIT The object is not associated with a file.
 */
status_t
BAppFileInfo::SetIconForType(const char* type, const uint8* data, size_t size,
	bool updateMimeDB)
{
	if (InitCheck() != B_OK)
		return B_NO_INIT;

	// set some icon related variables
	BString attributeString = kIconAttribute;
	int32 resourceID = type ? kIconForTypeResourceID : kIconResourceID;
	uint32 attrType = B_VECTOR_ICON_TYPE;

	// check type param
	if (type != NULL) {
		if (BMimeType::IsValid(type))
			attributeString += type;
		else
			return B_BAD_VALUE;
	} else
		attributeString += kIconType;

	const char* attribute = attributeString.String();

	status_t error;
	// write/remove the attribute
	if (data != NULL)
		error = _WriteData(attribute, resourceID, attrType, data, size, true);
	else	// no icon given => remove
		error = _RemoveData(attribute, attrType);

	// set the attribute on the MIME type, if the file has a signature
	BMimeType mimeType;
	if (updateMimeDB && error == B_OK && GetMetaMime(&mimeType) == B_OK) {
		if (!mimeType.IsInstalled())
			error = mimeType.Install();
		if (error == B_OK)
			error = mimeType.SetIconForType(type, data, size);
	}
	return error;
}


/**
 * @brief Sets the vector icon for a given MIME type from a raw byte buffer,
 *        updating the MIME database.
 *
 * Delegates to SetIconForType(type, data, size, true).
 *
 * @param type The MIME type to associate the icon with, or \c NULL.
 * @param data Pointer to the raw vector icon data, or \c NULL to remove.
 * @param size Size in bytes of the icon data.
 * @return A status code as described in
 *         SetIconForType(const char*, const uint8*, size_t, bool).
 */
status_t
BAppFileInfo::SetIconForType(const char* type, const uint8* data, size_t size)
{
	return SetIconForType(type, data, size, true);
}


/**
 * @brief Specifies where application metadata is read from and written to.
 *
 * If the internal BResources object was not initialised (i.e. the file has
 * no resource fork), any \c B_USE_RESOURCES bit in \a location is
 * automatically cleared, ensuring the object operates in attribute-only
 * mode.
 *
 * @param location A bitmask of \c info_location values
 *                 (\c B_USE_ATTRIBUTES, \c B_USE_RESOURCES, or
 *                 \c B_USE_BOTH_LOCATIONS).
 */
void
BAppFileInfo::SetInfoLocation(info_location location)
{
	// if the resources failed to initialize, we must not use them
	if (fResources == NULL)
		location = info_location(location & ~B_USE_RESOURCES);

	fWhere = location;
}

/**
 * @brief Returns whether extended file-system attributes are currently used
 *        for reading and writing metadata.
 *
 * @return \c true if the \c B_USE_ATTRIBUTES flag is set in the current
 *         info location.
 */
bool
BAppFileInfo::IsUsingAttributes() const
{
	return (fWhere & B_USE_ATTRIBUTES) != 0;
}


/**
 * @brief Returns whether embedded file resources are currently used for
 *        reading and writing metadata.
 *
 * @return \c true if the \c B_USE_RESOURCES flag is set in the current
 *         info location.
 */
bool
BAppFileInfo::IsUsingResources() const
{
	return (fWhere & B_USE_RESOURCES) != 0;
}


// FBC
void BAppFileInfo::_ReservedAppFileInfo1() {}
void BAppFileInfo::_ReservedAppFileInfo2() {}
void BAppFileInfo::_ReservedAppFileInfo3() {}


#ifdef __HAIKU_BEOS_COMPATIBLE
/**
 * @brief Privatized assignment operator to prevent usage.
 *
 * @return A reference to \c *this (never actually callable externally).
 */
BAppFileInfo&
BAppFileInfo::operator=(const BAppFileInfo&)
{
	return *this;
}


/**
 * @brief Privatized copy constructor to prevent usage.
 */
BAppFileInfo::BAppFileInfo(const BAppFileInfo&)
{
}
#endif


/**
 * @brief Initialises a BMimeType to the application signature of the
 *        associated file.
 *
 * @warning The parameter \a meta is not checked for \c NULL.
 *
 * @param meta A pointer to a pre-allocated BMimeType that will be
 *             initialised to the file's application signature.
 * @return A status code.
 * @retval B_OK Everything went fine.
 * @retval B_BAD_VALUE \c NULL \a meta or the stored signature is not a valid
 *         MIME string.
 * @retval B_ENTRY_NOT_FOUND The file has no signature, or the signature is
 *         not installed in the MIME database.
 */
status_t
BAppFileInfo::GetMetaMime(BMimeType* meta) const
{
	char signature[B_MIME_TYPE_LENGTH];
	status_t error = GetSignature(signature);
	if (error == B_OK)
		error = meta->SetTo(signature);
	else if (error == B_BAD_VALUE)
		error = B_ENTRY_NOT_FOUND;
	if (error == B_OK && !meta->IsValid())
		error = B_BAD_VALUE;
	return error;
}


/**
 * @brief Reads data from an attribute or resource according to the current
 *        info location.
 *
 * @note Data is read from the location(s) specified by \a fWhere.
 *       Attributes are tried first; resources are used as a fallback.
 *
 * @warning The object must be properly initialised.  Parameters are
 *          \b NOT checked.
 *
 * @param name         Name of the attribute/resource to read.
 * @param id           Resource ID; ignored when negative.
 * @param type         Expected type code of the attribute/resource.
 * @param buffer       Pre-allocated destination buffer; ignored when
 *                     \a allocatedBuffer is non-\c NULL.
 * @param bufferSize   Size of the pre-allocated buffer.
 * @param bytesRead    Set to the number of bytes actually read on success.
 * @param allocatedBuffer If non-\c NULL, the method allocates a buffer large
 *                     enough to hold all data and stores a pointer to it
 *                     here; the caller must free it.
 * @return A status code.
 * @retval B_OK Everything went fine.
 * @retval B_ENTRY_NOT_FOUND The attribute/resource was not found.
 * @retval B_NO_MEMORY Buffer allocation failed.
 * @retval B_BAD_VALUE The stored type did not match \a type, or the stored
 *         data was larger than the provided buffer.
 */
status_t
BAppFileInfo::_ReadData(const char* name, int32 id, type_code type,
	void* buffer, size_t bufferSize, size_t& bytesRead, void** allocatedBuffer)
	const
{
	status_t error = B_OK;

	if (allocatedBuffer)
		buffer = NULL;

	bool foundData = false;

	if (IsUsingAttributes()) {
		// get an attribute info
		attr_info info;
		if (error == B_OK)
			error = fNode->GetAttrInfo(name, &info);

		// check type and size, allocate a buffer, if required
		if (error == B_OK && info.type != type)
			error = B_BAD_VALUE;
		if (error == B_OK && allocatedBuffer != NULL) {
			buffer = malloc(info.size);
			if (buffer == NULL)
				error = B_NO_MEMORY;
			bufferSize = info.size;
		}
		if (error == B_OK && (off_t)bufferSize < info.size)
			error = B_BAD_VALUE;

		// read the data
		if (error == B_OK) {
			ssize_t read = fNode->ReadAttr(name, type, 0, buffer, info.size);
			if (read < 0)
				error = read;
			else if (read != info.size)
				error = B_ERROR;
			else
				bytesRead = read;
		}

		foundData = error == B_OK;

		// free the allocated buffer on error
		if (!foundData && allocatedBuffer != NULL && buffer != NULL) {
			free(buffer);
			buffer = NULL;
		}
	}

	if (!foundData && IsUsingResources()) {
		// get a resource info
		error = B_OK;
		int32 idFound;
		size_t sizeFound;
		if (error == B_OK) {
			if (!fResources->GetResourceInfo(type, name, &idFound, &sizeFound))
				error = B_ENTRY_NOT_FOUND;
		}

		// check id and size, allocate a buffer, if required
		if (error == B_OK && id >= 0 && idFound != id)
			error = B_ENTRY_NOT_FOUND;
		if (error == B_OK && allocatedBuffer) {
			buffer = malloc(sizeFound);
			if (!buffer)
				error = B_NO_MEMORY;
			bufferSize = sizeFound;
		}
		if (error == B_OK && bufferSize < sizeFound)
			error = B_BAD_VALUE;

		// load resource
		const void* resourceData = NULL;
		if (error == B_OK) {
			resourceData = fResources->LoadResource(type, name, &bytesRead);
			if (resourceData != NULL && sizeFound == bytesRead)
				memcpy(buffer, resourceData, bytesRead);
			else
				error = B_ERROR;
		}
	} else if (!foundData)
		error = B_BAD_VALUE;

	// return the allocated buffer, or free it on error
	if (allocatedBuffer != NULL) {
		if (error == B_OK)
			*allocatedBuffer = buffer;
		else
			free(buffer);
	}

	return error;
}


/**
 * @brief Writes data to an attribute or resource according to the current
 *        info location.
 *
 * @note Data is written to all location(s) specified by \a fWhere.
 *
 * @warning The object must be properly initialised.  Parameters are
 *          \b NOT checked.
 *
 * @param name       Name of the attribute/resource to write.
 * @param id         Resource ID to use when writing to a resource.
 * @param type       Type code of the attribute/resource.
 * @param buffer     Buffer containing the data to write.
 * @param bufferSize Size of the data buffer in bytes.
 * @param findID     If \c true, reuse the existing resource ID for the
 *                   name/type pair, or find the first unused ID >= \a id.
 *                   If \c false, \a id is used as-is.
 * @return A status code.
 * @retval B_OK Everything went fine.
 * @retval B_ERROR A write error occurred.
 * @retval B_NO_INIT Neither attributes nor resources are in use.
 */
status_t
BAppFileInfo::_WriteData(const char* name, int32 id, type_code type,
	const void* buffer, size_t bufferSize, bool findID)
{
	if (!IsUsingAttributes() && !IsUsingResources())
		return B_NO_INIT;

	status_t error = B_OK;

	// write to attribute
	if (IsUsingAttributes()) {
		ssize_t written = fNode->WriteAttr(name, type, 0, buffer, bufferSize);
		if (written < 0)
			error = written;
		else if (written != (ssize_t)bufferSize)
			error = B_ERROR;
	}
	// write to resource
	if (IsUsingResources() && error == B_OK) {
		if (findID) {
			// get the resource info
			int32 idFound;
			size_t sizeFound;
			if (fResources->GetResourceInfo(type, name, &idFound, &sizeFound))
				id = idFound;
			else {
				// type-name pair doesn't exist yet -- find unused ID
				while (fResources->HasResource(type, id))
					id++;
			}
		}
		error = fResources->AddResource(type, id, buffer, bufferSize, name);
	}
	return error;
}


/**
 * @brief Removes an attribute or resource according to the current info
 *        location.
 *
 * @note The removal location is determined by \a fWhere.
 *
 * @warning The object must be properly initialised.  Parameters are
 *          \b NOT checked.
 *
 * @param name The name of the attribute/resource to remove.
 * @param type The type code of the attribute/resource to remove.
 * @return A status code.
 * @retval B_OK Everything went fine (including the case where no entry
 *         existed).
 * @retval B_NO_INIT Neither attributes nor resources are in use.
 * @retval B_ENTRY_NOT_FOUND The resource was not found (attribute absence
 *         is silently ignored).
 */
status_t
BAppFileInfo::_RemoveData(const char* name, type_code type)
{
	if (!IsUsingAttributes() && !IsUsingResources())
		return B_NO_INIT;

	status_t error = B_OK;

	// remove the attribute
	if (IsUsingAttributes()) {
		error = fNode->RemoveAttr(name);
		// It's no error, if there has been no attribute.
		if (error == B_ENTRY_NOT_FOUND)
			error = B_OK;
	}
	// remove the resource
	if (IsUsingResources() && error == B_OK) {
		// get a resource info
		int32 idFound;
		size_t sizeFound;
		if (fResources->GetResourceInfo(type, name, &idFound, &sizeFound))
			error = fResources->RemoveResource(type, idFound);
	}
	return error;
}
