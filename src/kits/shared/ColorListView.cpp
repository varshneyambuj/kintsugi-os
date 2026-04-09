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
 *       DarkWyrm, darkwyrm@earthlink.net
 *       John Scipione, jscipione@gmail.com
 */

/** @file ColorListView.cpp
 *  @brief Implements BColorListView, a BListView subclass that supports
 *         drag-and-drop of color values from BColorItem entries.
 */


#include <ColorListView.h>

#include <algorithm>

#include <math.h>
#include <stdio.h>

#include <Bitmap.h>
#include <ColorItem.h>
#include <String.h>


// golden ratio
#ifdef M_PHI
#	undef M_PHI
#endif
#define M_PHI 1.61803398874989484820


namespace BPrivate {


//	#pragma mark - ColorListView


/** @brief Constructs a BColorListView.
 *  @param name   The internal name of the view.
 *  @param type   The list selection type (single or multiple).
 *  @param flags  View flags passed to BListView.
 */
BColorListView::BColorListView(const char* name, list_view_type type, uint32 flags)
	:
	BListView(name, type, flags)
{
}


/** @brief Destructor. */
BColorListView::~BColorListView()
{
}


/** @brief Initiates a drag-and-drop operation for the color at \a index.
 *
 *  Constructs a drag message carrying the color in both hex-string and
 *  raw RGBColor forms, and a small color-chip bitmap for visual feedback.
 *
 *  @param where        The point where the drag was initiated (view coordinates).
 *  @param index        The index of the item being dragged.
 *  @param wasSelected  True if the item was already selected before the drag.
 *  @return True if a drag was successfully started, false otherwise.
 */
bool
BColorListView::InitiateDrag(BPoint where, int32 index, bool wasSelected)
{
	BColorItem* item = dynamic_cast<BColorItem*>(ItemAt(index));
	if (item == NULL)
		return false;

	rgb_color color = item->Color();

	BString hexStr;
	hexStr.SetToFormat("#%.2X%.2X%.2X", color.red, color.green, color.blue);

	BMessage message(B_PASTE);
	message.AddData("text/plain", B_MIME_TYPE, hexStr.String(), hexStr.Length());
	message.AddData("RGBColor", B_RGB_COLOR_TYPE, &color, sizeof(color));
	message.AddMessenger("be:sender", BMessenger(this));
	message.AddPointer("source", this);
	message.AddInt64("when", (int64)system_time());

	float itemHeight = item->Height() - 5;
	BRect rect(0, 0, roundf(itemHeight * M_PHI) - 1, itemHeight - 1);

	BBitmap* bitmap = new BBitmap(rect, B_RGB32, true);
	if (bitmap->Lock()) {
		BView* view = new BView(rect, "", B_FOLLOW_NONE, B_WILL_DRAW);
		bitmap->AddChild(view);

		view->SetHighColor(B_TRANSPARENT_COLOR);
		view->FillRect(view->Bounds());

		++rect.top;
		++rect.left;

		view->SetHighColor(0, 0, 0, 100);
		view->FillRect(rect);
		rect.OffsetBy(-1, -1);

		int32 red = (int32)std::min(255, (int)(1.2 * color.red + 40));
		int32 green = (int32)std::min(255, (int)(1.2 * color.green + 40));
		int32 blue = (int32)std::min(255, (int)(1.2 * color.blue + 40));

		view->SetHighColor(red, green, blue);
		view->StrokeRect(rect);

		++rect.left;
		++rect.top;

		red = (int32)(0.8 * color.red);
		green = (int32)(0.8 * color.green);
		blue = (int32)(0.8 * color.blue);

		view->SetHighColor(red, green, blue);
		view->StrokeRect(rect);

		--rect.right;
		--rect.bottom;

		view->SetHighColor(color.red, color.green, color.blue);
		view->FillRect(rect);
		view->Sync();

		bitmap->Unlock();
	}

	DragMessage(&message, bitmap, B_OP_ALPHA, BPoint(14, 14));

	return true;
}


/** @brief Handles mouse-up events.
 *
 *  Currently delegates to BView::MouseUp.  A future TODO notes that drops
 *  from external views should not alter the selection.
 *
 *  @param where  The position of the mouse cursor when the button was released.
 */
void
BColorListView::MouseUp(BPoint where)
{
	// TODO drag and drop from an external view should not alter selection
	BView::MouseUp(where);
}


} // namespace BPrivate
