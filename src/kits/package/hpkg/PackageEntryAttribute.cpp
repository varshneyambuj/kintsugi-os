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
 * @file PackageEntryAttribute.cpp
 * @brief Representation of a single extended attribute attached to a package entry.
 *
 * BPackageEntryAttribute stores the name, BeOS attribute type, and data
 * payload (as a BPackageData) for one named attribute on a BPackageEntry.
 * Instances are created by the package reader while processing the TOC and
 * are delivered to the content handler via HandleEntryAttribute().
 *
 * @see BPackageEntry, BPackageContentHandler, BPackageData
 */


#include <package/hpkg/PackageEntryAttribute.h>


namespace BPackageKit {

namespace BHPKG {


/**
 * @brief Construct an attribute descriptor with the given name.
 *
 * The name is stored as a pointer (borrowed reference) into the TOC buffer;
 * the type is initialised to zero and must be set later via SetType().
 *
 * @param name Null-terminated name of the extended attribute.
 */
BPackageEntryAttribute::BPackageEntryAttribute(const char* name)
	:
	fName(name),
	fType(0)
{
}


}	// namespace BHPKG

}	// namespace BPackageKit
