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
 *   Copyright 2010-2013 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       Adrien Destugues, pulkomandy@pulkomandy.ath.cx
 *       Axel Dörfler, axeld@pinc-software.de
 *       John Scipione, jscipione@gmail.com
 *       Oliver Tappe, zooey@hirschkaefer.de
 */


/**
 * @file TimeZoneListItem.cpp
 * @brief Implementation of TimeZoneListItem, one row in the time-zone list.
 *
 * Each item carries an optional BCountry (used to draw a flag icon) and an
 * optional BTimeZone (used for tool tips and the OffsetFromGMT lookup).
 * Pure region or country header rows have neither; only leaf time-zone
 * entries supply both icon and zone data.
 */


#include "TimeZoneListItem.h"

#include <new>

#include <Bitmap.h>
#include <Country.h>
#include <ControlLook.h>
#include <String.h>
#include <TimeZone.h>
#include <Window.h>


/** @brief Sentinel empty string returned by ID()/Name() when no zone is set. */
static const BString skDefaultString;


/**
 * @brief Constructs a list item with optional country and time-zone info.
 *
 * Region header rows pass NULL for both @a country and @a timeZone; leaf
 * time-zone rows supply both. Ownership of both pointers transfers to this
 * item and they are deleted in the destructor.
 *
 * @param text     Display label shown in the list view.
 * @param country  Country whose flag icon should be drawn, or NULL.
 * @param timeZone Time zone associated with this row, or NULL.
 */
TimeZoneListItem::TimeZoneListItem(const char* text, BCountry* country,
	BTimeZone* timeZone)
	:
	BStringItem(text, 0, false),
	fCountry(country),
	fTimeZone(timeZone),
	fIcon(NULL)
{
}


/**
 * @brief Releases the owned country, time-zone, and cached icon bitmap.
 */
TimeZoneListItem::~TimeZoneListItem()
{
	delete fCountry;
	delete fTimeZone;
	delete fIcon;
}


/**
 * @brief Draws the row, prefixing the label with the country flag if any.
 *
 * @param owner    View into which the row is drawn.
 * @param frame    Rectangle assigned to this row.
 * @param complete Whether the entire row background should be repainted.
 */
void
TimeZoneListItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	if (fIcon != NULL && fIcon->IsValid()) {
		float iconSize = fIcon->Bounds().Width();
		_DrawItemWithTextOffset(owner, frame, complete,
			iconSize + be_control_look->DefaultLabelSpacing());

		BRect iconFrame(frame.left + be_control_look->DefaultLabelSpacing(),
			frame.top,
			frame.left + iconSize - 1 + be_control_look->DefaultLabelSpacing(),
			frame.top + iconSize - 1);
		owner->SetDrawingMode(B_OP_OVER);
		owner->DrawBitmap(fIcon, iconFrame);
		owner->SetDrawingMode(B_OP_COPY);
	} else
		_DrawItemWithTextOffset(owner, frame, complete, 0);
}


/**
 * @brief Refreshes the cached icon bitmap when the row height changes.
 *
 * Recomputes the row width to make space for the icon and rebuilds the
 * country flag bitmap at the new size. Rows without an associated country
 * bypass the bitmap creation entirely.
 *
 * @param owner View whose font metrics drive the new row height.
 * @param font  Font used to render the label text.
 */
void
TimeZoneListItem::Update(BView* owner, const BFont* font)
{
	float oldIconSize = Height();
	BStringItem::Update(owner, font);
	if (!HasCountry())
		return;

	float iconSize = Height();
	if (iconSize == oldIconSize && fIcon != NULL)
		return;

	SetWidth(Width() + iconSize + be_control_look->DefaultLabelSpacing());

	delete fIcon;
	fIcon = new(std::nothrow) BBitmap(BRect(0, 0, iconSize - 1, iconSize - 1),
		B_RGBA32);
	if (fIcon != NULL && fCountry->GetIcon(fIcon) != B_OK) {
		delete fIcon;
		fIcon = NULL;
	}
}


/**
 * @brief Replaces the owned country, deleting the previous one.
 *
 * @param country New country whose ownership transfers to this item.
 */
void
TimeZoneListItem::SetCountry(BCountry* country)
{
	delete fCountry;
	fCountry = country;
}


/**
 * @brief Replaces the owned time zone, deleting the previous one.
 *
 * @param timeZone New zone whose ownership transfers to this item.
 */
void
TimeZoneListItem::SetTimeZone(BTimeZone* timeZone)
{
	delete fTimeZone;
	fTimeZone = timeZone;
}


/**
 * @brief Returns the IANA zone ID, or an empty string when no zone is set.
 *
 * @return Zone ID for leaf rows, empty sentinel for header rows.
 */
const BString&
TimeZoneListItem::ID() const
{
	if (!HasTimeZone())
		return skDefaultString;

	return fTimeZone->ID();
}


/**
 * @brief Returns the localized zone name, or empty string when none is set.
 *
 * @return Localized zone name for leaf rows, empty sentinel otherwise.
 */
const BString&
TimeZoneListItem::Name() const
{
	if (!HasTimeZone())
		return skDefaultString;

	return fTimeZone->Name();
}


/**
 * @brief Returns the zone's UTC offset in seconds, or zero when unknown.
 *
 * @return Offset from GMT in seconds, including any DST adjustment.
 */
int
TimeZoneListItem::OffsetFromGMT() const
{
	if (!HasTimeZone())
		return 0;

	return fTimeZone->OffsetFromGMT();
}


/**
 * @brief Draws the row text and selection background at a given text offset.
 *
 * Used by DrawItem() to share rendering between the icon-prefixed and
 * unprefixed code paths. Adjusts colors for selected and disabled states
 * before drawing the label, then restores the previous high/low colors.
 *
 * @param owner      View into which the row is drawn.
 * @param frame      Rectangle assigned to this row.
 * @param complete   Whether the entire row background should be repainted.
 * @param textOffset Horizontal pixel offset applied before drawing the
 *                   label, used to make room for the country icon.
 */
void
TimeZoneListItem::_DrawItemWithTextOffset(BView* owner, BRect frame,
	bool complete, float textOffset)
{
	rgb_color highColor = owner->HighColor();
	rgb_color lowColor = owner->LowColor();

	if (IsSelected() || complete) {
		rgb_color color;
		if (IsSelected())
			color = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
		else
			color = owner->ViewColor();

		owner->SetHighColor(color);
		owner->SetLowColor(color);
		owner->FillRect(frame);
	} else
		owner->SetLowColor(owner->ViewColor());

	if (!IsEnabled()) {
		rgb_color textColor = ui_color(B_LIST_ITEM_TEXT_COLOR);
		if (textColor.red + textColor.green + textColor.blue > 128 * 3)
			owner->SetHighColor(tint_color(textColor, B_DARKEN_2_TINT));
		else
			owner->SetHighColor(tint_color(textColor, B_LIGHTEN_2_TINT));
	} else {
		if (IsSelected())
			owner->SetHighColor(ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR));
		else
			owner->SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));
	}

	owner->MovePenTo(
		frame.left + be_control_look->DefaultLabelSpacing() + textOffset,
		frame.top + BaselineOffset());
	owner->DrawString(Text());

	owner->SetHighColor(highColor);
	owner->SetLowColor(lowColor);
}
