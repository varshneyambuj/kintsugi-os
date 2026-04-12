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
 *   Copyright 2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file PackageInfoContentHandlerV1.cpp
 * @brief Content handler that populates a BPackageInfo from a v1 HPKG package.
 *
 * BPackageInfoContentHandler (v1) implements BPackageContentHandler and translates
 * the stream of package attribute callbacks received from the v1 reader into
 * setter calls on the BPackageInfo supplied at construction time. File-system
 * entry and extended-attribute callbacks are silently ignored; only package
 * metadata attributes are processed.
 *
 * @see BPackageContentHandler (V1), BPackageReader (V1)
 */


#include <package/hpkg/v1/PackageInfoContentHandler.h>

#include <package/PackageInfo.h>
#include <package/hpkg/ErrorOutput.h>
#include <package/hpkg/PackageInfoAttributeValue.h>


namespace BPackageKit {

namespace BHPKG {

namespace V1 {


/**
 * @brief Constructs a BPackageInfoContentHandler that will populate @a packageInfo.
 *
 * @param packageInfo  Reference to the BPackageInfo object to fill in.
 * @param errorOutput  Callback for error messages; may be NULL to suppress output.
 */
BPackageInfoContentHandler::BPackageInfoContentHandler(
	BPackageInfo& packageInfo, BErrorOutput* errorOutput)
	:
	fPackageInfo(packageInfo),
	fErrorOutput(errorOutput)
{
}


/**
 * @brief Destroys the BPackageInfoContentHandler.
 */
BPackageInfoContentHandler::~BPackageInfoContentHandler()
{
}


/**
 * @brief Called for each file-system entry encountered; always returns B_OK.
 *
 * Package info extraction does not process directory entries.
 *
 * @param entry The entry encountered; ignored.
 * @return B_OK.
 */
status_t
BPackageInfoContentHandler::HandleEntry(BPackageEntry* entry)
{
	return B_OK;
}


/**
 * @brief Called for each extended attribute of an entry; always returns B_OK.
 *
 * Package info extraction does not process extended attributes.
 *
 * @param entry     The parent entry; ignored.
 * @param attribute The extended attribute; ignored.
 * @return B_OK.
 */
status_t
BPackageInfoContentHandler::HandleEntryAttribute(BPackageEntry* entry,
	BPackageEntryAttribute* attribute)
{
	return B_OK;
}


/**
 * @brief Called when all sub-attributes of an entry are done; always returns B_OK.
 *
 * @param entry The entry that was completed; ignored.
 * @return B_OK.
 */
status_t
BPackageInfoContentHandler::HandleEntryDone(BPackageEntry* entry)
{
	return B_OK;
}


/**
 * @brief Dispatches a package attribute value to the appropriate BPackageInfo setter.
 *
 * Maps each BPackageInfoAttributeID to the corresponding BPackageInfo method.
 * Returns B_BAD_DATA if an unrecognised attribute ID is encountered and an
 * error output object was supplied at construction time.
 *
 * @param value Decoded package attribute value including its ID and payload.
 * @return B_OK on success, B_BAD_DATA for an unrecognised attribute ID.
 */
status_t
BPackageInfoContentHandler::HandlePackageAttribute(
	const BPackageInfoAttributeValue& value)
{
	switch (value.attributeID) {
		case B_PACKAGE_INFO_NAME:
			fPackageInfo.SetName(value.string);
			break;

		case B_PACKAGE_INFO_SUMMARY:
			fPackageInfo.SetSummary(value.string);
			break;

		case B_PACKAGE_INFO_DESCRIPTION:
			fPackageInfo.SetDescription(value.string);
			break;

		case B_PACKAGE_INFO_VENDOR:
			fPackageInfo.SetVendor(value.string);
			break;

		case B_PACKAGE_INFO_PACKAGER:
			fPackageInfo.SetPackager(value.string);
			break;

		case B_PACKAGE_INFO_FLAGS:
			fPackageInfo.SetFlags(value.unsignedInt);
			break;

		case B_PACKAGE_INFO_ARCHITECTURE:
			fPackageInfo.SetArchitecture(
				(BPackageArchitecture)value.unsignedInt);
			break;

		case B_PACKAGE_INFO_VERSION:
			fPackageInfo.SetVersion(value.version);
			break;

		case B_PACKAGE_INFO_COPYRIGHTS:
			fPackageInfo.AddCopyright(value.string);
			break;

		case B_PACKAGE_INFO_LICENSES:
			fPackageInfo.AddLicense(value.string);
			break;

		case B_PACKAGE_INFO_PROVIDES:
			fPackageInfo.AddProvides(value.resolvable);
			break;

		case B_PACKAGE_INFO_REQUIRES:
			fPackageInfo.AddRequires(value.resolvableExpression);
			break;

		case B_PACKAGE_INFO_SUPPLEMENTS:
			fPackageInfo.AddSupplements(value.resolvableExpression);
			break;

		case B_PACKAGE_INFO_CONFLICTS:
			fPackageInfo.AddConflicts(value.resolvableExpression);
			break;

		case B_PACKAGE_INFO_FRESHENS:
			fPackageInfo.AddFreshens(value.resolvableExpression);
			break;

		case B_PACKAGE_INFO_REPLACES:
			fPackageInfo.AddReplaces(value.string);
			break;

		case B_PACKAGE_INFO_URLS:
			fPackageInfo.AddURL(value.string);
			break;

		case B_PACKAGE_INFO_SOURCE_URLS:
			fPackageInfo.AddSourceURL(value.string);
			break;

		case B_PACKAGE_INFO_CHECKSUM:
			fPackageInfo.SetChecksum(value.string);
			break;

		case B_PACKAGE_INFO_INSTALL_PATH:
			fPackageInfo.SetInstallPath(value.string);
			break;

		default:
			if (fErrorOutput != NULL) {
				fErrorOutput->PrintError(
					"Invalid package attribute section: unexpected package "
					"attribute id %d encountered\n", value.attributeID);
			}
			return B_BAD_DATA;
	}

	return B_OK;
}


/**
 * @brief Called when a parse error occurs; performs no action.
 */
void
BPackageInfoContentHandler::HandleErrorOccurred()
{
}


}	// namespace V1

}	// namespace BHPKG

}	// namespace BPackageKit
