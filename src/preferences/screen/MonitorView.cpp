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
 *   Copyright 2001-2009, Haiku.
 *   Copyright 2002, Thomas Kurschel.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Rafael Romo
 *       Thomas Kurschel
 *       Axel Doerfler, axeld@pinc-software.de
 */


/**
 * @file MonitorView.cpp
 * @brief Schematic monitor preview rendered inside the Screen window.
 *
 * The view draws a small "monitor" rectangle scaled relative to the
 * maximum supported resolution and overlays a DPI readout when EDID
 * monitor info is available.
 */


#include "MonitorView.h"

#include <stdio.h>

#include <Catalog.h>
#include <Locale.h>
#include <Roster.h>
#include <Screen.h>

#include "Constants.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Monitor View"


/**
 * @brief Construct the monitor preview at @a rect with an initial size.
 *
 * The desktop tint is sampled from the current workspace's desktop color
 * so the preview matches what the user will see.
 *
 * @param rect   Frame of the view.
 * @param name   View name.
 * @param width  Current desktop width in pixels.
 * @param height Current desktop height in pixels.
 */
MonitorView::MonitorView(BRect rect, const char *name, int32 width, int32 height)
	: BView(rect, name, B_FOLLOW_ALL, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fMaxWidth(1920),
	fMaxHeight(1200),
	fWidth(width),
	fHeight(height),
	fDPI(0)
{
	BScreen screen(B_MAIN_SCREEN_ID);
	fDesktopColor = screen.DesktopColor(current_workspace());
}


/** @brief Trivial destructor; no owned resources. */
MonitorView::~MonitorView()
{
}


/**
 * @brief Cache the panel-background color and refresh the DPI readout.
 */
void
MonitorView::AttachedToWindow()
{
	SetViewColor(B_TRANSPARENT_COLOR);
	fBackgroundColor = ui_color(B_PANEL_BACKGROUND_COLOR);

	_UpdateDPI();
}


/**
 * @brief Launch the Backgrounds preferences app on click.
 *
 * @param point Click location (unused).
 */
void
MonitorView::MouseDown(BPoint point)
{
	be_roster->Launch(kBackgroundsSignature);
}


/**
 * @brief Render the schematic monitor, desktop tint, power LED, and DPI text.
 *
 * @param updateRect Region requiring redraw.
 */
void
MonitorView::Draw(BRect updateRect)
{
	rgb_color darkColor = {160, 160, 160, 255};
	rgb_color blackColor = {0, 0, 0, 255};
	rgb_color redColor = {228, 0, 0, 255};
	rgb_color whiteColor = {255, 255, 255, 255};
	BRect outerRect = _MonitorBounds();

	SetHighColor(fBackgroundColor);
	FillRect(updateRect);

	SetDrawingMode(B_OP_OVER);

	// frame & background

	SetHighColor(darkColor);
	FillRoundRect(outerRect, 3.0, 3.0);

	SetHighColor(blackColor);
	StrokeRoundRect(outerRect, 3.0, 3.0);

	SetHighColor(fDesktopColor);

	BRect innerRect(outerRect.InsetByCopy(4, 4));
	FillRoundRect(innerRect, 2.0, 2.0);

	SetHighColor(blackColor);
	StrokeRoundRect(innerRect, 2.0, 2.0);

	SetDrawingMode(B_OP_COPY);

	// power light

	SetHighColor(redColor);
	BPoint powerPos(outerRect.left + 5, outerRect.bottom - 2);
	StrokeLine(powerPos, BPoint(powerPos.x + 2, powerPos.y));

	// DPI

	if (fDPI == 0)
		return;

	font_height fontHeight;
	GetFontHeight(&fontHeight);
	float height = ceilf(fontHeight.ascent + fontHeight.descent);

	char text[64];
	snprintf(text, sizeof(text), B_TRANSLATE("%ld dpi"), (long int)fDPI);

	float width = StringWidth(text);
	if (width > innerRect.Width() || height > innerRect.Height())
		return;

	SetLowColor(fDesktopColor);
	SetHighColor(whiteColor);

	DrawString(text, BPoint(innerRect.left + (innerRect.Width() - width) / 2,
		innerRect.top + fontHeight.ascent + (innerRect.Height() - height) / 2));
}


/**
 * @brief Update the previewed resolution and redraw.
 *
 * Recomputes DPI when the resolution actually changed.
 *
 * @param width  New desktop width in pixels.
 * @param height New desktop height in pixels.
 */
void
MonitorView::SetResolution(int32 width, int32 height)
{
	if (fWidth == width && fHeight == height)
		return;

	fWidth = width;
	fHeight = height;

	_UpdateDPI();
	Invalidate();
}


/**
 * @brief Set the upper-bound resolution used to scale the preview.
 *
 * The drawn monitor rectangle is sized as a fraction of the maximum
 * supported resolution so the user can compare modes visually.
 *
 * @param width  Maximum supported width in pixels.
 * @param height Maximum supported height in pixels.
 */
void
MonitorView::SetMaxResolution(int32 width, int32 height)
{
	if (fMaxWidth == width && fMaxHeight == height)
		return;

	fMaxWidth = width;
	fMaxHeight = height;

	Invalidate();
}


/**
 * @brief Respond to color/desktop-update notifications and resolution changes.
 *
 * Handles @c B_COLORS_UPDATED to track panel color changes,
 * @c UPDATE_DESKTOP_MSG for resolution previews from ScreenWindow, and
 * @c UPDATE_DESKTOP_COLOR_MSG when the desktop tint changes.
 *
 * @param message Incoming message.
 */
void
MonitorView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_COLORS_UPDATED:
		{
			message->FindColor(ui_color_name(B_PANEL_BACKGROUND_COLOR),
				&fBackgroundColor);
			break;
		}
		case UPDATE_DESKTOP_MSG:
		{
			int32 width, height;
			if (message->FindInt32("width", &width) == B_OK
				&& message->FindInt32("height", &height) == B_OK)
				SetResolution(width, height);
			break;
		}

		case UPDATE_DESKTOP_COLOR_MSG:
		{
			BScreen screen(Window());
			rgb_color color = screen.DesktopColor(current_workspace());
			if (color != fDesktopColor) {
				fDesktopColor = color;
				Invalidate();
			}
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Compute the rectangle to draw given the current resolution ratio.
 *
 * Preserves the aspect ratio of the monitor and scales it within the view
 * bounds so the preview rectangle visually communicates how big a chosen
 * resolution is relative to the max.
 *
 * @return The frame to draw the monitor rectangle in, in view coordinates.
 */
BRect
MonitorView::_MonitorBounds()
{
	float maxWidth = Bounds().Width();
	float maxHeight = Bounds().Height();
	if (maxWidth / maxHeight > (float)fMaxWidth / fMaxHeight)
		maxWidth = maxHeight / fMaxHeight * fMaxWidth;
	else
		maxHeight = maxWidth / fMaxWidth * fMaxHeight;

	float factorX = (float)fWidth / fMaxWidth;
	float factorY = (float)fHeight / fMaxHeight;

	if (factorX > factorY && factorX > 1) {
		factorY /= factorX;
		factorX = 1;
	} else if (factorY > factorX && factorY > 1) {
		factorX /= factorY;
		factorY = 1;
	}

	float width = maxWidth * factorX;
	float height = maxHeight * factorY;

	BSize size = Bounds().Size();
	return BRect((size.width - width) / 2, (size.height - height) / 2,
		(size.width + width) / 2, (size.height + height) / 2);
}


/**
 * @brief Recompute the displayed DPI from EDID monitor info.
 *
 * Sets @c fDPI to the average of horizontal and vertical pixel density,
 * computed from the physical screen dimensions reported by the monitor.
 * Leaves @c fDPI at zero when EDID info is not available, which
 * suppresses the on-screen DPI label.
 */
void
MonitorView::_UpdateDPI()
{
	fDPI = 0;

	BScreen screen(Window());
	monitor_info info;
	if (screen.GetMonitorInfo(&info) != B_OK)
		return;

	double x = info.width / 2.54;
	double y = info.height / 2.54;

	fDPI = (int32)round((fWidth / x + fHeight / y) / 2);
}
