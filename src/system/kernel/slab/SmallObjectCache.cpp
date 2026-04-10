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
 * @brief Slab object cache for small objects with embedded slab metadata.
 *
 * For objects small enough that the slab control structure fits in the same
 * memory page as the objects themselves. Offers better cache locality than
 * HashedObjectCache for small allocations.
 *
 * @see HashedObjectCache.cpp, ObjectCache.cpp
 */


#include "SmallObjectCache.h"

#include <BytePointer.h>
#include "MemoryManager.h"
#include "slab_private.h"


RANGE_MARKER_FUNCTION_BEGIN(SlabSmallObjectCache)


/**
 * @brief Return a pointer to the @c slab descriptor embedded at the end of a
 *        memory page group.
 *
 * The @c slab struct is placed in the last @c sizeof(slab) bytes of the
 * page chunk, immediately after the usable object storage. Using a
 * @c BytePointer avoids strict-aliasing violations.
 *
 * @param pages      Base address of the page chunk.
 * @param slab_size  Total size of the page chunk in bytes.
 * @return           Pointer to the @c slab descriptor at
 *                   @c pages + slab_size - sizeof(slab).
 */
static inline slab *
slab_in_pages(void *pages, size_t slab_size)
{
	BytePointer<slab> pointer(pages);
	pointer += slab_size - sizeof(slab);
	return &pointer;
}


/**
 * @brief Factory method — allocate and fully initialise a @c SmallObjectCache.
 *
 * Allocates the cache struct from the internal slab allocator using placement
 * new, then calls @c ObjectCache::Init() to set up the depot and common
 * fields. The slab size defaults to @c SLAB_CHUNK_SIZE_SMALL or, when
 * @c CACHE_LARGE_SLAB is set, to 1024x the object size; in both cases the
 * value is rounded to an acceptable chunk size by the memory manager.
 *
 * @param name              Human-readable cache name.
 * @param object_size       Size of each managed object in bytes.
 * @param alignment         Required object alignment in bytes.
 * @param maximum           Byte limit on total cache memory, or 0 for none.
 * @param magazineCapacity  Objects per depot magazine; 0 for automatic sizing.
 * @param maxMagazineCount  Maximum full magazines in the depot; 0 for automatic.
 * @param flags             Cache creation flags (e.g. @c CACHE_LARGE_SLAB,
 *                          @c CACHE_NO_DEPOT, @c CACHE_DURING_BOOT).
 * @param cookie            Opaque value forwarded to @p constructor and
 *                          @p destructor.
 * @param constructor       Per-object constructor callback; may be @c NULL.
 * @param destructor        Per-object destructor callback; may be @c NULL.
 * @param reclaimer         Memory-pressure reclaim callback; may be @c NULL.
 * @return                  Pointer to the initialised cache, or @c NULL if
 *                          allocation or initialisation failed.
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
 * @brief Destroy and free this @c SmallObjectCache.
 *
 * Explicitly invokes the destructor to release depot and mutex resources,
 * then returns the storage to the internal slab allocator.
 */
void
SmallObjectCache::Delete()
{
	this->~SmallObjectCache();
	slab_internal_free(this, 0);
}


/**
 * @brief Allocate a new slab backed by a contiguous page chunk.
 *
 * Checks the cache quota, drops the cache lock while asking the memory manager
 * for a fresh chunk, then locates the embedded @c slab descriptor at the end
 * of the chunk. If allocation tracking is enabled, allocates tracking info
 * before calling @c InitSlab(). All partially-completed work is rolled back on
 * failure.
 *
 * @param flags  Allocation flags controlling memory-pressure behaviour.
 * @return       Pointer to the initialised @c slab, or @c NULL on failure.
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
 * @brief Return a fully-empty slab's pages to the memory manager.
 *
 * Uninitialises the slab (running all object destructors), drops the cache
 * lock, frees the tracking info and backing pages, then re-acquires the lock.
 *
 * @param slab   Pointer to the @c slab to return; must be completely empty.
 * @param flags  Deallocation flags forwarded to @c MemoryManager::Free().
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
 * @brief Locate the @c slab that contains @p object.
 *
 * Computes the page-aligned base address of the chunk containing @p object
 * using @c lower_boundary(), then returns the embedded @c slab descriptor
 * at the end of that chunk via @c slab_in_pages().
 *
 * @param object  Pointer to an object managed by this cache.
 * @return        Pointer to the owning @c slab descriptor.
 */
slab*
SmallObjectCache::ObjectSlab(void* object) const
{
	return slab_in_pages(lower_boundary(object, slab_size), slab_size);
}


RANGE_MARKER_FUNCTION_END(SlabSmallObjectCache)
