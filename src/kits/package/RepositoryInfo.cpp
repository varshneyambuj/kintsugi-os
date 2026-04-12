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
 *   Copyright 2011-2020, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe <zooey@hirschkaefer.de>
 *       Andrew Lindesay <apl@lindesay.co.nz>
 */


/**
 * @file RepositoryInfo.cpp
 * @brief Implementation of BRepositoryInfo, the metadata descriptor for a package repository.
 *
 * BRepositoryInfo carries all human-visible and machine-readable metadata about
 * a repository: its name, vendor, summary, canonical identifier URL, base URL,
 * priority, target architecture, and license texts. It can be serialised to/from
 * BMessage for IPC as well as read from the driver-settings-format info file
 * embedded in a repository cache.
 *
 * @see BRepositoryConfig, BRepositoryCache
 */


#include <package/RepositoryInfo.h>

#include <stdlib.h>

#include <new>

#include <driver_settings.h>
#include <File.h>
#include <Message.h>

#include <AutoDeleter.h>
#include <AutoDeleterDrivers.h>
#include <package/PackageInfo.h>


namespace BPackageKit {


/** @brief Default repository priority used when none is explicitly configured. */
const uint8 BRepositoryInfo::kDefaultPriority	= 50;

/** @brief BMessage field name for the repository name. */
const char* const BRepositoryInfo::kNameField			= "name";
/** @brief BMessage field name for the legacy identifier URL. */
const char* const BRepositoryInfo::kURLField			= "url";
/** @brief BMessage field name for the canonical identifier URL. */
const char* const BRepositoryInfo::kIdentifierField		= "identifier";
/** @brief BMessage field name for the base URL. */
const char* const BRepositoryInfo::kBaseURLField		= "baseUrl";
/** @brief BMessage field name for the vendor string. */
const char* const BRepositoryInfo::kVendorField			= "vendor";
/** @brief BMessage field name for the repository summary. */
const char* const BRepositoryInfo::kSummaryField		= "summary";
/** @brief BMessage field name for the priority value. */
const char* const BRepositoryInfo::kPriorityField		= "priority";
/** @brief BMessage field name for the target architecture. */
const char* const BRepositoryInfo::kArchitectureField	= "architecture";
/** @brief BMessage field name for a license name entry. */
const char* const BRepositoryInfo::kLicenseNameField	= "licenseName";
/** @brief BMessage field name for a license text entry. */
const char* const BRepositoryInfo::kLicenseTextField	= "licenseText";


/**
 * @brief Default-construct an uninitialised BRepositoryInfo.
 */
BRepositoryInfo::BRepositoryInfo()
	:
	fInitStatus(B_NO_INIT),
	fPriority(kDefaultPriority),
	fArchitecture(B_PACKAGE_ARCHITECTURE_ENUM_COUNT)
{
}


/**
 * @brief Construct a BRepositoryInfo from an archived BMessage.
 *
 * @param data  The BMessage produced by a prior Archive() call.
 */
BRepositoryInfo::BRepositoryInfo(BMessage* data)
	:
	inherited(data),
	fLicenseTexts(5)
{
	fInitStatus = _SetTo(data);
}


/**
 * @brief Construct a BRepositoryInfo by reading an info file.
 *
 * @param entry  The BEntry pointing to the driver-settings info file.
 */
BRepositoryInfo::BRepositoryInfo(const BEntry& entry)
{
	fInitStatus = _SetTo(entry);
}


/**
 * @brief Destroy the BRepositoryInfo.
 */
BRepositoryInfo::~BRepositoryInfo()
{
}


/**
 * @brief Instantiate a BRepositoryInfo from an archived BMessage.
 *
 * Validates the class name stored in @a data before constructing.
 *
 * @param data  The BMessage to instantiate from.
 * @return A new BRepositoryInfo on success, or NULL on failure.
 */
/*static*/ BRepositoryInfo*
BRepositoryInfo::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BPackageKit::BRepositoryInfo"))
		return new (std::nothrow) BRepositoryInfo(data);

	return NULL;
}


/**
 * @brief Serialise this info object into a BMessage for archiving or IPC.
 *
 * @param data  The BMessage to write fields into.
 * @param deep  If true, child objects are also archived (forwarded to base class).
 * @return B_OK on success, or an error code if any field cannot be added.
 */
status_t
BRepositoryInfo::Archive(BMessage* data, bool deep) const
{
	status_t result = inherited::Archive(data, deep);
	if (result != B_OK)
		return result;

	if (!fBaseURL.IsEmpty()) {
		if ((result = data->AddString(kBaseURLField, fBaseURL)) != B_OK)
			return result;
	}

	if ((result = data->AddString(kNameField, fName)) != B_OK)
		return result;
	if ((result = data->AddString(kIdentifierField, fIdentifier)) != B_OK)
		return result;
	// "url" is an older, deprecated key for "identifier"
	if ((result = data->AddString(kURLField, fIdentifier)) != B_OK)
		return result;
	if ((result = data->AddString(kVendorField, fVendor)) != B_OK)
		return result;
	if ((result = data->AddString(kSummaryField, fSummary)) != B_OK)
		return result;
	if ((result = data->AddUInt8(kPriorityField, fPriority)) != B_OK)
		return result;
	if ((result = data->AddUInt8(kArchitectureField, fArchitecture)) != B_OK)
		return result;
	for (int i = 0; i < fLicenseNames.CountStrings(); ++i) {
		result = data->AddString(kLicenseNameField, fLicenseNames.StringAt(i));
		if (result != B_OK)
			return result;
	}
	for (int i = 0; i < fLicenseTexts.CountStrings(); ++i) {
		result = data->AddString(kLicenseTextField, fLicenseTexts.StringAt(i));
		if (result != B_OK)
			return result;
	}

	return B_OK;
}


/**
 * @brief Check whether this info object has been successfully initialised.
 *
 * @return B_OK if valid, B_NO_INIT otherwise.
 */
status_t
BRepositoryInfo::InitCheck() const
{
	return fInitStatus;
}


/**
 * @brief Reinitialise from an archived BMessage.
 *
 * @param data  The BMessage to read fields from.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BRepositoryInfo::SetTo(const BMessage* data)
{
	return fInitStatus = _SetTo(data);
}


/**
 * @brief Reinitialise from a driver-settings info file on disk.
 *
 * @param entry  The BEntry pointing to the info file.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BRepositoryInfo::SetTo(const BEntry& entry)
{
	return fInitStatus = _SetTo(entry);
}


/**
 * @brief Return the display name of the repository.
 *
 * @return Reference to the name string.
 */
const BString&
BRepositoryInfo::Name() const
{
	return fName;
}


/**
 * @brief Return the base URL of the repository.
 *
 * @return Reference to the base URL string.
 */
const BString&
BRepositoryInfo::BaseURL() const
{
	return fBaseURL;
}


/**
 * @brief Return the canonical identifier URL of the repository.
 *
 * @return Reference to the identifier string.
 */
const BString&
BRepositoryInfo::Identifier() const
{
	return fIdentifier;
}


/**
 * @brief Return the vendor name of the repository.
 *
 * @return Reference to the vendor string.
 */
const BString&
BRepositoryInfo::Vendor() const
{
	return fVendor;
}


/**
 * @brief Return a short description of the repository.
 *
 * @return Reference to the summary string.
 */
const BString&
BRepositoryInfo::Summary() const
{
	return fSummary;
}


/**
 * @brief Return the download priority of the repository.
 *
 * @return Priority value; lower values indicate higher preference.
 */
uint8
BRepositoryInfo::Priority() const
{
	return fPriority;
}


/**
 * @brief Return the target CPU architecture of this repository.
 *
 * @return The BPackageArchitecture enum value.
 */
BPackageArchitecture
BRepositoryInfo::Architecture() const
{
	return fArchitecture;
}


/**
 * @brief Return the list of license names associated with this repository.
 *
 * @return Reference to the license-name string list.
 */
const BStringList&
BRepositoryInfo::LicenseNames() const
{
	return fLicenseNames;
}


/**
 * @brief Return the list of full license texts associated with this repository.
 *
 * @return Reference to the license-text string list.
 */
const BStringList&
BRepositoryInfo::LicenseTexts() const
{
	return fLicenseTexts;
}


/**
 * @brief Set the display name.
 *
 * @param name  The new name.
 */
void
BRepositoryInfo::SetName(const BString& name)
{
	fName = name;
}


/**
 * @brief Set the canonical identifier URL.
 *
 * @param identifier  The new identifier URL.
 */
void
BRepositoryInfo::SetIdentifier(const BString& identifier)
{
	fIdentifier = identifier;
}


/**
 * @brief Set the base URL.
 *
 * @param url  The new base URL.
 */
void
BRepositoryInfo::SetBaseURL(const BString& url)
{
	fBaseURL = url;
}


/**
 * @brief Set the vendor name.
 *
 * @param vendor  The new vendor string.
 */
void
BRepositoryInfo::SetVendor(const BString& vendor)
{
	fVendor = vendor;
}


/**
 * @brief Set the repository summary.
 *
 * @param summary  The new summary string.
 */
void
BRepositoryInfo::SetSummary(const BString& summary)
{
	fSummary = summary;
}


/**
 * @brief Set the download priority.
 *
 * @param priority  The new priority value.
 */
void
BRepositoryInfo::SetPriority(uint8 priority)
{
	fPriority = priority;
}


/**
 * @brief Set the target architecture.
 *
 * @param architecture  The new BPackageArchitecture value.
 */
void
BRepositoryInfo::SetArchitecture(BPackageArchitecture architecture)
{
	fArchitecture = architecture;
}


/**
 * @brief Append a license name/text pair to the repository's license list.
 *
 * @param licenseName  SPDX or display name of the license.
 * @param licenseText  Full text of the license.
 * @return B_OK on success, B_NO_MEMORY if appending to either list fails.
 */
status_t
BRepositoryInfo::AddLicense(const BString& licenseName,
	const BString& licenseText)
{
	if (!fLicenseNames.Add(licenseName) || !fLicenseTexts.Add(licenseText))
		return B_NO_MEMORY;

	return B_OK;
}


/**
 * @brief Remove all license name/text pairs from this repository info.
 */
void
BRepositoryInfo::ClearLicenses()
{
	fLicenseNames.MakeEmpty();
	fLicenseTexts.MakeEmpty();
}


/**
 * @brief Populate fields from an archived BMessage.
 *
 * @param data  The BMessage containing serialised repository info.
 * @return B_OK on success, B_BAD_VALUE if @a data is NULL, or an error
 *         code if a required field is missing or invalid.
 */
status_t
BRepositoryInfo::_SetTo(const BMessage* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t result;
	if ((result = data->FindString(kNameField, &fName)) != B_OK)
		return result;
	result = data->FindString(kIdentifierField, &fIdentifier);
	if (result == B_NAME_NOT_FOUND) {
		result = data->FindString(kURLField, &fIdentifier);
			// this is a legacy key for the identifier.
	}
	if (result != B_OK)
		return result;
	if ((result = data->FindString(kVendorField, &fVendor)) != B_OK)
		return result;
	if ((result = data->FindString(kSummaryField, &fSummary)) != B_OK)
		return result;
	if ((result = data->FindUInt8(kPriorityField, &fPriority)) != B_OK)
		return result;
	if ((result = data->FindUInt8(
			kArchitectureField, (uint8*)&fArchitecture)) != B_OK) {
		return result;
	}
	if (fArchitecture == B_PACKAGE_ARCHITECTURE_ANY)
		return B_BAD_DATA;

	// this field is optional because earlier versions did not support this
	// field.
	status_t baseUrlResult = data->FindString(kBaseURLField, &fBaseURL);
	switch (baseUrlResult) {
		case B_NAME_NOT_FOUND:
			// This is a temporary measure because older versions of the file
			// format would take the "url" (identifier) field for the "base-url"
			// Once this transitional period is over this can be removed.
			if (fIdentifier.StartsWith("http"))
				fBaseURL = fIdentifier;
			break;
		case B_OK:
			break;
		default:
			return baseUrlResult;
	}

	const char* licenseName;
	const char* licenseText;
	for (int i = 0;
		data->FindString(kLicenseNameField, i, &licenseName) == B_OK
			&& data->FindString(kLicenseTextField, i, &licenseText) == B_OK;
		++i) {
		if (!fLicenseNames.Add(licenseName) || !fLicenseTexts.Add(licenseText))
			return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Populate fields from a driver-settings info file on disk.
 *
 * Reads and parses the text file, validates mandatory fields, and populates
 * all member variables.
 *
 * @param entry  The BEntry of the info file to read.
 * @return B_OK on success, B_BAD_DATA if required fields are absent or invalid,
 *         or another error code if the file cannot be read.
 */
status_t
BRepositoryInfo::_SetTo(const BEntry& entry)
{
	BFile file(&entry, B_READ_ONLY);
	status_t result = file.InitCheck();
	if (result != B_OK)
		return result;

	off_t size;
	if ((result = file.GetSize(&size)) != B_OK)
		return result;

	BString configString;
	char* buffer = configString.LockBuffer(size);
	if (buffer == NULL)
		return B_NO_MEMORY;

	if ((result = file.Read(buffer, size)) < size) {
		configString.UnlockBuffer(0);
		return (result >= 0) ? B_IO_ERROR : result;
	}

	buffer[size] = '\0';
	configString.UnlockBuffer(size);

	DriverSettingsUnloader settingsHandle(
		parse_driver_settings_string(configString.String()));
	if (!settingsHandle.IsSet())
		return B_BAD_DATA;

	const char* name = get_driver_parameter(settingsHandle.Get(), "name", NULL,
		NULL);
	const char* identifier = get_driver_parameter(settingsHandle.Get(),
		"identifier", NULL, NULL);
	// Also handle the old name if the new one isn't found
	if (identifier == NULL || *identifier == '\0')
		identifier = get_driver_parameter(settingsHandle.Get(),
			"url", NULL, NULL);
	const char* baseUrl = get_driver_parameter(settingsHandle.Get(),
		"baseurl", NULL, NULL);
	const char* vendor = get_driver_parameter(settingsHandle.Get(),
		"vendor", NULL, NULL);
	const char* summary = get_driver_parameter(settingsHandle.Get(),
		"summary", NULL, NULL);
	const char* priorityString = get_driver_parameter(settingsHandle.Get(),
		"priority", NULL, NULL);
	const char* architectureString = get_driver_parameter(settingsHandle.Get(),
		"architecture", NULL, NULL);

	if (name == NULL || *name == '\0'
		|| identifier == NULL || *identifier == '\0'
		|| vendor == NULL || *vendor == '\0'
		|| summary == NULL || *summary == '\0'
		|| priorityString == NULL || *priorityString == '\0'
		|| architectureString == NULL || *architectureString == '\0') {
		return B_BAD_DATA;
	}

	BPackageArchitecture architecture;
	if (BPackageInfo::GetArchitectureByName(architectureString, architecture)
			!= B_OK || architecture == B_PACKAGE_ARCHITECTURE_ANY) {
		return B_BAD_DATA;
	}

	fName = name;
	fBaseURL = baseUrl;
	fIdentifier = identifier;
	fVendor = vendor;
	fSummary = summary;
	fPriority = atoi(priorityString);
	fArchitecture = architecture;

	return B_OK;
}


}	// namespace BPackageKit
