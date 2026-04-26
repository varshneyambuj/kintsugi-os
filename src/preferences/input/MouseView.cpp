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
 * @file MouseView.cpp
 * @brief Implementation of MouseView, the schematic mouse drawing widget.
 *
 * MouseView paints a stylised mouse with one to six labelled buttons,
 * highlights the button currently held down by the user, and pops up a
 * mapping menu when the user clicks a button so that they can reassign
 * its logical role.
 */


#include "MouseView.h"

#include <algorithm>

#include <Box.h>
#include <Button.h>
#include <Debug.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <Shape.h>
#include <Slider.h>
#include <TextControl.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>
#include <Window.h>

#include "InputConstants.h"
#include "MouseSettings.h"


/** @brief Top inset of the rendered button strip, in unscaled units. */
static const int32 kButtonTop = 3;
/** @brief Width of the rendered mouse-button strip, in unscaled units. */
static const int32 kMouseDownWidth = 72;
/** @brief Height of the rendered mouse-button strip, in unscaled units. */
static const int32 kMouseDownHeight = 35;

#define W kMouseDownWidth / 100
static const int32 kButtonOffsets[][7] = {
	{ 0, 100 * W },
	{ 0, 50 * W, 100 * W },
	{ 0, 35 * W, 65 * W, 100 * W },
	{ 0, 25 * W, 50 * W, 75 * W, 100 * W },
	{ 0, 20 * W, 40 * W, 60 * W, 80 * W, 100 * W },
	{ 0, 19 * W, 34 * W, 50 * W, 66 * W, 82 * W, 100 * W }
};
#undef W

static const rgb_color kButtonTextColor = {0, 0, 0, 255};
static const rgb_color kMouseShadowColor = {100, 100, 100, 128};
static const rgb_color kMouseBodyTopColor = {0xed, 0xed, 0xed, 255};
static const rgb_color kMouseBodyBottomColor = {0x85, 0x85, 0x85, 255};
static const rgb_color kMouseOutlineColor = {0x51, 0x51, 0x51, 255};
static const rgb_color kMouseButtonOutlineColor = {0xa0, 0xa0, 0xa0, 255};
static const rgb_color kButtonPressedColor = {110, 110, 110, 110};


/**
 * @brief Returns the per-button x-offset row appropriate for a given button count.
 *
 * @param type  Configured number of buttons (1..6).
 * @return      Pointer to a static row of cumulative x positions; falls back
 *              to the 3-button row when @a type is out of range.
 */
static const int32*
getButtonOffsets(int32 type)
{
	if ((type - 1) >= (int32)B_COUNT_OF(kButtonOffsets))
		return kButtonOffsets[2];
	return kButtonOffsets[type - 1];
}


/**
 * @brief Converts a single-bit B_MOUSE_BUTTON mask into its 0-based index.
 *
 * @param mapping  Single-bit mask such as B_PRIMARY_MOUSE_BUTTON.
 * @return         Zero-based index of the set bit, or 0 if @a mapping is 0.
 */
static uint32
getMappingNumber(uint32 mapping)
{
	if (mapping == 0)
		return 0;

	int i;
	for (i = 0; mapping != 1; i++)
		mapping >>= 1;

	return i;
}


/**
 * @brief Constructs a MouseView observing the given settings model.
 *
 * Sets up event masks needed to render pressed-button feedback in real
 * time and computes a font-derived scaling factor so the schematic
 * mouse remains legible at unusual font sizes.
 *
 * @param settings  Settings model owned by the parent pane; must outlive
 *                  this view.
 */
MouseView::MouseView(const MouseSettings& settings)
	:
	BView("Mouse", B_PULSE_NEEDED | B_WILL_DRAW),
	fSettings(settings),
	fType(-1),
	fButtons(0),
	fOldButtons(0)
{
	SetEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	fScaling = std::max(1.0f, be_plain_font->Size() / 7.0f);
}


/**
 * @brief Destroys the view. The settings reference is not owned.
 */
MouseView::~MouseView()
{
}


/**
 * @brief Updates the rendered button count and triggers a redraw.
 *
 * @param type  Number of buttons to render (1..6).
 */
void
MouseView::SetMouseType(int32 type)
{
	fType = type;
	Invalidate();
}


/**
 * @brief Notifies the view that the underlying button mapping changed.
 */
void
MouseView::MouseMapUpdated()
{
	Invalidate();
}


/**
 * @brief Pulls the current button count from the settings and applies it.
 *
 * @note Aborts with debugger() if the settings report more than 6 buttons.
 */
void
MouseView::UpdateFromSettings()
{
	if (fSettings.MouseType() > 6)
		debugger("Mouse type is invalid");
	SetMouseType(fSettings.MouseType());
}


/**
 * @brief Reports the natural drawing size of the schematic mouse.
 *
 * @param _width   Output for preferred width; may be NULL.
 * @param _height  Output for preferred height; may be NULL.
 */
void
MouseView::GetPreferredSize(float* _width, float* _height)
{
	if (_width != NULL)
		*_width = fScaling * (kMouseDownWidth + 2);
	if (_height != NULL)
		*_height = fScaling * 104;
}


/**
 * @brief Initialises font metrics and the cached button outline picture.
 */
void
MouseView::AttachedToWindow()
{
	AdoptParentColors();

	UpdateFromSettings();
	_CreateButtonsPicture();

	font_height fontHeight;
	GetFontHeight(&fontHeight);
	fDigitHeight = int32(ceilf(fontHeight.ascent) + ceilf(fontHeight.descent));
	fDigitBaseline = int32(ceilf(fontHeight.ascent));
}


/**
 * @brief Clears the pressed-buttons state and redraws the button strip.
 */
void
MouseView::MouseUp(BPoint)
{
	fButtons = 0;
	Invalidate(_ButtonsRect());
	fOldButtons = fButtons;
}


/**
 * @brief Handles a click on the schematic mouse.
 *
 * Updates the pressed-buttons state for visual feedback. If the click
 * lands on a recognised button, opens a popup menu for choosing which
 * logical mouse button (1..6) is mapped to that physical button.
 *
 * @param where  Click position in view coordinates.
 */
void
MouseView::MouseDown(BPoint where)
{
	BMessage* mouseMsg = Window()->CurrentMessage();
	fButtons = mouseMsg->FindInt32("buttons");
	int32 modifiers = mouseMsg->FindInt32("modifiers");
	if (modifiers & B_CONTROL_KEY) {
		if (modifiers & B_COMMAND_KEY)
			fButtons = B_TERTIARY_MOUSE_BUTTON;
		else
			fButtons = B_SECONDARY_MOUSE_BUTTON;
	}
	// Get the current clipping region before requesting any updates.
	// Otherwise those parts would be excluded from the region.
	BRegion clipping;
	GetClippingRegion(&clipping);

	if (fOldButtons != fButtons) {
		Invalidate(_ButtonsRect());
		fOldButtons = fButtons;
	}

	const int32* offset = getButtonOffsets(fType);
	int32 button = -1;
	for (int32 i = 0; i <= fType; i++) {
		if (_ButtonRect(offset, i).Contains(where)) {
			button = i;
			break;
		}
	}
	if (button < 0)
		return;

	if (clipping.Contains(where)) {
		button = _ConvertFromVisualOrder(button);

		BPopUpMenu menu("Mouse Map Menu");
		BMessage message(kMsgMouseMap);
		message.AddInt32("button", button);

		for (int i = 1; i < 7; i++) {
			char tmp[2];
			sprintf(tmp, "%d", i);
			menu.AddItem(new BMenuItem(tmp, new BMessage(message)));
		}

		int32 mapping = fSettings.Mapping(button);
		BMenuItem* item = menu.ItemAt(getMappingNumber(mapping));
		if (item)
			item->SetMarked(true);
		menu.SetTargetForItems(Window());

		ConvertToScreen(&where);
		menu.Go(where, true);
	}
}


/**
 * @brief Renders the schematic mouse, its buttons, and per-button labels.
 *
 * Draws the mouse body using radial gradients, strokes the outline,
 * highlights any buttons currently held down, and centres the mapped
 * logical-button number over each physical button.
 *
 * @param updateFrame  Region requested for redraw (currently unused).
 */
void
MouseView::Draw(BRect updateFrame)
{
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	SetScale(fScaling * 1.8);

	BShape mouseShape;
	mouseShape.MoveTo(BPoint(16, 12));
	// left
	BPoint control[3] = {BPoint(12, 16), BPoint(8, 64), BPoint(32, 64)};
	mouseShape.BezierTo(control);
	// right
	BPoint control2[3] = {BPoint(56, 64), BPoint(52, 16), BPoint(48, 12)};
	mouseShape.BezierTo(control2);
	// top
	BPoint control3[3] = {BPoint(44, 8), BPoint(20, 8), BPoint(16, 12)};
	mouseShape.BezierTo(control3);
	mouseShape.Close();

	// Draw the shadow
	SetOrigin(-17 * fScaling, -11 * fScaling);
	SetHighColor(kMouseShadowColor);
	FillShape(&mouseShape, B_SOLID_HIGH);

	// Draw the body
	SetOrigin(-21 * fScaling, -14 * fScaling);
	BGradientRadial bodyGradient(28, 24, 128);
	bodyGradient.AddColor(kMouseBodyTopColor, 0);
	bodyGradient.AddColor(kMouseBodyBottomColor, 255);

	FillShape(&mouseShape, bodyGradient);

	// Draw the outline
	SetPenSize(1 / 1.8 / fScaling);
	SetDrawingMode(B_OP_OVER);
	SetHighColor(kMouseOutlineColor);

	StrokeShape(&mouseShape, B_SOLID_HIGH);

	// bottom button border
	BShape buttonsOutline;
	buttonsOutline.MoveTo(BPoint(13, 27));
	BPoint control4[] = {BPoint(18, 30), BPoint(46, 30), BPoint(51, 27)};
	buttonsOutline.BezierTo(control4);

	SetHighColor(kMouseButtonOutlineColor);
	StrokeShape(&buttonsOutline, B_SOLID_HIGH);

	SetScale(1);
	SetOrigin(0, 0);

	mouse_map map;
	fSettings.Mapping(map);

	SetDrawingMode(B_OP_OVER);

	// All button drawing is clipped to the outline of the buttons area,
	// simplifying the code below as it can overdraw things.
	ClipToPicture(&fButtonsPicture, B_ORIGIN, false);

	// Separator between the buttons
	const int32* offset = getButtonOffsets(fType);
	for (int32 i = 1; i < fType; i++) {
		BRect buttonRect = _ButtonRect(offset, i);
		StrokeLine(buttonRect.LeftTop(), buttonRect.LeftBottom());
	}

	for (int32 i = 0; i < fType; i++) {
		// draw mapping number centered over the button

		bool pressed = (fButtons & map.button[_ConvertFromVisualOrder(i)]) != 0;
		// is button currently pressed?
		if (pressed) {
			SetDrawingMode(B_OP_ALPHA);
			SetHighColor(kButtonPressedColor);
			FillRect(_ButtonRect(offset, i));
		}

		BRect border(fScaling * (offset[i] + 1), fScaling * (kButtonTop + 5),
			fScaling * offset[i + 1] - 1,
			fScaling * (kButtonTop + kMouseDownHeight - 4));
		if (i == 0)
			border.left += fScaling * 5;
		if (i == fType - 1)
			border.right -= fScaling * 4;

		char label[2] = {0};
		int32 number = getMappingNumber(map.button[_ConvertFromVisualOrder(i)]);
		label[0] = number + '1';

		SetDrawingMode(B_OP_OVER);
		SetHighColor(kButtonTextColor);
		DrawString(label,
			BPoint(border.left + (border.Width() - StringWidth(label)) / 2,
				border.top + fDigitBaseline
					+ (border.IntegerHeight() - fDigitHeight) / 2));
	}

	ClipToPicture(NULL);
}


/**
 * @brief Returns the rectangle containing the entire button strip.
 *
 * @return Frame in view coordinates, scaled by the font-derived factor.
 */
BRect
MouseView::_ButtonsRect() const
{
	return BRect(0, fScaling * kButtonTop, fScaling * kMouseDownWidth,
		fScaling * (kButtonTop + kMouseDownHeight));
}


/**
 * @brief Returns the rectangle for the @a index -th button in visual order.
 *
 * @param offsets  Cumulative x-offsets row from getButtonOffsets().
 * @param index    Zero-based visual button index.
 * @return         Frame in view coordinates, scaled by the font-derived factor.
 */
BRect
MouseView::_ButtonRect(const int32* offsets, int index) const
{
	return BRect(fScaling * offsets[index], fScaling * kButtonTop,
		fScaling * offsets[index + 1] - 1,
		fScaling * (kButtonTop + kMouseDownHeight));
}


/**
 * @brief Maps a visual button index to its logical button index.
 *
 * The buttons on a mouse are normally 1 (left), 2 (right), 3 (middle)
 * so we need to swap the indices for the second and third visual
 * buttons when at least three buttons are present.
 *
 * @param i  Visual button index (left-to-right).
 * @return   Corresponding logical button index used by mouse_map.
 */
int32
MouseView::_ConvertFromVisualOrder(int32 i)
{
	if (fType < 3)
		return i;

	switch (i) {
		case 0:
			return 0;
		case 1:
			return 2;
		case 2:
			return 1;
		default:
			return i;
	}
}


/**
 * @brief Records a clipping picture matching the mouse-button outline.
 *
 * The recorded BPicture is used during Draw() to clip per-button
 * highlighting and labels to the curved button area.
 */
void
MouseView::_CreateButtonsPicture()
{
	BeginPicture(&fButtonsPicture);
	SetScale(1.8 * fScaling);
	SetOrigin(-21 * fScaling, -14 * fScaling);

	BShape mouseShape;
	mouseShape.MoveTo(BPoint(48, 12));
	// top
	BPoint control3[3] = {BPoint(44, 8), BPoint(20, 8), BPoint(16, 12)};
	mouseShape.BezierTo(control3);
	// left
	BPoint control[3] = {BPoint(12, 16), BPoint(13, 27), BPoint(13, 27)};
	mouseShape.BezierTo(control);
	// bottom
	BPoint control4[3] = {BPoint(18, 30), BPoint(46, 30), BPoint(51, 27)};
	mouseShape.BezierTo(control4);
	// right
	BPoint control2[3] = {BPoint(51, 27), BPoint(50, 14), BPoint(48, 12)};
	mouseShape.BezierTo(control2);

	mouseShape.Close();

	SetHighColor(255, 0, 0, 255);
	FillShape(&mouseShape, B_SOLID_HIGH);

	EndPicture();
	SetOrigin(0, 0);
	SetScale(1);
}
