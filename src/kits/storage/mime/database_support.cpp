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
 *   Copyright 2002-2013, Haiku.
 *   Authors:
 *       Tyler Dauwalder
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file database_support.cpp
 * @brief Global constants, error codes, and utility functions for the MIME database.
 *
 * Defines all attribute name and type constants used throughout the MIME database
 * implementation, the well-known MIME type strings, and the kMimeGuessFailureError
 * sentinel.  Also provides default_database_location() which initialises and
 * returns the process-wide DatabaseLocation, and get_icon_data() which converts
 * a BBitmap to a flat B_CMAP8 byte array suitable for database storage.
 *
 * @see DatabaseLocation
 */


#include <mime/database_support.h>

#if defined(__HAIKU__) && !defined(HAIKU_HOST_PLATFORM_HAIKU)
#	include <pthread.h>
#endif

#include <new>

#include <Bitmap.h>
#include <FindDirectory.h>
#include <Path.h>

#include <mime/DatabaseLocation.h>


namespace BPrivate {
namespace Storage {
namespace Mime {


#define ATTR_PREFIX "META:"
#define MINI_ICON_ATTR_PREFIX ATTR_PREFIX "M:"
#define LARGE_ICON_ATTR_PREFIX ATTR_PREFIX "L:"

const char *kMiniIconAttrPrefix		= MINI_ICON_ATTR_PREFIX;
const char *kLargeIconAttrPrefix	= LARGE_ICON_ATTR_PREFIX;
const char *kIconAttrPrefix			= ATTR_PREFIX;

// attribute names
const char *kFileTypeAttr			= "BEOS:TYPE";
const char *kTypeAttr				= ATTR_PREFIX "TYPE";
const char *kAppHintAttr			= ATTR_PREFIX "PPATH";
const char *kAttrInfoAttr			= ATTR_PREFIX "ATTR_INFO";
const char *kShortDescriptionAttr	= ATTR_PREFIX "S:DESC";
const char *kLongDescriptionAttr	= ATTR_PREFIX "L:DESC";
const char *kFileExtensionsAttr		= ATTR_PREFIX "EXTENS";
const char *kMiniIconAttr			= MINI_ICON_ATTR_PREFIX "STD_ICON";
const char *kLargeIconAttr			= LARGE_ICON_ATTR_PREFIX "STD_ICON";
const char *kIconAttr				= ATTR_PREFIX "ICON";
const char *kPreferredAppAttr		= ATTR_PREFIX "PREF_APP";
const char *kSnifferRuleAttr		= ATTR_PREFIX "SNIFF_RULE";
const char *kSupportedTypesAttr		= ATTR_PREFIX "FILE_TYPES";

// attribute data types (as used in the R5 database)
const int32 kFileTypeType			= 'MIMS';	// B_MIME_STRING_TYPE
const int32 kTypeType				= B_STRING_TYPE;
const int32 kAppHintType			= 'MPTH';
const int32 kAttrInfoType			= B_MESSAGE_TYPE;
const int32 kShortDescriptionType	= 'MSDC';
const int32 kLongDescriptionType	= 'MLDC';
const int32 kFileExtensionsType		= B_MESSAGE_TYPE;
const int32 kMiniIconType			= B_MINI_ICON_TYPE;
const int32 kLargeIconType			= B_LARGE_ICON_TYPE;
const int32 kIconType				= B_VECTOR_ICON_TYPE;
const int32 kPreferredAppType		= 'MSIG';
const int32 kSnifferRuleType		= B_STRING_TYPE;
const int32 kSupportedTypesType		= B_MESSAGE_TYPE;

// Message fields
const char *kApplicationsField				= "applications";
const char *kExtensionsField				= "extensions";
const char *kSupertypesField				= "super_types";
const char *kSupportingAppsSubCountField	= "be:sub";
const char *kSupportingAppsSuperCountField	= "be:super";
const char *kTypesField						= "types";

// Mime types
const char *kGenericFileType	= "application/octet-stream";
const char *kDirectoryType		= "application/x-vnd.Be-directory";
const char *kSymlinkType		= "application/x-vnd.Be-symlink";
const char *kMetaMimeType		= "application/x-vnd.Be-meta-mime";

// Error codes
const status_t kMimeGuessFailureError	= B_ERRORS_END+1;


#if defined(__HAIKU__) && !defined(HAIKU_HOST_PLATFORM_HAIKU)


static const directory_which kBaseDirectoryConstants[] = {
	B_USER_SETTINGS_DIRECTORY,
	B_USER_NONPACKAGED_DATA_DIRECTORY,
	B_USER_DATA_DIRECTORY,
	B_SYSTEM_NONPACKAGED_DATA_DIRECTORY,
	B_SYSTEM_DATA_DIRECTORY
};

static pthread_once_t sDefaultDatabaseLocationInitOnce = PTHREAD_ONCE_INIT;
static DatabaseLocation* sDefaultDatabaseLocation = NULL;


/**
 * @brief One-time initialiser for the process-wide default DatabaseLocation.
 *
 * Called via pthread_once; iterates over kBaseDirectoryConstants to build the
 * ordered list of MIME database directories and stores the result in
 * sDefaultDatabaseLocation.
 */
static void
init_default_database_location()
{
	static DatabaseLocation databaseLocation;
	sDefaultDatabaseLocation = &databaseLocation;

	for (size_t i = 0;
		i < sizeof(kBaseDirectoryConstants)
			/ sizeof(kBaseDirectoryConstants[0]); i++) {
		BString directoryPath;
		BPath path;
		if (find_directory(kBaseDirectoryConstants[i], &path) == B_OK)
			directoryPath = path.Path();
		else if (i == 0)
			directoryPath = "/boot/home/config/settings";
		else
			continue;

		directoryPath += "/mime_db";
		databaseLocation.AddDirectory(directoryPath);
	}
}


/**
 * @brief Returns the process-wide default DatabaseLocation, initialising it on first call.
 *
 * Uses pthread_once to guarantee thread-safe single initialisation across the
 * standard Haiku directory hierarchy.
 *
 * @return Pointer to the process-wide DatabaseLocation; never NULL.
 */
DatabaseLocation*
default_database_location()
{
	pthread_once(&sDefaultDatabaseLocationInitOnce,
		&init_default_database_location);
	return sDefaultDatabaseLocation;
}


#else	// building for the host platform


/**
 * @brief Returns a stub DatabaseLocation pointing to /tmp for host-platform builds.
 *
 * This overload is compiled when building the MIME kit for the host (non-Haiku)
 * platform.  It returns a valid but minimal DatabaseLocation so that code that
 * calls default_database_location() links and runs without crashing, even though
 * the returned location is not actually used for real MIME lookups.
 *
 * @return Pointer to a static DatabaseLocation rooted at /tmp.
 */
DatabaseLocation*
default_database_location()
{
	// Should never actually be used, but make it valid, anyway.
	static DatabaseLocation location;
	if (location.Directories().IsEmpty())
		location.AddDirectory("/tmp");
	return &location;
}


#endif


/**
 * @brief Converts a BBitmap to a flat B_CMAP8 byte array for MIME database storage.
 *
 * Takes the given bitmap, verifies its bounds match the expected size for
 * \a which (16x16 for B_MINI_ICON, 32x32 for B_LARGE_ICON), converts it to
 * the B_CMAP8 colour space if necessary, and returns a newly allocated copy of
 * the raw pixel data.  The caller is responsible for releasing the array with
 * delete[].
 *
 * @param icon     Pointer to the source BBitmap; must be initialised and non-NULL.
 * @param which    The icon size: B_MINI_ICON or B_LARGE_ICON.
 * @param data     Output parameter receiving a pointer to the newly allocated
 *                 pixel data array.
 * @param dataSize Output parameter receiving the size of the allocated array
 *                 in bytes.
 * @return B_OK on success, B_BAD_VALUE for invalid arguments or wrong bounds,
 *         or B_NO_MEMORY if allocation fails.
 */
status_t
get_icon_data(const BBitmap *icon, icon_size which, void **data,
	int32 *dataSize)
{
	if (icon == NULL || data == NULL || dataSize == 0
		|| icon->InitCheck() != B_OK)
		return B_BAD_VALUE;

	BRect bounds;
	BBitmap *icon8 = NULL;
	void *srcData = NULL;
	bool otherColorSpace = false;

	// Figure out what kind of data we *should* have
	switch (which) {
		case B_MINI_ICON:
			bounds.Set(0, 0, 15, 15);
			break;
		case B_LARGE_ICON:
			bounds.Set(0, 0, 31, 31);
			break;
		default:
			return B_BAD_VALUE;
	}

	// Check the icon
	status_t err = icon->Bounds() == bounds ? B_OK : B_BAD_VALUE;

	// Convert to B_CMAP8 if necessary
	if (!err) {
		otherColorSpace = (icon->ColorSpace() != B_CMAP8);
		if (otherColorSpace) {
			icon8 = new(std::nothrow) BBitmap(bounds, B_BITMAP_NO_SERVER_LINK,
				B_CMAP8);
			if (!icon8)
				err = B_NO_MEMORY;
			if (!err)
				err = icon8->ImportBits(icon);
			if (!err) {
				srcData = icon8->Bits();
				*dataSize = icon8->BitsLength();
			}
		} else {
			srcData = icon->Bits();
			*dataSize = icon->BitsLength();
		}
	}

	// Alloc a new data buffer
	if (!err) {
		*data = new(std::nothrow) char[*dataSize];
		if (!*data)
			err = B_NO_MEMORY;
	}

	// Copy the data into it.
	if (!err)
		memcpy(*data, srcData, *dataSize);
	if (otherColorSpace)
		delete icon8;
	return err;
}


} // namespace Mime
} // namespace Storage
} // namespace BPrivate
