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
 * MIT License. Copyright 2002-2015, Haiku, Inc.
 * Original authors: Oliver Siebenmarck, Andrew McCall, Michael Wilber.
 */

/** @file DataTranslationsWindow.h
    @brief Main window class for the DataTranslations preferences app. */

#ifndef DATA_TRANSLATIONS_WINDOW_H
#define DATA_TRANSLATIONS_WINDOW_H


#include <Box.h>
#include <Button.h>
#include <IconView.h>
#include <Path.h>
#include <View.h>
#include <Window.h>

#include "TranslatorListView.h"


class BTranslatorReleaseDelegate;
class BTextView;


/**
 * @brief Main window listing installed translators and embedding their
 *        per-translator configuration views.
 *
 * Watches the default BTranslatorRoster so additions and removals (e.g. as a
 * result of drag-and-drop installation) immediately update the list. Saves
 * its window position to DataTranslationsSettings on close.
 */
class DataTranslationsWindow : public BWindow {
public:
							DataTranslationsWindow();
							~DataTranslationsWindow();

	virtual	bool			QuitRequested();
	virtual	void			MessageReceived(BMessage* message);

private:
			void			_ShowInfoView();
			status_t		_GetTranslatorInfo(int32 id, const char*& name,
								const char*& info, int32& version, BPath& path);
			void			_ShowInfoAlert(int32 id);
			status_t		_ShowConfigView(int32 id);
			status_t		_PopulateListView();
			void			_SetupViews();

			TranslatorListView*	fTranslatorListView;
			BTranslatorReleaseDelegate*		fRelease;

			BBox*			fRightBox;
			BView*			fConfigView;
			IconView*		fIconView;
			BButton*		fButton;
			BTextView*		fInfoText;
};


#endif	// DATA_TRANSLATIONS_WINDOW_H
