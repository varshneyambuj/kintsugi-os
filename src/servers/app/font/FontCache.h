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
 * Original authors: Stephan Aßmus.
 */

/** @file FontCache.h
    @brief Process-wide cache of FontCacheEntry objects keyed by font signature. */

#ifndef FONT_CACHE_H
#define FONT_CACHE_H

#include "FontCacheEntry.h"
#include "HashMap.h"
#include "HashString.h"
#include "MultiLocker.h"
#include "ServerFont.h"


/**
 * @brief Shared cache for FontCacheEntry instances used by app_server text rendering.
 *
 * Inherits from MultiLocker so the cache supports concurrent readers and a
 * single writer. Entries are looked up by a textual signature derived from
 * the ServerFont (family/style, size, hinting, etc.) and a flag selecting
 * vector vs. raster glyph storage. The default instance is reused across
 * all drawing contexts.
 */
class FontCache : public MultiLocker {
 public:
								FontCache();
	virtual						~FontCache();

	/** @brief Returns the singleton FontCache instance. */
	// global instance
	static	FontCache*			Default();

			FontCacheEntry*		FontCacheEntryFor(const ServerFont& font,
									bool forceVector);
			void				Recycle(FontCacheEntry* entry);

 private:
			void				_ConstrainEntryCount();

	static	FontCache			sDefaultInstance;

	typedef HashMap<HashString, BReference<FontCacheEntry> > FontMap;

			FontMap				fFontCacheEntries;
};

#endif // FONT_CACHE_H
