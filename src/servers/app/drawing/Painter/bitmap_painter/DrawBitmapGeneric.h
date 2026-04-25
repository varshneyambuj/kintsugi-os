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

/** @file DrawBitmapGeneric.h
    @brief Generic AGG-rasterised bitmap blit fallback that handles
           affine transforms, alpha masks, and tiled or clamped fills. */

#ifndef DRAW_BITMAP_GENERIC_H
#define DRAW_BITMAP_GENERIC_H

#include "Painter.h"


/** @brief Fill mode tag: clamps source coordinates to the bitmap edge. */
struct Fill {};

/** @brief Fill mode tag: wraps source coordinates with repeat semantics. */
struct Tile {};

/** @brief Primary template selecting the AGG image-accessor type for a
           pixel format and a Fill / Tile fill mode. */
template<typename PixFmt, typename Mode>
struct ImageAccessor {};

/** @brief ImageAccessor specialisation for Fill: edge-clamping clone
           accessor. */
template<typename PixFmt>
struct ImageAccessor<PixFmt, Fill> {
	typedef agg::image_accessor_clone<PixFmt> type;
};

/** @brief ImageAccessor specialisation for Tile: 2D wrap-repeat
           accessor. */
template<typename PixFmt>
struct ImageAccessor<PixFmt, Tile> {
	typedef agg::image_accessor_wrap<PixFmt,
		agg::wrap_mode_repeat, agg::wrap_mode_repeat> type;
};


/**
 * @brief Generic bitmap blitter that uses AGG's rasterizer plus an image
 *        span generator to render an affinely-transformed bitmap.
 *
 * Handles bilinear and nearest-neighbor sampling per
 * B_FILTER_BITMAP_BILINEAR, alpha masks, and tile or fill behaviour
 * selected by the FillMode template tag.
 */
template<typename FillMode>
struct DrawBitmapGeneric {
	/** @brief Sets up the source/destination affine transforms, the AGG
	           image span generator (bilinear or nearest-neighbor), and
	           runs render_scanlines_aa over a quad enclosing the
	           destination rect, optionally through an alpha mask. */
	static void
	Draw(const Painter* painter, PainterAggInterface& aggInterface,
		agg::rendering_buffer& bitmap, BPoint offset,
		double scaleX, double scaleY, BRect destinationRect, uint32 options)
	{
		// pixel format attached to bitmap
		typedef agg::pixfmt_bgra32 pixfmt_image;
		pixfmt_image pixf_img(bitmap);

		agg::trans_affine srcMatrix;
		// NOTE: R5 seems to ignore this offset when drawing bitmaps
		//	srcMatrix *= agg::trans_affine_translation(-actualBitmapRect.left,
		//		-actualBitmapRect.top);
		srcMatrix *= painter->Transform();

		agg::trans_affine imgMatrix;
		imgMatrix *= agg::trans_affine_translation(
			offset.x - destinationRect.left, offset.y - destinationRect.top);
		imgMatrix *= agg::trans_affine_scaling(scaleX, scaleY);
		imgMatrix *= agg::trans_affine_translation(destinationRect.left,
			destinationRect.top);
		imgMatrix *= painter->Transform();
		imgMatrix.invert();

		// image interpolator
		typedef agg::span_interpolator_linear<> interpolator_type;
		interpolator_type interpolator(imgMatrix);

		// scanline allocator
		agg::span_allocator<pixfmt_image::color_type> spanAllocator;

		// image accessor attached to pixel format of bitmap
		typedef
			typename ImageAccessor<pixfmt_image, FillMode>::type source_type;
		source_type source(pixf_img);

		// clip to the current clipping region's frame
		if (painter->IsIdentityTransform()) {
			destinationRect = destinationRect
				& painter->ClippingRegion()->Frame();
		}
		// convert to pixel coords (versus pixel indices)
		destinationRect.right++;
		destinationRect.bottom++;

		// path enclosing the bitmap
		agg::path_storage& path = aggInterface.fPath;
		rasterizer_type& rasterizer = aggInterface.fRasterizer;

		path.remove_all();
		path.move_to(destinationRect.left, destinationRect.top);
		path.line_to(destinationRect.right, destinationRect.top);
		path.line_to(destinationRect.right, destinationRect.bottom);
		path.line_to(destinationRect.left, destinationRect.bottom);
		path.close_polygon();

		agg::conv_transform<agg::path_storage> transformedPath(path,
			srcMatrix);
		rasterizer.reset();
		rasterizer.add_path(transformedPath);

		if ((options & B_FILTER_BITMAP_BILINEAR) != 0) {
			// image filter (bilinear)
			typedef agg::span_image_filter_rgba_bilinear<
				source_type, interpolator_type> span_gen_type;
			span_gen_type spanGenerator(source, interpolator);

			// render the path with the bitmap as scanline fill
			if (aggInterface.fMaskedUnpackedScanline != NULL) {
				agg::render_scanlines_aa(rasterizer,
					*aggInterface.fMaskedUnpackedScanline,
					aggInterface.fBaseRenderer, spanAllocator, spanGenerator);
			} else {
				agg::render_scanlines_aa(rasterizer,
					aggInterface.fUnpackedScanline,
					aggInterface.fBaseRenderer, spanAllocator, spanGenerator);
			}
		} else {
			// image filter (nearest neighbor)
			typedef agg::span_image_filter_rgba_nn<
				source_type, interpolator_type> span_gen_type;
			span_gen_type spanGenerator(source, interpolator);

			// render the path with the bitmap as scanline fill
			if (aggInterface.fMaskedUnpackedScanline != NULL) {
				agg::render_scanlines_aa(rasterizer,
					*aggInterface.fMaskedUnpackedScanline,
					aggInterface.fBaseRenderer, spanAllocator, spanGenerator);
			} else {
				agg::render_scanlines_aa(rasterizer,
					aggInterface.fUnpackedScanline,
					aggInterface.fBaseRenderer, spanAllocator, spanGenerator);
			}
		}
	}
};


#endif // DRAW_BITMAP_GENERIC_H
