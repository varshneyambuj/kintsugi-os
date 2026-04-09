/*
 * Copyright 2025, Kintsugi OS Contributors.
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright 2001-2020, Haiku, Inc.
 * Authors:
 *		DarkWyrm, bpmagic@columbus.rr.com
 *		Adi Oanca, adioanca@gmail.com
 *		Stephan Aßmus, superstippi@gmx.de
 *		Axel Dörfler, axeld@pinc-software.de
 *		Brecht Machiels, brecht@mos6581.org
 *		Clemens Zeidler, haiku@clemens-zeidler.de
 *		Tri-Edge AI
 *		Jacob Secunda, secundja@gmail.com
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file Window.h
 *  @brief Core window management class and WindowStack grouping for the app_server. */

#ifndef WINDOW_H
#define WINDOW_H


#include "RegionPool.h"
#include "ServerWindow.h"
#include "View.h"
#include "WindowList.h"

#include <AutoDeleter.h>
#include <ObjectList.h>
#include <Referenceable.h>
#include <Region.h>
#include <String.h>


class Window;


/** @brief Alias for a list of Window pointers used by WindowStack. */
typedef	BObjectList<Window>	StackWindows;


/** @brief Groups a set of tabbed/stacked windows sharing a single decorator. */
class WindowStack : public BReferenceable {
public:
	/** @brief Constructs a WindowStack with the given decorator.
	 *  @param decorator Decorator to use for the stack. */
								WindowStack(::Decorator* decorator);
								~WindowStack();

	/** @brief Replaces the decorator for this stack.
	 *  @param decorator New decorator. */
			void				SetDecorator(::Decorator* decorator);

	/** @brief Returns the decorator used by this stack.
	 *  @return Pointer to the Decorator. */
			::Decorator*		Decorator();

	/** @brief Returns the ordered list of windows in this stack.
	 *  @return Reference to the StackWindows list. */
	const	StackWindows&		WindowList() const { return fWindowList; }

	/** @brief Returns the layer-order list of windows in this stack.
	 *  @return Reference to the layer-order StackWindows list. */
	const	StackWindows&		LayerOrder() const { return fWindowLayerOrder; }

	/** @brief Returns the topmost window in the layer order.
	 *  @return Pointer to the top Window, or NULL if empty. */
			Window*				TopLayerWindow() const;

	/** @brief Returns the total number of windows in this stack.
	 *  @return Window count. */
			int32				CountWindows();

	/** @brief Returns the Window at the given index.
	 *  @param index Zero-based index.
	 *  @return Pointer to the Window, or NULL if out of range. */
			Window*				WindowAt(int32 index);

	/** @brief Adds a window to the stack at the given position.
	 *  @param window   Window to add.
	 *  @param position Insertion position (-1 appends).
	 *  @return true on success. */
			bool				AddWindow(Window* window,
									int32 position = -1);

	/** @brief Removes a window from the stack.
	 *  @param window Window to remove.
	 *  @return true if the window was found and removed. */
			bool				RemoveWindow(Window* window);

	/** @brief Moves the given window to the top of the layer order.
	 *  @param window Window to promote.
	 *  @return true on success. */
			bool				MoveToTopLayer(Window* window);

	/** @brief Moves a window from one layer position to another.
	 *  @param from Source index.
	 *  @param to   Destination index.
	 *  @return true on success. */
			bool				Move(int32 from, int32 to);
private:
			ObjectDeleter< ::Decorator>
								fDecorator;

			StackWindows		fWindowList;
			StackWindows		fWindowLayerOrder;
};


namespace BPrivate {
	class PortLink;
};

class ClickTarget;
class ClientLooper;
class Decorator;
class Desktop;
class DrawingEngine;
class EventDispatcher;
class Screen;
class WindowBehaviour;
class WorkspacesView;

// TODO: move this into a proper place
#define AS_REDRAW 'rdrw'


/** @brief Manages all state for a single on-screen window including regions, update sessions, and input. */
class Window {
public:
	/** @brief Constructs a Window with the given properties.
	 *  @param frame         Initial window frame.
	 *  @param name          Window title.
	 *  @param look          Window look (decoration style).
	 *  @param feel          Window feel (modality / type).
	 *  @param flags         Window flags bitfield.
	 *  @param workspaces    Workspace bitmask.
	 *  @param window        ServerWindow that owns this Window.
	 *  @param drawingEngine Drawing engine to use. */
								Window(const BRect& frame, const char *name,
									window_look look, window_feel feel,
									uint32 flags, uint32 workspaces,
									::ServerWindow* window,
									DrawingEngine* drawingEngine);
	virtual						~Window();

	/** @brief Returns B_OK if the Window was fully initialised.
	 *  @return B_OK or an error code. */
			status_t			InitCheck() const;

	/** @brief Returns the current window frame.
	 *  @return Frame rectangle. */
			BRect				Frame() const { return fFrame; }

	/** @brief Returns the window title string.
	 *  @return Null-terminated title. */
			const char*			Title() const { return fTitle.String(); }

	/** @brief Returns the window_anchor link node for the given list.
	 *  @param index List index (kAllWindowList, etc.).
	 *  @return Reference to the window_anchor. */
			window_anchor&		Anchor(int32 index);

	/** @brief Returns the next window in the given list.
	 *  @param index List index.
	 *  @return Pointer to the next Window, or NULL. */
			Window*				NextWindow(int32 index) const;

	/** @brief Returns the previous window in the given list.
	 *  @param index List index.
	 *  @return Pointer to the previous Window, or NULL. */
			Window*				PreviousWindow(int32 index) const;

	/** @brief Returns the Desktop this window lives on.
	 *  @return Pointer to the Desktop. */
			::Desktop*			Desktop() const { return fDesktop; }

	/** @brief Returns the Decorator for this window.
	 *  @return Pointer to the Decorator, or NULL. */
			::Decorator*		Decorator() const;

	/** @brief Returns the ServerWindow that owns this Window.
	 *  @return Pointer to the ServerWindow. */
			::ServerWindow*		ServerWindow() const { return fWindow; }

	/** @brief Returns the event target for input events.
	 *  @return Reference to the EventTarget. */
			::EventTarget&		EventTarget() const
									{ return fWindow->EventTarget(); }

	/** @brief Reloads the decorator from the current decorator add-on.
	 *  @return true if the decorator was reloaded successfully. */
			bool				ReloadDecor();

	/** @brief Associates this window with a physical screen.
	 *  @param screen Screen to associate with. */
			void				SetScreen(const ::Screen* screen);

	/** @brief Returns the screen this window is currently on.
	 *  @return Pointer to the Screen. */
			const ::Screen*		Screen() const;

			// setting and getting the "hard" clipping, you need to have
			// WriteLock()ed the clipping!
	/** @brief Sets the clipping region from the Desktop's allocation pass.
	 *  @param stillAvailableOnScreen Clipping that remains available on screen. */
			void				SetClipping(BRegion* stillAvailableOnScreen);

			// you need to have ReadLock()ed the clipping!
	/** @brief Returns the visible region (requires read-lock on clipping).
	 *  @return Reference to the visible BRegion. */
	inline	BRegion&			VisibleRegion() { return fVisibleRegion; }

	/** @brief Returns the visible content region (excluding border), computing if needed.
	 *  @return Reference to the visible content BRegion. */
			BRegion&			VisibleContentRegion();

			// TODO: not protected by a lock, but noone should need this anyways
			// make private? when used inside Window, it has the ReadLock()
	/** @brief Fills @a region with the full window region including decorator.
	 *  @param region Region to fill. */
			void				GetFullRegion(BRegion* region);

	/** @brief Fills @a region with just the border/decorator region.
	 *  @param region Region to fill. */
			void				GetBorderRegion(BRegion* region);

	/** @brief Fills @a region with just the content (client) region.
	 *  @param region Region to fill. */
			void				GetContentRegion(BRegion* region);

	/** @brief Moves the window by the given pixel delta.
	 *  @param x          Horizontal delta.
	 *  @param y          Vertical delta.
	 *  @param moveStack  If true, also move other windows in the same stack. */
			void				MoveBy(int32 x, int32 y, bool moveStack = true);

	/** @brief Resizes the window by the given pixel delta.
	 *  @param x           Horizontal delta.
	 *  @param y           Vertical delta.
	 *  @param dirtyRegion Receives the region that must be redrawn.
	 *  @param resizeStack If true, also resize other windows in the same stack. */
			void				ResizeBy(int32 x, int32 y,
									BRegion* dirtyRegion,
									bool resizeStack = true);

	/** @brief Applies an outline drag delta without committing the resize.
	 *  @param delta       Drag delta.
	 *  @param dirtyRegion Receives the invalidated region. */
			void				SetOutlinesDelta(BPoint delta,
									BRegion* dirtyRegion);

	/** @brief Scrolls a child view within the window.
	 *  @param view View to scroll.
	 *  @param dx   Horizontal scroll amount.
	 *  @param dy   Vertical scroll amount. */
			void				ScrollViewBy(View* view, int32 dx, int32 dy);

	/** @brief Sets the root view of this window's view hierarchy.
	 *  @param topView The top-level View. */
			void				SetTopView(View* topView);

	/** @brief Returns the root view of this window.
	 *  @return Pointer to the top-level View. */
			View*				TopView() const { return fTopView.Get(); }

	/** @brief Returns the deepest view at the given screen-space point.
	 *  @param where Point in screen coordinates.
	 *  @return Pointer to the hit View, or NULL. */
			View*				ViewAt(const BPoint& where);

	/** @brief Returns whether this is an offscreen (non-visible) window.
	 *  @return false for normal windows. */
	virtual	bool				IsOffscreenWindow() const { return false; }

	/** @brief Returns the effective drawing region for a view in this window.
	 *  @param view   View to query.
	 *  @param region Receives the effective drawing region. */
			void				GetEffectiveDrawingRegion(View* view,
									BRegion& region);

	/** @brief Returns whether the drawing region for a view has changed since the last query.
	 *  @param view View to check.
	 *  @return true if the drawing region has changed. */
			bool				DrawingRegionChanged(View* view) const;

			// generic version, used by the Desktop
	/** @brief Processes both dirty and expose regions, scheduling redraws as needed.
	 *  @param dirtyRegion  Region with stale content.
	 *  @param exposeRegion Newly exposed region requiring immediate fill. */
			void				ProcessDirtyRegion(const BRegion& dirtyRegion,
									const BRegion& exposeRegion);

	/** @brief Processes a region that is both dirty and exposed.
	 *  @param exposeRegion Region to process. */
			void				ProcessDirtyRegion(const BRegion& exposeRegion)
									{ ProcessDirtyRegion(exposeRegion, exposeRegion); }

	/** @brief Triggers an immediate redraw of the current dirty region. */
			void				RedrawDirtyRegion();

			// can be used from inside classes that don't
			// need to know about Desktop (first version uses Desktop)
	/** @brief Marks a screen-space region as dirty via the Desktop.
	 *  @param regionOnScreen Region in screen coordinates to mark dirty. */
			void				MarkDirty(BRegion& regionOnScreen);

			// these versions do not use the Desktop
	/** @brief Marks content regions dirty and exposes them; does not use Desktop.
	 *  @param dirtyRegion  Dirty content region.
	 *  @param exposeRegion Expose region to process together. */
			void				MarkContentDirty(BRegion& dirtyRegion,
									BRegion& exposeRegion);

	/** @brief Marks a content region dirty asynchronously without an expose region.
	 *  @param dirtyRegion Content region to mark dirty. */
			void				MarkContentDirtyAsync(BRegion& dirtyRegion);

	/** @brief Invalidates a single view's region directly.
	 *  @param view        View to invalidate.
	 *  @param viewRegion  Region within the view to invalidate. */
			void				InvalidateView(View* view, BRegion& viewRegion);

	/** @brief Temporarily disables automatic update request generation. */
			void				DisableUpdateRequests();

	/** @brief Re-enables automatic update request generation. */
			void				EnableUpdateRequests();

	/** @brief Begins an update session, writing the dirty region into a PortLink.
	 *  @param link PortLink to receive the update region. */
			void				BeginUpdate(BPrivate::PortLink& link);

	/** @brief Ends the current update session, allowing new dirty regions to accumulate. */
			void				EndUpdate();

	/** @brief Returns whether an update session is currently active.
	 *  @return true if inside BeginUpdate/EndUpdate. */
			bool				InUpdate() const
									{ return fInUpdate; }

	/** @brief Returns whether the window has pending dirty content needing an update.
	 *  @return true if an update has been requested. */
			bool				NeedsUpdate() const
									{ return fUpdateRequested; }

	/** @brief Returns the drawing engine for this window.
	 *  @return Pointer to the DrawingEngine. */
			DrawingEngine*		GetDrawingEngine() const
									{ return fDrawingEngine.Get(); }

			// managing a region pool
	/** @brief Returns this window's region pool.
	 *  @return Pointer to the RegionPool. */
			::RegionPool*		RegionPool()
									{ return &fRegionPool; }

	/** @brief Gets a blank BRegion from the pool.
	 *  @return Pointer to a usable BRegion. */
	inline	BRegion*			GetRegion()
									{ return fRegionPool.GetRegion(); }

	/** @brief Gets a BRegion from the pool initialised as a copy of @a copy.
	 *  @param copy Source region to copy.
	 *  @return Pointer to the copied BRegion. */
	inline	BRegion*			GetRegion(const BRegion& copy)
									{ return fRegionPool.GetRegion(copy); }

	/** @brief Returns a BRegion to the pool.
	 *  @param region Region to recycle. */
	inline	void				RecycleRegion(BRegion* region)
									{ fRegionPool.Recycle(region); }

	/** @brief Copies a region of content within the window to a new position.
	 *  @param region  Region to copy.
	 *  @param xOffset Horizontal offset for the copy.
	 *  @param yOffset Vertical offset for the copy. */
			void				CopyContents(BRegion* region,
									int32 xOffset, int32 yOffset);

	/** @brief Handles a mouse-down event in this window.
	 *  @param message         Message carrying event details.
	 *  @param where           Position of the click in screen coordinates.
	 *  @param lastClickTarget Last click target for multi-click detection.
	 *  @param clickCount      Receives the consecutive click count.
	 *  @param _clickTarget    Receives the current click target. */
			void				MouseDown(BMessage* message, BPoint where,
									const ClickTarget& lastClickTarget,
									int32& clickCount,
									ClickTarget& _clickTarget);

	/** @brief Handles a mouse-up event in this window.
	 *  @param message    Message carrying event details.
	 *  @param where      Position of the release in screen coordinates.
	 *  @param _viewToken Receives the token of the view under the cursor. */
			void				MouseUp(BMessage* message, BPoint where,
									int32* _viewToken);

	/** @brief Handles a mouse-moved event in this window.
	 *  @param message            Message carrying event details.
	 *  @param where              Current cursor position in screen coordinates.
	 *  @param _viewToken         Receives the token of the view under the cursor.
	 *  @param isLatestMouseMoved true if this is the most recent mouse-moved event.
	 *  @param isFake             true if this is a synthesised (fake) event. */
			void				MouseMoved(BMessage* message, BPoint where,
									int32* _viewToken, bool isLatestMouseMoved,
									bool isFake);

	/** @brief Handles a modifier-key state change.
	 *  @param modifiers New modifier flags bitfield. */
			void				ModifiersChanged(int32 modifiers);

			// some hooks to inform the client window
			// TODO: move this to ServerWindow maybe?
	/** @brief Notifies the client when the window's workspace becomes active/inactive.
	 *  @param index  Workspace index.
	 *  @param active true if the workspace is now active. */
			void				WorkspaceActivated(int32 index, bool active);

	/** @brief Notifies the client that the window's workspace assignment changed.
	 *  @param oldWorkspaces Previous workspace bitmask.
	 *  @param newWorkspaces New workspace bitmask. */
			void				WorkspacesChanged(uint32 oldWorkspaces,
									uint32 newWorkspaces);

	/** @brief Notifies the client that the window gained or lost activation.
	 *  @param active true if the window is now active. */
			void				Activated(bool active);

			// changing some properties
	/** @brief Changes the window title.
	 *  @param name  New title string.
	 *  @param dirty Receives the region that needs to be redrawn. */
			void				SetTitle(const char* name, BRegion& dirty);

	/** @brief Sets or clears focus for this window.
	 *  @param focus true to give focus, false to remove it. */
			void				SetFocus(bool focus);

	/** @brief Returns whether this window currently has keyboard focus.
	 *  @return true if focused. */
			bool				IsFocus() const { return fIsFocus; }

	/** @brief Hides or shows this window.
	 *  @param hidden true to hide, false to show. */
			void				SetHidden(bool hidden);

	/** @brief Returns whether the window is hidden.
	 *  @return true if hidden. */
	inline	bool				IsHidden() const { return fHidden; }

	/** @brief Sets the show level (positive values hide, 0 shows).
	 *  @param showLevel New show level. */
			void				SetShowLevel(int32 showLevel);

	/** @brief Returns the current show level.
	 *  @return Show level value. */
	inline	int32				ShowLevel() const { return fShowLevel; }

	/** @brief Minimises or restores the window.
	 *  @param minimized true to minimise, false to restore. */
			void				SetMinimized(bool minimized);

	/** @brief Returns whether the window is minimised.
	 *  @return true if minimised. */
	inline	bool				IsMinimized() const { return fMinimized; }

	/** @brief Sets both the current and prior workspace to the given index.
	 *  @param index New workspace index. */
			void				SetCurrentWorkspace(int32 index)
									{ fCurrentWorkspace = index; fPriorWorkspace = index; }

	/** @brief Returns the index of the currently active workspace for this window.
	 *  @return Workspace index. */
			int32				CurrentWorkspace() const
									{ return fCurrentWorkspace; }

	/** @brief Returns whether this window is currently visible on screen.
	 *  @return true if visible. */
			bool				IsVisible() const;

	/** @brief Sets the prior (previous) workspace index.
	 *  @param index Workspace index to record as prior. */
			void				SetPriorWorkspace(int32 index)
									{ fPriorWorkspace = index; }

	/** @brief Returns the workspace index recorded before the most recent switch.
	 *  @return Prior workspace index. */
			int32				PriorWorkspace() const
									{ return fPriorWorkspace; }

	/** @brief Returns whether a drag operation is in progress for this window.
	 *  @return true if dragging. */
			bool				IsDragging() const;

	/** @brief Returns whether a resize operation is in progress for this window.
	 *  @return true if resizing. */
			bool				IsResizing() const;

	/** @brief Sets the minimum and maximum allowed window dimensions.
	 *  @param minWidth  Minimum width in pixels.
	 *  @param maxWidth  Maximum width in pixels.
	 *  @param minHeight Minimum height in pixels.
	 *  @param maxHeight Maximum height in pixels. */
			void				SetSizeLimits(int32 minWidth, int32 maxWidth,
									int32 minHeight, int32 maxHeight);

	/** @brief Returns the minimum and maximum allowed window dimensions.
	 *  @param minWidth  Receives minimum width.
	 *  @param maxWidth  Receives maximum width.
	 *  @param minHeight Receives minimum height.
	 *  @param maxHeight Receives maximum height. */
			void				GetSizeLimits(int32* minWidth, int32* maxWidth,
									int32* minHeight, int32* maxHeight) const;

								// 0.0 -> left .... 1.0 -> right
	/** @brief Sets the tab (title bar) location within the available range.
	 *  @param location  Normalised position (0.0 = left, 1.0 = right).
	 *  @param isShifting true if the tab is being dragged interactively.
	 *  @param dirty     Receives the region that must be redrawn.
	 *  @return true if the tab location changed. */
			bool				SetTabLocation(float location, bool isShifting,
									BRegion& dirty);

	/** @brief Returns the current normalised tab location.
	 *  @return Tab location (0.0–1.0). */
			float				TabLocation() const;

	/** @brief Applies decorator settings from a BMessage.
	 *  @param settings Settings message.
	 *  @param dirty    Receives the region that must be redrawn.
	 *  @return true if any settings changed. */
			bool				SetDecoratorSettings(const BMessage& settings,
													 BRegion& dirty);

	/** @brief Retrieves current decorator settings into a BMessage.
	 *  @param settings Receives current settings.
	 *  @return true on success. */
			bool				GetDecoratorSettings(BMessage* settings);

	/** @brief Activates or deactivates the decorator highlight.
	 *  @param active true to highlight, false to remove highlight. */
			void				HighlightDecorator(bool active);

	/** @brief Notifies the window that system fonts have changed.
	 *  @param updateRegion Receives the region that must be redrawn. */
			void				FontsChanged(BRegion* updateRegion);

	/** @brief Notifies the window that system colors have changed.
	 *  @param updateRegion Receives the region that must be redrawn. */
			void				ColorsChanged(BRegion* updateRegion);

	/** @brief Changes the window look (decoration style).
	 *  @param look         New window_look constant.
	 *  @param updateRegion Receives the region that must be redrawn. */
			void				SetLook(window_look look,
									BRegion* updateRegion);

	/** @brief Changes the window feel (modality / type).
	 *  @param feel New window_feel constant. */
			void				SetFeel(window_feel feel);

	/** @brief Changes the window flags.
	 *  @param flags        New flags bitfield.
	 *  @param updateRegion Receives the region that must be redrawn. */
			void				SetFlags(uint32 flags, BRegion* updateRegion);

	/** @brief Returns the window look.
	 *  @return window_look constant. */
			window_look			Look() const { return fLook; }

	/** @brief Returns the window feel.
	 *  @return window_feel constant. */
			window_feel			Feel() const { return fFeel; }

	/** @brief Returns the window flags.
	 *  @return Flags bitfield. */
			uint32				Flags() const { return fFlags; }

			// window manager stuff
	/** @brief Returns the workspace bitmask for this window.
	 *  @return Workspace bitmask. */
			uint32				Workspaces() const { return fWorkspaces; }

	/** @brief Sets the workspace bitmask for this window.
	 *  @param workspaces New workspace bitmask. */
			void				SetWorkspaces(uint32 workspaces)
									{ fWorkspaces = workspaces; }

	/** @brief Returns whether this window is present on the given workspace.
	 *  @param index Zero-based workspace index.
	 *  @return true if present on that workspace. */
			bool				InWorkspace(int32 index) const;

	/** @brief Returns whether this window can receive focus (supports front).
	 *  @return true if the window can be brought to front. */
			bool				SupportsFront();

	/** @brief Returns whether this window has a modal feel.
	 *  @return true if modal. */
			bool				IsModal() const;

	/** @brief Returns whether this window has a floating feel.
	 *  @return true if floating. */
			bool				IsFloating() const;

	/** @brief Returns whether this window has a normal feel.
	 *  @return true if normal. */
			bool				IsNormal() const;

	/** @brief Returns whether a modal window is blocking this window.
	 *  @return true if a modal child exists. */
			bool				HasModal() const;

	/** @brief Returns the frontmost window in the given workspace reachable from @a first.
	 *  @param first     Starting window for search (NULL = use window list head).
	 *  @param workspace Workspace index (-1 = current).
	 *  @return Pointer to the frontmost Window, or NULL. */
			Window*				Frontmost(Window* first = NULL,
									int32 workspace = -1);

	/** @brief Returns the backmost window in the given workspace reachable from @a first.
	 *  @param first     Starting window for search (NULL = use window list head).
	 *  @param workspace Workspace index (-1 = current).
	 *  @return Pointer to the backmost Window, or NULL. */
			Window*				Backmost(Window* first = NULL,
									int32 workspace = -1);

	/** @brief Adds a window to this window's subset.
	 *  @param window Window to add.
	 *  @return true on success. */
			bool				AddToSubset(Window* window);

	/** @brief Removes a window from this window's subset.
	 *  @param window Window to remove. */
			void				RemoveFromSubset(Window* window);

	/** @brief Returns whether the given window is in this window's subset.
	 *  @param window Window to check.
	 *  @return true if in the subset. */
			bool				HasInSubset(const Window* window) const;

	/** @brief Returns whether this window and @a window share the same subset.
	 *  @param window Window to compare.
	 *  @return true if subsets are identical. */
			bool				SameSubset(Window* window);

	/** @brief Returns the combined workspace bitmask of this window's subset.
	 *  @return Workspace bitmask. */
			uint32				SubsetWorkspaces() const;

	/** @brief Returns whether this window is in the given workspace via its subset.
	 *  @param index Workspace index.
	 *  @return true if the subset covers that workspace. */
			bool				InSubsetWorkspace(int32 index) const;

	/** @brief Returns whether this window contains any WorkspacesView instances.
	 *  @return true if fWorkspacesViewCount > 0. */
			bool				HasWorkspacesViews() const
									{ return fWorkspacesViewCount != 0; }

	/** @brief Increments the count of WorkspacesView instances in this window. */
			void				AddWorkspacesView()
									{ fWorkspacesViewCount++; }

	/** @brief Decrements the count of WorkspacesView instances in this window. */
			void				RemoveWorkspacesView()
									{ fWorkspacesViewCount--; }

	/** @brief Collects all WorkspacesView instances in this window's view tree.
	 *  @param list List that receives the found WorkspacesView pointers. */
			void				FindWorkspacesViews(
									BObjectList<WorkspacesView>& list) const;

	/** @brief Returns whether the given window_look constant is valid.
	 *  @param look window_look to test.
	 *  @return true if valid. */
	static	bool				IsValidLook(window_look look);

	/** @brief Returns whether the given window_feel constant is valid.
	 *  @param feel window_feel to test.
	 *  @return true if valid. */
	static	bool				IsValidFeel(window_feel feel);

	/** @brief Returns whether the given window_feel is a modal feel.
	 *  @param feel window_feel to test.
	 *  @return true if modal. */
	static	bool				IsModalFeel(window_feel feel);

	/** @brief Returns whether the given window_feel is a floating feel.
	 *  @param feel window_feel to test.
	 *  @return true if floating. */
	static	bool				IsFloatingFeel(window_feel feel);

	/** @brief Returns all valid window flags for any feel.
	 *  @return Bitfield of valid flags. */
	static	uint32				ValidWindowFlags();

	/** @brief Returns the valid window flags for the given feel.
	 *  @param feel window_feel to query.
	 *  @return Bitfield of valid flags for that feel. */
	static	uint32				ValidWindowFlags(window_feel feel);

			// Window stack methods.
	/** @brief Returns the WindowStack this window belongs to.
	 *  @return Pointer to the WindowStack. */
			WindowStack*		GetWindowStack();

	/** @brief Detaches this window from its current WindowStack.
	 *  @param ownStackNeeded If true, create a new single-window stack.
	 *  @return true on success. */
			bool				DetachFromWindowStack(
									bool ownStackNeeded = true);

	/** @brief Adds another window to this window's stack.
	 *  @param window Window to add to the stack.
	 *  @return true on success. */
			bool				AddWindowToStack(Window* window);

	/** @brief Returns the stacked window at the given screen point.
	 *  @param where Point in screen coordinates.
	 *  @return Pointer to the topmost stacked Window at that point, or NULL. */
			Window*				StackedWindowAt(const BPoint& where);

	/** @brief Returns the topmost window in the layer order of this window's stack.
	 *  @return Pointer to the top Window. */
			Window*				TopLayerStackWindow();

	/** @brief Returns this window's position (index) within its stack.
	 *  @return Zero-based stack position. */
			int32				PositionInStack() const;

	/** @brief Moves this window to the top layer of its stack.
	 *  @return true on success. */
			bool				MoveToTopStackLayer();

	/** @brief Moves this window to a specific index in its stack.
	 *  @param index    Target position in the stack.
	 *  @param isMoving true if this is part of an interactive move operation.
	 *  @return true on success. */
			bool				MoveToStackPosition(int32 index,
									bool isMoving);
protected:
			void				_ShiftPartOfRegion(BRegion* region,
									BRegion* regionToShift, int32 xOffset,
									int32 yOffset);

			// different types of drawing
			void				_TriggerContentRedraw(BRegion& dirty,
									const BRegion& expose = BRegion());
			void				_DrawBorder();

			// handling update sessions
			void				_TransferToUpdateSession(
									BRegion* contentDirtyRegion);
			void				_SendUpdateMessage();

			void				_UpdateContentRegion();

			void				_ObeySizeLimits();
			void				_PropagatePosition();

			BString				fTitle;
			// TODO: no fp rects anywhere
			BRect				fFrame;
			const ::Screen*		fScreen;

			window_anchor		fAnchor[kListCount];

			// the visible region is only recalculated from the
			// Desktop thread, when using it, Desktop::ReadLockClipping()
			// has to be called

			BRegion				fVisibleRegion;
			BRegion				fVisibleContentRegion;

			// Our part of the "global" dirty region (what needs to be redrawn).
			// It is calculated from the desktop thread, but we can write to it when we read locked
			// the clipping, since it is local and the desktop thread is blocked.
			BRegion				fDirtyRegion;

			// Subset of the dirty region that is newly exposed. While the dirty region is merely
			// showing out of date data on screen, this subset of it is showing remains of other
			// windows. To avoid glitches, it must be set to a reasonable state as fast as possible,
			// without waiting for a roundtrip to the window's Draw() methods. So it will be filled
			// using background color and view bitmap, which can all be done without leaving
			// app_server.
			BRegion				fExposeRegion;

			// caching local regions
			BRegion				fContentRegion;
			BRegion				fEffectiveDrawingRegion;

			bool				fVisibleContentRegionValid : 1;
			bool				fContentRegionValid : 1;
			bool				fEffectiveDrawingRegionValid : 1;

			::RegionPool		fRegionPool;

			BObjectList<Window> fSubsets;

			ObjectDeleter<WindowBehaviour>
								fWindowBehaviour;
			ObjectDeleter<View>	fTopView;
			::ServerWindow*		fWindow;
			ObjectDeleter<DrawingEngine>
								fDrawingEngine;
			::Desktop*			fDesktop;

			// The synchronization, which client drawing commands
			// belong to the redraw of which dirty region is handled
			// through an UpdateSession. When the client has
			// been informed that it should redraw stuff, then
			// this is the current update session. All new
			// redraw requests from the Desktop will go
			// into the pending update session.
	/** @brief Tracks one active or pending update sweep for dirty-region coordination. */
	class UpdateSession {
	public:
									UpdateSession();

	/** @brief Adds a region to the session's dirty set.
	 *  @param additionalDirty Region to merge into the dirty set. */
				void				Include(BRegion* additionalDirty);

	/** @brief Removes a region from this session, deferring it to the next session.
	 *  @param dirtyInNextSession Region to move into the pending session. */
				void				Exclude(BRegion* dirtyInNextSession);

	/** @brief Returns the mutable dirty region for this session.
	 *  @return Reference to the dirty BRegion. */
		inline	BRegion&			DirtyRegion()
										{ return fDirtyRegion; }

	/** @brief Offsets all rectangles in the dirty region by the given delta.
	 *  @param x Horizontal offset.
	 *  @param y Vertical offset. */
				void				MoveBy(int32 x, int32 y);

	/** @brief Marks the session as in use or completed.
	 *  @param used true if the session is active. */
				void				SetUsed(bool used);

	/** @brief Returns whether this session is currently active.
	 *  @return true if in use. */
		inline	bool				IsUsed() const
										{ return fInUse; }

	private:
				BRegion				fDirtyRegion;
				bool				fInUse;
	};

			UpdateSession		fUpdateSessions[2];
			UpdateSession*		fCurrentUpdateSession;
			UpdateSession*		fPendingUpdateSession;
			// these two flags are supposed to ensure a sane
			// and consistent update session
			bool				fUpdateRequested : 1;
			bool				fInUpdate : 1;
			bool				fUpdatesEnabled : 1;

			bool				fHidden : 1;
			int32				fShowLevel;
			bool				fMinimized : 1;
			bool				fIsFocus : 1;

			window_look			fLook;
			window_feel			fFeel;
			uint32				fOriginalFlags;
			uint32				fFlags;
			uint32				fWorkspaces;
			int32				fCurrentWorkspace;
			int32				fPriorWorkspace;

			int32				fMinWidth;
			int32				fMaxWidth;
			int32				fMinHeight;
			int32				fMaxHeight;

			int32				fWorkspacesViewCount;

		friend class DecorManager;

private:
			WindowStack*		_InitWindowStack();

			BReference<WindowStack>		fCurrentStack;
};


#endif // WINDOW_H
