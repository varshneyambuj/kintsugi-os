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
 *   Copyright 2010-2012 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       John Scipione <jscipione@gmail.com>
 */


/**
 * @file FakeScrollBar.cpp
 * @brief Read-only scroll-bar preview used by the Look-and-feel tab.
 *
 * Renders a non-functional scroll bar with the system's current arrow
 * style and knob style so the user can pick between single- and
 * double-arrow modes by clicking on the preview.
 */


#include "FakeScrollBar.h"

#include <Box.h>
#include <ControlLook.h>
#include <Message.h>
#include <ScrollBar.h>
#include <Shape.h>
#include <Size.h>
#include <Window.h>


typedef enum {
	ARROW_LEFT = 0,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	ARROW_NONE
} arrow_direction;


/**
 * @brief Constructs a fake scroll-bar preview control.
 *
 * @param drawArrows   When @c true, render arrow buttons at each end.
 * @param doubleArrows When @c true, render the double-arrow style.
 * @param message      Message posted when the preview is clicked.
 */
FakeScrollBar::FakeScrollBar(bool drawArrows, bool doubleArrows,
	BMessage* message)
	:
	BControl("FakeScrollBar", NULL, message, B_WILL_DRAW | B_NAVIGABLE),
	fDrawArrows(drawArrows),
	fDoubleArrows(doubleArrows)
{
	float height = be_control_look->GetScrollBarWidth(B_HORIZONTAL);
	height += be_control_look->ComposeSpacing(B_USE_HALF_ITEM_SPACING);
		// add some height to draw the ring around the scroll bar
	SetExplicitSize(BSize(B_SIZE_UNSET, height));
}


/**
 * @brief Destructor; nothing to release.
 */
FakeScrollBar::~FakeScrollBar(void)
{
}


/**
 * @brief Draws the scroll-bar preview, including selection ring and thumb.
 *
 * Uses BControlLook to render a horizontal scroll bar with the
 * configured arrow style and knob style. The selection ring around the
 * bar reflects the control's current value.
 *
 * @param updateRect Region requiring redraw.
 */
void
FakeScrollBar::Draw(BRect updateRect)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);

	uint32 flags = BControlLook::B_PARTIALLY_ACTIVATED;

	if (Value() == B_CONTROL_ON)
		SetHighColor(ui_color(B_CONTROL_MARK_COLOR));
	else
		SetHighColor(base);

	BRect rect(Bounds());

	// draw the selected border (2px)
	StrokeRect(rect);
	rect.InsetBy(1, 1);
	StrokeRect(rect);
	rect.InsetBy(1, 1);

	// draw a 1px gap
	SetHighColor(base);
	StrokeRect(rect);
	rect.InsetBy(1, 1);

	// draw a 1px border around the entire scroll bar
	be_control_look->DrawScrollBarBorder(this, rect, updateRect, base, flags,
		B_HORIZONTAL);

	// inset past border
	rect.InsetBy(1, 1);

	// draw arrow buttons
	if (fDrawArrows) {
		BRect buttonFrame(rect.left, rect.top, rect.left + rect.Height(),
			rect.bottom);
		be_control_look->DrawScrollBarButton(this, buttonFrame, updateRect,
			base, text, flags, BControlLook::B_LEFT_ARROW, B_HORIZONTAL);
		if (fDoubleArrows) {
			buttonFrame.OffsetBy(rect.Height() + 1, 0.0f);
			be_control_look->DrawScrollBarButton(this, buttonFrame,
				updateRect, base, text, flags, BControlLook::B_RIGHT_ARROW,
				B_HORIZONTAL);
			buttonFrame.OffsetTo(rect.right - ((rect.Height() * 2) + 1),
				rect.top);
			be_control_look->DrawScrollBarButton(this, buttonFrame,
				updateRect, base, text, flags, BControlLook::B_LEFT_ARROW,
				B_HORIZONTAL);
		}
		buttonFrame.OffsetTo(rect.right - rect.Height(), rect.top);
		be_control_look->DrawScrollBarButton(this, buttonFrame, updateRect,
			base, text, flags, BControlLook::B_RIGHT_ARROW, B_HORIZONTAL);
	}

	// inset rect to make room for arrows
	if (fDrawArrows) {
		if (fDoubleArrows)
			rect.InsetBy((rect.Height() + 1) * 2, 0.0f);
		else
			rect.InsetBy(rect.Height() + 1, 0.0f);
	}

	// draw background and thumb
	float less = floorf(rect.Width() / 3);
	BRect thumbRect(rect.left + less, rect.top, rect.right - less,
		rect.bottom);
	BRect leftOfThumb(rect.left, thumbRect.top, thumbRect.left - 1,
		thumbRect.bottom);
	BRect rightOfThumb(thumbRect.right + 1, thumbRect.top, rect.right,
		thumbRect.bottom);

	be_control_look->DrawScrollBarBackground(this, leftOfThumb,
		rightOfThumb, updateRect, base, flags, B_HORIZONTAL);
	be_control_look->DrawScrollBarThumb(this, thumbRect, updateRect,
		ui_color(B_SCROLL_BAR_THUMB_COLOR), flags, B_HORIZONTAL, fKnobStyle);
}


/**
 * @brief Forwards the press to BControl; selection happens on MouseUp.
 *
 * @param point Mouse location in view coordinates.
 */
void
FakeScrollBar::MouseDown(BPoint point)
{
	BControl::MouseDown(point);
}


/**
 * @brief Forwards mouse-move events to BControl.
 *
 * @param point   Mouse location in view coordinates.
 * @param transit Transit flag passed by the framework.
 * @param message Optional drag message.
 */
void
FakeScrollBar::MouseMoved(BPoint point, uint32 transit,
	const BMessage* message)
{
	BControl::MouseMoved(point, transit, message);
}


/**
 * @brief Marks this preview as selected and posts the bound message.
 *
 * Sets the control to ON, repaints, and forwards to BControl.
 *
 * @param point Mouse release location in view coordinates.
 */
void
FakeScrollBar::MouseUp(BPoint point)
{
	SetValue(B_CONTROL_ON);
	Invoke();

	Invalidate();

	BControl::MouseUp(point);
}


/**
 * @brief Sets the control value and clears sibling FakeScrollBars.
 *
 * Acts as a radio-group: when this control becomes ON, every sibling
 * FakeScrollBar (including the LabelView of an enclosing BBox) is set
 * to OFF so only one preview can be selected at a time.
 *
 * @param value New control value (B_CONTROL_ON or B_CONTROL_OFF).
 */
void
FakeScrollBar::SetValue(int32 value)
{
	if (value != Value()) {
		BControl::SetValueNoUpdate(value);
		Invalidate();
	}

	if (!value)
		return;

	BView* parent = Parent();
	BView* child = NULL;

	if (parent != NULL) {
		// If the parent is a BBox, the group parent is the parent of the BBox
		BBox* box = dynamic_cast<BBox*>(parent);

		if (box && box->LabelView() == this)
			parent = box->Parent();

		if (parent != NULL) {
			BBox* box = dynamic_cast<BBox*>(parent);

			// If the parent is a BBox, skip the label if there is one
			if (box && box->LabelView())
				child = parent->ChildAt(1);
			else
				child = parent->ChildAt(0);
		} else
			child = Window()->ChildAt(0);
	} else if (Window())
		child = Window()->ChildAt(0);

	while (child) {
		FakeScrollBar* scrollbar = dynamic_cast<FakeScrollBar*>(child);

		if (scrollbar != NULL && (scrollbar != this))
			scrollbar->SetValue(B_CONTROL_OFF);
		else {
			// If the child is a BBox, check if the label is a scrollbarbutton
			BBox* box = dynamic_cast<BBox*>(child);

			if (box && box->LabelView()) {
				scrollbar = dynamic_cast<FakeScrollBar*>(box->LabelView());

				if (scrollbar != NULL && (scrollbar != this))
					scrollbar->SetValue(B_CONTROL_OFF);
			}
		}

		child = child->NextSibling();
	}

	//ASSERT(Value() == B_CONTROL_ON);
}


//	#pragma mark -


/**
 * @brief Toggles whether the preview shows the double-arrow style.
 *
 * @param doubleArrows Pass @c true for double arrows, @c false for single.
 */
void
FakeScrollBar::SetDoubleArrows(bool doubleArrows)
{
	fDoubleArrows = doubleArrows;
	Invalidate();
}


/**
 * @brief Updates the knob style used when rendering the thumb.
 *
 * @param knobStyle One of the BControlLook knob constants.
 */
void
FakeScrollBar::SetKnobStyle(uint32 knobStyle)
{
	fKnobStyle = knobStyle;
	Invalidate();
}


/**
 * @brief Mirrors the system-wide @a info struct into this preview.
 *
 * @param info Settings struct returned by @c get_scroll_bar_info().
 */
void
FakeScrollBar::SetFromScrollBarInfo(const scroll_bar_info &info)
{
	fDoubleArrows = info.double_arrows;
	fKnobStyle = info.knob;
	Invalidate();
}


//	#pragma mark -


/**
 * @brief Draws a single arrow button at @a rect in @a direction.
 *
 * Currently unused by Draw(); retained for future custom rendering.
 *
 * @param direction  BControlLook arrow direction constant.
 * @param rect       Bounding rectangle of the button.
 * @param updateRect Region requiring redraw.
 */
void
FakeScrollBar::_DrawArrowButton(int32 direction, BRect rect,
	const BRect& updateRect)
{
	if (!updateRect.Intersects(rect))
		return;

	uint32 flags = 0;

	rgb_color baseColor = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_LIGHTEN_1_TINT);

	be_control_look->DrawButtonBackground(this, rect, updateRect, baseColor,
		flags, BControlLook::B_ALL_BORDERS, B_HORIZONTAL);

	rect.InsetBy(-1, -1);
	be_control_look->DrawArrowShape(this, rect, updateRect,
		baseColor, direction, flags, B_DARKEN_MAX_TINT);
}
