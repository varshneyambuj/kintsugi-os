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
 * MIT License. Copyright 2001-2006, Haiku.
 * Original authors: Rafael Romo, Stefano Ceccherini (burton666@libero.it),
 *                   Axel Doerfler (axeld@pinc-software.de).
 */

/** @file ScreenSettings.h
    @brief Persistence of the Screen preferences window position. */

#ifndef SCREEN_SETTINGS_H
#define SCREEN_SETTINGS_H


#include <Rect.h>


/**
 * @brief Loads and saves the Screen preferences window frame.
 *
 * The saved frame is read on construction from the user settings file
 * and written back on destruction so that the window reopens where the
 * user last placed it.
 */
class ScreenSettings {
	public:
		ScreenSettings();
		virtual ~ScreenSettings();

		/** @brief Return the cached window frame. */
		BRect WindowFrame() const { return fWindowFrame; };
		void SetWindowFrame(BRect);

	private:
		BRect fWindowFrame;
};

#endif	// SCREEN_SETTINGS_H
