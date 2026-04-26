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
 *   Copyright 2001-2015, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Rafael Romo
 *       Stefano Ceccherini (burton666@libero.it)
 *       Axel Doerfler, axeld@pinc-software.de
 */


/**
 * @file ScreenSettings.cpp
 * @brief Persists the Screen preferences window frame between sessions.
 */


#include "ScreenSettings.h"

#include <File.h>
#include <FindDirectory.h>
#include <Path.h>


/** @brief File name (in the user settings dir) holding the window position. */
static const char* kSettingsFileName = "Screen_data";


/**
 * @brief Initialize the window frame from disk, falling back to defaults.
 *
 * Reads a single @c BPoint offset from the user settings file and applies
 * it to the default 450x250 frame. If the file is missing the default
 * frame at @c {0, 0, 450, 250} is used.
 */
ScreenSettings::ScreenSettings()
{
	fWindowFrame.Set(0, 0, 450, 250);
	BPoint offset;

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append(kSettingsFileName);

		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() == B_OK)
			file.Read(&offset, sizeof(BPoint));
	}

	fWindowFrame.OffsetBy(offset);
}


/**
 * @brief Persist the current window frame on shutdown.
 *
 * Writes the top-left corner of @c fWindowFrame to the settings file so
 * that the next launch restores the window's position.
 */
ScreenSettings::~ScreenSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) < B_OK)
		return;

	path.Append(kSettingsFileName);

	BPoint offset = fWindowFrame.LeftTop();

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE);
	if (file.InitCheck() == B_OK)
		file.Write(&offset, sizeof(BPoint));
}


/**
 * @brief Update the cached window frame.
 *
 * The new frame is not written to disk until the destructor runs.
 *
 * @param frame New window frame, in screen coordinates.
 */
void
ScreenSettings::SetWindowFrame(BRect frame)
{
	fWindowFrame = frame;
}
