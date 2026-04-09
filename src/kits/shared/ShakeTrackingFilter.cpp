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

/** @file ShakeTrackingFilter.cpp
 *  @brief Implements ShakeTrackingFilter, a BMessageFilter that detects quick
 *         mouse-shake gestures by counting direction reversals within a time
 *         window, and LowPassFilter, a fixed-size sliding-window averager for
 *         BPoint values.
 */

/*!
	\class ShakeTrackingFilter
	\brief A simple mouse shake detection filter
	*
	* A simple mouse filter that detects quick mouse shakes.
	*
	* It's detecting rough edges (u-turns) in the mouse movement
	* and counts them within a time window.
	* You can configure the message sent, the u-turn count threshold
	* and the time threshold.
	* It sends the count along with the message.
	* For now, detection is limited within the view bounds, but
	* it might be modified to accept a BRegion mask.
	*
*/


#include <ShakeTrackingFilter.h>

#include <Message.h>
#include <Messenger.h>
#include <MessageRunner.h>
#include <View.h>


const uint32 kMsgCancel = 'Canc';


/** @brief Constructs a ShakeTrackingFilter.
 *  @param targetView      The view inside which shakes are detected.
 *  @param messageWhat     The 'what' field of the message sent on shake detection.
 *  @param countThreshold  Number of direction reversals required to trigger the
 *                         shake message.
 *  @param timeThreshold   Time window in microseconds within which all reversals
 *                         must occur.
 */
ShakeTrackingFilter::ShakeTrackingFilter(BView* targetView, uint32 messageWhat,
	uint32 countThreshold, bigtime_t timeThreshold)
	:
	BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
	fTargetView(targetView),
	fMessageWhat(messageWhat),
	fCancelRunner(NULL),
	fLowPass(8),
	fLastDelta(0, 0),
	fCounter(0),
	fCountThreshold(countThreshold),
	fTimeThreshold(timeThreshold)
{
}


/** @brief Destructor — cancels the reset timer. */
ShakeTrackingFilter::~ShakeTrackingFilter()
{
	delete fCancelRunner;
}


/** @brief Filters mouse-moved messages to count direction reversals.
 *
 *  On each B_MOUSE_MOVED event inside the target view's bounds the movement
 *  delta is smoothed through a low-pass filter.  The dot product between
 *  consecutive smoothed deltas is used to detect a u-turn (negative dot
 *  product).  When the reversal count reaches fCountThreshold the shake
 *  message is sent.  A cancel timer resets the counter after fTimeThreshold
 *  microseconds of inactivity.
 *
 *  @param message  The message to examine.
 *  @param _target  Unused handler pointer.
 *  @return B_DISPATCH_MESSAGE for most messages; B_SKIP_MESSAGE for the
 *          internal cancel timer message.
 */
filter_result
ShakeTrackingFilter::Filter(BMessage* message, BHandler** /*_target*/)
{
	if (fTargetView == NULL)
		return B_DISPATCH_MESSAGE;

	switch (message->what) {
		case B_MOUSE_MOVED:
		{
			BPoint position;
			message->FindPoint("be:view_where", &position);

			// TODO: allow using BRegion masks
			if (!fTargetView->Bounds().Contains(position))
				return B_DISPATCH_MESSAGE;

			fLowPass.Input(position - fLastPosition);

			BPoint delta = fLowPass.Output();

			// normalized dot product
			float norm = delta.x * delta.x + delta.y * delta.y;
			if (norm > 0.01) {
				delta.x /= norm;
				delta.y /= norm;
			}

			norm = fLastDelta.x * fLastDelta.x + fLastDelta.y * fLastDelta.y;
			if (norm > 0.01) {
				fLastDelta.x /= norm;
				fLastDelta.y /= norm;
			}

			float dot = delta.x * fLastDelta.x + delta.y * fLastDelta.y;

			if (dot < 0.0) {
				if (fCounter == 0) {
					BMessage * cancelMessage = new BMessage(kMsgCancel);
		 			fCancelRunner = new BMessageRunner(BMessenger(fTargetView),
		 				cancelMessage, fTimeThreshold, 1);
				}

				fCounter++;

				if (fCounter >= fCountThreshold) {
					BMessage shakeMessage(fMessageWhat);
					shakeMessage.AddUInt32("count", fCounter);
					BMessenger messenger(fTargetView);
					messenger.SendMessage(&shakeMessage);
				}
			}

			fLastDelta = fLowPass.Output();
			fLastPosition = position;

			return B_DISPATCH_MESSAGE;
		}

		case kMsgCancel:
			delete fCancelRunner;
			fCancelRunner = NULL;
			fCounter = 0;
			return B_SKIP_MESSAGE;

		default:
			break;
	}

	return B_DISPATCH_MESSAGE;
}


//	#pragma mark -


/** @brief Constructs a LowPassFilter with a fixed buffer size.
 *  @param size  Number of samples to average (window length).
 */
LowPassFilter::LowPassFilter(uint32 size)
	:
	fSize(size)
{
	fPoints = new BPoint[fSize];
}


/** @brief Destructor — frees the sample buffer. */
LowPassFilter::~LowPassFilter()
{
	delete [] fPoints;
}


/** @brief Feeds a new sample into the sliding window.
 *
 *  Shifts the FIFO buffer by one, subtracts the oldest sample from the
 *  running sum, and adds the new sample.
 *
 *  @param p  The new BPoint sample to insert.
 */
void
LowPassFilter::Input(const BPoint& p)
{
	// A fifo buffer that maintains a sum of its elements
	fSum -= fPoints[0];
	for (uint32 i = 0; i < fSize - 1; i++)
		fPoints[i] = fPoints[i + 1];
	fPoints[fSize - 1] = p;
	fSum += p;
}


/** @brief Returns the current averaged output of the filter.
 *  @return The arithmetic mean of all samples currently in the window.
 */
BPoint
LowPassFilter::Output() const
{
	return BPoint(fSum.x / (float) fSize, fSum.y / (float) fSize);
}
