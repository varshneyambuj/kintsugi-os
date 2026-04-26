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
 *   Copyright 2017 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Brian Hill
 */


/**
 * @file RepositoriesSettings.cpp
 * @brief Persistent settings store for the Repositories preflet.
 *
 * Reads and writes a flattened BMessage at
 * `~/config/settings/Repositories_settings`. The stored settings cover the
 * window frame and the manually managed list of repository name/URL pairs
 * that should appear in the list even when not currently enabled in pkgman.
 */


#include "RepositoriesSettings.h"

#include <FindDirectory.h>
#include <StringList.h>

#include "constants.h"

/** @brief Filename, relative to user settings dir, holding the preflet's
 *         flattened BMessage. */
const char* settingsFilename = "Repositories_settings";


/**
 * @brief Builds the absolute path to the settings file under the user
 *        settings directory.
 *
 * If the user settings directory cannot be resolved or appended to, the
 * resulting status is stored in fInitStatus; subsequent reads and writes
 * will then fail at the file-open step.
 */
RepositoriesSettings::RepositoriesSettings()
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &fFilePath);
	if (status == B_OK)
		status = fFilePath.Append(settingsFilename);
	fInitStatus = status;
}


/**
 * @brief Returns the saved window frame, or a default off-screen rectangle
 *        when no frame has been persisted.
 *
 * The default frame is intentionally placed off-screen so the window will
 * recenter itself the first time the preflet runs.
 *
 * @return Persisted window frame, or the default fall-back rectangle.
 */
BRect
RepositoriesSettings::GetFrame()
{
	BMessage settings(_ReadFromFile());
	BRect frame;
	status_t status = settings.FindRect(key_frame, &frame);
	// Set default off screen so it will center itself
	if (status != B_OK)
		frame.Set(-10, -10, 750, 300);
	return frame;
}


/**
 * @brief Persists the supplied window frame, replacing any previous value.
 *
 * @param frame Current window frame to remember for the next launch.
 */
void
RepositoriesSettings::SetFrame(BRect frame)
{
	BMessage settings(_ReadFromFile());
	settings.RemoveData(key_frame);
	settings.AddRect(key_frame, frame);
	_SaveToFile(settings);
}


/**
 * @brief Loads the persisted list of repository name/URL pairs.
 *
 * Iterates over the parallel name and URL string fields in the settings
 * message and appends matching pairs to @a nameList and @a urlList. Entries
 * for which either lookup fails are skipped and the function reports
 * B_ERROR while still returning whatever was successfully read.
 *
 * @param[out] repoCount Number of name/URL pairs appended to the lists.
 * @param[out] nameList  Receives repository names in stored order.
 * @param[out] urlList   Receives repository URLs in matching order.
 * @return B_OK if every stored entry was read; B_ERROR otherwise.
 */
status_t
RepositoriesSettings::GetRepositories(int32& repoCount, BStringList& nameList,
	BStringList& urlList)
{
	BMessage settings(_ReadFromFile());
	type_code type;
	int32 count;
	settings.GetInfo(key_name, &type, &count);

	status_t result = B_OK;
	int32 index, total = 0;
	BString foundName, foundUrl;
	// get each repository and add to lists
	for (index = 0; index < count; index++) {
		status_t result1 = settings.FindString(key_name, index, &foundName);
		status_t result2 = settings.FindString(key_url, index, &foundUrl);
		if (result1 == B_OK && result2 == B_OK) {
			nameList.Add(foundName);
			urlList.Add(foundUrl);
			total++;
		} else
			result = B_ERROR;
	}
	repoCount = total;
	return result;
}


/**
 * @brief Replaces the persisted repository list with the supplied pairs.
 *
 * Removes any existing name and URL fields from the on-disk settings
 * message and writes the new lists in lock-step.
 *
 * @param nameList Repository names in display order.
 * @param urlList  Repository URLs in matching order.
 */
void
RepositoriesSettings::SetRepositories(BStringList& nameList, BStringList& urlList)
{
	BMessage settings(_ReadFromFile());
	settings.RemoveName(key_name);
	settings.RemoveName(key_url);

	int32 index, count = nameList.CountStrings();
	for (index = 0; index < count; index++) {
		settings.AddString(key_name, nameList.StringAt(index));
		settings.AddString(key_url, urlList.StringAt(index));
	}
	_SaveToFile(settings);
}


/**
 * @brief Reads and unflattens the settings message from disk.
 *
 * @return Unflattened settings message; an empty message is returned when
 *         the file is missing or fails to parse.
 */
BMessage
RepositoriesSettings::_ReadFromFile()
{
	BMessage settings;
	status_t status = fFile.SetTo(fFilePath.Path(), B_READ_ONLY);
	if (status == B_OK)
		status = settings.Unflatten(&fFile);
	fFile.Unset();
	return settings;
}


/**
 * @brief Flattens the supplied settings message and writes it to disk.
 *
 * Truncates and rewrites the entire file on each call.
 *
 * @param settings Settings message to persist.
 * @return B_OK on success; otherwise the underlying BFile or flatten error.
 */
status_t
RepositoriesSettings::_SaveToFile(BMessage settings)
{
	status_t status = fFile.SetTo(fFilePath.Path(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (status == B_OK)
		status = settings.Flatten(&fFile);
	fFile.Unset();
	return status;
}
