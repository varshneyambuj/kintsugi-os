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
 *   Copyright 2002-2010, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Oliver Siebenmarck
 *       Axel Dörfler
 */


/**
 * @file DataTranslationsSettings.cpp
 * @brief Persistence of the DataTranslations window position across runs.
 *
 * Reads and writes a flattened BMessage in the user settings directory so the
 * preferences window reopens at the location it last had on screen.
 */


#include "DataTranslationsSettings.h"

#include <stdio.h>

#include <Application.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>


/** @brief Process-wide singleton instance returned by Instance(). */
static DataTranslationsSettings sDataTranslationsSettings;


/**
 * @brief Loads the persisted window corner from the user settings file.
 *
 * Falls back to BPoint(-1, -1), which the window treats as "use default
 * placement", when the settings file is missing or unreadable.
 */
DataTranslationsSettings::DataTranslationsSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;

	fCorner = BPoint(-1, -1);

	path.Append("system/DataTranslations settings");
	BFile file(path.Path(), B_READ_ONLY);
	BMessage settings;

	if (file.InitCheck() == B_OK
		&& settings.Unflatten(&file) == B_OK) {
		BPoint corner;
		if (settings.FindPoint("window corner", &corner) == B_OK)
			fCorner = corner;
	}
}


/**
 * @brief Persists the current window corner to the settings file on shutdown.
 *
 * Silently ignores failures to locate or open the settings directory.
 */
DataTranslationsSettings::~DataTranslationsSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) < B_OK)
		return;

	BMessage settings;
	settings.AddPoint("window corner", fCorner);

	path.Append("system/DataTranslations settings");
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		settings.Flatten(&file);
}


/**
 * @brief Records the new top-left window corner to be saved on exit.
 *
 * @param corner  Screen-space coordinate of the window's left-top corner.
 */
void
DataTranslationsSettings::SetWindowCorner(BPoint corner)
{
	fCorner = corner;
}


/**
 * @brief Returns the process-wide singleton settings instance.
 *
 * @return Pointer to the shared DataTranslationsSettings; never NULL.
 */
DataTranslationsSettings*
DataTranslationsSettings::Instance()
{
	return &sDataTranslationsSettings;
}
