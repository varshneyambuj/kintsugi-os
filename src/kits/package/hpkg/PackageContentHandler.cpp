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
 * @file PackageContentHandler.cpp
 * @brief Callback interfaces for consuming HPKG package content during parsing.
 *
 * Defines the virtual destructors and the static attribute-name lookup table
 * for BLowLevelPackageContentHandler and BPackageContentHandler.  The
 * attribute-name table is generated at compile time from PackageAttributes.h
 * so that numeric attribute IDs can be mapped to human-readable strings during
 * low-level attribute traversal.
 *
 * @see BPackageReader, BLowLevelPackageContentHandler
 */


#include <package/hpkg/PackageContentHandler.h>


namespace BPackageKit {

namespace BHPKG {


// #pragma mark - BLowLevelPackageContentHandler


/** @brief Compile-time table mapping HPKG attribute IDs to their string names. */
static const char* kAttributeNames[B_HPKG_ATTRIBUTE_ID_ENUM_COUNT + 1] = {
	#define B_DEFINE_HPKG_ATTRIBUTE(id, type, name, constant)	\
		name,
	#include <package/hpkg/PackageAttributes.h>
	#undef B_DEFINE_HPKG_ATTRIBUTE
	//
	NULL	// B_HPKG_ATTRIBUTE_ID_ENUM_COUNT
};


/**
 * @brief Virtual destructor for BLowLevelPackageContentHandler.
 *
 * Allows derived low-level handlers to be deleted through a base-class pointer.
 */
BLowLevelPackageContentHandler::~BLowLevelPackageContentHandler()
{
}


/**
 * @brief Look up the human-readable name of an HPKG attribute by its numeric ID.
 *
 * @param id Numeric attribute identifier (one of the B_HPKG_ATTRIBUTE_ID_*
 *           constants).
 * @return A pointer to the null-terminated attribute name string, or NULL if
 *         \a id is out of range.
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
 * @brief Virtual destructor for BPackageContentHandler.
 *
 * Allows derived high-level content handlers to be deleted through a
 * base-class pointer.
 */
BPackageContentHandler::~BPackageContentHandler()
{
}


}	// namespace BHPKG

}	// namespace BPackageKit
