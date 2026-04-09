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
 *   Copyright 2021, Pascal R. G. Abresch, nep@packageloss.eu.
 *   Distributed under the terms of the MIT License.
 */

/** @file StatusView.cpp
 *  @brief Provides \c BPrivate::AdoptScrollBarFontSize(), a helper that
 *         adjusts a view's font size to fit within the system scroll-bar
 *         width using binary search.
 */

#include <ControlLook.h>
#include <View.h>


namespace BPrivate {


/**
 * @brief Sets \a view's font size so that the font height fits within the
 *        system scroll-bar width.
 *
 * Performs a binary search between 0 and 48 points, querying
 * \c font_height::ascent + \c font_height::descent at each midpoint, until
 * the range collapses to within 1 point. The largest size whose rendered
 * height does not exceed \c be_control_look->GetScrollBarWidth() is applied
 * via \c BView::SetFontSize().
 *
 * @param view The view whose font size is to be adjusted. Must not be
 *             \c NULL.
 */
void
AdoptScrollBarFontSize(BView* view)
{
	float maxSize = be_control_look->GetScrollBarWidth();
	BFont testFont = be_plain_font;
	float currentSize;
	font_height fontHeight;

	float minFontSize = 0.0f;
	float maxFontSize = 48.0f;

	while (maxFontSize - minFontSize > 1.0f) {
		float midFontSize = (maxFontSize + minFontSize) / 2.0f;

		testFont.SetSize(midFontSize);
		testFont.GetHeight(&fontHeight);
		currentSize = fontHeight.ascent + fontHeight.descent;

		if (currentSize > maxSize)
			maxFontSize = midFontSize;
		else
			minFontSize = midFontSize;
	}

	view->SetFontSize(minFontSize);
}


} // namespace BPrivate
