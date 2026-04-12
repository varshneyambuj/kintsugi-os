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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file PackageEntry.cpp
 * @brief Representation of a single file-system entry within an HPKG package.
 *
 * BPackageEntry holds the metadata (name, parent, permissions, timestamps,
 * symlink path, and data payload) for one directory entry found in a package
 * TOC.  The content handler receives a BPackageEntry for every entry during
 * parsing; the object remains valid only for the duration of the callback.
 *
 * @see BPackageContentHandler, BPackageEntryAttribute, BPackageData
 */


#include <package/hpkg/PackageEntry.h>


namespace BPackageKit {

namespace BHPKG {


/**
 * @brief Construct a package entry with the given parent and name.
 *
 * All timestamps are zeroed, mode is set to a read-only regular file
 * (S_IFREG | S_IRUSR | S_IRGRP | S_IROTH), and no symlink path is stored.
 *
 * @param parent Pointer to the parent directory entry, or NULL for the
 *               root of the package.
 * @param name   Null-terminated entry name (borrowed reference into the
 *               TOC buffer; must remain valid while this object is used).
 */
BPackageEntry::BPackageEntry(BPackageEntry* parent, const char* name)
	:
	fParent(parent),
	fName(name),
	fUserToken(NULL),
	fMode(S_IFREG | S_IRUSR | S_IRGRP | S_IROTH),
	fSymlinkPath(NULL)
{
	fAccessTime.tv_sec = 0;
	fAccessTime.tv_nsec = 0;
	fModifiedTime.tv_sec = 0;
	fModifiedTime.tv_nsec = 0;
	fCreationTime.tv_sec = 0;
	fCreationTime.tv_nsec = 0;
}


}	// namespace BHPKG

}	// namespace BPackageKit
