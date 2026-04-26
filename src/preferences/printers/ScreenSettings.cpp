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
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file ScreenSettings.cpp
 * @brief Persists the Printers window position between sessions.
 *
 * The on-disk format is a single BPoint (offset from a default frame)
 * stored in the user settings directory under @c
 * Print_preflet_Screen_data.
 */


#include "ScreenSettings.h"

#include <File.h>
#include <FindDirectory.h>
#include <Path.h>


/** @brief File name (under B_USER_SETTINGS_DIRECTORY) holding the saved
    window offset. */
static const char* kSettingsFileName = "Print_preflet_Screen_data";


/**
 * @brief Loads the saved window offset and applies it to a default frame.
 *
 * The default frame is 450x250 at the origin; the offset stored in the
 * settings file is applied via OffsetBy() so the window appears where the
 * user last left it.
 */
ScreenSettings::ScreenSettings()
{
	fWindowFrame.Set(0, 0, 450, 250);
	BPoint offset(-1000, -1000);

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
 * @brief Persists the current window offset to disk.
 *
 * Called when the Printers application is shutting down. Failures are
 * silent: a missing settings directory or write error simply means the
 * next launch starts at the default position.
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
 * @brief Updates the cached window frame.
 *
 * The new value is written to disk by the destructor; callers do not
 * need to flush manually.
 *
 * @param frame New BRect to remember.
 */
void
ScreenSettings::SetWindowFrame(BRect frame)
{
	fWindowFrame = frame;
}
