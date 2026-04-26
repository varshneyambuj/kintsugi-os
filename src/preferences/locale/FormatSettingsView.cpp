/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
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
 * Authors:
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2009, Adrien Destugues, pulkomandy@gmail.com.
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file FormatSettingsView.cpp
 * @brief Implementation of FormatSettingsView, the format-preview locale pane.
 *
 * Builds the four boxed example groups, captures the initial
 * formatting conventions and filesystem-translation flag, and updates
 * the live previews whenever the user changes a setting. Persists
 * changes through the MutableLocaleRoster.
 */


#include "FormatSettingsView.h"

#include <stdlib.h>

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <DateFormat.h>
#include <GridLayout.h>
#include <GridLayoutBuilder.h>
#include <GroupLayout.h>
#include <GroupLayoutBuilder.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MutableLocaleRoster.h>
#include <Message.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <NumberFormat.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <ScrollView.h>
#include <ScrollBar.h>
#include <SeparatorView.h>
#include <SpaceLayoutItem.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <TimeFormat.h>
#include <Window.h>

#include "LocalePreflet.h"


using BPrivate::MutableLocaleRoster;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TimeFormatSettings"


// #pragma mark -


/**
 * @brief Constructs the formatting preferences pane and seeds the example boxes.
 *
 * Creates the language-strings, filesystem-translation, and 12/24
 * hour controls, pre-populates the date, time, number, and currency
 * preview labels, then assembles them into four titled BBoxes.
 */
FormatSettingsView::FormatSettingsView()
	:
	BView("WindowsSettingsView", B_FRAME_EVENTS)
{
	fUseLanguageStringsCheckBox = new BCheckBox(
		B_TRANSLATE("Use month/day-names from preferred language"),
		new BMessage(kStringsLanguageChange));

	fFilesystemTranslationCheckbox = new BCheckBox("filesystemTranslation",
		B_TRANSLATE("Translate application and folder names"),
		new BMessage(kMsgFilesystemTranslationChanged));
	fFilesystemTranslationCheckbox->SetValue(
		BLocaleRoster::Default()->IsFilesystemTranslationPreferred());

	BStringView* fullDateLabel
		= new BStringView("", B_TRANSLATE("Full format:"));
	fullDateLabel->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	BStringView* longDateLabel
		= new BStringView("", B_TRANSLATE("Long format:"));
	longDateLabel->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	BStringView* mediumDateLabel
		= new BStringView("", B_TRANSLATE("Medium format:"));
	mediumDateLabel->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	BStringView* shortDateLabel
		= new BStringView("", B_TRANSLATE("Short format:"));
	shortDateLabel->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));

	fFullDateExampleView = new BStringView("", "");
	fFullDateExampleView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	fLongDateExampleView = new BStringView("", "");
	fLongDateExampleView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	fMediumDateExampleView = new BStringView("", "");
	fMediumDateExampleView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	fShortDateExampleView = new BStringView("", "");
	fShortDateExampleView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));

	f24HourRadioButton = new BRadioButton("", B_TRANSLATE("24 hour"),
		new BMessage(kClockFormatChange));
	f12HourRadioButton = new BRadioButton("", B_TRANSLATE("12 hour"),
		new BMessage(kClockFormatChange));

	float spacing = be_control_look->DefaultItemSpacing();

	BStringView* fullTimeLabel
		= new BStringView("", B_TRANSLATE("Full format:"));
	fullTimeLabel->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	BStringView* longTimeLabel
		= new BStringView("", B_TRANSLATE("Long format:"));
	longTimeLabel->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	BStringView* mediumTimeLabel
		= new BStringView("", B_TRANSLATE("Medium format:"));
	mediumTimeLabel->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	BStringView* shortTimeLabel
		= new BStringView("", B_TRANSLATE("Short format:"));
	shortTimeLabel->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));

	fFullTimeExampleView = new BStringView("", "");
	fFullTimeExampleView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	fLongTimeExampleView = new BStringView("", "");
	fLongTimeExampleView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	fMediumTimeExampleView = new BStringView("", "");
	fMediumTimeExampleView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	fShortTimeExampleView = new BStringView("", "");
	fShortTimeExampleView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));

	BStringView* positiveNumberLabel
		= new BStringView("", B_TRANSLATE("Positive:"));
	positiveNumberLabel->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	BStringView* negativeNumberLabel
		= new BStringView("", B_TRANSLATE("Negative:"));
	negativeNumberLabel->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));

	fPositiveNumberExampleView = new BStringView("", "");
	fPositiveNumberExampleView->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT,
		B_ALIGN_VERTICAL_UNSET));
	fNegativeNumberExampleView = new BStringView("", "");
	fNegativeNumberExampleView->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT,
		B_ALIGN_VERTICAL_UNSET));

	BStringView* positiveMonetaryLabel
		= new BStringView("", B_TRANSLATE("Positive:"));
	positiveMonetaryLabel->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	BStringView* negativeMonetaryLabel
		= new BStringView("", B_TRANSLATE("Negative:"));
	negativeMonetaryLabel->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));

	fPositiveMonetaryExampleView = new BStringView("", "");
	fPositiveMonetaryExampleView->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT,
		B_ALIGN_VERTICAL_UNSET));
	fNegativeMonetaryExampleView = new BStringView("", "");
	fNegativeMonetaryExampleView->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT,
		B_ALIGN_VERTICAL_UNSET));

	Refresh(true);

	fDateBox = new BBox(B_TRANSLATE("Date"));
	fTimeBox = new BBox(B_TRANSLATE("Time"));
	fNumberBox = new BBox(B_TRANSLATE("Numbers"));
	fMonetaryBox = new BBox(B_TRANSLATE("Currency"));

	fDateBox->SetLabel(B_TRANSLATE("Date"));
	fTimeBox->SetLabel(B_TRANSLATE("Time"));
	fNumberBox->SetLabel(B_TRANSLATE("Numbers"));
	fMonetaryBox->SetLabel(B_TRANSLATE("Currency"));

	fDateBox->AddChild(BLayoutBuilder::Grid<>(spacing, spacing / 2)
		.SetInsets(spacing, spacing, spacing, spacing)
		.Add(fullDateLabel, 0, 0)
		.Add(fFullDateExampleView, 1, 0)
		.Add(BSpaceLayoutItem::CreateGlue(), 2, 0)
		.Add(longDateLabel, 0, 1)
		.Add(fLongDateExampleView, 1, 1)
		.Add(BSpaceLayoutItem::CreateGlue(), 2, 1)
		.Add(mediumDateLabel, 0, 2)
		.Add(fMediumDateExampleView, 1, 2)
		.Add(BSpaceLayoutItem::CreateGlue(), 2, 2)
		.Add(shortDateLabel, 0, 3)
		.Add(fShortDateExampleView, 1, 3)
		.Add(BSpaceLayoutItem::CreateGlue(), 2, 3)
		.View());

	fTimeBox->AddChild(BLayoutBuilder::Grid<>(spacing, spacing / 2)
		.SetInsets(spacing, spacing, spacing, spacing)
		.AddGroup(B_HORIZONTAL, spacing, 0, 0, 2)
			.Add(f24HourRadioButton, 0)
			.Add(f12HourRadioButton, 0)
			.AddGlue()
			.End()
		.Add(fullTimeLabel, 0, 1)
		.Add(fFullTimeExampleView, 1, 1)
		.Add(BSpaceLayoutItem::CreateGlue(), 2, 1)
		.Add(longTimeLabel, 0, 2)
		.Add(fLongTimeExampleView, 1, 2)
		.Add(BSpaceLayoutItem::CreateGlue(), 2, 2)
		.Add(mediumTimeLabel, 0, 3)
		.Add(fMediumTimeExampleView, 1, 3)
		.Add(BSpaceLayoutItem::CreateGlue(), 2, 3)
		.Add(shortTimeLabel, 0, 4)
		.Add(fShortTimeExampleView, 1, 4)
		.Add(BSpaceLayoutItem::CreateGlue(), 2, 4)
		.View());

	fNumberBox->AddChild(BLayoutBuilder::Grid<>(spacing, spacing / 2)
		.SetInsets(spacing, spacing, spacing, spacing)
		.Add(positiveNumberLabel, 0, 0)
		.Add(fPositiveNumberExampleView, 1, 0)
		.Add(BSpaceLayoutItem::CreateGlue(), 2, 0)
		.Add(negativeNumberLabel, 0, 1)
		.Add(fNegativeNumberExampleView, 1, 1)
		.Add(BSpaceLayoutItem::CreateGlue(), 2, 1)
		.View());

	fMonetaryBox->AddChild(BLayoutBuilder::Grid<>(spacing, spacing / 2)
		.SetInsets(spacing, spacing, spacing, spacing)
		.Add(positiveMonetaryLabel, 0, 0)
		.Add(fPositiveMonetaryExampleView, 1, 0)
		.Add(BSpaceLayoutItem::CreateGlue(), 2, 0)
		.Add(negativeMonetaryLabel, 0, 1)
		.Add(fNegativeMonetaryExampleView, 1, 1)
		.Add(BSpaceLayoutItem::CreateGlue(), 2, 1)
		.View());

	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.Add(fFilesystemTranslationCheckbox)
		.Add(fUseLanguageStringsCheckBox)
		.Add(fDateBox)
		.Add(fTimeBox)
		.AddGroup(B_HORIZONTAL, spacing)
			.Add(fNumberBox)
			.Add(fMonetaryBox)
			.End()
		.AddGlue();
}


/**
 * @brief Destroys the view; child controls are owned by the layout system.
 */
FormatSettingsView::~FormatSettingsView()
{
}


/**
 * @brief Wires control targets so messages reach the right looper on attach.
 */
void
FormatSettingsView::AttachedToWindow()
{
	fFilesystemTranslationCheckbox->SetTarget(Window());
	fUseLanguageStringsCheckBox->SetTarget(this);
	f24HourRadioButton->SetTarget(this);
	f12HourRadioButton->SetTarget(this);
}


/**
 * @brief Routes locale-changed and per-toggle messages to the underlying conventions.
 *
 * Updates the active BFormattingConventions on every meaningful
 * change and notifies the parent window so it can refresh its
 * Revert/Defaults buttons.
 *
 * @param message  Incoming BMessage.
 */
void
FormatSettingsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_LOCALE_CHANGED:
		{
			// Time updated 12/24 hour clock
			BFormattingConventions conventions;
			BLocale::Default()->GetFormattingConventions(&conventions);
			if (conventions.Use24HourClock())
				f24HourRadioButton->SetValue(B_CONTROL_ON);
			else
				f12HourRadioButton->SetValue(B_CONTROL_ON);

			_UpdateExamples();
			Window()->PostMessage(kMsgSettingsChanged);
			break;
		}

		case kClockFormatChange:
		{
			BFormattingConventions conventions;
			BLocale::Default()->GetFormattingConventions(&conventions);
			conventions.SetExplicitUse24HourClock(
				f24HourRadioButton->Value() == B_CONTROL_ON);
			MutableLocaleRoster::Default()->SetDefaultFormattingConventions(
				conventions);

			_UpdateExamples();
			Window()->PostMessage(kMsgSettingsChanged);
			break;
		}

		case kStringsLanguageChange:
		{
			BFormattingConventions conventions;
			BLocale::Default()->GetFormattingConventions(&conventions);
			conventions.SetUseStringsFromPreferredLanguage(
				fUseLanguageStringsCheckBox->Value() ? true : false);
			MutableLocaleRoster::Default()->SetDefaultFormattingConventions(
				conventions);

			_UpdateExamples();

			Window()->PostMessage(kMsgSettingsChanged);
			break;
		}

		default:
			BView::MessageReceived(message);
	}
}


/**
 * @brief Restores the conventions and filesystem-translation flag captured at startup.
 */
void
FormatSettingsView::Revert()
{
	MutableLocaleRoster::Default()->SetDefaultFormattingConventions(
		fInitialConventions);

	fFilesystemTranslationCheckbox->SetValue(fInitialTranslateNames);

	_UpdateExamples();
}


/**
 * @brief Re-reads the active conventions and refreshes every preview.
 *
 * @param setInitial  When true, also captures the freshly read state as
 *                    the snapshot Revert() should restore to.
 */
void
FormatSettingsView::Refresh(bool setInitial)
{
	BFormattingConventions conventions;
	BLocale::Default()->GetFormattingConventions(&conventions);
	if (setInitial) {
		fInitialConventions = conventions;
		fInitialTranslateNames
			= BLocaleRoster::Default()->IsFilesystemTranslationPreferred();
	}

	if (!conventions.Use24HourClock()) {
		f12HourRadioButton->SetValue(B_CONTROL_ON);
		fLocaleIs24Hour = false;
	} else {
		f24HourRadioButton->SetValue(B_CONTROL_ON);
		fLocaleIs24Hour = true;
	}

	fUseLanguageStringsCheckBox->SetValue(
		conventions.UseStringsFromPreferredLanguage()
			? B_CONTROL_ON : B_CONTROL_OFF);

	_UpdateExamples();
}


/**
 * @brief Returns true when the user's edits diverge from the captured snapshot.
 *
 * @return true if either the conventions or the filesystem-translation
 *         flag differ from their initial values.
 */
bool
FormatSettingsView::IsReversible() const
{
	BFormattingConventions conventions;
	BLocale::Default()->GetFormattingConventions(&conventions);

	return (conventions != fInitialConventions)
		|| (fFilesystemTranslationCheckbox->Value() != fInitialTranslateNames);
}


/**
 * @brief Reformats every preview label using the currently active conventions.
 *
 * The format objects are intentionally constructed locally on each
 * call so they pick up the latest conventions from the locale roster.
 * Failed conversions display a translated "ERROR" placeholder.
 */
void
FormatSettingsView::_UpdateExamples()
{
	time_t timeValue = (time_t)time(NULL);
	BString result;

	// Do NOT make these class members. We do want to recreate it everytime, as
	// to get the updated settings from the locale roster.
	BDateFormat dateFormat;
	BNumberFormat numberFormat;
	BString errorString = B_TRANSLATE("ERROR");
	BTimeFormat timeFormat;

	dateFormat.Format(result, timeValue, B_FULL_DATE_FORMAT);
	fFullDateExampleView->SetText(result);

	dateFormat.Format(result, timeValue, B_LONG_DATE_FORMAT);
	fLongDateExampleView->SetText(result);

	dateFormat.Format(result, timeValue, B_MEDIUM_DATE_FORMAT);
	fMediumDateExampleView->SetText(result);

	dateFormat.Format(result, timeValue, B_SHORT_DATE_FORMAT);
	fShortDateExampleView->SetText(result);

	timeFormat.Format(result, timeValue, B_FULL_TIME_FORMAT);
	fFullTimeExampleView->SetText(result);

	timeFormat.Format(result, timeValue, B_LONG_TIME_FORMAT);
	fLongTimeExampleView->SetText(result);

	timeFormat.Format(result, timeValue, B_MEDIUM_TIME_FORMAT);
	fMediumTimeExampleView->SetText(result);

	timeFormat.Format(result, timeValue, B_SHORT_TIME_FORMAT);
	fShortTimeExampleView->SetText(result);

	status_t status = numberFormat.Format(result, 1234.56);
	if (status == B_OK)
		fPositiveNumberExampleView->SetText(result);
	else
		fPositiveNumberExampleView->SetText(errorString);

	status = numberFormat.Format(result, -1234.56);
	if (status == B_OK)
		fNegativeNumberExampleView->SetText(result);
	else
		fNegativeNumberExampleView->SetText(errorString);

	status = numberFormat.FormatMonetary(result, 1234.56);
	if (status == B_OK)
		fPositiveMonetaryExampleView->SetText(result);
	else
		fPositiveMonetaryExampleView->SetText(errorString);

	status = numberFormat.FormatMonetary(result, -1234.56);
	if (status == B_OK)
		fNegativeMonetaryExampleView->SetText(result);
	else
		fNegativeMonetaryExampleView->SetText(errorString);
}
