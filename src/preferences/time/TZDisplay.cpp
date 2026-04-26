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
 *   Copyright 2004-2011, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Andrew McCall <mccall@@digitalparadise.co.uk>
 *       Mike Berg <mike@berg-net.us>
 *       Julun <host.haiku@gmx.de>
 *       Hamish Morrison <hamish@lavabit.com>
 */


/**
 * @file TZDisplay.cpp
 * @brief Implementation of the time-zone label/city/time display widget.
 *
 * Renders the label and time on the first line and the city description
 * underneath. Computes its preferred size from the longer of the two lines.
 */


#include "TZDisplay.h"

#include <stdio.h>

#include <LayoutUtils.h>



/**
 * @brief Constructs the display with a fixed left-side label.
 *
 * @param name  View name passed to BView.
 * @param label Constant label text rendered on the first line.
 */
TTZDisplay::TTZDisplay(const char* name, const char* label)
	:
	BView(name, B_WILL_DRAW),
	fLabel(label),
	fText(""),
	fTime("")
{
}


/**
 * @brief Destructor; nothing to release.
 */
TTZDisplay::~TTZDisplay()
{
}


/**
 * @brief Inherits the parent's color scheme on attach.
 */
void
TTZDisplay::AttachedToWindow()
{
	AdoptParentColors();
}


/**
 * @brief Resizes the widget to its computed preferred size.
 */
void
TTZDisplay::ResizeToPreferred()
{
	BSize size = _CalcPrefSize();
	ResizeTo(size.width, size.height);
}


/**
 * @brief Repaints the widget.
 *
 * Fills the background, draws the label flush left and the time flush
 * right on the first line, and the city description below.
 *
 * @param updateRect Update rectangle (unused).
 */
void
TTZDisplay::Draw(BRect)
{
	SetLowColor(ViewColor());

	BRect bounds = Bounds();
	FillRect(Bounds(), B_SOLID_LOW);

	font_height height;
	GetFontHeight(&height);
	float fontHeight = ceilf(height.descent + height.ascent +
		height.leading);

	BPoint pt(bounds.left, ceilf(bounds.top + height.ascent));
	DrawString(fLabel.String(), pt);

	pt.y += fontHeight;
	DrawString(fText.String(), pt);

	pt.y -= fontHeight;
	pt.x = bounds.right - StringWidth(fTime.String());
	DrawString(fTime.String(), pt);
}


/**
 * @brief Returns the current label string.
 */
const char*
TTZDisplay::Label() const
{
	return fLabel.String();
}


/**
 * @brief Replaces the label and triggers a redraw and relayout.
 *
 * @param label New label text; copied internally.
 */
void
TTZDisplay::SetLabel(const char* label)
{
	fLabel.SetTo(label);
	Invalidate();
	InvalidateLayout();
}


/**
 * @brief Returns the current city description string.
 */
const char*
TTZDisplay::Text() const
{
	return fText.String();
}


/**
 * @brief Replaces the city description and triggers a redraw and relayout.
 *
 * @param text New description text; copied internally.
 */
void
TTZDisplay::SetText(const char* text)
{
	fText.SetTo(text);
	Invalidate();
	InvalidateLayout();
}


/**
 * @brief Returns the current time string.
 */
const char*
TTZDisplay::Time() const
{
	return fTime.String();
}


/**
 * @brief Replaces the time string and triggers a redraw and relayout.
 *
 * @param time New formatted time text; copied internally.
 */
void
TTZDisplay::SetTime(const char* time)
{
	fTime.SetTo(time);
	Invalidate();
	InvalidateLayout();
}


/**
 * @brief Returns an unbounded maximum width composed with the explicit max.
 */
BSize
TTZDisplay::MaxSize()
{
	BSize size = _CalcPrefSize();
	size.width = B_SIZE_UNLIMITED;

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		size);
}


/**
 * @brief Returns the minimum size composed with the explicit minimum.
 */
BSize
TTZDisplay::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		_CalcPrefSize());
}


/**
 * @brief Returns the preferred size composed with the explicit preferred.
 */
BSize
TTZDisplay::PreferredSize()
{
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		_CalcPrefSize());
}


/**
 * @brief Computes the preferred size from the current text contents.
 *
 * Height is two text lines including leading; width is the wider of the
 * (label + space + time) line and the city description line, plus a small
 * padding constant.
 *
 * @return BSize whose width fits the longer line and whose height fits
 *         two lines.
 */
BSize
TTZDisplay::_CalcPrefSize()
{
	font_height fontHeight;
	GetFontHeight(&fontHeight);

	BSize size;
	size.height = 2 * ceilf(fontHeight.ascent + fontHeight.descent +
		fontHeight.leading);

	// Add a little padding
	float padding = 10.0;
	float firstLine = ceilf(StringWidth(fLabel.String()) +
		StringWidth(" ") + StringWidth(fTime.String()) + padding);
	float secondLine = ceilf(StringWidth(fText.String()) + padding);
	size.width = firstLine > secondLine ? firstLine : secondLine;

	return size;
}
