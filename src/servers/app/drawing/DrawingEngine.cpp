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
 *   Copyright 2001-2018, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Julian Harnath <julian.harnath@rwth-aachen.de>
 */


/**
 * @file DrawingEngine.cpp
 * @brief Implementation of the software drawing pipeline used by every Window.
 *
 * DrawingEngine wraps a Painter (AGG-based software rasteriser) bound to the
 * frame buffer of an HWInterface. Drawing primitive entry points compute a
 * bounding rectangle, hide floating overlays such as the cursor through
 * AutoFloatingOverlaysHider, run the Painter to produce pixels, and emit a
 * dirty region back to the HWInterface through DrawTransaction. The class
 * also implements text rendering, region-aware copies (CopyRegion), and a
 * read-back path used to capture screenshots.
 */


#include "DrawingEngine.h"

#include <Bitmap.h>
#include <StackOrHeapArray.h>

#include <stdio.h>

#include <algorithm>
#include <stack>

#include "DrawState.h"
#include "GlyphLayoutEngine.h"
#include "Painter.h"
#include "ServerBitmap.h"
#include "ServerCursor.h"
#include "RenderingBuffer.h"

#include "drawing_support.h"


#if DEBUG
#	define ASSERT_PARALLEL_LOCKED() \
	{ if (!IsParallelAccessLocked()) debugger("not parallel locked!"); }
#	define ASSERT_EXCLUSIVE_LOCKED() \
	{ if (!IsExclusiveAccessLocked()) debugger("not exclusive locked!"); }
#else
#	define ASSERT_PARALLEL_LOCKED()
#	define ASSERT_EXCLUSIVE_LOCKED()
#endif


/**
 * @brief Swaps the corners of @a rect when needed so that left <= right and top <= bottom.
 *
 * Several BView entry points accept rectangles whose @c right/@c bottom is
 * smaller than @c left/@c top; the rasteriser needs them normalised before
 * the bounds tests can be trusted.
 *
 * @param rect Rectangle to normalise in place.
 */
static inline void
make_rect_valid(BRect& rect)
{
	if (rect.left > rect.right) {
		float temp = rect.left;
		rect.left = rect.right;
		rect.right = temp;
	}
	if (rect.top > rect.bottom) {
		float temp = rect.top;
		rect.top = rect.bottom;
		rect.bottom = temp;
	}
}


/**
 * @brief Inflates @a rect outwards by half the stroke width.
 *
 * Used to extend the bounding rectangle of a stroked primitive so the
 * rasteriser sees the full pen-painted extent.
 *
 * @param rect    Rectangle to inflate in place.
 * @param penSize Current pen size in pixels.
 */
static inline void
extend_by_stroke_width(BRect& rect, float penSize)
{
	// "- 0.5" because if stroke width == 1, we don't need to extend
	float inset = -ceilf(penSize / 2.0 - 0.5);
	rect.InsetBy(inset, inset);
}


/** @brief RAII helper that hides cursor / drag bitmap for the lifetime of the scope. */
class AutoFloatingOverlaysHider {
	public:
		/** @brief Hides overlays that intersect @a area. */
		AutoFloatingOverlaysHider(HWInterface* interface, const BRect& area)
			:
			fInterface(interface),
			fHidden(interface->HideFloatingOverlays(area))
		{
		}

		/** @brief Hides overlays unconditionally. */
		AutoFloatingOverlaysHider(HWInterface* interface)
			:
			fInterface(interface),
			fHidden(fInterface->HideFloatingOverlays())
		{
		}

		/** @brief Restores any overlays that were hidden by the constructor. */
		~AutoFloatingOverlaysHider()
		{
			if (fHidden)
				fInterface->ShowFloatingOverlays();
		}

		/** @brief Returns true when overlays were actually hidden by the constructor. */
		bool WasHidden() const
		{
			return fHidden;
		}

	private:
		HWInterface*	fInterface;
		bool			fHidden;

};

/** @brief Computes a dirty region for a single drawing primitive and refreshes it on destruction.
 *
 * DrawTransaction snapshots the current Painter clipping region, intersects
 * it with the supplied bounds, hides floating overlays that intersect the
 * resulting region, and finally schedules an InvalidateRegion() on the
 * HWInterface when the engine has copy-back-to-front enabled.
 */
class DrawTransaction {
public:
	/** @brief Builds a transaction whose initial dirty region is @a bounds clipped to the painter. */
	DrawTransaction(DrawingEngine *engine, const BRect &bounds)
		:
		fEngine(engine),
		fOverlaysHidden(false)
	{
		fDirty.Set(bounds);
		fDirty.IntersectWith(fEngine->fPainter->ClippingRegion());
		if (fDirty.CountRects() == 0)
			return;
		fOverlaysHidden
			= fEngine->fGraphicsCard->HideFloatingOverlays(fDirty.Frame());
	}

	/** @brief Builds a transaction covering the entire painter clipping region. */
	DrawTransaction(DrawingEngine *engine)
		:
		fEngine(engine),
		fOverlaysHidden(false)
	{
		fDirty = *fEngine->fPainter->ClippingRegion();
		if (fDirty.CountRects() == 0)
			return;
		fOverlaysHidden
			= fEngine->fGraphicsCard->HideFloatingOverlays(fDirty.Frame());
	}

	/** @brief Builds a transaction whose dirty region is exactly @a region (already clipped). */
	DrawTransaction(DrawingEngine *engine, const BRegion &region)
		:
		fEngine(engine),
		fOverlaysHidden(false)
	{
		// region is already clipped
		fDirty = region;
		if (fDirty.CountRects() == 0)
			return;
		fOverlaysHidden
			= fEngine->fGraphicsCard->HideFloatingOverlays(fDirty.Frame());
	}

	/** @brief Schedules invalidation and restores overlays. */
	~DrawTransaction()
	{
		if (fEngine->fCopyToFront)
			fEngine->fGraphicsCard->InvalidateRegion(fDirty);
		if (fOverlaysHidden)
			fEngine->fGraphicsCard->ShowFloatingOverlays();
	}

	/** @brief Returns true when the dirty region is non-empty (drawing should proceed). */
	bool IsDirty() const
	{
		return fDirty.CountRects() > 0;
	}

	/** @brief Replaces the dirty region with @a rect intersected with the painter clip. */
	void SetDirty(const BRect &rect)
	{
		fDirty.Set(rect);
		fDirty.IntersectWith(fEngine->fPainter->ClippingRegion());
	}

	/** @brief Returns the current dirty region. */
	const BRegion &DirtyRegion() const
	{
		return fDirty;
	}

	/** @brief Returns true when the constructor hid overlays. */
	bool WasOverlaysHidden() const
	{
		return fOverlaysHidden;
	}

private:
	DrawingEngine *fEngine;
	bool fOverlaysHidden;
	BRegion fDirty;
};


//	#pragma mark -


/**
 * @brief Constructs the engine, optionally pre-attaching it to @a interface.
 *
 * @param interface HWInterface to attach to (may be NULL; subsequent
 *                  SetHWInterface() calls can change this).
 */
DrawingEngine::DrawingEngine(HWInterface* interface)
	:
	fPainter(new Painter()),
	fGraphicsCard(NULL),
	fCopyToFront(true)
{
	SetHWInterface(interface);
}


/**
 * @brief Detaches from the HWInterface; the Painter is freed by the smart pointer.
 */
DrawingEngine::~DrawingEngine()
{
	SetHWInterface(NULL);
}


// #pragma mark - locking


/**
 * @brief Acquires the HWInterface's parallel (read) lock.
 *
 * @return True when the lock was acquired.
 */
bool
DrawingEngine::LockParallelAccess()
{
	return fGraphicsCard->LockParallelAccess();
}


#if DEBUG
/**
 * @brief Returns true while the calling thread holds the parallel lock.
 *
 * @return Lock state of the underlying HWInterface (debug only).
 */
bool
DrawingEngine::IsParallelAccessLocked() const
{
	return fGraphicsCard->IsParallelAccessLocked();
}
#endif


/**
 * @brief Releases the parallel lock.
 */
void
DrawingEngine::UnlockParallelAccess()
{
	fGraphicsCard->UnlockParallelAccess();
}


/**
 * @brief Acquires the HWInterface's exclusive (write) lock.
 *
 * @return True when the lock was acquired.
 */
bool
DrawingEngine::LockExclusiveAccess()
{
	return fGraphicsCard->LockExclusiveAccess();
}


/**
 * @brief Returns true while the calling thread holds the exclusive lock.
 */
bool
DrawingEngine::IsExclusiveAccessLocked() const
{
	return fGraphicsCard->IsExclusiveAccessLocked();
}


/**
 * @brief Releases the exclusive lock.
 */
void
DrawingEngine::UnlockExclusiveAccess()
{
	fGraphicsCard->UnlockExclusiveAccess();
}


// #pragma mark -


/**
 * @brief HWInterfaceListener entry point invoked when the framebuffer changes.
 *
 * Detaches the Painter from the previous buffer (or attaches it to the new
 * one), holding the exclusive lock for the duration of the swap.
 *
 * @note Locking is approximate; we are typically called on the thread that
 *       just performed the swap.
 */
void
DrawingEngine::FrameBufferChanged()
{
	if (!fGraphicsCard) {
		fPainter->DetachFromBuffer();
		return;
	}

	// NOTE: locking is probably bogus, since we are called
	// in the thread that changed the frame buffer...
	if (LockExclusiveAccess()) {
		fPainter->AttachToBuffer(fGraphicsCard->DrawingBuffer());
		UnlockExclusiveAccess();
	}
}


/**
 * @brief Re-binds the engine to a different HWInterface.
 *
 * Removes the listener registration from the old interface, registers with
 * the new one, and re-attaches the Painter to the new framebuffer.
 *
 * @param interface New HWInterface to use; may be NULL.
 */
void
DrawingEngine::SetHWInterface(HWInterface* interface)
{
	if (fGraphicsCard == interface)
		return;

	if (fGraphicsCard)
		fGraphicsCard->RemoveListener(this);

	fGraphicsCard = interface;

	if (fGraphicsCard)
		fGraphicsCard->AddListener(this);

	FrameBufferChanged();
}


/**
 * @brief Toggles automatic copy-back-to-front after each draw.
 *
 * When false, drawing still happens in the back buffer but no invalidation
 * is sent to the HWInterface; the caller is then responsible for refreshing
 * the screen via CopyToFront().
 *
 * @param enable Desired state.
 */
void
DrawingEngine::SetCopyToFrontEnabled(bool enable)
{
	fCopyToFront = enable;
}


/**
 * @brief Schedules @a region for back-to-front refresh on the HWInterface.
 */
void
DrawingEngine::CopyToFront(/*const*/ BRegion& region)
{
	fGraphicsCard->InvalidateRegion(region);
}


// #pragma mark -


/**
 * @brief Updates the Painter clipping region.
 *
 * @param region New clip region; pass NULL to remove any clipping (drawing
 *               then is allowed everywhere on the buffer).
 *
 * @note The DrawingEngine must be parallel-locked.
 */
void
DrawingEngine::ConstrainClippingRegion(const BRegion* region)
{
	ASSERT_PARALLEL_LOCKED();

	fPainter->ConstrainClipping(region);
}


/**
 * @brief Sets every Painter parameter from a complete DrawState snapshot.
 *
 * @param state   Source draw state.
 * @param xOffset Origin X offset added to the state's coordinate system.
 * @param yOffset Origin Y offset added to the state's coordinate system.
 */
void
DrawingEngine::SetDrawState(const DrawState* state, int32 xOffset,
	int32 yOffset)
{
	fPainter->SetDrawState(state, xOffset, yOffset);
}


/**
 * @brief Sets the high color used by stipple patterns.
 */
void
DrawingEngine::SetHighColor(const rgb_color& color)
{
	fPainter->SetHighColor(color);
}


/**
 * @brief Sets the low color used by stipple patterns.
 */
void
DrawingEngine::SetLowColor(const rgb_color& color)
{
	fPainter->SetLowColor(color);
}


/**
 * @brief Sets the pen size used by all stroking primitives.
 */
void
DrawingEngine::SetPenSize(float size)
{
	fPainter->SetPenSize(size);
}


/**
 * @brief Sets the cap, join, and miter parameters used while stroking paths.
 */
void
DrawingEngine::SetStrokeMode(cap_mode lineCap, join_mode joinMode,
	float miterLimit)
{
	fPainter->SetStrokeMode(lineCap, joinMode, miterLimit);
}


/**
 * @brief Selects between B_NONZERO and B_EVEN_ODD fill rules.
 */
void
DrawingEngine::SetFillRule(int32 fillRule)
{
	fPainter->SetFillRule(fillRule);
}


/**
 * @brief Configures the source-alpha and alpha-function used in B_OP_ALPHA.
 */
void
DrawingEngine::SetBlendingMode(source_alpha srcAlpha, alpha_function alphaFunc)
{
	fPainter->SetBlendingMode(srcAlpha, alphaFunc);
}


/**
 * @brief Sets the active stipple pattern.
 */
void
DrawingEngine::SetPattern(const struct pattern& pattern)
{
	fPainter->SetPattern(pattern);
}


/**
 * @brief Sets the active drawing mode.
 *
 * @param mode One of B_OP_COPY, B_OP_OVER, B_OP_ALPHA, etc.
 */
void
DrawingEngine::SetDrawingMode(drawing_mode mode)
{
	fPainter->SetDrawingMode(mode);
}


/**
 * @brief Sets the drawing mode and returns the previous one in @a oldMode.
 */
void
DrawingEngine::SetDrawingMode(drawing_mode mode, drawing_mode& oldMode)
{
	oldMode = fPainter->DrawingMode();
	fPainter->SetDrawingMode(mode);
}


/**
 * @brief Sets the current text font from a ServerFont.
 */
void
DrawingEngine::SetFont(const ServerFont& font)
{
	fPainter->SetFont(font);
}


/**
 * @brief Sets the current text font from the font slot of @a state.
 */
void
DrawingEngine::SetFont(const DrawState* state)
{
	fPainter->SetFont(state);
}


/**
 * @brief Sets the current view-to-screen affine transform.
 */
void
DrawingEngine::SetTransform(const BAffineTransform& transform, int32 xOffset,
	int32 yOffset)
{
	fPainter->SetTransform(transform, xOffset, yOffset);
}


// #pragma mark -


// CopyRegion() does a topological sort of the rects in the
// region. The algorithm was suggested by Ingo Weinhold.
// It compares each rect with each rect and builds a tree
// of successors so we know the order in which they can be copied.
// For example, let's suppose these rects are in a BRegion:
//                        ************
//                        *    B     *
//                        ************
//      *************
//      *           *
//      *     A     ****************
//      *           **             *
//      **************             *
//                   *     C       *
//                   *             *
//                   *             *
//                   ***************
// When copying stuff from LEFT TO RIGHT, TOP TO BOTTOM, the
// result of the sort will be C, A, B. For this direction, we search
// for the rects that have no neighbors to their right and to their
// bottom, These can be copied without drawing into the area of
// rects yet to be copied. If you move from RIGHT TO LEFT, BOTTOM TO TOP,
// you go look for the ones that have no neighbors to their top and left.
//
// Here I draw some rays to illustrate LEFT TO RIGHT, TOP TO BOTTOM:
//                        ************
//                        *    B     *
//                        ************
//      *************
//      *           *
//      *     A     ****************-----------------
//      *           **             *
//      **************             *
//                   *     C       *
//                   *             *
//                   *             *
//                   ***************
//                   |
//                   |
//                   |
//                   |
// There are no rects in the area defined by the rays to the right
// and bottom of rect C, so that's the one we want to copy first
// (for positive x and y offsets).
// Since A is to the left of C and B is to the top of C, The "node"
// for C will point to the nodes of A and B as its "successors". Therefor,
// A and B will have an "indegree" of 1 for C pointing to them. C will
// have an "indegree" of 0, because there was no rect to which C
// was to the left or top of. When comparing A and B, neither is left
// or top from the other and in the sense that the algorithm cares about.

// NOTE: comparison of coordinates assumes that rects don't overlap
// and don't share the actual edge either (as is the case in BRegions).

/** @brief DAG node used by CopyRegion() to topologically order overlapping rects. */
struct node {
	node()
	{
		pointers = NULL;
	}

	node(const BRect& r, int32 maxPointers)
	{
		init(r, maxPointers);
	}

	~node()
	{
		delete [] pointers;
	}

	/** @brief Initialises the rectangle and allocates the successor array. */
	void init(const BRect& r, int32 maxPointers)
	{
		rect = r;
		pointers = new(std::nothrow) node*[maxPointers];
		in_degree = 0;
		next_pointer = 0;
	}

	/** @brief Pushes a successor pointer onto the node's array. */
	void push(node* node)
	{
		pointers[next_pointer] = node;
		next_pointer++;
	}

	/** @brief Returns the successor at the current top of the array. */
	node* top()
	{
		return pointers[next_pointer];
	}

	/** @brief Pops and returns the successor at the top of the array. */
	node* pop()
	{
		node* ret = top();
		next_pointer--;
		return ret;
	}

	BRect	rect;
	int32	in_degree;
	node**	pointers;
	int32	next_pointer;
};


/**
 * @brief Returns true when @a a is strictly to the left of @a b (no edge sharing).
 */
static bool
is_left_of(const BRect& a, const BRect& b)
{
	return (a.right < b.left);
}


/**
 * @brief Returns true when @a a is strictly above @a b (no edge sharing).
 */
static bool
is_above(const BRect& a, const BRect& b)
{
	return (a.bottom < b.top);
}


/**
 * @brief Topologically sorts and copies the rectangles of @a region by (xOffset, yOffset).
 *
 * The algorithm prevents pixels that have not yet been copied from being
 * overwritten by their own translated copies. See the long comment block
 * above for a detailed explanation of the topological ordering.
 *
 * @param region  Region to copy (already clipped against the destination).
 * @param xOffset Horizontal translation in pixels.
 * @param yOffset Vertical translation in pixels.
 *
 * @note The DrawingEngine must be parallel-locked.
 */
void
DrawingEngine::CopyRegion(/*const*/ BRegion* region, int32 xOffset,
	int32 yOffset)
{
	// NOTE: region is already clipped
	ASSERT_PARALLEL_LOCKED();

	BRect frame = region->Frame();
	frame = frame | frame.OffsetByCopy(xOffset, yOffset);

	AutoFloatingOverlaysHider _(fGraphicsCard, frame);

	int32 count = region->CountRects();

	// TODO: make this step unnecessary
	// (by using different stack impl inside node)
	BStackOrHeapArray<node, 64> nodes(count);
	for (int32 i= 0; i < count; i++) {
		nodes[i].init(region->RectAt(i), count);
		if (nodes[i].pointers == NULL)
			return;
	}

	for (int32 i = 0; i < count; i++) {
		BRect a = region->RectAt(i);
		for (int32 k = i + 1; k < count; k++) {
			BRect b = region->RectAt(k);
			int cmp = 0;
			// compare horizontally
			if (xOffset > 0) {
				if (is_left_of(a, b)) {
					cmp -= 1;
				} else if (is_left_of(b, a)) {
					cmp += 1;
				}
			} else if (xOffset < 0) {
				if (is_left_of(a, b)) {
					cmp += 1;
				} else if (is_left_of(b, a)) {
					cmp -= 1;
				}
			}
			// compare vertically
			if (yOffset > 0) {
				if (is_above(a, b)) {
					cmp -= 1;
				} else if (is_above(b, a)) {
					cmp += 1;
				}
			} else if (yOffset < 0) {
				if (is_above(a, b)) {
					cmp += 1;
				} else if (is_above(b, a)) {
					cmp -= 1;
				}
			}
			// add appropriate node as successor
			if (cmp > 0) {
				nodes[i].push(&nodes[k]);
				nodes[k].in_degree++;
			} else if (cmp < 0) {
				nodes[k].push(&nodes[i]);
				nodes[i].in_degree++;
			}
		}
	}
	// put all nodes onto a stack that have an "indegree" count of zero
	std::stack<node*> inDegreeZeroNodes;
	for (int32 i = 0; i < count; i++) {
		if (nodes[i].in_degree == 0) {
			inDegreeZeroNodes.push(&nodes[i]);
		}
	}
	// pop the rects from the stack, do the actual copy operation
	// and decrease the "indegree" count of the other rects not
	// currently on the stack and to which the current rect pointed
	// to. If their "indegree" count reaches zero, put them onto the
	// stack as well.

	while (!inDegreeZeroNodes.empty()) {
		node* n = inDegreeZeroNodes.top();
		inDegreeZeroNodes.pop();

		BRect touched = CopyRect(n->rect, xOffset, yOffset);
		fGraphicsCard->Invalidate(touched);

		for (int32 k = 0; k < n->next_pointer; k++) {
			n->pointers[k]->in_degree--;
			if (n->pointers[k]->in_degree == 0)
				inDegreeZeroNodes.push(n->pointers[k]);
		}
	}
}


/**
 * @brief Inverts the pixels inside @a r (XOR with white).
 *
 * @param r Rectangle to invert; normalised before clipping.
 *
 * @note The DrawingEngine must be parallel-locked.
 */
void
DrawingEngine::InvertRect(BRect r)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	// NOTE: Currently ignores view transformation, so no TransformAndClipRect()
	DrawTransaction transaction(this, fPainter->ClipRect(r));
	if (!transaction.IsDirty())
		return;

	fPainter->InvertRect(r);
}


/**
 * @brief Blits @a bitmap into @a viewRect using @a bitmapRect as the source.
 *
 * @param bitmap     Source bitmap.
 * @param bitmapRect Sub-rectangle of @a bitmap to sample from.
 * @param viewRect   Destination rectangle on the drawing target.
 * @param options    Bitmap drawing options (filter mode, tile mode, ...).
 *
 * @note The DrawingEngine must be parallel-locked.
 */
void
DrawingEngine::DrawBitmap(ServerBitmap* bitmap, const BRect& bitmapRect,
	const BRect& viewRect, uint32 options)
{
	ASSERT_PARALLEL_LOCKED();

	DrawTransaction transaction(this, fPainter->TransformAndClipRect(viewRect));
	if (transaction.IsDirty())
		fPainter->DrawBitmap(bitmap, bitmapRect, viewRect, options);
}


/**
 * @brief Draws a circular arc inside @a r starting at @a angle, sweeping @a span degrees.
 *
 * @param r      Bounding rectangle of the ellipse the arc is part of.
 * @param angle  Start angle in degrees.
 * @param span   Sweep in degrees (positive counter-clockwise).
 * @param filled True to fill the pie slice, false to stroke the arc.
 *
 * @note The DrawingEngine must be parallel-locked.
 */
void
DrawingEngine::DrawArc(BRect r, const float& angle, const float& span,
	bool filled)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	fPainter->AlignEllipseRect(&r, filled);

	BRect clipped(r);
	if (!filled)
		extend_by_stroke_width(clipped, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(clipped));
	if (!transaction.IsDirty())
		return;

	float xRadius = r.Width() / 2.0;
	float yRadius = r.Height() / 2.0;
	BPoint center(r.left + xRadius,
				  r.top + yRadius);

	if (filled)
		fPainter->FillArc(center, xRadius, yRadius, angle, span);
	else
		fPainter->StrokeArc(center, xRadius, yRadius, angle, span);
}


/**
 * @brief Draws a gradient-filled arc.
 */
void
DrawingEngine::DrawArc(BRect r, const float& angle, const float& span,
	bool filled, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	fPainter->AlignEllipseRect(&r, true);
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(r));
	if (!transaction.IsDirty())
		return;

	float xRadius = r.Width() / 2.0;
	float yRadius = r.Height() / 2.0;
	BPoint center(r.left + xRadius,
				  r.top + yRadius);

	if (filled)
		fPainter->FillArc(center, xRadius, yRadius, angle, span, gradient);
	else
		fPainter->StrokeArc(center, xRadius, yRadius, angle, span, gradient);
}


/**
 * @brief Draws a cubic Bezier curve through four control points.
 *
 * @param pts    Array of four BPoints (start, two control points, end).
 * @param filled True to fill the resulting closed curve.
 *
 * @todo Compute the actual bounding box rather than hiding overlays for the entire clip.
 */
void
DrawingEngine::DrawBezier(BPoint* pts, bool filled)
{
	ASSERT_PARALLEL_LOCKED();

	// TODO: figure out bounds and hide cursor depending on that
	DrawTransaction transaction(this);

	transaction.SetDirty(fPainter->DrawBezier(pts, filled));
}


/**
 * @brief Draws a gradient-filled cubic Bezier curve.
 */
void
DrawingEngine::DrawBezier(BPoint* pts, bool filled, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	// TODO: figure out bounds and hide cursor depending on that
	DrawTransaction transaction(this);

	transaction.SetDirty(fPainter->DrawBezier(pts, filled, gradient));
}


/**
 * @brief Draws an ellipse fitting the rectangle @a r.
 *
 * @param r      Bounding rectangle.
 * @param filled True to fill, false to stroke.
 */
void
DrawingEngine::DrawEllipse(BRect r, bool filled)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	BRect clipped = r;
	fPainter->AlignEllipseRect(&clipped, filled);

	if (!filled)
		extend_by_stroke_width(clipped, fPainter->PenSize());

	clipped.left = floorf(clipped.left);
	clipped.top = floorf(clipped.top);
	clipped.right = ceilf(clipped.right);
	clipped.bottom = ceilf(clipped.bottom);

	DrawTransaction transaction(this, fPainter->TransformAndClipRect(clipped));
	if (!transaction.IsDirty())
		return;

	fPainter->DrawEllipse(r, filled);
}


/**
 * @brief Draws a gradient-filled ellipse.
 */
void
DrawingEngine::DrawEllipse(BRect r, bool filled, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	BRect clipped = r;
	fPainter->AlignEllipseRect(&clipped, filled);

	if (!filled)
		extend_by_stroke_width(clipped, fPainter->PenSize());

	clipped.left = floorf(clipped.left);
	clipped.top = floorf(clipped.top);
	clipped.right = ceilf(clipped.right);
	clipped.bottom = ceilf(clipped.bottom);

	DrawTransaction transaction(this, fPainter->TransformAndClipRect(clipped));
	if (!transaction.IsDirty())
		return;

	fPainter->DrawEllipse(r, filled, gradient);
}


/**
 * @brief Draws a polygon described by @a ptlist / @a numpts.
 *
 * @param ptlist Array of polygon vertices.
 * @param numpts Vertex count.
 * @param bounds Bounding rectangle (used for clipping / overlay hiding).
 * @param filled True to fill, false to stroke.
 * @param closed True to close the polygon by connecting the last vertex to the first.
 */
void
DrawingEngine::DrawPolygon(BPoint* ptlist, int32 numpts, BRect bounds,
	bool filled, bool closed)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(bounds);
	if (!filled)
		extend_by_stroke_width(bounds, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(bounds));
	if (!transaction.IsDirty())
		return;

	fPainter->DrawPolygon(ptlist, numpts, filled, closed);
}


/**
 * @brief Draws a gradient-filled polygon.
 */
void
DrawingEngine::DrawPolygon(BPoint* ptlist, int32 numpts, BRect bounds,
	bool filled, bool closed, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(bounds);
	if (!filled)
		extend_by_stroke_width(bounds, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(bounds));
	if (!transaction.IsDirty())
		return;

	fPainter->DrawPolygon(ptlist, numpts, filled, closed, gradient);
}


// #pragma mark - rgb_color


/**
 * @brief Plots a single pixel at @a pt in the supplied @a color.
 *
 * Implemented as a degenerate StrokeLine() to keep one rasteriser code path.
 */
void
DrawingEngine::StrokePoint(const BPoint& pt, const rgb_color& color)
{
	StrokeLine(pt, pt, color);
}


/**
 * @brief Strokes a 1-pixel-wide line in @a color (server-internal entry point).
 *
 * Used by Decorators where the pen size and pattern are known to be the
 * defaults. The fast path uses Painter::StraightLine() for axis-aligned
 * cases and falls back to a full StrokeLine() with B_OP_OVER otherwise.
 *
 * @note The DrawingEngine must be parallel-locked.
 */
void
DrawingEngine::StrokeLine(const BPoint& start, const BPoint& end,
	const rgb_color& color)
{
	ASSERT_PARALLEL_LOCKED();

	BRect touched(start, end);
	make_rect_valid(touched);
	touched = fPainter->ClipRect(touched);
	DrawTransaction transaction(this, touched);

	if (!fPainter->StraightLine(start, end, color)) {
		rgb_color previousColor = fPainter->HighColor();
		drawing_mode previousMode = fPainter->DrawingMode();

		fPainter->SetHighColor(color);
		fPainter->SetDrawingMode(B_OP_OVER);
		fPainter->StrokeLine(start, end);

		fPainter->SetDrawingMode(previousMode);
		fPainter->SetHighColor(previousColor);
	}
}


/**
 * @brief Strokes a 1-pixel-wide rectangle in @a color (server-internal entry point).
 *
 * @note The DrawingEngine must be parallel-locked.
 */
void
DrawingEngine::StrokeRect(BRect r, const rgb_color& color)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	DrawTransaction transaction(this, fPainter->ClipRect(r));
	if (!transaction.IsDirty())
		return;

	fPainter->StrokeRect(r, color);
}


/**
 * @brief Fills a rectangle with @a color (server-internal entry point).
 *
 * @note The DrawingEngine must be parallel-locked.
 */
void
DrawingEngine::FillRect(BRect r, const rgb_color& color)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	r = fPainter->ClipRect(r);
	DrawTransaction transaction(this, r);
	if (!transaction.IsDirty())
		return;

	fPainter->FillRect(r, color);
}


/**
 * @brief Fills @a r with @a color, expecting it to already be clipped (server-internal).
 *
 * @param r     Already-clipped region (one rect per FillRectNoClipping call).
 * @param color Solid fill color.
 *
 * @note The DrawingEngine must be parallel-locked. The caller is responsible
 *       for ensuring @a r lies inside the framebuffer bounds. See bug #634
 *       for context on the upstream defensive check.
 */
void
DrawingEngine::FillRegion(BRegion& r, const rgb_color& color)
{
	ASSERT_PARALLEL_LOCKED();

	// NOTE: region expected to be already clipped correctly!!
	BRect frame = r.Frame();
	if (!fPainter->Bounds().Contains(frame)) {
		// NOTE: I am not quite sure yet how this can happen, but apparently it
		// can (see bug #634).
		// This function is used for internal app_server painting, in the case of
		// bug #634, the background of views is painted. But the view region
		// should never be outside the frame buffer bounds.
//		char message[1024];
//		BRect bounds = fPainter->Bounds();
//		sprintf(message, "FillRegion() - painter: (%d, %d)->(%d, %d), region: (%d, %d)->(%d, %d)",
//			(int)bounds.left, (int)bounds.top, (int)bounds.right, (int)bounds.bottom,
//			(int)frame.left, (int)frame.top, (int)frame.right, (int)frame.bottom);
//		debugger(message);
		return;
	}

	DrawTransaction transaction(this, r);

	int32 count = r.CountRects();
	for (int32 i = 0; i < count; i++)
		fPainter->FillRectNoClipping(r.RectAtInt(i), color);
}


// #pragma mark - DrawState


/**
 * @brief Strokes a rectangle using the current draw state.
 */
void
DrawingEngine::StrokeRect(BRect r)
{
	ASSERT_PARALLEL_LOCKED();

	// support invalid rects
	make_rect_valid(r);
	BRect clipped(r);
	extend_by_stroke_width(clipped, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(clipped));
	if (!transaction.IsDirty())
		return;

	fPainter->StrokeRect(r);
}


/**
 * @brief Fills a rectangle using the current draw state.
 */
void
DrawingEngine::FillRect(BRect r)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);

	r = fPainter->AlignRect(r);

	DrawTransaction transaction(this, fPainter->TransformAndClipRect(r));
	if (!transaction.IsDirty())
		return;

	fPainter->FillRect(r);
}


/**
 * @brief Strokes a rectangle, sampling @a gradient along the stroke path.
 */
void
DrawingEngine::StrokeRect(BRect r, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	// support invalid rects
	make_rect_valid(r);
	BRect clipped(r);
	extend_by_stroke_width(clipped, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(clipped));
	if (!transaction.IsDirty())
		return;

	fPainter->StrokeRect(r, gradient);
}


/**
 * @brief Fills a rectangle with @a gradient.
 */
void
DrawingEngine::FillRect(BRect r, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	r = fPainter->AlignRect(r);

	DrawTransaction transaction(this, fPainter->TransformAndClipRect(r));
	if (!transaction.IsDirty())
		return;

	fPainter->FillRect(r, gradient);
}


/**
 * @brief Fills @a r using the current draw state, walking the region rect by rect.
 */
void
DrawingEngine::FillRegion(BRegion& r)
{
	ASSERT_PARALLEL_LOCKED();

	BRect clipped = fPainter->TransformAndClipRect(r.Frame());
	DrawTransaction transaction(this, clipped);
	if (!transaction.IsDirty())
		return;

	int32 count = r.CountRects();
	for (int32 i = 0; i < count; i++)
		fPainter->FillRect(r.RectAt(i));
}


/**
 * @brief Fills @a r with @a gradient, walking the region rect by rect.
 */
void
DrawingEngine::FillRegion(BRegion& r, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	BRect clipped = fPainter->TransformAndClipRect(r.Frame());
	DrawTransaction transaction(this, clipped);
	if (!transaction.IsDirty())
		return;

	int32 count = r.CountRects();
	for (int32 i = 0; i < count; i++)
		fPainter->FillRect(r.RectAt(i), gradient);
}


/**
 * @brief Draws a rounded rectangle with corner radii (@a xrad, @a yrad).
 *
 * @param r      Bounding rectangle.
 * @param xrad   Horizontal corner radius.
 * @param yrad   Vertical corner radius.
 * @param filled True to fill, false to stroke.
 */
void
DrawingEngine::DrawRoundRect(BRect r, float xrad, float yrad, bool filled)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	if (!filled)
		extend_by_stroke_width(r, fPainter->PenSize());
	BRect clipped = fPainter->TransformAndClipRect(r);

	clipped.left = floorf(clipped.left);
	clipped.top = floorf(clipped.top);
	clipped.right = ceilf(clipped.right);
	clipped.bottom = ceilf(clipped.bottom);

	DrawTransaction transaction(this, clipped);
	if (!transaction.IsDirty())
		return;

	if (filled)
		fPainter->FillRoundRect(r, xrad, yrad);
	else
		fPainter->StrokeRoundRect(r, xrad, yrad);
}


/**
 * @brief Draws a gradient-filled rounded rectangle.
 */
void
DrawingEngine::DrawRoundRect(BRect r, float xrad, float yrad,
	bool filled, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	if (!filled)
		extend_by_stroke_width(r, fPainter->PenSize());
	BRect clipped = fPainter->TransformAndClipRect(r);

	clipped.left = floorf(clipped.left);
	clipped.top = floorf(clipped.top);
	clipped.right = ceilf(clipped.right);
	clipped.bottom = ceilf(clipped.bottom);

	DrawTransaction transaction(this, clipped);
	if (!transaction.IsDirty())
		return;

	if (filled)
		fPainter->FillRoundRect(r, xrad, yrad, gradient);
	else
		fPainter->StrokeRoundRect(r, xrad, yrad, gradient);
}


/**
 * @brief Renders a BShape described by @a opList / @a ptList.
 *
 * @param bounds              Bounding box reported by the caller (currently
 *                            only used for documentation; clipping happens
 *                            inside Painter).
 * @param opCount             Number of operation codes in @a opList.
 * @param opList              BShape operation codes.
 * @param ptCount             Number of points in @a ptList.
 * @param ptList              BShape control points.
 * @param filled              True to fill, false to stroke.
 * @param viewToScreenOffset  Offset added to all points before rasterising.
 * @param viewScale           Scale applied to all points before rasterising.
 *
 * @todo The supplied bounds do not currently take curves and arcs into
 *       account, so the precomputed clip path is bypassed.
 */
void
DrawingEngine::DrawShape(const BRect& bounds, int32 opCount,
	const uint32* opList, int32 ptCount, const BPoint* ptList, bool filled,
	const BPoint& viewToScreenOffset, float viewScale)
{
	ASSERT_PARALLEL_LOCKED();

// TODO: bounds probably does not take curves and arcs into account...
//	BRect clipped(bounds);
//	if (!filled)
//		extend_by_stroke_width(clipped, fPainter->PenSize());
//	clipped = fPainter->TransformAndClipRect(bounds);
//
//	clipped.left = floorf(clipped.left);
//	clipped.top = floorf(clipped.top);
//	clipped.right = ceilf(clipped.right);
//	clipped.bottom = ceilf(clipped.bottom);
//
//	DrawTransaction transaction(this, clipped);
//	if (!transaction.IsDirty())
//		return;
	DrawTransaction transaction(this);

	transaction.SetDirty(fPainter->DrawShape(opCount, opList, ptCount, ptList,
		filled, viewToScreenOffset, viewScale));
}


/**
 * @brief Renders a gradient-filled BShape.
 */
void
DrawingEngine::DrawShape(const BRect& bounds, int32 opCount,
	const uint32* opList, int32 ptCount, const BPoint* ptList,
	bool filled, const BGradient& gradient, const BPoint& viewToScreenOffset,
	float viewScale)
{
	ASSERT_PARALLEL_LOCKED();

// TODO: bounds probably does not take curves and arcs into account...
//	BRect clipped = fPainter->TransformAndClipRect(bounds);
//
//	clipped.left = floorf(clipped.left);
//	clipped.top = floorf(clipped.top);
//	clipped.right = ceilf(clipped.right);
//	clipped.bottom = ceilf(clipped.bottom);
//
//	DrawTransaction transaction(this, clipped);
//	if (!transaction.IsDirty())
//		return;
	DrawTransaction transaction(this);

	transaction.SetDirty(fPainter->DrawShape(opCount, opList, ptCount, ptList,
		filled, gradient, viewToScreenOffset, viewScale));
}


/**
 * @brief Draws a triangle.
 *
 * @param pts    Three vertices.
 * @param bounds Pre-computed bounding box.
 * @param filled True to fill, false to stroke.
 */
void
DrawingEngine::DrawTriangle(BPoint* pts, const BRect& bounds, bool filled)
{
	ASSERT_PARALLEL_LOCKED();

	BRect clipped(bounds);
	if (!filled)
		extend_by_stroke_width(clipped, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(clipped));
	if (!transaction.IsDirty())
		return;

	if (filled)
		fPainter->FillTriangle(pts[0], pts[1], pts[2]);
	else
		fPainter->StrokeTriangle(pts[0], pts[1], pts[2]);
}


/**
 * @brief Draws a gradient-filled triangle.
 */
void
DrawingEngine::DrawTriangle(BPoint* pts, const BRect& bounds,
	bool filled, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	BRect clipped(bounds);
	if (!filled)
		extend_by_stroke_width(clipped, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(clipped));
	if (!transaction.IsDirty())
		return;

	if (filled)
		fPainter->FillTriangle(pts[0], pts[1], pts[2], gradient);
	else
		fPainter->StrokeTriangle(pts[0], pts[1], pts[2], gradient);
}


/**
 * @brief Strokes a line using the current draw state.
 */
void
DrawingEngine::StrokeLine(const BPoint& start, const BPoint& end)
{
	ASSERT_PARALLEL_LOCKED();

	BRect touched(start, end);
	make_rect_valid(touched);
	extend_by_stroke_width(touched, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(touched));
	if (!transaction.IsDirty())
		return;

	fPainter->StrokeLine(start, end);
}


/**
 * @brief Strokes a line, sampling @a gradient along its length.
 */
void
DrawingEngine::StrokeLine(const BPoint& start, const BPoint& end, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	BRect touched(start, end);
	make_rect_valid(touched);
	extend_by_stroke_width(touched, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(touched));
	if (!transaction.IsDirty())
		return;

	fPainter->StrokeLine(start, end, gradient);
}


/**
 * @brief Strokes an array of independently colored lines (BView::StrokeLineArray).
 *
 * @param numLines Number of entries in @a lineData.
 * @param lineData Array of line / color records.
 *
 * @note Saves and restores the painter's high color and pattern around the
 *       loop because each entry has its own color.
 */
void
DrawingEngine::StrokeLineArray(int32 numLines,
	const ViewLineArrayInfo *lineData)
{
	ASSERT_PARALLEL_LOCKED();

	if (!lineData || numLines <= 0)
		return;

	// figure out bounding box for line array
	const ViewLineArrayInfo* data = (const ViewLineArrayInfo*)&lineData[0];
	BRect touched(min_c(data->startPoint.x, data->endPoint.x),
		min_c(data->startPoint.y, data->endPoint.y),
		max_c(data->startPoint.x, data->endPoint.x),
		max_c(data->startPoint.y, data->endPoint.y));

	for (int32 i = 1; i < numLines; i++) {
		data = (const ViewLineArrayInfo*)&lineData[i];
		BRect box(min_c(data->startPoint.x, data->endPoint.x),
			min_c(data->startPoint.y, data->endPoint.y),
			max_c(data->startPoint.x, data->endPoint.x),
			max_c(data->startPoint.y, data->endPoint.y));
		touched = touched | box;
	}
	extend_by_stroke_width(touched, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(touched));
	if (!transaction.IsDirty())
		return;

	data = (const ViewLineArrayInfo*)&(lineData[0]);

	// store current graphics state, we mess with the
	// high color and pattern...
	rgb_color oldColor = fPainter->HighColor();
	struct pattern pattern = fPainter->Pattern();

	fPainter->SetHighColor(data->color);
	fPainter->SetPattern(B_SOLID_HIGH);
	fPainter->StrokeLine(data->startPoint, data->endPoint);

	for (int32 i = 1; i < numLines; i++) {
		data = (const ViewLineArrayInfo*)&(lineData[i]);
		fPainter->SetHighColor(data->color);
		fPainter->StrokeLine(data->startPoint, data->endPoint);
	}

	// restore correct drawing state highcolor and pattern
	fPainter->SetHighColor(oldColor);
	fPainter->SetPattern(pattern);
}


// #pragma mark -


/**
 * @brief Renders @a string at @a pt and returns the resulting pen position.
 *
 * The fast path returns the advance without rendering when the string is
 * entirely outside the clipping region (avoids the expensive bounding box
 * computation). Otherwise the bounding box is computed via Painter, the
 * dirty region is clipped, and the string is rendered.
 *
 * @param string   UTF-8 string to render.
 * @param length   Length of @a string in bytes.
 * @param pt       Pen baseline position.
 * @param delta    Optional escapement deltas, NULL for default spacing.
 * @return  The pen position after rendering the string.
 *
 * @note The DrawingEngine must be parallel-locked.
 */
BPoint
DrawingEngine::DrawString(const char* string, int32 length,
	const BPoint& pt, escapement_delta* delta)
{
	ASSERT_PARALLEL_LOCKED();

	BPoint penLocation = pt;

	// try a fast clipping path
	if (fPainter->ClippingRegion() != NULL
		&& fPainter->Font().Rotation() == 0.0f
		&& fPainter->IsIdentityTransform()) {
		float fontSize = fPainter->Font().Size();
		BRect clippingFrame = fPainter->ClippingRegion()->Frame();
		if (pt.x - fontSize > clippingFrame.right
			|| pt.y + fontSize < clippingFrame.top
			|| pt.y - fontSize > clippingFrame.bottom) {
			penLocation.x += StringWidth(string, length, delta);
			return penLocation;
		}
	}

	// use a FontCacheRefernece to speed up the second pass of
	// drawing the string
	FontCacheReference cacheReference;

//bigtime_t now = system_time();
// TODO: BoundingBox is quite slow!! Optimizing it will be beneficial.
// Cursiously, the DrawString after it is actually faster!?!
// TODO: make the availability of the hardware cursor part of the
// HW acceleration flags and skip all calculations for HideFloatingOverlays
// in case we don't have one.
// TODO: Watch out about penLocation and use Painter::PenLocation() when
// not using BoundindBox anymore.
	BRect b = fPainter->BoundingBox(string, length, pt, &penLocation, delta,
		&cacheReference);
	// stop here if we're supposed to render outside of the clipping
	DrawTransaction transaction(this, fPainter->ClipRect(b));
	if (transaction.IsDirty()) {
//printf("bounding box '%s': %lld us\n", string, system_time() - now);

//now = system_time();
		fPainter->DrawString(string, length, pt, delta, &cacheReference);
//printf("drawing string: %lld us\n", string, system_time() - now);
	}

	return penLocation;
}


/**
 * @brief Renders @a string with a per-glyph @a offsets array.
 *
 * @param string  UTF-8 string to render.
 * @param length  Length of @a string in bytes.
 * @param offsets Array of glyph positions, one per UTF-8 character.
 * @return  The pen position after rendering the string.
 */
BPoint
DrawingEngine::DrawString(const char* string, int32 length,
	const BPoint* offsets)
{
	ASSERT_PARALLEL_LOCKED();

	// use a FontCacheReference to speed up the second pass of
	// drawing the string
	FontCacheReference cacheReference;

	BPoint penLocation;
	BRect b = fPainter->BoundingBox(string, length, offsets, &penLocation,
		&cacheReference);
	// stop here if we're supposed to render outside of the clipping
	DrawTransaction transaction(this, fPainter->ClipRect(b));
	if (transaction.IsDirty()) {
//printf("bounding box '%s': %lld us\n", string, system_time() - now);

//now = system_time();
		fPainter->DrawString(string, length, offsets, &cacheReference);
//printf("drawing string: %lld us\n", string, system_time() - now);
	}

	return penLocation;
}


/**
 * @brief Returns the rendered width of @a string under the current font.
 */
float
DrawingEngine::StringWidth(const char* string, int32 length,
	escapement_delta* delta)
{
	return fPainter->StringWidth(string, length, delta);
}


/**
 * @brief Returns the rendered width of @a string under the supplied @a font.
 *
 * @note Independent of the current draw state; useful for Decorator code that
 *       must measure text without disturbing the engine state.
 */
float
DrawingEngine::StringWidth(const char* string, int32 length,
	const ServerFont& font, escapement_delta* delta)
{
	return font.StringWidth(string, length, delta);
}


/**
 * @brief Computes the pen advance for @a string at @a pt without rendering it.
 *
 * @return The pen position that would result from a real DrawString().
 */
BPoint
DrawingEngine::DrawStringDry(const char* string, int32 length,
	const BPoint& pt, escapement_delta* delta)
{
	ASSERT_PARALLEL_LOCKED();

	BPoint penLocation = pt;

	// try a fast path first
	if (fPainter->Font().Rotation() == 0.0f
		&& fPainter->IsIdentityTransform()) {
		penLocation.x += StringWidth(string, length, delta);
		return penLocation;
	}

	fPainter->BoundingBox(string, length, pt, &penLocation, delta, NULL);

	return penLocation;
}


/**
 * @brief Computes the pen advance for @a string with offsets, without rendering.
 */
BPoint
DrawingEngine::DrawStringDry(const char* string, int32 length,
	const BPoint* offsets)
{
	ASSERT_PARALLEL_LOCKED();

	BPoint penLocation;
	fPainter->BoundingBox(string, length, offsets, &penLocation, NULL);

	return penLocation;
}


// #pragma mark -


/**
 * @brief Returns a freshly allocated bitmap with the framebuffer contents.
 *
 * @return Always NULL in the base implementation; subclasses with screen
 *         capture support override it.
 */
ServerBitmap*
DrawingEngine::DumpToBitmap()
{
	return NULL;
}


/**
 * @brief Reads framebuffer pixels into @a bitmap, optionally including the cursor.
 *
 * Captures @a bounds from the front buffer into @a bitmap; when @a drawCursor
 * is true, composites the cursor sprite at its current location on top of
 * the captured pixels.
 *
 * @param bitmap     Destination bitmap; must be large enough for @a bounds.
 * @param drawCursor When true, blends the cursor onto the captured pixels.
 * @param bounds     Source rectangle in framebuffer coordinates; clipped to
 *                   the buffer.
 * @retval B_OK     Capture succeeded.
 * @retval B_ERROR  No front buffer available.
 *
 * @note The DrawingEngine must be exclusive-locked.
 */
status_t
DrawingEngine::ReadBitmap(ServerBitmap* bitmap, bool drawCursor, BRect bounds)
{
	ASSERT_EXCLUSIVE_LOCKED();

	RenderingBuffer* buffer = fGraphicsCard->FrontBuffer();
	if (buffer == NULL)
		return B_ERROR;

	BRect clip(0, 0, buffer->Width() - 1, buffer->Height() - 1);
	bounds = bounds & clip;
	AutoFloatingOverlaysHider _(fGraphicsCard, bounds);

	status_t result = bitmap->ImportBits(buffer->Bits(), buffer->BitsLength(),
		buffer->BytesPerRow(), buffer->ColorSpace(),
		bounds.LeftTop(), BPoint(0, 0),
		bounds.IntegerWidth() + 1, bounds.IntegerHeight() + 1);

	if (drawCursor) {
		ServerCursorReference cursorRef = fGraphicsCard->Cursor();
		ServerCursor* cursor = cursorRef.Get();
		if (!cursor)
			return result;
		int32 cursorWidth = cursor->Width();
		int32 cursorHeight = cursor->Height();

		BPoint cursorPosition = fGraphicsCard->CursorPosition();
		cursorPosition -= bounds.LeftTop() + cursor->GetHotSpot();

		BBitmap cursorArea(BRect(0, 0, cursorWidth - 1, cursorHeight - 1),
			B_BITMAP_NO_SERVER_LINK, B_RGBA32);

		cursorArea.ImportBits(bitmap->Bits(), bitmap->BitsLength(),
			bitmap->BytesPerRow(), bitmap->ColorSpace(),
			cursorPosition,	BPoint(0, 0),
			cursorArea.Bounds().Size());

		uint8* bits = (uint8*)cursorArea.Bits();
		uint8* cursorBits = (uint8*)cursor->Bits();
		for (int32 i = 0; i < cursorHeight; i++) {
			for (int32 j = 0; j < cursorWidth; j++) {
				uint8 alpha = 255 - cursorBits[3];
				bits[0] = ((bits[0] * alpha) >> 8) + cursorBits[0];
				bits[1] = ((bits[1] * alpha) >> 8) + cursorBits[1];
				bits[2] = ((bits[2] * alpha) >> 8) + cursorBits[2];
				cursorBits += 4;
				bits += 4;
			}
		}

		bitmap->ImportBits(cursorArea.Bits(), cursorArea.BitsLength(),
			cursorArea.BytesPerRow(), cursorArea.ColorSpace(),
			BPoint(0, 0), cursorPosition,
			cursorWidth, cursorHeight);
	}

	return result;
}


// #pragma mark -


/**
 * @brief Performs the actual back-buffer pixel move for one rect of CopyRegion().
 *
 * Calculates the clipped source / destination rectangles, walks the back
 * buffer, and dispatches to _CopyRect() to do the byte-level move.
 *
 * @param src     Source rectangle in framebuffer coordinates.
 * @param xOffset Horizontal translation in pixels.
 * @param yOffset Vertical translation in pixels.
 * @return        The destination rectangle that was actually touched.
 *
 * @todo Currently assumes the drawing buffer is 32 bits per pixel.
 */
BRect
DrawingEngine::CopyRect(BRect src, int32 xOffset, int32 yOffset) const
{
	// TODO: assumes drawing buffer is 32 bits (which it currently always is)
	BRect dst;
	RenderingBuffer* buffer = fGraphicsCard->DrawingBuffer();
	if (buffer) {
		BRect clip(0, 0, buffer->Width() - 1, buffer->Height() - 1);

		dst = src;
		dst.OffsetBy(xOffset, yOffset);

		if (clip.Intersects(src) && clip.Intersects(dst)) {
			uint32 bytesPerRow = buffer->BytesPerRow();
			uint8* bits = (uint8*)buffer->Bits();

			// clip source rect
			src = src & clip;
			// clip dest rect
			dst = dst & clip;
			// move dest back over source and clip source to dest
			dst.OffsetBy(-xOffset, -yOffset);
			src = src & dst;

			// calc offset in buffer
			bits += (ssize_t)src.left * 4 + (ssize_t)src.top * bytesPerRow;

			uint32 width = src.IntegerWidth() + 1;
			uint32 height = src.IntegerHeight() + 1;

			_CopyRect(bits, width, height, bytesPerRow,
				xOffset, yOffset);

			// offset dest again, because it is return value
			dst.OffsetBy(xOffset, yOffset);
		}
	}
	return dst;
}


/**
 * @brief Sets the renderer's offset (used when drawing into a sub-region).
 */
void
DrawingEngine::SetRendererOffset(int32 offsetX, int32 offsetY)
{
	fPainter->SetRendererOffset(offsetX, offsetY);
}


/**
 * @brief Performs the byte-level pixel block move for CopyRect().
 *
 * Chooses copy direction (top-to-bottom or bottom-to-top) based on the sign
 * of @a yOffset and uses memmove() instead of memcpy() when the source and
 * destination ranges overlap horizontally.
 *
 * @param src         Pointer to the first source pixel.
 * @param width       Number of pixels per row.
 * @param height      Number of rows.
 * @param bytesPerRow Stride in bytes.
 * @param xOffset     Horizontal translation in pixels.
 * @param yOffset     Vertical translation in pixels.
 *
 * @todo Currently assumes the drawing buffer is 32 bits per pixel.
 */
void
DrawingEngine::_CopyRect(uint8* src, uint32 width, uint32 height,
	uint32 bytesPerRow, int32 xOffset, int32 yOffset) const
{
	// TODO: assumes drawing buffer is 32 bits (which it currently always is)
	int32 yIncrement;
	const bool needMemmove = (yOffset == 0 && xOffset > 0 && uint32(xOffset) <= width);

	if (yOffset > 0) {
		// copy from bottom to top
		yIncrement = -bytesPerRow;
		src += (height - 1) * bytesPerRow;
	} else {
		// copy from top to bottom
		yIncrement = bytesPerRow;
	}

	uint8* dst = src + (ssize_t)yOffset * bytesPerRow + (ssize_t)xOffset * 4;

	if (!needMemmove) {
		for (uint32 y = 0; y < height; y++) {
			memcpy(dst, src, width * 4);
			src += yIncrement;
			dst += yIncrement;
		}
	} else {
		for (uint32 y = 0; y < height; y++) {
			memmove(dst, src, width * 4);
			src += yIncrement;
			dst += yIncrement;
		}
	}
}
