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
 *   Copyright 2009-2010, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Lotz <mmlr@mlotz.ch>
 */


/**
 * @file RemoteDrawingEngine.cpp
 * @brief DrawingEngine that serialises every draw operation as RP_*
 *        RemoteMessage frames for the connected network viewer.
 *
 * Each instance owns a token registered with the global token space and
 * issues RP_CREATE_STATE on construction so the viewer can mirror its
 * draw state. State setters (high/low colour, pen size, drawing mode,
 * font, transform, clipping region) and primitives (rect / ellipse /
 * polygon / shape / line / bitmap / string) all map onto a single
 * RP_* opcode. Operations that need a result (DrawString position,
 * StringWidth, ReadBitmap) install a callback on the HWInterface and
 * block on fResultNotify until the matching RP_*_RESULT arrives.
 *
 * For features that have no equivalent on the wire (CopyRect,
 * stroke-line-array text mode), a private BitmapDrawingEngine is used to
 * render locally and the resulting bitmap is shipped with RP_DRAW_BITMAP
 * messages.
 */


#include "RemoteDrawingEngine.h"
#include "RemoteMessage.h"

#include "BitmapDrawingEngine.h"
#include "DrawState.h"
#include "ServerTokenSpace.h"

#include <Bitmap.h>
#include <utf8_functions.h>

#include <new>


#define TRACE(x...)				/*debug_printf("RemoteDrawingEngine: " x)*/
#define TRACE_ALWAYS(x...)		debug_printf("RemoteDrawingEngine: " x)
#define TRACE_ERROR(x...)		debug_printf("RemoteDrawingEngine: " x)


/**
 * @brief Constructs the engine, registers a token, and tells the viewer
 *        to allocate a parallel draw state.
 *
 * Sends RP_CREATE_STATE with the new token; the viewer keeps a state
 * object keyed by that token until RP_DELETE_STATE arrives.
 *
 * @param interface  Owning RemoteHWInterface; the engine borrows its
 *                   ring buffers and callback table.
 */
RemoteDrawingEngine::RemoteDrawingEngine(RemoteHWInterface* interface)
	:
	DrawingEngine(interface),
	fHWInterface(interface),
	fToken(gTokenSpace.NewToken(kRemoteDrawingEngineToken, this)),
	fExtendWidth(0),
	fCallbackAdded(false),
	fResultNotify(-1),
	fStringWidthResult(0.0f),
	fReadBitmapResult(NULL),
	fBitmapDrawingEngine(NULL)
{
	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_CREATE_STATE);
	message.Add(fToken);
}


/**
 * @brief Tells the viewer to drop the parallel state, removes the reply
 *        callback (if any), and frees the notification semaphore.
 */
RemoteDrawingEngine::~RemoteDrawingEngine()
{
	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_DELETE_STATE);
	message.Add(fToken);
	message.Flush();

	if (fCallbackAdded)
		fHWInterface->RemoveCallback(fToken);
	if (fResultNotify >= 0)
		delete_sem(fResultNotify);
}


// #pragma mark -


/**
 * @brief HWInterfaceListener hook; the remote engine never sees a local
 *        framebuffer change so this is intentionally a no-op.
 */
void
RemoteDrawingEngine::FrameBufferChanged()
{
	// Not allowed
}


// #pragma mark -


/**
 * @brief Forwards copy-to-front toggling to the viewer.
 *
 * Mirrors the local state in the base class and emits
 * RP_ENABLE_SYNC_DRAWING / RP_DISABLE_SYNC_DRAWING.
 *
 * @param enabled  true to make draws synchronous on the viewer side.
 */
void
RemoteDrawingEngine::SetCopyToFrontEnabled(bool enabled)
{
	DrawingEngine::SetCopyToFrontEnabled(enabled);

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(enabled ? RP_ENABLE_SYNC_DRAWING : RP_DISABLE_SYNC_DRAWING);
	message.Add(fToken);
}


// #pragma mark -


/**
 * @brief Forwards a clipping-region change to the viewer.
 *
 * Caches the region locally to elide redundant updates.
 *
 * @param region  New clipping region; NULL is not handled here (callers
 *                pass an empty region instead).
 * @note  The caller must hold the engine lock.
 */
void
RemoteDrawingEngine::ConstrainClippingRegion(const BRegion* region)
{
	if (fClippingRegion == *region)
		return;

	fClippingRegion = *region;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_CONSTRAIN_CLIPPING_REGION);
	message.Add(fToken);
	message.AddRegion(*region);
}


/**
 * @brief Forwards a complete DrawState plus view-to-screen offsets.
 *
 * Re-emits the individual setters (so the per-field cache stays in sync)
 * and follows up with RP_SET_OFFSETS for the (xOffset, yOffset) origin.
 *
 * @param state    Source draw state.
 * @param xOffset  View-to-screen X translation in pixels.
 * @param yOffset  View-to-screen Y translation in pixels.
 */
void
RemoteDrawingEngine::SetDrawState(const DrawState* state, int32 xOffset,
	int32 yOffset)
{
	SetPenSize(state->PenSize());
	SetDrawingMode(state->GetDrawingMode());
	SetBlendingMode(state->AlphaSrcMode(), state->AlphaFncMode());
	SetPattern(state->GetPattern().GetPattern());
	SetStrokeMode(state->LineCapMode(), state->LineJoinMode(),
		state->MiterLimit());
	SetHighColor(state->HighColor());
	SetLowColor(state->LowColor());
	SetFont(state->Font());
	SetTransform(state->CombinedTransform(), xOffset, yOffset);

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_SET_OFFSETS);
	message.Add(fToken);
	message.Add(xOffset);
	message.Add(yOffset);
}


/**
 * @brief Updates the high colour, deduplicating and emitting RP_SET_HIGH_COLOR.
 *
 * @param color  New high colour.
 */
void
RemoteDrawingEngine::SetHighColor(const rgb_color& color)
{
	if (fState.HighColor() == color)
		return;

	fState.SetHighColor(color);

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_SET_HIGH_COLOR);
	message.Add(fToken);
	message.Add(color);
}


/**
 * @brief Updates the low colour, deduplicating and emitting RP_SET_LOW_COLOR.
 *
 * @param color  New low colour.
 */
void
RemoteDrawingEngine::SetLowColor(const rgb_color& color)
{
	if (fState.LowColor() == color)
		return;

	fState.SetLowColor(color);

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_SET_LOW_COLOR);
	message.Add(fToken);
	message.Add(color);
}


/**
 * @brief Updates the pen size, recomputes the stroke half-width, and
 *        emits RP_SET_PEN_SIZE.
 *
 * @param size  New pen size in pixels.
 */
void
RemoteDrawingEngine::SetPenSize(float size)
{
	if (fState.PenSize() == size)
		return;

	fState.SetPenSize(size);
	fExtendWidth = -(size / 2);

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_SET_PEN_SIZE);
	message.Add(fToken);
	message.Add(size);
}


/**
 * @brief Updates the stroke parameters and emits RP_SET_STROKE_MODE.
 *
 * @param lineCap     New line-cap mode.
 * @param joinMode    New line-join mode.
 * @param miterLimit  New miter limit.
 */
void
RemoteDrawingEngine::SetStrokeMode(cap_mode lineCap, join_mode joinMode,
	float miterLimit)
{
	if (fState.LineCapMode() == lineCap && fState.LineJoinMode() == joinMode
		&& fState.MiterLimit() == miterLimit)
		return;

	fState.SetLineCapMode(lineCap);
	fState.SetLineJoinMode(joinMode);
	fState.SetMiterLimit(miterLimit);

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_SET_STROKE_MODE);
	message.Add(fToken);
	message.Add(lineCap);
	message.Add(joinMode);
	message.Add(miterLimit);
}


/**
 * @brief Updates the blending parameters and emits RP_SET_BLENDING_MODE.
 *
 * @param sourceAlpha  Source-alpha mode.
 * @param alphaFunc    Alpha function combining source and destination.
 */
void
RemoteDrawingEngine::SetBlendingMode(source_alpha sourceAlpha,
	alpha_function alphaFunc)
{
	if (fState.AlphaSrcMode() == sourceAlpha
		&& fState.AlphaFncMode() == alphaFunc)
		return;

	fState.SetBlendingMode(sourceAlpha, alphaFunc);

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_SET_BLENDING_MODE);
	message.Add(fToken);
	message.Add(sourceAlpha);
	message.Add(alphaFunc);
}


/**
 * @brief Updates the stipple pattern and emits RP_SET_PATTERN.
 *
 * @param pattern  New 8x8 stipple pattern.
 */
void
RemoteDrawingEngine::SetPattern(const struct pattern& pattern)
{
	if (fState.GetPattern() == pattern)
		return;

	fState.SetPattern(pattern);

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_SET_PATTERN);
	message.Add(fToken);
	message.Add(pattern);
}


/**
 * @brief Updates the drawing mode and emits RP_SET_DRAWING_MODE.
 *
 * @param mode  New drawing mode (B_OP_COPY, B_OP_OVER, ...).
 */
void
RemoteDrawingEngine::SetDrawingMode(drawing_mode mode)
{
	if (fState.GetDrawingMode() == mode)
		return;

	fState.SetDrawingMode(mode);

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_SET_DRAWING_MODE);
	message.Add(fToken);
	message.Add(mode);
}


/**
 * @brief Updates the drawing mode while reporting the previous value.
 *
 * @param mode     New drawing mode.
 * @param oldMode  Output, set to the previous drawing mode.
 */
void
RemoteDrawingEngine::SetDrawingMode(drawing_mode mode, drawing_mode& oldMode)
{
	oldMode = fState.GetDrawingMode();
	SetDrawingMode(mode);
}


/**
 * @brief Updates the active font and emits RP_SET_FONT.
 *
 * @param font  New font; copied into the cached state and serialised.
 */
void
RemoteDrawingEngine::SetFont(const ServerFont& font)
{
	if (fState.Font() == font)
		return;

	fState.SetFont(font);

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_SET_FONT);
	message.Add(fToken);
	message.AddFont(font);
}


/**
 * @brief Updates the active font from a DrawState's Font().
 *
 * @param state  Source draw state.
 */
void
RemoteDrawingEngine::SetFont(const DrawState* state)
{
	SetFont(state->Font());
}


/**
 * @brief Updates the affine transform and emits RP_SET_TRANSFORM.
 *
 * @param transform  New transform; identity is encoded compactly.
 * @param xOffset    View-to-screen X translation (currently unused on the
 *                   wire).
 * @param yOffset    View-to-screen Y translation (currently unused on the
 *                   wire).
 * @todo  Send the offsets along so the viewer can fold them into the
 *        transform server-side.
 */
void
RemoteDrawingEngine::SetTransform(const BAffineTransform& transform,
	int32 xOffset, int32 yOffset)
{
	// TODO: take offset into account

	if (fState.Transform() == transform)
		return;

	fState.SetTransform(transform);

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_SET_TRANSFORM);
	message.Add(fToken);
	message.AddTransform(transform);
}


// #pragma mark -


/**
 * @brief Asks the viewer to copy @a rect by (xOffset, yOffset).
 *
 * Used by CopyRegion() in the base class for each individually clipped
 * sub-rectangle; clipping has already been applied so the message uses
 * RP_COPY_RECT_NO_CLIPPING.
 *
 * @param rect     Source rectangle in screen coordinates.
 * @param xOffset  Destination X delta in pixels.
 * @param yOffset  Destination Y delta in pixels.
 * @return         The destination rectangle (input @a rect offset by the
 *                 deltas).
 */
BRect
RemoteDrawingEngine::CopyRect(BRect rect, int32 xOffset, int32 yOffset) const
{
	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_COPY_RECT_NO_CLIPPING);
	message.Add(xOffset);
	message.Add(yOffset);
	message.Add(rect);
	return rect.OffsetBySelf(xOffset, yOffset);
}


/**
 * @brief Asks the viewer to colour-invert @a rect, after early-out clipping.
 *
 * @param rect  Rectangle to invert in screen coordinates.
 */
void
RemoteDrawingEngine::InvertRect(BRect rect)
{
	if (!fClippingRegion.Intersects(rect))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_INVERT_RECT);
	message.Add(fToken);
	message.Add(rect);
}


/**
 * @brief Ships a bitmap blit, optionally pre-clipping into per-rect tiles.
 *
 * Clamps @a _bitmapRect to the bitmap's actual bounds (translating
 * @a _viewRect proportionally), intersects @a _viewRect with the
 * clipping region, and sends RP_DRAW_BITMAP_RECTS for the multi-rect
 * case (or any scale-down) and RP_DRAW_BITMAP for the simple case.
 *
 * @param bitmap       Source bitmap; must outlive the call.
 * @param _bitmapRect  Source rectangle in bitmap coordinates.
 * @param _viewRect    Destination rectangle in screen coordinates.
 * @param options      B_FILTER_BITMAP_BILINEAR / B_TILE_BITMAP / etc.
 * @todo  Cache or checksum bitmaps so unchanged ones are not retransmitted.
 */
void
RemoteDrawingEngine::DrawBitmap(ServerBitmap* bitmap, const BRect& _bitmapRect,
	const BRect& _viewRect, uint32 options)
{
	BRect bitmapRect = _bitmapRect;
	BRect viewRect = _viewRect;
	double xScale = (bitmapRect.Width() + 1) / (viewRect.Width() + 1);
	double yScale = (bitmapRect.Height() + 1) / (viewRect.Height() + 1);

	// constrain rect to passed bitmap bounds
	// and transfer the changes to the viewRect with the right scale
	BRect actualBitmapRect = bitmap->Bounds();
	if (bitmapRect.left < actualBitmapRect.left) {
		float diff = actualBitmapRect.left - bitmapRect.left;
		viewRect.left += diff / xScale;
		bitmapRect.left = actualBitmapRect.left;
	}
	if (bitmapRect.top < actualBitmapRect.top) {
		float diff = actualBitmapRect.top - bitmapRect.top;
		viewRect.top += diff / yScale;
		bitmapRect.top = actualBitmapRect.top;
	}
	if (bitmapRect.right > actualBitmapRect.right) {
		float diff = bitmapRect.right - actualBitmapRect.right;
		viewRect.right -= diff / xScale;
		bitmapRect.right = actualBitmapRect.right;
	}
	if (bitmapRect.bottom > actualBitmapRect.bottom) {
		float diff = bitmapRect.bottom - actualBitmapRect.bottom;
		viewRect.bottom -= diff / yScale;
		bitmapRect.bottom = actualBitmapRect.bottom;
	}

	BRegion clippedRegion(viewRect);
	clippedRegion.IntersectWith(&fClippingRegion);

	int32 rectCount = clippedRegion.CountRects();
	if (rectCount == 0)
		return;

	if (rectCount > 1 || (rectCount == 1 && clippedRegion.RectAt(0) != viewRect)
		|| viewRect.Width() < bitmapRect.Width()
		|| viewRect.Height() < bitmapRect.Height()) {
		UtilityBitmap** bitmaps;
		if (_ExtractBitmapRegions(*bitmap, options, bitmapRect, viewRect,
				xScale, yScale, clippedRegion, bitmaps) != B_OK) {
			return;
		}

		RemoteMessage message(NULL, fHWInterface->SendBuffer());
		message.Start(RP_DRAW_BITMAP_RECTS);
		message.Add(fToken);
		message.Add(options);
		message.Add(bitmap->ColorSpace());
		message.Add(bitmap->Flags());
		message.Add(rectCount);

		for (int32 i = 0; i < rectCount; i++) {
			message.Add(clippedRegion.RectAt(i));
			message.AddBitmap(*bitmaps[i], true);
			delete bitmaps[i];
		}

		free(bitmaps);
		return;
	}

	// TODO: we may want to cache/checksum bitmaps
	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_DRAW_BITMAP);
	message.Add(fToken);
	message.Add(bitmapRect);
	message.Add(viewRect);
	message.Add(options);
	message.AddBitmap(*bitmap);
}


/**
 * @brief Strokes or fills an arc.
 *
 * @param rect    Bounding rectangle.
 * @param angle   Start angle in degrees.
 * @param span    Sweep angle in degrees.
 * @param filled  true for fill, false for stroke.
 */
void
RemoteDrawingEngine::DrawArc(BRect rect, const float& angle, const float& span,
	bool filled)
{
	BRect bounds = rect;
	if (!filled)
		bounds.InsetBy(fExtendWidth, fExtendWidth);

	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(filled ? RP_FILL_ARC : RP_STROKE_ARC);
	message.Add(fToken);
	message.Add(rect);
	message.Add(angle);
	message.Add(span);
}

/**
 * @brief Gradient variant of DrawArc().
 *
 * @param rect      Bounding rectangle.
 * @param angle     Start angle in degrees.
 * @param span      Sweep angle in degrees.
 * @param filled    true for fill, false for stroke.
 * @param gradient  Gradient applied across the arc.
 */
void
RemoteDrawingEngine::DrawArc(BRect rect, const float& angle, const float& span,
	bool filled, const BGradient& gradient)
{
	if (!fClippingRegion.Intersects(rect))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(filled ? RP_FILL_ARC_GRADIENT : RP_STROKE_ARC_GRADIENT);
	message.Add(fToken);
	message.Add(rect);
	message.Add(angle);
	message.Add(span);
	message.AddGradient(gradient);
}


/**
 * @brief Strokes or fills a four-point Bezier curve.
 *
 * @param points  Array of exactly four control points.
 * @param filled  true for fill, false for stroke.
 */
void
RemoteDrawingEngine::DrawBezier(BPoint* points, bool filled)
{
	BRect bounds = _BuildBounds(points, 4);
	if (!filled)
		bounds.InsetBy(fExtendWidth, fExtendWidth);

	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(filled ? RP_FILL_BEZIER : RP_STROKE_BEZIER);
	message.Add(fToken);
	message.AddList(points, 4);
}


/**
 * @brief Gradient variant of DrawBezier().
 *
 * @param points    Four control points.
 * @param filled    true for fill, false for stroke.
 * @param gradient  Gradient applied across the curve.
 */
void
RemoteDrawingEngine::DrawBezier(BPoint* points, bool filled, const BGradient& gradient)
{
	BRect bounds = _BuildBounds(points, 4);
	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(filled ? RP_FILL_BEZIER_GRADIENT : RP_STROKE_BEZIER_GRADIENT);
	message.Add(fToken);
	message.AddList(points, 4);
	message.AddGradient(gradient);
}


/**
 * @brief Strokes or fills an ellipse inscribed in @a rect.
 *
 * @param rect    Bounding rectangle.
 * @param filled  true for fill, false for stroke.
 */
void
RemoteDrawingEngine::DrawEllipse(BRect rect, bool filled)
{
	BRect bounds = rect;
	if (!filled)
		bounds.InsetBy(fExtendWidth, fExtendWidth);

	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(filled ? RP_FILL_ELLIPSE : RP_STROKE_ELLIPSE);
	message.Add(fToken);
	message.Add(rect);
}


/**
 * @brief Gradient variant of DrawEllipse().
 *
 * @param rect      Bounding rectangle.
 * @param filled    true for fill, false for stroke.
 * @param gradient  Gradient applied across the ellipse.
 */
void
RemoteDrawingEngine::DrawEllipse(BRect rect, bool filled, const BGradient& gradient)
{
	if (!fClippingRegion.Intersects(rect))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(filled ? RP_FILL_ELLIPSE_GRADIENT : RP_STROKE_ELLIPSE_GRADIENT);
	message.Add(fToken);
	message.Add(rect);
	message.AddGradient(gradient);
}


/**
 * @brief Strokes or fills a polygon.
 *
 * @param pointList  Vertex array of length @a numPoints.
 * @param numPoints  Number of vertices.
 * @param bounds     Pre-computed axis-aligned bounding rectangle.
 * @param filled     true for fill, false for stroke.
 * @param closed     true to close the polygon back to the first vertex.
 */
void
RemoteDrawingEngine::DrawPolygon(BPoint* pointList, int32 numPoints,
	BRect bounds, bool filled, bool closed)
{
	BRect clipBounds = bounds;
	if (!filled)
		clipBounds.InsetBy(fExtendWidth, fExtendWidth);

	if (!fClippingRegion.Intersects(clipBounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(filled ? RP_FILL_POLYGON : RP_STROKE_POLYGON);
	message.Add(fToken);
	message.Add(bounds);
	message.Add(closed);
	message.Add(numPoints);
	for (int32 i = 0; i < numPoints; i++)
		message.Add(pointList[i]);
}


/**
 * @brief Gradient variant of DrawPolygon().
 *
 * @param pointList  Vertex array of length @a numPoints.
 * @param numPoints  Number of vertices.
 * @param bounds     Pre-computed bounding rectangle.
 * @param filled     true for fill, false for stroke.
 * @param closed     true to close the polygon back to the first vertex.
 * @param gradient   Gradient applied across the polygon.
 */
void
RemoteDrawingEngine::DrawPolygon(BPoint* pointList, int32 numPoints,
	BRect bounds, bool filled, bool closed, const BGradient& gradient)
{
	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(filled ? RP_FILL_POLYGON_GRADIENT : RP_STROKE_POLYGON_GRADIENT);
	message.Add(fToken);
	message.Add(bounds);
	message.Add(closed);
	message.Add(numPoints);
	for (int32 i = 0; i < numPoints; i++)
		message.Add(pointList[i]);
	message.AddGradient(gradient);
}


// #pragma mark - rgb_color versions


/**
 * @brief Strokes a single pixel-thick point in @a color (server-internal).
 *
 * @param point  Screen-space point.
 * @param color  Stroke colour.
 */
void
RemoteDrawingEngine::StrokePoint(const BPoint& point, const rgb_color& color)
{
	BRect bounds(point, point);
	bounds.InsetBy(fExtendWidth, fExtendWidth);

	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_STROKE_POINT_COLOR);
	message.Add(fToken);
	message.Add(point);
	message.Add(color);
}


/**
 * @brief Strokes a 1px line in @a color (server-internal Decorator path).
 *
 * @param start  Line start in screen space.
 * @param end    Line end in screen space.
 * @param color  Stroke colour.
 */
void
RemoteDrawingEngine::StrokeLine(const BPoint& start, const BPoint& end,
	const rgb_color& color)
{
	BPoint points[2] = { start, end };
	BRect bounds = _BuildBounds(points, 2);

	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_STROKE_LINE_1PX_COLOR);
	message.Add(fToken);
	message.AddList(points, 2);
	message.Add(color);
}


/**
 * @brief Strokes a 1px rectangle in @a color (server-internal Decorator path).
 *
 * @param rect   Rectangle to stroke.
 * @param color  Stroke colour.
 */
void
RemoteDrawingEngine::StrokeRect(BRect rect, const rgb_color &color)
{
	BRect bounds = rect;
	bounds.InsetBy(fExtendWidth, fExtendWidth);

	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_STROKE_RECT_1PX_COLOR);
	message.Add(fToken);
	message.Add(rect);
	message.Add(color);
}


/**
 * @brief Fills a rectangle with @a color (server-internal Decorator path).
 *
 * @param rect   Rectangle to fill.
 * @param color  Fill colour.
 */
void
RemoteDrawingEngine::FillRect(BRect rect, const rgb_color& color)
{
	if (!fClippingRegion.Intersects(rect))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_FILL_RECT_COLOR);
	message.Add(fToken);
	message.Add(rect);
	message.Add(color);
}


/**
 * @brief Fills a region with @a color, skipping the engine's own clipping.
 *
 * Used by the Decorator after pre-clipping has been done elsewhere.
 *
 * @param region  Region to fill.
 * @param color   Fill colour.
 */
void
RemoteDrawingEngine::FillRegion(BRegion& region, const rgb_color& color)
{
	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_FILL_REGION_COLOR_NO_CLIPPING);
	message.AddRegion(region);
	message.Add(color);
}


// #pragma mark - DrawState versions


/**
 * @brief Strokes a rectangle using the current draw state.
 *
 * @param rect  Rectangle to stroke.
 */
void
RemoteDrawingEngine::StrokeRect(BRect rect)
{
	BRect bounds = rect;
	bounds.InsetBy(fExtendWidth, fExtendWidth);

	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_STROKE_RECT);
	message.Add(fToken);
	message.Add(rect);
}


/**
 * @brief Strokes a rectangle filled with @a gradient.
 *
 * @param rect      Rectangle to stroke.
 * @param gradient  Gradient applied across the stroke.
 */
void
RemoteDrawingEngine::StrokeRect(BRect rect, const BGradient& gradient)
{
	BRect bounds = rect;
	bounds.InsetBy(fExtendWidth, fExtendWidth);

	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_STROKE_RECT_GRADIENT);
	message.Add(fToken);
	message.Add(rect);
	message.AddGradient(gradient);
}


/**
 * @brief Fills a rectangle using the current draw state.
 *
 * @param rect  Rectangle to fill.
 */
void
RemoteDrawingEngine::FillRect(BRect rect)
{
	if (!fClippingRegion.Intersects(rect))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_FILL_RECT);
	message.Add(fToken);
	message.Add(rect);
}


/**
 * @brief Fills a rectangle with @a gradient.
 *
 * @param rect      Rectangle to fill.
 * @param gradient  Gradient applied across the fill.
 */
void
RemoteDrawingEngine::FillRect(BRect rect, const BGradient& gradient)
{
	if (!fClippingRegion.Intersects(rect))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_FILL_RECT_GRADIENT);
	message.Add(fToken);
	message.Add(rect);
	message.AddGradient(gradient);
}


/**
 * @brief Fills a region using the current draw state.
 *
 * Sends the smaller of the input region and the clipped intersection to
 * minimise wire traffic.
 *
 * @param region  Region to fill (updated in-place during clipping).
 */
void
RemoteDrawingEngine::FillRegion(BRegion& region)
{
	BRegion clippedRegion = region;
	clippedRegion.IntersectWith(&fClippingRegion);
	if (clippedRegion.CountRects() == 0)
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_FILL_REGION);
	message.Add(fToken);
	message.AddRegion(clippedRegion.CountRects() < region.CountRects()
		? clippedRegion : region);
}


/**
 * @brief Fills a region with @a gradient.
 *
 * @param region    Region to fill.
 * @param gradient  Gradient applied across the fill.
 */
void
RemoteDrawingEngine::FillRegion(BRegion& region, const BGradient& gradient)
{
	BRegion clippedRegion = region;
	clippedRegion.IntersectWith(&fClippingRegion);
	if (clippedRegion.CountRects() == 0)
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_FILL_REGION_GRADIENT);
	message.Add(fToken);
	message.AddRegion(clippedRegion.CountRects() < region.CountRects()
		? clippedRegion : region);
	message.AddGradient(gradient);
}


/**
 * @brief Strokes or fills a rounded rectangle.
 *
 * @param rect     Bounding rectangle.
 * @param xRadius  X corner radius in pixels.
 * @param yRadius  Y corner radius in pixels.
 * @param filled   true for fill, false for stroke.
 */
void
RemoteDrawingEngine::DrawRoundRect(BRect rect, float xRadius, float yRadius,
	bool filled)
{
	BRect bounds = rect;
	if (!filled)
		bounds.InsetBy(fExtendWidth, fExtendWidth);

	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(filled ? RP_FILL_ROUND_RECT : RP_STROKE_ROUND_RECT);
	message.Add(fToken);
	message.Add(rect);
	message.Add(xRadius);
	message.Add(yRadius);
}


/**
 * @brief Gradient variant of DrawRoundRect().
 *
 * @param rect      Bounding rectangle.
 * @param xRadius   X corner radius in pixels.
 * @param yRadius   Y corner radius in pixels.
 * @param filled    true for fill, false for stroke.
 * @param gradient  Gradient applied across the rounded rectangle.
 */
void
RemoteDrawingEngine::DrawRoundRect(BRect rect, float xRadius, float yRadius,
	bool filled, const BGradient& gradient)
{
	if (!fClippingRegion.Intersects(rect))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(filled ? RP_FILL_ROUND_RECT_GRADIENT : RP_STROKE_ROUND_RECT_GRADIENT);
	message.Add(fToken);
	message.Add(rect);
	message.Add(xRadius);
	message.Add(yRadius);
	message.AddGradient(gradient);
}


/**
 * @brief Strokes or fills a BShape encoded as op-list and point-list.
 *
 * @param bounds              Pre-computed shape bounds.
 * @param opCount             Number of opcodes in @a opList.
 * @param opList              BShape opcode array.
 * @param pointCount          Number of points in @a pointList.
 * @param pointList           BShape point array.
 * @param filled              true for fill, false for stroke.
 * @param viewToScreenOffset  Translation applied to the shape.
 * @param viewScale           Uniform scale applied to the shape.
 */
void
RemoteDrawingEngine::DrawShape(const BRect& bounds, int32 opCount,
	const uint32* opList, int32 pointCount, const BPoint* pointList,
	bool filled, const BPoint& viewToScreenOffset, float viewScale)
{
	BRect clipBounds = bounds;
	if (!filled)
		clipBounds.InsetBy(fExtendWidth, fExtendWidth);

	if (!fClippingRegion.Intersects(clipBounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(filled ? RP_FILL_SHAPE : RP_STROKE_SHAPE);
	message.Add(fToken);
	message.Add(bounds);
	message.Add(opCount);
	message.AddList(opList, opCount);
	message.Add(pointCount);
	message.AddList(pointList, pointCount);
	message.Add(viewToScreenOffset);
	message.Add(viewScale);
}


/**
 * @brief Gradient variant of DrawShape().
 *
 * @param bounds              Pre-computed shape bounds.
 * @param opCount             Number of opcodes in @a opList.
 * @param opList              BShape opcode array.
 * @param pointCount          Number of points in @a pointList.
 * @param pointList           BShape point array.
 * @param filled              true for fill, false for stroke.
 * @param gradient            Gradient applied across the shape.
 * @param viewToScreenOffset  Translation applied to the shape.
 * @param viewScale           Uniform scale applied to the shape.
 */
void
RemoteDrawingEngine::DrawShape(const BRect& bounds, int32 opCount,
	const uint32* opList, int32 pointCount, const BPoint* pointList,
	bool filled, const BGradient& gradient, const BPoint& viewToScreenOffset,
	float viewScale)
{
	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(filled ? RP_FILL_SHAPE_GRADIENT : RP_STROKE_SHAPE_GRADIENT);
	message.Add(fToken);
	message.Add(bounds);
	message.Add(opCount);
	message.AddList(opList, opCount);
	message.Add(pointCount);
	message.AddList(pointList, pointCount);
	message.Add(viewToScreenOffset);
	message.Add(viewScale);
	message.AddGradient(gradient);
}


/**
 * @brief Strokes or fills a triangle.
 *
 * @param points  Three vertices.
 * @param bounds  Pre-computed bounding rectangle.
 * @param filled  true for fill, false for stroke.
 */
void
RemoteDrawingEngine::DrawTriangle(BPoint* points, const BRect& bounds,
	bool filled)
{
	BRect clipBounds = bounds;
	if (!filled)
		clipBounds.InsetBy(fExtendWidth, fExtendWidth);

	if (!fClippingRegion.Intersects(clipBounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(filled ? RP_FILL_TRIANGLE : RP_STROKE_TRIANGLE);
	message.Add(fToken);
	message.AddList(points, 3);
	message.Add(bounds);
}


/**
 * @brief Gradient variant of DrawTriangle().
 *
 * @param points    Three vertices.
 * @param bounds    Pre-computed bounding rectangle.
 * @param filled    true for fill, false for stroke.
 * @param gradient  Gradient applied across the triangle.
 */
void
RemoteDrawingEngine::DrawTriangle(BPoint* points, const BRect& bounds,
	bool filled, const BGradient& gradient)
{
	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(filled ? RP_FILL_TRIANGLE_GRADIENT : RP_STROKE_TRIANGLE_GRADIENT);
	message.Add(fToken);
	message.AddList(points, 3);
	message.Add(bounds);
	message.AddGradient(gradient);
}


/**
 * @brief Strokes a line using the current draw state.
 *
 * @param start  Line start in screen space.
 * @param end    Line end in screen space.
 */
void
RemoteDrawingEngine::StrokeLine(const BPoint &start, const BPoint &end)
{
	BPoint points[2] = { start, end };
	BRect bounds = _BuildBounds(points, 2);

	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_STROKE_LINE);
	message.Add(fToken);
	message.AddList(points, 2);
}


/**
 * @brief Gradient variant of StrokeLine().
 *
 * @param start     Line start in screen space.
 * @param end       Line end in screen space.
 * @param gradient  Gradient applied across the line.
 */
void
RemoteDrawingEngine::StrokeLine(const BPoint &start, const BPoint &end, const BGradient& gradient)
{
	BPoint points[2] = { start, end };
	BRect bounds = _BuildBounds(points, 2);

	if (!fClippingRegion.Intersects(bounds))
		return;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_STROKE_LINE_GRADIENT);
	message.Add(fToken);
	message.AddList(points, 2);
	message.AddGradient(gradient);
}


/**
 * @brief Strokes an array of lines, each with its own colour, in one
 *        RP_STROKE_LINE_ARRAY frame.
 *
 * @param numLines  Number of entries in @a lineData.
 * @param lineData  Array of (start, end, colour) tuples.
 */
void
RemoteDrawingEngine::StrokeLineArray(int32 numLines,
	const ViewLineArrayInfo *lineData)
{
	RemoteMessage message(NULL, fHWInterface->SendBuffer());
	message.Start(RP_STROKE_LINE_ARRAY);
	message.Add(fToken);
	message.Add(numLines);
	for (int32 i = 0; i < numLines; i++)
		message.AddArrayLine(lineData[i]);
}


// #pragma mark - string functions


/**
 * @brief Asks the viewer to draw a string and waits for the resulting
 *        pen position.
 *
 * Registers the per-token reply callback if needed, sends RP_DRAW_STRING,
 * and parks on fResultNotify until RP_DRAW_STRING_RESULT lands. Bails out
 * with the original @a point on flush, callback, or timeout failure.
 *
 * @param string  UTF-8 byte sequence; not required to be NUL-terminated.
 * @param length  Number of bytes in @a string.
 * @param point   Pen position to start drawing at.
 * @param delta   Optional per-character escapement deltas.
 * @return        New pen position reported by the viewer, or @a point
 *                if the round trip failed.
 */
BPoint
RemoteDrawingEngine::DrawString(const char* string, int32 length,
	const BPoint& point, escapement_delta* delta)
{
	RemoteMessage message(NULL, fHWInterface->SendBuffer());

	message.Start(RP_DRAW_STRING);
	message.Add(fToken);
	message.Add(point);
	message.AddString(string, length);
	message.Add(delta != NULL);
	if (delta != NULL)
		message.AddList(delta, length);

	status_t result = _AddCallback();
	if (message.Flush() != B_OK)
		return point;

	if (result != B_OK)
		return point;

	do {
		result = acquire_sem_etc(fResultNotify, 1, B_RELATIVE_TIMEOUT,
			1 * 1000 * 1000);
	} while (result == B_INTERRUPTED);

	if (result != B_OK)
		return point;

	return fDrawStringResult;
}


/**
 * @brief DrawString() variant that pins each character's position with an
 *        explicit offset list.
 *
 * @param string   UTF-8 byte sequence.
 * @param length   Number of bytes in @a string.
 * @param offsets  Array of one BPoint per UTF-8 character; must not be
 *                 NULL.
 * @return         New pen position reported by the viewer, or
 *                 @a offsets[0] if the round trip failed.
 */
BPoint
RemoteDrawingEngine::DrawString(const char* string, int32 length,
	const BPoint* offsets)
{
	// Guaranteed to have at least one point.
	RemoteMessage message(NULL, fHWInterface->SendBuffer());

	message.Start(RP_DRAW_STRING_WITH_OFFSETS);
	message.Add(fToken);
	message.AddString(string, length);
	message.AddList(offsets, UTF8CountChars(string, length));

	status_t result = _AddCallback();
	if (message.Flush() != B_OK)
		return offsets[0];

	if (result != B_OK)
		return offsets[0];

	do {
		result = acquire_sem_etc(fResultNotify, 1, B_RELATIVE_TIMEOUT,
			1 * 1000 * 1000);
	} while (result == B_INTERRUPTED);

	if (result != B_OK)
		return offsets[0];

	return fDrawStringResult;
}


/**
 * @brief Asks the viewer for the rendered width of @a string.
 *
 * Registers the reply callback (if needed), sends RP_STRING_WIDTH, and
 * blocks on fResultNotify until RP_STRING_WIDTH_RESULT is delivered.
 * Returns 0.0 on any flush, callback, or timeout failure.
 *
 * @param string  UTF-8 byte sequence.
 * @param length  Number of bytes in @a string.
 * @param delta   Optional per-character escapement deltas (not yet
 *                supported on the wire).
 * @return        Pixel width reported by the viewer, or 0.0 on failure.
 * @todo  Decide whether the round trip is worth the latency or whether a
 *        local approximation is acceptable.
 * @todo  Forward @a delta over the wire.
 */
float
RemoteDrawingEngine::StringWidth(const char* string, int32 length,
	escapement_delta* delta)
{
	// TODO: Decide if really needed.

	while (true) {
		if (_AddCallback() != B_OK)
			break;

		RemoteMessage message(NULL, fHWInterface->SendBuffer());

		message.Start(RP_STRING_WIDTH);
		message.Add(fToken);
		message.AddString(string, length);
			// TODO: Support escapement delta.

		if (message.Flush() != B_OK)
			break;

		status_t result;
		do {
			result = acquire_sem_etc(fResultNotify, 1, B_RELATIVE_TIMEOUT,
				1 * 1000 * 1000);
		} while (result == B_INTERRUPTED);

		if (result != B_OK)
			break;

		return fStringWidthResult;
	}

	// Fall back to local calculation.
	return fState.Font().StringWidth(string, length, delta);
}


// #pragma mark -


/**
 * @brief Asks the viewer for the pixels of a screen rectangle.
 *
 * Used by the screenshot path. Sends RP_READ_BITMAP and waits up to ten
 * seconds for RP_READ_BITMAP_RESULT.
 *
 * @param bitmap      Destination bitmap; must already be sized to @a bounds.
 * @param drawCursor  true to ask the viewer to composite the cursor into
 *                    the result.
 * @param bounds      Source rectangle in screen coordinates.
 * @return            B_OK on success, B_UNSUPPORTED if the round trip
 *                    fails or the viewer returned no payload, otherwise
 *                    the error from ImportBits().
 */
status_t
RemoteDrawingEngine::ReadBitmap(ServerBitmap* bitmap, bool drawCursor,
	BRect bounds)
{
	if (_AddCallback() != B_OK)
		return B_UNSUPPORTED;

	RemoteMessage message(NULL, fHWInterface->SendBuffer());

	message.Start(RP_READ_BITMAP);
	message.Add(fToken);
	message.Add(bounds);
	message.Add(drawCursor);
	if (message.Flush() != B_OK)
		return B_UNSUPPORTED;

	status_t result;
	do {
		result = acquire_sem_etc(fResultNotify, 1, B_RELATIVE_TIMEOUT,
			10 * 1000 * 1000);
	} while (result == B_INTERRUPTED);

	if (result != B_OK)
		return result;

	BBitmap* read = fReadBitmapResult;
	if (read == NULL)
		return B_UNSUPPORTED;

	result = bitmap->ImportBits(read->Bits(), read->BitsLength(),
		read->BytesPerRow(), read->ColorSpace());
	delete read;
	return result;
}


// #pragma mark -


/**
 * @brief Lazily registers the engine's per-token reply callback and
 *        creates the wakeup semaphore.
 *
 * Idempotent: subsequent calls return B_OK once the callback has been
 * installed.
 *
 * @return     B_OK on success, the negative semaphore error code if
 *             create_sem() fails, or whatever AddCallback() returns.
 */
status_t
RemoteDrawingEngine::_AddCallback()
{
	if (fCallbackAdded)
		return B_OK;

	if (fResultNotify < 0)
		fResultNotify = create_sem(0, "drawing engine result");
	if (fResultNotify < 0)
		return fResultNotify;

	status_t result = fHWInterface->AddCallback(fToken, &_DrawingEngineResult,
		this);

	fCallbackAdded = result == B_OK;
	return result;
}


/**
 * @brief Token reply callback that decodes the three round-trip results.
 *
 * Recognises RP_DRAW_STRING_RESULT, RP_STRING_WIDTH_RESULT, and
 * RP_READ_BITMAP_RESULT, stashes the payload in the engine, and releases
 * fResultNotify so the waiter wakes up.
 *
 * @param cookie   RemoteDrawingEngine* expected by the callback contract.
 * @param message  Decoded inbound message.
 * @return         true when the message was consumed, false to let the
 *                 dispatcher try other handlers.
 */
bool
RemoteDrawingEngine::_DrawingEngineResult(void* cookie, RemoteMessage& message)
{
	RemoteDrawingEngine* engine = (RemoteDrawingEngine*)cookie;

	switch (message.Code()) {
		case RP_DRAW_STRING_RESULT:
		{
			status_t result = message.Read(engine->fDrawStringResult);
			if (result != B_OK) {
				TRACE_ERROR("failed to read draw string result: %s\n",
					strerror(result));
				return false;
			}

			break;
		}

		case RP_STRING_WIDTH_RESULT:
		{
			status_t result = message.Read(engine->fStringWidthResult);
			if (result != B_OK) {
				TRACE_ERROR("failed to read string width result: %s\n",
					strerror(result));
				return false;
			}

			break;
		}

		case RP_READ_BITMAP_RESULT:
		{
			status_t result = message.ReadBitmap(&engine->fReadBitmapResult);
			if (result != B_OK) {
				TRACE_ERROR("failed to read bitmap of read bitmap result: %s\n",
					strerror(result));
				return false;
			}

			break;
		}

		default:
			return false;
	}

	release_sem(engine->fResultNotify);
	return true;
}


/**
 * @brief Computes an axis-aligned bounding box for an arbitrary point set.
 *
 * @param points      Point array of length @a pointCount.
 * @param pointCount  Number of points.
 * @return            BRect spanning every point.
 */
BRect
RemoteDrawingEngine::_BuildBounds(BPoint* points, int32 pointCount)
{
	BRect bounds(1000000, 1000000, 0, 0);
	for (int32 i = 0; i < pointCount; i++) {
		bounds.left = min_c(bounds.left, points[i].x);
		bounds.top = min_c(bounds.top, points[i].y);
		bounds.right = max_c(bounds.right, points[i].x);
		bounds.bottom = max_c(bounds.bottom, points[i].y);
	}

	return bounds;
}


/**
 * @brief Splits a bitmap blit into per-clip-rect tile bitmaps for
 *        RP_DRAW_BITMAP_RECTS.
 *
 * For each rectangle in @a region, computes the matching source slice in
 * the bitmap. When the destination is significantly smaller than the
 * source, scales locally through a private BitmapDrawingEngine to avoid
 * shipping pixels that would be discarded by the viewer; otherwise just
 * imports the relevant slice into a UtilityBitmap.
 *
 * @param bitmap      Source bitmap.
 * @param options     Blit options forwarded to the local scaler.
 * @param bitmapRect  Source rectangle in @a bitmap.
 * @param viewRect    Destination rectangle in screen coordinates.
 * @param xScale      Horizontal source/destination scale ratio.
 * @param yScale      Vertical source/destination scale ratio.
 * @param region      Pre-clipped destination region.
 * @param bitmaps     Output, malloc'd array of UtilityBitmap* the caller
 *                    frees together with each entry.
 * @return            B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
RemoteDrawingEngine::_ExtractBitmapRegions(ServerBitmap& bitmap, uint32 options,
	const BRect& bitmapRect, const BRect& viewRect, double xScale,
	double yScale, BRegion& region, UtilityBitmap**& bitmaps)
{
	int32 rectCount = region.CountRects();
	bitmaps = (UtilityBitmap**)malloc(rectCount * sizeof(UtilityBitmap*));
	if (bitmaps == NULL)
		return B_NO_MEMORY;

	for (int32 i = 0; i < rectCount; i++) {
		BRect sourceRect = region.RectAt(i).OffsetByCopy(-viewRect.LeftTop());
		int32 targetWidth = (int32)(sourceRect.Width() + 1.5);
		int32 targetHeight = (int32)(sourceRect.Height() + 1.5);

		if (xScale != 1.0) {
			sourceRect.left = (int32)(sourceRect.left * xScale + 0.5);
			sourceRect.right = (int32)(sourceRect.right * xScale + 0.5);
			if (xScale < 1.0)
				targetWidth = (int32)(sourceRect.Width() + 1.5);
		}

		if (yScale != 1.0) {
			sourceRect.top = (int32)(sourceRect.top * yScale + 0.5);
			sourceRect.bottom = (int32)(sourceRect.bottom * yScale + 0.5);
			if (yScale < 1.0)
				targetHeight = (int32)(sourceRect.Height() + 1.5);
		}

		sourceRect.OffsetBy(bitmapRect.LeftTop());
			// sourceRect is now the part of the bitmap we want copied

		status_t result = B_OK;
		if ((xScale > 1.0 || yScale > 1.0)
			&& (targetWidth * targetHeight < (int32)(sourceRect.Width() + 1.5)
				* (int32)(sourceRect.Height() + 1.5))) {
			// the target bitmap is smaller than the source, scale it locally
			// and send over the smaller version to avoid sending any extra data
			if (!fBitmapDrawingEngine.IsSet()) {
				fBitmapDrawingEngine.SetTo(
					new(std::nothrow) BitmapDrawingEngine(B_RGBA32));
				if (!fBitmapDrawingEngine.IsSet())
					result = B_NO_MEMORY;
			}

			if (result == B_OK) {
				result = fBitmapDrawingEngine->SetSize(targetWidth,
					targetHeight);
			}

			if (result == B_OK) {
				fBitmapDrawingEngine->SetDrawingMode(B_OP_COPY);

				switch (bitmap.ColorSpace()) {
					case B_RGBA32:
					case B_RGBA32_BIG:
					case B_RGBA15:
					case B_RGBA15_BIG:
						break;

					default:
					{
						// we need to clear the background if there may be
						// transparency through transparent magic (we use
						// B_OP_COPY when we draw alpha enabled bitmaps, so we
						// don't need to clear there)
						// TODO: this is not actually correct, as we're going to
						// loose the transparency with the conversion to the
						// original non-alpha colorspace happening in
						// ExportToBitmap
						rgb_color background = { 0, 0, 0, 0 };
						fBitmapDrawingEngine->FillRect(
							BRect(0, 0, targetWidth - 1, targetHeight -1),
							background);
						fBitmapDrawingEngine->SetDrawingMode(B_OP_OVER);
						break;
					}
				}

				fBitmapDrawingEngine->DrawBitmap(&bitmap, sourceRect,
					BRect(0, 0, targetWidth - 1, targetHeight - 1), options);
				bitmaps[i] = fBitmapDrawingEngine->ExportToBitmap(targetWidth,
					targetHeight, bitmap.ColorSpace());
				if (bitmaps[i] == NULL)
					result = B_NO_MEMORY;
			}
		} else {
			// source is smaller or equal target, extract the relevant rects
			// directly without any scaling and conversion
			targetWidth = (int32)(sourceRect.Width() + 1.5);
			targetHeight = (int32)(sourceRect.Height() + 1.5);

			bitmaps[i] = new(std::nothrow) UtilityBitmap(
				BRect(0, 0, targetWidth - 1, targetHeight - 1),
				bitmap.ColorSpace(), 0);
			if (bitmaps[i] == NULL) {
				result = B_NO_MEMORY;
			} else {
				result = bitmaps[i]->ImportBits(bitmap.Bits(),
						bitmap.BitsLength(), bitmap.BytesPerRow(),
						bitmap.ColorSpace(), sourceRect.LeftTop(),
						BPoint(0, 0), targetWidth, targetHeight);
				if (result != B_OK) {
					delete bitmaps[i];
					bitmaps[i] = NULL;
				}
			}
		}

		if (result != B_OK) {
			for (int32 j = 0; j < i; j++)
				delete bitmaps[j];
			free(bitmaps);
			return result;
		}
	}

	return B_OK;
}
