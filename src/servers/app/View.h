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
 * Copyright (c) 2001-2015, Haiku, Inc.
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Adi Oanca <adioanca@gmail.com>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Marcus Overhagen <marcus@overhagen.de>
 *		Adrien Destugues <pulkomandy@pulkomandy.tk>
 *		Julian Harnath <julian.harnath@rwth-aachen.de>
 *		Joseph Groover <looncraz@looncraz.net>
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file View.h
 *  @brief Server-side view node in the window view hierarchy, carrying clipping and drawing state. */

#ifndef	VIEW_H
#define VIEW_H


#include "Canvas.h"
#include "IntRect.h"

#include <AutoDeleter.h>
#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <ObjectList.h>
#include <Region.h>
#include <Referenceable.h>
#include <String.h>

class BList;
class BMessage;

namespace BPrivate {
	class PortLink;
};

class DrawingEngine;
class Overlay;
class Window;
class ServerBitmap;
class ServerCursor;
class ServerPicture;
class BGradient;

/** @brief Represents one node in the server-side view tree, managing layout, clipping, and events. */
class View: public Canvas {
public:
	/** @brief Constructs a View with the given geometry and properties.
	 *  @param frame          Frame in parent coordinate space.
	 *  @param scrollingOffset Scrolling offset for the local coordinate system.
	 *  @param name           View name for identification.
	 *  @param token          Unique token for this view.
	 *  @param resizeMode     Resize mode flags.
	 *  @param flags          View flags (B_WILL_DRAW, etc.). */
							View(IntRect frame, IntPoint scrollingOffset,
								const char* name, int32 token,
								uint32 resizeMode, uint32 flags);
	virtual					~View();

	/** @brief Returns the unique token identifying this view.
	 *  @return Token value. */
			int32			Token() const
								{ return fToken; }

	/** @brief Returns the view's frame in parent coordinate space.
	 *  @return Frame rectangle. */
			IntRect			Frame() const
								{ return fFrame; }

	/** @brief Returns the view's local bounds rectangle.
	 *  @return Bounds rectangle. */
	virtual	IntRect			Bounds() const;

	/** @brief Sets the resize mode flags.
	 *  @param resizeMode New resize mode. */
			void			SetResizeMode(uint32 resizeMode)
								{ fResizeMode = resizeMode; }

	/** @brief Sets the view's name.
	 *  @param string New name string. */
			void			SetName(const char* string);

	/** @brief Returns the view's name.
	 *  @return Null-terminated name string. */
			const char*		Name() const
								{ return fName.String(); }

	/** @brief Sets the view flags (B_WILL_DRAW, etc.).
	 *  @param flags New flags bitfield. */
			void			SetFlags(uint32 flags);

	/** @brief Returns the current view flags.
	 *  @return Flags bitfield. */
			uint32			Flags() const
								{ return fFlags; }

	/** @brief Returns the scrolling offset applied to the local coordinate system.
	 *  @return Scrolling offset as an IntPoint. */
	inline	IntPoint		ScrollingOffset() const
								{ return fScrollingOffset; }

			// converts the given frame up the view hierarchy and
			// clips to each views bounds
	/** @brief Clips and converts a rectangle through the view hierarchy to the top-level visible area.
	 *  @param bounds Rectangle to clip in place. */
			void			ConvertToVisibleInTopView(IntRect* bounds) const;

	/** @brief Called when this view is attached to a Window.
	 *  @param window The Window this view is now part of. */
	virtual	void			AttachedToWindow(::Window* window);

	/** @brief Called when this view is detached from its Window. */
	virtual void			DetachedFromWindow();

	/** @brief Returns the Window this view is attached to.
	 *  @return Pointer to the Window, or NULL. */
			::Window*		Window() const { return fWindow; }

			// Shorthands for opaque Window access
	/** @brief Returns the DrawingEngine of the parent Window.
	 *  @return Pointer to DrawingEngine. */
			DrawingEngine*	GetDrawingEngine() const;

	/** @brief Looks up a ServerPicture by token through the parent Window.
	 *  @param token Picture token.
	 *  @return Pointer to the ServerPicture, or NULL. */
			ServerPicture*	GetPicture(int32 token) const;

	/** @brief Re-synchronises the drawing state for this view from the server. */
			void			ResyncDrawState();

	/** @brief Updates the current drawing region to reflect clipping changes. */
			void			UpdateCurrentDrawingRegion();

			// tree stuff
	/** @brief Adds a child view to this view's subtree.
	 *  @param view View to add. */
			void			AddChild(View* view);

	/** @brief Removes a child view from this view's subtree.
	 *  @param view View to remove.
	 *  @return true if the view was found and removed. */
			bool			RemoveChild(View* view);

	/** @brief Returns whether the given view is an ancestor of this view.
	 *  @param candidate View to test.
	 *  @return true if candidate is a direct or indirect parent. */
	inline	bool			HasParent(View* candidate) const
							{
								return fParent == candidate
									|| (fParent != NULL
										&& fParent->HasParent(candidate));
							}

	/** @brief Returns this view's direct parent.
	 *  @return Pointer to the parent View, or NULL for the root. */
	inline	View*			Parent() const
								{ return fParent; }

	/** @brief Returns the first child view.
	 *  @return Pointer to the first child, or NULL. */
	inline	View*			FirstChild() const
								{ return fFirstChild; }

	/** @brief Returns the last child view.
	 *  @return Pointer to the last child, or NULL. */
	inline	View*			LastChild() const
								{ return fLastChild; }

	/** @brief Returns the previous sibling view.
	 *  @return Pointer to the previous sibling, or NULL. */
	inline	View*			PreviousSibling() const
								{ return fPreviousSibling; }

	/** @brief Returns the next sibling view.
	 *  @return Pointer to the next sibling, or NULL. */
	inline	View*			NextSibling() const
								{ return fNextSibling; }

	/** @brief Returns the topmost ancestor of this view.
	 *  @return Pointer to the root view. */
			View*			TopView();

	/** @brief Returns the number of child views.
	 *  @param deep If true, counts all descendants recursively.
	 *  @return Child count. */
			uint32			CountChildren(bool deep = false) const;

	/** @brief Collects tokens for all child views into a BList.
	 *  @param tokenMap List that receives the child tokens. */
			void			CollectTokensForChildren(BList* tokenMap) const;

	/** @brief Finds all views matching the given flags.
	 *  @param flags   Flags to match.
	 *  @param list    List that receives matching views.
	 *  @param left    Decremented for each match; stops at zero. */
			void			FindViews(uint32 flags, BObjectList<View>& list,
								int32& left);

	/** @brief Returns whether the given view is in this view's subtree.
	 *  @param view View to search for.
	 *  @return true if found. */
			bool			HasView(View* view);

	/** @brief Returns the deepest view at the given point.
	 *  @param where Point in screen coordinates.
	 *  @return Pointer to the hit view, or NULL. */
			View*			ViewAt(const BPoint& where);

public:
	/** @brief Moves the view by the given delta and reports the dirty region.
	 *  @param dx           Horizontal delta in pixels.
	 *  @param dy           Vertical delta in pixels.
	 *  @param dirtyRegion  Receives the region that must be redrawn. */
			void			MoveBy(int32 dx, int32 dy,
								BRegion* dirtyRegion);

	/** @brief Resizes the view by the given delta and reports the dirty region.
	 *  @param dx           Horizontal size delta.
	 *  @param dy           Vertical size delta.
	 *  @param dirtyRegion  Receives the region that must be redrawn. */
			void			ResizeBy(int32 dx, int32 dy,
								BRegion* dirtyRegion);

	/** @brief Scrolls the view contents and reports the dirty region.
	 *  @param dx           Horizontal scroll amount.
	 *  @param dy           Vertical scroll amount.
	 *  @param dirtyRegion  Receives the region that must be redrawn. */
			void			ScrollBy(int32 dx, int32 dy,
								BRegion* dirtyRegion);

	/** @brief Adjusts layout when the parent is resized.
	 *  @param dx           Horizontal resize delta of the parent.
	 *  @param dy           Vertical resize delta of the parent.
	 *  @param dirtyRegion  Receives the region that must be redrawn. */
			void			ParentResized(int32 dx, int32 dy,
								BRegion* dirtyRegion);

	/** @brief Copies a rectangle of pixels within the view.
	 *  @param src                    Source rectangle in local coordinates.
	 *  @param dst                    Destination rectangle in local coordinates.
	 *  @param windowContentClipping  Window clipping applied during the copy. */
			void			CopyBits(IntRect src, IntRect dst,
								BRegion& windowContentClipping);

	/** @brief Returns the local (non-screen) clipping region.
	 *  @return Reference to the local BRegion. */
			const BRegion&	LocalClipping() const { return fLocalClipping; }

	/** @brief Returns the view's background color.
	 *  @return Reference to the background rgb_color. */
			const rgb_color& ViewColor() const
								{ return fViewColor; }

	/** @brief Sets the view's background color.
	 *  @param color New background color. */
			void			SetViewColor(const rgb_color& color)
								{ fViewColor = color; }

	/** @brief Propagates a UI color change to this view.
	 *  @param which Updated color constant.
	 *  @param color New color value. */
			void			ColorUpdated(color_which which, rgb_color color);

	/** @brief Ties the view background to a system UI color.
	 *  @param which UI color constant.
	 *  @param tint  Tint factor applied to the UI color. */
			void			SetViewUIColor(color_which which, float tint);

	/** @brief Returns the UI color constant currently backing the view color.
	 *  @param tint Receives the current tint factor.
	 *  @return color_which constant, or B_NO_COLOR if not set. */
			color_which		ViewUIColor(float* tint);

	/** @brief Returns the background bitmap for this view.
	 *  @return Pointer to the ServerBitmap, or NULL. */
			ServerBitmap*	ViewBitmap() const
								{ return fViewBitmap; }

	/** @brief Sets a background bitmap for this view.
	 *  @param bitmap       Bitmap to use as background.
	 *  @param sourceRect   Source rectangle within the bitmap.
	 *  @param destRect     Destination rectangle in view coordinates.
	 *  @param resizingMode Bitmap resizing/tiling mode.
	 *  @param options      Additional rendering options. */
			void			SetViewBitmap(ServerBitmap* bitmap,
								IntRect sourceRect, IntRect destRect,
								int32 resizingMode, int32 options);

	/** @brief Saves the current drawing state onto the state stack. */
			void			PushState();

	/** @brief Restores the most recently pushed drawing state. */
			void			PopState();

	/** @brief Sets the event mask controlling which events this view receives.
	 *  @param eventMask Event mask bitfield.
	 *  @param options   Event delivery options. */
			void			SetEventMask(uint32 eventMask, uint32 options);

	/** @brief Returns the event mask for this view.
	 *  @return Event mask bitfield. */
			uint32			EventMask() const
								{ return fEventMask; }

	/** @brief Returns the event delivery options for this view.
	 *  @return Event options bitfield. */
			uint32			EventOptions() const
								{ return fEventOptions; }

	/** @brief Sets the cursor displayed when the mouse is over this view.
	 *  @param cursor New cursor to use. */
			void			SetCursor(ServerCursor* cursor);

	/** @brief Returns the cursor associated with this view.
	 *  @return Pointer to the ServerCursor. */
			ServerCursor*	Cursor() const { return fCursor; }

	/** @brief Associates a recording ServerPicture with this view.
	 *  @param picture Picture to attach. */
			void			SetPicture(ServerPicture* picture);

	/** @brief Returns the ServerPicture currently attached to this view.
	 *  @return Pointer to the ServerPicture, or NULL. */
			ServerPicture*	Picture() const
								{ return fPicture; }

	/** @brief Blends all compositing layers of this view into the backing store. */
			void			BlendAllLayers();

			// for background clearing
	/** @brief Draws the view's background into the given drawing engine.
	 *  @param drawingEngine          Engine to draw with.
	 *  @param effectiveClipping      Combined clipping region.
	 *  @param windowContentClipping  Window content clipping region.
	 *  @param deep                   If true, also draw children. */
	virtual void			Draw(DrawingEngine* drawingEngine,
								const BRegion* effectiveClipping,
								const BRegion* windowContentClipping,
								bool deep = false);

	/** @brief Handles a mouse-down event.
	 *  @param message Message carrying event details.
	 *  @param where   Position of the click in view coordinates. */
	virtual void			MouseDown(BMessage* message, BPoint where);

	/** @brief Handles a mouse-up event.
	 *  @param message Message carrying event details.
	 *  @param where   Position of the release in view coordinates. */
	virtual void			MouseUp(BMessage* message, BPoint where);

	/** @brief Handles a mouse-moved event.
	 *  @param message Message carrying event details.
	 *  @param where   New cursor position in view coordinates. */
	virtual void			MouseMoved(BMessage* message, BPoint where);

	/** @brief Hides or shows the view.
	 *  @param hidden true to hide, false to show. */
			void			SetHidden(bool hidden);

	/** @brief Returns whether the view has been explicitly hidden.
	 *  @return true if hidden. */
			bool			IsHidden() const;

			// takes into account parent views hidden status
	/** @brief Returns whether the view is currently visible (considering parent visibility).
	 *  @return true if visible. */
			bool			IsVisible() const
								{ return fVisible; }
			// update visible status for this view and all children
			// according to the parents visibility
	/** @brief Recursively updates the visible flag based on parent visibility.
	 *  @param parentVisible true if the parent is visible. */
			void			UpdateVisibleDeep(bool parentVisible);

	/** @brief Updates the overlay associated with the view's background bitmap. */
			void			UpdateOverlay();

	/** @brief Marks the view's background as dirty, requiring a redraw. */
			void			MarkBackgroundDirty();

	/** @brief Returns whether the view's background is currently marked dirty.
	 *  @return true if the background needs redrawing. */
			bool			IsBackgroundDirty() const
								{ return fBackgroundDirty; }

	/** @brief Returns whether this view is the desktop background view.
	 *  @return true if this is the desktop background. */
			bool			IsDesktopBackground() const
								{ return fIsDesktopBackground; }

	/** @brief Appends tokens of views intersecting the given region to a PortLink.
	 *  @param link                   Link to write tokens to.
	 *  @param region                 Region to test intersection against.
	 *  @param windowContentClipping  Window content clipping applied during the test. */
			void			AddTokensForViewsInRegion(BPrivate::PortLink& link,
								BRegion& region,
								BRegion* windowContentClipping);

			// clipping
	/** @brief Rebuilds the local clipping region, optionally for the entire subtree.
	 *  @param deep If true, rebuild clipping for all descendant views as well. */
			void			RebuildClipping(bool deep);

	/** @brief Returns the combined screen and user clipping region.
	 *  @param windowContentClipping Window content clipping to intersect with.
	 *  @param force                 If true, recompute even if cached value is valid.
	 *  @return Reference to the combined clipping BRegion. */
			BRegion&		ScreenAndUserClipping(
								const BRegion* windowContentClipping,
								bool force = false) const;

	/** @brief Marks the screen clipping region as invalid, forcing recomputation. */
			void			InvalidateScreenClipping();

	/** @brief Returns whether the cached screen clipping is currently valid.
	 *  @return true if the cached clipping may be used without recomputation. */
	inline	bool			IsScreenClippingValid() const
								{
									return fScreenClippingValid
										&& (!fUserClipping.IsSet()
										|| (fUserClipping.IsSet()
										&& fScreenAndUserClipping.IsSet()));
								}

			// debugging
	/** @brief Prints view properties to standard output for debugging. */
			void			PrintToStream() const;
#if 0
			bool			MarkAt(DrawingEngine* engine, const BPoint& where,
								int32 level = 0);
#endif

protected:
	virtual	void			_LocalToScreenTransform(
								SimpleTransform& transform) const;
	virtual	void			_ScreenToLocalTransform(
								SimpleTransform& transform) const;

			BRegion&		_ScreenClipping(const BRegion* windowContentClipping,
								bool force = false) const;
			void			_MoveScreenClipping(int32 x, int32 y,
								bool deep);
			Overlay*		_Overlay() const;
			void			_UpdateOverlayView() const;

			BString			fName;
			int32			fToken;
			// area within parent coordinate space
			IntRect			fFrame;
			// offset of the local area (bounds)
			IntPoint		fScrollingOffset;

			rgb_color		fViewColor;
			color_which		fWhichViewColor;
			float			fWhichViewColorTint;
			BReference<ServerBitmap>
							fViewBitmap;
			IntRect			fBitmapSource;
			IntRect			fBitmapDestination;
			int32			fBitmapResizingMode;
			int32			fBitmapOptions;

			uint32			fResizeMode;
			uint32			fFlags;
			bool			fHidden : 1;
			bool			fVisible : 1;
			bool			fBackgroundDirty : 1;
			bool			fIsDesktopBackground : 1;

			uint32			fEventMask;
			uint32			fEventOptions;

			::Window*		fWindow;
			View*			fParent;

			View*			fFirstChild;
			View*			fPreviousSibling;
			View*			fNextSibling;
			View*			fLastChild;

			BReference<ServerCursor>
							fCursor;
			BReference<ServerPicture>
							fPicture;

			// clipping
			BRegion			fLocalClipping;

	mutable	BRegion			fScreenClipping;
	mutable	bool			fScreenClippingValid;

			ObjectDeleter<BRegion>
							fUserClipping;
	mutable	ObjectDeleter<BRegion>
							fScreenAndUserClipping;
};

#endif	// VIEW_H
