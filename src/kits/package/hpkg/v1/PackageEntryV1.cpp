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
 * @file PackageEntryV1.cpp
 * @brief File-system entry descriptor for a v1 HPKG package.
 *
 * BPackageEntry (v1) represents one node in the package's virtual file tree.
 * It stores the entry's parent pointer, name, permissions, timestamps, optional
 * symlink target, and an opaque user token that content handlers may use to
 * associate application-level state with the entry during parsing.
 *
 * @see BPackageEntryAttribute (V1), BPackageContentHandler (V1), PackageReaderImplV1
 */


#include <package/hpkg/v1/PackageEntry.h>


namespace BPackageKit {

namespace BHPKG {

namespace V1 {


/**
 * @brief Constructs a BPackageEntry with default permissions and zeroed timestamps.
 *
 * The default mode is set to a world-readable regular file
 * (S_IFREG | S_IRUSR | S_IRGRP | S_IROTH). All three timestamps (access,
 * modification, creation) are initialised to the UNIX epoch.
 *
 * @param parent Pointer to the parent directory entry, or NULL for root.
 * @param name   Entry name; the caller retains ownership of the string.
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


}	// namespace V1

}	// namespace BHPKG

}	// namespace BPackageKit
