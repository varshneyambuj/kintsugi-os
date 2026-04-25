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
 *   Copyright 2007, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file FontCache.cpp
 * @brief Implementation of the process-wide FontCacheEntry registry.
 *
 * The cache stores at most ::kMaxEntryCount FontCacheEntry instances keyed
 * by a textual signature derived from each ServerFont. Reader/writer
 * locking is supplied by the MultiLocker base class so the common lookup
 * path stays read-only and only insertion takes the write lock. When the
 * table fills up the entry with the lowest @ref usage_index() is evicted.
 *
 * @see FontCacheEntry
 */


#include "FontCache.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include <Entry.h>
#include <Path.h>

#include "AutoLocker.h"


using std::nothrow;


/** @brief Static singleton instance returned by FontCache::Default(). */
FontCache
FontCache::sDefaultInstance;

// #pragma mark -

/**
 * @brief Constructs an empty FontCache and names its MultiLocker.
 */
// constructor
FontCache::FontCache()
	: MultiLocker("FontCache lock")
	, fFontCacheEntries()
{
}


/**
 * @brief Destroys the cache; reference-counted entries are released by the map.
 */
// destructor
FontCache::~FontCache()
{
}


/**
 * @brief Returns the singleton FontCache used by the entire app_server.
 *
 * @return Pointer to the static instance; never NULL.
 */
// Default
/*static*/ FontCache*
FontCache::Default()
{
	return &sDefaultInstance;
}


/**
 * @brief Returns a cached FontCacheEntry for @a font, creating one on miss.
 *
 * The lookup is first attempted under a read lock; if the signature is
 * not present the lock is upgraded to a write lock, a re-check guards
 * against a racing inserter, and a fresh FontCacheEntry is created and
 * inserted. Cache size is bounded by @ref _ConstrainEntryCount() before
 * each new insertion.
 *
 * @param font         The ServerFont whose entry is requested.
 * @param forceVector  Force vector glyph storage for shape rendering.
 * @return  A reference-detached pointer to the cache entry, or NULL on
 *          allocation/lock failure or when the underlying font cannot
 *          be loaded. Caller balances the returned reference via
 *          @ref FontCache::Recycle().
 */
// FontCacheEntryFor
FontCacheEntry*
FontCache::FontCacheEntryFor(const ServerFont& font, bool forceVector)
{
	static const size_t signatureSize = 512;
	char signature[signatureSize];
	FontCacheEntry::GenerateSignature(signature, signatureSize, font,
		forceVector);

	AutoReadLocker readLocker(this);

	BReference<FontCacheEntry> entry = fFontCacheEntries.Get(signature);

	if (entry) {
		// the entry was already there
//printf("FontCacheEntryFor(%ld): %p\n", font.GetFamilyAndStyle(), entry);
		return entry.Detach();
	}

	readLocker.Unlock();

	AutoWriteLocker locker(this);
	if (!locker.IsLocked())
		return NULL;

	// prevent getting screwed by a race condition:
	// when we released the readlock above, another thread might have
	// gotten the writelock before we have, and might have already
	// inserted a cache entry for this font. So we look again if there
	// is an entry now, and only then create it if it's still not there,
	// all while holding the writelock
	entry = fFontCacheEntries.Get(signature);

	if (!entry) {
		// remove old entries, keep entries below certain count
		_ConstrainEntryCount();
		entry.SetTo(new (nothrow) FontCacheEntry(), true);
		if (!entry || !entry->Init(font, forceVector)
			|| fFontCacheEntries.Put(signature, entry) < B_OK) {
			fprintf(stderr, "FontCache::FontCacheEntryFor() - "
				"out of memory or no font file\n");
			return NULL;
		}
	}
//printf("FontCacheEntryFor(%ld): %p (insert)\n", font.GetFamilyAndStyle(), entry);

	return entry.Detach();
}


/**
 * @brief Returns @a entry to the cache and updates its LRU statistics.
 *
 * Bookkeeps usage so a future @ref _ConstrainEntryCount() pass sees
 * recently-used entries as still-hot, then drops the reference taken by
 * @ref FontCacheEntryFor().
 *
 * @param entry  Entry previously obtained from FontCacheEntryFor(); may
 *               be NULL, in which case the call is a no-op.
 */
// Recycle
void
FontCache::Recycle(FontCacheEntry* entry)
{
//printf("Recycle(%p)\n", entry);
	if (!entry)
		return;
	entry->UpdateUsage();
	entry->ReleaseReference();
}


/** @brief Maximum number of FontCacheEntry instances retained simultaneously. */
static const int32 kMaxEntryCount = 30;


/**
 * @brief Heuristic LRU score used to pick eviction victims.
 *
 * Higher scores mean "more useful": the score grows with @a useCount and
 * shrinks with @a age, so frequently and recently used entries are
 * favored over rarely used ones.
 *
 * @param useCount  Cumulative number of times the entry was recycled.
 * @param age       Microseconds since the entry was last used.
 * @return          Dimensionless usage index (higher is better to keep).
 */
static inline double
usage_index(uint64 useCount, bigtime_t age)
{
	return 100.0 * useCount / age;
}


/**
 * @brief Drops the least-recently-used entry when the table reaches its cap.
 *
 * Walks the map computing @ref usage_index() for every entry and removes
 * the one with the lowest score. Must only be called with the write lock
 * held.
 *
 * @note Returns immediately if the cache holds fewer than ::kMaxEntryCount
 *       entries, so eviction is amortized.
 */
// _ConstrainEntryCount
void
FontCache::_ConstrainEntryCount()
{
	// this function is only ever called with the WriteLock held
	if (fFontCacheEntries.Size() < kMaxEntryCount)
		return;
//printf("FontCache::_ConstrainEntryCount()\n");

	FontMap::Iterator iterator = fFontCacheEntries.GetIterator();

	// NOTE: if kMaxEntryCount has a sane value, there has got to be
	// some entries, so using the iterator like that should be ok
	FontCacheEntry* leastUsedEntry = iterator.Next().value;
	bigtime_t now = system_time();
	bigtime_t age = now - leastUsedEntry->LastUsed();
	uint64 useCount = leastUsedEntry->UsedCount();
	double leastUsageIndex = usage_index(useCount, age);
//printf("  leastUsageIndex: %f\n", leastUsageIndex);

	while (iterator.HasNext()) {
		FontCacheEntry* entry = iterator.Next().value;
		age = now - entry->LastUsed();
		useCount = entry->UsedCount();
		double usageIndex = usage_index(useCount, age);
//printf("  usageIndex: %f\n", usageIndex);
		if (usageIndex < leastUsageIndex) {
			leastUsedEntry = entry;
			leastUsageIndex = usageIndex;
		}
	}

	iterator = fFontCacheEntries.GetIterator();
	while (iterator.HasNext()) {
		if (iterator.Next().value.Get() == leastUsedEntry) {
			fFontCacheEntries.Remove(iterator);
			break;
		}
	}
}
