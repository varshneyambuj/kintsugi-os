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
 *   Copyright 2009, Christian Packmann.
 *   Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 *   Copyright 2005-2014, Stephan Aßmus <superstippi@gmx.de>.
 *   Copyright 2015, Julian Harnath <julian.harnath@rwth-aachen.de>
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file Painter.cpp
 * @brief Anti-Grain Geometry based software rasterizer for the app_server.
 *
 * Implements the Painter class, the workhorse 2D rasterizer that turns
 * BView draw commands (lines, polygons, bezier curves, shapes, rects,
 * round-rects, ellipses, arcs, strings, bitmaps and gradients) into pixel
 * writes on a destination buffer. It composes AGG rasterizers, scanlines
 * and renderers via PainterAggInterface, applies the active drawing mode,
 * pen size, pattern, clipping region and affine transform, and dispatches
 * text rendering through AGGTextRenderer and bitmap rendering through
 * BitmapPainter.
 *
 * @see PainterAggInterface, AGGTextRenderer, BitmapPainter, DrawingEngine
 */


#include "Painter.h"

#include <new>

#include <stdio.h>
#include <string.h>

#include <Bitmap.h>
#include <GraphicsDefs.h>
#include <Region.h>
#include <String.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <GradientRadialFocus.h>
#include <GradientDiamond.h>
#include <GradientConic.h>

#include <ShapePrivate.h>

#include <agg_bezier_arc.h>
#include <agg_bounding_rect.h>
#include <agg_conv_clip_polygon.h>
#include <agg_conv_curve.h>
#include <agg_conv_stroke.h>
#include <agg_ellipse.h>
#include <agg_image_accessors.h>
#include <agg_path_storage.h>
#include <agg_pixfmt_rgba.h>
#include <agg_rounded_rect.h>
#include <agg_span_allocator.h>
#include <agg_span_image_filter_rgba.h>
#include <agg_span_interpolator_linear.h>

#include "drawing_support.h"

#include "DrawState.h"

#include <AutoDeleter.h>
#include <View.h>

#include "AlphaMask.h"
#include "BitmapPainter.h"
#include "DrawingMode.h"
#include "GlobalSubpixelSettings.h"
#include "PatternHandler.h"
#include "RenderingBuffer.h"
#include "ServerBitmap.h"
#include "ServerFont.h"
#include "SystemPalette.h"

#include "AppServer.h"

using std::nothrow;

#undef TRACE
// #define TRACE_PAINTER
#ifdef TRACE_PAINTER
#	define TRACE(x...)		printf(x)
#else
#	define TRACE(x...)
#endif

//#define TRACE_GRADIENTS
#ifdef TRACE_GRADIENTS
#	include <OS.h>
#	define GTRACE(x...)		debug_printf(x)
#else
#	define GTRACE(x...)
#endif


#define CHECK_CLIPPING	if (!fValidClipping) return BRect(0, 0, -1, -1);
#define CHECK_CLIPPING_NO_RETURN	if (!fValidClipping) return;


// Shortcuts for accessing internal data
#define fBuffer					fInternal.fBuffer
#define fPixelFormat			fInternal.fPixelFormat
#define fBaseRenderer			fInternal.fBaseRenderer
#define fUnpackedScanline		fInternal.fUnpackedScanline
#define fPackedScanline			fInternal.fPackedScanline
#define fRasterizer				fInternal.fRasterizer
#define fRenderer				fInternal.fRenderer
#define fRendererBin			fInternal.fRendererBin
#define fSubpixPackedScanline	fInternal.fSubpixPackedScanline
#define fSubpixUnpackedScanline	fInternal.fSubpixUnpackedScanline
#define fSubpixRasterizer		fInternal.fSubpixRasterizer
#define fSubpixRenderer			fInternal.fSubpixRenderer
#define fMaskedUnpackedScanline	fInternal.fMaskedUnpackedScanline
#define fClippedAlphaMask		fInternal.fClippedAlphaMask
#define fPath					fInternal.fPath
#define fCurve					fInternal.fCurve


static uint32 detect_simd();

/** @brief Cached SIMD capability flags shared across the app_server. */
uint32 gSIMDFlags = detect_simd();


/**
 * @brief Probes every CPU for SIMD support and returns the common subset.
 *
 * Walks each CPU reported by the kernel, reads its CPUID feature bits and
 * intersects the per-CPU SIMD flag sets so the returned value only includes
 * instruction sets that work on every core. Non-x86 builds always return 0.
 *
 * @return Bitmask of APPSERVER_SIMD_* flags supported by every CPU.
 */
static uint32
detect_simd()
{
#if __i386__
	// Only scan CPUs for which we are certain the SIMD flags are properly
	// defined.
	const char* vendorNames[] = {
		"GenuineIntel",
		"AuthenticAMD",
		"CentaurHauls", // Via CPUs, MMX and SSE support
		"RiseRiseRise", // should be MMX-only
		"CyrixInstead", // MMX-only, but custom MMX extensions
		"GenuineTMx86", // MMX and SSE
		0
	};

	system_info systemInfo;
	if (get_system_info(&systemInfo) != B_OK)
		return 0;

	// We start out with all flags set and end up with only those flags
	// supported across all CPUs found.
	uint32 systemSIMD = 0xffffffff;

	for (uint32 cpu = 0; cpu < systemInfo.cpu_count; cpu++) {
		cpuid_info cpuInfo;
		get_cpuid(&cpuInfo, 0, cpu);

		// Get the vendor string and terminate it manually
		char vendor[13];
		memcpy(vendor, cpuInfo.eax_0.vendor_id, 12);
		vendor[12] = 0;

		bool vendorFound = false;
		for (uint32 i = 0; vendorNames[i] != 0; i++) {
			if (strcmp(vendor, vendorNames[i]) == 0)
				vendorFound = true;
		}

		uint32 cpuSIMD = 0;
		uint32 maxStdFunc = cpuInfo.regs.eax;
		if (vendorFound && maxStdFunc >= 1) {
			get_cpuid(&cpuInfo, 1, 0);
			uint32 edx = cpuInfo.regs.edx;
			if (edx & (1 << 23))
				cpuSIMD |= APPSERVER_SIMD_MMX;
			if (edx & (1 << 25))
				cpuSIMD |= APPSERVER_SIMD_SSE;
		} else {
			// no flags can be identified
			cpuSIMD = 0;
		}
		systemSIMD &= cpuSIMD;
	}
	return systemSIMD;
#else	// !__i386__
	return 0;
#endif
}


// Gradients and strings don't use patterns, but we want the special handling
// we have for solid patterns in certain modes to get the expected results for
// border antialiasing.
/**
 * @brief Scoped switch to B_SOLID_HIGH pattern with automatic restore.
 *
 * Used by gradient and string draw paths so they benefit from the optimized
 * solid-pattern code paths in the drawing modes (which would otherwise miss
 * the AA border tweaks). The previous pattern is restored when the guard
 * goes out of scope.
 */
class SolidPatternGuard {
public:
	/**
	 * @brief Stores the current pattern and forces it to B_SOLID_HIGH.
	 *
	 * @param painter Painter whose pattern is temporarily replaced; must
	 *        outlive the guard.
	 */
	SolidPatternGuard(Painter* painter)
		:
		fPainter(painter),
		fPattern(fPainter->Pattern())
	{
		fPainter->SetPattern(B_SOLID_HIGH);
	}

	/** @brief Restores the original pattern on the guarded Painter. */
	~SolidPatternGuard()
	{
		fPainter->SetPattern(fPattern);
	}

private:
	Painter*	fPainter;
	pattern		fPattern;
};


// #pragma mark -


/**
 * @brief Constructs a Painter in a detached, identity-state configuration.
 *
 * Initializes pen size, drawing mode (B_OP_COPY), alpha blending defaults,
 * line cap/join modes, the PatternHandler, the AGGTextRenderer (wired to
 * the AGG renderers), and the AGG pipeline aggregate (PainterAggInterface).
 * No buffer is attached and no clipping region is set; AttachToBuffer()
 * and ConstrainClipping() must be called before drawing.
 */
Painter::Painter()
	:
	fSubpixelPrecise(false),
	fValidClipping(false),
	fAttached(false),

	fPenSize(1.0),
	fClippingRegion(NULL),
	fDrawingMode(B_OP_COPY),
	fAlphaSrcMode(B_PIXEL_ALPHA),
	fAlphaFncMode(B_ALPHA_OVERLAY),
	fLineCapMode(B_BUTT_CAP),
	fLineJoinMode(B_MITER_JOIN),
	fMiterLimit(B_DEFAULT_MITER_LIMIT),

	fPatternHandler(),
	fTextRenderer(fSubpixRenderer, fRenderer, fRendererBin, fUnpackedScanline,
		fSubpixUnpackedScanline, fSubpixRasterizer, fMaskedUnpackedScanline,
		fTransform),
	fInternal(fPatternHandler)
{
	fPixelFormat.SetDrawingMode(fDrawingMode, fAlphaSrcMode, fAlphaFncMode);

#if ALIASED_DRAWING
	fRasterizer.gamma(agg::gamma_threshold(0.5));
	fSubpixRasterizer.gamma(agg:gamma_threshold(0.5));
#endif
}


/** @brief Destructor; pipeline objects clean up themselves. */
Painter::~Painter()
{
}


// #pragma mark -


/**
 * @brief Attaches this Painter to a destination rendering buffer.
 *
 * Validates that @a buffer is initialized and uses a 32-bit color space
 * (B_RGBA32 or B_RGB32), points the AGG rendering buffer at its bits, and
 * marks the Painter as attached. Clipping becomes valid only when a region
 * has also been set via ConstrainClipping(). Buffers in unsupported color
 * spaces are silently rejected.
 *
 * @param buffer Destination rendering buffer.
 */
void
Painter::AttachToBuffer(RenderingBuffer* buffer)
{
	if (buffer && buffer->InitCheck() >= B_OK
		&& (buffer->ColorSpace() == B_RGBA32
			|| buffer->ColorSpace() == B_RGB32)) {
		// TODO: implement drawing on B_RGB24, B_RGB15, B_RGB16,
		// B_CMAP8 and B_GRAY8 :-[
		// (if ever we want to support some devices where this gives
		// a great speed up, right now it seems fine, even in emulation)

		fBuffer.attach((uint8*)buffer->Bits(),
			buffer->Width(), buffer->Height(), buffer->BytesPerRow());

		fAttached = true;
		fValidClipping = fClippingRegion != NULL
			&& fClippingRegion->Frame().IsValid();

		// These are the AGG renderes and rasterizes which
		// will be used for stroking paths

		_SetRendererColor(fPatternHandler.HighColor());
	}
}


/**
 * @brief Detaches this Painter from its current buffer.
 *
 * After this call further drawing is suppressed until AttachToBuffer() is
 * invoked again.
 */
void
Painter::DetachFromBuffer()
{
	fBuffer.attach(NULL, 0, 0, 0);
	fAttached = false;
	fValidClipping = false;
}


/**
 * @brief Returns the destination buffer's pixel-area bounds.
 *
 * @return BRect of (0, 0, width-1, height-1) for the attached buffer.
 */
BRect
Painter::Bounds() const
{
	return BRect(0, 0, fBuffer.width() - 1, fBuffer.height() - 1);
}


// #pragma mark -


/**
 * @brief Adopts every drawing attribute from a DrawState.
 *
 * Copies the combined transform, pen size, font, subpixel precision flag,
 * alpha mask, drawing mode and pattern, line cap/join modes, miter limit,
 * fill rule, and high/low colors from @a state. The DrawState's private
 * clipping region is intentionally ignored — it is applied elsewhere by
 * the DrawingEngine.
 *
 * @param state   Source DrawState to copy attributes from.
 * @param xOffset Horizontal pattern/transform origin offset.
 * @param yOffset Vertical pattern/transform origin offset.
 * @note Used when switching the active view in ServerWindow or after a
 *       decorator has clobbered the pipeline state.
 */
void
Painter::SetDrawState(const DrawState* state, int32 xOffset, int32 yOffset)
{
	// NOTE: The custom clipping in "state" is ignored, because it has already
	// been taken into account elsewhere

	// NOTE: Usually this function is only used when the "current view"
	// is switched in the ServerWindow and after the decorator has drawn
	// and messed up the state. For other graphics state changes, the
	// Painter methods are used directly, so this function is much less
	// speed critical than it used to be.

	SetTransform(state->CombinedTransform(), xOffset, yOffset);

	SetPenSize(state->PenSize());

	SetFont(state);

	fSubpixelPrecise = state->SubPixelPrecise();

	if (state->GetAlphaMask() != NULL) {
		fMaskedUnpackedScanline = state->GetAlphaMask()->Scanline();
		fClippedAlphaMask = state->GetAlphaMask()->Mask();
	} else {
		fMaskedUnpackedScanline = NULL;
		fClippedAlphaMask = NULL;
	}

	// any of these conditions means we need to use a different drawing
	// mode instance, but when the pattern changes it is already changed
	// from SetPattern
	bool updateDrawingMode
		= state->GetPattern() == fPatternHandler.GetPattern()
			&& (state->GetDrawingMode() != fDrawingMode
				|| (state->GetDrawingMode() == B_OP_ALPHA
					&& (state->AlphaSrcMode() != fAlphaSrcMode
						|| state->AlphaFncMode() != fAlphaFncMode)));

	fDrawingMode = state->GetDrawingMode();
	fAlphaSrcMode = state->AlphaSrcMode();
	fAlphaFncMode = state->AlphaFncMode();
	SetPattern(state->GetPattern().GetPattern());
	fPatternHandler.SetOffsets(xOffset, yOffset);
	fLineCapMode = state->LineCapMode();
	fLineJoinMode = state->LineJoinMode();
	fMiterLimit = state->MiterLimit();

	SetFillRule(state->FillRule());

	// adopt the color *after* the pattern is set
	// to set the renderers to the correct color
	SetHighColor(state->HighColor());
	SetLowColor(state->LowColor());

	if (updateDrawingMode)
		_UpdateDrawingMode();
}


// #pragma mark - state


/**
 * @brief Sets the clipping region applied to every subsequent draw call.
 *
 * Stores @a region, propagates it to the AGG base renderer and the AA and
 * subpixel rasterizer clip boxes, and updates the validity flag used by
 * the CHECK_CLIPPING guards.
 *
 * @param region Clipping region; the Painter does not take ownership and
 *        the caller must keep it alive while drawing.
 */
void
Painter::ConstrainClipping(const BRegion* region)
{
	fClippingRegion = region;
	fBaseRenderer.set_clipping_region(const_cast<BRegion*>(region));
	fValidClipping = region->Frame().IsValid() && fAttached;

	if (fValidClipping) {
		clipping_rect cb = fClippingRegion->FrameInt();
		fRasterizer.clip_box(cb.left, cb.top, cb.right + 1, cb.bottom + 1);
		fSubpixRasterizer.clip_box(cb.left, cb.top, cb.right + 1, cb.bottom + 1);
	}
}


/**
 * @brief Sets the view-space affine transform.
 *
 * Builds the internal Transformable as translate(-offset) * @a transform *
 * translate(+offset) so the user-supplied transform pivots around the
 * view's logical origin. The identity flag short-circuits transform-aware
 * fast paths in the geometry helpers.
 *
 * @param transform Source BAffineTransform.
 * @param xOffset   Horizontal origin offset to apply before/after.
 * @param yOffset   Vertical origin offset to apply before/after.
 */
void
Painter::SetTransform(BAffineTransform transform, int32 xOffset, int32 yOffset)
{
	fIdentityTransform = transform.IsIdentity();
	if (!fIdentityTransform) {
		fTransform = agg::trans_affine_translation(-xOffset, -yOffset);
		fTransform *= agg::trans_affine(transform.sx, transform.shy,
			transform.shx, transform.sy, transform.tx, transform.ty);
		fTransform *= agg::trans_affine_translation(xOffset, yOffset);
	} else {
		fTransform.reset();
	}
}


/**
 * @brief Sets the high color used by B_SOLID_HIGH patterns.
 *
 * Skips work when the new color matches the current one. When the active
 * pattern is B_SOLID_HIGH, the AGG renderers are also reseeded with the
 * new color so the optimized solid drawing modes pick it up.
 *
 * @param color New high color.
 */
void
Painter::SetHighColor(const rgb_color& color)
{
	if (fPatternHandler.HighColor() == color)
		return;
	fPatternHandler.SetHighColor(color);
	if (*(fPatternHandler.GetR5Pattern()) == B_SOLID_HIGH)
		_SetRendererColor(color);
}


/**
 * @brief Sets the low color used by B_SOLID_LOW patterns.
 *
 * @param color New low color.
 */
void
Painter::SetLowColor(const rgb_color& color)
{
	fPatternHandler.SetLowColor(color);
	if (*(fPatternHandler.GetR5Pattern()) == B_SOLID_LOW)
		_SetRendererColor(color);
}


/**
 * @brief Selects the active BeOS drawing mode (B_OP_COPY, B_OP_OVER, etc.).
 *
 * Forwards to _UpdateDrawingMode() to refresh the PixelFormat-managed
 * drawing-mode dispatch table.
 *
 * @param mode New drawing mode.
 */
void
Painter::SetDrawingMode(drawing_mode mode)
{
	if (fDrawingMode != mode) {
		fDrawingMode = mode;
		_UpdateDrawingMode();
	}
}


/**
 * @brief Selects the alpha-source and alpha-function used by B_OP_ALPHA.
 *
 * Triggers a drawing-mode refresh only when the active mode is B_OP_ALPHA,
 * because the alpha pair has no effect outside it.
 *
 * @param srcAlpha  Source alpha selector (B_PIXEL_ALPHA or B_CONSTANT_ALPHA).
 * @param alphaFunc Alpha blending function (B_ALPHA_OVERLAY, B_ALPHA_COMPOSITE).
 */
void
Painter::SetBlendingMode(source_alpha srcAlpha, alpha_function alphaFunc)
{
	if (fAlphaSrcMode != srcAlpha || fAlphaFncMode != alphaFunc) {
		fAlphaSrcMode = srcAlpha;
		fAlphaFncMode = alphaFunc;
		if (fDrawingMode == B_OP_ALPHA)
			_UpdateDrawingMode();
	}
}


/**
 * @brief Sets the stroke pen size in pixels.
 *
 * @param size New pen size.
 */
void
Painter::SetPenSize(float size)
{
	fPenSize = size;
}


/**
 * @brief Sets line cap, line join, and miter limit for stroked geometry.
 *
 * @param lineCap    Cap style for line ends.
 * @param joinMode   Join style at corners.
 * @param miterLimit Maximum miter length before falling back to a bevel.
 */
void
Painter::SetStrokeMode(cap_mode lineCap, join_mode joinMode, float miterLimit)
{
	fLineCapMode = lineCap;
	fLineJoinMode = joinMode;
	fMiterLimit = miterLimit;
}


/**
 * @brief Sets the polygon fill rule on both AA and subpixel rasterizers.
 *
 * @param fillRule B_EVEN_ODD or B_NONZERO.
 */
void
Painter::SetFillRule(int32 fillRule)
{
	agg::filling_rule_e aggFillRule = fillRule == B_EVEN_ODD
		? agg::fill_even_odd : agg::fill_non_zero;

	fRasterizer.filling_rule(aggFillRule);
	fSubpixRasterizer.filling_rule(aggFillRule);
}


/**
 * @brief Sets the active 8x8 stipple pattern.
 *
 * Updates the drawing-mode dispatch (since the pixel format takes a fast
 * path for solid patterns) and reseeds the AGG renderer color when the
 * pattern collapses to B_SOLID_HIGH or B_SOLID_LOW.
 *
 * @param p New pattern.
 */
void
Painter::SetPattern(const pattern& p)
{
	if (p != *fPatternHandler.GetR5Pattern()) {
		fPatternHandler.SetPattern(p);
		_UpdateDrawingMode();

		// update renderer color if necessary
		if (fPatternHandler.IsSolidHigh()) {
			// pattern was not solid high before
			_SetRendererColor(fPatternHandler.HighColor());
		} else if (fPatternHandler.IsSolidLow()) {
			// pattern was not solid low before
			_SetRendererColor(fPatternHandler.LowColor());
		}
	}
}


/**
 * @brief Sets the font and AA flag from a ServerFont.
 *
 * @param font Source ServerFont.
 */
void
Painter::SetFont(const ServerFont& font)
{
	fTextRenderer.SetFont(font);
	fTextRenderer.SetAntialiasing(!(font.Flags() & B_DISABLE_ANTIALIASING));
}


/**
 * @brief Sets the font from a DrawState, honoring forced font aliasing.
 *
 * @param state Source DrawState (must be non-NULL).
 */
void
Painter::SetFont(const DrawState* state)
{
	fTextRenderer.SetFont(state->Font());
	fTextRenderer.SetAntialiasing(!state->ForceFontAliasing()
		&& (state->Font().Flags() & B_DISABLE_ANTIALIASING) == 0);
}


// #pragma mark - drawing


/**
 * @brief Strokes a single line from @a a to @a b with the current pen.
 *
 * Tries the optimized solid-color StraightLine() fast path first (1px pen,
 * identity transform, B_OP_COPY/B_OP_OVER, no alpha mask). Falls back to a
 * full path stroke for thick or transformed lines, with a special-case for
 * single-pixel "dots" when @a a equals @a b. Single-pixel non-subpixel lines
 * use B_SQUARE_CAP so endpoints are inclusive (R5 compatibility).
 *
 * @param a Start point.
 * @param b End point.
 */
void
Painter::StrokeLine(BPoint a, BPoint b)
{
	CHECK_CLIPPING_NO_RETURN

	// "false" means not to do the pixel center offset,
	// because it would mess up our optimized versions
	_Align(&a, false);
	_Align(&b, false);

	// first, try an optimized version
	if (fPenSize == 1.0 && fIdentityTransform
		&& (fDrawingMode == B_OP_COPY || fDrawingMode == B_OP_OVER)
		&& fMaskedUnpackedScanline == NULL) {
		pattern pat = *fPatternHandler.GetR5Pattern();
		if (pat == B_SOLID_HIGH
			&& StraightLine(a, b, fPatternHandler.HighColor())) {
			return;
		} else if (pat == B_SOLID_LOW
			&& StraightLine(a, b, fPatternHandler.LowColor())) {
			return;
		}
	}

	fPath.remove_all();

	if (a == b) {
		// special case dots
		if (fPenSize == 1.0 && !fSubpixelPrecise && fIdentityTransform) {
			if (fClippingRegion->Contains(a)) {
				int dotX = (int)a.x;
				int dotY = (int)a.y;
				fBaseRenderer.translate_to_base_ren(dotX, dotY);
				fPixelFormat.blend_pixel(dotX, dotY, fRenderer.color(),
					255);
			}
		} else {
			fPath.move_to(a.x, a.y);
			fPath.line_to(a.x + 1, a.y);
			fPath.line_to(a.x + 1, a.y + 1);
			fPath.line_to(a.x, a.y + 1);

			_FillPath(fPath);
		}
	} else {
		// Do the pixel center offset here
		if (!fSubpixelPrecise && fmodf(fPenSize, 2.0) != 0.0) {
			_Align(&a, true);
			_Align(&b, true);
		}

		fPath.move_to(a.x, a.y);
		fPath.line_to(b.x, b.y);

		if (!fSubpixelPrecise && fPenSize == 1.0f) {
			// Tweak ends to "include" the pixel at the index,
			// we need to do this in order to produce results like R5,
			// where coordinates were inclusive
			_StrokePath(fPath, B_SQUARE_CAP);
		} else
			_StrokePath(fPath);
	}
}


/**
 * @brief Strokes a line with a gradient stroke.
 *
 * Same shape logic as the solid-color overload but always rasterizes
 * through the gradient pipeline.
 *
 * @param a        Start point.
 * @param b        End point.
 * @param gradient Gradient to apply along the stroke.
 */
void
Painter::StrokeLine(BPoint a, BPoint b, const BGradient& gradient)
{
	CHECK_CLIPPING_NO_RETURN

	// "false" means not to do the pixel center offset,
	// because it would mess up our optimized versions
	_Align(&a, false);
	_Align(&b, false);

	fPath.remove_all();

	if (a == b) {
		// special case dots
		fPath.move_to(a.x, a.y);
		fPath.line_to(a.x + 1, a.y);
		fPath.line_to(a.x + 1, a.y + 1);
		fPath.line_to(a.x, a.y + 1);

		_FillPath(fPath, gradient);
	} else {
		// Do the pixel center offset here
		if (!fSubpixelPrecise && fmodf(fPenSize, 2.0) != 0.0) {
			_Align(&a, true);
			_Align(&b, true);
		}

		fPath.move_to(a.x, a.y);
		fPath.line_to(b.x, b.y);

		if (!fSubpixelPrecise && fPenSize == 1.0f) {
			// Tweak ends to "include" the pixel at the index,
			// we need to do this in order to produce results like R5,
			// where coordinates were inclusive
			_StrokePath(fPath, B_SQUARE_CAP, gradient);
		} else
			_StrokePath(fPath, gradient);
	}
}


/**
 * @brief Fast-path renderer for axis-aligned 1px solid lines.
 *
 * Iterates the clipping region's rectangles and writes raw 32-bit pixels
 * directly into the buffer for purely vertical or horizontal lines.
 *
 * @param a Start point.
 * @param b End point.
 * @param c Solid color to write (alpha is forced to 255).
 * @retval true  Line was vertical or horizontal and has been drawn.
 * @retval false Line is diagonal; the caller should fall back to AGG.
 */
bool
Painter::StraightLine(BPoint a, BPoint b, const rgb_color& c) const
{
	if (!fValidClipping)
		return false;

	if (a.x == b.x) {
		// vertical
		uint8* dst = fBuffer.row_ptr(0);
		uint32 bpr = fBuffer.stride();
		int32 x = (int32)a.x;
		dst += x * 4;
		int32 y1 = (int32)min_c(a.y, b.y);
		int32 y2 = (int32)max_c(a.y, b.y);
		pixel32 color;
		color.data8[0] = c.blue;
		color.data8[1] = c.green;
		color.data8[2] = c.red;
		color.data8[3] = 255;
		// draw a line, iterate over clipping boxes
		fBaseRenderer.first_clip_box();
		do {
			if (fBaseRenderer.xmin() <= x &&
				fBaseRenderer.xmax() >= x) {
				int32 i = max_c(fBaseRenderer.ymin(), y1);
				int32 end = min_c(fBaseRenderer.ymax(), y2);
				uint8* handle = dst + i * bpr;
				for (; i <= end; i++) {
					*(uint32*)handle = color.data32;
					handle += bpr;
				}
			}
		} while (fBaseRenderer.next_clip_box());

		return true;
	}

	if (a.y == b.y) {
		// horizontal
		int32 y = (int32)a.y;
		if (y < 0 || y >= (int32)fBuffer.height())
			return true;

		uint8* dst = fBuffer.row_ptr(y);
		int32 x1 = (int32)min_c(a.x, b.x);
		int32 x2 = (int32)max_c(a.x, b.x);
		pixel32 color;
		color.data8[0] = c.blue;
		color.data8[1] = c.green;
		color.data8[2] = c.red;
		color.data8[3] = 255;
		// draw a line, iterate over clipping boxes
		fBaseRenderer.first_clip_box();
		do {
			if (fBaseRenderer.ymin() <= y &&
				fBaseRenderer.ymax() >= y) {
				int32 i = max_c(fBaseRenderer.xmin(), x1);
				int32 end = min_c(fBaseRenderer.xmax(), x2);
				uint32* handle = (uint32*)(dst + i * 4);
				for (; i <= end; i++) {
					*handle++ = color.data32;
				}
			}
		} while (fBaseRenderer.next_clip_box());

		return true;
	}
	return false;
}


// #pragma mark -


/**
 * @brief Strokes a triangle outline.
 *
 * @param pt1 First vertex.
 * @param pt2 Second vertex.
 * @param pt3 Third vertex.
 * @return Bounding rectangle of the rendered geometry, clipped.
 */
BRect
Painter::StrokeTriangle(BPoint pt1, BPoint pt2, BPoint pt3) const
{
	return _DrawTriangle(pt1, pt2, pt3, false);
}


/**
 * @brief Strokes a triangle with a gradient stroke.
 *
 * @param pt1      First vertex.
 * @param pt2      Second vertex.
 * @param pt3      Third vertex.
 * @param gradient Gradient applied to the stroke.
 * @return Bounding rectangle of the rendered geometry, clipped.
 */
BRect
Painter::StrokeTriangle(BPoint pt1, BPoint pt2, BPoint pt3, const BGradient& gradient)
{
	return _DrawTriangle(pt1, pt2, pt3, false, gradient);
}


/**
 * @brief Fills a triangle with the current pattern.
 *
 * @param pt1 First vertex.
 * @param pt2 Second vertex.
 * @param pt3 Third vertex.
 * @return Bounding rectangle of the filled triangle, clipped.
 */
BRect
Painter::FillTriangle(BPoint pt1, BPoint pt2, BPoint pt3) const
{
	return _DrawTriangle(pt1, pt2, pt3, true);
}


/**
 * @brief Fills a triangle with a gradient.
 *
 * @param pt1      First vertex.
 * @param pt2      Second vertex.
 * @param pt3      Third vertex.
 * @param gradient Gradient to fill the triangle with.
 * @return Bounding rectangle of the filled triangle, clipped.
 */
BRect
Painter::FillTriangle(BPoint pt1, BPoint pt2, BPoint pt3,
	const BGradient& gradient)
{
	return _DrawTriangle(pt1, pt2, pt3, true, gradient);
}


/**
 * @brief Strokes or fills a polygon defined by an array of points.
 *
 * Aligns each vertex to pixel boundaries (with optional half-pixel center
 * offset for odd-width strokes), builds the path, and dispatches to
 * _StrokePath() or _FillPath().
 *
 * @param p      Vertex array, will be modified for alignment.
 * @param numPts Number of vertices.
 * @param filled True to fill, false to stroke.
 * @param closed True to close the polygon.
 * @return Bounding rectangle of the rendered polygon, clipped.
 */
BRect
Painter::DrawPolygon(BPoint* p, int32 numPts, bool filled, bool closed) const
{
	CHECK_CLIPPING

	if (numPts == 0)
		return BRect(0.0, 0.0, -1.0, -1.0);

	bool centerOffset = !filled && fIdentityTransform
		&& fmodf(fPenSize, 2.0) != 0.0;

	fPath.remove_all();

	_Align(p, centerOffset);
	fPath.move_to(p->x, p->y);

	for (int32 i = 1; i < numPts; i++) {
		p++;
		_Align(p, centerOffset);
		fPath.line_to(p->x, p->y);
	}

	if (closed)
		fPath.close_polygon();

	if (filled)
		return _FillPath(fPath);

	return _StrokePath(fPath);
}


/**
 * @brief Strokes or fills a polygon with a gradient.
 *
 * @param p        Vertex array, modified for alignment.
 * @param numPts   Number of vertices.
 * @param filled   True to fill, false to stroke.
 * @param closed   True to close the polygon.
 * @param gradient Gradient applied to the stroke or fill.
 * @return Bounding rectangle of the rendered polygon, clipped.
 */
BRect
Painter::DrawPolygon(BPoint* p, int32 numPts, bool filled, bool closed, const BGradient& gradient)
{
	CHECK_CLIPPING

	if (numPts == 0)
		return BRect(0.0, 0.0, -1.0, -1.0);

	bool centerOffset = !filled && fIdentityTransform
		&& fmodf(fPenSize, 2.0) != 0.0;

	fPath.remove_all();

	_Align(p, centerOffset);
	fPath.move_to(p->x, p->y);

	for (int32 i = 1; i < numPts; i++) {
		p++;
		_Align(p, centerOffset);
		fPath.line_to(p->x, p->y);
	}

	if (closed)
		fPath.close_polygon();

	if (filled)
		return _FillPath(fPath, gradient);

	return _StrokePath(fPath, gradient);
}


/**
 * @brief Strokes or fills a cubic Bezier curve defined by 4 control points.
 *
 * @param p      Array of four control points (start, ctrl1, ctrl2, end).
 * @param filled True to fill the curve as a closed shape.
 * @return Bounding rectangle of the rendered curve, clipped.
 */
BRect
Painter::DrawBezier(BPoint* p, bool filled) const
{
	CHECK_CLIPPING

	fPath.remove_all();

	_Align(&(p[0]));
	_Align(&(p[1]));
	_Align(&(p[2]));
	_Align(&(p[3]));

	fPath.move_to(p[0].x, p[0].y);
	fPath.curve4(p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y);

	if (filled) {
		fPath.close_polygon();
		return _FillPath(fCurve);
	}

	return _StrokePath(fCurve);
}


/**
 * @brief Strokes or fills a cubic Bezier curve with a gradient.
 *
 * @param p        Array of four control points.
 * @param filled   True to fill, false to stroke.
 * @param gradient Gradient to apply.
 * @return Bounding rectangle of the rendered curve, clipped.
 */
BRect
Painter::DrawBezier(BPoint* p, bool filled, const BGradient& gradient)
{
	CHECK_CLIPPING

	fPath.remove_all();

	_Align(&(p[0]));
	_Align(&(p[1]));
	_Align(&(p[2]));
	_Align(&(p[3]));

	fPath.move_to(p[0].x, p[0].y);
	fPath.curve4(p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y);

	if (filled) {
		fPath.close_polygon();
		return _FillPath(fCurve, gradient);
	}

	return _StrokePath(fCurve, gradient);
}


/**
 * @brief Renders a BShape-style operation/point list.
 *
 * Iterates the (opList, points) shape encoding through _IterateShapeData()
 * and either fills or strokes the resulting AGG path.
 *
 * @param opCount             Number of opcodes in @a opList.
 * @param opList              Shape opcodes (OP_MOVETO, OP_LINETO, etc.).
 * @param ptCount             Number of points referenced by @a points.
 * @param points              Point list.
 * @param filled              True to fill, false to stroke.
 * @param viewToScreenOffset  Translation applied to every point.
 * @param viewScale           Scale applied to every point.
 * @return Bounding rectangle of the rendered shape, clipped.
 */
BRect
Painter::DrawShape(const int32& opCount, const uint32* opList,
	const int32& ptCount, const BPoint* points, bool filled,
	const BPoint& viewToScreenOffset, float viewScale) const
{
	CHECK_CLIPPING

	_IterateShapeData(opCount, opList, ptCount, points, viewToScreenOffset,
		viewScale);

	if (filled)
		return _FillPath(fCurve);

	return _StrokePath(fCurve);
}


/**
 * @brief Renders a BShape with a gradient applied to its fill or stroke.
 *
 * @param opCount             Number of opcodes in @a opList.
 * @param opList              Shape opcodes.
 * @param ptCount             Number of points referenced by @a points.
 * @param points              Point list.
 * @param filled              True to fill, false to stroke.
 * @param gradient            Gradient to apply.
 * @param viewToScreenOffset  Translation applied to every point.
 * @param viewScale           Scale applied to every point.
 * @return Bounding rectangle of the rendered shape, clipped.
 */
BRect
Painter::DrawShape(const int32& opCount, const uint32* opList,
	const int32& ptCount, const BPoint* points, bool filled, const BGradient& gradient,
	const BPoint& viewToScreenOffset, float viewScale)
{
	CHECK_CLIPPING

	_IterateShapeData(opCount, opList, ptCount, points, viewToScreenOffset,
		viewScale);

	if (filled)
		return _FillPath(fCurve, gradient);

	return _StrokePath(fCurve, gradient);
}


/**
 * @brief Strokes a rectangle outline with the current pen and pattern.
 *
 * Tries the StraightLine() fast path for 1px strokes with B_OP_COPY/OVER
 * and a solid pattern; otherwise builds a closed path and runs the AGG
 * stroke pipeline. Single-pixel-width or single-pixel-height rectangles
 * collapse to a single line for accuracy.
 *
 * @param r Source rectangle.
 * @return Bounding rectangle of the stroke, clipped.
 */
BRect
Painter::StrokeRect(const BRect& r) const
{
	CHECK_CLIPPING

	BPoint a(r.left, r.top);
	BPoint b(r.right, r.bottom);
	_Align(&a, false);
	_Align(&b, false);

	// first, try an optimized version
	if (fPenSize == 1.0 && fIdentityTransform
			&& (fDrawingMode == B_OP_COPY || fDrawingMode == B_OP_OVER)
			&& fMaskedUnpackedScanline == NULL) {
		pattern p = *fPatternHandler.GetR5Pattern();
		if (p == B_SOLID_HIGH) {
			BRect rect(a, b);
			StrokeRect(rect, fPatternHandler.HighColor());
			return _Clipped(rect);
		} else if (p == B_SOLID_LOW) {
			BRect rect(a, b);
			StrokeRect(rect, fPatternHandler.LowColor());
			return _Clipped(rect);
		}
	}

	if (fIdentityTransform && fmodf(fPenSize, 2.0) != 0.0) {
		// shift coords to center of pixels
		a.x += 0.5;
		a.y += 0.5;
		b.x += 0.5;
		b.y += 0.5;
	}

	fPath.remove_all();
	fPath.move_to(a.x, a.y);
	if (a.x == b.x || a.y == b.y) {
		// special case rects with one pixel height or width
		fPath.line_to(b.x, b.y);
	} else {
		fPath.line_to(b.x, a.y);
		fPath.line_to(b.x, b.y);
		fPath.line_to(a.x, b.y);
	}
	fPath.close_polygon();

	return _StrokePath(fPath);
}


/**
 * @brief Strokes a rectangle with a gradient.
 *
 * @param r        Source rectangle.
 * @param gradient Gradient applied to the stroke.
 * @return Bounding rectangle of the stroke, clipped.
 */
BRect
Painter::StrokeRect(const BRect& r, const BGradient& gradient)
{
	CHECK_CLIPPING

	BPoint a(r.left, r.top);
	BPoint b(r.right, r.bottom);
	_Align(&a, false);
	_Align(&b, false);

	if (fIdentityTransform && fmodf(fPenSize, 2.0) != 0.0) {
		// shift coords to center of pixels
		a.x += 0.5;
		a.y += 0.5;
		b.x += 0.5;
		b.y += 0.5;
	}

	fPath.remove_all();
	fPath.move_to(a.x, a.y);
	if (a.x == b.x || a.y == b.y) {
		// special case rects with one pixel height or width
		fPath.line_to(b.x, b.y);
	} else {
		fPath.line_to(b.x, a.y);
		fPath.line_to(b.x, b.y);
		fPath.line_to(a.x, b.y);
	}
	fPath.close_polygon();

	return _StrokePath(fPath, gradient);
}


/**
 * @brief Strokes a 1-pixel solid-color rectangle without blending.
 *
 * Decomposes the rectangle into four StraightLine() calls; never anti-aliased.
 *
 * @param r Source rectangle.
 * @param c Solid color.
 */
void
Painter::StrokeRect(const BRect& r, const rgb_color& c) const
{
	StraightLine(BPoint(r.left, r.top), BPoint(r.right - 1, r.top), c);
	StraightLine(BPoint(r.right, r.top), BPoint(r.right, r.bottom - 1), c);
	StraightLine(BPoint(r.right, r.bottom), BPoint(r.left + 1, r.bottom), c);
	StraightLine(BPoint(r.left, r.bottom), BPoint(r.left, r.top + 1), c);
}


/**
 * @brief Fills a rectangle with the current pattern.
 *
 * Tries solid-color and B_OP_ALPHA fast paths first (see FillRect(rect, c)
 * and _BlendRect32()). Falls back to building a 4-vertex AGG path and
 * running the fill pipeline. Coordinates are interpreted as inclusive
 * pixel indices, then expanded by one to the AGG "pixel area" convention.
 *
 * @param r Source rectangle.
 * @return Bounding rectangle of the filled area, clipped.
 */
BRect
Painter::FillRect(const BRect& r) const
{
	CHECK_CLIPPING

	// support invalid rects
	BPoint a(min_c(r.left, r.right), min_c(r.top, r.bottom));
	BPoint b(max_c(r.left, r.right), max_c(r.top, r.bottom));
	_Align(&a, true, false);
	_Align(&b, true, false);

	// first, try an optimized version
	if ((fDrawingMode == B_OP_COPY || fDrawingMode == B_OP_OVER)
		&& fMaskedUnpackedScanline == NULL && fIdentityTransform) {
		pattern p = *fPatternHandler.GetR5Pattern();
		if (p == B_SOLID_HIGH) {
			BRect rect(a, b);
			FillRect(rect, fPatternHandler.HighColor());
			return _Clipped(rect);
		} else if (p == B_SOLID_LOW) {
			BRect rect(a, b);
			FillRect(rect, fPatternHandler.LowColor());
			return _Clipped(rect);
		}
	}
	if (fDrawingMode == B_OP_ALPHA && fAlphaFncMode == B_ALPHA_OVERLAY
		&& fMaskedUnpackedScanline == NULL && fIdentityTransform) {
		pattern p = *fPatternHandler.GetR5Pattern();
		if (p == B_SOLID_HIGH) {
			BRect rect(a, b);
			_BlendRect32(rect, fPatternHandler.HighColor());
			return _Clipped(rect);
		} else if (p == B_SOLID_LOW) {
			rgb_color c = fPatternHandler.LowColor();
			if (fAlphaSrcMode == B_CONSTANT_ALPHA)
				c.alpha = fPatternHandler.HighColor().alpha;
			BRect rect(a, b);
			_BlendRect32(rect, c);
			return _Clipped(rect);
		}
	}

	// account for stricter interpretation of coordinates in AGG
	// the rectangle ranges from the top-left (.0, .0)
	// to the bottom-right (.9999, .9999) corner of pixels
	b.x += 1.0;
	b.y += 1.0;

	fPath.remove_all();
	fPath.move_to(a.x, a.y);
	fPath.line_to(b.x, a.y);
	fPath.line_to(b.x, b.y);
	fPath.line_to(a.x, b.y);
	fPath.close_polygon();

	return _FillPath(fPath);
}


/**
 * @brief Fills a rectangle with a gradient.
 *
 * Vertical linear gradients short-circuit through FillRectVerticalGradient()
 * for big speedups; everything else goes through the generic AGG gradient
 * fill pipeline.
 *
 * @param r        Source rectangle.
 * @param gradient Gradient to apply.
 * @return Bounding rectangle of the filled area, clipped.
 */
BRect
Painter::FillRect(const BRect& r, const BGradient& gradient)
{
	CHECK_CLIPPING

	// support invalid rects
	BPoint a(min_c(r.left, r.right), min_c(r.top, r.bottom));
	BPoint b(max_c(r.left, r.right), max_c(r.top, r.bottom));
	_Align(&a, true, false);
	_Align(&b, true, false);

	// first, try an optimized version
	if (gradient.GetType() == BGradient::TYPE_LINEAR
		&& (fDrawingMode == B_OP_COPY || fDrawingMode == B_OP_OVER)
		&& fMaskedUnpackedScanline == NULL && fIdentityTransform) {
		const BGradientLinear* linearGradient
			= dynamic_cast<const BGradientLinear*>(&gradient);
		if (linearGradient->Start().x == linearGradient->End().x
			// TODO: Remove this second check once the optimized method
			// handled "upside down" gradients as well...
			&& linearGradient->Start().y <= linearGradient->End().y) {
			// a vertical gradient
			BRect rect(a, b);
			FillRectVerticalGradient(rect, *linearGradient);
			return _Clipped(rect);
		}
	}

	// account for stricter interpretation of coordinates in AGG
	// the rectangle ranges from the top-left (.0, .0)
	// to the bottom-right (.9999, .9999) corner of pixels
	b.x += 1.0;
	b.y += 1.0;

	fPath.remove_all();
	fPath.move_to(a.x, a.y);
	fPath.line_to(b.x, a.y);
	fPath.line_to(b.x, b.y);
	fPath.line_to(a.x, b.y);
	fPath.close_polygon();

	return _FillPath(fPath, gradient);
}


/**
 * @brief Fills a rectangle with a solid color, no blending.
 *
 * Walks the clipping region's rectangles and writes 32-bit pixels straight
 * into the buffer using gfxset32. Bypasses AGG entirely.
 *
 * @param r Source rectangle (inclusive pixel indices).
 * @param c Solid RGBA color.
 */
void
Painter::FillRect(const BRect& r, const rgb_color& c) const
{
	if (!fValidClipping)
		return;

	uint8* dst = fBuffer.row_ptr(0);
	uint32 bpr = fBuffer.stride();
	int32 left = (int32)r.left;
	int32 top = (int32)r.top;
	int32 right = (int32)r.right;
	int32 bottom = (int32)r.bottom;
	// get a 32 bit pixel ready with the color
	pixel32 color;
	color.data8[0] = c.blue;
	color.data8[1] = c.green;
	color.data8[2] = c.red;
	color.data8[3] = c.alpha;
	// fill rects, iterate over clipping boxes
	fBaseRenderer.first_clip_box();
	do {
		int32 x1 = max_c(fBaseRenderer.xmin(), left);
		int32 x2 = min_c(fBaseRenderer.xmax(), right);
		if (x1 <= x2) {
			int32 y1 = max_c(fBaseRenderer.ymin(), top);
			int32 y2 = min_c(fBaseRenderer.ymax(), bottom);
			uint8* offset = dst + x1 * 4;
			for (; y1 <= y2; y1++) {
//					uint32* handle = (uint32*)(offset + y1 * bpr);
//					for (int32 x = x1; x <= x2; x++) {
//						*handle++ = color.data32;
//					}
				gfxset32(offset + y1 * bpr, color.data32, (x2 - x1 + 1) * 4);
			}
		}
	} while (fBaseRenderer.next_clip_box());
}


/**
 * @brief Optimized solid-blend fill for vertical linear gradients.
 *
 * Pre-computes one row of gradient colors (length = rect height) using
 * _MakeGradient(), then writes it row by row through gfxset32 for each
 * clipping rectangle. Up-side-down gradients are not supported and
 * silently produce nothing.
 *
 * @param r        Destination rectangle, modified by intersecting with the clip.
 * @param gradient Vertical linear gradient. Caller must ensure verticality.
 */
void
Painter::FillRectVerticalGradient(BRect r,
	const BGradientLinear& gradient) const
{
	if (!fValidClipping)
		return;

	// Make sure the color array is no larger than the screen height.
	r = r & fClippingRegion->Frame();

	int32 gradientArraySize = r.IntegerHeight() + 1;
	uint32 gradientArray[gradientArraySize];
	int32 gradientTop = (int32)gradient.Start().y;
	int32 gradientBottom = (int32)gradient.End().y;
	int32 colorCount = gradientBottom - gradientTop + 1;
	if (colorCount < 0) {
		// Gradient is upside down. That's currently not supported by this
		// method.
		return;
	}

	_MakeGradient(gradient, colorCount, gradientArray,
		gradientTop - (int32)r.top, gradientArraySize);

	uint8* dst = fBuffer.row_ptr(0);
	uint32 bpr = fBuffer.stride();
	int32 left = (int32)r.left;
	int32 top = (int32)r.top;
	int32 right = (int32)r.right;
	int32 bottom = (int32)r.bottom;
	// fill rects, iterate over clipping boxes
	fBaseRenderer.first_clip_box();
	do {
		int32 x1 = max_c(fBaseRenderer.xmin(), left);
		int32 x2 = min_c(fBaseRenderer.xmax(), right);
		if (x1 <= x2) {
			int32 y1 = max_c(fBaseRenderer.ymin(), top);
			int32 y2 = min_c(fBaseRenderer.ymax(), bottom);
			uint8* offset = dst + x1 * 4;
			for (; y1 <= y2; y1++) {
//					uint32* handle = (uint32*)(offset + y1 * bpr);
//					for (int32 x = x1; x <= x2; x++) {
//						*handle++ = gradientArray[y1 - top];
//					}
				gfxset32(offset + y1 * bpr, gradientArray[y1 - top],
					(x2 - x1 + 1) * 4);
			}
		}
	} while (fBaseRenderer.next_clip_box());
}


/**
 * @brief Solid-color rect fill that skips the clipping region entirely.
 *
 * Caller is responsible for ensuring @a r lies within the buffer.
 *
 * @param r Rectangle in integer pixel coordinates.
 * @param c Solid RGBA color.
 */
void
Painter::FillRectNoClipping(const clipping_rect& r, const rgb_color& c) const
{
	int32 y = (int32)r.top;

	uint8* dst = fBuffer.row_ptr(y) + r.left * 4;
	uint32 bpr = fBuffer.stride();
	int32 bytes = (r.right - r.left + 1) * 4;

	// get a 32 bit pixel ready with the color
	pixel32 color;
	color.data8[0] = c.blue;
	color.data8[1] = c.green;
	color.data8[2] = c.red;
	color.data8[3] = c.alpha;

	for (; y <= r.bottom; y++) {
//			uint32* handle = (uint32*)dst;
//			for (int32 x = left; x <= right; x++) {
//				*handle++ = color.data32;
//			}
		gfxset32(dst, color.data32, bytes);
		dst += bpr;
	}
}


/**
 * @brief Strokes a rounded rectangle.
 *
 * @param r       Bounding rectangle.
 * @param xRadius Horizontal corner radius.
 * @param yRadius Vertical corner radius.
 * @return Bounding rectangle of the stroke, clipped.
 */
BRect
Painter::StrokeRoundRect(const BRect& r, float xRadius, float yRadius) const
{
	CHECK_CLIPPING

	BPoint lt(r.left, r.top);
	BPoint rb(r.right, r.bottom);
	bool centerOffset = fmodf(fPenSize, 2.0) != 0.0;
	_Align(&lt, centerOffset);
	_Align(&rb, centerOffset);

	agg::rounded_rect rect;
	rect.rect(lt.x, lt.y, rb.x, rb.y);
	rect.radius(xRadius, yRadius);

	return _StrokePath(rect);
}


/**
 * @brief Strokes a rounded rectangle with a gradient.
 *
 * @param r        Bounding rectangle.
 * @param xRadius  Horizontal corner radius.
 * @param yRadius  Vertical corner radius.
 * @param gradient Gradient applied to the stroke.
 * @return Bounding rectangle of the stroke, clipped.
 */
BRect
Painter::StrokeRoundRect(const BRect& r, float xRadius, float yRadius,
	const BGradient& gradient)
{
	CHECK_CLIPPING

	BPoint lt(r.left, r.top);
	BPoint rb(r.right, r.bottom);
	bool centerOffset = fmodf(fPenSize, 2.0) != 0.0;
	_Align(&lt, centerOffset);
	_Align(&rb, centerOffset);

	agg::rounded_rect rect;
	rect.rect(lt.x, lt.y, rb.x, rb.y);
	rect.radius(xRadius, yRadius);

	return _StrokePath(rect, gradient);
}


/**
 * @brief Fills a rounded rectangle with the current pattern.
 *
 * @param r       Bounding rectangle.
 * @param xRadius Horizontal corner radius.
 * @param yRadius Vertical corner radius.
 * @return Bounding rectangle of the filled area, clipped.
 */
BRect
Painter::FillRoundRect(const BRect& r, float xRadius, float yRadius) const
{
	CHECK_CLIPPING

	BPoint lt(r.left, r.top);
	BPoint rb(r.right, r.bottom);
	_Align(&lt, false);
	_Align(&rb, false);

	// account for stricter interpretation of coordinates in AGG
	// the rectangle ranges from the top-left (.0, .0)
	// to the bottom-right (.9999, .9999) corner of pixels
	rb.x += 1.0;
	rb.y += 1.0;

	agg::rounded_rect rect;
	rect.rect(lt.x, lt.y, rb.x, rb.y);
	rect.radius(xRadius, yRadius);

	return _FillPath(rect);
}


/**
 * @brief Fills a rounded rectangle with a gradient.
 *
 * @param r        Bounding rectangle.
 * @param xRadius  Horizontal corner radius.
 * @param yRadius  Vertical corner radius.
 * @param gradient Gradient to apply.
 * @return Bounding rectangle of the filled area, clipped.
 */
BRect
Painter::FillRoundRect(const BRect& r, float xRadius, float yRadius,
	const BGradient& gradient)
{
	CHECK_CLIPPING

	BPoint lt(r.left, r.top);
	BPoint rb(r.right, r.bottom);
	_Align(&lt, false);
	_Align(&rb, false);

	// account for stricter interpretation of coordinates in AGG
	// the rectangle ranges from the top-left (.0, .0)
	// to the bottom-right (.9999, .9999) corner of pixels
	rb.x += 1.0;
	rb.y += 1.0;

	agg::rounded_rect rect;
	rect.rect(lt.x, lt.y, rb.x, rb.y);
	rect.radius(xRadius, yRadius);

	return _FillPath(rect, gradient);
}


/**
 * @brief Aligns an ellipse bounding rectangle to pixel boundaries.
 *
 * Converts pixel indices to pixel area coordinates, and (for non-subpixel
 * strokes with odd pen size) insets by half a pixel so the stroke ends up
 * pixel-centered. Filled and subpixel rendering paths skip the alignment.
 *
 * @param rect   Rectangle to align in place.
 * @param filled True for fill, false for stroke (controls inset).
 */
void
Painter::AlignEllipseRect(BRect* rect, bool filled) const
{
	if (!fSubpixelPrecise) {
		// align rect to pixels
		align_rect_to_pixels(rect);
		// account for "pixel index" versus "pixel area"
		rect->right++;
		rect->bottom++;
		if (!filled && fmodf(fPenSize, 2.0) != 0.0) {
			// align the stroke
			rect->InsetBy(0.5, 0.5);
		}
	}
}


/**
 * @brief Strokes or fills an ellipse fitted in @a r.
 *
 * Picks an adaptive subdivision count based on the average radius and the
 * pen size, clamped to [12, 4096], and rasterizes through agg::ellipse.
 *
 * @param r    Bounding rectangle.
 * @param fill True to fill, false to stroke.
 * @return Bounding rectangle of the rendered ellipse, clipped.
 */
BRect
Painter::DrawEllipse(BRect r, bool fill) const
{
	CHECK_CLIPPING

	AlignEllipseRect(&r, fill);

	float xRadius = r.Width() / 2.0;
	float yRadius = r.Height() / 2.0;
	BPoint center(r.left + xRadius, r.top + yRadius);

	int32 divisions = (int32)((xRadius + yRadius + 2 * fPenSize) * M_PI / 2);
	if (divisions < 12)
		divisions = 12;
	if (divisions > 4096)
		divisions = 4096;

	agg::ellipse path(center.x, center.y, xRadius, yRadius, divisions);

	if (fill)
		return _FillPath(path);
	else
		return _StrokePath(path);
}


/**
 * @brief Strokes or fills an ellipse with a gradient.
 *
 * @param r        Bounding rectangle.
 * @param fill     True to fill, false to stroke.
 * @param gradient Gradient to apply.
 * @return Bounding rectangle of the rendered ellipse, clipped.
 */
BRect
Painter::DrawEllipse(BRect r, bool fill, const BGradient& gradient)
{
	CHECK_CLIPPING

	AlignEllipseRect(&r, fill);

	float xRadius = r.Width() / 2.0;
	float yRadius = r.Height() / 2.0;
	BPoint center(r.left + xRadius, r.top + yRadius);

	int32 divisions = (int32)((xRadius + yRadius + 2 * fPenSize) * M_PI / 2);
	if (divisions < 12)
		divisions = 12;
	if (divisions > 4096)
		divisions = 4096;

	agg::ellipse path(center.x, center.y, xRadius, yRadius, divisions);

	if (fill)
		return _FillPath(path, gradient);
	else
		return _StrokePath(path, gradient);
}


/**
 * @brief Strokes an elliptical arc.
 *
 * Builds the arc as a Bezier curve and runs the stroke pipeline. Both
 * @a angle and @a span are in degrees and increase counter-clockwise; the
 * implementation negates both to match BeOS's clockwise convention.
 *
 * @param center  Center of the ellipse.
 * @param xRadius Horizontal radius.
 * @param yRadius Vertical radius.
 * @param angle   Start angle in degrees.
 * @param span    Sweep angle in degrees.
 * @return Bounding rectangle of the stroked arc, clipped.
 */
BRect
Painter::StrokeArc(BPoint center, float xRadius, float yRadius, float angle,
	float span) const
{
	CHECK_CLIPPING

	_Align(&center);

	double angleRad = (angle * M_PI) / 180.0;
	double spanRad = (span * M_PI) / 180.0;
	agg::bezier_arc arc(center.x, center.y, xRadius, yRadius, -angleRad,
		-spanRad);

	agg::conv_curve<agg::bezier_arc> path(arc);
	path.approximation_scale(2.0);

	return _StrokePath(path);
}


/**
 * @brief Strokes an elliptical arc with a gradient.
 *
 * @param center   Center of the ellipse.
 * @param xRadius  Horizontal radius.
 * @param yRadius  Vertical radius.
 * @param angle    Start angle in degrees.
 * @param span     Sweep angle in degrees.
 * @param gradient Gradient applied to the stroke.
 * @return Bounding rectangle of the stroked arc, clipped.
 */
BRect
Painter::StrokeArc(BPoint center, float xRadius, float yRadius, float angle,
	float span, const BGradient& gradient)
{
	CHECK_CLIPPING

	_Align(&center);

	double angleRad = (angle * M_PI) / 180.0;
	double spanRad = (span * M_PI) / 180.0;
	agg::bezier_arc arc(center.x, center.y, xRadius, yRadius, -angleRad,
		-spanRad);

	agg::conv_curve<agg::bezier_arc> path(arc);
	path.approximation_scale(2.0);

	return _StrokePath(path, gradient);
}


/**
 * @brief Fills a pie slice traced from the arc back to the center.
 *
 * @param center  Center of the ellipse.
 * @param xRadius Horizontal radius.
 * @param yRadius Vertical radius.
 * @param angle   Start angle in degrees.
 * @param span    Sweep angle in degrees.
 * @return Bounding rectangle of the filled slice, clipped.
 */
BRect
Painter::FillArc(BPoint center, float xRadius, float yRadius, float angle,
	float span) const
{
	CHECK_CLIPPING

	_Align(&center);

	double angleRad = (angle * M_PI) / 180.0;
	double spanRad = (span * M_PI) / 180.0;
	agg::bezier_arc arc(center.x, center.y, xRadius, yRadius, -angleRad,
		-spanRad);

	agg::conv_curve<agg::bezier_arc> segmentedArc(arc);

	fPath.remove_all();

	// build a new path by starting at the center point,
	// then traversing the arc, then going back to the center
	fPath.move_to(center.x, center.y);

	segmentedArc.rewind(0);
	double x;
	double y;
	unsigned cmd = segmentedArc.vertex(&x, &y);
	while (!agg::is_stop(cmd)) {
		fPath.line_to(x, y);
		cmd = segmentedArc.vertex(&x, &y);
	}

	fPath.close_polygon();

	return _FillPath(fPath);
}


/**
 * @brief Fills a pie slice with a gradient.
 *
 * @param center   Center of the ellipse.
 * @param xRadius  Horizontal radius.
 * @param yRadius  Vertical radius.
 * @param angle    Start angle in degrees.
 * @param span     Sweep angle in degrees.
 * @param gradient Gradient to apply.
 * @return Bounding rectangle of the filled slice, clipped.
 */
BRect
Painter::FillArc(BPoint center, float xRadius, float yRadius, float angle,
	float span, const BGradient& gradient)
{
	CHECK_CLIPPING

	_Align(&center);

	double angleRad = (angle * M_PI) / 180.0;
	double spanRad = (span * M_PI) / 180.0;
	agg::bezier_arc arc(center.x, center.y, xRadius, yRadius, -angleRad,
		-spanRad);

	agg::conv_curve<agg::bezier_arc> segmentedArc(arc);

	fPath.remove_all();

	// build a new path by starting at the center point,
	// then traversing the arc, then going back to the center
	fPath.move_to(center.x, center.y);

	segmentedArc.rewind(0);
	double x;
	double y;
	unsigned cmd = segmentedArc.vertex(&x, &y);
	while (!agg::is_stop(cmd)) {
		fPath.line_to(x, y);
		cmd = segmentedArc.vertex(&x, &y);
	}

	fPath.close_polygon();

	return _FillPath(fPath, gradient);
}


// #pragma mark -


/**
 * @brief Renders a UTF-8 string anchored at @a baseLine.
 *
 * Snaps the baseline to integer pixels in non-subpixel mode, forces a
 * solid pattern through SolidPatternGuard, and dispatches to the
 * AGGTextRenderer.
 *
 * @param utf8String     Source string.
 * @param length         Byte length of @a utf8String.
 * @param baseLine       Pen origin in view space.
 * @param delta          Optional escapement delta (per-character spacing).
 * @param cacheReference Optional cache reference for the glyph cache.
 * @return Bounding rectangle of the rendered text, clipped.
 */
BRect
Painter::DrawString(const char* utf8String, uint32 length, BPoint baseLine,
	const escapement_delta* delta, FontCacheReference* cacheReference)
{
	CHECK_CLIPPING

	if (!fSubpixelPrecise) {
		baseLine.x = roundf(baseLine.x);
		baseLine.y = roundf(baseLine.y);
	}

	BRect bounds;

	SolidPatternGuard _(this);

	bounds = fTextRenderer.RenderString(utf8String, length,
		baseLine, fClippingRegion->Frame(), false, NULL, delta,
		cacheReference);

	return _Clipped(bounds);
}


/**
 * @brief Renders a UTF-8 string with explicit per-character positions.
 *
 * @param utf8String     Source string.
 * @param length         Byte length of @a utf8String.
 * @param offsets        Array of per-character pen positions.
 * @param cacheReference Optional cache reference for the glyph cache.
 * @return Bounding rectangle of the rendered text, clipped.
 */
BRect
Painter::DrawString(const char* utf8String, uint32 length,
	const BPoint* offsets, FontCacheReference* cacheReference)
{
	CHECK_CLIPPING

	// TODO: Round offsets to device pixel grid if !fSubpixelPrecise?

	BRect bounds;

	SolidPatternGuard _(this);

	bounds = fTextRenderer.RenderString(utf8String, length,
		offsets, fClippingRegion->Frame(), false, NULL,
		cacheReference);

	return _Clipped(bounds);
}


/**
 * @brief Measures a UTF-8 string anchored at @a baseLine without drawing.
 *
 * Runs the AGGTextRenderer in dry-run mode.
 *
 * @param utf8String     Source string.
 * @param length         Byte length of @a utf8String.
 * @param baseLine       Pen origin in view space.
 * @param penLocation    Optional out parameter for the pen position after measurement.
 * @param delta          Optional escapement delta.
 * @param cacheReference Optional glyph cache reference.
 * @return Bounding rectangle that drawing the string would produce.
 */
BRect
Painter::BoundingBox(const char* utf8String, uint32 length, BPoint baseLine,
	BPoint* penLocation, const escapement_delta* delta,
	FontCacheReference* cacheReference) const
{
	if (!fSubpixelPrecise) {
		baseLine.x = roundf(baseLine.x);
		baseLine.y = roundf(baseLine.y);
	}

	static BRect dummy;
	return fTextRenderer.RenderString(utf8String, length,
		baseLine, dummy, true, penLocation, delta, cacheReference);
}


/**
 * @brief Measures a UTF-8 string with explicit per-character offsets.
 *
 * @param utf8String     Source string.
 * @param length         Byte length of @a utf8String.
 * @param offsets        Per-character pen positions.
 * @param penLocation    Optional out parameter for pen position.
 * @param cacheReference Optional glyph cache reference.
 * @return Bounding rectangle that drawing the string would produce.
 */
BRect
Painter::BoundingBox(const char* utf8String, uint32 length,
	const BPoint* offsets, BPoint* penLocation,
	FontCacheReference* cacheReference) const
{
	// TODO: Round offsets to device pixel grid if !fSubpixelPrecise?

	static BRect dummy;
	return fTextRenderer.RenderString(utf8String, length,
		offsets, dummy, true, penLocation, cacheReference);
}


/**
 * @brief Returns the advance width of a UTF-8 string under the active font.
 *
 * @param utf8String Source string.
 * @param length     Byte length of @a utf8String.
 * @param delta      Optional escapement delta (per-character spacing).
 * @return Total advance width in pixels.
 */
float
Painter::StringWidth(const char* utf8String, uint32 length,
	const escapement_delta* delta)
{
	return Font().StringWidth(utf8String, length, delta);
}


// #pragma mark -


/**
 * @brief Draws a ServerBitmap into the destination view rectangle.
 *
 * Delegates to BitmapPainter when the transformed view rectangle is non-empty.
 *
 * @param bitmap     Source bitmap.
 * @param bitmapRect Source rectangle inside the bitmap.
 * @param viewRect   Destination rectangle in view space.
 * @param options    Bitmap rendering option flags (B_FILTER_BITMAP_BILINEAR, etc.).
 * @return Bounding rectangle of the area touched, clipped.
 */
BRect
Painter::DrawBitmap(const ServerBitmap* bitmap, BRect bitmapRect,
	BRect viewRect, uint32 options) const
{
	CHECK_CLIPPING

	BRect touched = TransformAlignAndClipRect(viewRect);

	if (touched.IsValid()) {
		BitmapPainter bitmapPainter(this, bitmap, options);
		bitmapPainter.Draw(bitmapRect, viewRect);
	}

	return touched;
}


// #pragma mark -


/**
 * @brief Fills every rectangle of @a region with the current pattern.
 *
 * @param region Region to fill (deep-copied so the original is not modified).
 * @return Union of the bounding rectangles touched, clipped.
 */
BRect
Painter::FillRegion(const BRegion* region) const
{
	CHECK_CLIPPING

	BRegion copy(*region);
	int32 count = copy.CountRects();
	BRect touched = FillRect(copy.RectAt(0));
	for (int32 i = 1; i < count; i++) {
		touched = touched | FillRect(copy.RectAt(i));
	}
	return touched;
}


/**
 * @brief Fills every rectangle of @a region with a gradient.
 *
 * @param region   Region to fill.
 * @param gradient Gradient to apply.
 * @return Union of the bounding rectangles touched, clipped.
 */
BRect
Painter::FillRegion(const BRegion* region, const BGradient& gradient)
{
	CHECK_CLIPPING

	BRegion copy(*region);
	int32 count = copy.CountRects();
	BRect touched = FillRect(copy.RectAt(0), gradient);
	for (int32 i = 1; i < count; i++) {
		touched = touched | FillRect(copy.RectAt(i), gradient);
	}
	return touched;
}


/**
 * @brief Bitwise-inverts every RGB channel inside @a r, intersected with the clip.
 *
 * @param r Rectangle to invert.
 * @return Bounding rectangle of the inverted area, clipped.
 * @note Currently implemented for B_RGB32 only.
 */
BRect
Painter::InvertRect(const BRect& r) const
{
	CHECK_CLIPPING

	BRegion region(r);
	region.IntersectWith(fClippingRegion);

	// implementation only for B_RGB32 at the moment
	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++)
		_InvertRect32(region.RectAt(i));

	return _Clipped(r);
}


/**
 * @brief Sets the screen-space offset of the AGG base renderer.
 *
 * Used by the DrawingEngine to translate the renderer's coordinates when
 * the buffer is mapped to a sub-area of the screen.
 *
 * @param offsetX Horizontal offset in pixels.
 * @param offsetY Vertical offset in pixels.
 */
void
Painter::SetRendererOffset(int32 offsetX, int32 offsetY)
{
	fBaseRenderer.set_offset(offsetX, offsetY);
}


// #pragma mark - private


/**
 * @brief Aligns a single coordinate to integer or half-pixel boundaries.
 *
 * @param coord        Coordinate to align.
 * @param round        Truncate to integer when true.
 * @param centerOffset Add 0.5 to land on a pixel center when true.
 * @return Aligned coordinate.
 */
inline float
Painter::_Align(float coord, bool round, bool centerOffset) const
{
	// rounding
	if (round)
		coord = (int32)coord;

	// This code is supposed to move coordinates to the center of pixels,
	// as AGG considers (0,0) to be the "upper left corner" of a pixel,
	// but BViews are less strict on those details
	if (centerOffset)
		coord += 0.5;

	return coord;
}


/**
 * @brief In-place point alignment that respects the subpixel-precise flag.
 *
 * @param point        Point to align; ignored if NULL.
 * @param centerOffset Add 0.5 to each coordinate when true.
 */
inline void
Painter::_Align(BPoint* point, bool centerOffset) const
{
	_Align(point, !fSubpixelPrecise, centerOffset);
}


/**
 * @brief In-place point alignment with explicit rounding control.
 *
 * @param point        Point to align.
 * @param round        Truncate coordinates when true.
 * @param centerOffset Add 0.5 when true.
 */
inline void
Painter::_Align(BPoint* point, bool round, bool centerOffset) const
{
	point->x = _Align(point->x, round, centerOffset);
	point->y = _Align(point->y, round, centerOffset);
}


/**
 * @brief Returns an aligned copy of @a point.
 *
 * @param point        Source point.
 * @param centerOffset Add 0.5 when true.
 * @return Aligned point.
 */
inline BPoint
Painter::_Align(const BPoint& point, bool centerOffset) const
{
	BPoint ret(point);
	_Align(&ret, centerOffset);
	return ret;
}


/**
 * @brief Intersects @a rect with the current clipping region's frame.
 *
 * @param rect Source rectangle.
 * @return Clipped rectangle, or the original if it is invalid.
 */
BRect
Painter::_Clipped(const BRect& rect) const
{
	if (rect.IsValid())
		return BRect(rect & fClippingRegion->Frame());

	return BRect(rect);
}


/**
 * @brief Refreshes the PixelFormat's drawing-mode dispatch table.
 *
 * Called whenever the drawing mode, alpha source/function, or pattern
 * changes; the AGG renderers rely on the PixelFormat to pick optimized
 * solid-pattern code paths.
 */
void
Painter::_UpdateDrawingMode()
{
	// The AGG renderers have their own color setting, however
	// almost all drawing mode classes ignore the color given
	// by the AGG renderer and use the colors from the PatternHandler
	// instead. If we have a B_SOLID_* pattern, we can actually use
	// the color in the renderer and special versions of drawing modes
	// that don't use PatternHandler and are more efficient. This
	// has been implemented for B_OP_COPY and a couple others (the
	// DrawingMode*Solid ones) as of now. The PixelFormat knows the
	// PatternHandler and makes its decision based on the pattern.
	// When a solid pattern is used, _SetRendererColor()
	// has to be called so that all internal colors in the renderes
	// are up to date for use by the solid drawing mode version.
	fPixelFormat.SetDrawingMode(fDrawingMode, fAlphaSrcMode, fAlphaFncMode);
}


/**
 * @brief Reseeds the AGG solid renderers with @a color converted to floats.
 *
 * @param color Source color (each channel is divided by 255 to produce AGG rgba).
 */
void
Painter::_SetRendererColor(const rgb_color& color) const
{
	fRenderer.color(agg::rgba(color.red / 255.0, color.green / 255.0,
		color.blue / 255.0, color.alpha / 255.0));
	fSubpixRenderer.color(agg::rgba(color.red / 255.0, color.green / 255.0,
		color.blue / 255.0, color.alpha / 255.0));
// TODO: bitmap fonts not yet correctly setup in AGGTextRenderer
//	fRendererBin.color(agg::rgba(color.red / 255.0, color.green / 255.0,
//		color.blue / 255.0, color.alpha / 255.0));
}


// #pragma mark -


/**
 * @brief Internal triangle stroke/fill helper.
 *
 * @param pt1  First vertex.
 * @param pt2  Second vertex.
 * @param pt3  Third vertex.
 * @param fill True to fill, false to stroke.
 * @return Bounding rectangle of the rendered triangle, clipped.
 */
inline BRect
Painter::_DrawTriangle(BPoint pt1, BPoint pt2, BPoint pt3, bool fill) const
{
	CHECK_CLIPPING

	_Align(&pt1);
	_Align(&pt2);
	_Align(&pt3);

	fPath.remove_all();

	fPath.move_to(pt1.x, pt1.y);
	fPath.line_to(pt2.x, pt2.y);
	fPath.line_to(pt3.x, pt3.y);

	fPath.close_polygon();

	if (fill)
		return _FillPath(fPath);

	return _StrokePath(fPath);
}


/**
 * @brief Internal gradient triangle stroke/fill helper.
 *
 * @param pt1      First vertex.
 * @param pt2      Second vertex.
 * @param pt3      Third vertex.
 * @param fill     True to fill, false to stroke.
 * @param gradient Gradient applied to the stroke or fill.
 * @return Bounding rectangle of the rendered triangle, clipped.
 */
inline BRect
Painter::_DrawTriangle(BPoint pt1, BPoint pt2, BPoint pt3, bool fill, const BGradient& gradient)
{
	CHECK_CLIPPING

	_Align(&pt1);
	_Align(&pt2);
	_Align(&pt3);

	fPath.remove_all();

	fPath.move_to(pt1.x, pt1.y);
	fPath.line_to(pt2.x, pt2.y);
	fPath.line_to(pt3.x, pt3.y);

	fPath.close_polygon();

	if (fill)
		return _FillPath(fPath, gradient);

	return _StrokePath(fPath, gradient);
}


/**
 * @brief Walks a BShape op/point stream and feeds segments into the AGG path.
 *
 * Supports OP_MOVETO, OP_LINETO, OP_BEZIERTO, the four arc op variants and
 * OP_CLOSE. Every coordinate is scaled by @a viewScale and translated by
 * @a viewToScreenOffset before being added to fPath.
 *
 * @param opCount            Number of opcodes.
 * @param opList             Opcode stream (top byte = op, low 24 bits = count).
 * @param ptCount            Number of points referenced by @a points.
 * @param points             Point stream consumed in order.
 * @param viewToScreenOffset Translation applied to every point.
 * @param viewScale          Scale applied to every point.
 * @todo Replace this with an AGG VertexSource adaptor over BShape data.
 */
void
Painter::_IterateShapeData(const int32& opCount, const uint32* opList,
	const int32& ptCount, const BPoint* points,
	const BPoint& viewToScreenOffset, float viewScale) const
{
	// TODO: if shapes are ever used more heavily in Haiku,
	// it would be nice to use BShape data directly (write
	// an AGG "VertexSource" adaptor)
	fPath.remove_all();
	for (int32 i = 0; i < opCount; i++) {
		uint32 op = opList[i] & 0xFF000000;
		if ((op & OP_MOVETO) != 0) {
			fPath.move_to(
				points->x * viewScale + viewToScreenOffset.x,
				points->y * viewScale + viewToScreenOffset.y);
			points++;
		}

		if ((op & OP_LINETO) != 0) {
			int32 count = opList[i] & 0x00FFFFFF;
			while (count--) {
				fPath.line_to(
					points->x * viewScale + viewToScreenOffset.x,
					points->y * viewScale + viewToScreenOffset.y);
				points++;
			}
		}

		if ((op & OP_BEZIERTO) != 0) {
			int32 count = opList[i] & 0x00FFFFFF;
			while (count) {
				fPath.curve4(
					points[0].x * viewScale + viewToScreenOffset.x,
					points[0].y * viewScale + viewToScreenOffset.y,
					points[1].x * viewScale + viewToScreenOffset.x,
					points[1].y * viewScale + viewToScreenOffset.y,
					points[2].x * viewScale + viewToScreenOffset.x,
					points[2].y * viewScale + viewToScreenOffset.y);
				points += 3;
				count -= 3;
			}
		}

		if ((op & OP_LARGE_ARC_TO_CW) != 0 || (op & OP_LARGE_ARC_TO_CCW) != 0
			|| (op & OP_SMALL_ARC_TO_CW) != 0
			|| (op & OP_SMALL_ARC_TO_CCW) != 0) {
			int32 count = opList[i] & 0x00FFFFFF;
			while (count > 0) {
				fPath.arc_to(
					points[0].x * viewScale,
					points[0].y * viewScale,
					points[1].x,
					op & (OP_LARGE_ARC_TO_CW | OP_LARGE_ARC_TO_CCW),
					op & (OP_SMALL_ARC_TO_CW | OP_LARGE_ARC_TO_CW),
					points[2].x * viewScale + viewToScreenOffset.x,
					points[2].y * viewScale + viewToScreenOffset.y);
				points += 3;
				count -= 3;
			}
		}

		if ((op & OP_CLOSE) != 0)
			fPath.close_polygon();
	}
}


/**
 * @brief 32-bit per-channel inversion (255 - x) inside @a r.
 *
 * @param r Rectangle to invert; assumed to be already clipped to the buffer.
 */
void
Painter::_InvertRect32(BRect r) const
{
	int32 width = r.IntegerWidth() + 1;
	for (int32 y = (int32)r.top; y <= (int32)r.bottom; y++) {
		uint8* dst = fBuffer.row_ptr(y);
		dst += (int32)r.left * 4;
		for (int32 i = 0; i < width; i++) {
			dst[0] = 255 - dst[0];
			dst[1] = 255 - dst[1];
			dst[2] = 255 - dst[2];
			dst += 4;
		}
	}
}


/**
 * @brief Alpha-overlay blend of @a c across @a r in 32-bit pixel format.
 *
 * Iterates the clipping region's rectangles and calls blend_line32 row by
 * row. Used by FillRect()'s B_OP_ALPHA fast path.
 *
 * @param r Rectangle to blend.
 * @param c Source color including alpha.
 */
void
Painter::_BlendRect32(const BRect& r, const rgb_color& c) const
{
	if (!fValidClipping)
		return;

	uint8* dst = fBuffer.row_ptr(0);
	uint32 bpr = fBuffer.stride();

	int32 left = (int32)r.left;
	int32 top = (int32)r.top;
	int32 right = (int32)r.right;
	int32 bottom = (int32)r.bottom;

	// fill rects, iterate over clipping boxes
	fBaseRenderer.first_clip_box();
	do {
		int32 x1 = max_c(fBaseRenderer.xmin(), left);
		int32 x2 = min_c(fBaseRenderer.xmax(), right);
		if (x1 <= x2) {
			int32 y1 = max_c(fBaseRenderer.ymin(), top);
			int32 y2 = min_c(fBaseRenderer.ymax(), bottom);

			uint8* offset = dst + x1 * 4 + y1 * bpr;
			for (; y1 <= y2; y1++) {
				blend_line32(offset, x2 - x1 + 1, c.red, c.green, c.blue,
					c.alpha);
				offset += bpr;
			}
		}
	} while (fBaseRenderer.next_clip_box());
}


// #pragma mark -


/**
 * @brief Computes the axis-aligned bounding box of an AGG vertex source.
 *
 * @tparam VertexSource AGG vertex source type.
 * @param  path         Path whose vertices are scanned.
 * @return Bounding rectangle containing every vertex.
 */
template<class VertexSource>
BRect
Painter::_BoundingBox(VertexSource& path) const
{
	double left = 0.0;
	double top = 0.0;
	double right = -1.0;
	double bottom = -1.0;
	uint32 pathID[1];
	pathID[0] = 0;
	agg::bounding_rect(path, pathID, 0, 1, &left, &top, &right, &bottom);
	return BRect(left, top, right, bottom);
}


/**
 * @brief Translates a BeOS cap_mode into the matching AGG line_cap_e.
 *
 * @param mode BeOS cap mode.
 * @return Equivalent AGG cap; defaults to butt_cap on unknown input.
 */
inline agg::line_cap_e
agg_line_cap_mode_for(cap_mode mode)
{
	switch (mode) {
		case B_BUTT_CAP:
			return agg::butt_cap;
		case B_SQUARE_CAP:
			return agg::square_cap;
		case B_ROUND_CAP:
			return agg::round_cap;
	}
	return agg::butt_cap;
}


/**
 * @brief Translates a BeOS join_mode into the matching AGG line_join_e.
 *
 * @param mode BeOS join mode.
 * @return Equivalent AGG join; defaults to miter_join on unknown input.
 */
inline agg::line_join_e
agg_line_join_mode_for(join_mode mode)
{
	switch (mode) {
		case B_MITER_JOIN:
			return agg::miter_join;
		case B_ROUND_JOIN:
			return agg::round_join;
		case B_BEVEL_JOIN:
		case B_BUTT_JOIN: // ??
		case B_SQUARE_JOIN: // ??
			return agg::bevel_join;
	}
	return agg::miter_join;
}


/**
 * @brief Stroke entry point; uses the configured cap mode.
 *
 * @tparam VertexSource AGG vertex source type.
 * @param  path         Path to stroke.
 * @return Bounding rectangle of the stroked geometry, clipped.
 */
template<class VertexSource>
BRect
Painter::_StrokePath(VertexSource& path) const
{
	return _StrokePath(path, fLineCapMode);
}


/**
 * @brief Strokes an AGG path with an explicit cap mode.
 *
 * Builds a conv_stroke pipeline with the active pen size, line join, and
 * miter limit. When the transform is non-identity, the stroke is run
 * through conv_transform so the pen width is computed in untransformed
 * space first and the result is transformed afterward.
 *
 * @tparam VertexSource AGG vertex source type.
 * @param  path         Path to stroke.
 * @param  capMode      Line cap mode to apply.
 * @return Bounding rectangle of the stroked geometry, clipped.
 */
template<class VertexSource>
BRect
Painter::_StrokePath(VertexSource& path, cap_mode capMode) const
{
	agg::conv_stroke<VertexSource> stroke(path);
	stroke.width(fPenSize);

	stroke.line_cap(agg_line_cap_mode_for(capMode));
	stroke.line_join(agg_line_join_mode_for(fLineJoinMode));
	stroke.miter_limit(fMiterLimit);

	if (fIdentityTransform)
		return _RasterizePath(stroke);

	stroke.approximation_scale(fTransform.scale());

	agg::conv_transform<agg::conv_stroke<VertexSource> > transformedStroke(
		stroke, fTransform);
	return _RasterizePath(transformedStroke);
}


/**
 * @brief Gradient stroke entry point; uses the configured cap mode.
 *
 * @tparam VertexSource AGG vertex source type.
 * @param  path         Path to stroke.
 * @param  gradient     Gradient applied to the stroke.
 * @return Bounding rectangle of the stroked geometry, clipped.
 */
template<class VertexSource>
BRect
Painter::_StrokePath(VertexSource& path, const BGradient& gradient)
{
	return _StrokePath(path, fLineCapMode, gradient);
}


/**
 * @brief Strokes an AGG path with a gradient and explicit cap mode.
 *
 * @tparam VertexSource AGG vertex source type.
 * @param  path         Path to stroke.
 * @param  capMode      Line cap mode to apply.
 * @param  gradient     Gradient applied to the stroke.
 * @return Bounding rectangle of the stroked geometry, clipped.
 */
template<class VertexSource>
BRect
Painter::_StrokePath(VertexSource& path, cap_mode capMode, const BGradient& gradient)
{
	agg::conv_stroke<VertexSource> stroke(path);
	stroke.width(fPenSize);

	stroke.line_cap(agg_line_cap_mode_for(capMode));
	stroke.line_join(agg_line_join_mode_for(fLineJoinMode));
	stroke.miter_limit(fMiterLimit);

	if (fIdentityTransform)
		return _RasterizePath(stroke, gradient);

	stroke.approximation_scale(fTransform.scale());

	agg::conv_transform<agg::conv_stroke<VertexSource> > transformedStroke(
		stroke, fTransform);
	return _RasterizePath(transformedStroke, gradient);
}


/**
 * @brief Fills an AGG path, transparently transforming it when needed.
 *
 * @tparam VertexSource AGG vertex source type.
 * @param  path         Path to fill.
 * @return Bounding rectangle of the filled area, clipped.
 */
template<class VertexSource>
BRect
Painter::_FillPath(VertexSource& path) const
{
	if (fIdentityTransform)
		return _RasterizePath(path);

	agg::conv_transform<VertexSource> transformedPath(path, fTransform);
	return _RasterizePath(transformedPath);
}


/**
 * @brief Rasterizes an AGG path through the appropriate solid pipeline.
 *
 * Picks one of three pipelines:
 *   1. Alpha-masked: AA rasterizer + masked scanline + solid renderer.
 *   2. Subpixel AA:  subpixel rasterizer + packed subpixel scanline.
 *   3. Plain AA:     AA rasterizer + packed scanline + solid renderer.
 *
 * @tparam VertexSource AGG vertex source type.
 * @param  path         Path to rasterize.
 * @return Bounding rectangle of the rasterized geometry, clipped.
 * @note Alpha masking and subpixel AA are mutually exclusive at the moment.
 */
template<class VertexSource>
BRect
Painter::_RasterizePath(VertexSource& path) const
{
	if (fMaskedUnpackedScanline != NULL) {
		// TODO: we can't do both alpha-masking and subpixel AA.
		fRasterizer.reset();
		fRasterizer.add_path(path);
		agg::render_scanlines(fRasterizer, *fMaskedUnpackedScanline,
			fRenderer);
	} else if (gSubpixelAntialiasing) {
		fSubpixRasterizer.reset();
		fSubpixRasterizer.add_path(path);
		agg::render_scanlines(fSubpixRasterizer,
			fSubpixPackedScanline, fSubpixRenderer);
	} else {
		fRasterizer.reset();
		fRasterizer.add_path(path);
		agg::render_scanlines(fRasterizer, fPackedScanline, fRenderer);
	}

	return _Clipped(_BoundingBox(path));
}


/**
 * @brief Fills an AGG path with a gradient, transforming it when needed.
 *
 * @tparam VertexSource AGG vertex source type.
 * @param  path         Path to fill.
 * @param  gradient     Gradient to apply.
 * @return Bounding rectangle of the filled area, clipped.
 */
template<class VertexSource>
BRect
Painter::_FillPath(VertexSource& path, const BGradient& gradient)
{
	if (fIdentityTransform)
		return _RasterizePath(path, gradient);

	agg::conv_transform<VertexSource> transformedPath(path, fTransform);
	return _RasterizePath(transformedPath, gradient);
}


/**
 * @brief Selects an AGG gradient function based on @a gradient's type.
 *
 * Computes the gradient transform with _CalcLinearGradientTransform() or
 * _CalcRadialGradientTransform() and dispatches to the templated
 * _RasterizePath() that runs the AGG span_gradient pipeline. Unknown or
 * TYPE_NONE gradients produce nothing.
 *
 * @tparam VertexSource AGG vertex source type.
 * @param  path         Path to rasterize.
 * @param  gradient     Gradient describing color stops and geometry.
 * @return Bounding rectangle of the rasterized geometry, clipped.
 */
template<class VertexSource>
BRect
Painter::_RasterizePath(VertexSource& path, const BGradient& gradient)
{
	GTRACE("Painter::_RasterizePath\n");

	agg::trans_affine gradientTransform;

	switch (gradient.GetType()) {
		case BGradient::TYPE_LINEAR:
		{
			GTRACE(("Painter::_FillPath> type == TYPE_LINEAR\n"));
			const BGradientLinear& linearGradient
				= (const BGradientLinear&) gradient;
			agg::gradient_x gradientFunction;
			_CalcLinearGradientTransform(linearGradient.Start(),
				linearGradient.End(), gradientTransform);
			_RasterizePath(path, gradient, gradientFunction, gradientTransform);
			break;
		}
		case BGradient::TYPE_RADIAL:
		{
			GTRACE(("Painter::_FillPathGradient> type == TYPE_RADIAL\n"));
			const BGradientRadial& radialGradient
				= (const BGradientRadial&) gradient;
			agg::gradient_radial gradientFunction;
			_CalcRadialGradientTransform(radialGradient.Center(),
				gradientTransform);
			_RasterizePath(path, gradient, gradientFunction, gradientTransform,
				radialGradient.Radius());
			break;
		}
		case BGradient::TYPE_RADIAL_FOCUS:
		{
			GTRACE(("Painter::_FillPathGradient> type == TYPE_RADIAL_FOCUS\n"));
			const BGradientRadialFocus& radialGradient
				= (const BGradientRadialFocus&) gradient;
			agg::gradient_radial_focus gradientFunction;
			_CalcRadialGradientTransform(radialGradient.Center(),
				gradientTransform);
			_RasterizePath(path, gradient, gradientFunction, gradientTransform,
				radialGradient.Radius());
			break;
		}
		case BGradient::TYPE_DIAMOND:
		{
			GTRACE(("Painter::_FillPathGradient> type == TYPE_DIAMOND\n"));
			const BGradientDiamond& diamontGradient
				= (const BGradientDiamond&) gradient;
			agg::gradient_diamond gradientFunction;
			_CalcRadialGradientTransform(diamontGradient.Center(),
				gradientTransform);
			_RasterizePath(path, gradient, gradientFunction, gradientTransform);
			break;
		}
		case BGradient::TYPE_CONIC:
		{
			GTRACE(("Painter::_FillPathGradient> type == TYPE_CONIC\n"));
			const BGradientConic& conicGradient
				= (const BGradientConic&) gradient;
			agg::gradient_conic gradientFunction;
			_CalcRadialGradientTransform(conicGradient.Center(),
				gradientTransform);
			_RasterizePath(path, gradient, gradientFunction, gradientTransform);
			break;
		}

		default:
		case BGradient::TYPE_NONE:
			GTRACE(("Painter::_FillPathGradient> type == TYPE_NONE/unkown\n"));
			break;
	}

	return _Clipped(_BoundingBox(path));
}


/**
 * @brief Builds the AGG transform for a linear gradient.
 *
 * Composes scale-by-(distance/d2), rotation, translation, the active view
 * transform, and finally inverts the matrix because AGG's span_gradient
 * walks the gradient by mapping screen pixels back into gradient space.
 *
 * @param startPoint   Gradient start in view space.
 * @param endPoint     Gradient end in view space.
 * @param matrix       Output AGG transform; reset and overwritten.
 * @param gradient_d2  Gradient virtual length (defaults to 100).
 */
void
Painter::_CalcLinearGradientTransform(BPoint startPoint, BPoint endPoint,
	agg::trans_affine& matrix, float gradient_d2) const
{
	float dx = endPoint.x - startPoint.x;
	float dy = endPoint.y - startPoint.y;

	matrix.reset();
	matrix *= agg::trans_affine_scaling(sqrt(dx * dx + dy * dy) / gradient_d2);
	matrix *= agg::trans_affine_rotation(atan2(dy, dx));
	matrix *= agg::trans_affine_translation(startPoint.x, startPoint.y);
	matrix *= fTransform;
	matrix.invert();
}


/**
 * @brief Builds the AGG transform for a radial/diamond/conic gradient.
 *
 * Translates @a center to the origin, applies the view transform, and
 * inverts the result for use by span_gradient.
 *
 * @param center       Gradient center in view space.
 * @param matrix       Output AGG transform; reset and overwritten.
 * @param gradient_d2  Gradient virtual radius hint (currently unused here).
 */
void
Painter::_CalcRadialGradientTransform(BPoint center,
	agg::trans_affine& matrix, float gradient_d2) const
{
	matrix.reset();
	matrix *= agg::trans_affine_translation(center.x, center.y);
	matrix *= fTransform;
	matrix.invert();
}


/**
 * @brief Materializes a gradient into a flat 32-bit color array.
 *
 * Walks the gradient's color stops and writes interpolated BGRA pixels
 * into @a colors. Used by FillRectVerticalGradient() to drive a fast,
 * blend-free row writer. Pixels outside the gradient extents are clamped
 * to the start/end colors so the array is always fully populated.
 *
 * @param gradient    Source gradient.
 * @param colorCount  Number of stops in the gradient direction.
 * @param colors      Output BGRA32 array.
 * @param arrayOffset Index in @a colors at which the first stop lands.
 * @param arraySize   Total size of @a colors in entries.
 */
void
Painter::_MakeGradient(const BGradient& gradient, int32 colorCount,
	uint32* colors, int32 arrayOffset, int32 arraySize) const
{
	BGradient::ColorStop* from = gradient.ColorStopAt(0);

	if (!from)
		return;

	// current index into "colors" array
//	int32 index = (int32)floorf(colorCount * from->offset + 0.5)
//		+ arrayOffset;
	int32 index = (int32)floorf(colorCount * from->offset / 255 + 0.5)
		+ arrayOffset;
	if (index > arraySize)
		index = arraySize;
	// Make sure we fill the entire array in case the gradient is outside.
	if (index > 0) {
		uint8* c = (uint8*)&colors[0];
		for (int32 i = 0; i < index; i++) {
			c[0] = from->color.blue;
			c[1] = from->color.green;
			c[2] = from->color.red;
			c[3] = from->color.alpha;
			c += 4;
		}
	}

	// interpolate "from" to "to"
	int32 stopCount = gradient.CountColorStops();
	for (int32 i = 1; i < stopCount; i++) {
		// find the step with the next offset
		BGradient::ColorStop* to = gradient.ColorStopAtFast(i);

		// interpolate
//		int32 offset = (int32)floorf((colorCount - 1) * to->offset + 0.5);
		int32 offset = (int32)floorf((colorCount - 1)
			* to->offset / 255 + 0.5);
		if (offset > colorCount - 1)
			offset = colorCount - 1;
		offset += arrayOffset;
		int32 dist = offset - index;
		if (dist >= 0) {
			int32 startIndex = max_c(index, 0);
			int32 stopIndex = min_c(offset, arraySize - 1);
			uint8* c = (uint8*)&colors[startIndex];
			for (int32 i = startIndex; i <= stopIndex; i++) {
				float f = (float)(offset - i) / (float)(dist + 1);
				float t = 1.0 - f;
				c[0] = (uint8)floorf(from->color.blue * f
					+ to->color.blue * t + 0.5);
				c[1] = (uint8)floorf(from->color.green * f
					+ to->color.green * t + 0.5);
				c[2] = (uint8)floorf(from->color.red * f
					+ to->color.red * t + 0.5);
				c[3] = (uint8)floorf(from->color.alpha * f
					+ to->color.alpha * t + 0.5);
				c += 4;
			}
		}
		index = offset + 1;
		// the current "to" will be the "from" in the next interpolation
		from = to;
	}
	//  make sure we fill the entire array
	if (index < arraySize) {
		int32 startIndex = max_c(index, 0);
		uint8* c = (uint8*)&colors[startIndex];
		for (int32 i = startIndex; i < arraySize; i++) {
			c[0] = from->color.blue;
			c[1] = from->color.green;
			c[2] = from->color.red;
			c[3] = from->color.alpha;
			c += 4;
		}
	}
}


/**
 * @brief Materializes a gradient into an AGG color array (rgba8).
 *
 * Variant of _MakeGradient() used by the AGG span_gradient pipeline; fills
 * @a array with rgba8 colors interpolated between successive stops.
 *
 * @tparam Array   Container with at least 256 rgba8 entries.
 * @param  array   Output array.
 * @param  gradient Source gradient.
 */
template<class Array>
void
Painter::_MakeGradient(Array& array, const BGradient& gradient) const
{
	for (int i = 0; i < gradient.CountColorStops() - 1; i++) {
		BGradient::ColorStop* from = gradient.ColorStopAtFast(i);
		BGradient::ColorStop* to = gradient.ColorStopAtFast(i + 1);
		agg::rgba8 fromColor(from->color.red, from->color.green,
							 from->color.blue, from->color.alpha);
		agg::rgba8 toColor(to->color.red, to->color.green,
						   to->color.blue, to->color.alpha);
		GTRACE("Painter::_MakeGradient> fromColor(%d, %d, %d, %d) offset = %f\n",
			   fromColor.r, fromColor.g, fromColor.b, fromColor.a,
			   from->offset);
		GTRACE("Painter::_MakeGradient> toColor(%d, %d, %d %d) offset = %f\n",
			   toColor.r, toColor.g, toColor.b, toColor.a, to->offset);
		float dist = to->offset - from->offset;
		GTRACE("Painter::_MakeGradient> dist = %f\n", dist);
		// TODO: Review this... offset should better be on [0..1]
		if (dist > 0) {
			for (int j = (int)from->offset; j <= (int)to->offset; j++) {
				float f = (float)(to->offset - j) / (float)(dist + 1);
				array[j] = toColor.gradient(fromColor, f);
				GTRACE("Painter::_MakeGradient> array[%d](%d, %d, %d, %d)\n",
					   j, array[j].r, array[j].g, array[j].b, array[j].a);
			}
		}
	}
}


/**
 * @brief Runs the AGG span_gradient pipeline for a single path.
 *
 * Builds linear span interpolation, span allocator, color array, and the
 * span_gradient renderer, then rasterizes @a path through either the
 * regular or alpha-masked scanline. Wraps everything in a SolidPatternGuard
 * so pattern-aware drawing modes still take their solid fast paths.
 *
 * @tparam VertexSource     AGG vertex source type.
 * @tparam GradientFunction AGG gradient function (gradient_x, gradient_radial, ...).
 * @param  path              Path to rasterize.
 * @param  gradient          Source BGradient (color stops).
 * @param  function          Per-type AGG gradient function.
 * @param  gradientTransform Pre-computed gradient transform.
 * @param  gradientStop      Gradient cap (defaults to 100; radius for radial).
 */
template<class VertexSource, typename GradientFunction>
void
Painter::_RasterizePath(VertexSource& path, const BGradient& gradient,
	GradientFunction function, agg::trans_affine& gradientTransform,
	int gradientStop)
{
	GTRACE("Painter::_RasterizePath\n");

	typedef agg::span_interpolator_linear<> interpolator_type;
	typedef agg::pod_auto_array<agg::rgba8, 256> color_array_type;
	typedef agg::span_allocator<agg::rgba8> span_allocator_type;
	typedef agg::span_gradient<agg::rgba8, interpolator_type,
				GradientFunction, color_array_type> span_gradient_type;
	typedef agg::renderer_scanline_aa<renderer_base, span_allocator_type,
				span_gradient_type> renderer_gradient_type;

	SolidPatternGuard _(this);

	interpolator_type spanInterpolator(gradientTransform);
	span_allocator_type spanAllocator;
	color_array_type colorArray;

	_MakeGradient(colorArray, gradient);

	span_gradient_type spanGradient(spanInterpolator, function, colorArray,
		0, gradientStop);

	renderer_gradient_type gradientRenderer(fBaseRenderer, spanAllocator,
		spanGradient);

	fRasterizer.reset();
	fRasterizer.add_path(path);
	if (fMaskedUnpackedScanline == NULL)
		agg::render_scanlines(fRasterizer, fUnpackedScanline, gradientRenderer);
	else {
		agg::render_scanlines(fRasterizer, *fMaskedUnpackedScanline,
			gradientRenderer);
	}
}
