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
 * MIT License. Copyright 2001-2018, Haiku.
 * Original authors: DarkWyrm, Gabe Yoder, Stephan Aßmus, Julian Harnath.
 */

/** @file DrawingEngine.h
    @brief Software drawing primitives layered on top of an HWInterface (uses Painter / AGG). */

#ifndef DRAWING_ENGINE_H_
#define DRAWING_ENGINE_H_


#include <AutoDeleter.h>
#include <Accelerant.h>
#include <Font.h>
#include <Locker.h>
#include <Point.h>
#include <Gradient.h>
#include <ServerProtocolStructs.h>

#include "HWInterface.h"


class BPoint;
class BRect;
class BRegion;

class DrawState;
class Painter;
class ServerBitmap;
class ServerCursor;
class ServerFont;


/** @brief All drawing primitive operations layered on top of an HWInterface.
 *
 * DrawingEngine wraps a Painter (AGG-based software rasteriser) and binds it
 * to the framebuffer exposed by an HWInterface. Each Window owns its own
 * DrawingEngine; the engine receives clip regions, a DrawState (color, font,
 * pen, transform, ...), and primitive draw calls and produces pixels in the
 * back buffer. When @c fCopyToFront is enabled, dirty regions are scheduled
 * onto the front buffer through the underlying HWInterface.
 *
 * @see Painter, HWInterface, DrawState
 */
class DrawingEngine : public HWInterfaceListener {
public:
	/** @brief Constructs an engine optionally pre-bound to @a interface. */
							DrawingEngine(HWInterface* interface = NULL);
	virtual					~DrawingEngine();

	// HWInterfaceListener interface
	/** @brief Called by the HWInterface when its framebuffer pointer changes. */
	virtual	void			FrameBufferChanged();

	// for "changing" hardware
	/** @brief Re-binds the engine to a different HWInterface (possibly NULL). */
			void			SetHWInterface(HWInterface* interface);

	/** @brief Enables or disables automatic copy-back-to-front after each draw. */
	virtual	void			SetCopyToFrontEnabled(bool enable);
	/** @brief Returns whether copy-back-to-front is currently enabled. */
			bool			CopyToFrontEnabled() const
								{ return fCopyToFront; }
	/** @brief Schedules @a region for back-to-front refresh on the HWInterface. */
	virtual	void			CopyToFront(/*const*/ BRegion& region);

	// locking
	/** @brief Acquires the HWInterface's parallel (read) lock. */
			bool			LockParallelAccess();
#if DEBUG
	/** @brief Returns true while the calling thread holds the parallel lock (debug only). */
	virtual	bool			IsParallelAccessLocked() const;
#endif
	/** @brief Releases the HWInterface's parallel lock. */
			void			UnlockParallelAccess();

	/** @brief Acquires the HWInterface's exclusive (write) lock. */
			bool			LockExclusiveAccess();
	/** @brief Returns true while the calling thread holds the exclusive lock. */
	virtual	bool			IsExclusiveAccessLocked() const;
	/** @brief Releases the HWInterface's exclusive lock. */
			void			UnlockExclusiveAccess();

	// for screen shots
	/** @brief Returns a freshly allocated bitmap with the framebuffer contents (caller owns). */
			ServerBitmap*	DumpToBitmap();
	/** @brief Reads the framebuffer (or a portion of it) into @a bitmap.
	 *  @param bitmap     Destination bitmap.
	 *  @param drawCursor When true, composites the cursor onto the result.
	 *  @param bounds     Region of the framebuffer to read. */
	virtual	status_t		ReadBitmap(ServerBitmap *bitmap, bool drawCursor,
								BRect bounds);

	// clipping for all drawing functions, passing a NULL region
	// will remove any clipping (drawing allowed everywhere)
	/** @brief Constrains drawing to @a region; pass NULL to allow drawing everywhere. */
	virtual	void			ConstrainClippingRegion(const BRegion* region);

	/** @brief Sets the full DrawState (color, font, pen, transform); offsets shift origin. */
	virtual	void			SetDrawState(const DrawState* state,
								int32 xOffset = 0, int32 yOffset = 0);

	/** @brief Sets the high color used by stipple patterns. */
	virtual	void			SetHighColor(const rgb_color& color);
	/** @brief Sets the low color used by stipple patterns. */
	virtual	void			SetLowColor(const rgb_color& color);
	/** @brief Sets the pen size used by all stroking primitives. */
	virtual	void			SetPenSize(float size);
	/** @brief Sets line cap, join mode, and miter limit for stroked paths. */
	virtual	void			SetStrokeMode(cap_mode lineCap, join_mode joinMode,
								float miterLimit);
	/** @brief Selects between B_NONZERO and B_EVEN_ODD fill rules. */
	virtual void			SetFillRule(int32 fillRule);
	/** @brief Sets the active stipple pattern. */
	virtual	void			SetPattern(const struct pattern& pattern);
	/** @brief Sets the active drawing mode (B_OP_COPY, B_OP_OVER, ...). */
	virtual	void			SetDrawingMode(drawing_mode mode);
	/** @brief Sets the drawing mode and returns the previous one in @a oldMode. */
	virtual	void			SetDrawingMode(drawing_mode mode,
								drawing_mode& oldMode);
	/** @brief Configures the source-alpha and alpha-function used in B_OP_ALPHA. */
	virtual	void			SetBlendingMode(source_alpha srcAlpha,
								alpha_function alphaFunc);
	/** @brief Sets the current text font from a ServerFont. */
	virtual	void			SetFont(const ServerFont& font);
	/** @brief Sets the current text font from the font slot of @a state. */
	virtual	void			SetFont(const DrawState* state);
	/** @brief Sets the current view-to-screen affine transform. */
	virtual	void			SetTransform(const BAffineTransform& transform,
								int32 xOffset, int32 yOffset);

	// drawing functions
	/** @brief Topologically sorts and copies the rectangles of @a region by (xOffset, yOffset). */
	virtual	void			CopyRegion(/*const*/ BRegion* region,
								int32 xOffset, int32 yOffset);

	/** @brief Inverts the pixels inside @a r (XOR with white). */
	virtual	void			InvertRect(BRect r);

	/** @brief Blits @a bitmap into @a viewRect using @a bitmapRect as source.
	 *  @param options Bitmap drawing options (filter mode, tile mode, ...). */
	virtual	void			DrawBitmap(ServerBitmap* bitmap,
								const BRect& bitmapRect, const BRect& viewRect,
								uint32 options = 0);
	// drawing primitives
	/** @brief Draws an arc bounded by @a r, starting at @a angle, sweeping @a span degrees. */
	virtual	void			DrawArc(BRect r, const float& angle,
								const float& span, bool filled);
	/** @brief Draws a gradient-filled arc. */
	virtual	void			DrawArc(BRect r, const float& angle,
								const float& span, bool filled, const BGradient& gradient);

	/** @brief Draws a cubic Bezier curve through four control points in @a pts. */
	virtual	void			DrawBezier(BPoint* pts, bool filled);
	/** @brief Draws a gradient-filled cubic Bezier curve. */
	virtual	void			DrawBezier(BPoint* pts, bool filled, const BGradient& gradient);

	/** @brief Draws an ellipse fitting @a r. */
	virtual	void			DrawEllipse(BRect r, bool filled);
	/** @brief Draws a gradient-filled ellipse. */
	virtual	void			DrawEllipse(BRect r, bool filled, const BGradient& gradient);

	/** @brief Draws (filled or stroked) polygon described by @a ptlist / @a numpts. */
	virtual	void			DrawPolygon(BPoint* ptlist, int32 numpts,
								BRect bounds, bool filled, bool closed);
	/** @brief Draws a gradient-filled polygon. */
	virtual	void			DrawPolygon(BPoint* ptlist, int32 numpts,
								BRect bounds, bool filled, bool closed,
								const BGradient& gradient);

	// these rgb_color versions are used internally by the server
	/** @brief Plots a single pixel at @a point in the supplied @a color. */
	virtual	void			StrokePoint(const BPoint& point,
								const rgb_color& color);
	/** @brief Strokes a 1-pixel-wide rectangle in the supplied @a color (server-internal). */
	virtual	void			StrokeRect(BRect rect, const rgb_color &color);
	/** @brief Fills a rectangle with @a color (server-internal). */
	virtual	void			FillRect(BRect rect, const rgb_color &color);
	/** @brief Fills @a region with @a color (server-internal). */
	virtual	void			FillRegion(BRegion& region, const rgb_color& color);

	/** @brief Strokes a rectangle using the current draw state. */
	virtual	void			StrokeRect(BRect rect);
	/** @brief Strokes a rectangle filled with @a gradient along the stroke path. */
	virtual	void			StrokeRect(BRect rect, const BGradient& gradient);
	/** @brief Fills a rectangle using the current draw state. */
	virtual	void			FillRect(BRect rect);
	/** @brief Fills a rectangle with @a gradient. */
	virtual	void			FillRect(BRect rect, const BGradient& gradient);

	/** @brief Fills @a region using the current draw state. */
	virtual	void			FillRegion(BRegion& region);
	/** @brief Fills @a region with @a gradient. */
	virtual	void			FillRegion(BRegion& region,
								const BGradient& gradient);

	/** @brief Draws a rounded rectangle with corner radii (@a xrad, @a yrad). */
	virtual	void			DrawRoundRect(BRect rect, float xrad,
								float yrad, bool filled);
	/** @brief Draws a gradient-filled rounded rectangle. */
	virtual	void			DrawRoundRect(BRect rect, float xrad,
								float yrad, bool filled, const BGradient& gradient);

	/** @brief Renders a BShape described by @a opList / @a ptList.
	 *  @param viewToScreenOffset Offset added to all points before rasterising.
	 *  @param viewScale          Scale applied to all points before rasterising. */
	virtual	void			DrawShape(const BRect& bounds,
								int32 opcount, const uint32* oplist,
								int32 ptcount, const BPoint* ptlist,
								bool filled, const BPoint& viewToScreenOffset,
								float viewScale);
	/** @brief Renders a gradient-filled BShape. */
	virtual	void			DrawShape(const BRect& bounds,
								int32 opcount, const uint32* oplist,
								int32 ptcount, const BPoint* ptlist,
								bool filled, const BGradient& gradient,
								const BPoint& viewToScreenOffset,
								float viewScale);

	/** @brief Draws a triangle. */
	virtual	void			DrawTriangle(BPoint* points, const BRect& bounds,
								bool filled);
	/** @brief Draws a gradient-filled triangle. */
	virtual	void			DrawTriangle(BPoint* points, const BRect& bounds,
								bool filled, const BGradient& gradient);

	// these versions are used by the Decorator
	/** @brief Strokes a 1-pixel line in @a color; convenience entry point used by Decorator. */
	virtual	void			StrokeLine(const BPoint& start,
								const BPoint& end, const rgb_color& color);

	/** @brief Strokes a line with the current draw state. */
	virtual	void			StrokeLine(const BPoint& start,
								const BPoint& end);
	/** @brief Strokes a gradient-filled line. */
	virtual	void			StrokeLine(const BPoint& start,
								const BPoint& end, const BGradient& gradient);

	/** @brief Strokes an array of independently colored lines (BView::StrokeLineArray). */
	virtual	void			StrokeLineArray(int32 numlines,
								const ViewLineArrayInfo* data);

	// -------- text related calls

	// returns the pen position behind the (virtually) drawn
	// string
	/** @brief Renders @a string at @a pt and returns the resulting pen position. */
	virtual	BPoint			DrawString(const char* string, int32 length,
								const BPoint& pt,
								escapement_delta* delta = NULL);
	/** @brief Renders @a string with a per-glyph @a offsets array. */
	virtual	BPoint			DrawString(const char* string, int32 length,
								const BPoint* offsets);

	/** @brief Returns the rendered width of @a string under the current font. */
			float			StringWidth(const char* string, int32 length,
								escapement_delta* delta = NULL);

	// convenience function which is independent of graphics
	// state (to be used by Decorator or ServerApp etc)
	/** @brief Returns the rendered width of @a string under the supplied @a font. */
			float			StringWidth(const char* string,
								int32 length, const ServerFont& font,
								escapement_delta* delta = NULL);

	/** @brief Computes the pen advance for @a string without rendering it. */
			BPoint			DrawStringDry(const char* string, int32 length,
								const BPoint& pt,
								escapement_delta* delta = NULL);
	/** @brief Computes the pen advance for @a string with offsets, without rendering. */
			BPoint			DrawStringDry(const char* string, int32 length,
								const BPoint* offsets);


	// software rendering backend invoked by CopyRegion() for the sorted
	// individual rects
	/** @brief Performs the actual back-buffer pixel move for one rect of CopyRegion(). */
	virtual	BRect			CopyRect(BRect rect, int32 xOffset,
								int32 yOffset) const;

	/** @brief Sets the renderer's offset (used when drawing into a sub-region). */
			void			SetRendererOffset(int32 offsetX, int32 offsetY);

private:
	friend class DrawTransaction;

			void			_CopyRect(uint8* bits,
								uint32 width, uint32 height, uint32 bytesPerRow,
								int32 xOffset, int32 yOffset) const;

			ObjectDeleter<Painter>
							fPainter;
			HWInterface*	fGraphicsCard;
			bool			fCopyToFront;
};

#endif // DRAWING_ENGINE_H_
