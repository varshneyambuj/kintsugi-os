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
 *   Copyright 2011, Alexandre Deckner, alex@zappotek.com
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Alexandre Deckner, alex@zappotek.com
 */

/** @file LongAndDragTrackingFilter.cpp
 *  @brief Implements LongAndDragTrackingFilter, a BMessageFilter that detects
 *         long mouse presses and pointer drags, sending distinct notification
 *         messages for each gesture type.
 */

/*!
	\class LongAndDragTrackingFilter
	\brief A simple long mouse down and drag detection filter
	*
	* A simple mouse filter that detects long clicks and pointer drags.
	* A long click message is sent when the mouse button is kept down
	* for a duration longer than a given threshold while the pointer stays
	* within the limits of a given threshold radius.
	* A drag message is triggered if the mouse goes further than the
	* threshold radius before the duration threshold elapsed.
	*
	* The messages contain the pointer position and the buttons state at
	* the moment of the click. The drag message is ready to use with the
	* be/haiku drag and drop API cf. comment in code.
	*
	* Current limitation: A long mouse down or a drag can be detected for
	* any mouse button, but any released button cancels the tracking.
	*
*/


#include <LongAndDragTrackingFilter.h>

#include <Message.h>
#include <Messenger.h>
#include <MessageRunner.h>
#include <View.h>

#include <new>


/** @brief Constructs a LongAndDragTrackingFilter.
 *  @param longMessageWhat      The 'what' field for the long-press message.
 *  @param dragMessageWhat      The 'what' field for the drag-start message.
 *  @param radiusThreshold      Pointer must stay within this radius (in pixels)
 *                              for a long-press to be recognized.
 *  @param durationThreshold    Microseconds the button must be held for a
 *                              long-press.  Pass 0 to use the system double-
 *                              click speed.
 */
LongAndDragTrackingFilter::LongAndDragTrackingFilter(uint32 longMessageWhat,
	uint32 dragMessageWhat, float radiusThreshold,
	bigtime_t durationThreshold)
	:
	BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
	fLongMessageWhat(longMessageWhat),
	fDragMessageWhat(dragMessageWhat),
	fMessageRunner(NULL),
	fClickButtons(0),
	fSquaredRadiusThreshold(radiusThreshold * radiusThreshold),
	fDurationThreshold(durationThreshold)
{
	if (durationThreshold == 0) {
		get_click_speed(&fDurationThreshold);
			// use system's doubleClickSpeed as default threshold
	}
}


/** @brief Destructor — cancels any pending long-press timer. */
LongAndDragTrackingFilter::~LongAndDragTrackingFilter()
{
	delete fMessageRunner;
}


/** @brief Cancels the long-press timer without sending a long-press message. */
void
LongAndDragTrackingFilter::_StopTracking()
{
	delete fMessageRunner;
	fMessageRunner = NULL;
}


/** @brief Filters mouse messages to detect long presses and drags.
 *
 *  On B_MOUSE_DOWN (single click) a BMessageRunner is armed; if it fires
 *  before the pointer moves outside the radius threshold, the long-press
 *  message is dispatched.  On B_MOUSE_MOVED the squared distance is tested
 *  against the radius threshold; if exceeded, the drag message is sent and
 *  the timer is cancelled.  On B_MOUSE_UP the timer is always cancelled.
 *
 *  @param message  The message to examine.
 *  @param target   Pointer to the handler that will receive the message.
 *  @return Always B_DISPATCH_MESSAGE so all handlers can process the event.
 */
filter_result
LongAndDragTrackingFilter::Filter(BMessage* message, BHandler** target)
{
	if (*target == NULL)
		return B_DISPATCH_MESSAGE;

	switch (message->what) {
		case B_MOUSE_DOWN: {
			int32 clicks = 0;

			message->FindInt32("buttons", (int32*)&fClickButtons);
			message->FindInt32("clicks", (int32*)&clicks);

			if (fClickButtons != 0 && clicks == 1) {

				BView* targetView = dynamic_cast<BView*>(*target);
				if (targetView != NULL)
					targetView->SetMouseEventMask(B_POINTER_EVENTS);

				message->FindPoint("where", &fClickPoint);
				BMessage message(fLongMessageWhat);
				message.AddPoint("where", fClickPoint);
				message.AddInt32("buttons", fClickButtons);

				delete fMessageRunner;
				fMessageRunner = new (std::nothrow) BMessageRunner(
					BMessenger(*target), &message, fDurationThreshold, 1);
			}
			return B_DISPATCH_MESSAGE;
		}

		case B_MOUSE_UP:
			_StopTracking();
			message->AddInt32("last_buttons", (int32)fClickButtons);
			message->FindInt32("buttons", (int32*)&fClickButtons);
			return B_DISPATCH_MESSAGE;

		case B_MOUSE_MOVED:
		{
			if (fMessageRunner != NULL) {
				BPoint where;
				message->FindPoint("be:view_where", &where);

				BPoint delta(fClickPoint - where);
				float squaredDelta = (delta.x * delta.x) + (delta.y * delta.y);

				if (squaredDelta >= fSquaredRadiusThreshold) {
					BMessage dragMessage(fDragMessageWhat);
					dragMessage.AddPoint("be:view_where", fClickPoint);
						// name it "be:view_where" since BView::DragMessage
						// positions the dragging frame/bitmap by retrieving
						// the current message and reading that field
					dragMessage.AddInt32("buttons", (int32)fClickButtons);
					BMessenger messenger(*target);
					messenger.SendMessage(&dragMessage);

					_StopTracking();
				}
			}
			return B_DISPATCH_MESSAGE;
		}

		default:
			if (message->what == fLongMessageWhat) {
				_StopTracking();
				return B_DISPATCH_MESSAGE;
			}
			break;
	}

	return B_DISPATCH_MESSAGE;
}
