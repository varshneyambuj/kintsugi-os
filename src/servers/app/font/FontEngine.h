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
 * MIT License. Copyright 2007, Haiku.
 * Original authors: Maxim Shemanarev, Stephan Aßmus, Andrej Spielmann.
 *
 * Portions derived from Anti-Grain Geometry, version 2.4,
 * Copyright (C) 2002-2005 Maxim Shemanarev (http://www.antigrain.com).
 * Permission to copy, use, modify, sell and distribute this software is
 * granted provided this copyright notice appears in all copies. This
 * software is provided "as is" without express or implied warranty, and
 * with no claim as to its suitability for any purpose.
 */

/** @file FontEngine.h
    @brief FreeType-backed glyph rasterizer that produces AGG scanline storage. */

#ifndef FONT_ENGINE_H
#define FONT_ENGINE_H

#include <SupportDefs.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <agg_scanline_storage_aa.h>
#include <agg_scanline_storage_bin.h>
#include <agg_scanline_u.h>
#include <agg_scanline_bin.h>
#include <agg_path_storage_integer.h>
#include <agg_rasterizer_scanline_aa.h>
#include <agg_conv_curve.h>
#include <agg_trans_affine.h>

#include "agg_scanline_storage_subpix.h"
#include "agg_scanline_u_subpix.h"
#include "agg_scanline_u_subpix_avrg_filtering.h"

#include "GlobalSubpixelSettings.h"


/**
 * @brief How a glyph should be rasterized by the engine.
 *
 * Selects between FreeType's native rasterizers (mono / gray8 / LCD subpixel)
 * and an outline mode that decomposes the FT outline into AGG path data.
 */
enum glyph_rendering {
	glyph_ren_native_mono,
	glyph_ren_native_gray8,
	glyph_ren_outline,
	glyph_ren_subpix
};


/**
 * @brief Discriminator for the format of a cached glyph's serialized data.
 *
 * Mirrors @ref glyph_rendering, plus an explicit "invalid" sentinel for
 * cache entries that hold no glyph data (e.g. zero-width control codes).
 */
enum glyph_data_type {
	glyph_data_invalid	= 0,
	glyph_data_mono		= 1,
	glyph_data_gray8	= 2,
	glyph_data_outline	= 3,
	glyph_data_subpix   = 4
};


/**
 * @brief Wrapper around an FT_Library / FT_Face that rasterizes glyphs into
 *        AGG scanline storage suitable for caching.
 *
 * Each FontEngine owns a single FreeType face, configured for one rendering
 * mode (mono, gray8, subpixel LCD, or vector outline). PrepareGlyph()
 * rasterizes a glyph and stores it into the engine's internal buffers;
 * WriteGlyphTo() then copies the serialized bytes into a caller-provided
 * buffer for the glyph cache.
 */
class FontEngine {
 public:
	typedef agg::serialized_scanlines_adaptor_subpix<uint8>	SubpixAdapter;
	typedef agg::serialized_scanlines_adaptor_aa<uint8>		Gray8Adapter;
	typedef agg::serialized_scanlines_adaptor_bin			MonoAdapter;
	typedef agg::scanline_storage_aa8						ScanlineStorageAA;
	typedef agg::scanline_storage_subpix8					ScanlineStorageSubpix;
	typedef agg::scanline_storage_bin						ScanlineStorageBin;
	typedef agg::serialized_integer_path_adaptor<int32, 6>	PathAdapter;

								FontEngine();
	virtual						~FontEngine();

			bool				Init(const char* fontFilePath,
									unsigned face_index, double size,
									FT_Encoding char_map,
									glyph_rendering ren_type,
									bool hinting,
									const void* fontFileBuffer = NULL,
									const long fontFileBufferSize = 0);

			/** @brief Returns the most recent FreeType error code (0 if none). */
			int					LastError() const
									{ return fLastError; }
			unsigned			CountFaces() const;
			/** @brief Returns true when hinting is enabled for this face. */
			bool				Hinting() const
									{ return fHinting; }


			uint32				GlyphIndexForGlyphCode(uint32 glyphCode) const;
			bool				PrepareGlyph(uint32 glyphIndex);

			/** @brief Byte size of the most recently prepared glyph's serialized data. */
			uint32				DataSize() const
									{ return fDataSize; }
			/** @brief Encoding of the most recently prepared glyph's serialized data. */
			glyph_data_type		DataType() const
									{ return fDataType; }
			/** @brief Pixel-space bounding rectangle of the prepared glyph. */
			const agg::rect_i&	Bounds() const
									{ return fBounds; }
			/** @brief Hinted horizontal advance of the prepared glyph, in pixels. */
			double				AdvanceX() const
									{ return fAdvanceX; }
			/** @brief Hinted vertical advance of the prepared glyph, in pixels. */
			double				AdvanceY() const
									{ return fAdvanceY; }
			/** @brief Unscaled, unhinted horizontal advance (for B_CHAR_SPACING). */
			double				PreciseAdvanceX() const
									{ return fPreciseAdvanceX; }
			/** @brief Unscaled, unhinted vertical advance (for B_CHAR_SPACING). */
			double				PreciseAdvanceY() const
									{ return fPreciseAdvanceY; }
			/** @brief Left side bearing of the prepared glyph, in pixels. */
			double				InsetLeft() const
									{ return fInsetLeft; }
			/** @brief Right side bearing of the prepared glyph, in pixels. */
			double				InsetRight() const
									{ return fInsetRight; }

			void				WriteGlyphTo(uint8* data) const;


			bool				GetKerning(uint32 first, uint32 second,
									double* x, double* y);

 private:
			// disallowed stuff:
								FontEngine(const FontEngine&);
			const FontEngine&	operator=(const FontEngine&);

			int					fLastError;
			bool				fLibraryInitialized;
			FT_Library			fLibrary;	// handle to library
			FT_Face				fFace;	  // FreeType font face handle

			glyph_rendering		fGlyphRendering;
			bool				fHinting;

			// members needed to generate individual glyphs according
			// to glyph rendering type
			uint32				fDataSize;
			glyph_data_type		fDataType;
			agg::rect_i			fBounds;
			double				fAdvanceX;
			double				fAdvanceY;
			double				fPreciseAdvanceX;
			double				fPreciseAdvanceY;
			double				fInsetLeft;
			double				fInsetRight;

			// these members are for caching memory allocations
			// when rendering glyphs
	typedef agg::path_storage_integer<int32, 6>		PathStorageType;
	typedef agg::conv_curve<PathStorageType>		CurveConverterType;

			PathStorageType		fPath;
			CurveConverterType	fCurves;
			agg::scanline_u8	fScanlineAA;
			agg::scanline_bin	fScanlineBin;
#ifdef AVERAGE_BASED_SUBPIXEL_FILTERING
			agg::scanline_u8_subpix_avrg_filtering fScanlineSubpix;
#else
			agg::scanline_u8_subpix fScanlineSubpix;
#endif

			ScanlineStorageAA	fScanlineStorageAA;
			ScanlineStorageBin	fScanlineStorageBin;
			ScanlineStorageSubpix fScanlineStorageSubpix;
};


#endif // FONT_ENGINE_H
