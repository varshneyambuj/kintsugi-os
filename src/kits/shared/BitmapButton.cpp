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
 *   Copyright 2010 Stephan Aßmus <superstippi@gmx.de>. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 */

/** @file BitmapButton.cpp
 *  @brief Implements BBitmapButton, a BButton subclass that renders a bitmap
 *         image instead of (or in addition to) a text label.
 */

#include "BitmapButton.h"

#include <string.h>

#include <Bitmap.h>
#include <ControlLook.h>
#include <TranslationUtils.h>


static const float kFrameInset = 2;


/** @brief Constructs a BBitmapButton by loading a bitmap from a named resource.
 *  @param resourceName  Name of the resource to load via BTranslationUtils.
 *  @param message       The message sent when the button is clicked.
 */
BBitmapButton::BBitmapButton(const char* resourceName, BMessage* message)
	:
	BButton("", message),
	fBitmap(BTranslationUtils::GetBitmap(resourceName)),
	fBackgroundMode(BUTTON_BACKGROUND)
{
}


/** @brief Constructs a BBitmapButton from raw bitmap data.
 *  @param bits     Pointer to the raw pixel data.
 *  @param width    Width of the bitmap in pixels.
 *  @param height   Height of the bitmap in pixels.
 *  @param format   Color space of the raw pixel data.
 *  @param message  The message sent when the button is clicked.
 */
BBitmapButton::BBitmapButton(const uint8* bits, uint32 width, uint32 height,
		color_space format, BMessage* message)
	:
	BButton("", message),
	fBitmap(new BBitmap(BRect(0, 0, width - 1, height - 1), 0, format)),
	fBackgroundMode(BUTTON_BACKGROUND)
{
	memcpy(fBitmap->Bits(), bits, fBitmap->BitsLength());
}


/** @brief Destructor — frees the owned bitmap. */
BBitmapButton::~BBitmapButton()
{
	delete fBitmap;
}


/** @brief Returns the minimum size required to display the bitmap with padding.
 *  @return A BSize with the bitmap dimensions plus kFrameInset on each side.
 */
BSize
BBitmapButton::MinSize()
{
	BSize min(0, 0);
	if (fBitmap) {
		min.width = fBitmap->Bounds().Width();
		min.height = fBitmap->Bounds().Height();
	}
	min.width += kFrameInset * 2;
	min.height += kFrameInset * 2;
	return min;
}


/** @brief Returns the maximum size, which is unlimited in both dimensions.
 *  @return B_SIZE_UNLIMITED for width and height.
 */
BSize
BBitmapButton::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
}


/** @brief Returns the preferred size, equal to the minimum size.
 *  @return The result of MinSize().
 */
BSize
BBitmapButton::PreferredSize()
{
	return MinSize();
}


/** @brief Draws the button background and the centered bitmap.
 *
 *  The background rendering depends on fBackgroundMode: a regular button
 *  background is used normally, or a menu-bar style background when the
 *  button is used inside a menu bar.  When disabled, the bitmap is drawn
 *  with reduced opacity.
 *
 *  @param updateRect  The rectangle that needs redrawing.
 */
void
BBitmapButton::Draw(BRect updateRect)
{
	BRect bounds(Bounds());
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	uint32 flags = be_control_look->Flags(this);

	if (fBackgroundMode == BUTTON_BACKGROUND || Value() == B_CONTROL_ON) {
		be_control_look->DrawButtonBackground(this, bounds, updateRect, base,
			flags);
	} else {
		SetHighColor(tint_color(base, B_DARKEN_2_TINT));
		StrokeLine(bounds.LeftBottom(), bounds.RightBottom());
		bounds.bottom--;
		be_control_look->DrawMenuBarBackground(this, bounds, updateRect, base,
			flags);
	}

	if (fBitmap == NULL)
		return;

	SetDrawingMode(B_OP_ALPHA);

	if (!IsEnabled()) {
		SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
		SetHighColor(0, 0, 0, 120);
	}

	BRect bitmapBounds(fBitmap->Bounds());
	BPoint bitmapLocation(
		floorf((bounds.left + bounds.right
			- (bitmapBounds.left + bitmapBounds.right)) / 2 + 0.5f),
		floorf((bounds.top + bounds.bottom
			- (bitmapBounds.top + bitmapBounds.bottom)) / 2 + 0.5f));

	DrawBitmap(fBitmap, bitmapLocation);
}


/** @brief Sets the background rendering mode and triggers a redraw if changed.
 *  @param mode  One of the background mode constants (e.g. BUTTON_BACKGROUND
 *               or MENU_BACKGROUND).
 */
void
BBitmapButton::SetBackgroundMode(uint32 mode)
{
	if (fBackgroundMode != mode) {
		fBackgroundMode = mode;
		Invalidate();
	}
}
