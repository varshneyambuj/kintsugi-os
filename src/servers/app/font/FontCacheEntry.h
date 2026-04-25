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
 * MIT License. Copyright 2007-2009, Haiku.
 * Original authors: Maxim Shemanarev, Stephan Aßmus, Andrej Spielmann.
 *
 * Portions derived from Anti-Grain Geometry, version 2.4,
 * Copyright (C) 2002-2005 Maxim Shemanarev (http://www.antigrain.com).
 * Permission to copy, use, modify, sell and distribute this software is
 * granted provided this copyright notice appears in all copies. This
 * software is provided "as is" without express or implied warranty, and
 * with no claim as to its suitability for any purpose.
 */

/** @file FontCacheEntry.h
    @brief Per-(font, size, hinting) glyph cache built on top of FontEngine. */

#ifndef FONT_CACHE_ENTRY_H
#define FONT_CACHE_ENTRY_H


#include <AutoDeleter.h>
#include <Locker.h>

#include <agg_conv_curve.h>
#include <agg_conv_contour.h>
#include <agg_conv_transform.h>

#include "ServerFont.h"
#include "FontEngine.h"
#include "MultiLocker.h"
#include "Referenceable.h"
#include "Transformable.h"


/**
 * @brief Single rasterized glyph held in a FontCacheEntry.
 *
 * Stores the serialized scanline (or path) data produced by FontEngine,
 * along with the metrics needed to lay out the glyph during text rendering.
 * @c hash_link participates in the open-addressing hash table inside the
 * GlyphCachePool.
 */
struct GlyphCache {
	GlyphCache(uint32 glyphIndex, uint32 dataSize, glyph_data_type dataType,
			const agg::rect_i& bounds, float advanceX, float advanceY,
			float preciseAdvanceX, float preciseAdvanceY,
			float insetLeft, float insetRight)
		:
		glyph_index(glyphIndex),
		data((uint8*)malloc(dataSize)),
		data_size(dataSize),
		data_type(dataType),
		bounds(bounds),
		advance_x(advanceX),
		advance_y(advanceY),
		precise_advance_x(preciseAdvanceX),
		precise_advance_y(preciseAdvanceY),
		inset_left(insetLeft),
		inset_right(insetRight),
		hash_link(NULL)
	{
	}

	~GlyphCache()
	{
		free(data);
	}

	uint32			glyph_index;
	uint8*			data;
	uint32			data_size;
	glyph_data_type	data_type;
	agg::rect_i		bounds;
	float			advance_x;
	float			advance_y;
	float			precise_advance_x;
	float			precise_advance_y;
	float			inset_left;
	float			inset_right;

	GlyphCache*		hash_link;
};

class FontCache;

/**
 * @brief Cached, locked rasterizer state for one font configuration.
 *
 * A FontCacheEntry owns one FontEngine plus a hash table of GlyphCache
 * records. The entry is reference-counted, MultiLocker-protected, and
 * keyed inside FontCache by a signature derived from the ServerFont and
 * the rendering mode (mono/gray8/subpixel/outline). Typedefs at the top
 * of the class expose AGG adapter types used by the rasterizer pipeline.
 */
class FontCacheEntry : public MultiLocker, public BReferenceable {
 public:
	typedef FontEngine::PathAdapter					GlyphPathAdapter;
	typedef FontEngine::Gray8Adapter				GlyphGray8Adapter;
	typedef GlyphGray8Adapter::embedded_scanline	GlyphGray8Scanline;
	typedef FontEngine::MonoAdapter					GlyphMonoAdapter;
	typedef GlyphMonoAdapter::embedded_scanline		GlyphMonoScanline;
	typedef FontEngine::SubpixAdapter				SubpixAdapter;
	typedef agg::conv_curve<GlyphPathAdapter>		CurveConverter;
	typedef agg::conv_contour<CurveConverter>		ContourConverter;

	typedef agg::conv_transform<CurveConverter, Transformable>
													TransformedOutline;

	typedef agg::conv_transform<ContourConverter, Transformable>
													TransformedContourOutline;


								FontCacheEntry();
	virtual						~FontCacheEntry();

			bool				Init(const ServerFont& font, bool forceVector);

			bool				HasGlyphs(const char* utf8String,
									ssize_t glyphCount) const;

			const GlyphCache*	CachedGlyph(uint32 glyphCode);
			const GlyphCache*	CreateGlyph(uint32 glyphCode,
									FontCacheEntry* fallbackEntry = NULL);
			bool				CanCreateGlyph(uint32 glyphCode);

			void				InitAdaptors(const GlyphCache* glyph,
									double x, double y,
									GlyphMonoAdapter& monoAdapter,
									GlyphGray8Adapter& gray8Adapter,
									GlyphPathAdapter& pathAdapter,
									double scale = 1.0);

			bool				GetKerning(uint32 glyphCode1,
									uint32 glyphCode2, double* x, double* y);

	static	void				GenerateSignature(char* signature,
									size_t signatureSize,
									const ServerFont& font, bool forceVector);

	// private to FontCache class:
			void				UpdateUsage();
			/** @brief Wall-clock timestamp of the most recent UpdateUsage() call. */
			bigtime_t			LastUsed() const
									{ return fLastUsedTime; }
			/** @brief Total number of times this entry has been recycled into use. */
			uint64				UsedCount() const
									{ return fUseCounter; }

 private:
								FontCacheEntry(const FontCacheEntry&);
			const FontCacheEntry& operator=(const FontCacheEntry&);

	static	glyph_rendering		_RenderTypeFor(const ServerFont& font,
									bool forceVector);

			class GlyphCachePool;

			ObjectDeleter<GlyphCachePool>
								fGlyphCache;
			FontEngine			fEngine;

	static	BLocker				sUsageUpdateLock;
			bigtime_t			fLastUsedTime;
			uint64				fUseCounter;
};

#endif // FONT_CACHE_ENTRY_H
