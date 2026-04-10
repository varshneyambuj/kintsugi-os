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
 *   Copyright 2008, Axel Dörfler. All Rights Reserved.
 *   Copyright 2007, Hugo Santos. All Rights Reserved.
 *
 *   Distributed under the terms of the MIT License.
 */

/** @file SmallObjectCache.h
 *  @brief ObjectCache that embeds the slab descriptor at the end of each page run. */

#ifndef SMALL_OBJECT_CACHE_H
#define SMALL_OBJECT_CACHE_H


#include "ObjectCache.h"


/** @brief ObjectCache for small objects whose slab descriptor lives in-page.
 *
 * For objects much smaller than a page, the slab descriptor is placed at
 * the end of the slab's page run. ObjectSlab() can therefore derive the
 * owning slab directly from any object pointer by masking off its low bits,
 * avoiding the hash-table overhead used by HashedObjectCache. */
struct SmallObjectCache final : ObjectCache {
	/** @brief Allocates and initialises a SmallObjectCache. See ObjectCache::Init(). */
	static	SmallObjectCache*	Create(const char* name, size_t object_size,
									size_t alignment, size_t maximum,
									size_t magazineCapacity,
									size_t maxMagazineCount,
									uint32 flags, void* cookie,
									object_cache_constructor constructor,
									object_cache_destructor destructor,
									object_cache_reclaimer reclaimer);
	/** @brief Tears down the cache and frees the underlying memory. */
	virtual	void				Delete();

	/** @brief Allocates a fresh slab and embeds its descriptor in the page run. */
	virtual	slab*				CreateSlab(uint32 flags);
	/** @brief Frees @p slab back to the page allocator. */
	virtual	void				ReturnSlab(slab* slab, uint32 flags);
	/** @brief Returns the slab that owns @p object by masking off the page offset. */
	virtual slab*				ObjectSlab(void* object) const;
};


#endif	// SMALL_OBJECT_CACHE_H
