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
 * Original authors: Rafael Romo, Stefano Ceccherini, Axel Dörfler.
 */

/** @file ScreenSettings.h
    @brief Persists the Printers window frame between sessions. */

#ifndef SCREEN_SETTINGS_H
#define SCREEN_SETTINGS_H


#include <Rect.h>


/**
 * @brief Tiny RAII wrapper around the Printers preflet's window frame.
 *
 * Loads the saved offset on construction and writes it back on
 * destruction.
 */
class ScreenSettings {
	public:
		ScreenSettings();
		virtual ~ScreenSettings();

		/** @brief Returns the loaded (or last-set) window frame. */
		BRect WindowFrame() const { return fWindowFrame; };
		void SetWindowFrame(BRect);

	private:
		BRect fWindowFrame;
};

#endif	// SCREEN_SETTINGS_H
