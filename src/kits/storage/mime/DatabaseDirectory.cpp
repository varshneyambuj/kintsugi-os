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
 *   Copyright 2013, Haiku, Inc. All Rights Reserved.
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file DatabaseDirectory.cpp
 * @brief Merged directory view across all MIME database location paths.
 *
 * DatabaseDirectory extends BMergedDirectory to provide a unified view of the
 * MIME database directories held by a DatabaseLocation.  When initialised with
 * an optional supertype name, it automatically adds the matching sub-directory
 * from every database location so that callers can iterate over all installed
 * subtypes transparently.  Preference between duplicate entries is resolved by
 * checking the presence of the MIME:TYPE attribute.
 *
 * @see DatabaseLocation
 */


#include <mime/DatabaseDirectory.h>

#include <fs_attr.h>
#include <Node.h>
#include <StringList.h>

#include <mime/database_support.h>
#include <mime/DatabaseLocation.h>


namespace BPrivate {
namespace Storage {
namespace Mime {


/**
 * @brief Constructs a DatabaseDirectory with comparison-based merge ordering.
 */
DatabaseDirectory::DatabaseDirectory()
	:
	BMergedDirectory(B_COMPARE)
{
}


/**
 * @brief Destroys the DatabaseDirectory.
 */
DatabaseDirectory::~DatabaseDirectory()
{
}


/**
 * @brief Initialises the merged directory from a DatabaseLocation, optionally scoped to a supertype.
 *
 * Calls BMergedDirectory::Init() then iterates over every directory path
 * stored in \a databaseLocation, appending \a superType (if non-NULL) to each
 * path before adding it to the merge set.
 *
 * @param databaseLocation The location object that holds the list of MIME database paths.
 * @param superType        Optional supertype name used to scope the search to a
 *                         single supertype sub-directory; pass NULL for the root.
 * @return B_OK on success, or an error code if BMergedDirectory::Init() fails.
 */
status_t
DatabaseDirectory::Init(DatabaseLocation* databaseLocation,
	const char* superType)
{
	status_t error = BMergedDirectory::Init();
	if (error != B_OK)
		return error;

	const BStringList& directories = databaseLocation->Directories();
	int32 count = directories.CountStrings();
	for (int32 i = 0; i < count; i++) {
		BString directory = directories.StringAt(i);
		if (superType != NULL)
			directory << '/' << superType;

		AddDirectory(directory);
	}

	return B_OK;
}


/**
 * @brief Determines which of two duplicate entries should be preferred in the merged view.
 *
 * Returns true if \a entry1 has the MIME:TYPE attribute (and is therefore a
 * valid MIME type entry), or if \a entry2 does not.  This gives priority to
 * well-formed MIME database entries over stale or incomplete ones.
 *
 * @param entry1 First candidate entry.
 * @param index1 Directory index of the first entry.
 * @param entry2 Second candidate entry.
 * @param index2 Directory index of the second entry.
 * @return true if entry1 should be preferred over entry2.
 */
bool
DatabaseDirectory::ShallPreferFirstEntry(const entry_ref& entry1, int32 index1,
	const entry_ref& entry2, int32 index2)
{
	return _IsValidMimeTypeEntry(entry1) || !_IsValidMimeTypeEntry(entry2);
}


/**
 * @brief Checks whether a filesystem entry is a valid MIME type entry.
 *
 * Opens the entry as a BNode and tests for the presence of the
 * BPrivate::Storage::Mime::kTypeAttr attribute.
 *
 * @param entry The filesystem entry to inspect.
 * @return true if the entry node has the MIME:TYPE attribute.
 */
bool
DatabaseDirectory::_IsValidMimeTypeEntry(const entry_ref& entry)
{
	// check whether the MIME:TYPE attribute exists
	BNode node;
	attr_info info;
	return node.SetTo(&entry) == B_OK
		&& node.GetAttrInfo(BPrivate::Storage::Mime::kTypeAttr, &info) == B_OK;
}


} // namespace Mime
} // namespace Storage
} // namespace BPrivate
