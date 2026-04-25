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
 * MIT License. Copyright 2009, Christian Packmann; 2008, Andrej Spielmann;
 * 2005-2014, Stephan Aßmus; 2015, Julian Harnath.
 */

/** @file DrawBitmapNoScale.h
    @brief 1:1 unscaled bitmap-blit fast paths for CMAP8 and BGRA32
           sources under B_OP_COPY, B_OP_OVER, alpha overlay and
           alpha-mask copy. */

#ifndef DRAW_BITMAP_NO_SCALE_H
#define DRAW_BITMAP_NO_SCALE_H

#include "IntPoint.h"
#include "IntRect.h"
#include "Painter.h"
#include "SystemPalette.h"


/**
 * @brief CRTP base for 1:1 bitmap blitters; iterates AGG clip boxes and
 *        forwards each clipped scanline to BlendType::BlendRow().
 *
 * Concrete BlendTypes (CMap8Copy, CMap8Over, Bgr32Copy, Bgr32Over,
 * Bgr32Alpha, Bgr32CopyMasked) implement BlendRow() with the per-pixel
 * format and drawing-mode logic.
 */
template<class BlendType>
struct DrawBitmapNoScale {
public:
	/** @brief Walks every clip box and dispatches each clipped scanline
	           band to the derived BlendType's BlendRow() implementation. */
	void
	Draw(PainterAggInterface& aggInterface, agg::rendering_buffer& bitmap,
		uint32 bytesPerSourcePixel, IntPoint offset, BRect destinationRect)
	{
		// NOTE: this would crash if destinationRect was large enough to read
		// outside the bitmap, so make sure this is not the case before calling
		// this function!
		uint8* dst = aggInterface.fBuffer.row_ptr(0);
		const uint32 dstBPR = aggInterface.fBuffer.stride();

		const uint8* src = bitmap.row_ptr(0);
		const uint32 srcBPR = bitmap.stride();

		const int32 left = (int32)destinationRect.left;
		const int32 top = (int32)destinationRect.top;
		const int32 right = (int32)destinationRect.right;
		const int32 bottom = (int32)destinationRect.bottom;

#if DEBUG_DRAW_BITMAP
	if (left - offset.x < 0
		|| left  - offset.x >= (int32)bitmap.width()
		|| right - offset.x >= (int32)srcBuffer.width()
		|| top - offset.y < 0
		|| top - offset.y >= (int32)bitmap.height()
		|| bottom - offset.y >= (int32)bitmap.height()) {
		char message[256];
		sprintf(message, "reading outside of bitmap (%ld, %ld, %ld, %ld) "
				"(%d, %d) (%ld, %ld)",
			left - offset.x, top - offset.y,
			right - offset.x, bottom - offset.y,
			bitmap.width(), bitmap.height(), offset.x, offset.y);
		debugger(message);
	}
#endif

		fColorMap = SystemPalette();
		fAlphaMask = aggInterface.fClippedAlphaMask;
		renderer_base& baseRenderer = aggInterface.fBaseRenderer;

		// copy rects, iterate over clipping boxes
		baseRenderer.first_clip_box();
		do {
			fRect.left  = max_c(baseRenderer.xmin(), left);
			fRect.right = min_c(baseRenderer.xmax(), right);
			if (fRect.left <= fRect.right) {
				fRect.top    = max_c(baseRenderer.ymin(), top);
				fRect.bottom = min_c(baseRenderer.ymax(), bottom);
				if (fRect.top <= fRect.bottom) {
					uint8* dstHandle = dst + fRect.top * dstBPR
						+ fRect.left * 4;
					const uint8* srcHandle = src
						+ (fRect.top  - offset.y) * srcBPR
						+ (fRect.left - offset.x) * bytesPerSourcePixel;

					for (; fRect.top <= fRect.bottom; fRect.top++) {
						static_cast<BlendType*>(this)->BlendRow(dstHandle,
							srcHandle, fRect.right - fRect.left + 1);

						dstHandle += dstBPR;
						srcHandle += srcBPR;
					}
				}
			}
		} while (baseRenderer.next_clip_box());
	}

protected:
	IntRect fRect;
	const rgb_color* fColorMap;
	const agg::clipped_alpha_mask* fAlphaMask;
};


/** @brief BlendType: CMAP8 -> BGRA32 unconditional copy through the
           system palette. */
struct CMap8Copy : public DrawBitmapNoScale<CMap8Copy>
{
	/** @brief Translates a row of CMAP8 indices into BGRA32 destination
	           pixels via @c fColorMap. */
	void BlendRow(uint8* dst, const uint8* src, int32 numPixels)
	{
		uint32* d = (uint32*)dst;
		const uint8* s = src;
		while (numPixels--) {
			const rgb_color c = fColorMap[*s++];
			*d++ = (c.alpha << 24) | (c.red << 16) | (c.green << 8) | (c.blue);
		}
	}
};


/** @brief BlendType: CMAP8 -> BGRA32 with B_OP_OVER semantics; pixels
           with palette alpha 0 leave the destination untouched. */
struct CMap8Over : public DrawBitmapNoScale<CMap8Over>
{
	/** @brief Translates a row of CMAP8 indices into BGRA32 pixels,
	           skipping fully transparent palette entries. */
	void BlendRow(uint8* dst, const uint8* src, int32 numPixels)
	{
		uint32* d = (uint32*)dst;
		const uint8* s = src;
		while (numPixels--) {
			const rgb_color c = fColorMap[*s++];
			if (c.alpha)
				*d = (c.alpha << 24) | (c.red << 16)
					| (c.green << 8) | (c.blue);
			d++;
		}
	}
};


/** @brief BlendType: BGRA32 -> BGRA32 verbatim copy via memcpy. */
struct Bgr32Copy : public DrawBitmapNoScale<Bgr32Copy>
{
	/** @brief Copies @a numPixels source words straight into @a dst. */
	void BlendRow(uint8* dst, const uint8* src, int32 numPixels)
	{
		memcpy(dst, src, numPixels * 4);
	}
};


/** @brief BlendType: BGRA32 -> BGRA32 copy that skips
           B_TRANSPARENT_MAGIC_RGBA32 source pixels. */
struct Bgr32Over : public DrawBitmapNoScale<Bgr32Over>
{
	/** @brief Copies non-transparent-magic source words; transparent
	           magic pixels leave the destination unchanged. */
	void BlendRow(uint8* dst, const uint8* src, int32 numPixels)
	{
		uint32* d = (uint32*)dst;
		uint32* s = (uint32*)src;
		while (numPixels--) {
			if (*s != B_TRANSPARENT_MAGIC_RGBA32)
				*(uint32*)d = *(uint32*)s;
			d++;
			s++;
		}
	}
};


/** @brief BlendType: BGRA32 -> BGRA32 source-over compositing using each
           source pixel's alpha; equivalent to B_OP_ALPHA + B_PIXEL_ALPHA
           + B_ALPHA_OVERLAY at 1:1 scale. */
struct Bgr32Alpha : public DrawBitmapNoScale<Bgr32Alpha>
{
	/** @brief Composites a row of BGRA32 source pixels onto @a dst using
	           per-pixel source alpha; opaque source pixels are copied
	           directly. */
	void BlendRow(uint8* dst, const uint8* src, int32 numPixels)
	{
		uint32* d = (uint32*)dst;
		int32 bytes = numPixels * 4;
		uint8 buffer[bytes];
		uint8* b = buffer;
		while (numPixels--) {
			if (src[3] == 255) {
				*(uint32*)b = *(uint32*)src;
			} else {
				*(uint32*)b = *d;
				b[0] = ((src[0] - b[0]) * src[3] + (b[0] << 8)) >> 8;
				b[1] = ((src[1] - b[1]) * src[3] + (b[1] << 8)) >> 8;
				b[2] = ((src[2] - b[2]) * src[3] + (b[2] << 8)) >> 8;
			}
			d++;
			b += 4;
			src += 4;
		}
		memcpy(dst, buffer, bytes);
	}
};


/** @brief BlendType: BGRA32 -> BGRA32 copy gated by a non-zero entry in
           the bound AGG alpha mask scanline. */
struct Bgr32CopyMasked : public DrawBitmapNoScale<Bgr32CopyMasked>
{
	/** @brief Reads the alpha-mask hspan for the current row and copies
	           source pixels only where the mask is non-zero. */
	void BlendRow(uint8* dst, const uint8* src, int32 numPixels)
	{
		uint8 covers[numPixels];
		fAlphaMask->get_hspan(fRect.left, fRect.top, covers, numPixels);

		uint32* destination = (uint32*)dst;
		uint32* source = (uint32*)src;
		uint8* mask = (uint8*)&covers[0];

		while (numPixels--) {
			if (*mask != 0)
				*destination = *source;
			destination++;
			source++;
			mask++;
		}
	}
};


#endif // DRAW_BITMAP_NO_SCALE_H
