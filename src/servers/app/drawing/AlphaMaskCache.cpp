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
 *   Copyright 2015 Julian Harnath <julian.harnath@rwth-aachen.de>
 *   All rights reserved. Distributed under the terms of the MIT license.
 */


/**
 * @file AlphaMaskCache.cpp
 * @brief Implementation of the bounded LRU-ish cache for ShapeAlphaMasks.
 *
 * Each cache entry holds a reference on its ShapeAlphaMask. The total bitmap
 * footprint is bounded by @c kMaxCacheBytes; insertion evicts entries (in
 * iteration order) until the new mask fits. Masks below @c mask in the mask
 * stack are tracked through @c fIndirectCacheReferences so they are kept
 * alive while any cached mask still references them.
 */


#include "AlphaMaskCache.h"

#include "AlphaMask.h"
#include "ShapePrivate.h"

#include <AutoLocker.h>


//#define PRINT_ALPHA_MASK_CACHE_STATISTICS
#ifdef PRINT_ALPHA_MASK_CACHE_STATISTICS
/** @brief Counter used to throttle statistics dumps to once every 200 Get() calls. */
static uint32 sAlphaMaskGetCount = 0;
#endif


/** @brief Process-global default cache shared by all rendering threads. */
AlphaMaskCache AlphaMaskCache::sDefaultInstance;


/**
 * @brief Constructs an empty cache and zeroes the statistics counters.
 */
AlphaMaskCache::AlphaMaskCache()
	:
	fLock("AlphaMask cache"),
	fCurrentCacheBytes(0),
	fTooLargeMaskCount(0),
	fMasksReplacedCount(0),
	fHitCount(0),
	fMissCount(0),
	fLowerMaskReferencedCount(0)
{
}


/**
 * @brief Empties the cache and releases every retained mask.
 */
AlphaMaskCache::~AlphaMaskCache()
{
	Clear();
}


/**
 * @brief Returns the process-global default cache instance.
 *
 * @return Pointer to the singleton; never NULL.
 */
/* static */ AlphaMaskCache*
AlphaMaskCache::Default()
{
	return &sDefaultInstance;
}


/**
 * @brief Inserts @a mask into the cache, evicting older entries as needed.
 *
 * Computes the bitmap footprint of @a mask plus any of its previous-mask
 * chain that is not already cached, evicts entries in iteration order until
 * either enough room is available or eviction can make no further progress,
 * and finally adopts a reference on the supplied mask.
 *
 * @param mask Mask to insert. Must outlive the call.
 * @retval B_OK         Mask was inserted.
 * @retval B_NO_MEMORY  Mask plus its chain exceeded @c kMaxCacheBytes even
 *                      after attempted eviction.
 */
status_t
AlphaMaskCache::Put(ShapeAlphaMask* mask)
{
	AutoLocker<BLocker> locker(fLock);

	size_t maskStackSize = mask->BitmapSize();
	maskStackSize += _FindUncachedPreviousMasks(mask, true);

	if (maskStackSize > kMaxCacheBytes) {
		_FindUncachedPreviousMasks(mask, false);
		fTooLargeMaskCount++;
		return B_NO_MEMORY;
	}

	if (fCurrentCacheBytes + maskStackSize > kMaxCacheBytes) {
		for (ShapeMaskSet::iterator it = fShapeMasks.begin();
			it != fShapeMasks.end();) {

			if (atomic_get(&it->fMask->fNextMaskCount) > 0) {
				it++;
				continue;
			}

			size_t removedMaskStackSize = it->fMask->BitmapSize();
			removedMaskStackSize += _FindUncachedPreviousMasks(it->fMask,
				false);
			fCurrentCacheBytes -= removedMaskStackSize;

			it->fMask->fInCache = false;
			it->fMask->ReleaseReference();
			fMasksReplacedCount++;
			fShapeMasks.erase(it++);

			if (fCurrentCacheBytes + maskStackSize <= kMaxCacheBytes)
				break;
		}
	}

	if (fCurrentCacheBytes + maskStackSize > kMaxCacheBytes) {
		_FindUncachedPreviousMasks(mask, false);
		fTooLargeMaskCount++;
		return B_NO_MEMORY;
	}

	fCurrentCacheBytes += maskStackSize;

	ShapeMaskElement element(mask->fShape, mask, mask->fPreviousMask.Get(),
		mask->fInverse);
	fShapeMasks.insert(element);
	mask->AcquireReference();
	mask->fInCache = true;
	return B_OK;
}


/**
 * @brief Retrieves a cached mask matching the supplied parameters.
 *
 * @param shape         Shape that the mask was rendered from.
 * @param previousMask  Lower mask in the mask stack (may be NULL).
 * @param inverse       Whether the lookup wants an inverted mask.
 * @return Reference-counted ShapeAlphaMask on hit, or NULL on miss.
 *         The caller must release the reference when done.
 */
ShapeAlphaMask*
AlphaMaskCache::Get(const shape_data& shape, AlphaMask* previousMask,
	bool inverse)
{
	AutoLocker<BLocker> locker(fLock);

#ifdef PRINT_ALPHA_MASK_CACHE_STATISTICS
	if (sAlphaMaskGetCount++ > 200) {
		_PrintAndResetStatistics();
		sAlphaMaskGetCount = 0;
	}
#endif

	ShapeMaskElement element(&shape, NULL, previousMask, inverse);
	ShapeMaskSet::iterator it = fShapeMasks.find(element);
 	if (it == fShapeMasks.end()) {
		fMissCount++;
		return NULL;
 	}
	fHitCount++;
	it->fMask->AcquireReference();
	return it->fMask;
}


/**
 * @brief Drops every cached entry and resets the statistics counters.
 */
void
AlphaMaskCache::Clear()
{
	AutoLocker<BLocker> locker(fLock);

	for (ShapeMaskSet::iterator it = fShapeMasks.begin();
		it != fShapeMasks.end(); it++) {
		it->fMask->fInCache = false;
		it->fMask->fIndirectCacheReferences = 0;
		it->fMask->ReleaseReference();
	}
	fShapeMasks.clear();
	fTooLargeMaskCount = 0;
	fMasksReplacedCount = 0;
	fHitCount = 0;
	fMissCount = 0;
	fLowerMaskReferencedCount = 0;
}


/**
 * @brief Walks the previous-mask chain, adjusting indirect reference counts.
 *
 * Each previous mask that is not itself cached needs to be kept alive as long
 * as some cached mask references it. This helper bumps or drops the indirect
 * reference count along @a mask's chain and reports the size impact on the
 * cache budget.
 *
 * @param mask      Top of the mask chain to walk.
 * @param reference True to add references, false to remove them.
 * @return  Total bitmap footprint that crossed the zero-references boundary
 *          as a result of the walk; positive when entries were referenced
 *          for the first time, negative-on-the-budget-side when released.
 */
size_t
AlphaMaskCache::_FindUncachedPreviousMasks(AlphaMask* mask, bool reference)
{
	const int32 referenceModifier = reference ? 1 : -1;
	size_t addedOrRemovedSize = 0;

	for (AlphaMask* lowerMask = mask->fPreviousMask.Get(); lowerMask != NULL;
		lowerMask = lowerMask->fPreviousMask.Get()) {
		if (lowerMask->fInCache)
			continue;
		uint32 oldReferences = lowerMask->fIndirectCacheReferences;
		lowerMask->fIndirectCacheReferences += referenceModifier;
		if (lowerMask->fIndirectCacheReferences == 0 || oldReferences == 0) {
			// We either newly referenced the mask for the first time, or
			// released the last reference
			addedOrRemovedSize += lowerMask->BitmapSize();
			fLowerMaskReferencedCount += referenceModifier;
		}
	}

	return addedOrRemovedSize;
}


/**
 * @brief Dumps cache statistics through @c debug_printf and resets them.
 *
 * Only invoked when @c PRINT_ALPHA_MASK_CACHE_STATISTICS is defined.
 */
void
AlphaMaskCache::_PrintAndResetStatistics()
{
	debug_printf("AlphaMaskCache statistics: size=%" B_PRIuSIZE " bytes=%"
		B_PRIuSIZE " lower=%4" B_PRIu32 " total=%" B_PRIuSIZE " too_large=%4"
		B_PRIu32 " replaced=%4" B_PRIu32 " hit=%4" B_PRIu32 " miss=%4" B_PRIu32
		"\n", fShapeMasks.size(), fCurrentCacheBytes, fLowerMaskReferencedCount,
		fShapeMasks.size() + fLowerMaskReferencedCount, fTooLargeMaskCount,
		fMasksReplacedCount, fHitCount, fMissCount);
	fTooLargeMaskCount = 0;
	fMasksReplacedCount = 0;
	fHitCount = 0;
	fMissCount = 0;
}
