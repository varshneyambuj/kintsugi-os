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
 *   Copyright 2008, Stephan Aßmus, <superstippi@gmx.de>
 *   Copyright 2008, Andrej Spielmann, <andrej.spielmann@seh.ox.ac.uk>
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file AntialiasingSettingsView.cpp
 * @brief Implementation of the Antialiasing tab in the Appearance preflet.
 *
 * Exposes per-system font antialiasing controls: subpixel vs grayscale
 * rendering, hinting mode, and the average-weight slider that tames
 * subpixel color fringes. Communicates with the app_server through the
 * private @c set_subpixel_antialiasing, @c set_hinting_mode and
 * @c set_average_weight calls.
 */


#include "AntialiasingSettingsView.h"

#include <stdio.h>
#include <stdlib.h>

#include <Box.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <String.h>
#include <TextView.h>


#include "AppearanceWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AntialiasingSettingsView"


/** @brief Message constant emitted when the antialiasing menu changes. */
static const int32 kMsgSetAntialiasing = 'anti';
/** @brief Message constant emitted when the hinting menu changes. */
static const int32 kMsgSetHinting = 'hint';
/** @brief Message constant emitted when the average-weight slider changes. */
static const int32 kMsgSetAverageWeight = 'avrg';
static const char* kSubpixelLabel = B_TRANSLATE_MARK("LCD subpixel");
static const char* kGrayscaleLabel = B_TRANSLATE_MARK("Grayscale");
static const char* kNoHintingLabel = B_TRANSLATE_MARK("Off");
static const char* kMonospacedHintingLabel =
	B_TRANSLATE_MARK("Monospaced fonts only");
static const char* kFullHintingLabel = B_TRANSLATE_MARK("On");


// #pragma mark - private libbe API


enum {
	HINTING_MODE_OFF = 0,
	HINTING_MODE_ON,
	HINTING_MODE_MONOSPACED_ONLY
};

static const uint8 kDefaultHintingMode = HINTING_MODE_ON;
static const unsigned char kDefaultAverageWeight = 120;
static const bool kDefaultSubpixelAntialiasing = true;

extern void set_subpixel_antialiasing(bool subpix);
extern status_t get_subpixel_antialiasing(bool* subpix);
extern void set_hinting_mode(uint8 hinting);
extern status_t get_hinting_mode(uint8* hinting);
extern void set_average_weight(unsigned char averageWeight);
extern status_t get_average_weight(unsigned char* averageWeight);


//	#pragma mark -


/**
 * @brief Constructs the antialiasing settings pane.
 *
 * Reads the current subpixel, hinting and average-weight settings from
 * the app_server (falling back to compiled-in defaults), builds the
 * popup menus and slider, and lays them out in a grid.
 *
 * @param name Identifier passed to the BView base class.
 */
AntialiasingSettingsView::AntialiasingSettingsView(const char* name)
	: BView(name, 0)
{
	// collect the current system settings
	if (get_subpixel_antialiasing(&fCurrentSubpixelAntialiasing) != B_OK)
		fCurrentSubpixelAntialiasing = kDefaultSubpixelAntialiasing;
	fSavedSubpixelAntialiasing = fCurrentSubpixelAntialiasing;

	if (get_hinting_mode(&fCurrentHinting) != B_OK)
		fCurrentHinting = kDefaultHintingMode;
	fSavedHinting = fCurrentHinting;

	if (get_average_weight(&fCurrentAverageWeight) != B_OK)
		fCurrentAverageWeight = kDefaultAverageWeight;
	fSavedAverageWeight = fCurrentAverageWeight;

	// create the controls

	// antialiasing menu
	_BuildAntialiasingMenu();
	fAntialiasingMenuField = new BMenuField("antialiasing",
		B_TRANSLATE("Antialiasing type:"), fAntialiasingMenu);

	// "average weight" in subpixel filtering
	fAverageWeightControl = new BSlider("averageWeightControl",
		B_TRANSLATE("Reduce colored edges filter strength:"),
		new BMessage(kMsgSetAverageWeight), 0, 255, B_HORIZONTAL);
	fAverageWeightControl->SetLimitLabels(B_TRANSLATE("Off"),
		B_TRANSLATE("Strong"));
	fAverageWeightControl->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fAverageWeightControl->SetHashMarkCount(255 / 15);
	fAverageWeightControl->SetEnabled(false);

	// hinting menu
	_BuildHintingMenu();
	fHintingMenuField = new BMenuField("hinting", B_TRANSLATE("Glyph hinting:"),
		fHintingMenu);

	BLayoutBuilder::Grid<>(this, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
	// controls pane
		.AddMenuField(fHintingMenuField, 0, 0)
		.AddMenuField(fAntialiasingMenuField, 0, 1)
		.Add(fAverageWeightControl, 0, 2, 3)

		.AddGlue(0, 3)
		.SetInsets(B_USE_WINDOW_SPACING);

	BGridLayout* layout = dynamic_cast<BGridLayout*>(GetLayout());
	layout->SetMinColumnWidth(0,
		StringWidth(B_TRANSLATE("Antialiasing type:")) * 2);

	_SetCurrentAntialiasing();
	_SetCurrentHinting();
	_SetCurrentAverageWeight();
}


/**
 * @brief Destructor; child views are owned and freed by the BView hierarchy.
 */
AntialiasingSettingsView::~AntialiasingSettingsView()
{
}


/**
 * @brief Adopts parent colors and wires control targets when attached.
 *
 * Called by the BView framework once the view enters a window. Ensures
 * menu items and slider deliver messages back to this view.
 */
void
AntialiasingSettingsView::AttachedToWindow()
{
	AdoptParentColors();

	if (Parent() == NULL)
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fAntialiasingMenu->SetTargetForItems(this);
	fHintingMenu->SetTargetForItems(this);
	fAverageWeightControl->SetTarget(this);
}


/**
 * @brief Handles antialiasing-, hinting- and slider-change messages.
 *
 * Updates the local state, pushes the new value to the app_server and
 * notifies the parent window so it can refresh the Defaults/Revert
 * buttons.
 *
 * @param msg The incoming BMessage.
 */
void
AntialiasingSettingsView::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case kMsgSetAntialiasing:
		{
			bool subpixelAntialiasing;
			if (msg->FindBool("antialiasing", &subpixelAntialiasing) != B_OK
				|| subpixelAntialiasing == fCurrentSubpixelAntialiasing)
				break;

			fCurrentSubpixelAntialiasing = subpixelAntialiasing;
			fAverageWeightControl->SetEnabled(fCurrentSubpixelAntialiasing);

			set_subpixel_antialiasing(fCurrentSubpixelAntialiasing);

			Window()->PostMessage(kMsgUpdate);
			break;
		}
		case kMsgSetHinting:
		{
			int8 hinting;
			if (msg->FindInt8("hinting", &hinting) != B_OK
				|| hinting == fCurrentHinting)
				break;

			fCurrentHinting = hinting;
			set_hinting_mode(fCurrentHinting);

			Window()->PostMessage(kMsgUpdate);
			break;
		}
		case kMsgSetAverageWeight:
		{
			int32 averageWeight = fAverageWeightControl->Value();
			if (averageWeight == fCurrentAverageWeight)
				break;

			fCurrentAverageWeight = averageWeight;

			set_average_weight(fCurrentAverageWeight);

			Window()->PostMessage(kMsgUpdate);
			break;
		}
		default:
			BView::MessageReceived(msg);
	}
}


/**
 * @brief Populates the antialiasing-type popup menu.
 *
 * Adds a "Grayscale" item (subpixel off) and a "LCD subpixel" item
 * (subpixel on) bound to kMsgSetAntialiasing.
 */
void
AntialiasingSettingsView::_BuildAntialiasingMenu()
{
	fAntialiasingMenu = new BPopUpMenu(B_TRANSLATE("Antialiasing menu"));

	BMessage* message = new BMessage(kMsgSetAntialiasing);
	message->AddBool("antialiasing", false);

	BMenuItem* item
		= new BMenuItem(B_TRANSLATE_NOCOLLECT(kGrayscaleLabel), message);

	fAntialiasingMenu->AddItem(item);

	message = new BMessage(kMsgSetAntialiasing);
	message->AddBool("antialiasing", true);

	item = new BMenuItem(B_TRANSLATE_NOCOLLECT(kSubpixelLabel), message);

	fAntialiasingMenu->AddItem(item);
}


/**
 * @brief Populates the hinting-mode popup menu.
 *
 * Adds Off, On, and "Monospaced fonts only" entries bound to
 * kMsgSetHinting and tagged with the corresponding HINTING_MODE_*
 * constant.
 */
void
AntialiasingSettingsView::_BuildHintingMenu()
{
	fHintingMenu = new BPopUpMenu(B_TRANSLATE("Hinting menu"));

	BMessage* message = new BMessage(kMsgSetHinting);
	message->AddInt8("hinting", HINTING_MODE_OFF);
	fHintingMenu->AddItem(new BMenuItem(B_TRANSLATE_NOCOLLECT(kNoHintingLabel),
		message));

	message = new BMessage(kMsgSetHinting);
	message->AddInt8("hinting", HINTING_MODE_ON);
	fHintingMenu->AddItem(new BMenuItem(
		B_TRANSLATE_NOCOLLECT(kFullHintingLabel), message));

	message = new BMessage(kMsgSetHinting);
	message->AddInt8("hinting", HINTING_MODE_MONOSPACED_ONLY);
	fHintingMenu->AddItem(new BMenuItem(
		B_TRANSLATE_NOCOLLECT(kMonospacedHintingLabel), message));
}


/**
 * @brief Marks the antialiasing menu item that matches the active mode.
 *
 * Also enables the average-weight slider when subpixel mode is active.
 */
void
AntialiasingSettingsView::_SetCurrentAntialiasing()
{
	BMenuItem *item = fAntialiasingMenu->FindItem(
		fCurrentSubpixelAntialiasing
		? B_TRANSLATE_NOCOLLECT(kSubpixelLabel)
		: B_TRANSLATE_NOCOLLECT(kGrayscaleLabel));
	if (item != NULL)
		item->SetMarked(true);
	if (fCurrentSubpixelAntialiasing)
		fAverageWeightControl->SetEnabled(true);
}


/**
 * @brief Marks the hinting menu item that matches the active mode.
 *
 * Falls through silently if @c fCurrentHinting holds an unrecognized
 * value.
 */
void
AntialiasingSettingsView::_SetCurrentHinting()
{
	const char* label;
	switch (fCurrentHinting) {
		case HINTING_MODE_OFF:
			label = kNoHintingLabel;
			break;
		case HINTING_MODE_ON:
			label = kFullHintingLabel;
			break;
		case HINTING_MODE_MONOSPACED_ONLY:
			label = kMonospacedHintingLabel;
			break;
		default:
			return;
	}

	BMenuItem *item = fHintingMenu->FindItem(B_TRANSLATE_NOCOLLECT(label));
	if (item != NULL)
		item->SetMarked(true);
}


/**
 * @brief Snaps the average-weight slider to the current setting value.
 */
void
AntialiasingSettingsView::_SetCurrentAverageWeight()
{
	fAverageWeightControl->SetValue(fCurrentAverageWeight);
}


/**
 * @brief Resets all three antialiasing settings to compiled-in defaults.
 *
 * Pushes the defaults to the app_server and updates the UI controls.
 * No-op if the current values already match the defaults.
 */
void
AntialiasingSettingsView::SetDefaults()
{
	if (!IsDefaultable())
		return;

	fCurrentSubpixelAntialiasing = kDefaultSubpixelAntialiasing;
	fCurrentHinting = kDefaultHintingMode;
	fCurrentAverageWeight = kDefaultAverageWeight;

	set_subpixel_antialiasing(fCurrentSubpixelAntialiasing);
	set_hinting_mode(fCurrentHinting);
	set_average_weight(fCurrentAverageWeight);

	_SetCurrentAntialiasing();
	_SetCurrentHinting();
	_SetCurrentAverageWeight();
}


/**
 * @brief Reports whether any setting differs from the system defaults.
 *
 * @return @c true if subpixel, hinting or average-weight differs from
 *         the kDefault* compiled-in values.
 */
bool
AntialiasingSettingsView::IsDefaultable()
{
	return fCurrentSubpixelAntialiasing != kDefaultSubpixelAntialiasing
		|| fCurrentHinting != kDefaultHintingMode
		|| fCurrentAverageWeight != kDefaultAverageWeight;
}


/**
 * @brief Reports whether any setting differs from its saved value.
 *
 * @return @c true if subpixel, hinting or average-weight differs from
 *         the snapshot taken at construction.
 */
bool
AntialiasingSettingsView::IsRevertable()
{
	return fCurrentSubpixelAntialiasing != fSavedSubpixelAntialiasing
		|| fCurrentHinting != fSavedHinting
		|| fCurrentAverageWeight != fSavedAverageWeight;
}


/**
 * @brief Restores the saved antialiasing settings.
 *
 * Pushes the snapshot back to the app_server and refreshes the UI
 * controls. No-op if nothing has changed.
 */
void
AntialiasingSettingsView::Revert()
{
	if (!IsRevertable())
		return;

	fCurrentSubpixelAntialiasing = fSavedSubpixelAntialiasing;
	fCurrentHinting = fSavedHinting;
	fCurrentAverageWeight = fSavedAverageWeight;

	set_subpixel_antialiasing(fCurrentSubpixelAntialiasing);
	set_hinting_mode(fCurrentHinting);
	set_average_weight(fCurrentAverageWeight);

	_SetCurrentAntialiasing();
	_SetCurrentHinting();
	_SetCurrentAverageWeight();
}
