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
 *   Copyright 2016-2024 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm, bpmagic@columbus.rr.com
 *       Rene Gollent, rene@gollent.com
 *       Ryan Leavengood, leavengood@gmail.com
 *       John Scipione, jscipione@gmail.com
 *
 *   Based on ColorWhichItem by DarkWyrm (bpmagic@columbus.rr.com)
 */

/** @file ColorItem.cpp
 *  @brief Implements BColorItem, a BStringItem subclass that displays a color
 *         swatch alongside a text label in a list view.
 */


#include <ColorItem.h>

#include <math.h>

#include <ControlLook.h>
#include <View.h>


// golden ratio
#ifdef M_PHI
#	undef M_PHI
#endif
#define M_PHI 1.61803398874989484820


namespace BPrivate {


//	#pragma mark - BColorItem


/** @brief Constructs a BColorItem with an explicit color value.
 *  @param string  The text label for the list item.
 *  @param color   The color swatch to display next to the label.
 */
BColorItem::BColorItem(const char* string, rgb_color color)
	:
	BStringItem(string, 0, false),
	fColor(color),
	fColorWhich(B_NO_COLOR)
{
}


/** @brief Constructs a BColorItem with a system color constant and a fallback.
 *  @param string  The text label for the list item.
 *  @param which   The color_which constant that identifies the system color.
 *  @param color   Fallback rgb_color used when the system color is unavailable.
 */
BColorItem::BColorItem(const char* string, color_which which, rgb_color color)
	:
	BStringItem(string, 0, false),
	fColor(color),
	fColorWhich(which)
{
}


/** @brief Draws the color swatch and text label within the given frame.
 *
 *  Renders a highlighted or plain background depending on the selection
 *  state, then draws a golden-ratio-proportioned color rectangle followed
 *  by the item's text string.
 *
 *  @param owner     The BView that owns this list item.
 *  @param frame     The bounding rectangle allocated to this item.
 *  @param complete  True if the entire item area must be redrawn from scratch.
 */
void
BColorItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	rgb_color highColor = owner->HighColor();
	rgb_color lowColor = owner->LowColor();

	if (IsSelected() || complete) {
		if (IsSelected()) {
			owner->SetHighUIColor(B_LIST_SELECTED_BACKGROUND_COLOR);
			owner->SetLowColor(owner->HighColor());
		} else
			owner->SetHighColor(lowColor);

		owner->FillRect(frame);
	}

	float spacer = ceilf(be_control_look->DefaultItemSpacing() / 2);

	BRect colorRect(frame);
	colorRect.InsetBy(2.0f, 2.0f);
	colorRect.left += spacer;
	colorRect.right = colorRect.left + floorf(colorRect.Height() * M_PHI);

	// draw the colored box
	owner->SetHighColor(fColor);
	owner->FillRect(colorRect);

	// draw the border
	owner->SetHighUIColor(B_CONTROL_BORDER_COLOR);
	owner->StrokeRect(colorRect);

	// draw the string
	owner->MovePenTo(colorRect.right + spacer, frame.top + BaselineOffset());

	if (!IsEnabled()) {
		rgb_color textColor = ui_color(B_LIST_ITEM_TEXT_COLOR);
		if (textColor.red + textColor.green + textColor.blue > 128 * 3)
			owner->SetHighColor(tint_color(textColor, B_DARKEN_2_TINT));
		else
			owner->SetHighColor(tint_color(textColor, B_LIGHTEN_2_TINT));
	} else {
		if (IsSelected())
			owner->SetHighUIColor(B_LIST_SELECTED_ITEM_TEXT_COLOR);
		else
			owner->SetHighUIColor(B_LIST_ITEM_TEXT_COLOR);
	}

	owner->DrawString(Text());

	owner->SetHighColor(highColor);
	owner->SetLowColor(lowColor);
}


} // namespace BPrivate
