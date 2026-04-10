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
 *   Copyright 2008-2010, Axel Dörfler. All Rights Reserved.
 *   Copyright 2007, Hugo Santos. All Rights Reserved.
 *
 *   Distributed under the terms of the MIT License.
 */

/** @file ObjectCache.h
 *  @brief Slab allocator core: object cache and slab descriptor types. */

#ifndef OBJECT_CACHE_H
#define OBJECT_CACHE_H


#include <condition_variable.h>
#include <lock.h>
#include <slab/Slab.h>
#include <util/DoublyLinkedList.h>

#include "ObjectDepot.h"
#include "slab_queue.h"

#include "kernel_debug_config.h"
#include "slab_debug.h"


struct ResizeRequest;


/** @brief One slab — a contiguous run of pages divided into fixed-size objects. */
struct slab : DoublyLinkedListLinkImpl<slab> {
	void*			pages;     /**< Base address of the slab's backing pages. */
	size_t			size;      /**< Total number of objects this slab can hold. */
	size_t			count;     /**< Number of objects currently free in this slab. */
	size_t			offset;    /**< Cache colour offset applied to the first object. */
	slab_queue		free;      /**< Free list of objects within this slab. */
#if SLAB_OBJECT_CACHE_ALLOCATION_TRACKING
	AllocationTrackingInfo*	tracking;  /**< Per-object allocation tracking metadata. */
#endif
};

typedef DoublyLinkedList<slab> SlabList;

/** @brief Records the state of a thread waiting for an in-progress cache resize. */
struct ObjectCacheResizeEntry {
	ConditionVariable	condition;  /**< Condition the waiter is blocked on. */
	thread_id			thread;     /**< Waiting thread for diagnostic dumps. */
};

/** @brief One slab cache: holds objects of a single fixed size for fast reuse.
 *
 * Each cache owns three lists of slabs (empty / partial / full) plus an
 * object depot of per-CPU magazines for lock-free fast paths. Allocations
 * walk the partial list first, fall through to the empty list, and finally
 * grow the cache by calling the subclass-provided CreateSlab(). */
struct ObjectCache : DoublyLinkedListLinkImpl<ObjectCache> {
			char				name[32];               /**< Debug name shown in slab dumps. */
			mutex				lock;                   /**< Mutex serialising slow-path operations. */
			size_t				object_size;            /**< Size of each object stored in this cache. */
			size_t				alignment;              /**< Required alignment for returned objects. */
			size_t				cache_color_cycle;      /**< Rotating cache-colour offset, applied to new slabs. */
			SlabList			empty;                  /**< Slabs that hold no live objects. */
			SlabList			partial;                /**< Slabs with both free and used objects. */
			SlabList			full;                   /**< Slabs with no free objects. */
			size_t				total_objects;          /**< Total objects across every slab in this cache. */
			size_t				used_count;             /**< Currently allocated (in-use) objects. */
			size_t				empty_count;            /**< Number of slabs on the @c empty list. */
			size_t				pressure;               /**< Memory-pressure score guiding maintenance decisions. */
			size_t				min_object_reserve;     /**< Minimum number of free objects to keep around. */

			size_t				slab_size;              /**< Bytes consumed per slab (header + objects). */
			size_t				usage;                  /**< Total bytes currently allocated to this cache. */
			size_t				maximum;                /**< Soft byte cap; 0 means unlimited. */
			uint32				flags;                  /**< Cache flags (see <slab/Slab.h>). */

			ResizeRequest*		resize_request;         /**< In-flight resize request, or NULL. */

			ObjectCacheResizeEntry* resize_entry_can_wait;  /**< Waiter list for callers willing to block. */
			ObjectCacheResizeEntry* resize_entry_dont_wait; /**< Waiter list for non-blocking callers. */

			DoublyLinkedListLink<ObjectCache> maintenance_link;  /**< Link in the global maintenance queue. */
			bool				maintenance_pending;     /**< True while queued for maintenance. */
			bool				maintenance_in_progress; /**< True while maintenance is running. */
			bool				maintenance_resize;      /**< True if maintenance should resize the cache. */
			bool				maintenance_delete;      /**< True if maintenance should delete the cache. */

			void*				cookie;                  /**< Opaque cookie passed to constructor / destructor. */
			object_cache_constructor constructor;        /**< Optional per-object constructor. */
			object_cache_destructor destructor;          /**< Optional per-object destructor. */
			object_cache_reclaimer reclaimer;            /**< Optional per-cache reclamation hook. */

			object_depot		depot;                   /**< Per-CPU magazine depot for the fast path. */

public:
	virtual						~ObjectCache();

	/** @brief Initialises the cache with the given object geometry and hooks.
	 *  @param name             Debug name (truncated to 31 chars).
	 *  @param objectSize       Size of each object in bytes.
	 *  @param alignment        Required alignment of returned objects.
	 *  @param maximum          Soft byte cap; 0 for unlimited.
	 *  @param magazineCapacity Per-magazine capacity for the depot.
	 *  @param maxMagazineCount Maximum number of magazines kept in the depot.
	 *  @param flags            Cache flags (see <slab/Slab.h>).
	 *  @param cookie           Opaque cookie passed to the per-object hooks.
	 *  @param constructor      Optional per-object constructor.
	 *  @param destructor       Optional per-object destructor.
	 *  @param reclaimer        Optional per-cache reclamation hook.
	 *  @return B_OK on success, or an error code on failure. */
			status_t			Init(const char* name, size_t objectSize,
									size_t alignment, size_t maximum,
									size_t magazineCapacity,
									size_t maxMagazineCount, uint32 flags,
									void* cookie,
									object_cache_constructor constructor,
									object_cache_destructor destructor,
									object_cache_reclaimer reclaimer);
	/** @brief Subclass hook: tear down and free the cache. */
	virtual	void				Delete() = 0;

	/** @brief Subclass hook: allocate and initialise a fresh slab. */
	virtual	slab*				CreateSlab(uint32 flags) = 0;
	/** @brief Subclass hook: free a slab back to the page allocator. */
	virtual	void				ReturnSlab(slab* slab, uint32 flags) = 0;
	/** @brief Subclass hook: locate the slab that owns @p object. */
	virtual slab*				ObjectSlab(void* object) const = 0;

	/** @brief Initialises a slab descriptor and lays out objects across @p pages. */
			slab*				InitSlab(slab* slab, void* pages,
									size_t byteCount, uint32 flags);
	/** @brief Tears down a slab descriptor before its memory is released. */
			void				UninitSlab(slab* slab);

	/** @brief Returns @p object to its owning slab and updates list memberships. */
			void				ReturnObjectToSlab(slab* source, void* object,
									uint32 flags);
	/** @brief Returns the @p index 'th object stored in @p source. */
			void*				ObjectAtIndex(slab* source, int32 index) const;

	/** @brief Acquires the cache lock; returns true on success. */
			bool				Lock()	{ return mutex_lock(&lock) == B_OK; }
	/** @brief Releases the cache lock. */
			void				Unlock()	{ mutex_unlock(&lock); }

	/** @brief Allocates the backing pages for a new slab. */
			status_t			AllocatePages(void** pages, uint32 flags);
	/** @brief Frees a slab's backing pages. */
			void				FreePages(void* pages);
	/** @brief Early-boot variant of AllocatePages() that bypasses the VM. */
			status_t			EarlyAllocatePages(void** pages, uint32 flags);
	/** @brief Early-boot variant of FreePages(). */
			void				EarlyFreePages(void* pages);

#if PARANOID_KERNEL_FREE
	/** @brief Returns true if @p object has not yet been freed.
	 *
	 * Used by the paranoid double-free detector to abort the kernel before
	 * the same object can be returned to the same cache twice. */
			bool				AssertObjectNotFreed(void* object);
#endif

	/** @brief Allocates per-object allocation tracking metadata for @p slab. */
			status_t			AllocateTrackingInfos(slab* slab,
									size_t byteCount, uint32 flags);
	/** @brief Frees per-object allocation tracking metadata for @p slab. */
			void				FreeTrackingInfos(slab* slab, uint32 flags);

#if SLAB_OBJECT_CACHE_ALLOCATION_TRACKING
	/** @brief Returns the tracking info record associated with @p object. */
			AllocationTrackingInfo*
								TrackingInfoFor(void* object) const;
#endif
};


static inline void*
link_to_object(slab_queue_link* link, size_t objectSize)
{
	return ((uint8*)link) - (objectSize - sizeof(slab_queue_link));
}


static inline slab_queue_link*
object_to_link(void* object, size_t objectSize)
{
	return (slab_queue_link*)(((uint8*)object)
		+ (objectSize - sizeof(slab_queue_link)));
}


static inline void*
lower_boundary(const void* object, size_t byteCount)
{
	return (void*)((addr_t)object & ~(byteCount - 1));
}


static inline bool
check_cache_quota(ObjectCache* cache)
{
	if (cache->maximum == 0)
		return true;

	return (cache->usage + cache->slab_size) <= cache->maximum;
}


#if !SLAB_OBJECT_CACHE_ALLOCATION_TRACKING

inline status_t
ObjectCache::AllocateTrackingInfos(slab* slab, size_t byteCount, uint32 flags)
{
	return B_OK;
}


inline void
ObjectCache::FreeTrackingInfos(slab* slab, uint32 flags)
{
}

#endif // !SLAB_OBJECT_CACHE_ALLOCATION_TRACKING

#endif	// OBJECT_CACHE_H
