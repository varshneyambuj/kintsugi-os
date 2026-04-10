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
 *   Copyright 2001-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini, burton666@libero.it
 *       Marc Flerackers, mflerackers@androme.be
 *       Bill Hayden, haydentech@users.sourceforge.net
 */


/**
 * @file SeparatorItem.cpp
 * @brief Implementation of BSeparatorItem, a visual divider item in a BMenu
 *
 * BSeparatorItem draws a horizontal rule between groups of menu items. It is
 * non-selectable and non-interactive, serving only as a visual separator.
 *
 * @see BMenuItem, BMenu
 */


#include <SeparatorItem.h>

#include <Font.h>


/** @brief Default constructor. Creates a disabled, empty separator item.
 *
 *  The item is immediately disabled via BMenuItem::SetEnabled(false) so
 *  that it can never be selected by the user.
 */
BSeparatorItem::BSeparatorItem()
	:
	BMenuItem("", NULL)
{
	BMenuItem::SetEnabled(false);
}


/** @brief Unarchiving constructor. Restores a BSeparatorItem from a BMessage archive.
 *
 *  Calls the BMenuItem unarchiving constructor and then forces the item
 *  disabled so that it remains non-interactive after restoration.
 *
 *  @param data The archive message produced by Archive().
 *  @see Archive(), Instantiate()
 */
BSeparatorItem::BSeparatorItem(BMessage* data)
	:
	BMenuItem(data)
{
	BMenuItem::SetEnabled(false);
}


/** @brief Destructor. */
BSeparatorItem::~BSeparatorItem()
{
}


/** @brief Archives this BSeparatorItem into the provided BMessage.
 *
 *  Delegates entirely to BMenuItem::Archive() since a separator carries
 *  no additional state beyond the base class.
 *
 *  @param data The target archive message.
 *  @param deep If true, child objects are also archived (passed to the base class).
 *  @return B_OK on success, or an error code from BMenuItem::Archive().
 */
status_t
BSeparatorItem::Archive(BMessage* data, bool deep) const
{
	return BMenuItem::Archive(data, deep);
}


/** @brief Instantiates a BSeparatorItem from an archive message.
 *
 *  Validates the archive class name before constructing. Returns NULL if
 *  validation fails.
 *
 *  @param data The archive message previously produced by Archive().
 *  @return A newly allocated BSeparatorItem, or NULL on failure.
 *  @see Archive()
 */
BArchivable*
BSeparatorItem::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BSeparatorItem"))
		return new BSeparatorItem(data);

	return NULL;
}


/** @brief Overrides BMenuItem::SetEnabled() to prevent the item from being enabled.
 *
 *  A separator must always remain disabled. This override silently ignores
 *  any request to enable the item.
 *
 *  @param enable Ignored; the separator is never enabled.
 */
void
BSeparatorItem::SetEnabled(bool enable)
{
	// Don't do anything - we don't want to get enabled ever
}


/** @brief Returns the content size of the separator rule.
 *
 *  For row-oriented menus the separator is 2x2 pixels (a thin vertical rule).
 *  For column-oriented menus the width is always 2 pixels and the height is
 *  computed from the menu font size (80% of the font size, rounded down to an
 *  even number, clamped to a minimum of 4 pixels).
 *
 *  @param _width  If not NULL, receives the separator width in pixels.
 *  @param _height If not NULL, receives the separator height in pixels.
 */
void
BSeparatorItem::GetContentSize(float* _width, float* _height)
{
	if (Menu() != NULL && Menu()->Layout() == B_ITEMS_IN_ROW) {
		if (_width != NULL)
			*_width = 2.0;

		if (_height != NULL)
			*_height = 2.0;
	} else {
		if (_width != NULL)
			*_width = 2.0;

		if (_height != NULL) {
			BFont font(be_plain_font);
			if (Menu() != NULL)
				Menu()->GetFont(&font);

			const float height = floorf((font.Size() * 0.8) / 2) * 2;
			*_height = max_c(4, height);
		}
	}
}


/** @brief Draws the separator rule within its frame rectangle.
 *
 *  Renders a two-pixel-wide rule (one dark line, one light line) centred
 *  within the item's frame. For B_ITEMS_IN_ROW menus the rule is vertical;
 *  otherwise it is horizontal. The original high color is restored after
 *  drawing.
 */
void
BSeparatorItem::Draw()
{
	BMenu *menu = Menu();
	if (menu == NULL)
		return;

	BRect bounds = Frame();
	rgb_color oldColor = menu->HighColor();
	rgb_color lowColor = menu->LowColor();

	if (menu->Layout() == B_ITEMS_IN_ROW) {
		const float startLeft = bounds.left + (floor(bounds.Width())) / 2;
		menu->SetHighColor(tint_color(lowColor, B_DARKEN_1_TINT));
		menu->StrokeLine(BPoint(startLeft, bounds.top + 1.0f),
			BPoint(startLeft, bounds.bottom - 1.0f));
		menu->SetHighColor(tint_color(lowColor, B_LIGHTEN_2_TINT));
		menu->StrokeLine(BPoint(startLeft + 1.0f, bounds.top + 1.0f),
			BPoint(startLeft + 1.0f, bounds.bottom - 1.0f));
	} else {
		const float startTop = bounds.top + (floor(bounds.Height())) / 2;
		menu->SetHighColor(tint_color(lowColor, B_DARKEN_1_TINT));
		menu->StrokeLine(BPoint(bounds.left + 1.0f, startTop),
			BPoint(bounds.right - 1.0f, startTop));
		menu->SetHighColor(tint_color(lowColor, B_LIGHTEN_2_TINT));
		menu->StrokeLine(BPoint(bounds.left + 1.0f, startTop + 1.0f),
			BPoint(bounds.right - 1.0f, startTop + 1.0f));
	}
	menu->SetHighColor(oldColor);
}


//	#pragma mark - private


void BSeparatorItem::_ReservedSeparatorItem1() {}
void BSeparatorItem::_ReservedSeparatorItem2() {}


BSeparatorItem &
BSeparatorItem::operator=(const BSeparatorItem &)
{
	return *this;
}
