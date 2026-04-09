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
 *   Copyright 2007-2016 Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ryan Leavengood <leavengood@gmail.com>
 *       John Scipione <jscipione@gmail.com>
 *       Joseph Groover <looncraz@looncraz.net>
 *       Brian Hill <supernova@tycho.email>
 */

/** @file StripeView.cpp
 *  @brief Implements \c BStripeView, a decorative panel used in alert-style
 *         dialogs. It renders a shaded left stripe and draws an icon bitmap
 *         at a scaled offset matching the \c BAlert layout conventions.
 */

#include "StripeView.h"

#include <LayoutUtils.h>


/** Width of the shaded left stripe in unscaled layout units. */
static const int kIconStripeWidth = 30;


/**
 * @brief Constructs a \c BStripeView that displays \a icon.
 *
 * Sets the view colour to \c B_PANEL_BACKGROUND_COLOR and, when \a icon is
 * valid, computes the preferred dimensions using the same scaling factor
 * (\c icon_layout_scale()) as \c BAlert so that the icon and surrounding
 * margins remain consistent across DPI settings.
 *
 * @param icon Reference to the icon bitmap to display. The bitmap must
 *             remain valid for the lifetime of this view; ownership is not
 *             transferred.
 */
BStripeView::BStripeView(BBitmap& icon)
	:
	BView("StripeView", B_WILL_DRAW),
	fIcon(icon),
	fIconSize(0.0),
	fPreferredWidth(0.0),
	fPreferredHeight(0.0)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	if (fIcon.IsValid()) {
		fIconSize = fIcon.Bounds().Width();
		// Use the same scaling as a BAlert
		int32 scale = icon_layout_scale();
		fPreferredWidth = 18 * scale + fIcon.Bounds().Width();
		fPreferredHeight = 6 * scale + fIcon.Bounds().Height();
	}
}


/**
 * @brief Draws the stripe background and the icon bitmap.
 *
 * If no icon is present (\c fIconSize == 0) the function returns immediately.
 * Otherwise it fills the full update rectangle with the panel background
 * colour, then draws a slightly darker stripe on the left whose width is
 * \c kIconStripeWidth scaled by \c icon_layout_scale(). The icon bitmap is
 * composited using alpha-overlay blending at the appropriate scaled offset.
 *
 * @param updateRect The rectangle that needs to be redrawn.
 */
void
BStripeView::Draw(BRect updateRect)
{
	if (fIconSize == 0)
		return;

	SetHighColor(ViewColor());
	FillRect(updateRect);

	BRect stripeRect = Bounds();
	int32 iconLayoutScale = icon_layout_scale();
	stripeRect.right = kIconStripeWidth * iconLayoutScale;
	SetHighColor(tint_color(ViewColor(), B_DARKEN_1_TINT));
	FillRect(stripeRect);

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	DrawBitmapAsync(&fIcon, BPoint(18 * iconLayoutScale,
		6 * iconLayoutScale));
}


/**
 * @brief Returns the preferred size of the stripe view.
 *
 * The preferred width accounts for the stripe and the icon; the height is
 * left as \c B_SIZE_UNSET so the layout system derives it from the parent
 * or sibling constraints.
 *
 * @return A \c BSize with the preferred width and \c B_SIZE_UNSET height.
 */
BSize
BStripeView::PreferredSize()
{
	return BSize(fPreferredWidth, B_SIZE_UNSET);
}


/**
 * @brief Fills the caller's pointers with the preferred width and height.
 *
 * Either pointer may be \c NULL if the caller does not need that dimension.
 *
 * @param _width  Set to the preferred width in pixels, or left untouched if
 *                \c NULL.
 * @param _height Set to the preferred height in pixels, or left untouched if
 *                \c NULL.
 */
void
BStripeView::GetPreferredSize(float* _width, float* _height)
{
	if (_width != NULL)
		*_width = fPreferredWidth;

	if (_height != NULL)
		*_height = fPreferredHeight;
}


/**
 * @brief Returns the maximum allowable size of the stripe view.
 *
 * The maximum width is capped at \c fPreferredWidth while the maximum height
 * is \c B_SIZE_UNLIMITED, composed with any explicitly set maximum size via
 * \c BLayoutUtils::ComposeSize().
 *
 * @return A \c BSize representing the maximum dimensions.
 */
BSize
BStripeView::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		BSize(fPreferredWidth, B_SIZE_UNLIMITED));
}
