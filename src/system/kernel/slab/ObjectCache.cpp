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
 *   Copyright 2008-2010, Axel Dörfler. All Rights Reserved.
 *   Copyright 2007, Hugo Santos. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file ObjectCache.cpp
 * @brief Base slab object cache — initialization, depot management, and stats.
 *
 * ObjectCache is the abstract base for SmallObjectCache and HashedObjectCache.
 * Manages the per-CPU magazine depot (ObjectDepot) and provides the common
 * Init/Destroy/Alloc/Free paths shared by both cache types.
 *
 * @see SmallObjectCache.cpp, HashedObjectCache.cpp, ObjectDepot.cpp
 */


#include "ObjectCache.h"

#include <string.h>

#include <smp.h>
#include <util/AutoLock.h>
#include <vm/vm.h>
#include <vm/VMAddressSpace.h>

#include "MemoryManager.h"
#include "slab_private.h"


RANGE_MARKER_FUNCTION_BEGIN(SlabObjectCache)


/**
 * @brief Depot return-object callback that routes freed objects back to their slab.
 *
 * This function is registered with the @c ObjectDepot so that when a magazine
 * is flushed the depot can return each object to the correct slab without
 * knowing about the cache internals. The cache lock is acquired for the
 * duration of the call.
 *
 * @param depot   The @c object_depot that is flushing a magazine (unused
 *                directly; @p cookie identifies the owning cache).
 * @param cookie  Pointer to the owning @c ObjectCache, cast from @c void*.
 * @param object  The object being returned.
 * @param flags   Flags forwarded to @c ReturnObjectToSlab().
 */
static void
object_cache_return_object_wrapper(object_depot* depot, void* cookie,
	void* object, uint32 flags)
{
	ObjectCache* cache = (ObjectCache*)cookie;

	MutexLocker _(cache->lock);
	cache->ReturnObjectToSlab(cache->ObjectSlab(object), object, flags);
}


// #pragma mark -


/** @brief Trivial virtual destructor; resource teardown is in @c Init()'s inverse. */
ObjectCache::~ObjectCache()
{
}


/**
 * @brief Initialise common @c ObjectCache fields and the per-CPU depot.
 *
 * Copies the cache name, aligns @p objectSize to at least @p alignment and
 * @c kMinObjectAlignment, zeroes all counters, and — unless running on a
 * single CPU or @c CACHE_NO_DEPOT is set — initialises the @c ObjectDepot
 * with sensible magazine sizing defaults.
 *
 * @param name              Human-readable cache name (copied, truncated to fit).
 * @param objectSize        Requested per-object size in bytes; raised to
 *                          @c sizeof(slab_queue_link) if smaller.
 * @param alignment         Required object alignment; raised to
 *                          @c kMinObjectAlignment if smaller.
 * @param maximum           Byte limit on total cache memory, or 0 for none.
 * @param magazineCapacity  Objects per depot magazine; 0 selects an automatic
 *                          value based on @p objectSize.
 * @param maxMagazineCount  Maximum full magazines held in the depot; 0 selects
 *                          half of @p magazineCapacity.
 * @param flags             Cache creation flags (e.g. @c CACHE_NO_DEPOT,
 *                          @c CACHE_DURING_BOOT).
 * @param cookie            Opaque cookie forwarded to @p constructor and
 *                          @p destructor.
 * @param constructor       Per-object constructor callback; may be @c NULL.
 * @param destructor        Per-object destructor callback; may be @c NULL.
 * @param reclaimer         Memory-pressure reclaim callback; may be @c NULL.
 * @retval B_OK             Initialisation succeeded.
 * @retval (error)          Returned from @c object_depot_init() on depot
 *                          initialisation failure; the mutex is also destroyed
 *                          before returning.
 */
status_t
ObjectCache::Init(const char* name, size_t objectSize, size_t alignment,
	size_t maximum, size_t magazineCapacity, size_t maxMagazineCount,
	uint32 flags, void* cookie, object_cache_constructor constructor,
	object_cache_destructor destructor, object_cache_reclaimer reclaimer)
{
	strlcpy(this->name, name, sizeof(this->name));

	mutex_init(&lock, this->name);

	if (objectSize < sizeof(slab_queue_link))
		objectSize = sizeof(slab_queue_link);

	if (alignment < kMinObjectAlignment)
		alignment = kMinObjectAlignment;

	if (alignment > 0 && (objectSize & (alignment - 1)))
		object_size = objectSize + alignment - (objectSize & (alignment - 1));
	else
		object_size = objectSize;

	TRACE_CACHE(this, "init %lu, %lu -> %lu", objectSize, alignment,
		object_size);

	this->alignment = alignment;
	cache_color_cycle = 0;
	total_objects = 0;
	used_count = 0;
	empty_count = 0;
	pressure = 0;
	min_object_reserve = 0;

	maintenance_pending = false;
	maintenance_in_progress = false;
	maintenance_resize = false;
	maintenance_delete = false;

	usage = 0;
	this->maximum = maximum;

	this->flags = flags;

	resize_request = NULL;
	resize_entry_can_wait = NULL;
	resize_entry_dont_wait = NULL;

	// no gain in using the depot in single cpu setups
	if (smp_get_num_cpus() == 1)
		this->flags |= CACHE_NO_DEPOT;

	if (!(this->flags & CACHE_NO_DEPOT)) {
		// Determine usable magazine configuration values if none had been given
		if (magazineCapacity == 0) {
			magazineCapacity = objectSize < 256
				? 32 : (objectSize < 512 ? 16 : 8);
		}
		if (maxMagazineCount == 0)
			maxMagazineCount = magazineCapacity / 2;

		status_t status = object_depot_init(&depot, magazineCapacity,
			maxMagazineCount, flags, this, object_cache_return_object_wrapper);
		if (status != B_OK) {
			mutex_destroy(&lock);
			return status;
		}
	}

	this->cookie = cookie;
	this->constructor = constructor;
	this->destructor = destructor;
	this->reclaimer = reclaimer;

	return B_OK;
}


/**
 * @brief Initialise a freshly-allocated slab for this cache.
 *
 * Partitions @p pages into @c (byteCount / object_size) objects, advances
 * the colour cycle, runs the constructor for each object, fills each freed
 * block with the debug pattern via @c fill_freed_block(), and pushes the
 * per-object free links onto the slab's free list. If any constructor fails
 * the already-constructed objects are destructed and @c NULL is returned.
 *
 * @param slab       The @c slab descriptor to populate (pre-allocated by the
 *                   subclass).
 * @param pages      Pointer to the raw memory region backing this slab.
 * @param byteCount  Size of the backing region in bytes.
 * @param flags      Flags for internal use (passed through to paranoia checks).
 * @return           @p slab on success, or @c NULL if a constructor failed.
 */
slab*
ObjectCache::InitSlab(slab* slab, void* pages, size_t byteCount, uint32 flags)
{
	TRACE_CACHE(this, "construct (%p, %p .. %p, %lu)", slab, pages,
		((uint8*)pages) + byteCount, byteCount);

	slab->pages = pages;
	slab->count = slab->size = byteCount / object_size;
	slab->free.Init();

	size_t spareBytes = byteCount - (slab->size * object_size);

	slab->offset = cache_color_cycle;

	cache_color_cycle += alignment;
	if (cache_color_cycle > spareBytes)
		cache_color_cycle = 0;

	TRACE_CACHE(this, "  %lu objects, %lu spare bytes, offset %lu",
		slab->size, spareBytes, slab->offset);

	uint8* data = ((uint8*)pages) + slab->offset;

	CREATE_PARANOIA_CHECK_SET(slab, "slab");

	for (size_t i = 0; i < slab->size; i++) {
		status_t status = B_OK;
		if (constructor)
			status = constructor(cookie, data);

		if (status != B_OK) {
			data = ((uint8*)pages) + slab->offset;
			for (size_t j = 0; j < i; j++) {
				if (destructor)
					destructor(cookie, data);
				data += object_size;
			}

			DELETE_PARANOIA_CHECK_SET(slab);
			return NULL;
		}

		fill_freed_block(data, object_size);
		slab->free.Push(object_to_link(data, object_size));

		ADD_PARANOIA_CHECK(PARANOIA_SUSPICIOUS, slab,
			&object_to_link(data, object_size)->next, sizeof(void*));

		data += object_size;
	}

	return slab;
}


/**
 * @brief Tear down all objects in a slab and update cache-level counters.
 *
 * Calls the destructor for every object in the slab (whether or not they are
 * currently allocated — callers must ensure the slab is fully empty before
 * calling), decrements @c total_objects and @c usage, and removes the
 * paranoia check set. Panics if @c slab->count != @c slab->size.
 *
 * @param slab  The fully-empty @c slab to uninitialise.
 */
void
ObjectCache::UninitSlab(slab* slab)
{
	TRACE_CACHE(this, "destruct %p", slab);

	if (slab->count != slab->size)
		panic("cache: destroying a slab which isn't empty.");

	usage -= slab_size;
	total_objects -= slab->size;

	DELETE_PARANOIA_CHECK_SET(slab);

	uint8* data = ((uint8*)slab->pages) + slab->offset;

	for (size_t i = 0; i < slab->size; i++) {
		if (destructor)
			destructor(cookie, data);
		data += object_size;
	}
}


/**
 * @brief Return a single object to its slab and manage slab list membership.
 *
 * Validates that @p object falls within @p source's address range (in
 * @c KDEBUG >= 1 builds), pushes the free link, and moves the slab between
 * the @c partial / @c empty / (returned) states as appropriate:
 *   - Completely full slab that now has one free slot: moved to @c partial.
 *   - Completely empty slab: either moved to @c empty (if under pressure) or
 *     returned to the memory manager via @c ReturnSlab().
 *
 * @param source  The slab that owns @p object. Panics if @c NULL.
 * @param object  Pointer to the object being freed.
 * @param flags   Flags forwarded to @c ReturnSlab() when the slab is released.
 */
void
ObjectCache::ReturnObjectToSlab(slab* source, void* object, uint32 flags)
{
	if (source == NULL) {
		panic("object_cache: free'd object %p has no slab", object);
		return;
	}

	ParanoiaChecker _(source);

#if KDEBUG >= 1
	uint8* objectsStart = (uint8*)source->pages + source->offset;
	if (object < objectsStart
		|| object >= objectsStart + source->size * object_size
		|| ((uint8*)object - objectsStart) % object_size != 0) {
		panic("object_cache: tried to free invalid object pointer %p", object);
		return;
	}
#endif // KDEBUG

	slab_queue_link* link = object_to_link(object, object_size);

	TRACE_CACHE(this, "returning %p (%p) to %p, %lu used (%lu empty slabs).",
		object, link, source, source->size - source->count,
		empty_count);

	source->free.Push(link);
	source->count++;
	used_count--;

	ADD_PARANOIA_CHECK(PARANOIA_SUSPICIOUS, source, &link->next, sizeof(void*));

	if (source->count == source->size) {
		partial.Remove(source);

		if (empty_count < pressure
				|| (total_objects - (used_count + source->size))
					< min_object_reserve) {
			empty_count++;
			empty.Add(source);
		} else {
			ReturnSlab(source, flags);
		}
	} else if (source->count == 1) {
		full.Remove(source);
		partial.Add(source);
	}
}


/**
 * @brief Return a pointer to the object at position @p index within @p source.
 *
 * Computes the address arithmetically using the slab's page base, colour
 * offset, and the cache's @c object_size.
 *
 * @param source  The slab to index into.
 * @param index   Zero-based object index within the slab.
 * @return        Pointer to the object at @p index.
 */
void*
ObjectCache::ObjectAtIndex(slab* source, int32 index) const
{
	return (uint8*)source->pages + source->offset + index * object_size;
}


#if PARANOID_KERNEL_FREE

/**
 * @brief Assert that @p object has not already been freed (double-free check).
 *
 * Acquires the cache lock, locates @p object's slab, checks that the slab
 * belongs to either the @c partial or @c full list (not @c empty), and
 * verifies that the free-link chain does not already contain @p object.
 * Panics on any violation.
 *
 * Only compiled when @c PARANOID_KERNEL_FREE is defined.
 *
 * @param object  Pointer to the object to verify.
 * @retval true   The object is live (not on the free list).
 * @retval false  A violation was detected and @c panic() was called.
 */
bool
ObjectCache::AssertObjectNotFreed(void* object)
{
	MutexLocker locker(lock);

	slab* source = ObjectSlab(object);
	if (!partial.Contains(source) && !full.Contains(source)) {
		panic("object_cache: to be freed object %p: slab not part of cache!",
			object);
		return false;
	}

	slab_queue_link* link = object_to_link(object, object_size);
	for (slab_queue_link* freeLink = source->free.head; freeLink != NULL;
			freeLink = freeLink->next) {
		if (freeLink == link) {
			panic("object_cache: double free of %p (slab %p, cache %p)",
				object, source, this);
			return false;
		}
	}

	return true;
}

#endif // PARANOID_KERNEL_FREE


#if SLAB_OBJECT_CACHE_ALLOCATION_TRACKING

/**
 * @brief Allocate and zero-initialise allocation tracking info for a slab.
 *
 * Allocates one @c AllocationTrackingInfo entry per object in the slab from
 * raw memory and stores the pointer in @c slab->tracking.
 *
 * Only compiled when @c SLAB_OBJECT_CACHE_ALLOCATION_TRACKING is defined.
 *
 * @param slab       The slab for which tracking info is being allocated.
 * @param byteCount  Total byte count of the slab's object region; used to
 *                   derive @c objectCount = byteCount / object_size.
 * @param flags      Allocation flags forwarded to @c MemoryManager::AllocateRaw().
 * @retval B_OK      Tracking info allocated and zeroed successfully.
 * @retval (error)   Error code from @c MemoryManager::AllocateRaw().
 */
status_t
ObjectCache::AllocateTrackingInfos(slab* slab, size_t byteCount, uint32 flags)
{
	void* pages;
	size_t objectCount = byteCount / object_size;
	status_t result = MemoryManager::AllocateRaw(
		objectCount * sizeof(AllocationTrackingInfo), flags, pages);
	if (result == B_OK) {
		slab->tracking = (AllocationTrackingInfo*)pages;
		for (size_t i = 0; i < objectCount; i++)
			slab->tracking[i].Clear();
	}

	return result;
}


/**
 * @brief Free the allocation tracking info associated with a slab.
 *
 * Only compiled when @c SLAB_OBJECT_CACHE_ALLOCATION_TRACKING is defined.
 *
 * @param slab   The slab whose @c tracking pointer is to be freed.
 * @param flags  Deallocation flags forwarded to @c MemoryManager::FreeRawOrReturnCache().
 */
void
ObjectCache::FreeTrackingInfos(slab* slab, uint32 flags)
{
	MemoryManager::FreeRawOrReturnCache(slab->tracking, flags);
}


/**
 * @brief Return a pointer to the @c AllocationTrackingInfo for @p object.
 *
 * Locates the owning slab, then computes the index of @p object within the
 * slab to index into the tracking array.
 *
 * Only compiled when @c SLAB_OBJECT_CACHE_ALLOCATION_TRACKING is defined.
 *
 * @param object  Pointer to an object managed by this cache.
 * @return        Pointer to the corresponding @c AllocationTrackingInfo entry.
 */
AllocationTrackingInfo*
ObjectCache::TrackingInfoFor(void* object) const
{
	slab* objectSlab = ObjectSlab(object);
	return &objectSlab->tracking[((addr_t)object - objectSlab->offset
		- (addr_t)objectSlab->pages) / object_size];
}

#endif // SLAB_OBJECT_CACHE_ALLOCATION_TRACKING


RANGE_MARKER_FUNCTION_END(SlabObjectCache)
