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

/** @file KeyboardSettings.h
 *  @brief Persisted keyboard repeat-rate and delay settings used by the input server. */

#ifndef KEYBOARD_SETTINGS_H_
#define KEYBOARD_SETTINGS_H_

#include <SupportDefs.h>
#include <kb_mouse_settings.h>

/** @brief Wrapper around the on-disk keyboard settings file. */
class KeyboardSettings
{
public :
	/** @brief Constructs the settings object and loads the on-disk file. */
				KeyboardSettings();
				~KeyboardSettings();

	/** @brief Updates the cached key-repeat rate (call Save() to persist). */
	void		SetKeyboardRepeatRate(int32 rate);
	/** @brief Updates the cached key-repeat delay (call Save() to persist). */
	void		SetKeyboardRepeatDelay(bigtime_t delay);

	/** @brief Returns the cached key-repeat rate in characters per second. */
	int32		KeyboardRepeatRate() const		{ return fSettings.key_repeat_rate; }
	/** @brief Returns the cached key-repeat delay in microseconds. */
	bigtime_t	KeyboardRepeatDelay() const 	{ return fSettings.key_repeat_delay; }

	/** @brief Writes the cached settings to disk. */
	void 		Save();

private:
	kb_settings			fSettings;  /**< In-memory copy of the on-disk keyboard settings. */
};

#endif
