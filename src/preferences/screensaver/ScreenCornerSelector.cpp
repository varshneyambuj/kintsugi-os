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
 *   Copyright 2003-2013 Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Phipps
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file ScreenCornerSelector.cpp
 * @brief Implementation of the four-corner picker BControl.
 *
 * Draws a stylized 4:3 monitor and lets the user click or use the keyboard
 * to choose one of the four corners or no corner. Used twice in the
 * ScreenSaver preflet to configure the "fade now" and "never fade"
 * hot corners.
 */


#include "ScreenCornerSelector.h"

#include <stdio.h>

#include <Rect.h>
#include <Point.h>
#include <Shape.h>
#include <Screen.h>
#include <Window.h>

#include "Constants.h"
#include "Utility.h"


/** @brief Aspect ratio (width/height) of the painted monitor. */
static const float kAspectRatio = 4.0f / 3.0f;
/** @brief Pixel thickness of the monitor's outer bezel. */
static const float kMonitorBorderSize = 3.0f;
/** @brief Side length of the corner-arrow triangle. */
static const float kArrowSize = 11.0f;
/** @brief Diameter of the "no corner" stop glyph. */
static const float kStopSize = 15.0f;


/**
 * @brief Constructs the selector with no initial corner.
 *
 * @param frame        Frame rectangle in parent coordinates.
 * @param name         Internal BView name.
 * @param message      Message dispatched on value changes.
 * @param resizingMode BView resizing flags.
 */
ScreenCornerSelector::ScreenCornerSelector(BRect frame, const char* name,
	BMessage* message, uint32 resizingMode)
	:
	BControl(frame, name, NULL, message, resizingMode,
		B_WILL_DRAW | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE),
	fCurrentCorner(NO_CORNER),
	fPreviousCorner(-1)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


/**
 * @brief Returns the largest 4:3 rectangle centered inside Bounds().
 *
 * @return Frame in view coordinates fit inside Bounds() at @c kAspectRatio.
 */
BRect
ScreenCornerSelector::_MonitorFrame() const
{
	float width = Bounds().Width();
	float height = Bounds().Height();

	if (width / kAspectRatio > height)
		width = height * kAspectRatio;
	else if (height * kAspectRatio > width)
		height = width / kAspectRatio;

	return BRect((Bounds().Width() - width) / 2,
		(Bounds().Height() - height) / 2,
		(Bounds().Width() + width) / 2, (Bounds().Height() + height) / 2);
}


/**
 * @brief Computes the inner content rectangle inside @a monitorFrame.
 *
 * @param monitorFrame Outer monitor frame returned by _MonitorFrame().
 * @return Frame insetted by the bezel size, accounting for an extra
 *         pixel margin used by the focus highlight.
 */
BRect
ScreenCornerSelector::_InnerFrame(BRect monitorFrame) const
{
	return monitorFrame.InsetByCopy(kMonitorBorderSize + 3,
		kMonitorBorderSize + 3);
}


/**
 * @brief Computes the central rectangle that maps to the "no corner" hit area.
 *
 * @param innerFrame Inner content rectangle.
 * @return Frame insetted on each side by @c kArrowSize.
 */
BRect
ScreenCornerSelector::_CenterFrame(BRect innerFrame) const
{
	return innerFrame.InsetByCopy(kArrowSize, kArrowSize);
}


/**
 * @brief BView Draw hook: paints the bezel, screen, focus ring, and glyph.
 *
 * The chassis is redrawn in full only when needed; focus changes redraw
 * only the inner frame. Depending on @c fCurrentCorner the inner area
 * shows either an arrow pointing at the selected corner or a no-entry
 * stop glyph.
 *
 * @param updateRect Region the app_server requested; used to skip the
 *                   chassis when only the inner frame is dirty.
 */
void
ScreenCornerSelector::Draw(BRect updateRect)
{
	rgb_color darkColor = {160, 160, 160, 255};
	rgb_color blackColor = {0, 0, 0, 255};
	rgb_color redColor = {228, 0, 0, 255};

	BRect outerRect = _MonitorFrame();
	BRect innerRect(outerRect.InsetByCopy(kMonitorBorderSize + 2,
		kMonitorBorderSize + 2));

	SetDrawingMode(B_OP_OVER);

	if (!_InnerFrame(outerRect).Contains(updateRect)) {
		// frame & background

		// if the focus is changing, we don't redraw the whole view, but only
		// the part that's affected by the change
		if (!IsFocusChanging()) {
			SetHighColor(darkColor);
			FillRoundRect(outerRect, kMonitorBorderSize * 3 / 2,
				kMonitorBorderSize * 3 / 2);
		}

		if (IsFocus() && Window()->IsActive())
			SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		else
			SetHighColor(blackColor);

		StrokeRoundRect(outerRect, kMonitorBorderSize * 3 / 2,
			kMonitorBorderSize * 3 / 2);

		if (IsFocusChanging())
			return;

		// power light

		SetHighColor(redColor);
		BPoint powerPos(outerRect.left + kMonitorBorderSize * 2, outerRect.bottom
			- kMonitorBorderSize);
		StrokeLine(powerPos, BPoint(powerPos.x + 2, powerPos.y));
	}

	if (!IsFocusChanging()) {
		SetHighColor(210, 210, 255);
		FillRoundRect(innerRect, kMonitorBorderSize, kMonitorBorderSize);
	}

	if (IsFocus() && Window()->IsActive())
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
	else
		SetHighColor(blackColor);
	StrokeRoundRect(innerRect, kMonitorBorderSize, kMonitorBorderSize);

	innerRect = _InnerFrame(outerRect);

	if (fCurrentCorner != NO_CORNER)
		_DrawArrow(innerRect);
	else
		_DrawStop(innerRect);

	SetDrawingMode(B_OP_COPY);
}


/**
 * @brief Returns the currently selected corner as an int32.
 *
 * @return The @c screen_corner enum cast to int32.
 */
int32
ScreenCornerSelector::Value()
{
	return (int32)fCurrentCorner;
}


/**
 * @brief Sets the selected corner, validates it, and invokes the message.
 *
 * Unknown values are silently coerced to @c NO_CORNER. If the new value
 * differs from the previous one, the inner area is invalidated and
 * Invoke() is called so observers receive the message.
 *
 * @param corner New corner value.
 */
void
ScreenCornerSelector::SetValue(int32 corner)
{
	switch (corner) {
		case UP_LEFT_CORNER:
		case UP_RIGHT_CORNER:
		case DOWN_LEFT_CORNER:
		case DOWN_RIGHT_CORNER:
		case NO_CORNER:
			break;

		default:
			corner = NO_CORNER;
	}
	if ((screen_corner)corner == fCurrentCorner)
		return;

	fCurrentCorner = (screen_corner)corner;
	Invalidate(_InnerFrame(_MonitorFrame()));
	Invoke();
}


/**
 * @brief Returns the currently selected corner.
 */
screen_corner
ScreenCornerSelector::Corner() const
{
	return fCurrentCorner;
}


/**
 * @brief Sets the selected corner using the typed enum.
 *
 * Routes through SetValue() so that out-of-range values are normalized.
 *
 * @param corner New corner value.
 */
void
ScreenCornerSelector::SetCorner(screen_corner corner)
{
	// redirected to SetValue() to make sure only valid values are set
	SetValue((int32)corner);
}


/**
 * @brief Draws the "no corner" stop glyph: a red circle with a slash.
 *
 * @param innerFrame Inner content rectangle to draw inside.
 */
void
ScreenCornerSelector::_DrawStop(BRect innerFrame)
{
	BRect centerRect = _CenterFrame(innerFrame);
	float size = kStopSize;
	BRect rect;
	rect.left = centerRect.left + (centerRect.Width() - size) / 2;
	rect.top = centerRect.top + (centerRect.Height() - size) / 2;
	if (rect.left < centerRect.left || rect.top < centerRect.top) {
		size = centerRect.Height();
		rect.top = centerRect.top;
		rect.left = centerRect.left + (centerRect.Width() - size) / 2;
	}
	rect.right = rect.left + size - 1;
	rect.bottom = rect.top + size - 1;

	SetHighColor(255, 0, 0);
	SetPenSize(2);
	SetFlags(Flags() | B_SUBPIXEL_PRECISE);

	StrokeEllipse(rect);

	size -= sin(M_PI / 4) * size + 2;
	rect.InsetBy(size, size);
	StrokeLine(rect.RightTop(), rect.LeftBottom());

	SetFlags(Flags() & ~B_SUBPIXEL_PRECISE);
	SetPenSize(1);
}


/**
 * @brief Draws a black triangle pointing at the currently selected corner.
 *
 * The triangle's orientation is chosen from @c fCurrentCorner so the
 * hypotenuse points away from the selected screen corner.
 *
 * @param innerFrame Inner content rectangle to draw inside.
 */
void
ScreenCornerSelector::_DrawArrow(BRect innerFrame)
{
	float size = kArrowSize;
	float sizeX = fCurrentCorner == UP_LEFT_CORNER
		|| fCurrentCorner == DOWN_LEFT_CORNER ? size : -size;
	float sizeY = fCurrentCorner == UP_LEFT_CORNER
		|| fCurrentCorner == UP_RIGHT_CORNER ? size : -size;

	innerFrame.InsetBy(2, 2);
	BPoint origin(sizeX < 0 ? innerFrame.right : innerFrame.left,
		sizeY < 0 ? innerFrame.bottom : innerFrame.top);

	SetHighColor(kBlack);
	FillTriangle(BPoint(origin.x, origin.y), BPoint(origin.x, origin.y + sizeY),
		BPoint(origin.x + sizeX, origin.y));
}


/**
 * @brief Maps a point in view coordinates to the corner it lies in.
 *
 * Points outside the inner frame return @a previousCorner so that
 * dragging off the widget keeps the previously selected corner. Points in
 * the central rectangle resolve to @c NO_CORNER.
 *
 * @param point          Point in view coordinates.
 * @param previousCorner Value to return when @a point is outside the
 *                       inner frame.
 * @return Resolved corner enum.
 */
screen_corner
ScreenCornerSelector::_ScreenCorner(BPoint point,
	screen_corner previousCorner) const
{
	BRect innerFrame = _InnerFrame(_MonitorFrame());

	if (!innerFrame.Contains(point))
		return previousCorner;

	if (_CenterFrame(innerFrame).Contains(point))
		return NO_CORNER;

	float centerX = innerFrame.left + innerFrame.Width() / 2;
	float centerY = innerFrame.top + innerFrame.Height() / 2;
	if (point.x < centerX)
		return point.y < centerY ? UP_LEFT_CORNER : DOWN_LEFT_CORNER;

	return point.y < centerY ? UP_RIGHT_CORNER : DOWN_RIGHT_CORNER;
}


/**
 * @brief BView MouseDown hook: starts a drag and records the previous corner.
 *
 * @param where Mouse location in view coordinates.
 */
void
ScreenCornerSelector::MouseDown(BPoint where)
{
	fPreviousCorner = Value();

	SetValue(_ScreenCorner(where, (screen_corner)fPreviousCorner));
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
}


/**
 * @brief BView MouseUp hook: ends the drag.
 *
 * @param where Mouse location in view coordinates (unused).
 */
void
ScreenCornerSelector::MouseUp(BPoint where)
{
	fPreviousCorner = -1;
}


/**
 * @brief BView MouseMoved hook: updates the corner during drag.
 *
 * Does nothing when no drag is in progress.
 *
 * @param where        Mouse location in view coordinates.
 * @param transit      Transit code (unused).
 * @param dragMessage  Drag-and-drop message (unused).
 */
void
ScreenCornerSelector::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (fPreviousCorner == -1)
		return;

	SetValue(_ScreenCorner(where, (screen_corner)fPreviousCorner));
}


/**
 * @brief BView KeyDown hook: lets arrow and numpad keys select corners.
 *
 * Arrow keys move the selection between the corners along an edge;
 * numpad home/page-up/page-down/end keys jump directly to the matching
 * corner. Unhandled keys fall through to BControl::KeyDown().
 *
 * @param bytes    Raw key bytes.
 * @param numBytes Length of @a bytes.
 */
void
ScreenCornerSelector::KeyDown(const char* bytes, int32 numBytes)
{
	switch (bytes[0]) {
		// arrow keys

		case B_LEFT_ARROW:
		case '4':
			if (Corner() == UP_RIGHT_CORNER)
				SetCorner(UP_LEFT_CORNER);
			else if (Corner() == DOWN_RIGHT_CORNER)
				SetCorner(DOWN_LEFT_CORNER);
			break;
		case B_RIGHT_ARROW:
		case '6':
			if (Corner() == UP_LEFT_CORNER)
				SetCorner(UP_RIGHT_CORNER);
			else if (Corner() == DOWN_LEFT_CORNER)
				SetCorner(DOWN_RIGHT_CORNER);
			break;
		case B_UP_ARROW:
		case '8':
			if (Corner() == DOWN_LEFT_CORNER)
				SetCorner(UP_LEFT_CORNER);
			else if (Corner() == DOWN_RIGHT_CORNER)
				SetCorner(UP_RIGHT_CORNER);
			break;
		case B_DOWN_ARROW:
		case '2':
			if (Corner() == UP_LEFT_CORNER)
				SetCorner(DOWN_LEFT_CORNER);
			else if (Corner() == UP_RIGHT_CORNER)
				SetCorner(DOWN_RIGHT_CORNER);
			break;

		// numlock keys

		case B_HOME:
		case '7':
			SetCorner(UP_LEFT_CORNER);
			break;
		case B_PAGE_UP:
		case '9':
			SetCorner(UP_RIGHT_CORNER);
			break;
		case B_PAGE_DOWN:
		case '3':
			SetCorner(DOWN_RIGHT_CORNER);
			break;
		case B_END:
		case '1':
			SetCorner(DOWN_LEFT_CORNER);
			break;

		default:
			BControl::KeyDown(bytes, numBytes);
	}
}

