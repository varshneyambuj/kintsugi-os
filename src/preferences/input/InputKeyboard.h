/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2019, Haiku, Inc.
 * Original author: Preetpal Kaur.
 */

/** @file InputKeyboard.h
    @brief Declares InputKeyboard, the keyboard settings card view. */

#ifndef INPUT_KEYBOARD_H
#define INPUT_KEYBOARD_H

#include <Button.h>
#include <Input.h>

#include "KeyboardSettings.h"
#include "KeyboardView.h"


/**
 * @brief BView card hosting the keyboard preferences sliders and buttons.
 *
 * Owns a KeyboardSettings model and a KeyboardView control panel. Sits
 * inside the InputWindow's BCardView and is shown when the user selects a
 * keyboard input device.
 */
class InputKeyboard : public BView
{
public:
			InputKeyboard(BInputDevice* dev);

	void	MessageReceived(BMessage* message);
private:
	KeyboardView		*fSettingsView;
	KeyboardSettings	fSettings;
	BButton*			fDefaultsButton;
	BButton*			fRevertButton;
};

#endif
