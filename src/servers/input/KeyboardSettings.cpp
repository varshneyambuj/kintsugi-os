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
 *   Copyright 2004-2006, the Haiku project. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors in chronological order:
 *       mccall@digitalparadise.co.uk
 *       Jérôme Duval
 *       Marcus Overhagen
 */

/** @file KeyboardSettings.cpp
 *  @brief Persistence implementation for keyboard repeat-rate and delay settings. */

#include <FindDirectory.h>
#include <File.h>
#include <Path.h>
#include "KeyboardSettings.h"

/**
 * @brief Constructs the keyboard settings by loading persisted values from disk.
 *
 * Reads the keyboard repeat rate and delay from the user settings file.
 * Falls back to compiled-in defaults if the file cannot be found, opened,
 * or read.
 */
KeyboardSettings::KeyboardSettings()
{
	BPath path;
	BFile file;
	
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) < B_OK)
		goto err;
	if (path.Append(kb_settings_file) < B_OK)
		goto err;
	if (file.SetTo(path.Path(), B_READ_ONLY) < B_OK)
		goto err;
	if (file.Read(&fSettings, sizeof(kb_settings)) != sizeof(kb_settings))
		goto err;
		
	return;
err:
	fSettings.key_repeat_delay = kb_default_key_repeat_delay;
	fSettings.key_repeat_rate  = kb_default_key_repeat_rate;
}


/** @brief Destructor. */
KeyboardSettings::~KeyboardSettings()
{
}


/**
 * @brief Sets the keyboard repeat rate and persists the change to disk.
 *
 * @param rate The new repeat rate in characters per second.
 */
void
KeyboardSettings::SetKeyboardRepeatRate(int32 rate)
{
	fSettings.key_repeat_rate = rate;
	Save();
}


/**
 * @brief Sets the keyboard repeat delay and persists the change to disk.
 *
 * @param delay The new repeat delay in microseconds before the first repeat.
 */
void
KeyboardSettings::SetKeyboardRepeatDelay(bigtime_t delay)
{
	fSettings.key_repeat_delay = delay;
	Save();
}


/**
 * @brief Writes the current keyboard settings to the user settings file.
 *
 * Silently returns on any filesystem error without propagating the failure.
 */
void
KeyboardSettings::Save()
{
	BPath path;
	BFile file;
	
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) < B_OK)
		return;
	if (path.Append(kb_settings_file) < B_OK)
		return;
	if (file.SetTo(path.Path(), B_WRITE_ONLY | B_CREATE_FILE) < B_OK)
		return;
	
	file.Write(&fSettings, sizeof(kb_settings));
}
