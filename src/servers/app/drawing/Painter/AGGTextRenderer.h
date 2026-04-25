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
 * MIT License. Copyright 2005-2009, Stephan Aßmus; 2008, Andrej Spielmann.
 */

/** @file AGGTextRenderer.h
    @brief AGG-based glyph rasterizer that turns ServerFont strings into
           pixels using cached FreeType glyphs and the Painter pipeline. */

#ifndef AGG_TEXT_RENDERER_H
#define AGG_TEXT_RENDERER_H


#include "defines.h"

#include "FontCacheEntry.h"
#include "ServerFont.h"
#include "Transformable.h"

#include <agg_conv_curve.h>
#include <agg_conv_contour.h>
#include <agg_scanline_u.h>


class FontCacheReference;


/**
 * @brief Glyph rasterizer connecting FreeType caches to the AGG pipeline.
 *
 * Holds the per-Painter glyph adaptors (mono, gray8, vector outline + curve +
 * contour) and consumes ServerFont metrics to render UTF-8 strings into the
 * supplied AGG renderers. Supports plain anti-aliased, subpixel anti-aliased
 * (LCD) and binary (mono) glyphs and falls back to vector outlines when the
 * embedded transform requires it.
 */
class AGGTextRenderer {
public:
								AGGTextRenderer(
									renderer_subpix_type& subpixRenderer,
									renderer_type& solidRenderer,
									renderer_bin_type& binRenderer,
									scanline_unpacked_type& scanline,
									scanline_unpacked_subpix_type&
										subpixScanline,
									rasterizer_subpix_type& subpixRasterizer,
									scanline_unpacked_masked_type*&
										maskedScanline,
									agg::trans_affine& viewTransformation);
	virtual						~AGGTextRenderer();

			void				SetFont(const ServerFont &font);
	/** @brief Returns the active ServerFont. */
	inline	const ServerFont&	Font() const
									{ return fFont; }

			void				SetHinting(bool hinting);
	/** @brief Returns whether glyph hinting is currently enabled. */
			bool				Hinting() const
									{ return fHinted; }

			void				SetAntialiasing(bool antialiasing);
	/** @brief Returns whether AA glyph rendering is currently enabled. */
			bool				Antialiasing() const
									{ return fAntialias; }

			BRect				RenderString(const char* utf8String,
									uint32 length, const BPoint& baseLine,
									const BRect& clippingFrame, bool dryRun,
									BPoint* nextCharPos,
									const escapement_delta* delta,
									FontCacheReference* cacheReference);

			BRect				RenderString(const char* utf8String,
									uint32 length, const BPoint* offsets,
									const BRect& clippingFrame, bool dryRun,
									BPoint* nextCharPos,
									FontCacheReference* cacheReference);

private:

	/** @brief Internal per-call helper consumed by GlyphLayoutEngine. */
	class StringRenderer;
	friend class StringRenderer;

	// Pipeline to process the vectors glyph paths (curves + contour)
	FontCacheEntry::GlyphPathAdapter	fPathAdaptor;
	FontCacheEntry::GlyphGray8Adapter	fGray8Adaptor;
	FontCacheEntry::GlyphGray8Scanline	fGray8Scanline;
	FontCacheEntry::GlyphMonoAdapter	fMonoAdaptor;
	FontCacheEntry::GlyphMonoScanline	fMonoScanline;

	FontCacheEntry::CurveConverter		fCurves;
	FontCacheEntry::ContourConverter	fContour;

	renderer_type&				fSolidRenderer;
	renderer_bin_type&			fBinRenderer;
	renderer_subpix_type&		fSubpixRenderer;
	scanline_unpacked_type&		fScanline;
	scanline_unpacked_subpix_type& fSubpixScanline;
	rasterizer_subpix_type&		fSubpixRasterizer;
	scanline_unpacked_masked_type*& fMaskedScanline;

	rasterizer_type				fRasterizer;
		// NOTE: the object has it's own rasterizer object
		// since it might be using a different gamma setting
		// to support non-anti-aliased text rendering

	ServerFont					fFont;
	bool						fHinted;
									// is glyph hinting active?
	bool						fAntialias;
	Transformable				fEmbeddedTransformation;
									// rotated or sheared font?
	agg::trans_affine&			fViewTransformation;
};

#endif // AGG_TEXT_RENDERER_H
