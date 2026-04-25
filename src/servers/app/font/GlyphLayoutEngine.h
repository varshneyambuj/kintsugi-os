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
 * MIT License. Copyright 2007-2022, Haiku.
 * Original authors: Stephan Aßmus.
 */

/** @file GlyphLayoutEngine.h
    @brief Header-only template engine that turns a UTF-8 string into a stream
           of cached glyphs, driving fallback selection and consumer callbacks.

    The core entry point is the function template
    GlyphLayoutEngine::LayoutGlyphs<GlyphConsumer>(). It walks a UTF-8 string,
    looks each character up in a primary FontCacheEntry, falls back through
    Noto-family caches when the primary engine cannot render a character, and
    invokes @c consumer.ConsumeGlyph()/ConsumeEmptyGlyph() for each code point.
    Templated on the consumer so the same layout logic serves bounding-box
    measurement, painting, and width queries without allocations or virtual
    dispatch. FontCacheReference encapsulates the read/write locking dance
    around a FontCacheEntry plus its currently-promoted fallback. */

#ifndef GLYPH_LAYOUT_ENGINE_H
#define GLYPH_LAYOUT_ENGINE_H

#include "utf8_functions.h"

#include "FontCache.h"
#include "FontCacheEntry.h"
#include "GlobalFontManager.h"
#include "ServerFont.h"

#include <Autolock.h>
#include <Debug.h>
#include <ObjectList.h>
#include <SupportDefs.h>

#include <ctype.h>

/**
 * @brief Scoped read/write lock around a FontCacheEntry plus optional fallback.
 *
 * Owns the lock state for one primary FontCacheEntry and at most one
 * fallback FontCacheReference. Promotes the read lock to a write lock
 * when CreateGlyph() needs to mutate the cache, and tears everything down
 * (unlocking and recycling) on destruction or after a lock failure.
 */
class FontCacheReference {
public:
	FontCacheReference()
		:
		fCacheEntry(NULL),
		fFallbackReference(NULL),
		fLocked(false),
		fWriteLocked(false)
	{
	}

	~FontCacheReference()
	{
		if (fCacheEntry == NULL)
			return;

		fFallbackReference = NULL;
		Unlock();
		if (fCacheEntry != NULL)
			FontCache::Default()->Recycle(fCacheEntry);
	}

	/** @brief Adopts @a entry as the primary cache entry; must be unset. */
	void SetTo(FontCacheEntry* entry)
	{
		ASSERT(entry != NULL);
		ASSERT(fCacheEntry == NULL);

		fCacheEntry = entry;
	}

	/** @brief Acquires (or upgrades to) a read lock on the primary entry. */
	bool ReadLock()
	{
		ASSERT(fCacheEntry != NULL);
		ASSERT(fWriteLocked == false);

		if (fLocked)
			return true;

		if (!fCacheEntry->ReadLock()) {
			_Cleanup();
			return false;
		}

		fLocked = true;
		return true;
	}

	/** @brief Promotes the held lock to a write lock for cache mutation. */
	bool WriteLock()
	{
		ASSERT(fCacheEntry != NULL);

		if (fWriteLocked)
			return true;

		if (fLocked) {
			if (!fCacheEntry->ReadUnlock()) {
				_Cleanup();
				return false;
			}
		}
		if (!fCacheEntry->WriteLock()) {
			_Cleanup();
			return false;
		}

		fLocked = true;
		fWriteLocked = true;
		return true;
	}

	/** @brief Releases whichever (read or write) lock is currently held. */
	bool Unlock()
	{
		ASSERT(fCacheEntry != NULL);

		if (!fLocked)
			return true;

		if (fWriteLocked) {
			if (!fCacheEntry->WriteUnlock()) {
				_Cleanup();
				return false;
			}
		} else {
			if (!fCacheEntry->ReadUnlock()) {
				_Cleanup();
				return false;
			}
		}

		fLocked = false;
		fWriteLocked = false;
		return true;
	}

	/**
	 * @brief Sets @a fallback as the engine for missing glyphs and locks both.
	 *
	 * Both the primary and fallback entries are taken under their write
	 * locks in pointer order to avoid deadlock, since CreateGlyph() needs
	 * to consult the fallback engine but write the resulting glyph into
	 * the primary cache.
	 */
	bool SetFallback(FontCacheReference* fallback)
	{
		ASSERT(fCacheEntry != NULL);
		ASSERT(fallback != NULL);
		ASSERT(fallback->Entry() != NULL);
		ASSERT(fallback->Entry() != fCacheEntry);

		if (fFallbackReference == fallback)
			return true;

		if (fFallbackReference != NULL) {
			fFallbackReference->Unlock();
			fFallbackReference = NULL;
		}

		// We need to create new glyphs with the engine of the fallback font
		// and store them in the main font cache (not just transfer them from
		// one cache to the other). So we need both to be write-locked.
		if (fallback->Entry() < fCacheEntry) {
			if (fLocked && !Unlock())
				return false;
			if (!fallback->WriteLock()) {
				WriteLock();
				return false;
			}
			fFallbackReference = fallback;
			return WriteLock();
		}
		if (fLocked && !fWriteLocked && !Unlock())
			return false;
		if (!WriteLock() || !fallback->WriteLock())
			return false;
		fFallbackReference = fallback;
		return true;
	}

	/** @brief Returns the held primary FontCacheEntry, or NULL if unset. */
	inline FontCacheEntry* Entry() const
	{
		return fCacheEntry;
	}

	/** @brief Returns true when the primary entry is currently write-locked. */
	inline bool WriteLocked() const
	{
		return fWriteLocked;
	}

private:

	void _Cleanup()
	{
		if (fFallbackReference != NULL) {
			fFallbackReference->Unlock();
			fFallbackReference = NULL;
		}
		if (fCacheEntry != NULL)
			FontCache::Default()->Recycle(fCacheEntry);
		fCacheEntry = NULL;
		fLocked = false;
		fWriteLocked = false;
	}

private:
			FontCacheEntry*		fCacheEntry;
			FontCacheReference*	fFallbackReference;
			bool				fLocked;
			bool				fWriteLocked;
};


/**
 * @brief Static, header-only facade that drives glyph layout for ServerFont text.
 *
 * The class is non-instantiable: every member is a static (or template-static)
 * helper. LayoutGlyphs() is the workhorse template; the rest of the surface
 * provides utilities to look up cache entries, build the fallback chain
 * (Noto Sans family by default), and classify white-space code points.
 */
class GlyphLayoutEngine {
public:
	static	bool				IsWhiteSpace(uint32 glyphCode);

	static	FontCacheEntry*		FontCacheEntryFor(const ServerFont& font,
									bool forceVector);

			template<class GlyphConsumer>
	static	bool				LayoutGlyphs(GlyphConsumer& consumer,
									const ServerFont& font,
									const char* utf8String,
									int32 length, int32 maxChars,
									const escapement_delta* delta = NULL,
									uint8 spacing = B_BITMAP_SPACING,
									const BPoint* offsets = NULL,
									FontCacheReference* cacheReference = NULL);

	static	void				PopulateFallbacks(
									BObjectList<FontCacheReference, true>& fallbacks,
									const ServerFont& font, bool forceVector);

	static FontCacheReference*	GetFallbackReference(
									BObjectList<FontCacheReference, true>& fallbacks,
									uint32 charCode);

private:
	static	const GlyphCache*	_CreateGlyph(
									FontCacheReference& cacheReference,
									BObjectList<FontCacheReference, true>& fallbacks,
									const ServerFont& font, bool needsVector,
									uint32 glyphCode);

								GlyphLayoutEngine();
	virtual						~GlyphLayoutEngine();
};


/**
 * @brief Returns true if @a charCode is one of the recognized white-space code points.
 *
 * Used by LayoutGlyphs() to decide whether @c escapement_delta::space or
 * @c escapement_delta::nonspace should be applied for that character.
 */
inline bool
GlyphLayoutEngine::IsWhiteSpace(uint32 charCode)
{
	switch (charCode) {
		case 0x0009:	/* tab */
		case 0x000b:	/* vertical tab */
		case 0x000c:	/* form feed */
		case 0x0020:	/* space */
		case 0x00a0:	/* non breaking space */
		case 0x000a:	/* line feed */
		case 0x000d:	/* carriage return */
		case 0x2028:	/* line separator */
		case 0x2029:	/* paragraph separator */
			return true;
	}

	return false;
}


/**
 * @brief Looks up or creates the FontCache entry matching @a font.
 *
 * @param font         ServerFont describing family/style/size/hinting.
 * @param forceVector  Force vector (outline) glyph storage even when the
 *                     subpixel rasterizer would otherwise be selected.
 * @return The cache entry, or NULL on allocation/load failure.
 */
inline FontCacheEntry*
GlyphLayoutEngine::FontCacheEntryFor(const ServerFont& font, bool forceVector)
{
	FontCache* cache = FontCache::Default();
	FontCacheEntry* entry = cache->FontCacheEntryFor(font, forceVector);
	return entry;
}


/**
 * @brief Walks @a utf8String and feeds each glyph to @a consumer.
 *
 * For every Unicode code point found in @a utf8String the engine resolves
 * a GlyphCache (using the primary FontCacheEntry, then a fallback chain
 * derived from the Noto family, then the missing-glyph as a last resort)
 * and invokes @c consumer.ConsumeGlyph()/ConsumeEmptyGlyph(). If
 * @a _cacheReference is non-NULL its already-locked entry is reused so
 * callers can run a measurement pass and a render pass back-to-back
 * without re-locking.
 *
 * @param consumer         User-supplied callback receiver.
 * @param font             ServerFont selecting the primary cache entry.
 * @param utf8String       NUL- or length-bounded UTF-8 input.
 * @param length           Maximum bytes to scan from @a utf8String.
 * @param maxChars         Maximum number of code points to emit.
 * @param delta            Optional per-character spacing adjustment.
 * @param spacing          One of the Be spacing modes (B_BITMAP_SPACING etc.).
 * @param offsets          Optional explicit per-glyph offset array.
 * @param _cacheReference  Optional pre-locked reference reused across passes.
 * @return  true on a normal walk-to-completion, false if the cache could
 *          not be acquired or the consumer returned a hard stop.
 */
template<class GlyphConsumer>
inline bool
GlyphLayoutEngine::LayoutGlyphs(GlyphConsumer& consumer,
	const ServerFont& font,
	const char* utf8String, int32 length, int32 maxChars,
	const escapement_delta* delta, uint8 spacing,
	const BPoint* offsets, FontCacheReference* _cacheReference)
{
	// TODO: implement spacing modes
	FontCacheEntry* entry = NULL;
	FontCacheReference* pCacheReference;
	FontCacheReference cacheReference;
	BObjectList<FontCacheReference, true> fallbacksList(21);

	if (_cacheReference != NULL) {
		pCacheReference = _cacheReference;
		entry = _cacheReference->Entry();
		// When there is already a cacheReference, it means there was already
		// an iteration over the glyphs. The use-case is for example to do
		// a layout pass to get the string width for the bounding box, then a
		// second layout pass to actually render the glyphs to the screen.
		// This means that the fallback entry mechanism will not do any good
		// for the second pass, since the fallback glyphs have been stored in
		// the original entry.
	} else
		pCacheReference = &cacheReference;

	if (entry == NULL) {
		entry = FontCacheEntryFor(font, consumer.NeedsVector());

		if (entry == NULL)
			return false;
		pCacheReference->SetTo(entry);
		if (!pCacheReference->ReadLock())
			return false;
	} // else the entry was already used and is still locked

	consumer.Start();

	double x = 0.0;
	double y = 0.0;
	if (offsets) {
		x = offsets[0].x;
		y = offsets[0].y;
	}

	double advanceX = 0.0;
	double advanceY = 0.0;
	double size = font.Size();

	uint32 lastCharCode = 0; // Needed for kerning in B_STRING_SPACING mode
	uint32 charCode;
	int32 index = 0;
	const char* start = utf8String;
	while (maxChars-- > 0 && (charCode = UTF8ToCharCode(&utf8String)) != 0) {

		if (offsets != NULL) {
			// Use direct glyph locations instead of calculating them
			// from the advance values
			x = offsets[index].x;
			y = offsets[index].y;
		} else {
			if (spacing == B_STRING_SPACING)
				entry->GetKerning(lastCharCode, charCode, &advanceX, &advanceY);

			x += advanceX;
			y += advanceY;
		}

		const GlyphCache* glyph = entry->CachedGlyph(charCode);
		if (glyph == NULL) {
			glyph = _CreateGlyph(*pCacheReference, fallbacksList, font,
				consumer.NeedsVector(), charCode);

			// Something may have gone wrong while reacquiring the entry lock
			if (pCacheReference->Entry() == NULL)
				return false;
		}

		if (glyph == NULL) {
			consumer.ConsumeEmptyGlyph(index++, charCode, x, y);
			advanceX = 0;
			advanceY = 0;
		} else {
			// get next increment for pen position
			if (spacing == B_CHAR_SPACING) {
				advanceX = glyph->precise_advance_x * size;
				advanceY = glyph->precise_advance_y * size;
			} else {
				advanceX = glyph->advance_x;
				advanceY = glyph->advance_y;
			}

			// adjust for custom spacing
			if (delta != NULL) {
				advanceX += IsWhiteSpace(charCode)
					? delta->space : delta->nonspace;
			}

			if (!consumer.ConsumeGlyph(index++, charCode, glyph, entry, x, y,
					advanceX, advanceY)) {
				advanceX = 0.0;
				advanceY = 0.0;
				break;
			}
		}

		lastCharCode = charCode;
		if (utf8String - start + 1 > length)
			break;
	}

	x += advanceX;
	y += advanceY;
	consumer.Finish(x, y);

	return true;
}


/**
 * @brief Materializes a glyph for @a charCode, picking up a fallback if needed.
 *
 * Tries the primary entry first; if it cannot create the glyph, lazily
 * populates @a fallbacks and walks them looking for one that can. Falls
 * back to the missing-glyph symbol when no engine in the chain knows
 * the code point.
 */
inline const GlyphCache*
GlyphLayoutEngine::_CreateGlyph(FontCacheReference& cacheReference,
	BObjectList<FontCacheReference, true>& fallbacks,
	const ServerFont& font, bool forceVector, uint32 charCode)
{
	FontCacheEntry* entry = cacheReference.Entry();

	// Avoid loading the fallbacks if our font can create the glyph.
	if (entry->CanCreateGlyph(charCode)) {
		if (cacheReference.WriteLock())
			return entry->CreateGlyph(charCode);
		return NULL;
	}

	if (fallbacks.IsEmpty())
		PopulateFallbacks(fallbacks, font, forceVector);

	FontCacheReference* fallbackReference = GetFallbackReference(fallbacks, charCode);
	if (fallbackReference != NULL) {
		if (cacheReference.SetFallback(fallbackReference))
			return entry->CreateGlyph(charCode, fallbackReference->Entry());
		if (cacheReference.Entry() == NULL)
			return NULL;
	}

	// No one knows how to draw this, so use the missing glyph symbol.
	if (cacheReference.WriteLock())
		return entry->CreateGlyph(charCode);
	return NULL;
}


/**
 * @brief Builds the fallback FontCacheReference chain for @a font.
 *
 * Iterates a fixed list of Noto fallback families in three degradation
 * passes (exact style, then "Regular", then any style) and adds one
 * cached reference per resolvable family/style. Lock contention with
 * gFontManager keeps this sequential.
 */
inline void
GlyphLayoutEngine::PopulateFallbacks(
	BObjectList<FontCacheReference, true>& fallbacksList,
	const ServerFont& font, bool forceVector)
{
	ASSERT(fallbacksList.IsEmpty());

	// TODO: We always get the fallback glyphs from the Noto family, but of
	// course the fallback font should a) contain the missing glyphs at all
	// and b) be similar to the original font. So there should be a mapping
	// of some kind to know the most suitable fallback font.
	static const char* fallbacks[] = {
		"Noto Sans",
		"Noto Sans Thai",
		"Noto Sans CJK JP",
		"Noto Sans Cherokee",
		"Noto Sans Symbols",
		"Noto Sans Symbols 2",
		"Noto Emoji",
	};

	if (!gFontManager->Lock())
		return;

	static const int nFallbacks = B_COUNT_OF(fallbacks);
	static const int acceptAnyStyle = 2;

	for (int degradeLevel = 0; degradeLevel <= acceptAnyStyle; degradeLevel++) {
		const char* fontStyle;
		if (degradeLevel == 0)
			fontStyle = font.Style();
		else if (degradeLevel == 1)
			fontStyle = "Regular";
		else
			fontStyle = NULL;

		for (int i = 0; i < nFallbacks; i++) {

			FontStyle* fallbackStyle;
			if (degradeLevel != acceptAnyStyle) {
				fallbackStyle = gFontManager->GetStyle(fallbacks[i], fontStyle);
			} else {
				// At this point we'll just take whatever we are given
				fallbackStyle = gFontManager->GetStyleByIndex(fallbacks[i], 0);
			}

			if (fallbackStyle == NULL)
				continue;

			ServerFont fallbackFont(*fallbackStyle, font.Size());

			FontCacheEntry* entry = FontCacheEntryFor(fallbackFont, forceVector);
			if (entry == NULL)
				continue;

			FontCacheReference* cacheReference = new(std::nothrow) FontCacheReference();
			if (cacheReference != NULL) {
				cacheReference->SetTo(entry);
				fallbacksList.AddItem(cacheReference);
			} else
				FontCache::Default()->Recycle(entry);
		}
	}

	gFontManager->Unlock();
}


/**
 * @brief Returns the first fallback in @a fallbacks able to render @a charCode,
 *        or NULL when none in the chain knows it.
 */
inline FontCacheReference*
GlyphLayoutEngine::GetFallbackReference(
	BObjectList<FontCacheReference, true>& fallbacks, uint32 charCode)
{
	int32 count = fallbacks.CountItems();
	for (int32 index = 0; index < count; index++) {
		FontCacheReference* fallbackReference = fallbacks.ItemAt(index);
		FontCacheEntry* fallbackEntry = fallbackReference->Entry();
		if (fallbackEntry != NULL && fallbackEntry->CanCreateGlyph(charCode))
			return fallbackReference;
	}
	return NULL;
}


#endif // GLYPH_LAYOUT_ENGINE_H
