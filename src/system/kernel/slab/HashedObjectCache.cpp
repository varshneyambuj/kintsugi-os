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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2008, Axel Dörfler. All Rights Reserved.
 *   Copyright 2007, Hugo Santos. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file HashedObjectCache.cpp
 * @brief Slab object cache for large objects using a hash table for slab lookup.
 *
 * Used when objects are too large to embed slab metadata in the slab itself.
 * A hash table maps object addresses back to their containing slab.
 *
 * @see SmallObjectCache.cpp, ObjectCache.cpp
 */


#include "HashedObjectCache.h"

#include "MemoryManager.h"
#include "slab_private.h"


RANGE_MARKER_FUNCTION_BEGIN(SlabHashedObjectCache)


/**
 * @brief Return the index of the highest set bit of a non-zero value.
 *
 * This is equivalent to @c floor(log2(value)) for positive integers.
 * Returns @c -1 when @p value is 0.
 *
 * @param value  Input value whose highest set bit index is sought.
 * @return       Zero-based bit index of the highest set bit, or @c -1 if
 *               @p value is 0.
 */
static inline int
__fls0(size_t value)
{
	if (value == 0)
		return -1;

	int bit;
	for (bit = 0; value != 1; bit++)
		value >>= 1;
	return bit;
}


/**
 * @brief Allocate a @c HashedSlab descriptor from the internal slab allocator.
 *
 * @param flags  Allocation flags forwarded to @c slab_internal_alloc().
 * @return       Pointer to the newly allocated @c HashedSlab, or @c NULL on
 *               failure.
 */
static HashedSlab*
allocate_slab(uint32 flags)
{
	return (HashedSlab*)slab_internal_alloc(sizeof(HashedSlab), flags);
}


/**
 * @brief Free a @c HashedSlab descriptor back to the internal slab allocator.
 *
 * @param slab   Pointer to the @c HashedSlab to free.
 * @param flags  Deallocation flags forwarded to @c slab_internal_free().
 */
static void
free_slab(HashedSlab* slab, uint32 flags)
{
	slab_internal_free(slab, flags);
}


// #pragma mark -


/**
 * @brief Construct a @c HashedObjectCache and initialise its hash table link.
 *
 * The hash table is not yet sized; call @c Create() which performs the full
 * two-phase construction including initial hash table allocation.
 */
HashedObjectCache::HashedObjectCache()
	:
	hash_table(this)
{
}


/**
 * @brief Factory method — allocate and fully initialise a @c HashedObjectCache.
 *
 * Allocates the cache struct and an initial hash table buffer from the
 * internal allocator, then calls @c ObjectCache::Init() to set up the depot
 * and common fields. The slab size is chosen as 8x the object size (or 128x
 * when @c CACHE_LARGE_SLAB is set) and rounded to an acceptable chunk size.
 *
 * @param name              Human-readable name stored in the cache (truncated
 *                          to @c CACHE_NAME_LENGTH).
 * @param object_size       Size of each object in bytes.
 * @param alignment         Required object alignment in bytes.
 * @param maximum           Maximum total memory the cache may consume, or 0
 *                          for unlimited.
 * @param magazineCapacity  Number of objects per magazine, or 0 for automatic.
 * @param maxMagazineCount  Maximum number of full magazines in the depot, or 0
 *                          for automatic.
 * @param flags             Cache creation flags (e.g. @c CACHE_LARGE_SLAB,
 *                          @c CACHE_NO_DEPOT).
 * @param cookie            Opaque value passed to @p constructor and
 *                          @p destructor.
 * @param constructor       Called for each object on slab creation; may be
 *                          @c NULL.
 * @param destructor        Called for each object on slab destruction; may be
 *                          @c NULL.
 * @param reclaimer         Called when the system is under memory pressure; may
 *                          be @c NULL.
 * @return                  Pointer to the initialised cache, or @c NULL if any
 *                          allocation step failed.
 */
/*static*/ HashedObjectCache*
HashedObjectCache::Create(const char* name, size_t object_size,
	size_t alignment, size_t maximum, size_t magazineCapacity,
	size_t maxMagazineCount, uint32 flags, void* cookie,
	object_cache_constructor constructor, object_cache_destructor destructor,
	object_cache_reclaimer reclaimer)
{
	void* buffer = slab_internal_alloc(sizeof(HashedObjectCache), flags);
	if (buffer == NULL)
		return NULL;

	HashedObjectCache* cache = new(buffer) HashedObjectCache();

	// init the hash table
	size_t hashSize = cache->hash_table.ResizeNeeded();
	buffer = slab_internal_alloc(hashSize, flags);
	if (buffer == NULL) {
		cache->Delete();
		return NULL;
	}

	cache->hash_table.Resize(buffer, hashSize, true);

	if (cache->Init(name, object_size, alignment, maximum, magazineCapacity,
			maxMagazineCount, flags, cookie, constructor, destructor,
			reclaimer) != B_OK) {
		cache->Delete();
		return NULL;
	}

	if ((flags & CACHE_LARGE_SLAB) != 0)
		cache->slab_size = 128 * object_size;
	else
		cache->slab_size = 8 * object_size;

	cache->slab_size = MemoryManager::AcceptableChunkSize(cache->slab_size);
	cache->lower_boundary = __fls0(cache->slab_size);

	return cache;
}


/**
 * @brief Destroy and free this @c HashedObjectCache.
 *
 * Calls the destructor explicitly (to release depot and lock resources) then
 * returns the storage to the internal slab allocator.
 */
void
HashedObjectCache::Delete()
{
	this->~HashedObjectCache();
	slab_internal_free(this, 0);
}


/**
 * @brief Allocate a new slab and register it in the hash table.
 *
 * Checks the cache quota, then drops the cache lock while allocating a
 * @c HashedSlab descriptor, backing pages, and tracking info. On success,
 * calls @c InitSlab() and inserts the slab into the hash table, resizing the
 * table if necessary. All partially-completed work is rolled back on failure.
 *
 * @param flags  Allocation flags controlling memory-pressure behaviour.
 * @return       Pointer to the initialised @c slab, or @c NULL on failure.
 */
slab*
HashedObjectCache::CreateSlab(uint32 flags)
{
	if (!check_cache_quota(this))
		return NULL;

	Unlock();

	HashedSlab* slab = allocate_slab(flags);
	if (slab != NULL) {
		void* pages = NULL;
		if (MemoryManager::Allocate(this, flags, pages) == B_OK
			&& AllocateTrackingInfos(slab, slab_size, flags) == B_OK) {
			Lock();
			if (InitSlab(slab, pages, slab_size, flags)) {
				hash_table.InsertUnchecked(slab);
				_ResizeHashTableIfNeeded(flags);
				return slab;
			}
			Unlock();
			FreeTrackingInfos(slab, flags);
		}

		if (pages != NULL)
			MemoryManager::Free(pages, flags);

		free_slab(slab, flags);
	}

	Lock();
	return NULL;
}


/**
 * @brief Return a fully-empty slab to the memory manager.
 *
 * Removes the slab from the hash table, uninitialises it (calling destructors
 * on all objects), then frees tracking info, backing pages, and the
 * @c HashedSlab descriptor. The hash table is resized if needed.
 *
 * @param _slab  Pointer to the @c slab to return; must be completely empty.
 * @param flags  Deallocation flags forwarded to @c MemoryManager::Free().
 */
void
HashedObjectCache::ReturnSlab(slab* _slab, uint32 flags)
{
	HashedSlab* slab = static_cast<HashedSlab*>(_slab);

	hash_table.RemoveUnchecked(slab);
	_ResizeHashTableIfNeeded(flags);

	UninitSlab(slab);

	Unlock();
	FreeTrackingInfos(slab, flags);
	MemoryManager::Free(slab->pages, flags);
	free_slab(slab, flags);
	Lock();
}


/**
 * @brief Look up the slab that contains @p object via the hash table.
 *
 * Uses @c lower_boundary() to find the page-aligned base address, then
 * queries the hash table. Panics if no matching slab is found, which indicates
 * a corrupted or invalid pointer.
 *
 * The cache lock must be held by the caller (@c ASSERT_LOCKED_MUTEX is used
 * to enforce this in debug builds).
 *
 * @param object  Pointer to an object managed by this cache.
 * @return        Pointer to the owning @c slab, or @c NULL after panic.
 */
slab*
HashedObjectCache::ObjectSlab(void* object) const
{
	ASSERT_LOCKED_MUTEX(&lock);

	HashedSlab* slab = hash_table.Lookup(::lower_boundary(object, slab_size));
	if (slab == NULL) {
		panic("hash object cache %p: unknown object %p", this, object);
		return NULL;
	}

	return slab;
}


/**
 * @brief Resize the hash table if the load factor warrants it.
 *
 * Drops the cache lock to allocate a new hash buffer, then re-acquires the
 * lock and checks whether a resize is still needed before committing. Any
 * displaced old buffer is freed with the lock dropped again to avoid
 * re-entrant lock issues.
 *
 * @param flags  Allocation flags used for the new hash buffer.
 */
void
HashedObjectCache::_ResizeHashTableIfNeeded(uint32 flags)
{
	size_t hashSize = hash_table.ResizeNeeded();
	if (hashSize == 0)
		return;

	Unlock();
	void* buffer = slab_internal_alloc(hashSize, flags);
	Lock();

	if (buffer == NULL)
		return;

	if (hash_table.ResizeNeeded() == hashSize) {
		void* oldHash = NULL;
		hash_table.Resize(buffer, hashSize, true, &oldHash);
		buffer = oldHash;
	}

	if (buffer != NULL) {
		Unlock();
		slab_internal_free(buffer, flags);
		Lock();
	}
}


RANGE_MARKER_FUNCTION_END(SlabHashedObjectCache)
