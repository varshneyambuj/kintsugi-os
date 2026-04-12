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
 *   Copyright 2009-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file PackageContentHandlerV1.cpp
 * @brief Abstract content handler interfaces for the HPKG v1 format.
 *
 * Defines the virtual destructors and the static AttributeNameForID() helper
 * for BLowLevelPackageContentHandler, as well as the virtual destructor for
 * BPackageContentHandler. The static name table maps each v1 attribute ID to
 * its canonical string name, enabling diagnostic output and protocol negotiation.
 *
 * @see BPackageReader (V1), ReaderImplBaseV1
 */


#include <package/hpkg/v1/PackageContentHandler.h>


namespace BPackageKit {

namespace BHPKG {

namespace V1 {


// #pragma mark - BLowLevelPackageContentHandler


/** @brief Mapping table from v1 BHPKGAttributeID to canonical attribute name strings. */
static const char* kAttributeNames[B_HPKG_ATTRIBUTE_ID_ENUM_COUNT + 1] = {
	"dir:entry",
	"file:type",
	"file:permissions",
	"file:user",
	"file:group",
	"file:atime",
	"file:mtime",
	"file:crtime",
	"file:atime:nanos",
	"file:mtime:nanos",
	"file:crtime:nanos",
	"file:attribute",
	"file:attribute:type",
	"data",
	"data:compression",
	"data:size",
	"data:chunk_size",
	"symlink:path",
	"package:name",
	"package:summary",
	"package:description",
	"package:vendor",
	"package:packager",
	"package:flags",
	"package:architecture",
	"package:version.major",
	"package:version.minor",
	"package:version.micro",
	"package:version.revision",
	"package:copyright",
	"package:license",
	"package:provides",
	"package:provides.type",
	"package:requires",
	"package:supplements",
	"package:conflicts",
	"package:freshens",
	"package:replaces",
	"package:resolvable.operator",
	"package:checksum",
	"package:version.prerelease",
	"package:provides.compatible",
	"package:url",
	"package:source-url",
	"package:install-path",
	NULL
};


/**
 * @brief Destroys the BLowLevelPackageContentHandler.
 *
 * The destructor is defined here to anchor the vtable in this translation unit.
 */
BLowLevelPackageContentHandler::~BLowLevelPackageContentHandler()
{
}


/**
 * @brief Returns the canonical attribute name string for a given v1 attribute ID.
 *
 * Provides a human-readable name for diagnostic output and forward-compatibility
 * checks. Returns NULL if @a id is out of range.
 *
 * @param id Attribute ID; must be less than B_HPKG_ATTRIBUTE_ID_ENUM_COUNT.
 * @return A pointer to a static string, or NULL if @a id is out of range.
 */
/*static*/ const char*
BLowLevelPackageContentHandler::AttributeNameForID(uint8 id)
{
	if (id >= B_HPKG_ATTRIBUTE_ID_ENUM_COUNT)
		return NULL;

	return kAttributeNames[id];
}


// #pragma mark - BPackageContentHandler


/**
 * @brief Destroys the BPackageContentHandler.
 *
 * The destructor is defined here to anchor the vtable in this translation unit.
 */
BPackageContentHandler::~BPackageContentHandler()
{
}


}	// namespace V1

}	// namespace BHPKG

}	// namespace BPackageKit
