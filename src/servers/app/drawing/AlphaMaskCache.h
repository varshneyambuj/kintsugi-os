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
 * MIT License. Copyright 2015, Haiku.
 * Original authors: Julian Harnath.
 */

/** @file AlphaMaskCache.h
    @brief Process-global cache mapping shape descriptions to their rendered alpha masks. */

#ifndef ALPHA_MASK_CACHE_H
#define ALPHA_MASK_CACHE_H

#include <set>

#include "ShapePrivate.h"
#include <Locker.h>
#include <kernel/OS.h>


class AlphaMask;
class ShapeAlphaMask;


/** @brief Bounded cache of ShapeAlphaMask bitmaps keyed by shape, previous mask, and inversion.
 *
 * Drawing a shape-clipped region is expensive: each mask requires rasterising
 * the shape into an 8-bit bitmap. AlphaMaskCache memoises recently produced
 * masks so that repeated draws using the same clipping shape (a common case
 * for round-rect window backgrounds) reuse the same bitmap. The cache is
 * size-bounded; oldest unreferenced entries are evicted when needed.
 */
class AlphaMaskCache {
private:
	enum {
		/** @brief Soft cap on total cached mask bitmap size in bytes. */
		kMaxCacheBytes = 8 * 1024 * 1024 // 8 MiB
	};

public:
	/** @brief Constructs an empty cache. */
								AlphaMaskCache();
	/** @brief Empties the cache and releases all retained masks. */
								~AlphaMaskCache();

	/** @brief Returns the process-global default cache instance. */
	static	AlphaMaskCache*		Default();

	/** @brief Inserts @a mask into the cache, evicting older entries if needed.
	 *  @return B_OK on success, B_NO_MEMORY when the mask plus its previous-mask
	 *          chain would exceed @c kMaxCacheBytes even after eviction. */
			status_t			Put(ShapeAlphaMask* mask);

	/** @brief Looks up a cached mask matching the supplied parameters.
	 *  @param shape         Shape used to compose the mask.
	 *  @param previousMask  Lower mask in the mask stack, or NULL.
	 *  @param inverse       Whether the lookup wants an inverted version.
	 *  @return  A new reference to the cached mask, or NULL on miss. */
			ShapeAlphaMask*		Get(const shape_data& shape,
									AlphaMask* previousMask,
									bool inverse);

	/** @brief Drops every entry and resets statistics counters. */
			void				Clear();

private:
			size_t				_FindUncachedPreviousMasks(AlphaMask* mask,
									bool reference);
			void				_PrintAndResetStatistics();

private:
	/** @brief Cache key/value pair: shape + inversion + previous mask, mapped to its rendered mask. */
	struct ShapeMaskElement {
		ShapeMaskElement(const shape_data* shape,
			ShapeAlphaMask* mask, AlphaMask* previousMask,
			bool inverse)
			:
			fShape(shape),
			fInverse(inverse),
			fMask(mask),
			fPreviousMask(previousMask)
		{
		}

		bool operator<(const ShapeMaskElement& other) const
		{
			if (fInverse != other.fInverse)
				return fInverse < other.fInverse;
			if (fPreviousMask != other.fPreviousMask)
				return fPreviousMask < other.fPreviousMask;

			// compare shapes
			if (fShape->ptCount != other.fShape->ptCount)
				return fShape->ptCount < other.fShape->ptCount;
			if (fShape->opCount != other.fShape->opCount)
				return fShape->opCount < other.fShape->opCount;
			int diff = memcmp(fShape->ptList, other.fShape->ptList,
				fShape->ptSize);
			if (diff != 0)
				return diff < 0;
			diff = memcmp(fShape->opList, other.fShape->opList,
				fShape->opSize);
			if (diff != 0)
				return diff < 0;

			// equal
			return false;
		}

		const shape_data*	fShape;
		bool				fInverse;
		ShapeAlphaMask*		fMask;
		AlphaMask*			fPreviousMask;
	};

private:
	typedef std::set<ShapeMaskElement> ShapeMaskSet;

	static	AlphaMaskCache		sDefaultInstance;

			BLocker				fLock;

			size_t				fCurrentCacheBytes;
			ShapeMaskSet		fShapeMasks;

			// Statistics counters
			uint32				fTooLargeMaskCount;
			uint32				fMasksReplacedCount;
			uint32				fHitCount;
			uint32				fMissCount;
			uint32				fLowerMaskReferencedCount;
};


#endif // ALPHA_MASK_CACHE_H
