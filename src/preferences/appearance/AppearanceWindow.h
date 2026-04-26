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
 * MIT License. Copyright 2002-2025, Haiku.
 * Original authors: DarkWyrm, Alexander von Gluck, Stephan Aßmus.
 */

/** @file AppearanceWindow.h
    @brief Tabbed BWindow that hosts all Appearance preference panes. */

#ifndef APPEARANCE_WINDOW_H
#define APPEARANCE_WINDOW_H


#include <Application.h>
#include <Button.h>
#include <Window.h>
#include <Message.h>
#include <TabView.h>

class ColorsView;
class AntialiasingSettingsView;
class FontView;
class LookAndFeelSettingsView;


/**
 * @brief Top-level window of the Appearance preference application.
 *
 * Holds the Fonts, Colors, Look-and-Feel, and Antialiasing tabs along
 * with the global Defaults and Revert buttons.
 */
class AppearanceWindow : public BWindow {
public:
									AppearanceWindow(BRect frame);
			void					MessageReceived(BMessage *message);

private:
			void					_UpdateButtons();
			bool					_IsDefaultable() const;
			bool					_IsRevertable() const;

		ColorsView*					fColorsView;
		BButton*					fDefaultsButton;
		BButton*					fRevertButton;

		AntialiasingSettingsView* 	fAntialiasingSettings;
		FontView*					fFontSettings;
		LookAndFeelSettingsView*	fLookAndFeelSettings;
};


/** @brief Notification posted by panes when a setting changes. */
static const int32 kMsgUpdate = 'updt';


#endif /* APPEARANCE_WINDOW_H */
