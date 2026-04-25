/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2010, Haiku.
 * Original authors: Clemens Zeidler.
 */

/** @file WindowBehaviour.h
    @brief Abstract interface for window input handling: mouse drag, resize, and modifier tracking. */

#ifndef WINDOW_BEHAVIOUR_H
#define WINDOW_BEHAVIOUR_H


#include <Region.h>

#include "Decorator.h"


class BMessage;
class ClickTarget;
class Window;


/** @brief Pluggable input-handling policy for a window: receives mouse and
           modifier events from the desktop and decides whether they should
           drag, resize, or otherwise mutate the window state. */
class WindowBehaviour {
public:
								WindowBehaviour();
	virtual						~WindowBehaviour();

	virtual	bool				MouseDown(BMessage* message, BPoint where,
									int32 lastHitRegion, int32& clickCount,
									int32& _hitRegion) = 0;
	virtual	void				MouseUp(BMessage* message, BPoint where) = 0;
	virtual	void				MouseMoved(BMessage *message, BPoint where,
									bool isFake) = 0;

	virtual	void				ModifiersChanged(int32 modifiers);

			/** @brief Returns true while a drag gesture is in progress. */
			bool				IsDragging() const { return fIsDragging; }
			/** @brief Returns true while a resize gesture is in progress. */
			bool				IsResizing() const { return fIsResizing; }

protected:
	/*! The window is going to be moved by delta. This hook should be used to
	implement the magnetic screen border, i.e. alter the delta accordantly.
	@return true if delta has been modified. */
	virtual bool				AlterDeltaForSnap(Window* window, BPoint& delta,
									bigtime_t now);

protected:
			bool				fIsResizing : 1;
			bool				fIsDragging : 1;
};


#endif
