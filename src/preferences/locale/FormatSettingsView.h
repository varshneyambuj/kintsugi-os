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
 * MIT License. Copyright 2009, Adrien Destugues.
 * Original author: Adrien Destugues.
 */

/** @file FormatSettingsView.h
    @brief Locale preferences pane for date, time, number, and currency formats. */

#ifndef _FORMAT_SETTINGS_H
#define _FORMAT_SETTINGS_H


#include <Box.h>
#include <FormattingConventions.h>
#include <String.h>
#include <View.h>


class BCheckBox;
class BCountry;
class BMenuField;
class BMessage;
class BRadioButton;
class BStringView;
class BTextControl;


/** @brief Posted when the user toggles 12/24 hour clock display. */
static const uint32 kClockFormatChange = 'cfmc';
/** @brief Posted when the "use names from preferred language" toggle changes. */
static const uint32 kStringsLanguageChange = 'strc';
/** @brief Posted when the filesystem-translation toggle changes. */
static const uint32 kMsgFilesystemTranslationChanged = 'fsys';


/**
 * @brief Locale preferences pane that previews date/time/number/currency formats.
 *
 * Displays four boxed example groups (Date, Time, Numbers, Currency)
 * driven by the active BFormattingConventions, plus toggles for
 * 12/24 hour clock display, using language-specific names, and
 * translating application/folder names. Snapshots the conventions and
 * filesystem-translation flag at construction so Revert can roll back.
 */
class FormatSettingsView : public BView {
public:
								FormatSettingsView();
								~FormatSettingsView();

	virtual	void				MessageReceived(BMessage* message);
	virtual	void				AttachedToWindow();

	virtual	void				Revert();
	virtual	void				Refresh(bool setInitial = false);
	virtual	bool				IsReversible() const;

private:
			void				_UpdateExamples();

private:
			BCheckBox*			fFilesystemTranslationCheckbox;
			BCheckBox*			fUseLanguageStringsCheckBox;

			BRadioButton*		f24HourRadioButton;
			BRadioButton*		f12HourRadioButton;

			BStringView*		fFullDateExampleView;
			BStringView*		fLongDateExampleView;
			BStringView*		fMediumDateExampleView;
			BStringView*		fShortDateExampleView;

			BStringView*		fFullTimeExampleView;
			BStringView*		fLongTimeExampleView;
			BStringView*		fMediumTimeExampleView;
			BStringView*		fShortTimeExampleView;

			BStringView*		fPositiveNumberExampleView;
			BStringView*		fNegativeNumberExampleView;
			BStringView*		fPositiveMonetaryExampleView;
			BStringView*		fNegativeMonetaryExampleView;

			bool				fLocaleIs24Hour;

			BFormattingConventions	fInitialConventions;
			bool	fInitialTranslateNames;

			BBox*				fDateBox;
			BBox*				fTimeBox;
			BBox*				fNumberBox;
			BBox*				fMonetaryBox;
};


#endif	// _FORMAT_SETTINGS_H
