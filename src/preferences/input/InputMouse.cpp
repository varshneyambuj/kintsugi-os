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
 *   Authors:
 *       Preetpal Kaur <preetpalok123@gmail.com>
 */


/**
 * @file InputMouse.cpp
 * @brief Implementation of InputMouse, the mouse preferences card.
 *
 * InputMouse is the BView shown when the user selects a non-touchpad
 * pointing device in the InputWindow. It hosts a SettingsView with the
 * full set of mouse controls plus the standard Defaults and Revert
 * buttons; messages drive a per-device MouseSettings model.
 *
 * @see SettingsView, MouseSettings
 */


#include "InputMouse.h"

#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Debug.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <SeparatorView.h>

#include "InputConstants.h"
#include "MouseSettings.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InputMouse"


/**
 * @brief Constructs the mouse settings card.
 *
 * Stores the per-device MouseSettings, builds the inner SettingsView, and
 * lays out the Defaults and Revert buttons separated from the controls
 * with a horizontal separator. The Defaults button reflects whether
 * @a settings differs from the system defaults.
 *
 * @param dev       BInputDevice for the selected mouse; reserved for
 *                  future per-device behaviour and not yet used.
 * @param settings  MouseSettings model owned by the InputWindow; must
 *                  outlive this view.
 */
InputMouse::InputMouse(BInputDevice* dev, MouseSettings* settings)
	:
	BView("InputMouse", B_WILL_DRAW)
{
	fSettings = settings;

	fSettingsView = new SettingsView(*fSettings);

	fDefaultsButton = new BButton(B_TRANSLATE("Defaults"),
		new BMessage(kMsgDefaults));
	fDefaultsButton->SetEnabled(fSettings->IsDefaultable());

	fRevertButton = new BButton(B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert));
	fRevertButton->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.Add(fSettingsView)
			.Add(new BSeparatorView(B_HORIZONTAL))
				.AddGroup(B_HORIZONTAL)
				.Add(fDefaultsButton)
				.Add(fRevertButton)
				.AddGlue()
				.End()
		.End();
}


/**
 * @brief Destroys the card.
 *
 * @note The MouseSettings pointer was supplied by the InputWindow and is
 *       not owned by this view.
 */
InputMouse::~InputMouse()
{
}


/**
 * @brief Translates user input into MouseSettings updates.
 *
 * Handles every mouse-specific message: Defaults/Revert, mouse type,
 * focus mode, focus-follows-mouse mode, accept-first-click toggle,
 * double-click speed, mouse speed, acceleration factor, and button
 * mapping. After each change the Defaults and Revert button enabled
 * states are re-evaluated against the persisted defaults and the
 * on-entry snapshot. The mouse-speed and acceleration values are passed
 * through the same exponential mappings used by the touchpad card to
 * cover the slow/fast dynamic range expected by the kernel driver.
 *
 * @param message  Incoming BMessage. Unhandled messages fall through to
 *                 BView::MessageReceived.
 */
void
InputMouse::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgDefaults:
		{
			fSettings->Defaults();
			fSettingsView->UpdateFromSettings();

			fDefaultsButton->SetEnabled(false);
			fRevertButton->SetEnabled(fSettings->IsRevertable());
			break;
		}

		case kMsgRevert:
		{
			fSettings->Revert();
			fSettingsView->UpdateFromSettings();

			fDefaultsButton->SetEnabled(fSettings->IsDefaultable());
			fRevertButton->SetEnabled(false);
			break;
		}

		case kMsgMouseType:
		{
			int32 type;
			if (message->FindInt32("be:value", &type) == B_OK) {
				if (type > 6)
					debugger("Mouse type is invalid");
				fSettings->SetMouseType(type);
				fSettingsView->SetMouseType(type);
				fDefaultsButton->SetEnabled(fSettings->IsDefaultable());
				fRevertButton->SetEnabled(fSettings->IsRevertable());
			}
			break;
		}

		case kMsgMouseFocusMode:
		{
			int32 mode;
			if (message->FindInt32("be:value", &mode) == B_OK) {
				fSettings->SetMouseMode((mode_mouse)mode);
				fDefaultsButton->SetEnabled(fSettings->IsDefaultable());
				fRevertButton->SetEnabled(fSettings->IsRevertable());
				fSettingsView->fAcceptFirstClickBox->SetEnabled(
					mode != B_FOCUS_FOLLOWS_MOUSE);
			}
			break;
		}

		case kMsgFollowsMouseMode:
		{
			int32 mode;
			if (message->FindInt32("mode_focus_follows_mouse", &mode) == B_OK) {
				fSettings->SetFocusFollowsMouseMode(
					(mode_focus_follows_mouse)mode);
				fDefaultsButton->SetEnabled(fSettings->IsDefaultable());
				fRevertButton->SetEnabled(fSettings->IsRevertable());
			}
			break;
		}

		case kMsgAcceptFirstClick:
		{
			BHandler* handler;
			if (message->FindPointer(
					"source", reinterpret_cast<void**>(&handler))
				== B_OK) {
				bool acceptFirstClick = true;
				BCheckBox* acceptFirstClickBox
					= dynamic_cast<BCheckBox*>(handler);
				if (acceptFirstClickBox)
					acceptFirstClick
						= acceptFirstClickBox->Value() == B_CONTROL_ON;
				fSettings->SetAcceptFirstClick(acceptFirstClick);
				fDefaultsButton->SetEnabled(fSettings->IsDefaultable());
				fRevertButton->SetEnabled(fSettings->IsRevertable());
			}
			break;
		}

		case kMsgDoubleClickSpeed:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				// slow = 1000000, fast = 0
				fSettings->SetClickSpeed(1000000LL - value * 1000);
				fDefaultsButton->SetEnabled(fSettings->IsDefaultable());
				fRevertButton->SetEnabled(fSettings->IsRevertable());
			}
			break;
		}

		case kMsgMouseSpeed:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				// slow = 8192, fast = 524287
				fSettings->SetMouseSpeed(
					(int32)pow(2, value * 6.0 / 1000) * 8192);
				fDefaultsButton->SetEnabled(fSettings->IsDefaultable());
				fRevertButton->SetEnabled(fSettings->IsRevertable());
			}
			break;
		}

		case kMsgAccelerationFactor:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				// slow = 0, fast = 262144
				fSettings->SetAccelerationFactor(
					(int32)pow(value * 4.0 / 1000, 2) * 16384);
				fDefaultsButton->SetEnabled(fSettings->IsDefaultable());
				fRevertButton->SetEnabled(fSettings->IsRevertable());
			}
			break;
		}

		case kMsgMouseMap:
		{
			int32 index;
			int32 button;
			if (message->FindInt32("index", &index) == B_OK
				&& message->FindInt32("button", &button) == B_OK) {
				int32 mapping = B_MOUSE_BUTTON(index + 1);
				fSettings->SetMapping(button, mapping);
				fDefaultsButton->SetEnabled(fSettings->IsDefaultable());
				fRevertButton->SetEnabled(fSettings->IsRevertable());
				fSettingsView->MouseMapUpdated();
			}
			break;
		}

		default:
			BView::MessageReceived(message);
	}
}
