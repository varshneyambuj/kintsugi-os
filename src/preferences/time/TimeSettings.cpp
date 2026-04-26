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
 *   Copyright 2002-2007, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Andrew McCall, mccall@digitalparadise.co.uk
 *       Mike Berg <mike@berg-net.us>
 *       Julun <host.haiku@gmx.de>
 */


/**
 * @file TimeSettings.cpp
 * @brief Persists the preference window's screen position between launches.
 *
 * Uses a small binary file in B_USER_SETTINGS_DIRECTORY containing a single
 * BPoint that records the window's last left-top corner.
 */


#include "TimeSettings.h"
#include "TimeMessages.h"


#include <File.h>
#include <FindDirectory.h>
#include <Path.h>


/**
 * @brief Constructs a settings object bound to the canonical filename.
 */
TimeSettings::TimeSettings()
	:
	fSettingsFile("Time_preflet_window")
{
}


/**
 * @brief Destructor; nothing to release.
 */
TimeSettings::~TimeSettings()
{
}


/**
 * @brief Reads the saved window position from the user settings directory.
 *
 * @return The saved BPoint, or (-1000, -1000) when the file is missing or
 *         unreadable. The sentinel encourages the caller to use a default
 *         placement.
 */
BPoint
TimeSettings::LeftTop() const
{
	BPath path;
	BPoint leftTop(-1000.0, -1000.0);

	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append(fSettingsFile.String());

		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() == B_OK) {
			BPoint tmp;
			if (file.Read(&tmp, sizeof(BPoint)) == sizeof(BPoint))
				leftTop = tmp;
		}
	}

	return leftTop;
}


/**
 * @brief Writes the current window position to the user settings directory.
 *
 * Errors are ignored because the value is non-essential.
 *
 * @param leftTop New top-left corner to persist.
 */
void
TimeSettings::SetLeftTop(const BPoint leftTop)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;

	path.Append(fSettingsFile.String());

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE);
	if (file.InitCheck() == B_OK)
		file.Write(&leftTop, sizeof(BPoint));
}

