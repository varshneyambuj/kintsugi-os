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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the OpenTracker License.
 */


/**
 * @file MiniMenuField.cpp
 * @brief A compact BView that presents a BPopUpMenu with a drawn arrow indicator.
 *
 * MiniMenuField renders a small rectangular button with a right-pointing
 * triangle.  Clicking the view or pressing Space/Down opens the attached
 * BPopUpMenu.  A focus ring is drawn when the view has keyboard focus.
 *
 * @see BPopUpMenu
 */


#include <ControlLook.h>
#include <InterfaceDefs.h>
#include <PopUpMenu.h>
#include <Window.h>

#include "MiniMenuField.h"
#include "Utilities.h"


/**
 * @brief Construct a MiniMenuField in the given frame.
 *
 * @param frame        The frame rectangle in the parent's coordinate system.
 * @param name         The view name.
 * @param menu         The BPopUpMenu to display; ownership is taken.
 * @param resizeFlags  Resize flags passed to BView.
 * @param flags        View flags passed to BView.
 */
MiniMenuField::MiniMenuField(BRect frame, const char* name, BPopUpMenu* menu,
	uint32 resizeFlags, uint32 flags)
	:
	BView(frame, name, resizeFlags, flags),
	fMenu(menu)
{
	SetFont(be_plain_font, B_FONT_FAMILY_AND_STYLE | B_FONT_SIZE);
}


/**
 * @brief Destroy the MiniMenuField, deleting the attached pop-up menu.
 */
MiniMenuField::~MiniMenuField()
{
	delete fMenu;
}


/**
 * @brief Inherit parent colours and set the high colour to black on attachment.
 */
void
MiniMenuField::AttachedToWindow()
{
	AdoptParentColors();
	SetHighColor(0, 0, 0);
}


/**
 * @brief Invalidate the view so the focus ring is redrawn when focus changes.
 *
 * @param on  True when gaining keyboard focus, false when losing it.
 */
void
MiniMenuField::MakeFocus(bool on)
{
	Invalidate();
	BView::MakeFocus(on);
}


/**
 * @brief Open the pop-up menu on Space, Down, or Right arrow key press.
 *
 * @param bytes     Pointer to the UTF-8 byte sequence of the pressed key.
 * @param numBytes  Length of @p bytes.
 */
void
MiniMenuField::KeyDown(const char* bytes, int32 numBytes)
{
	switch (bytes[0]) {
		case B_SPACE:
		case B_DOWN_ARROW:
		case B_RIGHT_ARROW:
			// invoke from keyboard
			fMenu->Go(ConvertToScreen(BPoint(4, 4)), true, true);
			break;

		default:
			BView::KeyDown(bytes, numBytes);
			break;
	}
}


/**
 * @brief Render the mini menu field button with optional focus ring and arrow triangle.
 */
void
MiniMenuField::Draw(BRect)
{
	BRect bounds(Bounds());
	bounds.OffsetBy(1, 2);
	bounds.right--;
	bounds.bottom -= 2;
	if (IsFocus()) {
		// draw the focus indicator border
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		StrokeRect(bounds);
	}
	bounds.right--;
	bounds.bottom--;
	BRect rect(bounds);
	rect.InsetBy(1, 1);

	rgb_color darkest = tint_color(kBlack, 0.6f);
	rgb_color dark = tint_color(kBlack, 0.4f);
	rgb_color medium = dark;
	rgb_color light = tint_color(kBlack, 0.03f);

	SetHighColor(medium);

	// draw frame and shadow
	BeginLineArray(10);
	AddLine(rect.RightTop(), rect.RightBottom(), darkest);
	AddLine(rect.RightBottom(), rect.LeftBottom(), darkest);
	AddLine(rect.LeftBottom(), rect.LeftTop(), medium);
	AddLine(rect.LeftTop(), rect.RightTop(), medium);
	AddLine(bounds.LeftBottom() + BPoint(2, 0), bounds.RightBottom(), dark);
	AddLine(bounds.RightTop() + BPoint(0, 1), bounds.RightBottom(), dark);
	rect.InsetBy(1, 1);
	AddLine(rect.RightTop(), rect.RightBottom(), medium);
	AddLine(rect.RightBottom(), rect.LeftBottom(), medium);
	AddLine(rect.LeftBottom(), rect.LeftTop(), light);
	AddLine(rect.LeftTop(), rect.RightTop(), light);
	EndLineArray();

	// draw triangle
	rect = BRect(0, 0, 12, 12);
	rect.OffsetBy(4, 4);
	const rgb_color arrowColor = {150, 150, 150, 255};
	float tint = Window()->IsActive() ? B_DARKEN_3_TINT : B_DARKEN_1_TINT;

	SetDrawingMode(B_OP_COPY);
	be_control_look->DrawArrowShape(this, rect, rect, arrowColor,
		BControlLook::B_RIGHT_ARROW, 0, tint);
}


/**
 * @brief Open the pop-up menu when the primary mouse button is pressed.
 */
void
MiniMenuField::MouseDown(BPoint)
{
	fMenu->Go(ConvertToScreen(BPoint(4, 4)), true);
}
