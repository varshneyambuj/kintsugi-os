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


/**
 * @file KeyboardSettings.cpp
 * @brief Implementation of KeyboardSettings, the keyboard preference model.
 *
 * Wraps the kit's get_/set_key_repeat_rate and get_/set_key_repeat_delay
 * helpers behind a small object that snapshots the on-entry values for
 * Revert() and exposes IsDefaultable() to drive the Defaults button.
 */


#include "KeyboardSettings.h"

#include <stdio.h>


/**
 * @brief Constructs the model and records the on-entry values.
 *
 * Reads the current key repeat rate and delay through the kit. Missing
 * values fall back to kb_default_key_repeat_rate / _delay. The current
 * settings are also snapshotted into fOriginalSettings so Revert() can
 * later restore them.
 */
KeyboardSettings::KeyboardSettings()
{
	if (get_key_repeat_rate(&fSettings.key_repeat_rate) != B_OK)
		fSettings.key_repeat_rate = kb_default_key_repeat_rate;

	if (get_key_repeat_delay(&fSettings.key_repeat_delay) != B_OK)
		fSettings.key_repeat_delay = kb_default_key_repeat_delay;

	fOriginalSettings = fSettings;
}


/**
 * @brief Destroys the model.
 *
 * @note Does not save the settings; the kit's set_key_repeat_* helpers
 *       have already pushed every change to the input server.
 */
KeyboardSettings::~KeyboardSettings()
{
}


/**
 * @brief Updates the live keyboard repeat rate.
 *
 * Pushes @a rate through set_key_repeat_rate; on success the cached
 * value is updated. Failures are reported to stderr without altering the
 * cached state.
 *
 * @param rate  New repeat rate in characters per second.
 */
void
KeyboardSettings::SetKeyboardRepeatRate(int32 rate)
{
	if (set_key_repeat_rate(rate) != B_OK)
		fprintf(stderr, "error while set_key_repeat_rate!\n");
	fSettings.key_repeat_rate = rate;
}


/**
 * @brief Updates the live key repeat delay.
 *
 * Pushes @a delay through set_key_repeat_delay; on success the cached
 * value is updated. Failures are reported to stderr without altering the
 * cached state.
 *
 * @param delay  New repeat delay in microseconds.
 */
void
KeyboardSettings::SetKeyboardRepeatDelay(bigtime_t delay)
{
	if (set_key_repeat_delay(delay) != B_OK)
		fprintf(stderr, "error while set_key_repeat_delay!\n");
	fSettings.key_repeat_delay = delay;
}


/**
 * @brief Restores the values present when the model was constructed.
 *
 * Calls SetKeyboardRepeatDelay/Rate with the on-entry snapshot so the
 * live keyboard reflects the change immediately.
 */
void
KeyboardSettings::Revert()
{
	SetKeyboardRepeatDelay(fOriginalSettings.key_repeat_delay);
	SetKeyboardRepeatRate(fOriginalSettings.key_repeat_rate);
}


/**
 * @brief Resets the keyboard to the kit's defaults.
 *
 * Pushes kb_default_key_repeat_delay and kb_default_key_repeat_rate
 * through the regular setters so the input server is updated.
 */
void
KeyboardSettings::Defaults()
{
	SetKeyboardRepeatDelay(kb_default_key_repeat_delay);
	SetKeyboardRepeatRate(kb_default_key_repeat_rate);
}


/**
 * @brief Returns whether the live values differ from the defaults.
 *
 * Drives the enabled state of the Defaults button on the keyboard card.
 *
 * @return true when at least one of the cached values diverges from the
 *         kit defaults.
 */
bool
KeyboardSettings::IsDefaultable() const
{
	return fSettings.key_repeat_delay != kb_default_key_repeat_delay
		|| fSettings.key_repeat_rate != kb_default_key_repeat_rate;
}
