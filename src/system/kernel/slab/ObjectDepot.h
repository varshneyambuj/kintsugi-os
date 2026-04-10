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
 *   Copyright 2010, Axel Dörfler. All Rights Reserved.
 *   Copyright 2007, Hugo Santos. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file ObjectDepot.h
 *  @brief Per-CPU magazine depot used as the slab allocator's lock-free fast path. */

#ifndef _SLAB_OBJECT_DEPOT_H_
#define _SLAB_OBJECT_DEPOT_H_


#include <lock.h>
#include <KernelExport.h>

#include "slab_queue.h"


struct DepotMagazine;

/** @brief Magazine-based per-CPU object depot.
 *
 * Each CPU keeps a small private magazine of free objects so the common
 * allocate/free path needs no locking. When a CPU's magazine fills or
 * empties, it swaps the magazine with the global @c full or @c empty list,
 * which is protected by a short critical section. */
typedef struct object_depot {
	rw_lock					outer_lock;          /**< Reader/writer lock guarding the global lists. */
	spinlock				inner_lock;          /**< Spinlock for fast operations on the global lists. */
	slab_queue				full;                /**< Magazines that are completely full. */
	slab_queue				empty;               /**< Magazines that are completely empty. */
	size_t					full_count;          /**< Number of magazines on the @c full list. */
	size_t					empty_count;         /**< Number of magazines on the @c empty list. */
	size_t					max_count;           /**< Maximum number of magazines kept overall. */
	size_t					magazine_capacity;   /**< Object capacity of each magazine. */
	struct depot_cpu_store*	stores;              /**< Per-CPU storage of currently-loaded magazines. */
	void*					cookie;              /**< Opaque cookie passed to @c return_object. */

	/** @brief Hook invoked when objects must be returned past the depot. */
	void (*return_object)(struct object_depot* depot, void* cookie,
		void* object, uint32 flags);
} object_depot;


#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialises @p depot.
 *  @param capacity     Object capacity of each magazine.
 *  @param maxCount     Maximum total number of magazines.
 *  @param flags        Allocation flags forwarded to internal allocations.
 *  @param cookie       Opaque cookie passed to @p returnObject.
 *  @param returnObject Hook invoked when objects must be returned past the depot.
 *  @return B_OK on success, or an error code on failure. */
status_t object_depot_init(object_depot* depot, size_t capacity,
	size_t maxCount, uint32 flags, void* cookie,
	void (*returnObject)(object_depot* depot, void* cookie, void* object,
		uint32 flags));
/** @brief Releases all resources held by @p depot. */
void object_depot_destroy(object_depot* depot, uint32 flags);

/** @brief Obtains a free object from the depot, or NULL if it is empty. */
void* object_depot_obtain(object_depot* depot);
/** @brief Returns @p object to the depot for later reuse. */
void object_depot_store(object_depot* depot, void* object, uint32 flags);

/** @brief Drains every magazine in the depot, returning all objects to the cache. */
void object_depot_make_empty(object_depot* depot, uint32 flags);

#if PARANOID_KERNEL_FREE
/** @brief Returns true if any magazine in @p depot currently holds @p object. */
bool object_depot_contains_object(object_depot* depot, void* object);
#endif

#ifdef __cplusplus
}
#endif


#endif	/* _SLAB_OBJECT_DEPOT_H_ */
