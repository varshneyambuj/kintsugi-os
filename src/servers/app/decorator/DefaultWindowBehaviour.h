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
 * MIT License. Copyright 2001-2010, Haiku.
 * Original authors: DarkWyrm, Adi Oanca, Stephan Aßmus, Axel Dörfler,
 *                   Brecht Machiels, Clemens Zeidler, Ingo Weinhold.
 */

/** @file DefaultWindowBehaviour.h
    @brief Default mouse/keyboard input policy for a window: drag, resize,
           tab sliding, decorator-button presses, and modifier-driven manage mode. */

#ifndef DEFAULT_WINDOW_BEHAVIOUR_H
#define DEFAULT_WINDOW_BEHAVIOUR_H


#include "WindowBehaviour.h"

#include "Decorator.h"
#include "MagneticBorder.h"
#include "ServerCursor.h"

#include <AutoDeleter.h>


class Desktop;
class Window;


/** @brief WindowBehaviour implementation used by the default decorator. It
           drives a state-machine over mouse events to perform window dragging,
           resizing (incl. outline-resize and resize-by-modifier), tab sliding,
           button presses, and edge-snap via MagneticBorder. */
class DefaultWindowBehaviour : public WindowBehaviour {
public:
								DefaultWindowBehaviour(Window* window);
	virtual						~DefaultWindowBehaviour();

	virtual	bool				MouseDown(BMessage* message, BPoint where,
									int32 lastHitRegion, int32& clickCount,
									int32& _hitRegion);
	virtual	void				MouseUp(BMessage* message, BPoint where);
	virtual	void				MouseMoved(BMessage *message, BPoint where,
									bool isFake);

	virtual	void				ModifiersChanged(int32 modifiers);

protected:
	virtual bool				AlterDeltaForSnap(Window* window, BPoint& delta,
									bigtime_t now);
private:
			enum Action {
				ACTION_NONE,
				ACTION_ZOOM,
				ACTION_CLOSE,
				ACTION_MINIMIZE,
				ACTION_TAB,
				ACTION_DRAG,
				ACTION_SLIDE_TAB,
				ACTION_RESIZE,
				ACTION_RESIZE_BORDER
			};

			enum {
				// 1 for the "natural" resize border, -1 for the opposite, so
				// multiplying the movement delta by that value results in the
				// size change.
				LEFT	= -1,
				TOP		= -1,
				NONE	= 0,
				RIGHT	= 1,
				BOTTOM	= 1
			};

			struct State;
			struct MouseTrackingState;
			struct DragState;
			struct ResizeState;
			struct SlideTabState;
			struct ResizeBorderState;
			struct DecoratorButtonState;
			struct ManageWindowState;

			// to keep gcc 2 happy
			friend struct State;
			friend struct MouseTrackingState;
			friend struct DragState;
			friend struct ResizeState;
			friend struct SlideTabState;
			friend struct ResizeBorderState;
			friend struct DecoratorButtonState;
			friend struct ManageWindowState;

private:
			bool				_IsWindowModifier(int32 modifiers) const;
			Decorator::Region	_RegionFor(const BMessage* message,
									int32& tab) const;

			void				_SetBorderHighlights(int8 horizontal,
									int8 vertical, bool active);

			ServerCursor*		_ResizeCursorFor(int8 horizontal,
									int8 vertical);
			void				_SetResizeCursor(int8 horizontal,
									int8 vertical);
			void				_ResetResizeCursor();
	static	void				_ComputeResizeDirection(float x, float y,
									int8& _horizontal, int8& _vertical);

			void				_NextState(State* state);

protected:
			Window*				fWindow;
			Desktop*			fDesktop;
			ObjectDeleter<State>
								fState;
			int32				fLastModifiers;

			MagneticBorder		fMagneticBorder;
};


#endif	// DEFAULT_WINDOW_BEHAVIOUR_H
