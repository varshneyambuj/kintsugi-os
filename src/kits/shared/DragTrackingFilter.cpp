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
 *   Copyright 2009, Alexandre Deckner, alex@zappotek.com
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Alexandre Deckner, alex@zappotek.com
 */

/** @file DragTrackingFilter.cpp
 *  @brief Implements DragTrackingFilter, a BMessageFilter that detects the
 *         start of a mouse drag once the pointer moves beyond a threshold
 *         distance and sends a configurable notification message.
 */

/*!
	\class DragTrackingFilter
	\brief A simple mouse drag detection filter
	*
	* A simple mouse filter that detects the start of a mouse drag over a
	* threshold distance and sends a message with the 'what' field of your
	* choice. Especially useful for drag and drop.
	* Allows you to free your code of encumbering mouse tracking details.
	*
	* It can detect fast drags spanning outside of a small view by temporarily
	* setting the B_POINTER_EVENTS flag on the view.
*/

#include <DragTrackingFilter.h>

#include <Message.h>
#include <Messenger.h>
#include <View.h>

static const int kSquaredDragThreshold = 9;

/** @brief Constructs a DragTrackingFilter for the given view.
 *  @param targetView   The view to watch for drag gestures.
 *  @param messageWhat  The 'what' field of the message sent when a drag is
 *                      detected.
 */
DragTrackingFilter::DragTrackingFilter(BView* targetView, uint32 messageWhat)
	: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
	fTargetView(targetView),
	fMessageWhat(messageWhat),
	fIsTracking(false),
	fClickButtons(0)
{
}


/** @brief Intercepts mouse messages and fires a drag notification when the
 *         pointer moves beyond the squared threshold distance.
 *
 *  On B_MOUSE_DOWN the click point and buttons are recorded and pointer
 *  event tracking is enabled on the target view.  On B_MOUSE_MOVED the
 *  squared distance from the click point is compared to
 *  kSquaredDragThreshold; if exceeded a message is sent to the target view
 *  and tracking stops.
 *
 *  @param message  The message to examine.
 *  @param _target  Unused handler pointer (may be modified by other filters).
 *  @return Always B_DISPATCH_MESSAGE so that other handlers also receive the event.
 */
filter_result
DragTrackingFilter::Filter(BMessage* message, BHandler** /*_target*/)
{
	if (fTargetView == NULL)
		return B_DISPATCH_MESSAGE;

	switch (message->what) {
		case B_MOUSE_DOWN:
			message->FindPoint("where", &fClickPoint);
			message->FindInt32("buttons", (int32*)&fClickButtons);
			fIsTracking = true;

			fTargetView->SetMouseEventMask(B_POINTER_EVENTS);

			return B_DISPATCH_MESSAGE;

		case B_MOUSE_UP:
			fIsTracking = false;
			return B_DISPATCH_MESSAGE;

		case B_MOUSE_MOVED:
		{
			BPoint where;
			message->FindPoint("be:view_where", &where);

			// TODO: be more flexible about buttons and pass their state
			//		 in the message
			if (fIsTracking && (fClickButtons & B_PRIMARY_MOUSE_BUTTON)) {

				BPoint delta(fClickPoint - where);
				float squaredDelta = (delta.x * delta.x) + (delta.y * delta.y);

				if (squaredDelta >= kSquaredDragThreshold) {
					BMessage dragClickMessage(fMessageWhat);
					dragClickMessage.AddPoint("be:view_where", fClickPoint);
						// name it "be:view_where" since BView::DragMessage
						// positions the dragging frame/bitmap by retrieving the
						// current message and reading that field
					BMessenger messenger(fTargetView);
					messenger.SendMessage(&dragClickMessage);

					fIsTracking = false;
				}
			}
			return B_DISPATCH_MESSAGE;
		}
		default:
			break;
	}

	return B_DISPATCH_MESSAGE;
}
