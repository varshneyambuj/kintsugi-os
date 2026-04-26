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
 * @file InputKeyboard.cpp
 * @brief Implementation of InputKeyboard, the keyboard preferences card.
 *
 * InputKeyboard is the BView the InputWindow shows when the user selects
 * a keyboard input device. It hosts a KeyboardView (sliders for repeat
 * rate and delay plus a typing test area) and the standard Defaults and
 * Revert buttons; messages drive the underlying KeyboardSettings model.
 *
 * @see KeyboardView, KeyboardSettings
 */


#include "InputKeyboard.h"

#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Message.h>
#include <SeparatorView.h>
#include <Slider.h>
#include <TextControl.h>

#include "InputConstants.h"
#include "KeyboardView.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InputKeyboard"


/**
 * @brief Constructs the keyboard settings card.
 *
 * Builds the inner KeyboardView, the Defaults and Revert buttons, and
 * lays them out vertically with a horizontal separator. Initialises the
 * sliders from the persisted KeyboardSettings.
 *
 * @param dev  BInputDevice for the selected keyboard. Currently unused;
 *             reserved for per-device overrides.
 */
InputKeyboard::InputKeyboard(BInputDevice* dev)
	:
	BView("InputKeyboard", B_WILL_DRAW)
{
	// Add the main settings view
	fSettingsView = new KeyboardView();

	// Add the "Default" button..
	fDefaultsButton = new BButton(B_TRANSLATE("Defaults"),
        new BMessage(kMsgDefaults));

	// Add the "Revert" button...
	fRevertButton = new BButton(B_TRANSLATE("Revert"),
        new BMessage(kMsgRevert));
	fRevertButton->SetEnabled(false);

	// Build the layout
	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.AddGroup(B_HORIZONTAL)
			.SetInsets(B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING,
				B_USE_WINDOW_SPACING, 0)
			.Add(fSettingsView)
			.End()
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL)
			.Add(fDefaultsButton)
			.Add(fRevertButton)
			.AddGlue()
			.End();

	BSlider* slider = (BSlider*)FindView("key_repeat_rate");
	if (slider != NULL)
		slider->SetValue(fSettings.KeyboardRepeatRate());

	slider = (BSlider*)FindView("delay_until_key_repeat");
	if (slider != NULL)
		slider->SetValue(fSettings.KeyboardRepeatDelay());

	fDefaultsButton->SetEnabled(fSettings.IsDefaultable());
}


/**
 * @brief Translates user input into KeyboardSettings updates.
 *
 * Handles Defaults/Revert button presses, the repeat-rate slider, and the
 * delay-until-repeat slider. The delay slider is snapped to the four
 * canonical positions (250, 500, 750, 1000 ms) so it behaves like the
 * legacy Keyboard preferences app. Updates the enabled state of the
 * Defaults and Revert buttons after every change.
 *
 * @param message  Incoming BMessage. Unhandled messages fall through to
 *                 BView::MessageReceived.
 */
void
InputKeyboard::MessageReceived(BMessage* message)
{
	BSlider* slider = NULL;

	switch (message->what) {
		case kMsgDefaults:
		{
			fSettings.Defaults();

			slider = (BSlider*)FindView("key_repeat_rate");
			if (slider != NULL)
				slider->SetValue(fSettings.KeyboardRepeatRate());

			slider = (BSlider*)FindView("delay_until_key_repeat");
			if (slider != NULL)
				slider->SetValue(fSettings.KeyboardRepeatDelay());

			fDefaultsButton->SetEnabled(false);

			fRevertButton->SetEnabled(true);
			break;
		}
		case kMsgRevert:
		{
			fSettings.Revert();

			slider = (BSlider*)FindView("key_repeat_rate");
			if (slider != NULL)
				slider->SetValue(fSettings.KeyboardRepeatRate());

			slider = (BSlider*)FindView("delay_until_key_repeat");
			if (slider != NULL)
				slider->SetValue(fSettings.KeyboardRepeatDelay());

			fDefaultsButton->SetEnabled(fSettings.IsDefaultable());

			fRevertButton->SetEnabled(false);
			break;
		}
		case kMsgSliderrepeatrate:
		{
			int32 rate;
			if (message->FindInt32("be:value", &rate) != B_OK)
				break;
			fSettings.SetKeyboardRepeatRate(rate);

			fDefaultsButton->SetEnabled(fSettings.IsDefaultable());

			fRevertButton->SetEnabled(true);
			break;
		}
		case kMsgSliderdelayrate:
		{
			int32 delay;
			if (message->FindInt32("be:value", &delay) != B_OK)
				break;

			// We need to look at the value from the slider and make it "jump"
			// to the next notch along. Setting the min and max values of the
			// slider to 1 and 4 doesn't work like the real Keyboard app.
			if (delay < 375000)
				delay = 250000;
			if (delay >= 375000 && delay < 625000)
				delay = 500000;
			if (delay >= 625000 && delay < 875000)
				delay = 750000;
			if (delay >= 875000)
				delay = 1000000;

			fSettings.SetKeyboardRepeatDelay(delay);

			slider = (BSlider*)FindView("delay_until_key_repeat");
			if (slider != NULL)
				slider->SetValue(delay);

			fDefaultsButton->SetEnabled(fSettings.IsDefaultable());

			fRevertButton->SetEnabled(true);
			break;
		}
		default:
			BView::MessageReceived(message);
	}
}
