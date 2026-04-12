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
 * @file RepositoryConfig.cpp
 * @brief Implementation of BRepositoryConfig, the persistent repository settings object.
 *
 * BRepositoryConfig reads and writes the driver-settings-format configuration
 * file that describes a single package repository: its display name, base URL,
 * canonical identifier, and download priority. Two storage format versions are
 * supported; a migration table maps legacy identifier URLs to their canonical
 * replacements.
 *
 * @see BRepositoryCache, BPackageRoster
 */


#include <package/RepositoryConfig.h>

#include <stdlib.h>

#include <new>

#include <Directory.h>
#include <driver_settings.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include <DriverSettings.h>


#define STORAGE_VERSION 2

#define KEY_BASE_URL_V1 "url"
#define KEY_IDENTIFIER_V1 "url"

#define KEY_BASE_URL_V2 "baseurl"
#define KEY_IDENTIFIER_V2 "identifier"
#define KEY_IDENTIFIER_V2_ALT "url"
	// should not be used any more in favour of 'identifier'

#define KEY_PRIORITY "priority"
#define KEY_CONFIG_VERSION "cfgversion"

namespace BPackageKit {


// these are mappings of known legacy identifier URLs that are possibly
// still present in some installations.  These are in pairs; the first
// being the legacy URL and the next being the replacement.  This can
// be phased out over time.

static const char* kLegacyUrlMappings[] = {
	"https://eu.hpkg.haiku-os.org/haikuports/master/x86_gcc2/current",
	"https://hpkg.haiku-os.org/haikuports/master/x86_gcc2/current",
	"https://eu.hpkg.haiku-os.org/haikuports/master/x86_64/current",
	"https://hpkg.haiku-os.org/haikuports/master/x86_64/current",
	NULL,
	NULL
};


/**
 * @brief Default-construct an uninitialised BRepositoryConfig.
 */
BRepositoryConfig::BRepositoryConfig()
	:
	fInitStatus(B_NO_INIT),
	fPriority(kUnsetPriority),
	fIsUserSpecific(false)
{
}


/**
 * @brief Construct a BRepositoryConfig by loading from a filesystem entry.
 *
 * @param entry  The BEntry pointing to the configuration file.
 */
BRepositoryConfig::BRepositoryConfig(const BEntry& entry)
{
	SetTo(entry);
}


/**
 * @brief Construct a BRepositoryConfig from explicit field values.
 *
 * @param name      Display name of the repository.
 * @param baseURL   Base URL where packages can be fetched.
 * @param priority  Download priority; lower values are preferred.
 */
BRepositoryConfig::BRepositoryConfig(const BString& name,
	const BString& baseURL, uint8 priority)
	:
	fInitStatus(B_OK),
	fName(name),
	fBaseURL(baseURL),
	fPriority(priority),
	fIsUserSpecific(false)
{
}


/**
 * @brief Destroy the BRepositoryConfig.
 */
BRepositoryConfig::~BRepositoryConfig()
{
}


/**
 * @brief Write this configuration to a driver-settings file.
 *
 * Creates or overwrites the file at @a entry with the version-2 format,
 * including base URL, identifier, and priority.
 *
 * @param entry  The BEntry designating the output file.
 * @return B_OK on success, or an error code if the file cannot be written.
 */
status_t
BRepositoryConfig::Store(const BEntry& entry) const
{
	BFile file(&entry, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t result = file.InitCheck();
	if (result != B_OK)
		return result;

	BString configString;
	configString << KEY_CONFIG_VERSION << "=" << STORAGE_VERSION << "\n";
	configString << "\n";
	configString << "# the url where the repository data can be "
		"accessed.\n";
	configString << KEY_BASE_URL_V2 << "=" << fBaseURL << "\n";
	configString << "\n";

	configString << "# an identifier for the repository that is "
		"consistent across mirrors\n";
	if (fIdentifier.IsEmpty())
		configString << "# " << KEY_IDENTIFIER_V2 << "=???\n";
	else
		configString << KEY_IDENTIFIER_V2 << "=" << fIdentifier << "\n";
	configString << "\n";

	configString << "# a deprecated copy of the ["
		<< KEY_IDENTIFIER_V2 << "] key above for older os versions\n";
	if (fIdentifier.IsEmpty())
		configString << "# " << KEY_IDENTIFIER_V2_ALT << "=???\n";
	else
		configString << KEY_IDENTIFIER_V2_ALT << "=" << fIdentifier << "\n";
	configString << "\n";

	configString << KEY_PRIORITY << "=" << fPriority << "\n";

	int32 size = configString.Length();
	if ((result = file.Write(configString.String(), size)) < size)
		return (result >= 0) ? B_ERROR : result;

	return B_OK;
}


/**
 * @brief Check whether this config has been successfully initialised.
 *
 * @return B_OK if valid, B_NO_INIT otherwise.
 */
status_t
BRepositoryConfig::InitCheck() const
{
	return fInitStatus;
}


/**
 * @brief Translate a legacy v1 identifier URL to its canonical replacement.
 *
 * Scans the kLegacyUrlMappings table and returns the replacement when found;
 * otherwise returns the original identifier unchanged.
 *
 * @param identifier  The identifier URL to look up.
 * @return The canonical identifier URL.
 */
static const char*
repository_config_swap_legacy_identifier_v1(const char* identifier)
{
	for (int32 i = 0; kLegacyUrlMappings[i] != NULL; i += 2) {
		if (strcmp(identifier, kLegacyUrlMappings[i]) == 0)
			return kLegacyUrlMappings[i + 1];
	}
	return identifier;
}


/**
 * @brief Load the repository configuration from a driver-settings file.
 *
 * Supports both version-1 and version-2 storage formats. The fIsUserSpecific
 * flag is derived from whether the entry lives under the user settings directory.
 *
 * @param entry  The BEntry pointing to the configuration file.
 * @return B_OK on success, B_BAD_DATA if the file format is invalid, or another
 *         error code on failure.
 */
status_t
BRepositoryConfig::SetTo(const BEntry& entry)
{
	fEntry = entry;
	fInitStatus = B_NO_INIT;

	entry_ref ref;
	status_t result = entry.GetRef(&ref);
	if (result != B_OK)
		return result;

	BDriverSettings driverSettings;
	result = driverSettings.Load(ref);
	if (result != B_OK)
		return result;

	const char* version = driverSettings.GetParameterValue(KEY_CONFIG_VERSION);
	int versionNumber = version == NULL ? 1 : atoi(version);
	const char *baseUrlKey;
	const char *identifierKeys[3] = { NULL, NULL, NULL };

	switch (versionNumber) {
		case 1:
			baseUrlKey = KEY_BASE_URL_V1;
			identifierKeys[0] = KEY_IDENTIFIER_V1;
			break;
		case 2:
			baseUrlKey = KEY_BASE_URL_V2;
			identifierKeys[0] = KEY_IDENTIFIER_V2;
			identifierKeys[1] = KEY_IDENTIFIER_V2_ALT;
			break;
		default:
			return B_BAD_DATA;
	}

	const char* baseUrl = driverSettings.GetParameterValue(baseUrlKey);
	const char* priorityString = driverSettings.GetParameterValue(KEY_PRIORITY);
	const char* identifier = NULL;

	for (int32 i = 0; identifier == NULL && identifierKeys[i] != NULL; i++)
		identifier = driverSettings.GetParameterValue(identifierKeys[i]);

	if (baseUrl == NULL || *baseUrl == '\0')
		return B_BAD_DATA;
	if (identifier == NULL || *identifier == '\0')
		return B_BAD_DATA;

	if (versionNumber == 1)
		identifier = repository_config_swap_legacy_identifier_v1(identifier);

	fName = entry.Name();
	fBaseURL = baseUrl;
	fPriority = priorityString == NULL
		? kUnsetPriority : atoi(priorityString);
	fIdentifier = identifier;

	BPath userSettingsPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &userSettingsPath) == B_OK) {
		BDirectory userSettingsDir(userSettingsPath.Path());
		fIsUserSpecific = userSettingsDir.Contains(&entry);
	} else
		fIsUserSpecific = false;

	fInitStatus = B_OK;

	return B_OK;
}


/**
 * @brief Return the display name of the repository.
 *
 * @return Reference to the name string.
 */
const BString&
BRepositoryConfig::Name() const
{
	return fName;
}


/**
 * @brief Return the base URL of the repository.
 *
 * @return Reference to the base URL string.
 */
const BString&
BRepositoryConfig::BaseURL() const
{
	return fBaseURL;
}


/**
 * @brief Return the canonical identifier URL of the repository.
 *
 * @return Reference to the identifier string.
 */
const BString&
BRepositoryConfig::Identifier() const
{
	return fIdentifier;
}


/**
 * @brief Return the download priority of the repository.
 *
 * @return Priority value; lower means higher preference.
 */
uint8
BRepositoryConfig::Priority() const
{
	return fPriority;
}


/**
 * @brief Check whether this configuration belongs to the per-user installation.
 *
 * @return True if the config file lives under the user settings directory.
 */
bool
BRepositoryConfig::IsUserSpecific() const
{
	return fIsUserSpecific;
}


/**
 * @brief Return the filesystem entry of the configuration file.
 *
 * @return Reference to the BEntry for the config file.
 */
const BEntry&
BRepositoryConfig::Entry() const
{
	return fEntry;
}


/**
 * @brief Derive and return the URL for fetching package files.
 *
 * @return A string of the form "baseURL/packages", or an empty string if
 *         the base URL has not been set.
 */
BString
BRepositoryConfig::PackagesURL() const
{
	if (fBaseURL.IsEmpty())
		return BString();
	return BString().SetToFormat("%s/packages", fBaseURL.String());
}


/**
 * @brief Set the display name of the repository.
 *
 * @param name  The new name.
 */
void
BRepositoryConfig::SetName(const BString& name)
{
	fName = name;
}


/**
 * @brief Set the base URL of the repository.
 *
 * @param baseURL  The new base URL.
 */
void
BRepositoryConfig::SetBaseURL(const BString& baseURL)
{
	fBaseURL = baseURL;
}


/**
 * @brief Set the canonical identifier URL of the repository.
 *
 * @param identifier  The new identifier URL.
 */
void
BRepositoryConfig::SetIdentifier(const BString& identifier)
{
	fIdentifier = identifier;
}


/**
 * @brief Set the download priority of the repository.
 *
 * @param priority  The new priority value.
 */
void
BRepositoryConfig::SetPriority(uint8 priority)
{
	fPriority = priority;
}


/**
 * @brief Mark this configuration as user-specific or system-wide.
 *
 * @param isUserSpecific  True for user-specific, false for system-wide.
 */
void
BRepositoryConfig::SetIsUserSpecific(bool isUserSpecific)
{
	fIsUserSpecific = isUserSpecific;
}


}	// namespace BPackageKit
