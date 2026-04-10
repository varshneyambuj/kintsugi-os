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
 *   Copyright 2002-2024, Haiku, Inc. All rights reserved.
 *   Authors: Tyler Dauwalder, Ingo Weinhold, Axel Dörfler
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file SupportingApps.cpp
 * @brief Tracks which applications support each MIME type across the database.
 *
 * SupportingApps maintains an in-memory mapping from MIME type strings to the
 * set of application signatures that declare support for that type. It is built
 * lazily on the first query by scanning all application entries in the MIME
 * database. Incremental updates are applied as types are installed or removed.
 *
 * @see Database
 */

#include <mime/SupportingApps.h>

#include <stdio.h>

#include <new>

#include <Directory.h>
#include <Message.h>
#include <MimeType.h>
#include <Path.h>
#include <String.h>

#include <mime/database_support.h>
#include <mime/DatabaseDirectory.h>
#include <mime/DatabaseLocation.h>
#include <storage_support.h>


#define DBG(x) x
//#define DBG(x)
#define OUT printf

namespace BPrivate {
namespace Storage {
namespace Mime {


/*!
	\class SupportingApps
	\brief Supporting apps information for the entire MIME database
*/


/**
 * @brief Constructs a SupportingApps object.
 *
 * @param databaseLocation Pointer to the DatabaseLocation used to read
 *                         application entries.
 */
SupportingApps::SupportingApps(DatabaseLocation* databaseLocation)
	:
	fDatabaseLocation(databaseLocation),
	fHaveDoneFullBuild(false)
{
}


/**
 * @brief Destroys the SupportingApps object.
 */
SupportingApps::~SupportingApps()
{
}


/**
 * @brief Returns the list of applications that support the given MIME type.
 *
 * The result is written into the pre-allocated BMessage @a apps. For a full
 * subtype query, both sub-count and super-count fields are populated; for a
 * supertype-only query, only the super-count field is populated. Triggers a
 * full database scan on first call.
 *
 * @param type The MIME type string.
 * @param apps Pointer to a pre-allocated BMessage that receives the result.
 * @return B_OK on success, or an error code on failure.
 */
status_t
SupportingApps::GetSupportingApps(const char *type, BMessage *apps)
{
	if (type == NULL || apps == NULL)
		return B_BAD_VALUE;

	// See if we need to do our initial build still
	if (!fHaveDoneFullBuild) {
		status_t status = BuildSupportingAppsTable();
		if (status != B_OK)
			return status;
	}

	// Clear the message, as we're just going to add to it
	apps->MakeEmpty();

	BString typeString(type);
	typeString.ToLower();

	BMimeType mime(type);
	status_t status = mime.InitCheck();
	if (status != B_OK)
		return status;

	if (mime.IsSupertypeOnly()) {
		// Add the apps that support this supertype (plus their count)
		std::set<std::string> &superApps = fSupportingApps[typeString.String()];
		int32 count = 0;
		std::set<std::string>::const_iterator i;
		for (i = superApps.begin(); i != superApps.end() && status == B_OK; i++) {
			status = apps->AddString(kApplicationsField, (*i).c_str());
			count++;
		}
		if (status == B_OK)
			status = apps->AddInt32(kSupportingAppsSuperCountField, count);
	} else {
		// Add the apps that support this subtype (plus their count)
		std::set<std::string> &subApps = fSupportingApps[typeString.String()];
		int32 count = 0;
		std::set<std::string>::const_iterator i;
		for (i = subApps.begin(); i != subApps.end() && status == B_OK; i++) {
			status = apps->AddString(kApplicationsField, (*i).c_str());
			count++;
		}
		if (status == B_OK)
			status = apps->AddInt32(kSupportingAppsSubCountField, count);

		// Now add any apps that support the supertype, but not the
		// subtype (plus their count).
		BMimeType superMime;
		status = mime.GetSupertype(&superMime);
		if (status == B_OK)
			status = superMime.InitCheck();
		if (status == B_OK) {
			std::set<std::string> &superApps = fSupportingApps[superMime.Type()];
			count = 0;
			for (i = superApps.begin(); i != superApps.end() && status == B_OK; i++) {
				if (subApps.find(*i) == subApps.end()) {
					status = apps->AddString(kApplicationsField, (*i).c_str());
					count++;
				}
			}
			if (status == B_OK)
				status = apps->AddInt32(kSupportingAppsSuperCountField, count);
		}
	}

	return status;
}


/**
 * @brief Updates the supported types for @a app and synchronises the mappings.
 *
 * Every type listed in @a types gains @a app as a supporting application. If
 * @a fullSync is true, @a app is also removed from every type it previously
 * supported but that no longer appears in @a types (including types that were
 * stranded by earlier calls with @a fullSync == false).
 *
 * @param app      Application signature whose supported types are being set.
 * @param types    BMessage whose "types" array lists the new supported types.
 * @param fullSync If true, perform a complete re-synchronisation.
 * @return B_OK on success, or an error code on failure.
 */
status_t
SupportingApps::SetSupportedTypes(const char *app, const BMessage *types, bool fullSync)
{
	if (app == NULL || types == NULL)
		return B_BAD_VALUE;

	if (!fHaveDoneFullBuild)
		return B_OK;

	std::set<std::string> oldTypes;
	std::set<std::string> &newTypes = fSupportedTypes[app];
	std::set<std::string> &strandedTypes = fStrandedTypes[app];

	// Make a copy of the previous types if we're doing a full sync
	oldTypes = newTypes;

	// Read through the list of new supported types, creating the new
	// supported types list and adding the app as a supporting app for
	// each type.
	newTypes.clear();
	BString type;
	for (int32 i = 0; types->FindString(kTypesField, i, &type) == B_OK; i++) {
		type.ToLower();
		newTypes.insert(type.String());
		AddSupportingApp(type.String(), app);
	}

	// Update the list of stranded types by removing any types that are newly
	// re-supported and adding any types that are newly un-supported
	for (std::set<std::string>::const_iterator i = newTypes.begin();
			i != newTypes.end(); i++) {
		strandedTypes.erase(*i);
	}
	for (std::set<std::string>::const_iterator i = oldTypes.begin();
			i != oldTypes.end(); i++) {
		if (newTypes.find(*i) == newTypes.end())
			strandedTypes.insert(*i);
	}

	// Now, if we're doing a full sync, remove the app as a supporting
	// app for any of its stranded types and then clear said list of
	// stranded types.
	if (fullSync) {
		for (std::set<std::string>::const_iterator i = strandedTypes.begin();
				i != strandedTypes.end(); i++) {
			RemoveSupportingApp((*i).c_str(), app);
		}
		strandedTypes.clear();
	}

	return B_OK;
}


/**
 * @brief Clears all supported types for the given application.
 *
 * Equivalent to calling SetSupportedTypes() with an empty types message.
 *
 * @param app      Application signature whose supported types are being cleared.
 * @param fullSync If true, remove @a app from each previously supported type.
 * @return B_OK on success, or an error code on failure.
 */
status_t
SupportingApps::DeleteSupportedTypes(const char *app, bool fullSync)
{
	BMessage types;
	return SetSupportedTypes(app, &types, fullSync);
}


/**
 * @brief Adds an application signature to the supporting-apps set for a type.
 *
 * @param type The full MIME type string.
 * @param app  The application signature (e.g. "application/x-vnd.foo").
 * @return B_OK on success (even if the app was already listed), or an error code.
 */
status_t
SupportingApps::AddSupportingApp(const char *type, const char *app)
{
	if (type == NULL || app == NULL)
		return B_BAD_VALUE;

	fSupportingApps[type].insert(app);
	return B_OK;
}


/**
 * @brief Removes an application signature from the supporting-apps set for a type.
 *
 * @param type The full MIME type string.
 * @param app  The application signature to remove.
 * @return B_OK on success (even if the app was not listed), or an error code.
 */
status_t
SupportingApps::RemoveSupportingApp(const char *type, const char *app)
{
	if (type == NULL || app == NULL)
		return B_BAD_VALUE;

	fSupportingApps[type].erase(app);
	return B_OK;
}


/**
 * @brief Scans the database and builds the complete supporting-apps mapping.
 *
 * Iterates over all entries under the "application" supertype, reads each
 * application's supported-types attribute, and populates the internal maps.
 * Sets fHaveDoneFullBuild to true on success.
 *
 * @return B_OK on success, or an error code on failure.
 */
status_t
SupportingApps::BuildSupportingAppsTable()
{
	fSupportedTypes.clear();
	fSupportingApps.clear();
	fStrandedTypes.clear();

	DatabaseDirectory dir;
	status_t status = dir.Init(fDatabaseLocation, "application");
	if (status != B_OK)
		return status;

	// Build the supporting apps table based on the mime database
	dir.Rewind();

	// Handle each application type
	while (true) {
		entry_ref ref;
		status = dir.GetNextRef(&ref);
		if (status < B_OK) {
			// If we've come to the end of list, it's not an error
			if (status == B_ENTRY_NOT_FOUND)
				status = B_OK;
			break;
		}

		// read application signature from file
		BString appSignature;
		BNode node(&ref);
		if (node.InitCheck() == B_OK && node.ReadAttrString(kTypeAttr,
				&appSignature) >= B_OK) {
			// Read in the list of supported types
			BMessage msg;
			if (fDatabaseLocation->ReadMessageAttribute(appSignature,
					kSupportedTypesAttr, msg) == B_OK) {
				// Iterate through the supported types, adding them to the list of
				// supported types for the application and adding the application's
				// signature to the list of supporting apps for each type
				BString type;
				std::set<std::string> &supportedTypes = fSupportedTypes[appSignature.String()];
				for (int i = 0; msg.FindString(kTypesField, i, &type) == B_OK; i++) {
					type.ToLower();
						// MIME types are case insensitive, so we lowercase everything
					supportedTypes.insert(type.String());
					AddSupportingApp(type.String(), appSignature.String());
				}
			}
		}
	}

	if (status == B_OK)
		fHaveDoneFullBuild = true;
	else
		DBG(OUT("SupportingApps::BuildSupportingAppsTable() failed: %s\n", strerror(status)));

	return status;
}


} // namespace Mime
} // namespace Storage
} // namespace BPrivate
