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
 *		DarkWyrm, bpmagic@columbus.rr.com
 *		Adi Oanca, adioanca@gmail.com
 *		Stephan Aßmus, superstippi@gmx.de
 *		Axel Dörfler, axeld@pinc-software.de
 *		Brecht Machiels, brecht@mos6581.org
 *		Clemens Zeidler, haiku@clemens-zeidler.de
 *		Tri-Edge AI
 *		Jacob Secunda, secundja@gmail.com
 */

/** @file Window.cpp
 *  @brief Server-side window object managing layout, clipping, update sessions,
 *         input dispatch, workspace membership, and window stacking.
 */

#include "Window.h"

#include <new>
#include <stdio.h>

#include <Debug.h>

#include <DirectWindow.h>
#include <PortLink.h>
#include <View.h>
#include <ViewPrivate.h>
#include <WindowPrivate.h>

#include "ClickTarget.h"
#include "Decorator.h"
#include "DecorManager.h"
#include "Desktop.h"
#include "DrawingEngine.h"
#include "HWInterface.h"
#include "MessagePrivate.h"
#include "PortLink.h"
#include "ServerApp.h"
#include "ServerWindow.h"
#include "WindowBehaviour.h"
#include "Workspace.h"
#include "WorkspacesView.h"


// Toggle debug output
//#define DEBUG_WINDOW

#ifdef DEBUG_WINDOW
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif

// IMPORTANT: nested LockSingleWindow()s are not supported (by MultiLocker)

using std::nothrow;

// if the background clearing is delayed until
// the client draws the view, we have less flickering
// when contents have to be redrawn because of resizing
// a window or because the client invalidates parts.
// when redrawing something that has been exposed from underneath
// other windows, the other window will be seen longer at
// its previous position though if the exposed parts are not
// cleared right away. maybe there ought to be a flag in
// the update session, which tells us the cause of the update


//static rgb_color sPendingColor = (rgb_color){ 255, 255, 0, 255 };
//static rgb_color sCurrentColor = (rgb_color){ 255, 0, 255, 255 };


/** @brief Constructs a Window with the given geometry, behaviour, and drawing engine.
 *
 *  Validates look and feel values, sets flags, allocates a decorator (unless
 *  B_NO_BORDER_WINDOW_LOOK), and allocates a WindowBehaviour (unless this is an
 *  offscreen window).  For desktop-feel windows the frame is reset to the origin.
 *
 *  @param frame         The initial window frame in screen coordinates.
 *  @param name          The window title string.
 *  @param look          The window look (border style).
 *  @param feel          The window feel (behaviour class).
 *  @param flags         Window flags (e.g. B_NOT_MOVABLE).
 *  @param workspaces    Bitmask of workspaces this window should appear on.
 *  @param window        The ServerWindow that owns this Window.
 *  @param drawingEngine The DrawingEngine used to render into this window.
 */
Window::Window(const BRect& frame, const char *name,
		window_look look, window_feel feel, uint32 flags, uint32 workspaces,
		::ServerWindow* window, DrawingEngine* drawingEngine)
	:
	fTitle(name),
	fFrame(frame),
	fScreen(NULL),

	fVisibleRegion(),
	fVisibleContentRegion(),
	fDirtyRegion(),

	fContentRegion(),
	fEffectiveDrawingRegion(),

	fVisibleContentRegionValid(false),
	fContentRegionValid(false),
	fEffectiveDrawingRegionValid(false),

	fRegionPool(),

	fWindow(window),
	fDrawingEngine(drawingEngine),
	fDesktop(window->Desktop()),

	fCurrentUpdateSession(&fUpdateSessions[0]),
	fPendingUpdateSession(&fUpdateSessions[1]),
	fUpdateRequested(false),
	fInUpdate(false),
	fUpdatesEnabled(false),

	// Windows start hidden
	fHidden(true),
	// Hidden is 1 or more
	fShowLevel(1),
	fMinimized(false),
	fIsFocus(false),

	fLook(look),
	fFeel(feel),
	fWorkspaces(workspaces),
	fCurrentWorkspace(-1),
	fPriorWorkspace(-1),

	fMinWidth(1),
	fMaxWidth(32768),
	fMinHeight(1),
	fMaxHeight(32768),

	fWorkspacesViewCount(0)
{
	_InitWindowStack();

	// make sure our arguments are valid
	if (!IsValidLook(fLook))
		fLook = B_TITLED_WINDOW_LOOK;
	if (!IsValidFeel(fFeel))
		fFeel = B_NORMAL_WINDOW_FEEL;

	SetFlags(flags, NULL);

	if (fLook != B_NO_BORDER_WINDOW_LOOK && fCurrentStack.IsSet()) {
		// allocates a decorator
		::Decorator* decorator = Decorator();
		if (decorator != NULL) {
			decorator->GetSizeLimits(&fMinWidth, &fMinHeight, &fMaxWidth,
				&fMaxHeight);
		}
	}
	if (fFeel != kOffscreenWindowFeel)
		fWindowBehaviour.SetTo(gDecorManager.AllocateWindowBehaviour(this));

	// do we need to change our size to let the decorator fit?
	// _ResizeBy() will adapt the frame for validity before resizing
	if (feel == kDesktopWindowFeel) {
		// the desktop window spans over the whole screen
		// TODO: this functionality should be moved somewhere else
		//  (so that it is always used when the workspace is changed)
		uint16 width, height;
		uint32 colorSpace;
		float frequency;
		if (Screen() != NULL) {
			Screen()->GetMode(width, height, colorSpace, frequency);
// TODO: MOVE THIS AWAY!!! ResizeBy contains calls to virtual methods!
// Also, there is no TopView()!
			fFrame.OffsetTo(B_ORIGIN);
//			ResizeBy(width - frame.Width(), height - frame.Height(), NULL);
		}
	}

	STRACE(("Window %p, %s:\n", this, Name()));
	STRACE(("\tFrame: (%.1f, %.1f, %.1f, %.1f)\n", fFrame.left, fFrame.top,
		fFrame.right, fFrame.bottom));
	STRACE(("\tWindow %s\n", window ? window->Title() : "NULL"));
}


/** @brief Destructor.
 *
 *  Detaches the top view from the window, detaches from the window stack, and
 *  releases any decorator resources held by the decorator manager.
 */
Window::~Window()
{
	if (fTopView.IsSet()) {
		fTopView->DetachedFromWindow();
	}

	DetachFromWindowStack(false);

	gDecorManager.CleanupForWindow(this);
}


/** @brief Returns whether the window was initialised successfully.
 *
 *  Checks that a drawing engine is present and (for non-offscreen windows)
 *  that a window behaviour was allocated.
 *
 *  @return B_OK if the window is ready for use, B_NO_MEMORY otherwise.
 */
status_t
Window::InitCheck() const
{
	if (GetDrawingEngine() == NULL
		|| (fFeel != kOffscreenWindowFeel && !fWindowBehaviour.IsSet()))
		return B_NO_MEMORY;
	// TODO: anything else?
	return B_OK;
}


/** @brief Updates the visible region of this window based on available screen area.
 *
 *  Called from the Desktop thread.  Resets the visible region to the full
 *  window region then clips it against \a stillAvailableOnScreen.  Marks the
 *  visible-content and effective-drawing regions as invalid.
 *
 *  @param stillAvailableOnScreen The region of screen space not occluded by other windows.
 */
void
Window::SetClipping(BRegion* stillAvailableOnScreen)
{
	// this function is only called from the Desktop thread

	// start from full region (as if the window was fully visible)
	GetFullRegion(&fVisibleRegion);
	// clip to region still available on screen
	fVisibleRegion.IntersectWith(stillAvailableOnScreen);

	fVisibleContentRegionValid = false;
	fEffectiveDrawingRegionValid = false;
}


/** @brief Fills \a region with the full window region including the decorator border.
 *
 *  Starts from the decorator border region and then includes the client frame.
 *
 *  @param region Output parameter filled with the complete window region.
 */
void
Window::GetFullRegion(BRegion* region)
{
	// TODO: if someone needs to call this from
	// the outside, the clipping needs to be readlocked!

	// start from the decorator border, extend to use the frame
	GetBorderRegion(region);
	region->Include(fFrame);
}


/** @brief Fills \a region with the decorator border (non-client) region.
 *
 *  If a decorator is present, returns its footprint; otherwise returns an
 *  empty region.
 *
 *  @param region Output parameter filled with the border region.
 */
void
Window::GetBorderRegion(BRegion* region)
{
	// TODO: if someone needs to call this from
	// the outside, the clipping needs to be readlocked!

	::Decorator* decorator = Decorator();
	if (decorator)
		*region = decorator->GetFootprint();
	else
		region->MakeEmpty();
}


/** @brief Fills \a region with the window's client content region.
 *
 *  Lazily computes the content region by calling _UpdateContentRegion() when
 *  the cached region is no longer valid.
 *
 *  @param region Output parameter filled with the content region.
 */
void
Window::GetContentRegion(BRegion* region)
{
	// TODO: if someone needs to call this from
	// the outside, the clipping needs to be readlocked!

	if (!fContentRegionValid) {
		_UpdateContentRegion();
	}

	*region = fContentRegion;
}


/** @brief Returns the visible content region (intersection of content and visible regions).
 *
 *  Lazily computes and caches the result.
 *
 *  @return Reference to the cached visible content region.
 */
BRegion&
Window::VisibleContentRegion()
{
	// TODO: if someone needs to call this from
	// the outside, the clipping needs to be readlocked!

	// regions expected to be locked
	if (!fVisibleContentRegionValid) {
		GetContentRegion(&fVisibleContentRegion);
		fVisibleContentRegion.IntersectWith(&fVisibleRegion);
	}
	return fVisibleContentRegion;
}


// #pragma mark -


/** @brief Propagates the window's current position to all workspace anchors
 *         when B_SAME_POSITION_IN_ALL_WORKSPACES is set.
 */
void
Window::_PropagatePosition()
{
	if ((fFlags & B_SAME_POSITION_IN_ALL_WORKSPACES) == 0)
		return;

	for (int32 i = 0; i < kListCount; i++) {
		Anchor(i).position = fFrame.LeftTop();
	}
}


/** @brief Moves the window by (\a x, \a y) pixels.
 *
 *  Offsets the frame, dirty and expose regions, view tree, and decorator.
 *  Propagates the move to all stacked windows unless \a moveStack is false.
 *  Sends a B_WINDOW_MOVED message to the client.
 *
 *  @param x          Horizontal pixel offset.
 *  @param y          Vertical pixel offset.
 *  @param moveStack  If true, move all other windows in the stack by the same amount.
 */
void
Window::MoveBy(int32 x, int32 y, bool moveStack)
{
	// this function is only called from the desktop thread

	if (x == 0 && y == 0)
		return;

	fFrame.OffsetBy(x, y);
	_PropagatePosition();

	// take along the dirty region which is not
	// processed yet
	fDirtyRegion.OffsetBy(x, y);
	fExposeRegion.OffsetBy(x, y);

	if (fContentRegionValid)
		fContentRegion.OffsetBy(x, y);

	if (fCurrentUpdateSession->IsUsed())
		fCurrentUpdateSession->MoveBy(x, y);
	if (fPendingUpdateSession->IsUsed())
		fPendingUpdateSession->MoveBy(x, y);

	fEffectiveDrawingRegionValid = false;

	if (fTopView.IsSet()) {
		fTopView->MoveBy(x, y, NULL);
		fTopView->UpdateOverlay();
	}

	::Decorator* decorator = Decorator();
	if (moveStack && decorator)
		decorator->MoveBy(x, y);

	WindowStack* stack = GetWindowStack();
	if (moveStack && stack) {
		for (int32 i = 0; i < stack->CountWindows(); i++) {
			Window* window = stack->WindowList().ItemAt(i);
			if (window == this)
				continue;
			window->MoveBy(x, y, false);
		}
	}

	// the desktop will take care of dirty regions

	// dispatch a message to the client informing about the changed size
	BMessage msg(B_WINDOW_MOVED);
	msg.AddInt64("when", system_time());
	msg.AddPoint("where", fFrame.LeftTop());
	fWindow->SendMessageToClient(&msg);
}


/** @brief Resizes the window by (\a x, \a y) pixels, honouring size limits.
 *
 *  Computes the wanted size, clamps it against all stacked windows' limits,
 *  resizes the top view and decorator, propagates the resize to other stacked
 *  windows when \a resizeStack is true, and sends a B_WINDOW_RESIZED message.
 *
 *  @param x            Horizontal size delta.
 *  @param y            Vertical size delta.
 *  @param dirtyRegion  Region to accumulate dirty areas into.
 *  @param resizeStack  If true, resize all other windows in the stack too.
 */
void
Window::ResizeBy(int32 x, int32 y, BRegion* dirtyRegion, bool resizeStack)
{
	// this function is only called from the desktop thread

	int32 wantWidth = fFrame.IntegerWidth() + x;
	int32 wantHeight = fFrame.IntegerHeight() + y;

	// enforce size limits
	WindowStack* stack = GetWindowStack();
	if (resizeStack && stack) {
		for (int32 i = 0; i < stack->CountWindows(); i++) {
			Window* window = stack->WindowList().ItemAt(i);

			if (wantWidth < window->fMinWidth)
				wantWidth = window->fMinWidth;
			if (wantWidth > window->fMaxWidth)
				wantWidth = window->fMaxWidth;

			if (wantHeight < window->fMinHeight)
				wantHeight = window->fMinHeight;
			if (wantHeight > window->fMaxHeight)
				wantHeight = window->fMaxHeight;
		}
	}

	x = wantWidth - fFrame.IntegerWidth();
	y = wantHeight - fFrame.IntegerHeight();

	if (x == 0 && y == 0)
		return;

	fFrame.right += x;
	fFrame.bottom += y;

	fContentRegionValid = false;
	fEffectiveDrawingRegionValid = false;

	if (fTopView.IsSet()) {
		fTopView->ResizeBy(x, y, dirtyRegion);
		fTopView->UpdateOverlay();
	}

	::Decorator* decorator = Decorator();
	if (decorator && resizeStack)
		decorator->ResizeBy(x, y, dirtyRegion);

	if (resizeStack && stack) {
		for (int32 i = 0; i < stack->CountWindows(); i++) {
			Window* window = stack->WindowList().ItemAt(i);
			if (window == this)
				continue;
			window->ResizeBy(x, y, dirtyRegion, false);
		}
	}

	// send a message to the client informing about the changed size
	BRect frame(Frame());
	BMessage msg(B_WINDOW_RESIZED);
	msg.AddInt64("when", system_time());
	msg.AddInt32("width", frame.IntegerWidth());
	msg.AddInt32("height", frame.IntegerHeight());
	fWindow->SendMessageToClient(&msg);
}


/** @brief Applies a resize-outline delta, enforcing size limits, and updates the decorator.
 *
 *  Used during live (outline) resize.  Clamps the delta against all stacked
 *  windows' size limits, then asks the decorator to render the outline and
 *  recalculates the content region.
 *
 *  @param delta       The desired resize delta as a BPoint (x = width, y = height change).
 *  @param dirtyRegion Region to accumulate dirty areas into.
 */
void
Window::SetOutlinesDelta(BPoint delta, BRegion* dirtyRegion)
{
	float wantWidth = fFrame.IntegerWidth() + delta.x;
	float wantHeight = fFrame.IntegerHeight() + delta.y;

	// enforce size limits
	WindowStack* stack = GetWindowStack();
	if (stack != NULL) {
		for (int32 i = 0; i < stack->CountWindows(); i++) {
			Window* window = stack->WindowList().ItemAt(i);

			if (wantWidth < window->fMinWidth)
				wantWidth = window->fMinWidth;
			if (wantWidth > window->fMaxWidth)
				wantWidth = window->fMaxWidth;

			if (wantHeight < window->fMinHeight)
				wantHeight = window->fMinHeight;
			if (wantHeight > window->fMaxHeight)
				wantHeight = window->fMaxHeight;
		}

		delta.x = wantWidth - fFrame.IntegerWidth();
		delta.y = wantHeight - fFrame.IntegerHeight();
	}

	::Decorator* decorator = Decorator();

	if (decorator != NULL)
		decorator->SetOutlinesDelta(delta, dirtyRegion);

	_UpdateContentRegion();
}


/** @brief Scrolls \a view by (\a dx, \a dy) and triggers a content redraw.
 *
 *  Executed in the ServerWindow thread with the read lock held.  Ignored
 *  for the top view or when the delta is zero.  Marks the affected region
 *  dirty via _TriggerContentRedraw().
 *
 *  @param view The view to scroll.
 *  @param dx   Horizontal scroll amount.
 *  @param dy   Vertical scroll amount.
 */
void
Window::ScrollViewBy(View* view, int32 dx, int32 dy)
{
	// this is executed in ServerWindow with the Readlock
	// held

	if (!view || view == fTopView.Get() || (dx == 0 && dy == 0))
		return;

	BRegion* dirty = fRegionPool.GetRegion();
	if (!dirty)
		return;

	view->ScrollBy(dx, dy, dirty);

//fDrawingEngine->FillRegion(*dirty, (rgb_color){ 255, 0, 255, 255 });
//snooze(20000);

	if (!IsOffscreenWindow() && IsVisible() && view->IsVisible()) {
		dirty->IntersectWith(&VisibleContentRegion());
		_TriggerContentRedraw(*dirty);
	}

	fRegionPool.Recycle(dirty);
}


//! Takes care of invalidating parts that could not be copied
/** @brief Copies visible content within the window and marks uncopyable parts dirty.
 *
 *  Executed in the ServerWindow thread with the read lock held.  Computes the
 *  intersection of \a region with the visible content at both the source and
 *  destination positions, blits the copyable area via the drawing engine, and
 *  adds the remaining area to the dirty region.  Excludes the copied area from
 *  any pending update session so the client does not repaint it redundantly.
 *
 *  @param region   The region to copy (in screen coordinates at the source location).
 *  @param xOffset  Horizontal copy offset.
 *  @param yOffset  Vertical copy offset.
 */
void
Window::CopyContents(BRegion* region, int32 xOffset, int32 yOffset)
{
	// executed in ServerWindow thread with the read lock held
	if (!IsVisible())
		return;

	BRegion* newDirty = fRegionPool.GetRegion(*region);

	// clip the region to the visible contents at the
	// source and destination location (note that VisibleContentRegion()
	// is used once to make sure it is valid, then fVisibleContentRegion
	// is used directly)
	region->IntersectWith(&VisibleContentRegion());
	if (region->CountRects() > 0) {
		// Constrain to content region at destination
		region->OffsetBy(xOffset, yOffset);
		region->IntersectWith(&fVisibleContentRegion);
		if (region->CountRects() > 0) {
			// if the region still contains any rects
			// offset to source location again
			region->OffsetBy(-xOffset, -yOffset);

			BRegion* allDirtyRegions = fRegionPool.GetRegion(fDirtyRegion);
			if (allDirtyRegions != NULL) {
				if (fPendingUpdateSession->IsUsed()) {
					allDirtyRegions->Include(
						&fPendingUpdateSession->DirtyRegion());
				}
				if (fCurrentUpdateSession->IsUsed()) {
					allDirtyRegions->Include(
						&fCurrentUpdateSession->DirtyRegion());
				}
				// Get just the part of the dirty regions which is semantically
				// copied along
				allDirtyRegions->IntersectWith(region);
			}

			BRegion* copyRegion = fRegionPool.GetRegion(*region);
			if (copyRegion != NULL) {
				// never copy what's already dirty
				if (allDirtyRegions != NULL)
					copyRegion->Exclude(allDirtyRegions);

				if (fDrawingEngine->LockParallelAccess()) {
					fDrawingEngine->CopyRegion(copyRegion, xOffset, yOffset);
					fDrawingEngine->UnlockParallelAccess();

					// Prevent those parts from being added to the dirty region...
					newDirty->Exclude(copyRegion);

					// The parts that could be copied are not dirty (at the
					// target location!)
					copyRegion->OffsetBy(xOffset, yOffset);
					// ... and even exclude them from the pending dirty region!
					if (fPendingUpdateSession->IsUsed())
						fPendingUpdateSession->DirtyRegion().Exclude(copyRegion);
				}

				fRegionPool.Recycle(copyRegion);
			} else {
				// Fallback, should never be here.
				if (fDrawingEngine->LockParallelAccess()) {
					fDrawingEngine->CopyRegion(region, xOffset, yOffset);
					fDrawingEngine->UnlockParallelAccess();
				}
			}

			if (allDirtyRegions != NULL)
				fRegionPool.Recycle(allDirtyRegions);
		}
	}
	// what is left visible from the original region
	// at the destination after the region which could be
	// copied has been excluded, is considered dirty
	// NOTE: it may look like dirty regions are not moved
	// if no region could be copied, but that's alright,
	// since these parts will now be in newDirty anyways
	// (with the right offset)
	newDirty->OffsetBy(xOffset, yOffset);
	newDirty->IntersectWith(&fVisibleContentRegion);
	if (newDirty->CountRects() > 0)
		ProcessDirtyRegion(*newDirty);

	fRegionPool.Recycle(newDirty);
}


// #pragma mark -


/** @brief Sets the top-level view for this window.
 *
 *  Detaches any existing top view, sets the new one, aligns its position and
 *  size to match the window frame, and calls AttachedToWindow() on it.
 *
 *  @param topView The new top-level View.
 */
void
Window::SetTopView(View* topView)
{
	if (fTopView.IsSet()) {
		fTopView->DetachedFromWindow();
	}

	fTopView.SetTo(topView);

	if (fTopView.IsSet()) {
		// the top view is special, it has a coordinate system
		// as if it was attached directly to the desktop, therefor,
		// the coordinate conversion through the view tree works
		// as expected, since the top view has no "parent" but has
		// fFrame as if it had

		// make sure the location of the top view on screen matches ours
		fTopView->MoveBy((int32)(fFrame.left - fTopView->Frame().left),
			(int32)(fFrame.top - fTopView->Frame().top), NULL);

		// make sure the size of the top view matches ours
		fTopView->ResizeBy((int32)(fFrame.Width() - fTopView->Frame().Width()),
			(int32)(fFrame.Height() - fTopView->Frame().Height()), NULL);

		fTopView->AttachedToWindow(this);
	}
}


/** @brief Returns the deepest view that contains the screen point \a where.
 *  @param where The point to test in screen coordinates.
 *  @return Pointer to the matching view.
 */
View*
Window::ViewAt(const BPoint& where)
{
	return fTopView->ViewAt(where);
}


/** @brief Returns the window anchor structure for the given list index.
 *  @param index The workspace or list index.
 *  @return Reference to the anchor (next, previous, position).
 */
window_anchor&
Window::Anchor(int32 index)
{
	return fAnchor[index];
}


/** @brief Returns the next window in the ordered list at \a index.
 *  @param index The list index.
 *  @return Pointer to the next Window, or NULL if this is the last.
 */
Window*
Window::NextWindow(int32 index) const
{
	return fAnchor[index].next;
}


/** @brief Returns the previous window in the ordered list at \a index.
 *  @param index The list index.
 *  @return Pointer to the previous Window, or NULL if this is the first.
 */
Window*
Window::PreviousWindow(int32 index) const
{
	return fAnchor[index].previous;
}


/** @brief Returns the decorator associated with this window's stack, or NULL. */
::Decorator*
Window::Decorator() const
{
	if (!fCurrentStack.IsSet())
		return NULL;
	return fCurrentStack->Decorator();
}


/** @brief Reloads the decorator after a decorator theme change.
 *
 *  Only the first window in the stack allocates a new decorator; subsequent
 *  windows return true immediately.  Rebuilds all tab entries for stacked
 *  windows and allocates a new WindowBehaviour.
 *
 *  @return true if the reload succeeded (or was not needed), false on failure.
 */
bool
Window::ReloadDecor()
{
	::Decorator* decorator = NULL;
	WindowBehaviour* windowBehaviour = NULL;
	WindowStack* stack = GetWindowStack();
	if (stack == NULL)
		return false;

	// only reload the window at the first position
	if (stack->WindowAt(0) != this)
		return true;

	if (fLook != B_NO_BORDER_WINDOW_LOOK) {
		// we need a new decorator
		decorator = gDecorManager.AllocateDecorator(this);
		if (decorator == NULL)
			return false;

		// add all tabs to the decorator
		for (int32 i = 1; i < stack->CountWindows(); i++) {
			Window* window = stack->WindowAt(i);
			BRegion dirty;
			DesktopSettings settings(fDesktop);
			if (decorator->AddTab(settings, window->Title(), window->Look(),
				window->Flags(), -1, &dirty) == NULL) {
				delete decorator;
				return false;
			}
		}
	} else
		return true;

	windowBehaviour = gDecorManager.AllocateWindowBehaviour(this);
	if (windowBehaviour == NULL) {
		delete decorator;
		return false;
	}

	stack->SetDecorator(decorator);

	fWindowBehaviour.SetTo(windowBehaviour);

	// set the correct focus and top layer tab
	for (int32 i = 0; i < stack->CountWindows(); i++) {
		Window* window = stack->WindowAt(i);
		if (window->IsFocus())
			decorator->SetFocus(i, true);
		if (window == stack->TopLayerWindow())
			decorator->SetTopTab(i);
	}

	return true;
}


/** @brief Sets the Screen this window is displayed on.
 *  @param screen The screen to associate with this window.
 */
void
Window::SetScreen(const ::Screen* screen)
{
	// TODO this assert fails in Desktop::ShowWindow
	//ASSERT_MULTI_WRITE_LOCKED(fDesktop->ScreenLocker());
	fScreen = screen;
}


/** @brief Returns the Screen this window is currently displayed on.
 *  @return Pointer to the Screen, or NULL if not yet assigned.
 */
const ::Screen*
Window::Screen() const
{
	// TODO this assert also fails
	//ASSERT_MULTI_READ_LOCKED(fDesktop->ScreenLocker());
	return fScreen;
}


// #pragma mark -


/** @brief Returns the effective drawing region for \a view.
 *
 *  Builds the effective drawing region from the visible content region,
 *  intersected with (or excluding) the current/pending update session's
 *  dirty region depending on whether an update is in progress.  The result
 *  is further clipped to the view's screen-and-user clipping.
 *
 *  @param view   The view requesting the drawing region.
 *  @param region Output region filled with the effective drawing area.
 */
void
Window::GetEffectiveDrawingRegion(View* view, BRegion& region)
{
	if (!fEffectiveDrawingRegionValid) {
		fEffectiveDrawingRegion = VisibleContentRegion();
		if (fUpdateRequested && !fInUpdate) {
			// We requested an update, but the client has not started it yet,
			// so it is only allowed to draw outside the pending update sessions
			// region
			fEffectiveDrawingRegion.Exclude(
				&fPendingUpdateSession->DirtyRegion());
		} else if (fInUpdate) {
			// enforce the dirty region of the update session
			fEffectiveDrawingRegion.IntersectWith(
				&fCurrentUpdateSession->DirtyRegion());
		} else {
			// not in update, the view can draw everywhere
//printf("Window(%s)::GetEffectiveDrawingRegion(for %s) - outside update\n", Title(), view->Name());
		}

		fEffectiveDrawingRegionValid = true;
	}

	// TODO: this is a region that needs to be cached later in the server
	// when the current view in ServerWindow is set, and we are currently
	// in an update (fInUpdate), than we can set this region and remember
	// it for the comming drawing commands until the current view changes
	// again or fEffectiveDrawingRegionValid is suddenly false.
	region = fEffectiveDrawingRegion;
	if (!fContentRegionValid)
		_UpdateContentRegion();

	region.IntersectWith(&view->ScreenAndUserClipping(&fContentRegion));
}


/** @brief Returns true if the drawing region for \a view has changed since last call.
 *  @param view The view to check.
 *  @return true if the effective drawing region or the view's screen clipping is invalid.
 */
bool
Window::DrawingRegionChanged(View* view) const
{
	return !fEffectiveDrawingRegionValid || !view->IsScreenClippingValid();
}


/** @brief Merges the given dirty and expose regions into the window's dirty region.
 *
 *  If the dirty region was empty before this call, a redraw message is
 *  requested from the ServerWindow so the client initiates a new update cycle.
 *
 *  @param dirtyRegion  The newly dirty region to incorporate.
 *  @param exposeRegion The newly exposed region to incorporate.
 */
void
Window::ProcessDirtyRegion(const BRegion& dirtyRegion, const BRegion& exposeRegion)
{
	// if this is executed in the desktop thread,
	// it means that the window thread currently
	// blocks to get the read lock, if it is
	// executed from the window thread, it should
	// have the read lock and the desktop thread
	// is blocking to get the write lock. IAW, this
	// is only executed in one thread.
	if (fDirtyRegion.CountRects() == 0) {
		// the window needs to be informed
		// when the dirty region was empty.
		// NOTE: when the window thread has processed
		// the dirty region in MessageReceived(),
		// it will make the region empty again,
		// when it is empty here, we need to send
		// the message to initiate the next update round.
		// Until the message is processed in the window
		// thread, the desktop thread can add parts to
		// the region as it likes.
		ServerWindow()->RequestRedraw();
	}

	fDirtyRegion.Include(&dirtyRegion);
	fExposeRegion.Include(&exposeRegion);
}


/** @brief Redraws the accumulated dirty region and resets it to empty.
 *
 *  Executed from ServerWindow with the read lock held.  Draws the decorator
 *  border, then triggers content redraw for the dirty and expose sub-regions
 *  of the visible content area.  Only operates on the top-layer stack window.
 */
void
Window::RedrawDirtyRegion()
{
	if (TopLayerStackWindow() != this) {
		fDirtyRegion.MakeEmpty();
		fExposeRegion.MakeEmpty();
		return;
	}

	// executed from ServerWindow with the read lock held
	if (IsVisible()) {
		_DrawBorder();

		BRegion* dirtyContentRegion = fRegionPool.GetRegion(VisibleContentRegion());
		BRegion* exposeContentRegion = fRegionPool.GetRegion(VisibleContentRegion());
		dirtyContentRegion->IntersectWith(&fDirtyRegion);
		exposeContentRegion->IntersectWith(&fExposeRegion);

		_TriggerContentRedraw(*dirtyContentRegion, *exposeContentRegion);

		fRegionPool.Recycle(dirtyContentRegion);
		fRegionPool.Recycle(exposeContentRegion);
	}

	// reset the dirty region, since
	// we're fully clean. If the desktop
	// thread wanted to mark something
	// dirty in the mean time, it was
	// blocking on the global region lock to
	// get write access, since we're holding
	// the read lock for the whole time.
	fDirtyRegion.MakeEmpty();
	fExposeRegion.MakeEmpty();
}


/** @brief Marks a region on the desktop dirty, causing all affected windows to redraw.
 *  @param regionOnScreen The screen-space region to mark dirty.
 */
void
Window::MarkDirty(BRegion& regionOnScreen)
{
	// for marking any part of the desktop dirty
	// this will get write access to the global
	// region lock, and result in ProcessDirtyRegion()
	// to be called for any windows affected
	if (fDesktop)
		fDesktop->MarkDirty(regionOnScreen);
}


/** @brief Marks content-only dirty and expose regions for this window.
 *
 *  Clips both regions to the visible content area and triggers a content
 *  redraw.  Ignores hidden and offscreen windows.
 *
 *  @param dirtyRegion  The dirty region (clipped in place).
 *  @param exposeRegion The expose region (clipped in place).
 */
void
Window::MarkContentDirty(BRegion& dirtyRegion, BRegion& exposeRegion)
{
	// for triggering AS_REDRAW
	// since this won't affect other windows, read locking
	// is sufficient. If there was no dirty region before,
	// an update message is triggered
	if (fHidden || IsOffscreenWindow())
		return;

	dirtyRegion.IntersectWith(&VisibleContentRegion());
	exposeRegion.IntersectWith(&VisibleContentRegion());
	_TriggerContentRedraw(dirtyRegion, exposeRegion);
}


/** @brief Asynchronously marks a content region dirty.
 *
 *  Adds \a dirtyRegion to the window's dirty region after clipping to visible
 *  content.  Sends a redraw request if the region was previously empty.
 *  Does not block waiting for the redraw to complete.
 *
 *  @param dirtyRegion The dirty region (clipped in place).
 */
void
Window::MarkContentDirtyAsync(BRegion& dirtyRegion)
{
	// NOTE: see comments in ProcessDirtyRegion()
	if (fHidden || IsOffscreenWindow())
		return;

	dirtyRegion.IntersectWith(&VisibleContentRegion());

	if (fDirtyRegion.CountRects() == 0) {
		ServerWindow()->RequestRedraw();
	}

	fDirtyRegion.Include(&dirtyRegion);
}


/** @brief Invalidates a specific region of \a view, triggering a redraw.
 *
 *  Converts the region to screen space, clips to the visible content area
 *  and the view's screen-and-user clipping, then triggers content redraw.
 *
 *  @param view        The view whose region should be invalidated.
 *  @param viewRegion  The region in view local coordinates to invalidate.
 */
void
Window::InvalidateView(View* view, BRegion& viewRegion)
{
	if (view && IsVisible() && view->IsVisible()) {
		if (!fContentRegionValid)
			_UpdateContentRegion();

		view->LocalToScreenTransform().Apply(&viewRegion);
		viewRegion.IntersectWith(&VisibleContentRegion());
		if (viewRegion.CountRects() > 0) {
			viewRegion.IntersectWith(
				&view->ScreenAndUserClipping(&fContentRegion));

//fDrawingEngine->FillRegion(viewRegion, rgb_color{ 0, 255, 0, 255 });
//snooze(10000);
			_TriggerContentRedraw(viewRegion);
		}
	}
}

// DisableUpdateRequests
/** @brief Prevents further update messages from being sent to the client. */
void
Window::DisableUpdateRequests()
{
	fUpdatesEnabled = false;
}


// EnableUpdateRequests
/** @brief Re-enables update message delivery and sends any pending update. */
void
Window::EnableUpdateRequests()
{
	fUpdatesEnabled = true;
	if (!fUpdateRequested && fPendingUpdateSession->IsUsed())
		_SendUpdateMessage();
}

// #pragma mark -


/*!	\brief Handles a mouse-down message for the window.

	\param message The message.
	\param where The point where the mouse click happened.
	\param lastClickTarget The target of the previous click.
	\param clickCount The number of subsequent, no longer than double-click
		interval separated clicks that have happened so far. This number doesn't
		necessarily match the value in the message. It has already been
		pre-processed in order to avoid erroneous multi-clicks (e.g. when a
		different button has been used or a different window was targeted). This
		is an in-out variable. The method can reset the value to 1, if it
		doesn't want this event handled as a multi-click. Returning a different
		click target will also make the caller reset the click count.
	\param _clickTarget Set by the method to a value identifying the clicked
		element. If not explicitly set, an invalid click target is assumed.
*/
/** @brief Dispatches a mouse-down event to the window behaviour or the view under the cursor.
 *
 *  Passes the event to the WindowBehaviour first (for decorator hit-testing).
 *  If the behaviour does not consume it, the click is delivered to the view
 *  at \a where.  Focus and activation logic is applied for windows that do not
 *  accept first click.
 *
 *  @param message         The mouse-down BMessage.
 *  @param where           The click position in screen coordinates.
 *  @param lastClickTarget The ClickTarget of the previous click.
 *  @param clickCount      In/out click count; may be reset to 1 by the method.
 *  @param _clickTarget    Output: set to a ClickTarget identifying the clicked element.
 */
void
Window::MouseDown(BMessage* message, BPoint where,
	const ClickTarget& lastClickTarget, int32& clickCount,
	ClickTarget& _clickTarget)
{
	// If the previous click hit our decorator, get the hit region.
	int32 windowToken = fWindow->ServerToken();
	int32 lastHitRegion = 0;
	if (lastClickTarget.GetType() == ClickTarget::TYPE_WINDOW_DECORATOR
		&& lastClickTarget.WindowToken() == windowToken) {
		lastHitRegion = lastClickTarget.WindowElement();
	}

	// Let the window behavior process the mouse event.
	int32 hitRegion = 0;
	bool eventEaten = fWindowBehaviour->MouseDown(message, where, lastHitRegion,
		clickCount, hitRegion);

	if (eventEaten) {
		// click on the decorator (or equivalent)
		_clickTarget = ClickTarget(ClickTarget::TYPE_WINDOW_DECORATOR,
			windowToken, (int32)hitRegion);
	} else {
		// click was inside the window contents
		int32 viewToken = B_NULL_TOKEN;
		if (View* view = ViewAt(where)) {
			if (HasModal())
				return;

			// clicking a simple View
			if (!IsFocus()) {
				bool acceptFirstClick
					= (Flags() & B_WILL_ACCEPT_FIRST_CLICK) != 0;

				// Activate or focus the window in case it doesn't accept first
				// click, depending on the mouse mode
				if (!acceptFirstClick) {
					bool avoidFocus = (Flags() & B_AVOID_FOCUS) != 0;
					DesktopSettings desktopSettings(fDesktop);
					if (desktopSettings.MouseMode() == B_NORMAL_MOUSE)
						fDesktop->ActivateWindow(this);
					else if (!avoidFocus)
						fDesktop->SetFocusWindow(this);

					// Eat the click if we don't accept first click
					// (B_AVOID_FOCUS never gets the focus, so they always accept
					// the first click)
					// TODO: the latter is unlike BeOS - if we really wanted to
					// imitate this behaviour, we would need to check if we're
					// the front window instead of the focus window
					if (!desktopSettings.AcceptFirstClick() && !avoidFocus)
						return;
				}
			}

			// fill out view token for the view under the mouse
			viewToken = view->Token();
			view->MouseDown(message, where);
		}

		_clickTarget = ClickTarget(ClickTarget::TYPE_WINDOW_CONTENTS,
			windowToken, viewToken);
	}
}


/** @brief Dispatches a mouse-up event to the window behaviour and the view under the cursor.
 *
 *  @param message     The mouse-up BMessage.
 *  @param where       The release position in screen coordinates.
 *  @param _viewToken  Output: set to the token of the view that received the event.
 */
void
Window::MouseUp(BMessage* message, BPoint where, int32* _viewToken)
{
	fWindowBehaviour->MouseUp(message, where);

	if (View* view = ViewAt(where)) {
		if (HasModal())
			return;

		*_viewToken = view->Token();
		view->MouseUp(message, where);
	}
}


/** @brief Dispatches a mouse-moved event to the window behaviour and the view under the cursor.
 *
 *  Ignores events that are not the latest mouse position (pointer history).
 *  Updates the application's active cursor to match the view under the pointer.
 *
 *  @param message             The mouse-moved BMessage.
 *  @param where               The current pointer position in screen coordinates.
 *  @param _viewToken          Output: set to the token of the view under the pointer.
 *  @param isLatestMouseMoved  If false, the event is part of pointer history and is skipped.
 *  @param isFake              True if this is a synthesised (fake) mouse event.
 */
void
Window::MouseMoved(BMessage *message, BPoint where, int32* _viewToken,
	bool isLatestMouseMoved, bool isFake)
{
	View* view = ViewAt(where);
	if (view != NULL)
		*_viewToken = view->Token();

	// ignore pointer history
	if (!isLatestMouseMoved)
		return;

	fWindowBehaviour->MouseMoved(message, where, isFake);

	// mouse cursor

	if (view != NULL) {
		view->MouseMoved(message, where);

		// TODO: there is more for real cursor support, ie. if a window is closed,
		//		new app cursor shouldn't override view cursor, ...
		ServerWindow()->App()->SetCurrentCursor(view->Cursor());
	}
}


/** @brief Notifies the window behaviour of a keyboard modifier change.
 *  @param modifiers The new modifier key bitmask.
 */
void
Window::ModifiersChanged(int32 modifiers)
{
	fWindowBehaviour->ModifiersChanged(modifiers);
}


// #pragma mark -


/** @brief Sends a B_WORKSPACE_ACTIVATED message to the client.
 *
 *  @param index  The workspace index that changed activation state.
 *  @param active True if the workspace became active, false if deactivated.
 */
void
Window::WorkspaceActivated(int32 index, bool active)
{
	BMessage activatedMsg(B_WORKSPACE_ACTIVATED);
	activatedMsg.AddInt64("when", system_time());
	activatedMsg.AddInt32("workspace", index);
	activatedMsg.AddBool("active", active);

	ServerWindow()->SendMessageToClient(&activatedMsg);
}


/** @brief Sends a B_WORKSPACES_CHANGED message to the client and updates the bitmask.
 *
 *  @param oldWorkspaces The previous workspace bitmask.
 *  @param newWorkspaces The new workspace bitmask.
 */
void
Window::WorkspacesChanged(uint32 oldWorkspaces, uint32 newWorkspaces)
{
	fWorkspaces = newWorkspaces;

	BMessage changedMsg(B_WORKSPACES_CHANGED);
	changedMsg.AddInt64("when", system_time());
	changedMsg.AddInt32("old", oldWorkspaces);
	changedMsg.AddInt32("new", newWorkspaces);

	ServerWindow()->SendMessageToClient(&changedMsg);
}


/** @brief Sends a B_WINDOW_ACTIVATED message to the client.
 *  @param active True if this window became the active (focus) window.
 */
void
Window::Activated(bool active)
{
	BMessage msg(B_WINDOW_ACTIVATED);
	msg.AddBool("active", active);
	ServerWindow()->SendMessageToClient(&msg);
}


//# pragma mark -


/** @brief Changes the window title and redraws the decorator tab.
 *
 *  @param name  The new title string.
 *  @param dirty Region that is dirtied by the title change (passed to the decorator).
 */
void
Window::SetTitle(const char* name, BRegion& dirty)
{
	// rebuild the clipping for the title area
	// and redraw it.

	fTitle = name;

	::Decorator* decorator = Decorator();
	if (decorator) {
		int32 index = PositionInStack();
		decorator->SetTitle(index, name, &dirty);
	}
}


/** @brief Sets the keyboard focus state and redraws the decorator accordingly.
 *
 *  Marks the decorator footprint region dirty so the focus highlight is
 *  repainted, updates the internal focus flag, informs the decorator, and
 *  calls Activated().
 *
 *  @param focus True to grant focus to this window, false to take it away.
 */
void
Window::SetFocus(bool focus)
{
	::Decorator* decorator = Decorator();

	// executed from Desktop thread
	// it holds the clipping write lock,
	// so the window thread cannot be
	// accessing fIsFocus

	BRegion* dirty = NULL;
	if (decorator)
		dirty = fRegionPool.GetRegion(decorator->GetFootprint());
	if (dirty) {
		dirty->IntersectWith(&fVisibleRegion);
		fDesktop->MarkDirty(*dirty);
		fRegionPool.Recycle(dirty);
	}

	fIsFocus = focus;
	if (decorator) {
		int32 index = PositionInStack();
		decorator->SetFocus(index, focus);
	}

	Activated(focus);
}


/** @brief Sets the window's hidden flag and propagates it to the top view.
 *  @param hidden True to hide the window, false to show it.
 */
void
Window::SetHidden(bool hidden)
{
	// the desktop takes care of dirty regions
	if (fHidden != hidden) {
		fHidden = hidden;

		fTopView->SetHidden(hidden);

		// TODO: anything else?
	}
}


/** @brief Sets the show level used to determine effective visibility.
 *  @param showLevel The new show level (>0 means hidden).
 */
void
Window::SetShowLevel(int32 showLevel)
{
	if (showLevel == fShowLevel)
		return;

	fShowLevel = showLevel;
}


/** @brief Sets the minimised state of the window.
 *  @param minimized True to mark the window as minimised.
 */
void
Window::SetMinimized(bool minimized)
{
	if (minimized == fMinimized)
		return;

	fMinimized = minimized;
}


/** @brief Returns whether the window is effectively visible on screen.
 *
 *  Offscreen windows are always "visible" for drawing purposes.  Normal windows
 *  must not be hidden and must be assigned to a valid workspace.
 *
 *  @return true if the window should be drawn.
 */
bool
Window::IsVisible() const
{
	if (IsOffscreenWindow())
		return true;

	if (IsHidden())
		return false;

/*
	if (fVisibleRegion.CountRects() == 0)
		return false;
*/
	return fCurrentWorkspace >= 0 && fCurrentWorkspace < kWorkingList;
}


/** @brief Returns true if the window is currently being dragged by the user.
 *  @return true if the window behaviour reports a drag in progress.
 */
bool
Window::IsDragging() const
{
	if (!fWindowBehaviour.IsSet())
		return false;
	return fWindowBehaviour->IsDragging();
}


/** @brief Returns true if the window is currently being resized by the user.
 *  @return true if the window behaviour reports a resize in progress.
 */
bool
Window::IsResizing() const
{
	if (!fWindowBehaviour.IsSet())
		return false;
	return fWindowBehaviour->IsResizing();
}


/** @brief Sets the minimum and maximum size limits for the window.
 *
 *  Clamps any negative minimums to zero, stores the new limits, then asks the
 *  decorator to contribute its own minimum requirements.  Finally calls
 *  _ObeySizeLimits() to resize the window if it currently violates the new limits.
 *
 *  @param minWidth  Minimum allowed width.
 *  @param maxWidth  Maximum allowed width.
 *  @param minHeight Minimum allowed height.
 *  @param maxHeight Maximum allowed height.
 */
void
Window::SetSizeLimits(int32 minWidth, int32 maxWidth, int32 minHeight,
	int32 maxHeight)
{
	if (minWidth < 0)
		minWidth = 0;

	if (minHeight < 0)
		minHeight = 0;

	fMinWidth = minWidth;
	fMaxWidth = maxWidth;
	fMinHeight = minHeight;
	fMaxHeight = maxHeight;

	// give the Decorator a say in this too
	::Decorator* decorator = Decorator();
	if (decorator) {
		decorator->GetSizeLimits(&fMinWidth, &fMinHeight, &fMaxWidth,
			&fMaxHeight);
	}

	_ObeySizeLimits();
}


/** @brief Retrieves the current size limits.
 *
 *  @param minWidth  Output: minimum allowed width.
 *  @param maxWidth  Output: maximum allowed width.
 *  @param minHeight Output: minimum allowed height.
 *  @param maxHeight Output: maximum allowed height.
 */
void
Window::GetSizeLimits(int32* minWidth, int32* maxWidth,
	int32* minHeight, int32* maxHeight) const
{
	*minWidth = fMinWidth;
	*maxWidth = fMaxWidth;
	*minHeight = fMinHeight;
	*maxHeight = fMaxHeight;
}


/** @brief Sets the horizontal tab location within the decorator.
 *
 *  @param location   The new tab position as a float in the range [0, 1].
 *  @param isShifting True if tabs are being interactively shifted (drag in progress).
 *  @param dirty      Region dirtied by the tab move.
 *  @return true if the decorator accepted the new location.
 */
bool
Window::SetTabLocation(float location, bool isShifting, BRegion& dirty)
{
	::Decorator* decorator = Decorator();
	if (decorator) {
		int32 index = PositionInStack();
		return decorator->SetTabLocation(index, location, isShifting, &dirty);
	}

	return false;
}


/** @brief Returns the current tab location within the decorator.
 *  @return Tab position as a float, or 0.0 if there is no decorator.
 */
float
Window::TabLocation() const
{
	::Decorator* decorator = Decorator();
	if (decorator) {
		int32 index = PositionInStack();
		return decorator->TabLocation(index);
	}
	return 0.0;
}


/** @brief Applies decorator settings from a BMessage.
 *
 *  Handles the special 'prVu' message for decorator preview.  Otherwise
 *  delegates to the decorator's SetSettings().
 *
 *  @param settings The settings BMessage.
 *  @param dirty    Region dirtied by the settings change.
 *  @return true if the settings were applied successfully.
 */
bool
Window::SetDecoratorSettings(const BMessage& settings, BRegion& dirty)
{
	if (settings.what == 'prVu') {
		// 'prVu' == preview a decorator!
		BString path;
		if (settings.FindString("preview", &path) == B_OK)
			return gDecorManager.PreviewDecorator(path, this) == B_OK;
		return false;
	}

	::Decorator* decorator = Decorator();
	if (decorator)
		return decorator->SetSettings(settings, &dirty);

	return false;
}


/** @brief Retrieves the current decorator settings into \a settings.
 *
 *  Also queries the desktop for any global decorator settings.
 *
 *  @param settings Output BMessage to fill with decorator settings.
 *  @return true if the decorator provided settings.
 */
bool
Window::GetDecoratorSettings(BMessage* settings)
{
	if (fDesktop)
		fDesktop->GetDecoratorSettings(this, *settings);

	::Decorator* decorator = Decorator();
	if (decorator)
		return decorator->GetSettings(settings);

	return false;
}


/** @brief Notifies the decorator that system fonts have changed.
 *  @param updateRegion Region to accumulate dirty areas caused by the font change.
 */
void
Window::FontsChanged(BRegion* updateRegion)
{
	::Decorator* decorator = Decorator();
	if (decorator != NULL) {
		DesktopSettings settings(fDesktop);
		decorator->FontsChanged(settings, updateRegion);
	}
}


/** @brief Notifies the decorator that system colours have changed.
 *  @param updateRegion Region to accumulate dirty areas caused by the colour change.
 */
void
Window::ColorsChanged(BRegion* updateRegion)
{
	::Decorator* decorator = Decorator();
	if (decorator != NULL) {
		DesktopSettings settings(fDesktop);
		decorator->ColorsChanged(settings, updateRegion);
	}
}


/** @brief Sets the window look (border style) and updates the decorator.
 *
 *  Invalidates the content and effective drawing regions because the decorator
 *  footprint may change.  Allocates a new decorator if needed for the new look,
 *  or removes it for B_NO_BORDER_WINDOW_LOOK.
 *
 *  @param look         The new window_look value.
 *  @param updateRegion Region to accumulate dirty areas into.
 */
void
Window::SetLook(window_look look, BRegion* updateRegion)
{
	fLook = look;

	fContentRegionValid = false;
		// mabye a resize handle was added...
	fEffectiveDrawingRegionValid = false;
		// ...and therefor the drawing region is
		// likely not valid anymore either

	if (!fCurrentStack.IsSet())
		return;

	int32 stackPosition = PositionInStack();

	::Decorator* decorator = Decorator();
	if (decorator == NULL && look != B_NO_BORDER_WINDOW_LOOK) {
		// we need a new decorator
		decorator = gDecorManager.AllocateDecorator(this);
		fCurrentStack->SetDecorator(decorator);
		if (IsFocus())
			decorator->SetFocus(stackPosition, true);
	}

	if (decorator != NULL) {
		DesktopSettings settings(fDesktop);
		decorator->SetLook(stackPosition, settings, look, updateRegion);

		// we might need to resize the window!
		decorator->GetSizeLimits(&fMinWidth, &fMinHeight, &fMaxWidth,
			&fMaxHeight);
		_ObeySizeLimits();
	}

	if (look == B_NO_BORDER_WINDOW_LOOK) {
		// we don't need a decorator for this window
		fCurrentStack->SetDecorator(NULL);
	}
}


/** @brief Sets the window feel and updates flags accordingly.
 *
 *  Clears the subset list when transitioning away from subset modal/floating.
 *  Re-applies valid flags for the new feel and propagates position for
 *  non-normal windows.
 *
 *  @param feel The new window_feel value.
 */
void
Window::SetFeel(window_feel feel)
{
	// if the subset list is no longer needed, clear it
	if ((fFeel == B_MODAL_SUBSET_WINDOW_FEEL
			|| fFeel == B_FLOATING_SUBSET_WINDOW_FEEL)
		&& feel != B_MODAL_SUBSET_WINDOW_FEEL
		&& feel != B_FLOATING_SUBSET_WINDOW_FEEL)
		fSubsets.MakeEmpty();

	fFeel = feel;

	// having modal windows with B_AVOID_FRONT or B_AVOID_FOCUS doesn't
	// make that much sense, so we filter those flags out on demand
	fFlags = fOriginalFlags;
	fFlags &= ValidWindowFlags(fFeel);

	if (!IsNormal()) {
		fFlags |= B_SAME_POSITION_IN_ALL_WORKSPACES;
		_PropagatePosition();
	}
}


/** @brief Sets the window flags, filtering those invalid for the current feel.
 *
 *  Stores the original flags, applies the valid subset, and notifies the
 *  decorator so it can update its appearance (e.g. resize handle).  Enforces
 *  size limits if the decorator's minimum requirements change.
 *
 *  @param flags        The desired set of window flags.
 *  @param updateRegion Region to accumulate dirty areas into.
 */
void
Window::SetFlags(uint32 flags, BRegion* updateRegion)
{
	fOriginalFlags = flags;
	fFlags = flags & ValidWindowFlags(fFeel);
	if (!IsNormal())
		fFlags |= B_SAME_POSITION_IN_ALL_WORKSPACES;

	if ((fFlags & B_SAME_POSITION_IN_ALL_WORKSPACES) != 0)
		_PropagatePosition();

	::Decorator* decorator = Decorator();
	if (decorator == NULL)
		return;

	int32 stackPosition = PositionInStack();
	decorator->SetFlags(stackPosition, flags, updateRegion);

	// we might need to resize the window!
	decorator->GetSizeLimits(&fMinWidth, &fMinHeight, &fMaxWidth, &fMaxHeight);
	_ObeySizeLimits();

// TODO: not sure if we want to do this
#if 0
	if ((fOriginalFlags & kWindowScreenFlag) != (flags & kWindowScreenFlag)) {
		// TODO: disabling needs to be nestable (or we might lose the previous
		// update state)
		if ((flags & kWindowScreenFlag) != 0)
			DisableUpdateRequests();
		else
			EnableUpdateRequests();
	}
#endif
}


/*!	Returns whether or not a window is in the workspace list with the
	specified \a index.
*/
/** @brief Returns whether this window is assigned to the workspace at \a index.
 *  @param index Zero-based workspace index.
 *  @return true if the corresponding bit in the workspace bitmask is set.
 */
bool
Window::InWorkspace(int32 index) const
{
	return (fWorkspaces & (1UL << index)) != 0;
}


/** @brief Returns whether this window can appear at the front of the z-order.
 *  @return false for desktop, menu, or B_AVOID_FRONT windows; true otherwise.
 */
bool
Window::SupportsFront()
{
	if (fFeel == kDesktopWindowFeel
		|| fFeel == kMenuWindowFeel
		|| (fFlags & B_AVOID_FRONT) != 0)
		return false;

	return true;
}


/** @brief Returns whether this window has a modal feel.
 *  @return true for B_MODAL_SUBSET, B_MODAL_APP, or B_MODAL_ALL feel.
 */
bool
Window::IsModal() const
{
	return IsModalFeel(fFeel);
}


/** @brief Returns whether this window has a floating feel.
 *  @return true for B_FLOATING_SUBSET, B_FLOATING_APP, or B_FLOATING_ALL feel.
 */
bool
Window::IsFloating() const
{
	return IsFloatingFeel(fFeel);
}


/** @brief Returns whether this window has a normal (non-modal, non-floating) feel.
 *  @return true for any feel that is not modal or floating.
 */
bool
Window::IsNormal() const
{
	return !IsFloatingFeel(fFeel) && !IsModalFeel(fFeel);
}


/** @brief Returns whether a visible modal window requires this window to be blocked.
 *
 *  Walks the windows above this one in the current workspace and checks
 *  whether any visible modal window includes this window in its subset.
 *
 *  @return true if a modal window blocks interaction with this window.
 */
bool
Window::HasModal() const
{
	for (Window* window = NextWindow(fCurrentWorkspace); window != NULL;
			window = window->NextWindow(fCurrentWorkspace)) {
		if (window->IsHidden() || !window->IsModal())
			continue;

		if (window->HasInSubset(this))
			return true;
	}

	return false;
}


/*!	\brief Returns the windows that's in behind of the backmost position
		this window can get.
	Returns NULL is this window can be the backmost window.

	\param workspace the workspace on which this check should be made. If
		the value is -1, the window's current workspace will be used.
*/
/** @brief Returns the window immediately behind the furthest-back position this window may occupy.
 *
 *  Desktop windows are always backmost (returns NULL for this window when it is
 *  the desktop).  Searches backward from \a window (or from the previous window
 *  in the stack) and returns the first window this window must remain in front of.
 *
 *  @param window    Starting point for the backward search, or NULL to start from the
 *                   previous window in the workspace list.
 *  @param workspace The workspace index to check, or -1 to use the current workspace.
 *  @return The window that constrains how far back this window can go, or NULL
 *          if it can go all the way to the back.
 */
Window*
Window::Backmost(Window* window, int32 workspace)
{
	if (workspace == -1)
		workspace = fCurrentWorkspace;

	ASSERT(workspace != -1);
	if (workspace == -1)
		return NULL;

	// Desktop windows are always backmost
	if (fFeel == kDesktopWindowFeel)
		return NULL;

	if (window == NULL)
		window = PreviousWindow(workspace);

	for (; window != NULL; window = window->PreviousWindow(workspace)) {
		if (window->IsHidden() || window == this)
			continue;

		if (HasInSubset(window))
			return window;
	}

	return NULL;
}


/*!	\brief Returns the window that's in front of the frontmost position
		this window can get.
	Returns NULL if this window can be the frontmost window.

	\param workspace the workspace on which this check should be made. If
		the value is -1, the window's current workspace will be used.
*/
/** @brief Returns the window immediately in front of the frontmost position this window may occupy.
 *
 *  Searches forward from \a first (or the next window in the stack) and returns
 *  the first window that must remain in front of this window.
 *
 *  @param first     Starting point for the forward search, or NULL to start from the
 *                   next window in the workspace list.
 *  @param workspace The workspace index to check, or -1 to use the current workspace.
 *  @return The constraining window, or NULL if this window can be frontmost.
 */
Window*
Window::Frontmost(Window* first, int32 workspace)
{
	if (workspace == -1)
		workspace = fCurrentWorkspace;

	ASSERT(workspace != -1);
	if (workspace == -1)
		return NULL;

	if (fFeel == kDesktopWindowFeel)
		return first ? first : NextWindow(workspace);

	if (first == NULL)
		first = NextWindow(workspace);

	for (Window* window = first; window != NULL;
			window = window->NextWindow(workspace)) {
		if (window->IsHidden() || window == this)
			continue;

		if (window->HasInSubset(this))
			return window;
	}

	return NULL;
}


/** @brief Adds \a window to this window's modal/floating subset.
 *  @param window The window to add.
 *  @return true if the window was added successfully.
 */
bool
Window::AddToSubset(Window* window)
{
	return fSubsets.AddItem(window);
}


/** @brief Removes \a window from this window's modal/floating subset.
 *  @param window The window to remove.
 */
void
Window::RemoveFromSubset(Window* window)
{
	fSubsets.RemoveItem(window);
}


/*!	Returns whether or not a window is in the subset of this window.
	If a window is in the subset of this window, it means it should always
	appear behind this window.
*/
/** @brief Returns whether \a window is in the subset of this window.
 *
 *  A window in the subset must always appear behind this window.  Menu
 *  windows are always above all windows of their application.  The method
 *  handles the fixed ordering of special feel values and delegates to the
 *  subset list for B_MODAL_SUBSET and B_FLOATING_SUBSET feels.
 *
 *  @param window The candidate window to test.
 *  @return true if \a window is in this window's subset.
 */
bool
Window::HasInSubset(const Window* window) const
{
	if (window == NULL || fFeel == window->Feel()
		|| fFeel == B_NORMAL_WINDOW_FEEL)
		return false;

	// Menus are a special case: they will always be on-top of every window
	// of their application
	if (fFeel == kMenuWindowFeel)
		return window->ServerWindow()->App() == ServerWindow()->App();
	if (window->Feel() == kMenuWindowFeel)
		return false;

	// we have a few special feels that have a fixed order

	const int32 kFeels[] = {kPasswordWindowFeel, kWindowScreenFeel,
		B_MODAL_ALL_WINDOW_FEEL, B_FLOATING_ALL_WINDOW_FEEL};

	for (uint32 order = 0;
			order < sizeof(kFeels) / sizeof(kFeels[0]); order++) {
		if (fFeel == kFeels[order])
			return true;
		if (window->Feel() == kFeels[order])
			return false;
	}

	if ((fFeel == B_FLOATING_APP_WINDOW_FEEL
			&& window->Feel() != B_MODAL_APP_WINDOW_FEEL)
		|| fFeel == B_MODAL_APP_WINDOW_FEEL)
		return window->ServerWindow()->App() == ServerWindow()->App();

	return fSubsets.HasItem(window);
}


/*!	\brief Collects all workspaces views in this window and puts it into \a list
*/
/** @brief Collects all WorkspacesView instances in this window's view tree.
 *  @param list Output list to append found views into.
 */
void
Window::FindWorkspacesViews(BObjectList<WorkspacesView>& list) const
{
	int32 count = fWorkspacesViewCount;
	fTopView->FindViews(kWorkspacesViewFlag, (BObjectList<View>&)list, count);
}


/*!	\brief Returns on which workspaces the window should be visible.

	A modal or floating window may be visible on a workspace if one
	of its subset windows is visible there. Floating windows also need
	to have a subset as front window to be visible.
*/
/** @brief Computes the set of workspaces on which this modal/floating window should appear.
 *
 *  Returns B_ALL_WORKSPACES for all-workspace modal/floating feels.  For
 *  app-scoped feels, returns the application's current workspace set.  For
 *  subset feels, unions the workspaces of all visible subset windows (with an
 *  additional constraint for floating subsets requiring a normal front window).
 *
 *  @return A workspace bitmask indicating where this window should be visible.
 */
uint32
Window::SubsetWorkspaces() const
{
	if (fFeel == B_MODAL_ALL_WINDOW_FEEL
		|| fFeel == B_FLOATING_ALL_WINDOW_FEEL)
		return B_ALL_WORKSPACES;

	if (fFeel == B_FLOATING_APP_WINDOW_FEEL) {
		Window* front = fDesktop->FrontWindow();
		if (front != NULL && front->IsNormal()
			&& front->ServerWindow()->App() == ServerWindow()->App())
			return ServerWindow()->App()->Workspaces();

		return 0;
	}

	if (fFeel == B_MODAL_APP_WINDOW_FEEL) {
		uint32 workspaces = ServerWindow()->App()->Workspaces();
		if (workspaces == 0) {
			// The application doesn't seem to have any other windows
			// open or visible - but we'd like to see modal windows
			// anyway, at least when they are first opened.
			return 1UL << fDesktop->CurrentWorkspace();
		}
		return workspaces;
	}

	if (fFeel == B_MODAL_SUBSET_WINDOW_FEEL
		|| fFeel == B_FLOATING_SUBSET_WINDOW_FEEL) {
		uint32 workspaces = 0;
		bool hasNormalFront = false;
		for (int32 i = 0; i < fSubsets.CountItems(); i++) {
			Window* window = fSubsets.ItemAt(i);

			if (!window->IsHidden())
				workspaces |= window->Workspaces();
			if (window == fDesktop->FrontWindow() && window->IsNormal())
				hasNormalFront = true;
		}

		if (fFeel == B_FLOATING_SUBSET_WINDOW_FEEL && !hasNormalFront)
			return 0;

		return workspaces;
	}

	return 0;
}


/*!	Returns whether or not a window is in the subset workspace list with the
	specified \a index.
	See SubsetWorkspaces().
*/
/** @brief Returns whether this window's subset workspaces include the one at \a index.
 *  @param index Zero-based workspace index.
 *  @return true if the subset workspace bitmask has the bit for \a index set.
 */
bool
Window::InSubsetWorkspace(int32 index) const
{
	return (SubsetWorkspaces() & (1UL << index)) != 0;
}


// #pragma mark - static


/*static*/ bool
/** @brief Returns whether \a look is a valid window_look constant.
 *  @param look The look value to validate.
 *  @return true if \a look is a recognised window look.
 */
Window::IsValidLook(window_look look)
{
	return look == B_TITLED_WINDOW_LOOK
		|| look == B_DOCUMENT_WINDOW_LOOK
		|| look == B_MODAL_WINDOW_LOOK
		|| look == B_FLOATING_WINDOW_LOOK
		|| look == B_BORDERED_WINDOW_LOOK
		|| look == B_NO_BORDER_WINDOW_LOOK
		|| look == kDesktopWindowLook
		|| look == kLeftTitledWindowLook;
}


/*static*/ bool
/** @brief Returns whether \a feel is a valid window_feel constant.
 *  @param feel The feel value to validate.
 *  @return true if \a feel is a recognised window feel.
 */
Window::IsValidFeel(window_feel feel)
{
	return feel == B_NORMAL_WINDOW_FEEL
		|| feel == B_MODAL_SUBSET_WINDOW_FEEL
		|| feel == B_MODAL_APP_WINDOW_FEEL
		|| feel == B_MODAL_ALL_WINDOW_FEEL
		|| feel == B_FLOATING_SUBSET_WINDOW_FEEL
		|| feel == B_FLOATING_APP_WINDOW_FEEL
		|| feel == B_FLOATING_ALL_WINDOW_FEEL
		|| feel == kDesktopWindowFeel
		|| feel == kMenuWindowFeel
		|| feel == kWindowScreenFeel
		|| feel == kPasswordWindowFeel
		|| feel == kOffscreenWindowFeel;
}


/*static*/ bool
/** @brief Returns whether \a feel represents a modal window feel.
 *  @param feel The feel value to test.
 *  @return true for B_MODAL_SUBSET, B_MODAL_APP, or B_MODAL_ALL.
 */
Window::IsModalFeel(window_feel feel)
{
	return feel == B_MODAL_SUBSET_WINDOW_FEEL
		|| feel == B_MODAL_APP_WINDOW_FEEL
		|| feel == B_MODAL_ALL_WINDOW_FEEL;
}


/*static*/ bool
/** @brief Returns whether \a feel represents a floating window feel.
 *  @param feel The feel value to test.
 *  @return true for B_FLOATING_SUBSET, B_FLOATING_APP, or B_FLOATING_ALL.
 */
Window::IsFloatingFeel(window_feel feel)
{
	return feel == B_FLOATING_SUBSET_WINDOW_FEEL
		|| feel == B_FLOATING_APP_WINDOW_FEEL
		|| feel == B_FLOATING_ALL_WINDOW_FEEL;
}


/*static*/ uint32
/** @brief Returns the bitmask of all flag bits valid for any window feel.
 *  @return Bitmask of valid window flags.
 */
Window::ValidWindowFlags()
{
	return B_NOT_MOVABLE
		| B_NOT_CLOSABLE
		| B_NOT_ZOOMABLE
		| B_NOT_MINIMIZABLE
		| B_NOT_RESIZABLE
		| B_NOT_H_RESIZABLE
		| B_NOT_V_RESIZABLE
		| B_AVOID_FRONT
		| B_AVOID_FOCUS
		| B_WILL_ACCEPT_FIRST_CLICK
		| B_OUTLINE_RESIZE
		| B_NO_WORKSPACE_ACTIVATION
		| B_NOT_ANCHORED_ON_ACTIVATE
		| B_ASYNCHRONOUS_CONTROLS
		| B_QUIT_ON_WINDOW_CLOSE
		| B_SAME_POSITION_IN_ALL_WORKSPACES
		| B_AUTO_UPDATE_SIZE_LIMITS
		| B_CLOSE_ON_ESCAPE
		| B_NO_SERVER_SIDE_WINDOW_MODIFIERS
		| kWindowScreenFlag
		| kAcceptKeyboardFocusFlag;
}


/*static*/ uint32
/** @brief Returns the valid flags bitmask filtered for the given feel.
 *
 *  For modal feels, B_AVOID_FOCUS and B_AVOID_FRONT are removed.
 *
 *  @param feel The feel whose valid flag mask to compute.
 *  @return Bitmask of valid window flags for \a feel.
 */
Window::ValidWindowFlags(window_feel feel)
{
	uint32 flags = ValidWindowFlags();
	if (IsModalFeel(feel))
		return flags & ~(B_AVOID_FOCUS | B_AVOID_FRONT);

	return flags;
}


// #pragma mark - private


/** @brief Shifts the part of \a region that intersects \a regionToShift by the given offset.
 *
 *  Extracts the common intersection of \a region and \a regionToShift,
 *  removes it from \a region, offsets it, and re-includes it.  Used to
 *  move dirty regions during window move operations.
 *
 *  @param region         The region to modify in place.
 *  @param regionToShift  The region defining which part of \a region to move.
 *  @param xOffset        Horizontal shift amount.
 *  @param yOffset        Vertical shift amount.
 */
void
Window::_ShiftPartOfRegion(BRegion* region, BRegion* regionToShift,
	int32 xOffset, int32 yOffset)
{
	BRegion* common = fRegionPool.GetRegion(*regionToShift);
	if (!common)
		return;
	// see if there is a common part at all
	common->IntersectWith(region);
	if (common->CountRects() > 0) {
		// cut the common part from the region,
		// offset that to destination and include again
		region->Exclude(common);
		common->OffsetBy(xOffset, yOffset);
		region->Include(common);
	}
	fRegionPool.Recycle(common);
}


/** @brief Transfers dirty content to the pending update session and exposes backgrounds.
 *
 *  Skips invisible, empty, or window-screen windows.  Adds \a dirty to the
 *  pending update session, sends an update message if none is in flight, and
 *  immediately draws exposed backgrounds to avoid stamping artifacts.
 *
 *  @param dirty  The dirty region (transferred to the pending update session).
 *  @param expose The exposed region to paint backgrounds for immediately.
 */
void
Window::_TriggerContentRedraw(BRegion& dirty, const BRegion& expose)
{
	if (!IsVisible() || dirty.CountRects() == 0 || (fFlags & kWindowScreenFlag) != 0)
		return;

	// put this into the pending dirty region
	// to eventually trigger a client redraw
	_TransferToUpdateSession(&dirty);

	if (expose.CountRects() > 0) {
		// draw exposed region background right now to avoid stamping artifacts
		if (fDrawingEngine->LockParallelAccess()) {
			bool copyToFrontEnabled = fDrawingEngine->CopyToFrontEnabled();
			fDrawingEngine->SetCopyToFrontEnabled(true);
			fTopView->Draw(fDrawingEngine.Get(), &expose, &fContentRegion, true);
			fDrawingEngine->SetCopyToFrontEnabled(copyToFrontEnabled);
			fDrawingEngine->UnlockParallelAccess();
		}
	}
}


/** @brief Draws the dirty portion of the window border using the decorator.
 *
 *  Intersects the decorator footprint with the visible and dirty regions,
 *  then asks the decorator to draw within that clipped area.  Copies the
 *  result to the front buffer and resyncs the draw state.
 */
void
Window::_DrawBorder()
{
	// this is executed in the window thread, but only
	// in respond to a REDRAW message having been received, the
	// clipping lock is held for reading
	::Decorator* decorator = Decorator();
	if (!decorator)
		return;

	// construct the region of the border that needs redrawing
	BRegion* dirtyBorderRegion = fRegionPool.GetRegion();
	if (!dirtyBorderRegion)
		return;
	GetBorderRegion(dirtyBorderRegion);
	// intersect with our visible region
	dirtyBorderRegion->IntersectWith(&fVisibleRegion);
	// intersect with the dirty region
	dirtyBorderRegion->IntersectWith(&fDirtyRegion);

	DrawingEngine* engine = decorator->GetDrawingEngine();
	if (dirtyBorderRegion->CountRects() > 0 && engine->LockParallelAccess()) {
		engine->ConstrainClippingRegion(dirtyBorderRegion);
		bool copyToFrontEnabled = engine->CopyToFrontEnabled();
		engine->SetCopyToFrontEnabled(false);

		decorator->Draw(dirtyBorderRegion->Frame());

		engine->SetCopyToFrontEnabled(copyToFrontEnabled);
		engine->CopyToFront(*dirtyBorderRegion);

// TODO: remove this once the DrawState stuff is handled
// more cleanly. The reason why this is needed is that
// when the decorator draws strings, a draw state is set
// on the Painter object, and this is were it might get
// out of sync with what the ServerWindow things is the
// current DrawState set on the Painter
fWindow->ResyncDrawState();

		engine->UnlockParallelAccess();
	}
	fRegionPool.Recycle(dirtyBorderRegion);
}


/*!	pre: the clipping is readlocked (this function is
	only called from _TriggerContentRedraw()), which
	in turn is only called from MessageReceived() with
	the clipping lock held
*/
/** @brief Moves the content dirty region into the pending update session.
 *
 *  If the region is non-empty, sets the pending session as in-use and includes
 *  the region.  If no update has been requested yet, sends the update message.
 *  Requires the clipping read lock to be held by the caller.
 *
 *  @param contentDirtyRegion The dirty region to transfer.
 */
void
Window::_TransferToUpdateSession(BRegion* contentDirtyRegion)
{
	if (contentDirtyRegion->CountRects() <= 0)
		return;

//fDrawingEngine->FillRegion(*contentDirtyRegion, sPendingColor);
//snooze(20000);

	// add to pending
	fPendingUpdateSession->SetUsed(true);
	fPendingUpdateSession->Include(contentDirtyRegion);

	if (!fUpdateRequested) {
		// send this to client
		_SendUpdateMessage();
		// the pending region is now the current,
		// though the update does not start until
		// we received BEGIN_UPDATE from the client
	}
}


/** @brief Sends an _UPDATE_ message to the client BWindow if updates are enabled.
 *
 *  If sending fails the dirty region continues to grow until a future attempt
 *  succeeds.  Sets fUpdateRequested and invalidates the effective drawing region.
 */
void
Window::_SendUpdateMessage()
{
	if (!fUpdatesEnabled)
		return;

	BMessage message(_UPDATE_);
	if (ServerWindow()->SendMessageToClient(&message) != B_OK) {
		// If sending the message failed, we'll just keep adding to the dirty
		// region until sending was successful.
		// TODO: we might want to automatically resend this message in this case
		return;
	}

	fUpdateRequested = true;
	fEffectiveDrawingRegionValid = false;
}


/** @brief Begins an update cycle for this window.
 *
 *  Swaps the pending and current update sessions, draws the background of the
 *  dirty region immediately (so it is ready for client drawing), and sends the
 *  update metadata (window geometry and dirty view tokens) to the client via
 *  \a link.  Suppresses copy-to-front during the update.
 *
 *  @param link The PortLink used to reply to the client's AS_BEGIN_UPDATE request.
 */
void
Window::BeginUpdate(BPrivate::PortLink& link)
{
	// NOTE: since we might "shift" parts of the
	// internal dirty regions from the desktop thread
	// in response to Window::ResizeBy(), which
	// might move arround views, the user of this function
	// needs to hold the global clipping lock so that the internal
	// dirty regions are not messed with from the Desktop thread
	// and ServerWindow thread at the same time.

	if (!fUpdateRequested) {
		link.StartMessage(B_ERROR);
		link.Flush();
		fprintf(stderr, "Window::BeginUpdate() - no update requested!\n");
		return;
	}

	// make the pending update session the current update session
	// (toggle the pointers)
	UpdateSession* temp = fCurrentUpdateSession;
	fCurrentUpdateSession = fPendingUpdateSession;
	fPendingUpdateSession = temp;
	fPendingUpdateSession->SetUsed(false);
	// all drawing command from the client
	// will have the dirty region from the update
	// session enforced
	fInUpdate = true;
	fEffectiveDrawingRegionValid = false;

	// TODO: each view could be drawn individually
	// right before carrying out the first drawing
	// command from the client during an update
	// (View::IsBackgroundDirty() can be used
	// for this)
	if (!fContentRegionValid)
		_UpdateContentRegion();

	BRegion* dirty = fRegionPool.GetRegion(
		fCurrentUpdateSession->DirtyRegion());
	if (!dirty) {
		link.StartMessage(B_ERROR);
		link.Flush();
		return;
	}

	dirty->IntersectWith(&VisibleContentRegion());

//if (!fCurrentUpdateSession->IsExpose()) {
////sCurrentColor.red = rand() % 255;
////sCurrentColor.green = rand() % 255;
////sCurrentColor.blue = rand() % 255;
////sPendingColor.red = rand() % 255;
////sPendingColor.green = rand() % 255;
////sPendingColor.blue = rand() % 255;
//fDrawingEngine->FillRegion(*dirty, sCurrentColor);
//snooze(10000);
//}

	link.StartMessage(B_OK);
	// append the current window geometry to the
	// message, the client will need it
	link.Attach<BPoint>(fFrame.LeftTop());
	link.Attach<float>(fFrame.Width());
	link.Attach<float>(fFrame.Height());
	// find and attach all views that intersect with
	// the dirty region
	fTopView->AddTokensForViewsInRegion(link, *dirty, &fContentRegion);
	// mark the end of the token "list"
	link.Attach<int32>(B_NULL_TOKEN);
	link.Flush();

	// supress back to front buffer copies in the drawing engine
	fDrawingEngine->SetCopyToFrontEnabled(false);

	if (fDrawingEngine->LockParallelAccess()) {
		fTopView->Draw(GetDrawingEngine(), dirty, &fContentRegion, true);

		fDrawingEngine->UnlockParallelAccess();
	} // else the background was cleared already

	fRegionPool.Recycle(dirty);
}


/** @brief Ends the current update cycle.
 *
 *  Re-enables copy-to-front, copies the updated dirty region to the front
 *  buffer, marks the current session as unused, and resets the update flag.
 *  If a new pending session accumulated during the update, sends another
 *  update message immediately.
 */
void
Window::EndUpdate()
{
	// NOTE: see comment in _BeginUpdate()

	if (fInUpdate) {
		// reenable copy to front
		fDrawingEngine->SetCopyToFrontEnabled(true);

		BRegion* dirty = fRegionPool.GetRegion(
			fCurrentUpdateSession->DirtyRegion());

		if (dirty) {
			dirty->IntersectWith(&VisibleContentRegion());

			fDrawingEngine->CopyToFront(*dirty);
			fRegionPool.Recycle(dirty);
		}

		fCurrentUpdateSession->SetUsed(false);

		fInUpdate = false;
		fEffectiveDrawingRegionValid = false;
	}
	if (fPendingUpdateSession->IsUsed()) {
		// send this to client
		_SendUpdateMessage();
	} else {
		fUpdateRequested = false;
	}
}


/** @brief Recomputes and caches the content region.
 *
 *  Sets the content region to the full window frame, then excludes the
 *  decorator's footprint so only the client area remains.
 */
void
Window::_UpdateContentRegion()
{
	fContentRegion.Set(fFrame);

	// resize handle
	::Decorator* decorator = Decorator();
	if (decorator)
		fContentRegion.Exclude(&decorator->GetFootprint());

	fContentRegionValid = true;
}


/** @brief Resizes the window to comply with its current size limits.
 *
 *  Normalises the limits (ensures max >= min), then computes how far the
 *  current frame violates them.  Delegates the actual resize to the desktop
 *  (if present) or directly to ResizeBy().
 */
void
Window::_ObeySizeLimits()
{
	// make sure we even have valid size limits
	if (fMaxWidth < fMinWidth)
		fMaxWidth = fMinWidth;

	if (fMaxHeight < fMinHeight)
		fMaxHeight = fMinHeight;

	// Automatically resize the window to fit these new limits
	// if it does not already.

	// On R5, Windows don't automatically resize, but since
	// BWindow::ResizeTo() even honors the limits, I would guess
	// this is a bug that we don't have to adopt.
	// Note that most current apps will do unnecessary resizing
	// after having set the limits, but the overhead is neglible.

	float minWidthDiff = fMinWidth - fFrame.Width();
	float minHeightDiff = fMinHeight - fFrame.Height();
	float maxWidthDiff = fMaxWidth - fFrame.Width();
	float maxHeightDiff = fMaxHeight - fFrame.Height();

	float xDiff = 0.0;
	if (minWidthDiff > 0.0)	// we're currently smaller than minWidth
		xDiff = minWidthDiff;
	else if (maxWidthDiff < 0.0) // we're currently larger than maxWidth
		xDiff = maxWidthDiff;

	float yDiff = 0.0;
	if (minHeightDiff > 0.0) // we're currently smaller than minHeight
		yDiff = minHeightDiff;
	else if (maxHeightDiff < 0.0) // we're currently larger than maxHeight
		yDiff = maxHeightDiff;

	if (fDesktop)
		fDesktop->ResizeWindowBy(this, xDiff, yDiff);
	else
		ResizeBy((int32)xDiff, (int32)yDiff, NULL);
}


// #pragma mark - UpdateSession


/** @brief Constructs an empty, unused UpdateSession. */
Window::UpdateSession::UpdateSession()
	:
	fDirtyRegion(),
	fInUse(false)
{
}


/** @brief Includes \a additionalDirty into this session's dirty region.
 *  @param additionalDirty The region to add.
 */
void
Window::UpdateSession::Include(BRegion* additionalDirty)
{
	fDirtyRegion.Include(additionalDirty);
}


/** @brief Excludes \a dirtyInNextSession from this session's dirty region.
 *  @param dirtyInNextSession The region to remove.
 */
void
Window::UpdateSession::Exclude(BRegion* dirtyInNextSession)
{
	fDirtyRegion.Exclude(dirtyInNextSession);
}


/** @brief Offsets the session's dirty region by (\a x, \a y).
 *  @param x Horizontal offset.
 *  @param y Vertical offset.
 */
void
Window::UpdateSession::MoveBy(int32 x, int32 y)
{
	fDirtyRegion.OffsetBy(x, y);
}


/** @brief Marks the session as used or unused.
 *
 *  When set to unused the dirty region is cleared so it does not carry
 *  stale data into the next update cycle.
 *
 *  @param used true to mark as in-use, false to reset and clear the region.
 */
void
Window::UpdateSession::SetUsed(bool used)
{
	fInUse = used;
	if (!fInUse)
		fDirtyRegion.MakeEmpty();
}


/** @brief Returns the zero-based position of this window within its stack.
 *  @return The stack index, or -1 if the window is not part of a stack.
 */
int32
Window::PositionInStack() const
{
	if (!fCurrentStack.IsSet())
		return -1;
	return fCurrentStack->WindowList().IndexOf(this);
}


/** @brief Removes this window from its current WindowStack.
 *
 *  If the stack has more than one window the window is unlinked, its decorator
 *  tab removed, and the remaining stack is updated (new top-layer window, focus
 *  propagation, look reload).  If \a ownStackNeeded is true a new single-window
 *  stack is created for this window.
 *
 *  @param ownStackNeeded If true, allocate a new stack for this window after removal.
 *  @return false if the window was not part of any stack; true otherwise.
 */
bool
Window::DetachFromWindowStack(bool ownStackNeeded)
{
	// The lock must normally be held but is not held when closing the window.
	//ASSERT_MULTI_WRITE_LOCKED(fDesktop->WindowLocker());

	if (!fCurrentStack.IsSet())
		return false;
	if (fCurrentStack->CountWindows() == 1)
		return true;

	int32 index = PositionInStack();

	if (fCurrentStack->RemoveWindow(this) == false)
		return false;

	BRegion invalidatedRegion;
	::Decorator* decorator = fCurrentStack->Decorator();
	if (decorator != NULL) {
		decorator->RemoveTab(index, &invalidatedRegion);
		decorator->SetTopTab(fCurrentStack->LayerOrder().CountItems() - 1);
	}

	Window* remainingTop = fCurrentStack->TopLayerWindow();
	if (remainingTop != NULL) {
		if (decorator != NULL)
			decorator->SetDrawingEngine(remainingTop->GetDrawingEngine());
		// propagate focus to the decorator
		remainingTop->SetFocus(remainingTop->IsFocus());
		remainingTop->SetLook(remainingTop->Look(), NULL);
	}

	fCurrentStack = NULL;
	if (ownStackNeeded == true)
		_InitWindowStack();
	// propagate focus to the new decorator
	SetFocus(IsFocus());

	if (remainingTop != NULL) {
		invalidatedRegion.Include(&remainingTop->VisibleRegion());
		fDesktop->RebuildAndRedrawAfterWindowChange(remainingTop,
			invalidatedRegion);
	}
	return true;
}


/** @brief Adds \a window to this window's stack.
 *
 *  Moves \a window to match this window's frame, adds it to the stack at the
 *  position immediately after this window, transfers the decorator tab, and
 *  triggers a rebuild/redraw.
 *
 *  @param window The window to stack onto this window.
 *  @return true if the window was successfully added.
 */
bool
Window::AddWindowToStack(Window* window)
{
	ASSERT_MULTI_WRITE_LOCKED(fDesktop->WindowLocker());

	WindowStack* stack = GetWindowStack();
	if (stack == NULL)
		return false;

	BRegion dirty;
	// move window to the own position
	BRect ownFrame = Frame();
	BRect frame = window->Frame();
	float deltaToX = round(ownFrame.left - frame.left);
	float deltaToY = round(ownFrame.top - frame.top);
	frame.OffsetBy(deltaToX, deltaToY);
	float deltaByX = round(ownFrame.right - frame.right);
	float deltaByY = round(ownFrame.bottom - frame.bottom);
	dirty.Include(&window->VisibleRegion());
	window->MoveBy(deltaToX, deltaToY, false);
	window->ResizeBy(deltaByX, deltaByY, &dirty, false);

	// first collect dirt from the window to add
	::Decorator* otherDecorator = window->Decorator();
	if (otherDecorator != NULL)
		dirty.Include(otherDecorator->TitleBarRect());
	::Decorator* decorator = stack->Decorator();
	if (decorator != NULL)
		dirty.Include(decorator->TitleBarRect());

	int32 position = PositionInStack() + 1;
	if (position >= stack->CountWindows())
		position = -1;
	if (stack->AddWindow(window, position) == false)
		return false;
	window->DetachFromWindowStack(false);
	window->fCurrentStack.SetTo(stack);

	if (decorator != NULL) {
		DesktopSettings settings(fDesktop);
		decorator->AddTab(settings, window->Title(), window->Look(),
			window->Flags(), position, &dirty);
	}

	window->SetLook(window->Look(), &dirty);
	fDesktop->RebuildAndRedrawAfterWindowChange(TopLayerStackWindow(), dirty);
	window->SetFocus(window->IsFocus());
	return true;
}


/** @brief Returns the stacked window whose tab is at screen point \a where.
 *
 *  If the decorator reports a valid tab index the corresponding window from the
 *  stack is returned; otherwise this window is returned as the default.
 *
 *  @param where The screen point to hit-test.
 *  @return The window whose tab covers \a where.
 */
Window*
Window::StackedWindowAt(const BPoint& where)
{
	::Decorator* decorator = Decorator();
	if (decorator == NULL)
		return this;

	int tab = decorator->TabAt(where);
	// if we have a decorator we also have a stack
	Window* window = fCurrentStack->WindowAt(tab);
	if (window != NULL)
		return window;
	return this;
}


/** @brief Returns the top-layer window of this window's stack.
 *
 *  If this window has no stack it is its own top-layer window.
 *
 *  @return The top-layer Window in the stack.
 */
Window*
Window::TopLayerStackWindow()
{
	if (!fCurrentStack.IsSet())
		return this;
	return fCurrentStack->TopLayerWindow();
}


/** @brief Returns (or lazily creates) the WindowStack for this window.
 *  @return The WindowStack, or NULL if allocation failed.
 */
WindowStack*
Window::GetWindowStack()
{
	if (!fCurrentStack.IsSet())
		return _InitWindowStack();
	return fCurrentStack;
}


/** @brief Moves this window to the top layer of its stack.
 *
 *  Updates the decorator's drawing engine to this window's engine, reloads the
 *  look, and moves this window to the top of the layer ordering.
 *
 *  @return true if the move succeeded.
 */
bool
Window::MoveToTopStackLayer()
{
	::Decorator* decorator = Decorator();
	if (decorator == NULL)
		return false;
	decorator->SetDrawingEngine(GetDrawingEngine());
	SetLook(Look(), NULL);
	decorator->SetTopTab(PositionInStack());
	return fCurrentStack->MoveToTopLayer(this);
}


/** @brief Moves this window to position \a to within the stack tab order.
 *
 *  @param to       The target tab position.
 *  @param isMoving True if the move is part of an interactive drag.
 *  @return true if the move and decorator tab update both succeeded.
 */
bool
Window::MoveToStackPosition(int32 to, bool isMoving)
{
	if (!fCurrentStack.IsSet())
		return false;
	int32 index = PositionInStack();
	if (fCurrentStack->Move(index, to) == false)
		return false;

	BRegion dirty;
	::Decorator* decorator = Decorator();
	if (decorator && decorator->MoveTab(index, to, isMoving, &dirty) == false)
		return false;

	fDesktop->RebuildAndRedrawAfterWindowChange(this, dirty);
	return true;
}


/** @brief Creates and initialises a new single-window WindowStack for this window.
 *
 *  Allocates a decorator (unless B_NO_BORDER_WINDOW_LOOK) and creates the
 *  stack, adding this window as its sole member.
 *
 *  @return Pointer to the new WindowStack, or NULL on allocation failure.
 */
WindowStack*
Window::_InitWindowStack()
{
	fCurrentStack = NULL;
	::Decorator* decorator = NULL;
	if (fLook != B_NO_BORDER_WINDOW_LOOK)
		decorator = gDecorManager.AllocateDecorator(this);

	WindowStack* stack = new(std::nothrow) WindowStack(decorator);
	if (stack == NULL)
		return NULL;

	if (stack->AddWindow(this) != true) {
		delete stack;
		return NULL;
	}
	fCurrentStack.SetTo(stack, true);
	return stack;
}


/** @brief Constructs a WindowStack owning the given decorator.
 *  @param decorator The decorator to associate with this stack (may be NULL).
 */
WindowStack::WindowStack(::Decorator* decorator)
	:
	fDecorator(decorator)
{

}


/** @brief Destructor. */
WindowStack::~WindowStack()
{
}


/** @brief Replaces the stack's decorator.
 *  @param decorator The new decorator, or NULL to remove it.
 */
void
WindowStack::SetDecorator(::Decorator* decorator)
{
	fDecorator.SetTo(decorator);
}


/** @brief Returns the stack's decorator.
 *  @return Pointer to the Decorator, or NULL if none.
 */
::Decorator*
WindowStack::Decorator()
{
	return fDecorator.Get();
}


/** @brief Returns the topmost window in the layer ordering.
 *  @return The top-layer Window, or NULL if the stack is empty.
 */
Window*
WindowStack::TopLayerWindow() const
{
	return fWindowLayerOrder.ItemAt(fWindowLayerOrder.CountItems() - 1);
}


/** @brief Returns the number of windows in this stack.
 *  @return The window count.
 */
int32
WindowStack::CountWindows()
{
	return fWindowList.CountItems();
}


/** @brief Returns the window at the given tab-order position.
 *  @param index Zero-based tab index.
 *  @return The Window at \a index, or NULL if out of range.
 */
Window*
WindowStack::WindowAt(int32 index)
{
	return fWindowList.ItemAt(index);
}


/** @brief Adds \a window to the stack at the given position.
 *
 *  Also adds the window to the layer ordering list.  Both additions must
 *  succeed; if the layer order addition fails, the tab addition is rolled back.
 *
 *  @param window   The window to add.
 *  @param position Tab position to insert at (-1 to append).
 *  @return true if both additions succeeded.
 */
bool
WindowStack::AddWindow(Window* window, int32 position)
{
	if (position >= 0) {
		if (fWindowList.AddItem(window, position) == false)
			return false;
	} else if (fWindowList.AddItem(window) == false)
		return false;

	if (fWindowLayerOrder.AddItem(window) == false) {
		fWindowList.RemoveItem(window);
		return false;
	}
	return true;
}


/** @brief Removes \a window from both the tab list and the layer order list.
 *  @param window The window to remove.
 *  @return true if the window was found and removed from the tab list.
 */
bool
WindowStack::RemoveWindow(Window* window)
{
	if (fWindowList.RemoveItem(window) == false)
		return false;

	fWindowLayerOrder.RemoveItem(window);
	return true;
}


/** @brief Moves \a window to the top of the layer ordering.
 *  @param window The window to promote to the top layer.
 *  @return true if the move succeeded.
 */
bool
WindowStack::MoveToTopLayer(Window* window)
{
	int32 index = fWindowLayerOrder.IndexOf(window);
	return fWindowLayerOrder.MoveItem(index,
		fWindowLayerOrder.CountItems() - 1);
}


/** @brief Moves a window from tab position \a from to tab position \a to.
 *  @param from The current tab index.
 *  @param to   The target tab index.
 *  @return true if the move succeeded.
 */
bool
WindowStack::Move(int32 from, int32 to)
{
	return fWindowList.MoveItem(from, to);
}
