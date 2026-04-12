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
 *   Copyright 2011-2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file PackageInfoContentHandler.cpp
 * @brief HPKG package-reader callback that populates a BPackageInfo from attributes.
 *
 * BPackageInfoContentHandler implements the BHPKG content-handler interface.
 * It is registered with a BPackageReader and receives package-info attribute
 * values one by one, forwarding each to the corresponding setter on the target
 * BPackageInfo object.
 *
 * @see BPackageInfo, BHPKG::BPackageReader
 */


#include <package/PackageInfoContentHandler.h>

#include <package/PackageInfo.h>
#include <package/hpkg/ErrorOutput.h>
#include <package/hpkg/PackageInfoAttributeValue.h>


namespace BPackageKit {


using namespace BHPKG;


/**
 * @brief Construct the handler targeting the given BPackageInfo.
 *
 * @param packageInfo   The BPackageInfo object that receives decoded attributes.
 * @param errorOutput   Optional error-output sink for diagnostic messages;
 *                      may be NULL to suppress error output.
 */
BPackageInfoContentHandler::BPackageInfoContentHandler(
	BPackageInfo& packageInfo, BErrorOutput* errorOutput)
	:
	fPackageInfo(packageInfo),
	fErrorOutput(errorOutput)
{
}


/**
 * @brief Destructor.
 */
BPackageInfoContentHandler::~BPackageInfoContentHandler()
{
}


/**
 * @brief Package-entry callback; not used for info extraction.
 *
 * @param entry  The package entry encountered (ignored).
 * @return Always B_OK.
 */
status_t
BPackageInfoContentHandler::HandleEntry(BPackageEntry* entry)
{
	return B_OK;
}


/**
 * @brief Package-entry attribute callback; not used for info extraction.
 *
 * @param entry      The package entry (ignored).
 * @param attribute  The entry attribute (ignored).
 * @return Always B_OK.
 */
status_t
BPackageInfoContentHandler::HandleEntryAttribute(BPackageEntry* entry,
	BPackageEntryAttribute* attribute)
{
	return B_OK;
}


/**
 * @brief Package-entry completion callback; not used for info extraction.
 *
 * @param entry  The completed package entry (ignored).
 * @return Always B_OK.
 */
status_t
BPackageInfoContentHandler::HandleEntryDone(BPackageEntry* entry)
{
	return B_OK;
}


/**
 * @brief Receive a single package-info attribute and apply it to the target BPackageInfo.
 *
 * Dispatches on the attribute ID and calls the appropriate setter or adder on
 * fPackageInfo.  Unknown attribute IDs are reported to fErrorOutput (if set)
 * and B_NOT_SUPPORTED is returned so the reader can skip future attributes.
 *
 * @param value  The decoded attribute value from the HPKG reader.
 * @return B_OK on success, B_NOT_SUPPORTED for unknown attribute IDs, or an
 *         error code if a setter fails.
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
			return fPackageInfo.AddCopyright(value.string);

		case B_PACKAGE_INFO_LICENSES:
			return fPackageInfo.AddLicense(value.string);

		case B_PACKAGE_INFO_PROVIDES:
			return fPackageInfo.AddProvides(value.resolvable);

		case B_PACKAGE_INFO_REQUIRES:
			return fPackageInfo.AddRequires(value.resolvableExpression);

		case B_PACKAGE_INFO_SUPPLEMENTS:
			return fPackageInfo.AddSupplements(value.resolvableExpression);

		case B_PACKAGE_INFO_CONFLICTS:
			return fPackageInfo.AddConflicts(value.resolvableExpression);

		case B_PACKAGE_INFO_FRESHENS:
			return fPackageInfo.AddFreshens(value.resolvableExpression);

		case B_PACKAGE_INFO_REPLACES:
			return fPackageInfo.AddReplaces(value.string);

		case B_PACKAGE_INFO_URLS:
			return fPackageInfo.AddURL(value.string);

		case B_PACKAGE_INFO_SOURCE_URLS:
			return fPackageInfo.AddSourceURL(value.string);

		case B_PACKAGE_INFO_CHECKSUM:
			fPackageInfo.SetChecksum(value.string);
			break;

		case B_PACKAGE_INFO_INSTALL_PATH:
			fPackageInfo.SetInstallPath(value.string);
			break;

		case B_PACKAGE_INFO_BASE_PACKAGE:
			fPackageInfo.SetBasePackage(value.string);
			break;

		case B_PACKAGE_INFO_GLOBAL_WRITABLE_FILES:
			return fPackageInfo.AddGlobalWritableFileInfo(
				value.globalWritableFileInfo);

		case B_PACKAGE_INFO_USER_SETTINGS_FILES:
			return fPackageInfo.AddUserSettingsFileInfo(value.userSettingsFileInfo);

		case B_PACKAGE_INFO_USERS:
			return fPackageInfo.AddUser(value.user);

		case B_PACKAGE_INFO_GROUPS:
			return fPackageInfo.AddGroup(value.string);

		case B_PACKAGE_INFO_POST_INSTALL_SCRIPTS:
			return fPackageInfo.AddPostInstallScript(value.string);

		case B_PACKAGE_INFO_PRE_UNINSTALL_SCRIPTS:
			fPackageInfo.AddPreUninstallScript(value.string);
			break;

		default:
			if (fErrorOutput != NULL) {
				fErrorOutput->PrintError(
					"Invalid package attribute section: unexpected package "
					"attribute id %d encountered\n", value.attributeID);
			}
			return B_NOT_SUPPORTED; // Could be a future attribute we can skip.
	}

	return B_OK;
}


/**
 * @brief Called by the HPKG reader when a parse error occurs.
 *
 * The default implementation is a no-op; subclasses may override to handle
 * errors from the HPKG reader.
 */
void
BPackageInfoContentHandler::HandleErrorOccurred()
{
}


}	// namespace BPackageKit
