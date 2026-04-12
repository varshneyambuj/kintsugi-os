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
 * @file PackageEntryAttributeV1.cpp
 * @brief Extended attribute descriptor for a file entry in a v1 HPKG package.
 *
 * BPackageEntryAttribute (v1) represents a single named extended attribute
 * attached to a BPackageEntry. It stores the attribute's name and MIME-style
 * type code, and is populated by the v1 reader as it processes the attribute
 * section of the package's TOC.
 *
 * @see BPackageEntry (V1), PackageReaderImplV1
 */


#include <package/hpkg/v1/PackageEntryAttribute.h>


namespace BPackageKit {

namespace BHPKG {

namespace V1 {


/**
 * @brief Constructs a BPackageEntryAttribute with a name and no type set.
 *
 * @param name The name of the extended attribute; the caller retains ownership
 *             of the string and must ensure it outlives this object.
 */
BPackageEntryAttribute::BPackageEntryAttribute(const char* name)
	:
	fName(name),
	fType(0)
{
}


}	// namespace V1

}	// namespace BHPKG

}	// namespace BPackageKit
