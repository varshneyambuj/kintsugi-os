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
 *
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of
 *   this software and associated documentation files (the "Software"), to deal in
 *   the Software without restriction, including without limitation the rights to
 *   use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *   of the Software, and to permit persons to whom the Software is furnished to do
 *   so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice applies to all licensees
 *   and shall be included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 *   AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
 *   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *   Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
 *   trademarks of Be Incorporated in the United States and other countries.
 *   All rights reserved.
 */


/**
 * @file DialogPane.cpp
 * @brief Collapsible/expandable pane-switch control used in Tracker dialogs.
 *
 * PaneSwitch is a BControl that draws a right- or down-pointing disclosure arrow
 * and optional labels. Clicking it toggles between collapsed and expanded states
 * and sends an Invoke message, allowing parent views to show or hide extra controls.
 *
 * @see BControl, BControlLook
 */


#include "DialogPane.h"

#include <ControlLook.h>
#include <LayoutUtils.h>

#include "Utilities.h"
#include "Window.h"


const rgb_color kNormalColor = {150, 150, 150, 255};
const rgb_color kHighlightColor = {100, 100, 0, 255};


//	#pragma mark - PaneSwitch


/**
 * @brief Construct a legacy (BRect-based) PaneSwitch.
 *
 * @param frame       The view frame in parent coordinates.
 * @param name        The view name.
 * @param leftAligned true if the arrow is on the left; false places it on the right.
 * @param resizeMask  Resize flags.
 * @param flags       BView flags.
 */
PaneSwitch::PaneSwitch(BRect frame, const char* name, bool leftAligned,
		uint32 resizeMask, uint32 flags)
	:
	BControl(frame, name, "", 0, resizeMask, flags),
	fLeftAligned(leftAligned),
	fPressing(false),
	fLabelOn(NULL),
	fLabelOff(NULL)
{
}


/**
 * @brief Construct a layout-manager-friendly PaneSwitch.
 *
 * @param name        The view name.
 * @param leftAligned true if the arrow is on the left.
 * @param flags       BView flags.
 */
PaneSwitch::PaneSwitch(const char* name, bool leftAligned, uint32 flags)
	:
	BControl(name, "", 0, flags),
	fLeftAligned(leftAligned),
	fPressing(false),
	fLabelOn(NULL),
	fLabelOff(NULL)
{
}


/**
 * @brief Destructor; frees the on/off label strings.
 */
PaneSwitch::~PaneSwitch()
{
	free(fLabelOn);
	free(fLabelOff);
}


/**
 * @brief Draw the disclosure arrow and optional label for the current state.
 *
 * @param  updateRect  (unused) The dirty rectangle passed by the rendering system.
 */
void
PaneSwitch::Draw(BRect)
{
	BRect bounds(Bounds());

	// Draw the label, if any
	const char* label = fLabelOff;
	if (fLabelOn != NULL && Value() == B_CONTROL_ON)
		label = fLabelOn;

	if (label != NULL) {
		BPoint point;
		float latchSize = be_plain_font->Size();
		float labelDist = latchSize + ceilf(latchSize / 2.0);
		if (fLeftAligned)
			point.x = labelDist;
		else
			point.x = bounds.right - labelDist - StringWidth(label);

		SetHighUIColor(B_PANEL_TEXT_COLOR);
		font_height fontHeight;
		GetFontHeight(&fontHeight);
		point.y = (bounds.top + bounds.bottom
			- ceilf(fontHeight.ascent) - ceilf(fontHeight.descent)) / 2
			+ ceilf(fontHeight.ascent);

		DrawString(label, point);
	}

	// draw the latch
	if (fPressing)
		DrawInState(kPressed);
	else if (Value())
		DrawInState(kExpanded);
	else
		DrawInState(kCollapsed);

	// ...and the focus indication
	if (!IsFocus() || !Window()->IsActive())
		return;

	rgb_color markColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);

	BeginLineArray(2);
	AddLine(BPoint(bounds.left + 2, bounds.bottom - 1),
		BPoint(bounds.right - 2, bounds.bottom - 1), markColor);
	AddLine(BPoint(bounds.left + 2, bounds.bottom),
		BPoint(bounds.right - 2, bounds.bottom), kWhite);
	EndLineArray();
}


/**
 * @brief Begin tracking a button press and capture pointer events.
 */
void
PaneSwitch::MouseDown(BPoint)
{
	if (!IsEnabled())
		return;

	fPressing = true;
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	Invalidate();
}


/**
 * @brief Track cursor movement and update the pressed state while a button is held.
 *
 * @param point    Current cursor position in view coordinates.
 * @param code     Transit code (B_ENTERED_VIEW, B_INSIDE_VIEW, etc.).
 * @param message  Any dragged message accompanying the movement.
 */
void
PaneSwitch::MouseMoved(BPoint point, uint32 code, const BMessage* message)
{
	int32 buttons;
	BMessage* currentMessage = Window()->CurrentMessage();
	if (currentMessage == NULL
		|| currentMessage->FindInt32("buttons", &buttons) != B_OK) {
		buttons = 0;
	}

	if (buttons != 0) {
		BRect bounds(Bounds());
		bounds.InsetBy(-3, -3);

		bool newPressing = bounds.Contains(point);
		if (newPressing != fPressing) {
			fPressing = newPressing;
			Invalidate();
		}
	}

	BControl::MouseMoved(point, code, message);
}


/**
 * @brief Complete the click: toggle the value and invoke if the cursor is still inside.
 *
 * @param point  Cursor position in view coordinates at the time of release.
 */
void
PaneSwitch::MouseUp(BPoint point)
{
	BRect bounds(Bounds());
	bounds.InsetBy(-3, -3);

	fPressing = false;
	Invalidate();
	if (bounds.Contains(point)) {
		SetValue(!Value());
		Invoke();
	}

	BControl::MouseUp(point);
}


/**
 * @brief Return the preferred size (delegates to MinSize()).
 *
 * @param _width   Receives the preferred width; may be NULL.
 * @param _height  Receives the preferred height; may be NULL.
 */
void
PaneSwitch::GetPreferredSize(float* _width, float* _height)
{
	BSize size = MinSize();
	if (_width != NULL)
		*_width = size.width;

	if (_height != NULL)
		*_height = size.height;
}


/**
 * @brief Compute the minimum size needed to display the arrow and optional label.
 *
 * @return The minimum BSize based on font metrics and label widths.
 */
BSize
PaneSwitch::MinSize()
{
	BSize size;
	float onLabelWidth = StringWidth(fLabelOn);
	float offLabelWidth = StringWidth(fLabelOff);
	float labelWidth = max_c(onLabelWidth, offLabelWidth);
	float latchSize = be_plain_font->Size();
	size.width = latchSize;
	if (labelWidth > 0.0)
		size.width += ceilf(latchSize / 2.0) + labelWidth;

	font_height fontHeight;
	GetFontHeight(&fontHeight);
	size.height = ceilf(fontHeight.ascent) + ceilf(fontHeight.descent);
	size.height = max_c(size.height, latchSize);

	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


/**
 * @brief Return the maximum size (same as MinSize for this control).
 *
 * @return Maximum BSize composed with the explicit max size if set.
 */
BSize
PaneSwitch::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), MinSize());
}


/**
 * @brief Return the preferred size (same as MinSize for this control).
 *
 * @return Preferred BSize composed with the explicit preferred size if set.
 */
BSize
PaneSwitch::PreferredSize()
{
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), MinSize());
}


/**
 * @brief Set the text labels shown when the pane is expanded and collapsed.
 *
 * @param labelOn   Text shown when Value() == B_CONTROL_ON; NULL to remove.
 * @param labelOff  Text shown when Value() == B_CONTROL_OFF; NULL to remove.
 */
void
PaneSwitch::SetLabels(const char* labelOn, const char* labelOff)
{
	free(fLabelOn);
	free(fLabelOff);

	if (labelOn != NULL)
		fLabelOn = strdup(labelOn);
	else
		fLabelOn = NULL;

	if (labelOff != NULL)
		fLabelOff = strdup(labelOff);
	else
		fLabelOff = NULL;

	Invalidate();
	InvalidateLayout();
}


/**
 * @brief Draw the disclosure arrow in the given visual state.
 *
 * @param state  kCollapsed (right arrow), kPressed (right-down arrow), or
 *               kExpanded (down arrow).
 */
void
PaneSwitch::DrawInState(PaneSwitch::State state)
{
	BRect rect(0, 0, be_plain_font->Size(), be_plain_font->Size());
	rect.OffsetBy(1, 1);

	rgb_color arrowColor = state == kPressed ? kHighlightColor : kNormalColor;
	int32 arrowDirection = BControlLook::B_RIGHT_ARROW;
	float tint = IsEnabled() && Window()->IsActive() ? B_DARKEN_3_TINT
		: B_DARKEN_1_TINT;

	switch (state) {
		case kCollapsed:
			arrowDirection = BControlLook::B_RIGHT_ARROW;
			break;

		case kPressed:
			arrowDirection = BControlLook::B_RIGHT_DOWN_ARROW;
			break;

		case kExpanded:
			arrowDirection = BControlLook::B_DOWN_ARROW;
			break;
	}

	SetDrawingMode(B_OP_COPY);
	be_control_look->DrawArrowShape(this, rect, rect, arrowColor,
		arrowDirection, 0, tint);
}
