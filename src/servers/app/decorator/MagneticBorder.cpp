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
 *   Copyright 2010-2011, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 *       Clemens Zeidler <haiku@clemens-zeidler.de>
 */


/**
 * @file MagneticBorder.cpp
 * @brief Edge-snap helper for window dragging.
 *
 * MagneticBorder examines a proposed move delta during a drag and, when the
 * resulting window frame would land within a small distance of any screen
 * edge, snaps the frame flush to that edge. Hysteresis based on the time of
 * the previous snap prevents a window from oscillating between snapped and
 * free movement when the cursor lingers near an edge.
 */


#include "MagneticBorder.h"

#include "Decorator.h"
#include "Window.h"
#include "Screen.h"


/**
 * @brief Constructs a MagneticBorder with no prior snap recorded.
 */
MagneticBorder::MagneticBorder()
	:
	fLastSnapTime(0)
{

}


/**
 * @brief Convenience overload that snaps a window's frame, including its
 *        decorator footprint, against its current screen.
 *
 * @param window The window being dragged.
 * @param delta  Proposed move delta; modified in place when snapping occurs.
 * @param now    Current time in microseconds, used to drive snap hysteresis.
 * @return true if @a delta was altered to snap to a screen edge.
 */
bool
MagneticBorder::AlterDeltaForSnap(Window* window, BPoint& delta, bigtime_t now)
{
	BRect frame = window->Frame();
	Decorator* decorator = window->Decorator();
	if (decorator)
		frame = decorator->GetFootprint().Frame();

	return AlterDeltaForSnap(window->Screen(), frame, delta, now);
}


/**
 * @brief Snaps @a frame flush against the edges of @a screen when the proposed
 *        move would bring it within the snap threshold.
 *
 * The horizontal and vertical axes are evaluated independently. If a snap
 * fires within the brief snapping window after a previous snap, the call is
 * suppressed so the user can move the window away from the edge without
 * fighting the magnetism.
 *
 * @param screen Screen whose frame defines the edges to snap against.
 * @param frame  Window/decorator frame in screen coordinates (not modified).
 * @param delta  Proposed move delta; modified in place when snapping occurs.
 * @param now    Current time in microseconds, used to drive snap hysteresis.
 * @return true if @a delta was altered to snap to a screen edge.
 *
 * @todo Use the area not covered by the Deskbar instead of the full screen.
 */
bool
MagneticBorder::AlterDeltaForSnap(const Screen* screen, BRect& frame,
	BPoint& delta, bigtime_t now)
{
	// Alter the delta (which is a proposed offset used while dragging a
	// window) so that the frame of the window 'snaps' to the edges of the
	// screen.

	const bigtime_t kSnappingDuration = 1500000LL;
	const bigtime_t kSnappingPause = 3000000LL;
	const float kSnapDistance = 8.0f;

	if (now - fLastSnapTime > kSnappingDuration
		&& now - fLastSnapTime < kSnappingPause) {
		// Maintain a pause between snapping.
		return false;
	}

	// TODO: Perhaps obtain the usable area (not covered by the Deskbar)?
	BRect screenFrame = screen->Frame();
	BRect originalFrame = frame;
	frame.OffsetBy(delta);

	float leftDist = fabs(frame.left - screenFrame.left);
	float topDist = fabs(frame.top - screenFrame.top);
	float rightDist = fabs(frame.right - screenFrame.right);
	float bottomDist = fabs(frame.bottom - screenFrame.bottom);

	bool snapped = false;
	if (leftDist < kSnapDistance || rightDist < kSnapDistance) {
		snapped = true;
		if (leftDist < rightDist)
			delta.x = screenFrame.left - originalFrame.left;
		else
			delta.x = screenFrame.right - originalFrame.right;
	}

	if (topDist < kSnapDistance || bottomDist < kSnapDistance) {
		snapped = true;
		if (topDist < bottomDist)
			delta.y = screenFrame.top - originalFrame.top;
		else
			delta.y = screenFrame.bottom - originalFrame.bottom;
	}
	if (snapped && now - fLastSnapTime > kSnappingPause)
		fLastSnapTime = now;

	return snapped;
}
