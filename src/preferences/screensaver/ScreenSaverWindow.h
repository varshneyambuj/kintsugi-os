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
 * Original authors: Axel Dörfler, Jérôme Duval, Michael Phipps,
 *                   John Scipione.
 */

/** @file ScreenSaverWindow.h
    @brief Top-level BWindow for the ScreenSaver preflet, hosting the General and Modules tabs. */

#ifndef SCREEN_SAVER_WINDOW_H
#define SCREEN_SAVER_WINDOW_H


#include <DirectWindow.h>

#include "PasswordWindow.h"
#include "ScreenSaverSettings.h"


class BMessage;
class BRect;

class FadeView;
class ModulesView;
class TabView;


/**
 * @brief Main preflet window that owns the tab view, settings model, and
 *        password sub-window.
 *
 * The window persists its frame across runs via ScreenSaverSettings, and
 * forwards screen-changed notifications to its FadeView so that DPMS
 * capabilities can be re-evaluated.
 */
class ScreenSaverWindow : public BWindow {
public:
								ScreenSaverWindow();
	virtual						~ScreenSaverWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	void				ScreenChanged(BRect frame, color_space space);
	virtual	bool				QuitRequested();

			void				LoadSettings();

private:
			float				fMinWidth;
			float				fMinHeight;
			ScreenSaverSettings	fSettings;
			PasswordWindow*		fPasswordWindow;

			FadeView*			fFadeView;
			ModulesView*		fModulesView;
			TabView*			fTabView;
};


/** @brief Message asking the modules view to re-scan add-on directories. */
static const int32 kMsgUpdateList = 'UPDL';


#endif	// SCREEN_SAVER_WINDOW_H
