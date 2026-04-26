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
 *   Copyright 2004-2006, the Haiku project. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors in chronological order:
 *       mccall@digitalparadise.co.uk
 *       Jérôme Duval
 *       Marcus Overhagen
 */


/**
 * @file KeyboardView.cpp
 * @brief Implementation of KeyboardView, the visual portion of the Keyboard preferences pane.
 *
 * KeyboardView lays out the two sliders (key repeat rate, delay until repeat),
 * a typing test text box, and renders decorative bitmaps next to the sliders.
 */


#include "KeyboardView.h"

#include <Bitmap.h>
#include <Catalog.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Slider.h>
#include <TextControl.h>

#include "InputConstants.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "KeyboardView"


/**
 * @brief Constructs the keyboard preferences view and builds its layout.
 *
 * Creates the repeat-rate slider, the delay-until-repeat slider, and the
 * typing test text control, then arranges them in a vertical group.
 */
KeyboardView::KeyboardView()
	:
	BGroupView()
{
	// Create the "Key repeat rate" slider...
	fRepeatSlider
		= new BSlider("key_repeat_rate", B_TRANSLATE("Key repeat rate"),
			new BMessage(kMsgSliderrepeatrate), 20, 300, B_HORIZONTAL);
	fRepeatSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fRepeatSlider->SetHashMarkCount(5);
	fRepeatSlider->SetLimitLabels(B_TRANSLATE("Slow"), B_TRANSLATE("Fast"));
	fRepeatSlider->SetExplicitMinSize(BSize(200, B_SIZE_UNSET));


	// Create the "Delay until key repeat" slider...
	fDelaySlider = new BSlider("delay_until_key_repeat",
		B_TRANSLATE("Delay until key repeat"),
		new BMessage(kMsgSliderdelayrate), 250000, 1000000, B_HORIZONTAL);
	fDelaySlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fDelaySlider->SetHashMarkCount(4);
	fDelaySlider->SetLimitLabels(B_TRANSLATE("Short"), B_TRANSLATE("Long"));

	// Create the "Typing test area" text box...
	BTextControl* textcontrol = new BTextControl(
		NULL, B_TRANSLATE("Typing test area"), new BMessage('TTEA'));
	textcontrol->SetAlignment(B_ALIGN_LEFT, B_ALIGN_CENTER);
	textcontrol->SetExplicitMinSize(
		BSize(textcontrol->StringWidth(B_TRANSLATE("Typing test area")),
			B_SIZE_UNSET));

	// Build the layout
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.Add(fRepeatSlider)
		.Add(fDelaySlider)
		.Add(textcontrol)
		.AddGlue();
}


/**
 * @brief Destroys the view; child controls are owned by the layout system.
 */
KeyboardView::~KeyboardView()
{
}


/**
 * @brief Renders icon and clock bitmaps next to the repeat and delay sliders.
 *
 * @param updateFrame  Region requested for redraw (currently unused; bitmaps
 *                     are drawn unconditionally relative to the slider frames).
 */
void
KeyboardView::Draw(BRect updateFrame)
{
	BPoint pt;
	pt.x = fRepeatSlider->Frame().right + 10;

	if (fIconBitmap != NULL) {
		pt.y = fRepeatSlider->Frame().bottom - 35
			- fIconBitmap->Bounds().Height() / 3;
		DrawBitmap(fIconBitmap, pt);
	}

	if (fClockBitmap != NULL) {
		pt.y = fDelaySlider->Frame().bottom - 35
			- fClockBitmap->Bounds().Height() / 3;
		DrawBitmap(fClockBitmap, pt);
	}
}
