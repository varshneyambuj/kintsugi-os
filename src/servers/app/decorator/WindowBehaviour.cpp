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
 *   Copyright 2010, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Clemens Zeidler <haiku@clemens-zeidler.de>
 */


/**
 * @file WindowBehaviour.cpp
 * @brief Abstract base for per-window input handlers (drag, resize, modifier
 *        tracking) used by the app_server decorator subsystem.
 *
 * Subclasses implement MouseDown(), MouseUp(), and MouseMoved() to drive a
 * window's interactive behaviour. The base class supplies sensible defaults
 * for the modifier-changed and snap-delta hooks.
 */


#include "WindowBehaviour.h"


/**
 * @brief Constructs a WindowBehaviour with no active drag or resize gesture.
 */
WindowBehaviour::WindowBehaviour()
	:
	fIsResizing(false),
	fIsDragging(false)
{
}


/**
 * @brief Destroys the WindowBehaviour. Provided so subclasses can override.
 */
WindowBehaviour::~WindowBehaviour()
{
}


/**
 * @brief Default modifier-change hook; the base class ignores modifier changes.
 *
 * Subclasses such as DefaultWindowBehaviour override this to enter or leave
 * "manage window" mode based on the current modifier set.
 *
 * @param modifiers Bitmask of currently held modifier keys.
 */
void
WindowBehaviour::ModifiersChanged(int32 modifiers)
{
}


/**
 * @brief Default snap-delta hook; the base class never modifies the delta.
 *
 * @param window  The window being moved.
 * @param delta   Proposed move delta (in/out); unused at this level.
 * @param now     Current time, in microseconds, used by snap hysteresis.
 * @return Always false in the base class, indicating @a delta is unchanged.
 */
bool
WindowBehaviour::AlterDeltaForSnap(Window* window, BPoint& delta, bigtime_t now)
{
	return false;
}


/*!	\fn WindowBehaviour::MouseDown()
	@brief Handles a mouse-down message for the window.

	Note that values passed and returned for the hit regions are only meaningful
	to the WindowBehavior subclass, save for the value 0, which is refers to an
	invalid region.

	@param message The message.
	@param where The point where the mouse click happened.
	@param lastHitRegion The hit region of the previous click.
	@param clickCount The number of subsequent, no longer than double-click
		interval separated clicks that have happened so far. This number doesn't
		necessarily match the value in the message. It has already been
		pre-processed in order to avoid erroneous multi-clicks (e.g. when a
		different button has been used or a different window was targeted). This
		is an in-out variable. The method can reset the value to 1, if it
		doesn't want this event handled as a multi-click. Returning a different
		click hit region will also make the caller reset the click count.
	@param _hitRegion Set by the method to a value identifying the clicked
		decorator element. If not explicitly set, an invalid hit region (0) is
		assumed. Only needs to be set when returning \c true.
	@return \c true, if the event was a WindowBehaviour event and should be
		discarded.
*/
