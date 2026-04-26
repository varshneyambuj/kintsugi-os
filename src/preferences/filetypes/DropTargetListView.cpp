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
 *   Copyright 2006, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file DropTargetListView.cpp
 * @brief Implementation of DropTargetListView, a BListView that draws a
 *        focus frame while a compatible drag is hovering, providing visual
 *        feedback for drop targets in the FileTypes preference app.
 */


#include "DropTargetListView.h"


/**
 * @brief Constructs the list view in non-drop-target state.
 *
 * @param name   Layout name forwarded to BListView.
 * @param type   Single- or multi-selection style.
 * @param flags  View creation flags forwarded to BListView.
 */
DropTargetListView::DropTargetListView(const char* name,
		list_view_type type, uint32 flags)
	: BListView(name, type, flags),
	fDropTarget(false)
{
}


/**
 * @brief Destructor; no owned resources.
 */
DropTargetListView::~DropTargetListView()
{
}


/**
 * @brief Renders the list contents and overlays a 2-pixel target frame
 *        when a valid drag is hovering inside the view.
 *
 * @param updateRect  Region requested by the view's redraw notification.
 */
void
DropTargetListView::Draw(BRect updateRect)
{
	BListView::Draw(updateRect);

	if (fDropTarget) {
		// mark this view as a drop target
		rgb_color color = HighColor();

		SetHighColor(0, 0, 0);
		SetPenSize(2);
		BRect rect = Bounds();
// TODO: this is an incompatibility between R5 and Haiku and should be fixed!
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
		rect.left++;
		rect.top++;
#else
		rect.right--;
		rect.bottom--;
#endif
		StrokeRect(rect);

		SetPenSize(1);
		SetHighColor(color);
	}
}


/**
 * @brief Tracks pointer transit during drag-and-drop and updates the
 *        drop-target highlight.
 *
 * Sets the internal target flag when @a dragMessage is acceptable and the
 * pointer is inside the view, clears it otherwise, and only repaints the
 * affected edge strips when the flag changes.
 *
 * @param where        Mouse position in view coordinates (unused).
 * @param transit      One of B_ENTERED_VIEW / B_INSIDE_VIEW /
 *                     B_EXITED_VIEW / B_OUTSIDE_VIEW.
 * @param dragMessage  Drag payload, or NULL when no drag is in progress.
 */
void
DropTargetListView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (dragMessage != NULL && AcceptsDrag(dragMessage)) {
		bool dropTarget = transit == B_ENTERED_VIEW || transit == B_INSIDE_VIEW;
		if (dropTarget != fDropTarget) {
			fDropTarget = dropTarget;
			_InvalidateFrame();
		}
	} else if (fDropTarget) {
		fDropTarget = false;
		_InvalidateFrame();
	}
}


/**
 * @brief Default policy: accept any drag. Subclasses override to restrict.
 *
 * @return Always true in the base implementation.
 */
bool
DropTargetListView::AcceptsDrag(const BMessage* /*message*/)
{
	return true;
}


/**
 * @brief Invalidates only the four 1-pixel edge strips of the view.
 *
 * Used after a drop-target state change so the highlight frame redraws
 * without forcing a full content redraw.
 */
void
DropTargetListView::_InvalidateFrame()
{
	// only update the parts affected by the change to reduce flickering
	BRect rect = Bounds();
	rect.right = rect.left + 1;
	Invalidate(rect);
	
	rect = Bounds();
	rect.left = rect.right - 1;
	Invalidate(rect);

	rect = Bounds();
	rect.bottom = rect.top + 1;
	Invalidate(rect);

	rect = Bounds();
	rect.top = rect.bottom - 1;
	Invalidate(rect);
}
