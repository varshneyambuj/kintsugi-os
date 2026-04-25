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

/** @file DrawBitmapNearestNeighbor.h
    @brief Nearest-neighbor scaled bitmap-blit specialisation for
           B_OP_COPY without filtering, transform, or alpha mask. */

#ifndef DRAW_BITMAP_NEAREST_NEIGHBOR_H
#define DRAW_BITMAP_NEAREST_NEIGHBOR_H

#include "Painter.h"


/** @brief Nearest-neighbor scaled bitmap blitter for B_OP_COPY. */
struct DrawBitmapNearestNeighborCopy {
	/** @brief Builds per-axis nearest-pixel index tables for the target
	           rect, walks every AGG clip box, and copies one source
	           uint32 per destination pixel without filtering. */
	static void
	Draw(const Painter* painter, PainterAggInterface& aggInterface,
		agg::rendering_buffer& bitmap, BPoint offset,
		double scaleX, double scaleY, BRect destinationRect)
	{
		//bigtime_t now = system_time();
		uint32 dstWidth = destinationRect.IntegerWidth() + 1;
		uint32 dstHeight = destinationRect.IntegerHeight() + 1;
		uint32 srcWidth = bitmap.width();
		uint32 srcHeight = bitmap.height();

		// Do not calculate more filter weights than necessary and also
		// keep the stack based allocations reasonably sized
		const BRegion& clippingRegion = *painter->ClippingRegion();
		if (clippingRegion.Frame().IntegerWidth() + 1 < (int32)dstWidth)
			dstWidth = clippingRegion.Frame().IntegerWidth() + 1;
		if (clippingRegion.Frame().IntegerHeight() + 1 < (int32)dstHeight)
			dstHeight = clippingRegion.Frame().IntegerHeight() + 1;

		// When calculating less filter weights than specified by
		// destinationRect, we need to compensate the offset.
		uint32 filterWeightXIndexOffset = 0;
		uint32 filterWeightYIndexOffset = 0;
		if (clippingRegion.Frame().left > destinationRect.left) {
			filterWeightXIndexOffset = (int32)(clippingRegion.Frame().left
				- destinationRect.left);
		}
		if (clippingRegion.Frame().top > destinationRect.top) {
			filterWeightYIndexOffset = (int32)(clippingRegion.Frame().top
				- destinationRect.top);
		}

		// should not pose a problem with stack overflows
		// (needs around 6Kb for 1920x1200)
		uint16 xIndices[dstWidth];
		uint16 yIndices[dstHeight];

		// Extract the cropping information for the source bitmap,
		// If only a part of the source bitmap is to be drawn with scale,
		// the offset will be different from the destinationRect left top
		// corner.
		const int32 xBitmapShift = (int32)(destinationRect.left - offset.x);
		const int32 yBitmapShift = (int32)(destinationRect.top - offset.y);

		for (uint32 i = 0; i < dstWidth; i++) {
			// index into source
			uint16 index = (uint16)((i + filterWeightXIndexOffset) * srcWidth
				/ (srcWidth * scaleX));
			// round down to get the left pixel
			xIndices[i] = index;
			// handle cropped source bitmap
			xIndices[i] += xBitmapShift;
			// precompute index for 32 bit pixels
			xIndices[i] *= 4;
		}

		for (uint32 i = 0; i < dstHeight; i++) {
			// index into source
			uint16 index = (uint16)((i + filterWeightYIndexOffset) * srcHeight
				/ (srcHeight * scaleY));
			// round down to get the top pixel
			yIndices[i] = index;
			// handle cropped source bitmap
			yIndices[i] += yBitmapShift;
		}
		//printf("X: %d ... %d, %d (%ld or %f)\n",
		//	xIndices[0], xIndices[dstWidth - 2], xIndices[dstWidth - 1],
		//	dstWidth, srcWidth * scaleX);
		//printf("Y: %d ... %d, %d (%ld or %f)\n",
		//	yIndices[0], yIndices[dstHeight - 2], yIndices[dstHeight - 1],
		//	dstHeight, srcHeight * scaleY);

		const int32 left = (int32)destinationRect.left;
		const int32 top = (int32)destinationRect.top;
		const int32 right = (int32)destinationRect.right;
		const int32 bottom = (int32)destinationRect.bottom;

		const uint32 dstBPR = aggInterface.fBuffer.stride();

		renderer_base& baseRenderer = aggInterface.fBaseRenderer;

		// iterate over clipping boxes
		baseRenderer.first_clip_box();
		do {
			const int32 x1 = max_c(baseRenderer.xmin(), left);
			const int32 x2 = min_c(baseRenderer.xmax(), right);
			if (x1 > x2)
				continue;

			int32 y1 = max_c(baseRenderer.ymin(), top);
			int32 y2 = min_c(baseRenderer.ymax(), bottom);
			if (y1 > y2)
				continue;

			// buffer offset into destination
			uint8* dst = aggInterface.fBuffer.row_ptr(y1) + x1 * 4;

			// x and y are needed as indeces into the wheight arrays, so the
			// offset into the target buffer needs to be compensated
			const int32 xIndexL = x1 - left - filterWeightXIndexOffset;
			const int32 xIndexR = x2 - left - filterWeightXIndexOffset;
			y1 -= top + filterWeightYIndexOffset;
			y2 -= top + filterWeightYIndexOffset;

		//printf("x: %ld - %ld\n", xIndexL, xIndexR);
		//printf("y: %ld - %ld\n", y1, y2);

			for (; y1 <= y2; y1++) {
				// buffer offset into source (top row)
				const uint8* src = bitmap.row_ptr(yIndices[y1]);
				// buffer handle for destination to be incremented per pixel
				uint32* d = (uint32*)dst;

				for (int32 x = xIndexL; x <= xIndexR; x++) {
					*d = *(uint32*)(src + xIndices[x]);
					d++;
				}
				dst += dstBPR;
			}
		} while (baseRenderer.next_clip_box());

		//printf("draw bitmap %.5fx%.5f: %lld\n", xScale, yScale,
		//	system_time() - now);
	}
};



#endif // DRAW_BITMAP_NEAREST_NEIGHBOR_H
