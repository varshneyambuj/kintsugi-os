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
 * MIT License. Copyright 2003-2013, Haiku.
 * Original authors: Michael Phipps, Jérôme Duval.
 */

/** @file PasswordWindow.h
    @brief Modal BWindow that lets the user choose between system and custom screensaver passwords. */

#ifndef PASSWORD_WINDOW_H
#define PASSWORD_WINDOW_H


#include <Window.h>


class BRadioButton;
class BTextControl;

class ScreenSaverSettings;


/**
 * @brief Modal dialog for editing the screensaver lock password.
 *
 * Offers two mutually exclusive modes via radio buttons: use the existing
 * system password, or provide a custom one (with a confirm field). On
 * "Done" the dialog hashes the custom password with @c crypt() and writes
 * the result back to the shared ScreenSaverSettings.
 */
class PasswordWindow : public BWindow {
public:
								PasswordWindow(ScreenSaverSettings& settings);

	virtual	void				MessageReceived(BMessage* message);

			void				Update();

private:
			void				_Setup();

			BRadioButton*		fUseCustom;
			BRadioButton*		fUseSystem;
			BTextControl*		fConfirmControl;
			BTextControl*		fPasswordControl;

			ScreenSaverSettings& fSettings;
};


#endif	// PASSWORD_WINDOW_H
