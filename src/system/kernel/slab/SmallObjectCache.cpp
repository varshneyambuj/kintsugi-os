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
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file SmallObjectCache.cpp
 * @brief Slab object-cache specialisation for objects much smaller than a
 *        page.
 *
 * SmallObjectCache is a strategy-pattern override of ObjectCache that places
 * the slab descriptor *inside* the slab's backing page chunk — specifically,
 * in the last sizeof(slab) bytes of the chunk, right after the usable object
 * storage. Because the slab metadata shares the chunk with the objects it
 * manages, ObjectSlab() can recover the owning slab from a free()-style
 * pointer by masking it down to its chunk boundary; no auxiliary hash table
 * is required (compare HashedObjectCache, which is used when the objects are
 * too large to share a chunk with their descriptor).
 *
 * Only the per-slab lifecycle hooks that differ from the base ObjectCache
 * live here — the common depot, magazine and quota machinery is inherited.
 *
 * @see HashedObjectCache.cpp, ObjectCache.cpp
 */


#include "SmallObjectCache.h"

#include <BytePointer.h>
#include "MemoryManager.h"
#include "slab_private.h"


RANGE_MARKER_FUNCTION_BEGIN(SlabSmallObjectCache)


/**
 * @brief Return the slab descriptor embedded at the tail of a chunk.
 *
 * The descriptor is placed in the last sizeof(slab) bytes of the chunk so
 * that its address is easily recovered from any object pointer via a mask
 * down to the chunk boundary. Using BytePointer avoids strict-aliasing
 * violations when stepping by raw byte counts.
 *
 * @param pages      Base address of the page chunk.
 * @param slab_size  Total size of the page chunk in bytes.
 * @return Pointer to the slab descriptor at pages + slab_size - sizeof(slab).
 */
static inline slab *
slab_in_pages(void *pages, size_t slab_size)
{
	BytePointer<slab> pointer(pages);
	pointer += slab_size - sizeof(slab);
	return &pointer;
}


/**
 * @brief Factory entry point — allocate and fully initialise a
 *        SmallObjectCache.
 *
 * Obtains raw storage from the internal slab allocator, placement-constructs
 * the cache, and delegates the shared setup (depot, magazines, callbacks,
 * quota) to ObjectCache::Init(). Finally, chooses a slab chunk size: the
 * default SLAB_CHUNK_SIZE_SMALL, or 1024 * object_size when
 * CACHE_LARGE_SLAB is requested. In either case the chosen size is rounded
 * up to a chunk granularity the memory manager actually accepts.
 *
 * @param name              Human-readable cache name.
 * @param object_size       Size of each managed object in bytes.
 * @param alignment         Required object alignment in bytes.
 * @param maximum           Byte ceiling on total cache memory, or 0 for none.
 * @param magazineCapacity  Objects per depot magazine; 0 for automatic sizing.
 * @param maxMagazineCount  Maximum full magazines kept in the depot; 0 auto.
 * @param flags             Cache flags (CACHE_LARGE_SLAB, CACHE_NO_DEPOT,
 *                          CACHE_DURING_BOOT, etc.).
 * @param cookie            Opaque value forwarded to the ctor/dtor callbacks.
 * @param constructor       Per-object constructor; may be NULL.
 * @param destructor        Per-object destructor; may be NULL.
 * @param reclaimer         Memory-pressure reclaim callback; may be NULL.
 * @return The initialised cache, or NULL on allocation or Init() failure.
 */
/*static*/ SmallObjectCache*
SmallObjectCache::Create(const char* name, size_t object_size,
	size_t alignment, size_t maximum, size_t magazineCapacity,
	size_t maxMagazineCount, uint32 flags, void* cookie,
	object_cache_constructor constructor, object_cache_destructor destructor,
	object_cache_reclaimer reclaimer)
{
	void* buffer = slab_internal_alloc(sizeof(SmallObjectCache), flags);
	if (buffer == NULL)
		return NULL;

	SmallObjectCache* cache = new(buffer) SmallObjectCache();

	if (cache->Init(name, object_size, alignment, maximum, magazineCapacity,
			maxMagazineCount, flags, cookie, constructor, destructor,
			reclaimer) != B_OK) {
		cache->Delete();
		return NULL;
	}

	if ((flags & CACHE_LARGE_SLAB) != 0)
		cache->slab_size = 1024 * object_size;
	else
		cache->slab_size = SLAB_CHUNK_SIZE_SMALL;

	cache->slab_size = MemoryManager::AcceptableChunkSize(cache->slab_size);

	return cache;
}


/**
 * @brief Destroy this cache and return its storage to the slab allocator.
 *
 * Runs the destructor explicitly so the depot, mutex, and any other members
 * release their resources, then frees the raw buffer obtained from
 * slab_internal_alloc() at construction time.
 */
void
SmallObjectCache::Delete()
{
	this->~SmallObjectCache();
	slab_internal_free(this, 0);
}


/**
 * @brief Allocate a fresh slab backed by one contiguous page chunk.
 *
 * Honours the cache's byte quota, drops the cache lock across the
 * potentially-blocking MemoryManager::Allocate() call, and on return locates
 * the embedded slab descriptor at the end of the chunk. Allocates any
 * tracking metadata required by kernel debugging before handing the slab to
 * InitSlab(), which populates the object free list and bookkeeping. Failures
 * roll back the partial state — the chunk is returned to the memory manager
 * and NULL is reported to the caller.
 *
 * @param flags Allocation flags controlling memory-pressure behaviour.
 * @return Pointer to the new slab, or NULL if quota or memory is exhausted.
 */
slab*
SmallObjectCache::CreateSlab(uint32 flags)
{
	if (!check_cache_quota(this))
		return NULL;

	void* pages;

	Unlock();
	status_t error = MemoryManager::Allocate(this, flags, pages);
	Lock();

	if (error != B_OK)
		return NULL;

	slab* newSlab = slab_in_pages(pages, slab_size);
	size_t byteCount = slab_size - sizeof(slab);
	if (AllocateTrackingInfos(newSlab, byteCount, flags) != B_OK) {
		MemoryManager::Free(pages, flags);
		return NULL;
	}

	return InitSlab(newSlab, pages, byteCount, flags);
}


/**
 * @brief Return a fully-empty slab's backing pages to the memory manager.
 *
 * Invokes UninitSlab() to run any per-object destructors and tear down the
 * free list, drops the cache lock across the blocking free path, releases
 * tracking info and the page chunk, and re-acquires the cache lock before
 * returning. The caller guarantees the slab has no outstanding objects.
 *
 * @param slab  The slab to release; must be completely empty.
 * @param flags Deallocation flags forwarded to MemoryManager::Free().
 */
void
SmallObjectCache::ReturnSlab(slab* slab, uint32 flags)
{
	UninitSlab(slab);

	Unlock();
	FreeTrackingInfos(slab, flags);
	MemoryManager::Free(slab->pages, flags);
	Lock();
}


/**
 * @brief Recover the owning slab for an object allocated from this cache.
 *
 * Because the slab descriptor lives in the last sizeof(slab) bytes of the
 * chunk, it can be found by rounding an object pointer down to its chunk
 * boundary (lower_boundary()) and indexing to the tail via slab_in_pages().
 * This is the key property that gives SmallObjectCache its O(1) free path
 * without needing a hash lookup.
 *
 * @param object Pointer to an object managed by this cache.
 * @return Pointer to the slab descriptor that owns @p object.
 */
slab*
SmallObjectCache::ObjectSlab(void* object) const
{
	return slab_in_pages(lower_boundary(object, slab_size), slab_size);
}


RANGE_MARKER_FUNCTION_END(SlabSmallObjectCache)
