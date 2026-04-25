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
 * @file BitmapPainter.cpp
 * @brief Implementation of Painter::BitmapPainter, the top-level dispatcher
 *        that picks the most specialised bitmap-blit code path for a given
 *        source/destination rectangle, drawing mode, color space, alpha
 *        mask and B_FILTER_BITMAP_BILINEAR / B_TILE_BITMAP options.
 *
 * BitmapPainter routes to one of the BitmapPainterPrivate specialisations:
 * DrawBitmapNoScale (CMap8/Bgr32 with no scale and no transform),
 * DrawBitmapBilinear (bilinear filtering with optional SIMD),
 * DrawBitmapNearestNeighborCopy (nearest-neighbor scaled B_OP_COPY) and
 * DrawBitmapGeneric (the generic AGG-rasterised fallback that also
 * handles affine transforms, alpha masks, and tiled fills). Color-space
 * conversion to B_RGBA32 happens up front when needed, with
 * B_TRANSPARENT_MAGIC_* values translated to alpha = 0.
 *
 * @see DrawBitmapBilinear, DrawBitmapNoScale, DrawBitmapNearestNeighbor,
 *      DrawBitmapGeneric, Painter
 */

#include "BitmapPainter.h"

#include <Bitmap.h>

#include <agg_image_accessors.h>
#include <agg_pixfmt_rgba.h>
#include <agg_span_image_filter_rgba.h>

#include "DrawBitmapBilinear.h"
#include "DrawBitmapGeneric.h"
#include "DrawBitmapNearestNeighbor.h"
#include "DrawBitmapNoScale.h"
#include "drawing_support.h"
#include "ServerBitmap.h"
#include "SystemPalette.h"


// #define TRACE_BITMAP_PAINTER
#ifdef TRACE_BITMAP_PAINTER
#	define TRACE(x...)		printf(x)
#else
#	define TRACE(x...)
#endif


/**
 * @brief Constructs a BitmapPainter bound to a source @a bitmap and a
 *        Painter instance.
 *
 * Captures the bitmap's bounds, color space, options and pixel buffer.
 * Sets fStatus to B_OK only when @a bitmap is non-NULL and IsValid();
 * Draw() returns immediately when the status is not B_OK.
 *
 * @param painter  Owning Painter providing transform, clipping, drawing
 *                 mode and alpha mode state.
 * @param bitmap   Source bitmap to blit. May be NULL; status will reflect
 *                 the failure.
 * @param options  Bitmap drawing options bitfield (B_FILTER_BITMAP_BILINEAR,
 *                 B_TILE_BITMAP).
 */
Painter::BitmapPainter::BitmapPainter(const Painter* painter,
	const ServerBitmap* bitmap, uint32 options)
	:
	fPainter(painter),
	fStatus(B_NO_INIT),
	fOptions(options)
{
	if (bitmap == NULL || !bitmap->IsValid())
		return;

	fBitmapBounds = bitmap->Bounds();
	fBitmapBounds.OffsetBy(-fBitmapBounds.left, -fBitmapBounds.top);
		// Compensate for the lefttop offset the bitmap bounds might have
		// It has the right size, but put it at B_ORIGIN

	fColorSpace = bitmap->ColorSpace();

	fBitmap.attach(bitmap->Bits(), bitmap->Width(), bitmap->Height(),
		bitmap->BytesPerRow());

	fStatus = B_OK;
}


/**
 * @brief Renders the configured source bitmap into @a destinationRect.
 *
 * Computes scale and offset from @a sourceRect and @a destinationRect via
 * _DetermineTransform(), then walks a chain of fast-path checks ordered
 * from cheapest to most general. Optimised paths cover (1) CMAP8 and
 * RGB32 no-scale/no-transform/no-mask copies and overs without color
 * space conversion, (2) Bgr32 no-scale/no-transform copies, alpha
 * overlays and masked copies after conversion, (3) bilinear or
 * nearest-neighbor scaled B_OP_COPY, and (4) bilinear B_OP_ALPHA pixel
 * overlay. Anything else (including B_TILE_BITMAP and affine transforms)
 * falls through to DrawBitmapGeneric with Fill or Tile semantics.
 *
 * @param sourceRect       Region of the source bitmap to draw from.
 * @param destinationRect  Destination rectangle in painter coordinates.
 */
void
Painter::BitmapPainter::Draw(const BRect& sourceRect,
	const BRect& destinationRect)
{
	using namespace BitmapPainterPrivate;

	if (fStatus != B_OK)
		return;

	TRACE("BitmapPainter::Draw()\n");
	TRACE("   bitmapBounds = (%.1f, %.1f) - (%.1f, %.1f)\n",
		fBitmapBounds.left, fBitmapBounds.top,
		fBitmapBounds.right, fBitmapBounds.bottom);
	TRACE("   sourceRect = (%.1f, %.1f) - (%.1f, %.1f)\n",
		sourceRect.left, sourceRect.top,
		sourceRect.right, sourceRect.bottom);
	TRACE("   destinationRect = (%.1f, %.1f) - (%.1f, %.1f)\n",
		destinationRect.left, destinationRect.top,
		destinationRect.right, destinationRect.bottom);

	bool success = _DetermineTransform(sourceRect, destinationRect);
	if (!success)
		return;

	if ((fOptions & B_TILE_BITMAP) == 0) {
		// optimized version for no scale in CMAP8 or RGB32 OP_OVER
		if (!_HasScale() && !_HasAffineTransform() && !_HasAlphaMask()) {
			if (fColorSpace == B_CMAP8) {
				if (fPainter->fDrawingMode == B_OP_COPY) {
					DrawBitmapNoScale<CMap8Copy> drawNoScale;
					drawNoScale.Draw(fPainter->fInternal, fBitmap, 1, fOffset,
						fDestinationRect);
					return;
				}
				if (fPainter->fDrawingMode == B_OP_OVER) {
					DrawBitmapNoScale<CMap8Over> drawNoScale;
					drawNoScale.Draw(fPainter->fInternal, fBitmap, 1, fOffset,
						fDestinationRect);
					return;
				}
			} else if (fColorSpace == B_RGB32) {
				if (fPainter->fDrawingMode == B_OP_OVER) {
					DrawBitmapNoScale<Bgr32Over> drawNoScale;
					drawNoScale.Draw(fPainter->fInternal, fBitmap, 4, fOffset,
						fDestinationRect);
					return;
				}
			}
		}
	}

	ObjectDeleter<BBitmap> convertedBitmapDeleter;
	_ConvertColorSpace(convertedBitmapDeleter);

	if ((fOptions & B_TILE_BITMAP) == 0) {
		// optimized version if there is no scale
		if (!_HasScale() && !_HasAffineTransform() && !_HasAlphaMask()) {
			if (fPainter->fDrawingMode == B_OP_COPY) {
				DrawBitmapNoScale<Bgr32Copy> drawNoScale;
				drawNoScale.Draw(fPainter->fInternal, fBitmap, 4, fOffset,
					fDestinationRect);
				return;
			}
			if (fPainter->fDrawingMode == B_OP_OVER
				|| (fPainter->fDrawingMode == B_OP_ALPHA
					 && fPainter->fAlphaSrcMode == B_PIXEL_ALPHA
					 && fPainter->fAlphaFncMode == B_ALPHA_OVERLAY)) {
				DrawBitmapNoScale<Bgr32Alpha> drawNoScale;
				drawNoScale.Draw(fPainter->fInternal, fBitmap, 4, fOffset,
					fDestinationRect);
				return;
			}
		}

		if (!_HasScale() && !_HasAffineTransform() && _HasAlphaMask()) {
			if (fPainter->fDrawingMode == B_OP_COPY) {
				DrawBitmapNoScale<Bgr32CopyMasked> drawNoScale;
				drawNoScale.Draw(fPainter->fInternal, fBitmap, 4, fOffset,
					fDestinationRect);
				return;
			}
		}

		// bilinear and nearest-neighbor scaled, OP_COPY only
		if (fPainter->fDrawingMode == B_OP_COPY
			&& !_HasAffineTransform() && !_HasAlphaMask()) {
			if ((fOptions & B_FILTER_BITMAP_BILINEAR) != 0) {
				DrawBitmapBilinear<ColorTypeRgb, DrawModeCopy> drawBilinear;
				drawBilinear.Draw(fPainter, fPainter->fInternal,
					fBitmap, fOffset, fScaleX, fScaleY, fDestinationRect);
			} else {
				DrawBitmapNearestNeighborCopy::Draw(fPainter, fPainter->fInternal,
					fBitmap, fOffset, fScaleX, fScaleY, fDestinationRect);
			}
			return;
		}

		if (fPainter->fDrawingMode == B_OP_ALPHA
			&& fPainter->fAlphaSrcMode == B_PIXEL_ALPHA
			&& fPainter->fAlphaFncMode == B_ALPHA_OVERLAY
			&& !_HasAffineTransform() && !_HasAlphaMask()
			&& (fOptions & B_FILTER_BITMAP_BILINEAR) != 0) {
			DrawBitmapBilinear<ColorTypeRgba, DrawModeAlphaOverlay> drawBilinear;
			drawBilinear.Draw(fPainter, fPainter->fInternal,
				fBitmap, fOffset, fScaleX, fScaleY, fDestinationRect);
			return;
		}
	}

	if ((fOptions & B_TILE_BITMAP) != 0) {
		DrawBitmapGeneric<Tile>::Draw(fPainter, fPainter->fInternal, fBitmap,
			fOffset, fScaleX, fScaleY, fDestinationRect, fOptions);
	} else {
		// for all other cases (non-optimized drawing mode or scaled drawing)
		DrawBitmapGeneric<Fill>::Draw(fPainter, fPainter->fInternal, fBitmap,
			fOffset, fScaleX, fScaleY, fDestinationRect, fOptions);
	}
}


/**
 * @brief Computes the per-axis scale factors and the source-to-destination
 *        offset, and clips the source rectangle to the bitmap bounds.
 *
 * In non-tiling mode, computes fScaleX and fScaleY from the source and
 * destination widths/heights, then trims @a sourceRect to fBitmapBounds
 * propagating the trim into fDestinationRect at the same scale. In
 * B_TILE_BITMAP mode, scale is forced to 1:1. Aligns rectangles to whole
 * pixels when the painter is not in subpixel-precise mode.
 *
 * @param sourceRect       Requested source rectangle (passed by value
 *                         because it is mutated locally).
 * @param destinationRect  Requested destination rectangle.
 * @return  @c true on success; @c false when clipping is invalid, the
 *          source intersects nothing, or the computed scale would be 0.
 */
bool
Painter::BitmapPainter::_DetermineTransform(BRect sourceRect,
	const BRect& destinationRect)
{
	if (!fPainter->fValidClipping
		|| !sourceRect.IsValid()
		|| ((fOptions & B_TILE_BITMAP) == 0
			&& !sourceRect.Intersects(fBitmapBounds))
		|| !destinationRect.IsValid()) {
		return false;
	}

	fDestinationRect = destinationRect;

	if (!fPainter->fSubpixelPrecise) {
		align_rect_to_pixels(&sourceRect);
		align_rect_to_pixels(&fDestinationRect);
	}

	if((fOptions & B_TILE_BITMAP) == 0) {
		fScaleX = (fDestinationRect.Width() + 1) / (sourceRect.Width() + 1);
		fScaleY = (fDestinationRect.Height() + 1) / (sourceRect.Height() + 1);

		if (fScaleX == 0.0 || fScaleY == 0.0)
			return false;

		// constrain source rect to bitmap bounds and transfer the changes to
		// the destination rect with the right scale
		if (sourceRect.left < fBitmapBounds.left) {
			float diff = fBitmapBounds.left - sourceRect.left;
			fDestinationRect.left += diff * fScaleX;
			sourceRect.left = fBitmapBounds.left;
		}
		if (sourceRect.top < fBitmapBounds.top) {
			float diff = fBitmapBounds.top - sourceRect.top;
			fDestinationRect.top += diff * fScaleY;
			sourceRect.top = fBitmapBounds.top;
		}
		if (sourceRect.right > fBitmapBounds.right) {
			float diff = sourceRect.right - fBitmapBounds.right;
			fDestinationRect.right -= diff * fScaleX;
			sourceRect.right = fBitmapBounds.right;
		}
		if (sourceRect.bottom > fBitmapBounds.bottom) {
			float diff = sourceRect.bottom - fBitmapBounds.bottom;
			fDestinationRect.bottom -= diff * fScaleY;
			sourceRect.bottom = fBitmapBounds.bottom;
		}
	} else {
		fScaleX = 1.0;
		fScaleY = 1.0;
	}

	fOffset.x = fDestinationRect.left - sourceRect.left;
	fOffset.y = fDestinationRect.top - sourceRect.top;

	return true;
}


/** @brief Returns @c true when the destination scale is not 1:1 on either
           axis, indicating that resampling is required. */
bool
Painter::BitmapPainter::_HasScale()
{
	return fScaleX != 1.0 || fScaleY != 1.0;
}


/** @brief Returns @c true when the painter has a non-identity affine
           transform, ruling out the no-transform fast paths. */
bool
Painter::BitmapPainter::_HasAffineTransform()
{
	return !fPainter->fIdentityTransform;
}


/** @brief Returns @c true when an AGG alpha mask scanline is bound on
           the painter, so masked code paths must be used. */
bool
Painter::BitmapPainter::_HasAlphaMask()
{
	return fPainter->fInternal.fMaskedUnpackedScanline != NULL;
}


/**
 * @brief Converts the source bitmap to B_RGBA32 in-place when needed and
 *        translates legacy transparent-magic colors to alpha = 0.
 *
 * Skips conversion entirely for B_RGBA32 sources, and for B_RGB32 sources
 * when the painter is in B_OP_COPY or B_OP_ALPHA (matching BeOS
 * behaviour). Otherwise allocates a temporary BBitmap, owned via @a
 * convertedBitmapDeleter, ImportBits-converts the source into it, then
 * rewrites B_TRANSPARENT_MAGIC_RGBA32 / B_TRANSPARENT_MAGIC_RGBA15 pixels
 * to alpha-zero. Finally re-points fBitmap at the converted buffer.
 *
 * @param convertedBitmapDeleter  Sink that takes ownership of the
 *                                temporary RGBA32 bitmap; must outlive
 *                                the subsequent draw call.
 */
void
Painter::BitmapPainter::_ConvertColorSpace(
	ObjectDeleter<BBitmap>& convertedBitmapDeleter)
{
	if (fColorSpace == B_RGBA32)
		return;

	if (fColorSpace == B_RGB32
		&& (fPainter->fDrawingMode == B_OP_COPY
#if 1
// Enabling this would make the behavior compatible to BeOS, which
// treats B_RGB32 bitmaps as B_RGB*A*32 bitmaps in B_OP_ALPHA - unlike in
// all other drawing modes, where B_TRANSPARENT_MAGIC_RGBA32 is handled.
// B_RGB32 bitmaps therefore don't draw correctly on BeOS if they actually
// use this color, unless the alpha channel contains 255 for all other
// pixels, which is inconsistent.
		|| fPainter->fDrawingMode == B_OP_ALPHA
#endif
		)) {
		return;
	}

	BBitmap* conversionBitmap = new(std::nothrow) BBitmap(fBitmapBounds,
		B_BITMAP_NO_SERVER_LINK, B_RGBA32);
	if (conversionBitmap == NULL) {
		fprintf(stderr, "BitmapPainter::_ConvertColorSpace() - "
			"out of memory for creating temporary conversion bitmap\n");
		return;
	}
	convertedBitmapDeleter.SetTo(conversionBitmap);

	status_t err = conversionBitmap->ImportBits(fBitmap.buf(),
		fBitmap.height() * fBitmap.stride(),
		fBitmap.stride(), 0, fColorSpace);
	if (err < B_OK) {
		fprintf(stderr, "BitmapPainter::_ConvertColorSpace() - "
			"colorspace conversion failed: %s\n", strerror(err));
		return;
	}

	// the original bitmap might have had some of the
	// transaparent magic colors set that we now need to
	// make transparent in our RGBA32 bitmap again.
	switch (fColorSpace) {
		case B_RGB32:
			_TransparentMagicToAlpha((uint32 *)fBitmap.buf(),
				fBitmap.width(), fBitmap.height(),
				fBitmap.stride(), B_TRANSPARENT_MAGIC_RGBA32,
				conversionBitmap);
			break;

		// TODO: not sure if this applies to B_RGBA15 too. It
		// should not because B_RGBA15 actually has an alpha
		// channel itself and it should have been preserved
		// when importing the bitmap. Maybe it applies to
		// B_RGB16 though?
		case B_RGB15:
			_TransparentMagicToAlpha((uint16 *)fBitmap.buf(),
				fBitmap.width(), fBitmap.height(),
				fBitmap.stride(), B_TRANSPARENT_MAGIC_RGBA15,
				conversionBitmap);
			break;

		default:
			break;
	}

	fBitmap.attach((uint8*)conversionBitmap->Bits(),
		(uint32)fBitmapBounds.IntegerWidth() + 1,
		(uint32)fBitmapBounds.IntegerHeight() + 1,
		conversionBitmap->BytesPerRow());
}


/**
 * @brief Rewrites pixels equal to @a transparentMagic in @a buffer to have
 *        alpha = 0 in @a output, leaving other pixels unchanged.
 *
 * Used after a color-space ImportBits-conversion that does not preserve
 * BeOS-style transparent-magic markers; @a sourcePixel is the source
 * bitmap's native pixel type (uint16 for B_RGB15, uint32 for B_RGB32).
 *
 * @param buffer              Source pixels in the original color space.
 * @param width               Image width in pixels.
 * @param height              Image height in pixels.
 * @param sourceBytesPerRow   Stride of @a buffer in bytes.
 * @param transparentMagic    Magic pixel value to treat as transparent.
 * @param output              RGBA32 destination bitmap; pixels matching
 *                            @a transparentMagic get their alpha cleared.
 */
template<typename sourcePixel>
void
Painter::BitmapPainter::_TransparentMagicToAlpha(sourcePixel* buffer,
	uint32 width, uint32 height, uint32 sourceBytesPerRow,
	sourcePixel transparentMagic, BBitmap* output)
{
	uint8* sourceRow = (uint8*)buffer;
	uint8* destRow = (uint8*)output->Bits();
	uint32 destBytesPerRow = output->BytesPerRow();

	for (uint32 y = 0; y < height; y++) {
		sourcePixel* pixel = (sourcePixel*)sourceRow;
		uint32* destPixel = (uint32*)destRow;
		for (uint32 x = 0; x < width; x++, pixel++, destPixel++) {
			if (*pixel == transparentMagic)
				*destPixel &= 0x00ffffff;
		}

		sourceRow += sourceBytesPerRow;
		destRow += destBytesPerRow;
	}
}
