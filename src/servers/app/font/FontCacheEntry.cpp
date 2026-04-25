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
 *   Copyright 2007-2009, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Maxim Shemanarev <mcseemagg@yahoo.com>
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Andrej Spielmann, <andrej.spielmann@seh.ox.ac.uk>
 *
 *   Portions derived from Anti-Grain Geometry, version 2.4,
 *   Copyright (C) 2002-2005 Maxim Shemanarev (http://www.antigrain.com).
 *   Permission to copy, use, modify, sell and distribute this software is
 *   granted provided this copyright notice appears in all copies. This
 *   software is provided "as is" without express or implied warranty,
 *   and with no claim as to its suitability for any purpose.
 *
 *   Contact: mcseem@antigrain.com
 *            mcseemagg@yahoo.com
 *            http://www.antigrain.com
 */


/**
 * @file FontCacheEntry.cpp
 * @brief Implementation of FontCacheEntry, the per-font glyph cache.
 *
 * A FontCacheEntry pairs one FontEngine (an FT_Face plus its rasterizer
 * configuration) with an open hash table of GlyphCache records keyed by
 * Unicode glyph code. The pool implementation lives in this file as a
 * private nested class so the hash table definition can stay self
 * contained. Special-casing for whitespace and zero-width control code
 * points avoids generating "tofu" glyphs for characters that should be
 * invisible.
 *
 * @see FontEngine, FontCache
 */


#include "FontCacheEntry.h"

#include <string.h>

#include <new>

#include <Autolock.h>

#include <agg_array.h>
#include <utf8_functions.h>
#include <util/OpenHashTable.h>

#include "GlobalSubpixelSettings.h"


/** @brief Static lock protecting concurrent UpdateUsage() callers. */
BLocker FontCacheEntry::sUsageUpdateLock("FontCacheEntry usage lock");


/**
 * @brief Open-addressed hash table of GlyphCache records owned by an entry.
 *
 * Defined as a nested class before any inline use so gcc2 in debug
 * builds can resolve the type. Wraps a BOpenHashTable with the
 * GlyphHashTableDefinition policy below; ownership of every glyph is
 * transferred to the pool which deletes them in its destructor.
 */
class FontCacheEntry::GlyphCachePool {
	// This class needs to be defined before any inline functions, as otherwise
	// gcc2 will barf in debug mode.
	/**
	 * @brief Hash policy mapping glyph code -> GlyphCache for the open hash table.
	 */
	struct GlyphHashTableDefinition {
		typedef uint32		KeyType;
		typedef	GlyphCache	ValueType;

		/** @brief Identity hash on the glyph index. */
		size_t HashKey(uint32 key) const
		{
			return key;
		}

		/** @brief Hash for an existing GlyphCache value. */
		size_t Hash(GlyphCache* value) const
		{
			return value->glyph_index;
		}

		/** @brief Equality between a glyph index key and a GlyphCache value. */
		bool Compare(uint32 key, GlyphCache* value) const
		{
			return value->glyph_index == key;
		}

		/** @brief Returns the next-pointer storage used by the hash chain. */
		GlyphCache*& GetLink(GlyphCache* value) const
		{
			return value->hash_link;
		}
	};
public:
	/** @brief Constructs an empty pool; Init() must be called before use. */
	GlyphCachePool()
	{
	}

	/** @brief Destroys every glyph held by the table. */
	~GlyphCachePool()
	{
		GlyphCache* glyph = fGlyphTable.Clear(true);
		while (glyph != NULL) {
			GlyphCache* next = glyph->hash_link;
			delete glyph;
			glyph = next;
		}
	}

	/** @brief Initializes the underlying open hash table. */
	status_t Init()
	{
		return fGlyphTable.Init();
	}

	/** @brief Looks up a glyph by code; returns NULL when absent. */
	const GlyphCache* FindGlyph(uint32 glyphIndex) const
	{
		return fGlyphTable.Lookup(glyphIndex);
	}

	/**
	 * @brief Allocates and inserts a new GlyphCache for @a glyphIndex.
	 *
	 * @return The freshly inserted glyph, or NULL when one already
	 *         exists for the same key or allocation failed.
	 *
	 * @todo The hash table grows without bounds; older entries are not
	 *       evicted yet.
	 */
	GlyphCache* CacheGlyph(uint32 glyphIndex,
		uint32 dataSize, glyph_data_type dataType, const agg::rect_i& bounds,
		float advanceX, float advanceY, float preciseAdvanceX,
		float preciseAdvanceY, float insetLeft, float insetRight)
	{
		GlyphCache* glyph = fGlyphTable.Lookup(glyphIndex);
		if (glyph != NULL)
			return NULL;

		glyph = new(std::nothrow) GlyphCache(glyphIndex, dataSize, dataType,
			bounds, advanceX, advanceY, preciseAdvanceX, preciseAdvanceY,
			insetLeft, insetRight);
		if (glyph == NULL || glyph->data == NULL) {
			delete glyph;
			return NULL;
		}

		// TODO: The HashTable grows without bounds. We should cleanup
		// older entries from time to time.

		fGlyphTable.Insert(glyph);

		return glyph;
	}

private:
	typedef BOpenHashTable<GlyphHashTableDefinition> GlyphTable;

	GlyphTable	fGlyphTable;
};


// #pragma mark -


/**
 * @brief Constructs an unused entry with a fresh, empty glyph pool.
 */
FontCacheEntry::FontCacheEntry()
	:
	MultiLocker("FontCacheEntry lock"),
	fGlyphCache(new(std::nothrow) GlyphCachePool()),
	fEngine(),
	fLastUsedTime(LONGLONG_MIN),
	fUseCounter(0)
{
}


/**
 * @brief Destroys the entry; the ObjectDeleter releases the glyph pool.
 */
FontCacheEntry::~FontCacheEntry()
{
//printf("~FontCacheEntry()\n");
}


/**
 * @brief Initializes the engine and glyph pool for @a font.
 *
 * Resolves the rendering type from the font's flags (subpixel, gray8,
 * mono, or vector) and asks the FontEngine to load the face either from
 * the on-disk path or from an in-memory blob. Failure leaves the entry
 * in an unusable state.
 *
 * @param font         Source ServerFont; supplies path, size, hinting.
 * @param forceVector  Force outline rasterization regardless of size.
 * @return  true on full success, false when the glyph pool is missing,
 *          the FreeType face cannot be loaded, or the hash table cannot
 *          be allocated.
 */
bool
FontCacheEntry::Init(const ServerFont& font, bool forceVector)
{
	if (!fGlyphCache.IsSet())
		return false;

	glyph_rendering renderingType = _RenderTypeFor(font, forceVector);

	// TODO: encoding from font
	FT_Encoding charMap = FT_ENCODING_NONE;
	bool hinting = font.Hinting();

	bool success;
	if (font.FontData() != NULL)
		success = fEngine.Init(NULL, font.FaceIndex(), font.Size(), charMap,
			renderingType, hinting, (const void*)font.FontData(), font.FontDataSize());
	else
		success = fEngine.Init(font.Path(), font.FaceIndex(), font.Size(), charMap,
			renderingType, hinting);

	if (!success) {
		fprintf(stderr, "FontCacheEntry::Init() - some error loading font "
			"file %s\n", font.Path());
		return false;
	}

	if (fGlyphCache->Init() != B_OK) {
		fprintf(stderr, "FontCacheEntry::Init() - failed to allocate "
			"GlyphCache table for font file %s\n", font.Path());
		return false;
	}

	return true;
}


/**
 * @brief Reports whether every code point in @a utf8String is already cached.
 *
 * @param utf8String  UTF-8 input bytes.
 * @param length      Maximum number of bytes to inspect.
 * @return  true when all decoded code points have a matching GlyphCache,
 *          false on the first miss.
 */
bool
FontCacheEntry::HasGlyphs(const char* utf8String, ssize_t length) const
{
	uint32 glyphCode;
	const char* start = utf8String;
	while ((glyphCode = UTF8ToCharCode(&utf8String))) {
		if (fGlyphCache->FindGlyph(glyphCode) == NULL)
			return false;
		if (utf8String - start + 1 > length)
			break;
	}
	return true;
}


/**
 * @brief Returns true when @a glyphCode should render as the space glyph.
 *
 * Covers the Unicode @c White_Space property: control characters,
 * no-break space variants, ogham space, en/em quad family, line/paragraph
 * separators, and the ideographic space.
 */
inline bool
render_as_space(uint32 glyphCode)
{
	// whitespace: render as space
	// as per Unicode PropList.txt: White_Space
	return (glyphCode >= 0x0009 && glyphCode <= 0x000d)
			// control characters
		|| (glyphCode == 0x0085)
			// another control
		|| (glyphCode == 0x00a0)
			// no-break space
		|| (glyphCode == 0x1680)
			// ogham space mark
		|| (glyphCode >= 0x2000 && glyphCode <= 0x200a)
			// en quand, hair space
		|| (glyphCode >= 0x2028 && glyphCode <= 0x2029)
			// line and paragraph separators
		|| (glyphCode == 0x202f)
			// narrow no-break space
		|| (glyphCode == 0x205f)
			// medium math space
		|| (glyphCode == 0x3000)
			// ideographic space
		;
}


/**
 * @brief Returns true when @a glyphCode should render as zero-width / invisible.
 *
 * Covers Unicode @c Default_Ignorable_Code_Point (soft hyphen, zero-width
 * spaces, format controls, variation selectors) plus reserved
 * non-characters; the catch-all path keeps "tofu" boxes from leaking
 * through for code points that have no visible representation.
 */
inline bool
render_as_zero_width(uint32 glyphCode)
{
	// ignorable chars: render as invisible
	// as per Unicode DerivedCoreProperties.txt: Default_Ignorable_Code_Point.
	// We also don't want tofu for noncharacters if we ever get one.
	return (glyphCode == 0x00ad)
			// soft hyphen
		|| (glyphCode == 0x034f)
			// combining grapheme joiner
		|| (glyphCode == 0x061c)
			// arabic letter mark
		|| (glyphCode >= 0x115f && glyphCode <= 0x1160)
			// hangul fillers
		|| (glyphCode >= 0x17b4 && glyphCode <= 0x17b5)
			// ignorable khmer vowels
		|| (glyphCode >= 0x180b && glyphCode <= 0x180f)
			// mongolian variation selectors and vowel separator
		|| (glyphCode >= 0x200b && glyphCode <= 0x200f)
			// zero width space, cursive joiners, ltr marks
		|| (glyphCode >= 0x202a && glyphCode <= 0x202e)
			// left to right embed, override
		|| (glyphCode >= 0x2060 && glyphCode <= 0x206f)
			// word joiner, invisible math operators, reserved
		|| (glyphCode == 0x3164)
			// hangul filler
		|| (glyphCode >= 0xfe00 && glyphCode <= 0xfe0f)
			// variation selectors
		|| (glyphCode == 0xfeff)
			// zero width no-break space
		|| (glyphCode == 0xffa0)
			// halfwidth hangul filler
		|| (glyphCode >= 0xfff0 && glyphCode <= 0xfff8)
			// reserved
		|| (glyphCode >= 0x1bca0 && glyphCode <= 0x1bca3)
			// shorthand format controls
		|| (glyphCode >= 0x1d173 && glyphCode <= 0x1d17a)
			// musical symbols
		|| (glyphCode >= 0xe0000 && glyphCode <= 0xe01ef)
			// variation selectors, tag space, reserved
		|| (glyphCode >= 0xe01f0 && glyphCode <= 0xe0fff)
			// reserved
		|| ((glyphCode & 0xffff) >= 0xfffe)
			// noncharacters
		|| ((glyphCode >= 0xfdd0 && glyphCode <= 0xfdef)
			&& glyphCode != 0xfdd1)
			// noncharacters; 0xfdd1 is used internally to force .notdef glyph
		;
}


/**
 * @brief Returns the cached glyph for @a glyphCode, or NULL on miss.
 *
 * @note Only requires the entry's read lock; never mutates the cache.
 */
const GlyphCache*
FontCacheEntry::CachedGlyph(uint32 glyphCode)
{
	// Only requires a read lock.
	return fGlyphCache->FindGlyph(glyphCode);
}


/**
 * @brief Reports whether the underlying engine knows how to draw @a glyphCode.
 *
 * Bypasses both the local cache and the fallback chain because it is
 * itself used to decide whether a fallback is needed.
 *
 * @return  true if FreeType maps @a glyphCode to a non-zero glyph index.
 */
bool
FontCacheEntry::CanCreateGlyph(uint32 glyphCode)
{
	// Note that this bypass any fallback or caching because it is used in
	// the fallback code itself.
	uint32 glyphIndex = fEngine.GlyphIndexForGlyphCode(glyphCode);
	return glyphIndex != 0;
}


/**
 * @brief Rasterizes (or fetches) the glyph for @a glyphCode and caches it.
 *
 * If the local FT_Face has no glyph for the code point and a
 * @a fallbackEntry is supplied, the fallback's engine is used to
 * rasterize the glyph; the bytes are still stored in the local pool so
 * subsequent lookups by glyph code hit. Whitespace is normalized to the
 * regular space glyph and zero-width controls cache an empty record.
 *
 * @param glyphCode      Unicode code point to render.
 * @param fallbackEntry  Optional companion entry consulted on miss.
 * @return  Pointer into the local glyph pool, or NULL on rasterization
 *          failure.
 *
 * @note Both this and the fallback FontCacheEntry are expected to be
 *       write-locked.
 */
const GlyphCache*
FontCacheEntry::CreateGlyph(uint32 glyphCode, FontCacheEntry* fallbackEntry)
{
	// We cache the glyph by the requested glyphCode. The FontEngine of this
	// FontCacheEntry may not contain a glyph for the given code, in which case
	// we ask the fallbackEntry for the code to index translation and let it
	// generate the glyph data. We will still use our own cache for storing the
	// glyph. The next time it will be found (by glyphCode).

	// NOTE: Both this and the fallback FontCacheEntry are expected to be
	// write-locked!

	const GlyphCache* glyph = fGlyphCache->FindGlyph(glyphCode);
	if (glyph != NULL)
		return glyph;

	FontEngine* engine = &fEngine;
	uint32 glyphIndex = engine->GlyphIndexForGlyphCode(glyphCode);
	if (glyphIndex == 0 && fallbackEntry != NULL) {
		// Our FontEngine does not contain this glyph, but we can retry with
		// the fallbackEntry.
		engine = &fallbackEntry->fEngine;
		glyphIndex = engine->GlyphIndexForGlyphCode(glyphCode);
	}

	if (glyphIndex == 0) {
		if (render_as_zero_width(glyphCode)) {
			// cache and return a zero width glyph
			return fGlyphCache->CacheGlyph(glyphCode, 0, glyph_data_invalid,
				agg::rect_i(0, 0, -1, -1), 0, 0, 0, 0, 0, 0);
		}

		// reset to our engine
		engine = &fEngine;
		if (render_as_space(glyphCode)) {
			// get the normal space glyph
			glyphIndex = engine->GlyphIndexForGlyphCode(0x20 /* space */);
		}
	}

	if (engine->PrepareGlyph(glyphIndex)) {
		glyph = fGlyphCache->CacheGlyph(glyphCode,
			engine->DataSize(), engine->DataType(), engine->Bounds(),
			engine->AdvanceX(), engine->AdvanceY(),
			engine->PreciseAdvanceX(), engine->PreciseAdvanceY(),
			engine->InsetLeft(), engine->InsetRight());

		if (glyph != NULL)
			engine->WriteGlyphTo(glyph->data);
	}

	return glyph;
}


/**
 * @brief Initializes the AGG adaptors that consume @a glyph's serialized data.
 *
 * Selects the right adapter for the glyph's storage format (mono/gray8/
 * subpixel/outline) and configures it with the glyph origin and an
 * optional outline scale. A null @a glyph is silently ignored so the
 * caller can pass through cache misses.
 *
 * @param glyph          The glyph to render, or NULL for a no-op.
 * @param x              Pen x position in pixels.
 * @param y              Pen y position in pixels.
 * @param monoAdapter    Output adapter populated for 1-bit glyphs.
 * @param gray8Adapter   Output adapter populated for 8-bit AA glyphs.
 * @param pathAdapter    Output adapter populated for vector glyphs.
 * @param scale          Per-glyph scale applied to outline mode only.
 */
void
FontCacheEntry::InitAdaptors(const GlyphCache* glyph,
	double x, double y, GlyphMonoAdapter& monoAdapter,
	GlyphGray8Adapter& gray8Adapter, GlyphPathAdapter& pathAdapter,
	double scale)
{
	if (glyph == NULL)
		return;

	switch(glyph->data_type) {
		case glyph_data_mono:
			monoAdapter.init(glyph->data, glyph->data_size, x, y);
			break;

		case glyph_data_gray8:
			gray8Adapter.init(glyph->data, glyph->data_size, x, y);
			break;

		case glyph_data_subpix:
			gray8Adapter.init(glyph->data, glyph->data_size, x, y);
			break;

		case glyph_data_outline:
			pathAdapter.init(glyph->data, glyph->data_size, x, y, scale);
			break;

		default:
			break;
	}
}


/**
 * @brief Adds the kerning offset between two consecutive glyphs to (*x, *y).
 *
 * Forwards to FontEngine::GetKerning(); returns false (and leaves the
 * outputs untouched) when the face has no kerning data.
 *
 * @param glyphCode1  First glyph in the pair.
 * @param glyphCode2  Second glyph in the pair.
 * @param x           In-out pen x; updated by the kerning delta on success.
 * @param y           In-out pen y; updated by the kerning delta on success.
 * @return  true when a kerning value was applied, false otherwise.
 */
bool
FontCacheEntry::GetKerning(uint32 glyphCode1, uint32 glyphCode2,
	double* x, double* y)
{
	return fEngine.GetKerning(glyphCode1, glyphCode2, x, y);
}


/**
 * @brief Builds a stable signature string identifying this font + render mode.
 *
 * The signature combines family/style ID, manager pointer, encoding,
 * face mask, rendering type, size, hinting, and the global subpixel
 * filter weight, so two ServerFonts that would produce identical glyphs
 * map to the same FontCache slot.
 *
 * @param signature      Output buffer; must be at least @a signatureSize bytes.
 * @param signatureSize  Capacity of @a signature.
 * @param font           Source ServerFont.
 * @param forceVector    Forces vector glyph storage in the signature.
 *
 * @todo  Read more rendering knobs (encoding, etc.) from the ServerFont.
 */
/*static*/ void
FontCacheEntry::GenerateSignature(char* signature, size_t signatureSize,
	const ServerFont& font, bool forceVector)
{
	glyph_rendering renderingType = _RenderTypeFor(font, forceVector);

	// TODO: read more of these from the font
	FT_Encoding charMap = FT_ENCODING_NONE;
	bool hinting = font.Hinting();
	uint8 averageWeight = gSubpixelAverageWeight;

	snprintf(signature, signatureSize, "%" B_PRId32 ",%p,%u,%d,%d,%.1f,%d,%d",
		font.GetFamilyAndStyle(), font.Manager(), charMap,
		font.Face(), int(renderingType), font.Size(), hinting, averageWeight);
}


/**
 * @brief Stamps the entry with the current time and bumps its use counter.
 *
 * Called by FontCache::Recycle() to feed the LRU heuristic. Uses a
 * single static lock for all entries to keep the semaphore footprint
 * bounded; the critical section is intentionally tiny.
 */
void
FontCacheEntry::UpdateUsage()
{
	// this is a static lock to prevent usage of too many semaphores,
	// but on the other hand, it is not so nice to be using a lock
	// here at all
	// the hope is that the time is so short to hold this lock, that
	// there is not much contention
	BAutolock _(sUsageUpdateLock);

	fLastUsedTime = system_time();
	fUseCounter++;
}


/**
 * @brief Selects the rasterizer mode for a given ServerFont configuration.
 *
 * Defaults to subpixel or gray8 depending on the global subpixel
 * antialiasing setting, then falls back to vector outlines whenever the
 * raster path cannot represent the request: rotated/sheared text, false
 * bold, antialiasing disabled, very large sizes, or hinting off.
 *
 * @param font         The ServerFont in question.
 * @param forceVector  Caller-imposed override forcing outline rendering.
 * @return  The chosen ::glyph_rendering value.
 */
/*static*/ glyph_rendering
FontCacheEntry::_RenderTypeFor(const ServerFont& font, bool forceVector)
{
	glyph_rendering renderingType = gSubpixelAntialiasing ?
		glyph_ren_subpix : glyph_ren_native_gray8;

	if (forceVector || font.Rotation() != 0.0 || font.Shear() != 90.0
		|| font.FalseBoldWidth() != 0.0
		|| (font.Flags() & B_DISABLE_ANTIALIASING) != 0
		|| font.Size() > 30
		|| !font.Hinting()) {
		renderingType = glyph_ren_outline;
	}

	return renderingType;
}
