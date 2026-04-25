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
 * MIT License. Copyright 2005-2007, Stephan Aßmus; 2008, Andrej Spielmann;
 * 2015, Julian Harnath.
 */

/** @file PainterAggInterface.h
    @brief Aggregate of every AGG pipeline object (rendering buffer, pixel
           format, rasterizers, scanlines, renderers, path storage) the
           Painter operates on. */

#ifndef PAINTER_DATA_H
#define PAINTER_DATA_H


#include "defines.h"

#include <agg_path_storage.h>


/**
 * @brief Bag of AGG pipeline state owned by a Painter.
 *
 * Bundles the rendering buffer, pixel format, base renderer, scanlines,
 * rasterizers and high-level renderers used by Painter for solid, fast
 * (binary), subpixel-AA and alpha-masked drawing modes. Holding these
 * together keeps construction order well-defined and lets Painter forward
 * fields by name through the macro shortcuts in Painter.cpp.
 */
struct PainterAggInterface {
	/**
	 * @brief Wires every AGG component to the same PatternHandler and
	 *        rendering buffer.
	 *
	 * @param patternHandler Pattern source consulted by the pixel format
	 *        when blending; must outlive this object.
	 */
	PainterAggInterface(PatternHandler& patternHandler)
		:
		fBuffer(),
		fPixelFormat(fBuffer, &patternHandler),
		fBaseRenderer(fPixelFormat),
		fUnpackedScanline(),
		fPackedScanline(),
		fRasterizer(),
		fRenderer(fBaseRenderer),
		fRendererBin(fBaseRenderer),
		fSubpixPackedScanline(),
		fSubpixUnpackedScanline(),
		fSubpixRasterizer(),
		fSubpixRenderer(fBaseRenderer),
		fMaskedUnpackedScanline(NULL),
		fClippedAlphaMask(NULL),
		fPath(),
		fCurve(fPath)
	{
	}

	/** @brief AGG view of the destination frame buffer (raw pixel rows). */
	agg::rendering_buffer	fBuffer;

	// AGG rendering and rasterization classes
	/** @brief Pixel format wrapping the buffer and consulting the PatternHandler. */
	pixfmt					fPixelFormat;
	/** @brief BRegion-clipped base renderer that gates every draw call. */
	renderer_base			fBaseRenderer;

	// Regular drawing mode: pixel-aligned, no alpha masking
	/** @brief Unpacked scanline used by the AA rasterizer (one cover per pixel). */
	scanline_unpacked_type	fUnpackedScanline;
	/** @brief Packed scanline used for general-purpose AA fills/strokes. */
	scanline_packed_type	fPackedScanline;
	/** @brief AA scanline rasterizer for ordinary path rendering. */
	rasterizer_type			fRasterizer;
	/** @brief Solid-color AA scanline renderer. */
	renderer_type			fRenderer;

	// Fast mode: no antialiasing needed (horizontal/vertical lines, ...)
	/** @brief Binary (non-AA) renderer for fast horizontal/vertical primitives. */
	renderer_bin_type		fRendererBin;

	// Subpixel mode
	/** @brief Packed subpixel scanline (three covers per pixel). */
	scanline_packed_subpix_type fSubpixPackedScanline;
	/** @brief Unpacked subpixel scanline (three covers per pixel). */
	scanline_unpacked_subpix_type fSubpixUnpackedScanline;
	/** @brief Subpixel-aware rasterizer (3x horizontal cells). */
	rasterizer_subpix_type	fSubpixRasterizer;
	/** @brief Subpixel scanline renderer used for LCD-style text. */
	renderer_subpix_type	fSubpixRenderer;

	// Alpha-Masked mode: for ClipToPicture
	// (this uses the standard rasterizer and renderer)
	/** @brief Optional alpha-masked scanline; non-NULL when ClipToPicture is active. */
	scanline_unpacked_masked_type* fMaskedUnpackedScanline;
	/** @brief Optional clipped alpha mask paired with @ref fMaskedUnpackedScanline. */
	agg::clipped_alpha_mask* fClippedAlphaMask;

	/** @brief Reusable path storage; refilled per draw call. */
	agg::path_storage		fPath;
	/** @brief Curve converter that turns @ref fPath bezier segments into line segments. */
	agg::conv_curve<agg::path_storage> fCurve;
};


#endif // PAINTER_DATA_H
