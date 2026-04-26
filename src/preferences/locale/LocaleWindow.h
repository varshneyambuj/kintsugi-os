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
 * MIT License. Copyright 2005-2010, Axel Dörfler.
 * Original author: Axel Dörfler.
 */

/** @file LocaleWindow.h
    @brief Top-level window of the Locale preferences applet. */

#ifndef LOCALE_WINDOW_H
#define LOCALE_WINDOW_H


#include <Message.h>
#include <Window.h>


/** @brief Posted by the Revert button to roll back uncommitted edits. */
static const uint32 kMsgRevert = 'revt';


class BButton;
class BCheckBox;
class BListView;
class FormatSettingsView;
class LanguageListItem;
class LanguageListView;


/**
 * @brief Main window of the Locale preferences applet.
 *
 * Hosts a tabbed UI: a Language tab with the available and preferred
 * language lists, and a Formatting tab with the conventions list and
 * the FormatSettingsView. Tracks the initial preferred-language and
 * conventions selection so the Revert button can restore them.
 */
class LocaleWindow : public BWindow {
public:
								LocaleWindow();
	virtual						~LocaleWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();
	virtual void				Show();

private:
			void				_SettingsChanged();
			void				_SettingsReverted();

			bool				_IsReversible() const;

			void				_Refresh(bool setInitial = false);
			void				_Revert();

			void				_SetPreferredLanguages(
									const BMessage& languages);
			void				_PreferredLanguagesChanged();
			void				_EnableDisableLanguages();
			void				_InsertPreferredLanguage(LanguageListItem* item,
									int32 atIndex = -1);
			void				_Defaults();

			BButton*			fRevertButton;
			LanguageListView*	fLanguageListView;
			LanguageListView*	fPreferredListView;
			LanguageListView*	fConventionsListView;
			FormatSettingsView*	fFormatView;
			LanguageListItem*	fInitialConventionsItem;
			LanguageListItem*	fDefaultConventionsItem;
			BMessage			fInitialPreferredLanguages;
};


#endif	// LOCALE_WINDOW_H

