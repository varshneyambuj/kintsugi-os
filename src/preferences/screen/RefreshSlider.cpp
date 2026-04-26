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
 *   Copyright 2001-2006, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Rafael Romo
 *       Stefano Ceccherini (burton666@libero.it)
 *       Axel Doerfler, axeld@pinc-software.de
 */


/**
 * @file RefreshSlider.cpp
 * @brief Slider widget for picking a custom refresh rate in 0.1 Hz steps.
 */


#include "RefreshSlider.h"
#include "Constants.h"

#include <Catalog.h>
#include <String.h>
#include <Window.h>

#include <new>
#include <stdio.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Screen"


/**
 * @brief Construct a refresh-rate slider with the given Hz range.
 *
 * The internal range is encoded as @c min*10 .. @c max*10 to retain one
 * decimal of precision. Hash marks are placed every 5 Hz.
 *
 * @param frame        Initial frame.
 * @param min          Minimum selectable refresh rate in Hz.
 * @param max          Maximum selectable refresh rate in Hz.
 * @param resizingMode Standard BView resizing mode flags.
 */
RefreshSlider::RefreshSlider(BRect frame, float min, float max, uint32 resizingMode)
	: BSlider(frame, B_TRANSLATE("Screen"), B_TRANSLATE("Refresh rate:"),
		new BMessage(SLIDER_INVOKE_MSG), (int32)rintf(min * 10), (int32)rintf(max * 10),
		B_BLOCK_THUMB, resizingMode),
	fStatus(new (std::nothrow) char[32])
{
	BString minRefresh;
	minRefresh << (uint32)min;
	BString maxRefresh;
	maxRefresh << (uint32)max;
	SetLimitLabels(minRefresh.String(), maxRefresh.String());

	SetHashMarks(B_HASH_MARKS_BOTTOM);
	SetHashMarkCount(uint32(max - min) / 5 + 1);

	SetKeyIncrementValue(1);
}


/**
 * @brief Free the cached "%.1f Hz" status string buffer.
 */
RefreshSlider::~RefreshSlider()
{
	delete[] fStatus;
}


/**
 * @brief Draw a blue rectangle inside the thumb when this slider has focus.
 */
void
RefreshSlider::DrawFocusMark()
{
	if (IsFocus()) {
		rgb_color blue = { 0, 0, 229, 255 };
		
		BRect rect(ThumbFrame());		
		BView *view = OffscreenView();
				
		rect.InsetBy(2.0, 2.0);
		rect.right--;
		rect.bottom--;
		
		view->SetHighColor(blue);
		view->StrokeRect(rect);
	}
}


/**
 * @brief Move the slider by 0.1 Hz when the left/right arrow keys are pressed.
 *
 * @param bytes    Bytes of the pressed key sequence.
 * @param numBytes Length of @a bytes (unused).
 */
void
RefreshSlider::KeyDown(const char *bytes, int32 numBytes)
{
	switch (*bytes) {
		case B_LEFT_ARROW:
		{
			SetValue(Value() - 1);
			Invoke();
			break;
		}
		
		case B_RIGHT_ARROW:
		{
			SetValue(Value() + 1);
			Invoke();
			break;
		}

		default:
			break;
	}
}


/**
 * @brief Build the live status text shown next to the slider thumb.
 *
 * Converts the integer slider value back to floating-point Hz and formats
 * it as "%.1f Hz" into the cached @c fStatus buffer.
 *
 * @return Pointer to the cached status string, or NULL if allocation
 *         failed during construction.
 */
const char*
RefreshSlider::UpdateText() const
{
	if (fStatus != NULL)
		snprintf(fStatus, 32, B_TRANSLATE("%.1f Hz"), (float)Value() / 10);

	return fStatus;
}
