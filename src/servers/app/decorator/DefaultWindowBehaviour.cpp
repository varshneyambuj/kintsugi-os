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
 *   Copyright 2001-2020, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       DarkWyrm, bpmagic@columbus.rr.com
 *       Adi Oanca, adioanca@gmail.com
 *       Stephan Aßmus, superstippi@gmx.de
 *       Axel Dörfler, axeld@pinc-software.de
 *       Brecht Machiels, brecht@mos6581.org
 *       Clemens Zeidler, haiku@clemens-zeidler.de
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 *       Tri-Edge AI
 *       Jacob Secunda, secundja@gmail.com
 */


/**
 * @file DefaultWindowBehaviour.cpp
 * @brief Default mouse and keyboard input policy for app_server windows.
 *
 * Implements a state machine over MouseDown / MouseUp / MouseMoved /
 * ModifiersChanged events. Each State subtype handles one mode of
 * interaction: dragging a window, resizing it from a corner, sliding a tab,
 * resizing along a border, pressing a decorator button, or hovering with
 * the window-management modifier held. Edge snapping is delegated to
 * MagneticBorder.
 *
 * @see WindowBehaviour, MagneticBorder, Decorator
 */


#include "DefaultWindowBehaviour.h"

#include <math.h>

#include <PortLink.h>
#include <WindowPrivate.h>

#include "AppServer.h"
#include "ClickTarget.h"
#include "Desktop.h"
#include "DefaultDecorator.h"
#include "DrawingEngine.h"
#include "Window.h"


//#define DEBUG_WINDOW_CLICK
#ifdef DEBUG_WINDOW_CLICK
#	define STRACE_CLICK(x) printf x
#else
#	define STRACE_CLICK(x) ;
#endif


/** @brief Maximum time, in microseconds, between mouse-down and mouse-up
           for a click to still count as a window-activation gesture. */
// The span between mouse down
static const bigtime_t kWindowActivationTimeout = 500000LL;


// #pragma mark - State


/** @brief Base class for the input state machine; each concrete state
           implements a single interaction mode (drag, resize, slide tab,
           button-press, manage). */
struct DefaultWindowBehaviour::State {
	/** @brief Captures the behaviour, window, and desktop the state operates on. */
	State(DefaultWindowBehaviour& behavior)
		:
		fBehavior(behavior),
		fWindow(behavior.fWindow),
		fDesktop(behavior.fDesktop)
	{
	}

	virtual ~State()
	{
	}

	/** @brief Hook called once when the state is installed. */
	virtual void EnterState(State* previousState)
	{
	}

	/** @brief Hook called once before the state is replaced. */
	virtual void ExitState(State* nextState)
	{
	}

	/** @brief Default mouse-down handler; absorbs the event by default. */
	virtual bool MouseDown(BMessage* message, BPoint where, bool& _unhandled)
	{
		return true;
	}

	/** @brief Default mouse-up handler (no-op). */
	virtual void MouseUp(BMessage* message, BPoint where)
	{
	}

	/** @brief Default mouse-moved handler (no-op). */
	virtual void MouseMoved(BMessage* message, BPoint where, bool isFake)
	{
	}

	/** @brief Default modifier-change handler (no-op). */
	virtual void ModifiersChanged(BPoint where, int32 modifiers)
	{
	}

protected:
	DefaultWindowBehaviour&	fBehavior;
	Window*					fWindow;
	Desktop*				fDesktop;
};


// #pragma mark - MouseTrackingState


/** @brief Common state for any interaction that follows a held mouse button:
           rate-limits move events, accumulates drag distance to suppress
           accidental double-clicks, and triggers an action on mouse up. */
struct DefaultWindowBehaviour::MouseTrackingState : State {
	/** @brief Constructs the tracking state for one button.
	    @param behavior      Owning behaviour.
	    @param where         Initial mouse position.
	    @param windowActionOnMouseUp  Run MouseUpWindowAction() if released
	                                  without significant movement.
	    @param minimizeCheckOnMouseUp Treat a held click on the title bar as
	                                  a minimize gesture on release.
	    @param mouseButton   Mouse button being tracked. */
	MouseTrackingState(DefaultWindowBehaviour& behavior, BPoint where,
		bool windowActionOnMouseUp, bool minimizeCheckOnMouseUp,
		int32 mouseButton = B_PRIMARY_MOUSE_BUTTON)
		:
		State(behavior),
		fMouseButton(mouseButton),
		fWindowActionOnMouseUp(windowActionOnMouseUp),
		fMinimizeCheckOnMouseUp(minimizeCheckOnMouseUp),
		fLastMousePosition(where),
		fMouseMoveDistance(0),
		fLastMoveTime(system_time())
	{
	}

	/** @brief Handles release of the tracked mouse button: optionally
	           minimizes the window or invokes MouseUpWindowAction(). */
	virtual void MouseUp(BMessage* message, BPoint where)
	{
		// ignore, if it's not our mouse button
		int32 buttons = message->FindInt32("buttons");
		if ((buttons & fMouseButton) != 0)
			return;

		if (fMinimizeCheckOnMouseUp) {
			// If the modifiers haven't changed in the meantime and not too
			// much time has elapsed, we're supposed to minimize the window.
			fMinimizeCheckOnMouseUp = false;
			if (message->FindInt32("modifiers") == fBehavior.fLastModifiers
				&& (fWindow->Flags() & B_NOT_MINIMIZABLE) == 0
				&& system_time() - fLastMoveTime < kWindowActivationTimeout) {
				fWindow->ServerWindow()->NotifyMinimize(true);
			}
		}

		// Perform the window action in case the mouse was not moved.
		if (fWindowActionOnMouseUp) {
			// There is a time window for this feature, i.e. click and press
			// too long, nothing will happen.
			if (system_time() - fLastMoveTime < kWindowActivationTimeout)
				MouseUpWindowAction();
		}

		fBehavior._NextState(NULL);
	}

	/** @brief Rate-limits mouse-moved events, suppresses tiny accidental
	           jitters, and forwards the resulting delta to MouseMovedAction(). */
	virtual void MouseMoved(BMessage* message, BPoint where, bool isFake)
	{
		// Limit the rate at which "mouse moved" events are handled that move
		// or resize the window. At the moment this affects also tab sliding,
		// but 1/75 s is a pretty fine granularity anyway, so don't bother.
		bigtime_t now = system_time();
		if (now - fLastMoveTime < 13333) {
			// TODO: add a "timed event" to query for
			// the then current mouse position
			return;
		}
		if (fWindowActionOnMouseUp || fMinimizeCheckOnMouseUp) {
			if (now - fLastMoveTime >= kWindowActivationTimeout) {
				// This click is too long already for window activation/
				// minimizing.
				fWindowActionOnMouseUp = false;
				fMinimizeCheckOnMouseUp = false;
				fLastMoveTime = now;
			}
		} else
			fLastMoveTime = now;

		BPoint delta = where - fLastMousePosition;
		// NOTE: "delta" is later used to change fLastMousePosition.
		// If for some reason no change should take effect, delta
		// is to be set to (0, 0) so that fLastMousePosition is not
		// adjusted. This way the relative mouse position to the
		// item being changed (border during resizing, tab during
		// sliding...) stays fixed when the mouse is moved so that
		// changes are taking effect again.

		// If the window was moved enough, it doesn't come to
		// the front in FFM mode when the mouse is released.
		if (fWindowActionOnMouseUp || fMinimizeCheckOnMouseUp) {
			fMouseMoveDistance += delta.x * delta.x + delta.y * delta.y;
			if (fMouseMoveDistance > 16.0f) {
				fWindowActionOnMouseUp = false;
				fMinimizeCheckOnMouseUp = false;
			} else
				delta = B_ORIGIN;
		}

		// perform the action (this also updates the delta)
		MouseMovedAction(delta, now);

		// set the new mouse position
		fLastMousePosition += delta;
	}

	/** @brief Per-frame action invoked with the rate-limited delta;
	           subclasses override to move, resize, or slide. */
	virtual void MouseMovedAction(BPoint& delta, bigtime_t now)
	{
	}

	/** @brief Action triggered on mouse-up without movement; default is to
	           activate the window. */
	virtual void MouseUpWindowAction()
	{
		// default is window activation
		fDesktop->ActivateWindow(fWindow);
	}

protected:
	int32				fMouseButton;
	bool				fWindowActionOnMouseUp : 1;
	bool				fMinimizeCheckOnMouseUp : 1;

	BPoint				fLastMousePosition;
	float				fMouseMoveDistance;
	bigtime_t			fLastMoveTime;
};


// #pragma mark - DragState


/** @brief State engaged when the user drags a window by its title bar or a
           border with the primary mouse button held. */
struct DefaultWindowBehaviour::DragState : MouseTrackingState {
	/** @brief Constructs a drag state.
	    @param behavior              Owning behaviour.
	    @param where                 Initial mouse position.
	    @param activateOnMouseUp     true to activate on a click without movement.
	    @param minimizeCheckOnMouseUp true to allow a quick double-click to minimize. */
	DragState(DefaultWindowBehaviour& behavior, BPoint where,
		bool activateOnMouseUp, bool minimizeCheckOnMouseUp)
		:
		MouseTrackingState(behavior, where, activateOnMouseUp,
			minimizeCheckOnMouseUp)
	{
	}

	/** @brief Allows a right click during a drag to send the window behind
	           or activate the back window. */
	virtual bool MouseDown(BMessage* message, BPoint where, bool& _unhandled)
	{
		// right-click while dragging shall bring the window to front
		int32 buttons = message->FindInt32("buttons");
		if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
			if (fWindow == fDesktop->BackWindow())
				fDesktop->ActivateWindow(fWindow);
			else
				fDesktop->SendWindowBehind(fWindow);
			return true;
		}

		return MouseTrackingState::MouseDown(message, where, _unhandled);
	}

	/** @brief Moves the window by the rate-limited delta unless B_NOT_MOVABLE,
	           applying edge-snap via the behaviour's MagneticBorder. */
	virtual void MouseMovedAction(BPoint& delta, bigtime_t now)
	{
		if ((fWindow->Flags() & B_NOT_MOVABLE) == 0) {
			BPoint oldLeftTop = fWindow->Frame().LeftTop();

			fBehavior.AlterDeltaForSnap(fWindow, delta, now);
			fDesktop->MoveWindowBy(fWindow, delta.x, delta.y);

			// constrain delta to true change in position
			delta = fWindow->Frame().LeftTop() - oldLeftTop;
		} else
			delta = BPoint(0, 0);
	}
};


// #pragma mark - ResizeState


/** @brief State engaged when the user resizes a window from its bottom-right
           corner using the primary mouse button. */
struct DefaultWindowBehaviour::ResizeState : MouseTrackingState {
	/** @brief Cumulative outline-resize delta applied on mouse-up when
	           B_OUTLINE_RESIZE is enabled. */
	BPoint fDelta;

	/** @brief Constructs a corner-resize state. */
	ResizeState(DefaultWindowBehaviour& behavior, BPoint where,
		bool activateOnMouseUp, bool minimizeCheckOnMouseUp)
		:
		MouseTrackingState(behavior, where, activateOnMouseUp, minimizeCheckOnMouseUp)
	{
		fDelta = BPoint(0, 0);
	}

	/** @brief Hook called on entry; nothing to set up for corner resize. */
	virtual void EnterState(State* prevState)
	{
	}

	/** @brief Commits a pending outline-resize delta on exit, if any. */
	virtual void ExitState(State* nextState)
	{
		if ((fWindow->Flags() & B_OUTLINE_RESIZE) != 0) {
			fDesktop->SetWindowOutlinesDelta(fWindow, BPoint(0, 0));
			fDesktop->ResizeWindowBy(fWindow, fDelta.x, fDelta.y);
		}
	}

	/** @brief Resizes the window by @a delta, honouring B_NOT_*_RESIZABLE
	           flags and the live-vs-outline resize mode. */
	virtual void MouseMovedAction(BPoint& delta, bigtime_t now)
	{
		if ((fWindow->Flags() & B_NOT_RESIZABLE) == 0) {
			if ((fWindow->Flags() & B_NOT_V_RESIZABLE) != 0)
				delta.y = 0;
			if ((fWindow->Flags() & B_NOT_H_RESIZABLE) != 0)
				delta.x = 0;

			BPoint oldRightBottom = fWindow->Frame().RightBottom();

			if ((fWindow->Flags() & B_OUTLINE_RESIZE) != 0) {
				fDelta = delta;
				fDesktop->SetWindowOutlinesDelta(fWindow, delta);
			} else
				fDesktop->ResizeWindowBy(fWindow, delta.x, delta.y);

			// constrain delta to true change in size
			delta = fWindow->Frame().RightBottom() - oldRightBottom;
		} else
			delta = BPoint(0, 0);
	}
};


// #pragma mark - SlideTabState


/** @brief State engaged when the user slides a tab horizontally with
           Shift+left-drag, including reordering inside multi-tab stacks. */
struct DefaultWindowBehaviour::SlideTabState : MouseTrackingState {
	/** @brief Constructs the slide-tab state. */
	SlideTabState(DefaultWindowBehaviour& behavior, BPoint where)
		:
		MouseTrackingState(behavior, where, false, false)
	{
	}

	/** @brief Commits the final tab location when the slide ends. */
	virtual
	~SlideTabState()
	{
		fDesktop->SetWindowTabLocation(fWindow, fWindow->TabLocation(), false);
	}

	/** @brief Updates the tab offset by the rate-limited horizontal delta;
	           also evaluates whether the dragged tab should swap positions
	           with a neighbour in a stacked window. */
	virtual void MouseMovedAction(BPoint& delta, bigtime_t now)
	{
		float location = fWindow->TabLocation();
		// TODO: change to [0:1]
		location += delta.x;
		AdjustMultiTabLocation(location, true);
		if (fDesktop->SetWindowTabLocation(fWindow, location, true))
			delta.y = 0;
		else
			delta = BPoint(0, 0);
	}

	/** @brief In a stacked window, reorders the dragged tab past the next or
	           previous neighbour when the drag distance exceeds the
	           neighbour's half-width threshold.
	    @note Only handles continuous shifts; rapid jumps may be missed. */
	void AdjustMultiTabLocation(float location, bool isShifting)
	{
		::Decorator* decorator = fWindow->Decorator();
		if (decorator == NULL || decorator->CountTabs() <= 1)
			return;

		// TODO does not work for none continuous shifts
		int32 windowIndex = fWindow->PositionInStack();
		DefaultDecorator::Tab*	movingTab = static_cast<DefaultDecorator::Tab*>(
			decorator->TabAt(windowIndex));
		int32 neighbourIndex = windowIndex;
		if (movingTab->tabOffset > location)
			neighbourIndex--;
		else
			neighbourIndex++;

		DefaultDecorator::Tab* neighbourTab
			= static_cast<DefaultDecorator::Tab*>(decorator->TabAt(
				neighbourIndex));
		if (neighbourTab == NULL)
			return;

		if (movingTab->tabOffset > location) {
			if (location > neighbourTab->tabOffset
					+ neighbourTab->tabRect.Width() / 2) {
				return;
			}
		} else {
			if (location + movingTab->tabRect.Width() < neighbourTab->tabOffset
					+ neighbourTab->tabRect.Width() / 2) {
				return;
			}
		}

		fWindow->MoveToStackPosition(neighbourIndex, isShifting);
	}
};


// #pragma mark - ResizeBorderState


/** @brief State engaged when the user resizes a window by clicking and
           dragging on a border or corner with the secondary mouse button,
           or on the manage-window mode borders. */
struct DefaultWindowBehaviour::ResizeBorderState : MouseTrackingState {
	/** @brief Cumulative outline-resize delta committed on exit when
	           B_OUTLINE_RESIZE is enabled. */
	BPoint fDelta;

	/** @brief Builds a border-resize state from a hit-tested decorator
	           region, mapping the region to a horizontal/vertical sign. */
	ResizeBorderState(DefaultWindowBehaviour& behavior, BPoint where,
		Decorator::Region region)
		:
		MouseTrackingState(behavior, where, true, false,
			B_SECONDARY_MOUSE_BUTTON),
		fHorizontal(NONE),
		fVertical(NONE)
	{
		switch (region) {
			case Decorator::REGION_TAB:
				// TODO: Handle like the border it is attached to (top/left)?
				break;
			case Decorator::REGION_LEFT_BORDER:
				fHorizontal = LEFT;
				break;
			case Decorator::REGION_RIGHT_BORDER:
				fHorizontal = RIGHT;
				break;
			case Decorator::REGION_TOP_BORDER:
				fVertical = TOP;
				break;
			case Decorator::REGION_BOTTOM_BORDER:
				fVertical = BOTTOM;
				break;
			case Decorator::REGION_LEFT_TOP_CORNER:
				fHorizontal = LEFT;
				fVertical = TOP;
				break;
			case Decorator::REGION_LEFT_BOTTOM_CORNER:
				fHorizontal = LEFT;
				fVertical = BOTTOM;
				break;
			case Decorator::REGION_RIGHT_TOP_CORNER:
				fHorizontal = RIGHT;
				fVertical = TOP;
				break;
			case Decorator::REGION_RIGHT_BOTTOM_CORNER:
				fHorizontal = RIGHT;
				fVertical = BOTTOM;
				break;
			default:
				break;
		}

		fDelta = B_ORIGIN;
	}

	/** @brief Builds a border-resize state from explicit horizontal and
	           vertical signs (used when entering from manage-window mode). */
	ResizeBorderState(DefaultWindowBehaviour& behavior, BPoint where,
		int8 horizontal, int8 vertical)
		:
		MouseTrackingState(behavior, where, true, false,
			B_SECONDARY_MOUSE_BUTTON),
		fHorizontal(horizontal),
		fVertical(vertical)
	{
		fDelta = B_ORIGIN;
	}

	/** @brief Suppresses non-resizable axes and installs the appropriate
	           resize cursor when the state becomes active. */
	virtual void EnterState(State* previousState)
	{
		if ((fWindow->Flags() & B_NOT_RESIZABLE) != 0)
			fHorizontal = fVertical = NONE;
		else {
			if ((fWindow->Flags() & B_NOT_H_RESIZABLE) != 0)
				fHorizontal = NONE;

			if ((fWindow->Flags() & B_NOT_V_RESIZABLE) != 0)
				fVertical = NONE;
		}
		fBehavior._SetResizeCursor(fHorizontal, fVertical);
	}

	/** @brief Restores the cursor and commits any pending outline resize. */
	virtual void ExitState(State* nextState)
	{
		fBehavior._ResetResizeCursor();

		if (fWindow->Flags() & B_OUTLINE_RESIZE) {
			fDesktop->SetWindowOutlinesDelta(fWindow, B_ORIGIN);
			fDesktop->ResizeWindowBy(fWindow, fDelta.x, fDelta.y);
		}
	}

	/** @brief Resizes the window along the active axes, then translates it
	           when resizing from a left or top border so the opposite edge
	           appears anchored. */
	virtual void MouseMovedAction(BPoint& delta, bigtime_t now)
	{
		if (fHorizontal == NONE)
			delta.x = 0;
		if (fVertical == NONE)
			delta.y = 0;

		if (delta.x == 0 && delta.y == 0)
			return;

		// Resize first -- due to the window size limits this is not unlikely
		// to turn out differently from what we request.
		BPoint oldRightBottom = fWindow->Frame().RightBottom();

		if (fWindow->Flags() & B_OUTLINE_RESIZE) {
			fDelta = delta;
			fDesktop->SetWindowOutlinesDelta(fWindow, BPoint(
				delta.x * fHorizontal, delta.y * fVertical));
		} else {
			fDesktop->ResizeWindowBy(fWindow, delta.x * fHorizontal,
				delta.y * fVertical);
		}

		// constrain delta to true change in size
		delta = fWindow->Frame().RightBottom() - oldRightBottom;
		delta.x *= fHorizontal;
		delta.y *= fVertical;

		// see, if we have to move, too
		float moveX = fHorizontal == LEFT ? delta.x : 0;
		float moveY = fVertical == TOP ? delta.y : 0;

		if (moveX != 0 || moveY != 0)
			fDesktop->MoveWindowBy(fWindow, moveX, moveY);
	}

	/** @brief Sends the window behind on a click without movement (so a
	           right-click on a border without a drag re-orders the stack). */
	virtual void MouseUpWindowAction()
	{
		fDesktop->SendWindowBehind(fWindow);
	}

private:
	int8	fHorizontal;
	int8	fVertical;
};


// #pragma mark - DecoratorButtonState


/** @brief State engaged while the user is holding a decorator button
           (close, zoom, or minimize); paints pressed/unpressed state and
           triggers the action only if the release lands on the same button. */
struct DefaultWindowBehaviour::DecoratorButtonState : State {
	/** @brief Constructs the button state for a given tab and button region. */
	DecoratorButtonState(DefaultWindowBehaviour& behavior,
		int32 tab, Decorator::Region button)
		:
		State(behavior),
		fTab(tab),
		fButton(button)
	{
	}

	/** @brief Paints the button as pressed when the state is installed. */
	virtual void EnterState(State* previousState)
	{
		_RedrawDecorator(NULL);
	}

	/** @brief Releases the button. If the release is over the same button,
	           the corresponding window action (close/zoom/minimize) fires. */
	virtual void MouseUp(BMessage* message, BPoint where)
	{
		// ignore, if it's not the primary mouse button
		int32 buttons = message->FindInt32("buttons");
		if ((buttons & B_PRIMARY_MOUSE_BUTTON) != 0)
			return;

		// redraw the decorator
		if (Decorator* decorator = fWindow->Decorator()) {
			BRegion* visibleBorder = fWindow->RegionPool()->GetRegion();
			fWindow->GetBorderRegion(visibleBorder);
			visibleBorder->IntersectWith(&fWindow->VisibleRegion());

			DrawingEngine* engine = decorator->GetDrawingEngine();
			engine->LockParallelAccess();
			engine->ConstrainClippingRegion(visibleBorder);

			int32 tab;
			switch (fButton) {
				case Decorator::REGION_CLOSE_BUTTON:
					decorator->SetClose(fTab, false);
					if (fBehavior._RegionFor(message, tab) == fButton)
						fWindow->ServerWindow()->NotifyQuitRequested();
					break;

				case Decorator::REGION_ZOOM_BUTTON:
					decorator->SetZoom(fTab, false);
					if (fBehavior._RegionFor(message, tab) == fButton)
						fWindow->ServerWindow()->NotifyZoom();
					break;

				case Decorator::REGION_MINIMIZE_BUTTON:
					decorator->SetMinimize(fTab, false);
					if (fBehavior._RegionFor(message, tab) == fButton)
						fWindow->ServerWindow()->NotifyMinimize(true);
					break;

				default:
					break;
			}

			engine->UnlockParallelAccess();

			fWindow->RegionPool()->Recycle(visibleBorder);
		}

		fBehavior._NextState(NULL);
	}

	/** @brief Tracks the cursor and toggles the button's pressed appearance
	           depending on whether the cursor is still over it. */
	virtual void MouseMoved(BMessage* message, BPoint where, bool isFake)
	{
		_RedrawDecorator(message);
	}

private:
	/** @brief Repaints the button in pressed state when the cursor (per
	           @a message) is still over it; otherwise repaints unpressed. */
	void _RedrawDecorator(const BMessage* message)
	{
		if (Decorator* decorator = fWindow->Decorator()) {
			BRegion* visibleBorder = fWindow->RegionPool()->GetRegion();
			fWindow->GetBorderRegion(visibleBorder);
			visibleBorder->IntersectWith(&fWindow->VisibleRegion());

			DrawingEngine* engine = decorator->GetDrawingEngine();
			engine->LockParallelAccess();
			engine->ConstrainClippingRegion(visibleBorder);

			int32 tab;
			Decorator::Region hitRegion = message != NULL
				? fBehavior._RegionFor(message, tab) : fButton;

			switch (fButton) {
				case Decorator::REGION_CLOSE_BUTTON:
					decorator->SetClose(fTab, hitRegion == fButton);
					break;

				case Decorator::REGION_ZOOM_BUTTON:
					decorator->SetZoom(fTab, hitRegion == fButton);
					break;

				case Decorator::REGION_MINIMIZE_BUTTON:
					decorator->SetMinimize(fTab, hitRegion == fButton);
					break;

				default:
					break;
			}

			engine->UnlockParallelAccess();
			fWindow->RegionPool()->Recycle(visibleBorder);
		}
	}

protected:
	int32				fTab;
	Decorator::Region	fButton;
};


// #pragma mark - ManageWindowState


/** @brief State engaged while the window-management modifier keys are held;
           highlights the border closest to the cursor so a right-click can
           start a resize from it. */
struct DefaultWindowBehaviour::ManageWindowState : State {
	/** @brief Constructs the manage state with the cursor's current position. */
	ManageWindowState(DefaultWindowBehaviour& behavior, BPoint where)
		:
		State(behavior),
		fLastMousePosition(where),
		fHorizontal(NONE),
		fVertical(NONE)
	{
	}

	/** @brief Paints the initial border highlight for the cursor position. */
	virtual void EnterState(State* previousState)
	{
		_UpdateBorders(fLastMousePosition);
	}

	/** @brief Clears the border highlight when the state is replaced. */
	virtual void ExitState(State* nextState)
	{
		fBehavior._SetBorderHighlights(fHorizontal, fVertical, false);
	}

	/** @brief Switches into ResizeBorderState when the secondary mouse
	           button is pressed. */
	virtual bool MouseDown(BMessage* message, BPoint where, bool& _unhandled)
	{
		// We're only interested if the secondary mouse button was pressed,
		// otherwise let the caller handle the event.
		int32 buttons = message->FindInt32("buttons");
		if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0) {
			_unhandled = true;
			return true;
		}

		fBehavior._NextState(new (std::nothrow) ResizeBorderState(fBehavior,
			where, fHorizontal, fVertical));
		return true;
	}

	/** @brief Updates the highlighted border as the cursor moves; leaves
	           the state when the cursor exits the window. */
	virtual void MouseMoved(BMessage* message, BPoint where, bool isFake)
	{
		// If the mouse is still over our window, update the borders. Otherwise
		// leave the state.
		if (fDesktop->WindowAt(where) == fWindow) {
			fLastMousePosition = where;
			_UpdateBorders(fLastMousePosition);
		} else
			fBehavior._NextState(NULL);
	}

	/** @brief Leaves the manage state when the user releases the modifier keys. */
	virtual void ModifiersChanged(BPoint where, int32 modifiers)
	{
		if (!fBehavior._IsWindowModifier(modifiers))
			fBehavior._NextState(NULL);
	}

private:
	/** @brief Computes which border the cursor is closest to (taking the
	           window's aspect ratio into account) and refreshes the
	           highlight accordingly. */
	void _UpdateBorders(BPoint where)
	{
		if ((fWindow->Flags() & B_NOT_RESIZABLE) != 0)
			return;

		// Compute the window center relative location of where. We divide by
		// the width respective the height, so we compensate for the window's
		// aspect ratio.
		BRect frame(fWindow->Frame());
		if (frame.Width() + 1 == 0 || frame.Height() + 1 == 0)
			return;

		float x = (where.x - (frame.left + frame.right) / 2)
			/ (frame.Width() + 1);
		float y = (where.y - (frame.top + frame.bottom) / 2)
			/ (frame.Height() + 1);

		// compute the resize direction
		int8 horizontal;
		int8 vertical;
		_ComputeResizeDirection(x, y, horizontal, vertical);

		if ((fWindow->Flags() & B_NOT_H_RESIZABLE) != 0)
			horizontal = NONE;
		if ((fWindow->Flags() & B_NOT_V_RESIZABLE) != 0)
			vertical = NONE;

		// update the highlight, if necessary
		if (horizontal != fHorizontal || vertical != fVertical) {
			fBehavior._SetBorderHighlights(fHorizontal, fVertical, false);
			fHorizontal = horizontal;
			fVertical = vertical;
			fBehavior._SetBorderHighlights(fHorizontal, fVertical, true);
		}
	}

private:
	BPoint	fLastMousePosition;
	int8	fHorizontal;
	int8	fVertical;
};


// #pragma mark - DefaultWindowBehaviour


/**
 * @brief Constructs a DefaultWindowBehaviour bound to a single window.
 *
 * @param window Window the behaviour will manage.
 */
DefaultWindowBehaviour::DefaultWindowBehaviour(Window* window)
	:
	fWindow(window),
	fDesktop(window->Desktop()),
	fLastModifiers(0)
{
}


/**
 * @brief Destroys the behaviour. The active state, if any, is released by
 *        the ObjectDeleter holding fState.
 */
DefaultWindowBehaviour::~DefaultWindowBehaviour()
{
}


/**
 * @brief Routes a mouse-down event into the state machine.
 *
 * If a state is active and consumes the event, the call returns immediately.
 * Otherwise the click is hit-tested against the decorator and translated
 * into a window action (close/zoom/minimize, drag, resize, slide tab,
 * resize-border). Single-button mice with Control held emulate a right-click.
 *
 * @param message      Original B_MOUSE_DOWN message.
 * @param where        Click position in screen coordinates.
 * @param lastHitRegion Hit region of the previous click, used for click-count
 *                     reset.
 * @param clickCount   In/out click counter; reset to 1 if the region changed.
 * @param _hitRegion   Out-parameter set to the hit Decorator::Region.
 * @return true if the event was handled by the decorator/behaviour, false
 *         to let it pass through to the window contents.
 */
bool
DefaultWindowBehaviour::MouseDown(BMessage* message, BPoint where,
	int32 lastHitRegion, int32& clickCount, int32& _hitRegion)
{
	fLastModifiers = message->FindInt32("modifiers");
	int32 buttons = message->FindInt32("buttons");

	int32 numButtons;
	if (get_mouse_type(&numButtons) == B_OK) {
		switch (numButtons) {
			case 1:
				// 1 button mouse
				if ((fLastModifiers & B_CONTROL_KEY) != 0) {
					// emulate right click by holding control
					buttons = B_SECONDARY_MOUSE_BUTTON;
					message->ReplaceInt32("buttons", buttons);
				}
				break;

			case 2:
				// TODO: 2 button mouse, pressing both buttons simultaneously
				// emulates middle click

			default:
				break;
		}
	}

	// if a state is active, let it do the job
	if (fState.IsSet()) {
		bool unhandled = false;
		bool result = fState->MouseDown(message, where, unhandled);
		if (!unhandled)
			return result;
	}

	// No state active yet, or it wants us to handle the event -- determine the
	// click region and decide what to do.

	Decorator* decorator = fWindow->Decorator();

	Decorator::Region hitRegion = Decorator::REGION_NONE;
	int32 tab = -1;
	Action action = ACTION_NONE;

	bool inBorderRegion = false;
	if (decorator != NULL)
		inBorderRegion = decorator->GetFootprint().Contains(where);

	bool windowModifier = _IsWindowModifier(fLastModifiers);

	if (windowModifier || inBorderRegion) {
		// click on the window decorator or we have the window modifier keys
		// held

		// get the functional hit region
		if (windowModifier) {
			// click with window modifier keys -- let the whole window behave
			// like the border
			hitRegion = Decorator::REGION_LEFT_BORDER;
		} else {
			// click on the decorator -- get the exact region
			hitRegion = _RegionFor(message, tab);
		}

		// translate the region into an action
		uint32 flags = fWindow->Flags();

		if ((buttons & B_PRIMARY_MOUSE_BUTTON) != 0) {
			// left mouse button
			switch (hitRegion) {
				case Decorator::REGION_TAB: {
					// tab sliding in any case if either shift key is held down
					// except sliding up-down by moving mouse left-right would
					// look strange
					if ((fLastModifiers & B_SHIFT_KEY) != 0
						&& fWindow->Look() != kLeftTitledWindowLook) {
						action = ACTION_SLIDE_TAB;
						break;
					}
					action = ACTION_DRAG;
					break;
				}

				case Decorator::REGION_LEFT_BORDER:
				case Decorator::REGION_RIGHT_BORDER:
				case Decorator::REGION_TOP_BORDER:
				case Decorator::REGION_BOTTOM_BORDER:
					action = ACTION_DRAG;
					break;

				case Decorator::REGION_CLOSE_BUTTON:
					action = (flags & B_NOT_CLOSABLE) == 0
						? ACTION_CLOSE : ACTION_DRAG;
					break;

				case Decorator::REGION_ZOOM_BUTTON:
					action = (flags & B_NOT_ZOOMABLE) == 0
						? ACTION_ZOOM : ACTION_DRAG;
					break;

				case Decorator::REGION_MINIMIZE_BUTTON:
					action = (flags & B_NOT_MINIMIZABLE) == 0
						? ACTION_MINIMIZE : ACTION_DRAG;
					break;

				case Decorator::REGION_LEFT_TOP_CORNER:
				case Decorator::REGION_LEFT_BOTTOM_CORNER:
				case Decorator::REGION_RIGHT_TOP_CORNER:
					// TODO: Handle correctly!
					action = ACTION_DRAG;
					break;

				case Decorator::REGION_RIGHT_BOTTOM_CORNER:
					action = (flags & B_NOT_RESIZABLE) == 0
						? ACTION_RESIZE : ACTION_DRAG;
					break;

				default:
					break;
			}
		} else if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
			// right mouse button
			switch (hitRegion) {
				case Decorator::REGION_TAB:
				case Decorator::REGION_LEFT_BORDER:
				case Decorator::REGION_RIGHT_BORDER:
				case Decorator::REGION_TOP_BORDER:
				case Decorator::REGION_BOTTOM_BORDER:
				case Decorator::REGION_CLOSE_BUTTON:
				case Decorator::REGION_ZOOM_BUTTON:
				case Decorator::REGION_MINIMIZE_BUTTON:
				case Decorator::REGION_LEFT_TOP_CORNER:
				case Decorator::REGION_LEFT_BOTTOM_CORNER:
				case Decorator::REGION_RIGHT_TOP_CORNER:
				case Decorator::REGION_RIGHT_BOTTOM_CORNER:
					action = ACTION_RESIZE_BORDER;
					break;

				default:
					break;
			}
		}
	}

	_hitRegion = (int32)hitRegion;

	if (action == ACTION_NONE) {
		// No action -- if this is a click inside the window's contents,
		// let it be forwarded to the window.
		return inBorderRegion;
	}

	// reset the click count, if the hit region differs from the previous one
	if (hitRegion != lastHitRegion)
		clickCount = 1;

	DesktopSettings desktopSettings(fDesktop);
	if (!desktopSettings.AcceptFirstClick()) {
		// Ignore clicks on decorator buttons if the
		// non-floating window doesn't have focus
		if (!fWindow->IsFocus() && !fWindow->IsFloating()
			&& action != ACTION_RESIZE_BORDER
			&& action != ACTION_RESIZE && action != ACTION_SLIDE_TAB)
			action = ACTION_DRAG;
	}

	bool activateOnMouseUp = false;
	if (action != ACTION_RESIZE_BORDER) {
		// activate window if in click to activate mode, else only focus it
		if (desktopSettings.MouseMode() == B_NORMAL_MOUSE) {
			fDesktop->ActivateWindow(fWindow);
		} else {
			fDesktop->SetFocusWindow(fWindow);
			activateOnMouseUp = true;
		}
	}

	// switch to the new state
	switch (action) {
		case ACTION_CLOSE:
		case ACTION_ZOOM:
		case ACTION_MINIMIZE:
			_NextState(
				new (std::nothrow) DecoratorButtonState(*this, tab, hitRegion));
			STRACE_CLICK(("===> ACTION_CLOSE/ZOOM/MINIMIZE\n"));
			break;

		case ACTION_DRAG:
			_NextState(new (std::nothrow) DragState(*this, where,
				activateOnMouseUp, clickCount == 2));
			STRACE_CLICK(("===> ACTION_DRAG\n"));
			break;

		case ACTION_RESIZE:
			_NextState(new (std::nothrow) ResizeState(*this, where,
				activateOnMouseUp, clickCount == 2));
			STRACE_CLICK(("===> ACTION_RESIZE\n"));
			break;

		case ACTION_SLIDE_TAB:
			_NextState(new (std::nothrow) SlideTabState(*this, where));
			STRACE_CLICK(("===> ACTION_SLIDE_TAB\n"));
			break;

		case ACTION_RESIZE_BORDER:
			_NextState(new (std::nothrow) ResizeBorderState(*this, where,
				hitRegion));
			STRACE_CLICK(("===> ACTION_RESIZE_BORDER\n"));
			break;

		default:
			break;
	}

	return true;
}


/**
 * @brief Forwards a mouse-up event to the active state, if any.
 *
 * @param message Original B_MOUSE_UP message.
 * @param where   Release position in screen coordinates.
 */
void
DefaultWindowBehaviour::MouseUp(BMessage* message, BPoint where)
{
	if (fState.IsSet())
		fState->MouseUp(message, where);
}


/**
 * @brief Forwards a mouse-moved event to the active state and applies the
 *        focus-follows-mouse policy.
 *
 * If no state is active and the user is holding the window-management
 * modifier, the behaviour enters ManageWindowState. In FFM mode the focus
 * is transferred to the window under the cursor; a fake (synthetic) move
 * sends focus to NULL so the previously focused window can reclaim it.
 *
 * @param message Original B_MOUSE_MOVED message.
 * @param where   Cursor position in screen coordinates.
 * @param isFake  true if the event was synthesized rather than user-driven.
 */
void
DefaultWindowBehaviour::MouseMoved(BMessage* message, BPoint where, bool isFake)
{
	if (fState.IsSet()) {
		fState->MouseMoved(message, where, isFake);
	} else {
		// If the window modifiers are hold, enter the window management state.
		if (_IsWindowModifier(message->FindInt32("modifiers")))
			_NextState(new(std::nothrow) ManageWindowState(*this, where));
	}

	// change focus in FFM mode
	DesktopSettings desktopSettings(fDesktop);
	if (desktopSettings.FocusFollowsMouse()
		&& !fWindow->IsFocus() && (fWindow->Flags() & B_AVOID_FOCUS) == 0) {
		// If the mouse move is a fake one, we set the focus to NULL, which
		// will cause the window that had focus last to retrieve it again - this
		// makes FFM much nicer to use with the keyboard.
		fDesktop->SetFocusWindow(isFake ? NULL : fWindow);
	}
}


/**
 * @brief Notifies the active state that the modifier set changed; otherwise
 *        enters ManageWindowState when the window-modifier becomes active.
 *
 * @param modifiers New modifier bitmask.
 */
void
DefaultWindowBehaviour::ModifiersChanged(int32 modifiers)
{
	BPoint where;
	int32 buttons;
	fDesktop->GetLastMouseState(&where, &buttons);

	if (fState.IsSet()) {
		fState->ModifiersChanged(where, modifiers);
	} else {
		// If the window modifiers are hold, enter the window management state.
		if (_IsWindowModifier(modifiers))
			_NextState(new(std::nothrow) ManageWindowState(*this, where));
	}
}


/**
 * @brief Delegates the snap-to-edge decision to the embedded MagneticBorder.
 *
 * @param window The window being moved.
 * @param delta  Proposed move delta; modified in place when snapping fires.
 * @param now    Current time for snap hysteresis.
 * @return true if @a delta was altered.
 */
bool
DefaultWindowBehaviour::AlterDeltaForSnap(Window* window, BPoint& delta,
	bigtime_t now)
{
	return fMagneticBorder.AlterDeltaForSnap(window, delta, now);
}


/**
 * @brief Returns true when @a modifiers matches the window-management chord
 *        (Command+Control with no Option/Shift) and the window allows
 *        server-side modifiers.
 *
 * @param modifiers Modifier bitmask to test.
 * @return true if the chord is the configured window modifier.
 */
bool
DefaultWindowBehaviour::_IsWindowModifier(int32 modifiers) const
{
	return (fWindow->Flags() & B_NO_SERVER_SIDE_WINDOW_MODIFIERS) == 0
		&& (modifiers & (B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY
				| B_SHIFT_KEY)) == (B_COMMAND_KEY | B_CONTROL_KEY);
}


/**
 * @brief Hit-tests the click position carried in @a message against the
 *        window's decorator and returns the matching region.
 *
 * @param message Mouse message with a "where" point field.
 * @param tab     Out-parameter set to the hit tab index, or -1.
 * @return The hit Decorator::Region, or REGION_NONE if there is no decorator
 *         or no "where" field in the message.
 */
Decorator::Region
DefaultWindowBehaviour::_RegionFor(const BMessage* message, int32& tab) const
{
	Decorator* decorator = fWindow->Decorator();
	if (decorator == NULL)
		return Decorator::REGION_NONE;

	BPoint where;
	if (message->FindPoint("where", &where) != B_OK)
		return Decorator::REGION_NONE;

	return decorator->RegionAt(where, tab);
}


/**
 * @brief Toggles the resize-border highlight for the chosen edges and corner.
 *
 * For each axis the corresponding border region is highlighted; when both
 * axes are active, the matching corner region is also highlighted. The
 * dirty region produced by Decorator::SetRegionHighlight is then forwarded
 * to the window for repainting.
 *
 * @param horizontal LEFT, RIGHT, or NONE.
 * @param vertical   TOP, BOTTOM, or NONE.
 * @param active     true to apply HIGHLIGHT_RESIZE_BORDER, false to clear.
 */
void
DefaultWindowBehaviour::_SetBorderHighlights(int8 horizontal, int8 vertical,
	bool active)
{
	if (Decorator* decorator = fWindow->Decorator()) {
		uint8 highlight = active
			? Decorator::HIGHLIGHT_RESIZE_BORDER
			: Decorator::HIGHLIGHT_NONE;

		// set the highlights for the borders
		BRegion dirtyRegion;
		switch (horizontal) {
			case LEFT:
				decorator->SetRegionHighlight(Decorator::REGION_LEFT_BORDER,
					highlight, &dirtyRegion);
				break;
			case RIGHT:
				decorator->SetRegionHighlight(
					Decorator::REGION_RIGHT_BORDER, highlight,
					&dirtyRegion);
				break;
		}

		switch (vertical) {
			case TOP:
				decorator->SetRegionHighlight(Decorator::REGION_TOP_BORDER,
					highlight, &dirtyRegion);
				break;
			case BOTTOM:
				decorator->SetRegionHighlight(
					Decorator::REGION_BOTTOM_BORDER, highlight,
					&dirtyRegion);
				break;
		}

		// set the highlights for the corners
		if (horizontal != NONE && vertical != NONE) {
			if (horizontal == LEFT) {
				if (vertical == TOP) {
					decorator->SetRegionHighlight(
						Decorator::REGION_LEFT_TOP_CORNER, highlight,
						&dirtyRegion);
				} else {
					decorator->SetRegionHighlight(
						Decorator::REGION_LEFT_BOTTOM_CORNER, highlight,
						&dirtyRegion);
				}
			} else {
				if (vertical == TOP) {
					decorator->SetRegionHighlight(
						Decorator::REGION_RIGHT_TOP_CORNER, highlight,
						&dirtyRegion);
				} else {
					decorator->SetRegionHighlight(
						Decorator::REGION_RIGHT_BOTTOM_CORNER, highlight,
						&dirtyRegion);
				}
			}
		}

		// invalidate the affected regions
		fWindow->ProcessDirtyRegion(dirtyRegion);
	}
}


/**
 * @brief Selects the system cursor that visually matches a resize direction.
 *
 * @param horizontal LEFT, RIGHT, or NONE.
 * @param vertical   TOP, BOTTOM, or NONE.
 * @return A pointer to the matching cursor (e.g. north-west diagonal,
 *         east-west horizontal). Caller does not own the returned pointer.
 */
ServerCursor*
DefaultWindowBehaviour::_ResizeCursorFor(int8 horizontal, int8 vertical)
{
	// get the cursor ID corresponding to the border/corner
	BCursorID cursorID = B_CURSOR_ID_SYSTEM_DEFAULT;

	if (horizontal == LEFT) {
		if (vertical == TOP)
			cursorID = B_CURSOR_ID_RESIZE_NORTH_WEST;
		else if (vertical == BOTTOM)
			cursorID = B_CURSOR_ID_RESIZE_SOUTH_WEST;
		else
			cursorID = B_CURSOR_ID_RESIZE_WEST;
	} else if (horizontal == RIGHT) {
		if (vertical == TOP)
			cursorID = B_CURSOR_ID_RESIZE_NORTH_EAST;
		else if (vertical == BOTTOM)
			cursorID = B_CURSOR_ID_RESIZE_SOUTH_EAST;
		else
			cursorID = B_CURSOR_ID_RESIZE_EAST;
	} else {
		if (vertical == TOP)
			cursorID = B_CURSOR_ID_RESIZE_NORTH;
		else if (vertical == BOTTOM)
			cursorID = B_CURSOR_ID_RESIZE_SOUTH;
	}

	return fDesktop->GetCursorManager().GetCursor(cursorID);
}


/**
 * @brief Installs the desktop's management cursor to a resize cursor.
 *
 * @param horizontal Horizontal resize direction.
 * @param vertical   Vertical resize direction.
 */
void
DefaultWindowBehaviour::_SetResizeCursor(int8 horizontal, int8 vertical)
{
	fDesktop->SetManagementCursor(_ResizeCursorFor(horizontal, vertical));
}


/**
 * @brief Restores the desktop's management cursor (back to the system default).
 */
void
DefaultWindowBehaviour::_ResetResizeCursor()
{
	fDesktop->SetManagementCursor(NULL);
}


/**
 * @brief Maps a window-relative cursor offset @a x,@a y to discrete
 *        horizontal and vertical resize directions.
 *
 * The plane is divided into eight 45-degree sectors. The cursor's angle
 * relative to the window centre selects exactly one sector, which is then
 * decomposed into a horizontal and vertical sign.
 *
 * @param x           Cursor x relative to window centre, normalised by width.
 * @param y           Cursor y relative to window centre, normalised by height.
 * @param _horizontal Out-parameter receiving LEFT, RIGHT, or NONE.
 * @param _vertical   Out-parameter receiving TOP, BOTTOM, or NONE.
 */
/*static*/ void
DefaultWindowBehaviour::_ComputeResizeDirection(float x, float y,
	int8& _horizontal, int8& _vertical)
{
	_horizontal = NONE;
	_vertical = NONE;

	// compute the angle
	if (x == 0 && y == 0)
		return;

	float angle = atan2f(y, x);

	// rotate by 22.5 degree to align our sectors with 45 degree multiples
	angle += M_PI / 8;

	// add 180 degree to the negative values, so we get a nice 0 to 360
	// degree range
	if (angle < 0)
		angle += M_PI * 2;

	switch (int(angle / M_PI_4)) {
		case 0:
			_horizontal = RIGHT;
			break;
		case 1:
			_horizontal = RIGHT;
			_vertical = BOTTOM;
			break;
		case 2:
			_vertical = BOTTOM;
			break;
		case 3:
			_horizontal = LEFT;
			_vertical = BOTTOM;
			break;
		case 4:
			_horizontal = LEFT;
			break;
		case 5:
			_horizontal = LEFT;
			_vertical = TOP;
			break;
		case 6:
			_vertical = TOP;
			break;
		case 7:
		default:
			_horizontal = RIGHT;
			_vertical = TOP;
			break;
	}
}


/**
 * @brief Replaces the active state with @a state, running the standard
 *        ExitState / EnterState lifecycle.
 *
 * The previous state is exited (passing the new state for context) and
 * deleted; the new state is entered. While a state is active this window
 * also receives all mouse events even outside its frame; when the state is
 * cleared and this window was the mouse-event target, that target is
 * cleared too.
 *
 * @param state New state, taking ownership; may be NULL to deactivate.
 */
void
DefaultWindowBehaviour::_NextState(State* state)
{
	// exit the old state
	if (fState.IsSet())
		fState->ExitState(state);

	// set and enter the new state
	ObjectDeleter<State> oldState(fState.Detach());
	fState.SetTo(state);

	if (fState.IsSet()) {
		fState->EnterState(oldState.Get());
		fDesktop->SetMouseEventWindow(fWindow);
	} else if (oldState.IsSet()) {
		// no state anymore -- reset the mouse event window, if it's still us
		if (fDesktop->MouseEventWindow() == fWindow)
			fDesktop->SetMouseEventWindow(NULL);
	}
}
