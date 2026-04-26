/*
 * Copyright 2026, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2004-2006, the Haiku project.
 * Original authors (chronological): mccall@digitalparadise.co.uk,
 *                   Jérôme Duval, Marcus Overhagen.
 */

/** @file KeyboardSettings.h
    @brief Settings model for the Keyboard preferences pane (repeat rate and delay). */

#ifndef KEYBOARD_SETTINGS_H_
#define KEYBOARD_SETTINGS_H_

#include <SupportDefs.h>

#include "kb_mouse_settings.h"

/**
 * @brief Persists and restores keyboard repeat-rate and repeat-delay settings.
 *
 * Wraps the on-disk \c kb_settings record exposed by the input server and
 * keeps a copy of the values active at construction time so that the
 * Keyboard preferences pane can offer a Revert action.
 */
class KeyboardSettings {
public :
	KeyboardSettings();
	~KeyboardSettings();

	void Revert();
	void Defaults();
	bool IsDefaultable() const;

	/** @brief Returns the current key-repeat rate (characters per second). */
	int32 KeyboardRepeatRate() const
		{ return fSettings.key_repeat_rate; }
	void SetKeyboardRepeatRate(int32 rate);

	/** @brief Returns the current delay before repeat begins, in microseconds. */
	bigtime_t KeyboardRepeatDelay() const
		{ return fSettings.key_repeat_delay; }
	void SetKeyboardRepeatDelay(bigtime_t delay);

private:
	kb_settings			fSettings;
	kb_settings			fOriginalSettings;
};

#endif
