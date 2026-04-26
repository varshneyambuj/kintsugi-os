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

/** @file InputMouse.h
    @brief Declares InputMouse, the mouse settings card view. */

#ifndef INPUT_MOUSE_H
#define INPUT_MOUSE_H


#include <Box.h>
#include <Button.h>
#include <Input.h>
#include <View.h>

#include "MouseSettings.h"
#include "MouseView.h"
#include "SettingsView.h"

#define MOUSE_SETTINGS 'Mss'

class DeviceListView;


/**
 * @brief BView card hosting the mouse preferences controls and buttons.
 *
 * Owns a SettingsView with the full mouse control panel and a pair of
 * Defaults/Revert buttons. Forwards user actions into the per-device
 * MouseSettings model supplied by InputWindow.
 */
class InputMouse : public BView {
public:
					InputMouse(BInputDevice* dev, MouseSettings* settings);
	virtual			~InputMouse();
	void			SetMouseType(int32 type);
	void			MessageReceived(BMessage* message);
private:

	typedef BBox inherited;

	SettingsView*		fSettingsView;
	MouseView*			fMouseView;
	BButton*			fDefaultsButton;
	BButton*			fRevertButton;
	MouseSettings*		fSettings;
};

#endif	/* INPUT_MOUSE_H */
