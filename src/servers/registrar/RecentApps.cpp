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
   Copyright 2001-2009, Haiku Inc.
   Distributed under the terms of the MIT License.

   Authors:
   Tyler Dauwalder
   Ingo Weinhold, bonefish@users.sf.net
   Axel Dörfler, axeld@pinc-software.de
 */

/** @file RecentApps.cpp
 *  @brief Tracks and persists the list of recently launched applications. */

#include "RecentApps.h"

#include <tracker_private.h>

#include <AppFileInfo.h>
#include <Entry.h>
#include <File.h>
#include <Message.h>
#include <Mime.h>
#include <Roster.h>
#include <storage_support.h>

#include <strings.h>

#include "Debug.h"


/** @brief Constructs an empty RecentApps list. */
RecentApps::RecentApps()
{
}


/** @brief Destroys the RecentApps list. */
RecentApps::~RecentApps()
{
}


/**
 * @brief Adds the application with the given signature to the front of the list.
 *
 * Any previous entry with the same signature is removed first so only one
 * instance exists. Background apps, argv-only apps, Tracker, and Deskbar
 * are silently excluded.
 *
 * @param appSig   The application's MIME signature.
 * @param appFlags The application's launch flags.
 * @return @c B_OK on success (including when the app is excluded by flags),
 *         @c B_BAD_VALUE if @a appSig is NULL, or @c B_NO_MEMORY.
 */
status_t
RecentApps::Add(const char *appSig, int32 appFlags)
{
	if (appSig == NULL)
		return B_BAD_VALUE;

	// don't add background apps, as well as Tracker and Deskbar to the list
	// of recent apps
	if (!strcasecmp(appSig, kTrackerSignature)
		|| !strcasecmp(appSig, kDeskbarSignature)
		|| (appFlags & (B_ARGV_ONLY | B_BACKGROUND_APP)) != 0)
		return B_OK;

	// Remove any previous instance
	std::list<std::string>::iterator i;
	for (i = fAppList.begin(); i != fAppList.end(); i++) {
		if (!strcasecmp((*i).c_str(), appSig)) {
			fAppList.erase(i);
			break;
		}
	}

	try {
		// Add to the front
		fAppList.push_front(appSig);
	} catch (...) {
		return B_NO_MEMORY;
	}

	int32 remove = fAppList.size() - kMaxRecentApps;
	while (remove > 0) {
		fAppList.pop_back();
		remove--;
	}

	return B_OK;
}


/**
 * @brief Adds the application at the given entry_ref to the recent apps list.
 *
 * Reads the application's MIME signature from its BEOS:APP_SIG attribute
 * or resources, then delegates to Add(const char*, int32).
 *
 * @param ref      Entry ref pointing to the application.
 * @param appFlags The application's launch flags.
 * @return @c B_OK on success, @c B_BAD_VALUE if @a ref is NULL, or an error
 *         code if the signature cannot be determined.
 */
status_t
RecentApps::Add(const entry_ref *ref, int32 appFlags)
{
	if (ref == NULL)
		return B_BAD_VALUE;

	BFile file;
	BAppFileInfo info;
	char signature[B_MIME_TYPE_LENGTH];

	status_t err = file.SetTo(ref, B_READ_ONLY);
	if (!err)
		err = info.SetTo(&file);
	if (!err)
		err = info.GetSignature(signature);
	if (!err)
		err = Add(signature, appFlags);
	return err;
}


/**
 * @brief Retrieves entry_refs for the most recently launched applications.
 *
 * The output message is cleared and filled with up to @a maxCount entry_refs
 * stored in the "refs" field.
 *
 * @param maxCount Maximum number of recent apps to return.
 * @param list     Output message to populate with "refs" entries.
 * @return @c B_OK on success, @c B_BAD_VALUE if @a list is NULL.
 */
status_t
RecentApps::Get(int32 maxCount, BMessage *list)
{
	if (list == NULL)
		return B_BAD_VALUE;

	// Clear
	list->MakeEmpty();

	// Fill
	std::list<std::string>::iterator item;
	status_t status = B_OK;
	int counter = 0;
	for (item = fAppList.begin();
			status == B_OK && counter < maxCount && item != fAppList.end();
			counter++, item++) {
		entry_ref ref;
		if (GetRefForApp(item->c_str(), &ref) == B_OK)
			status = list->AddRef("refs", &ref);
		else {
			D(PRINT("WARNING: RecentApps::Get(): No ref found for app '%s'\n",
				item->c_str()));
		}
	}

	return status;
}


/**
 * @brief Clears the list of recently launched applications.
 *
 * @return @c B_OK always.
 */
status_t
RecentApps::Clear()
{
	fAppList.clear();
	return B_OK;
}


/**
 * @brief Prints the current recent apps list to stdout for debugging.
 *
 * @return @c B_OK always.
 */
status_t
RecentApps::Print()
{
	std::list<std::string>::iterator item;
	int counter = 1;
	for (item = fAppList.begin(); item != fAppList.end(); item++) {
		printf("%d: '%s'\n", counter++, item->c_str());
	}
	return B_OK;
}


/**
 * @brief Writes the recent apps list to the given file stream.
 *
 * Each entry is written as a "RecentApp <signature>" line.
 *
 * @param file The output file stream.
 * @return @c B_OK on success, @c B_BAD_VALUE if @a file is NULL.
 */
status_t
RecentApps::Save(FILE* file)
{
	status_t error = file ? B_OK : B_BAD_VALUE;
	if (!error) {
		fprintf(file, "# Recent applications\n");
		std::list<std::string>::iterator item;
		for (item = fAppList.begin(); item != fAppList.end(); item++) {
			fprintf(file, "RecentApp %s\n", item->c_str());
		}
		fprintf(file, "\n");
	}
	return error;
}


/**
 * @brief Resolves an application signature to an entry_ref via the MIME database.
 *
 * Looks up the app hint attribute for the given MIME type to obtain a file
 * system reference to the application.
 *
 * @param appSig The application's MIME signature.
 * @param result Output parameter receiving the resolved entry_ref.
 * @return @c B_OK on success, @c B_BAD_VALUE if either argument is NULL,
 *         or an error code if the app hint cannot be found.
 */
status_t
RecentApps::GetRefForApp(const char *appSig, entry_ref *result)
{
	if (appSig == NULL || result == NULL)
		return B_BAD_VALUE;

	// We'll use BMimeType to check for the app hint, since I'm lazy
	// and Ingo's on vacation :-P :-)
	BMimeType mime(appSig);
	status_t err = mime.InitCheck();
	if (!err)
		err = mime.GetAppHint(result);
	return err;
}

