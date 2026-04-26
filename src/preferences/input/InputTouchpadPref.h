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

/** @file InputTouchpadPref.h
    @brief Declares TouchpadPref, the persistence model behind the touchpad card. */

#ifndef TOUCHPAD_PREF_H
#define TOUCHPAD_PREF_H


#include <Debug.h>
#include <Input.h>
#include <Path.h>

#include "touchpad_settings.h"


#if DEBUG
#	define LOG(text...) PRINT((text))
#else
#	define LOG(text...)
#endif


/**
 * @brief Owns and persists the settings for a single touchpad device.
 *
 * Reads and writes the per-user touchpad_settings file, builds a BMessage
 * representation that can be sent to the input server, and pushes
 * speed/acceleration changes through the kit's set_mouse_* helpers. The
 * snapshot taken on construction is what Revert() restores.
 */
class TouchpadPref {
public:
								TouchpadPref(BInputDevice* device);
			virtual				~TouchpadPref();

			void				Revert();
			void				Defaults();

			/** @brief Returns the saved window position; (-1,-1) means centred. */
			BPoint 				WindowPosition()
									{ return fWindowPosition; }
			/** @brief Records a new window position to persist on save. */
			void				SetWindowPosition(BPoint position)
									{ fWindowPosition = position; }

			/** @brief Returns a mutable reference to the live touchpad settings. */
			touchpad_settings&	Settings()
									{ return fSettings; }
			BMessage			BuildSettingsMessage();
			status_t			LoadSettings();

			status_t			UpdateRunningSettings();

			void				SetSpeed(int32 value);
			void				SetAcceleration(int32 value);

private:
			status_t			GetSettingsPath(BPath& path);
			status_t			SaveSettings();

			BInputDevice* 		fTouchPad;

			touchpad_settings	fSettings;
			touchpad_settings	fStartSettings;
			BPoint				fWindowPosition;
};


#endif	// TOUCHPAD_PREF_H
