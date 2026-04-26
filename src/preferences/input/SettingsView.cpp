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
 *   Copyright 2019, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Author:
 *       Preetpal Kaur <preetpalok123@gmail.com>
 */


/**
 * @file SettingsView.cpp
 * @brief Implementation of SettingsView, the shared layout for mouse preferences.
 *
 * SettingsView assembles the mouse-type popup, the schematic MouseView,
 * the click-speed/mouse-speed/acceleration sliders, and the focus-mode
 * controls, and binds them to a MouseSettings model. The actual data
 * lives in the MouseSettings object passed in at construction time.
 */


#include "SettingsView.h"

#include <Box.h>
#include <Catalog.h>
#include <Debug.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <SeparatorView.h>
#include <Slider.h>
#include <TextControl.h>

#include "InputConstants.h"
#include "MouseSettings.h"
#include "MouseView.h"


//	#pragma mark -

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SettingsView"


/**
 * @brief Constructs the settings view and builds its child controls.
 *
 * Creates the mouse-type popup (1- to 6-button), the click-speed,
 * mouse-speed, and acceleration sliders, the schematic MouseView, the
 * focus-mode popup, and the accept-first-click checkbox, then arranges
 * them in a two-column layout with the focus-mode block underneath.
 *
 * @param settings  Mouse settings model bound to this view; must
 *                  outlive the view.
 * @note The constructor aborts via debugger() if the model reports more
 *       than 6 buttons.
 */
SettingsView::SettingsView(MouseSettings& settings)
	:
	BBox("main_view"),
	fSettings(settings)
{
	if (fSettings.MouseType() > 6)
		debugger("Mouse type is invalid");

	// Add the "Mouse Type" pop up menu
	fTypeMenu = new BOptionPopUp(
		"type", B_TRANSLATE("Mouse type:"), new BMessage(kMsgMouseType));
	fTypeMenu->AddOption(B_TRANSLATE("1-Button"), 1);
	fTypeMenu->AddOption(B_TRANSLATE("2-Button"), 2);
	fTypeMenu->AddOption(B_TRANSLATE("3-Button"), 3);
	fTypeMenu->AddOption(B_TRANSLATE("4-Button"), 4);
	fTypeMenu->AddOption(B_TRANSLATE("5-Button"), 5);
	fTypeMenu->AddOption(B_TRANSLATE("6-Button"), 6);

	// Create the "Double-click speed slider...
	fClickSpeedSlider
		= new BSlider("double_click_speed", B_TRANSLATE("Double-click speed"),
			new BMessage(kMsgDoubleClickSpeed), 0, 1000, B_HORIZONTAL);
	fClickSpeedSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fClickSpeedSlider->SetHashMarkCount(7);

	// Create the "Mouse Speed" slider...
	fMouseSpeedSlider = new BSlider("mouse_speed", B_TRANSLATE("Mouse speed"),
		new BMessage(kMsgMouseSpeed), 0, 1000, B_HORIZONTAL);
	fMouseSpeedSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fMouseSpeedSlider->SetHashMarkCount(7);

	// Create the "Mouse Acceleration" slider...
	fAccelerationSlider
		= new BSlider("mouse_acceleration", B_TRANSLATE("Mouse acceleration"),
			new BMessage(kMsgAccelerationFactor), 0, 1000, B_HORIZONTAL);
	fAccelerationSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fAccelerationSlider->SetHashMarkCount(7);

	fMouseView = new MouseView(fSettings);

	// Create the "Double-click test area" text box...
	const char* label = B_TRANSLATE("Double-click test area");
	BTextControl* doubleClickTextControl = new BTextControl(NULL, label, NULL);
	doubleClickTextControl->SetAlignment(B_ALIGN_LEFT, B_ALIGN_CENTER);
	doubleClickTextControl->SetExplicitMinSize(
		BSize(StringWidth(label), B_SIZE_UNSET));

	// Add the "Mouse focus mode" pop up menu
	fFocusMenu = new BOptionPopUp("focus_mode", B_TRANSLATE("Focus mode:"),
		new BMessage(kMsgMouseFocusMode));

	const char* focusLabels[] = {B_TRANSLATE_MARK("Click to focus and raise"),
		B_TRANSLATE_MARK("Click to focus"),
		B_TRANSLATE_MARK("Focus follows mouse")};
	const mode_mouse focusModes[]
		= {B_NORMAL_MOUSE, B_CLICK_TO_FOCUS_MOUSE, B_FOCUS_FOLLOWS_MOUSE};

	for (size_t i = 0; i < B_COUNT_OF(focusModes); i++)
		fFocusMenu->AddOption(
			B_TRANSLATE_NOCOLLECT(focusLabels[i]), focusModes[i]);

	// Add the "Click-through" check box
	fAcceptFirstClickBox = new BCheckBox(
		B_TRANSLATE("Accept first click"), new BMessage(kMsgAcceptFirstClick));

	// Build the layout

	// Layout is :
	// A | B
	// -----
	//   C

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		// Horizontal : A|B
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)

			// Vertical block A: mouse type/view/test
			.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING)
				.Add(fTypeMenu)
				.AddGroup(B_HORIZONTAL, 0)
					.AddGlue()
					.Add(fMouseView)
					.AddGlue()
					.End()
				.AddGlue()
				.Add(doubleClickTextControl)
				.End()
			.Add(new BSeparatorView(B_VERTICAL))

			// Vertical block B: speed settings
			.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING, 3)
				.AddGroup(B_HORIZONTAL, 0)
					.Add(fClickSpeedSlider)
					.End()
				.AddGroup(B_HORIZONTAL, 0)
					.Add(fMouseSpeedSlider)
					.End()
				.AddGroup(B_HORIZONTAL, 0)
					.Add(fAccelerationSlider)
					.End()
				.End()
			.End()
		.AddStrut(B_USE_DEFAULT_SPACING)

		// Horizontal Block C: focus mode
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fFocusMenu)
			.AddGlue()
			.AddGroup(B_VERTICAL, 0)
				.Add(fAcceptFirstClickBox)
				.End()
			.End();

	SetBorder(B_NO_BORDER);
}


/**
 * @brief Destroys the view. Child controls are owned by the layout system.
 */
SettingsView::~SettingsView()
{
}


/**
 * @brief Synchronises the controls with the current model state when shown.
 */
void
SettingsView::AttachedToWindow()
{
	UpdateFromSettings();
}


/**
 * @brief Forwards a button-count change to the embedded MouseView.
 *
 * @param type  Number of buttons (1..6).
 * @note Aborts via debugger() if @a type exceeds 6.
 */
void
SettingsView::SetMouseType(int32 type)
{
	if (type > 6)
		debugger("Mouse type is invalid");
	fMouseView->SetMouseType(type);
}


/**
 * @brief Notifies the embedded MouseView that the button mapping changed.
 */
void
SettingsView::MouseMapUpdated()
{
	fMouseView->MouseMapUpdated();
}


/**
 * @brief Pushes the model state into every child control.
 *
 * Maps the click-speed and mouse-speed values through their respective
 * slider scales, selects the right options in the type and focus
 * popups, and refreshes the schematic MouseView.
 */
void
SettingsView::UpdateFromSettings()
{
	int32 value = int32((1000000LL - fSettings.ClickSpeed()) / 1000);
	// slow = 1000000, fast = 0
	fClickSpeedSlider->SetValue(value);

	value = int32((log(fSettings.MouseSpeed() / 8192.0) / log(2)) * 1000 / 6);
	// slow = 8192, fast = 524287
	fMouseSpeedSlider->SetValue(value);

	value = int32(sqrt(fSettings.AccelerationFactor() / 16384.0) * 1000 / 4);
	// slow = 0, fast = 262144
	fAccelerationSlider->SetValue(value);

	fTypeMenu->SelectOptionFor(fSettings.MouseType());
	fMouseView->SetMouseType(fSettings.MouseType());

	fFocusMenu->SelectOptionFor(fSettings.MouseMode());

	fAcceptFirstClickBox->SetValue(
		fSettings.AcceptFirstClick() ? B_CONTROL_ON : B_CONTROL_OFF);
	fAcceptFirstClickBox->SetEnabled(
		fSettings.MouseMode() != B_FOCUS_FOLLOWS_MOUSE);
}
