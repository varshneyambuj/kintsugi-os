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
 *   Copyright 2011, Michael Lotz <mmlr@mlotz.ch>.
 *   Copyright 2011, Ingo Weinhold <ingo_weinhold@gmx.de>.
 *
 *   Distributed under the terms of the MIT License.
 */

/** @file slab_debug.h
 *  @brief Debug-only macros and helpers for the slab allocator (tracing, paranoia fills, dumps). */

#ifndef SLAB_DEBUG_H
#define SLAB_DEBUG_H


#include <AllocationTracking.h>
#include <debug.h>
#include <slab/Slab.h>
#include <tracing.h>

#include "kernel_debug_config.h"


//#define TRACE_SLAB
#ifdef TRACE_SLAB
#define TRACE_CACHE(cache, format, args...) \
	dprintf("Cache[%p, %s] " format "\n", cache, cache->name , ##args)
#else
#define TRACE_CACHE(cache, format, bananas...) do { } while (0)
#endif


#define COMPONENT_PARANOIA_LEVEL	OBJECT_CACHE_PARANOIA
#include <debug_paranoia.h>


// Macros determining whether allocation tracking is actually available.
#define SLAB_OBJECT_CACHE_ALLOCATION_TRACKING (SLAB_ALLOCATION_TRACKING != 0 \
	&& SLAB_OBJECT_CACHE_TRACING != 0 \
	&& SLAB_OBJECT_CACHE_TRACING_STACK_TRACE > 0)
	// The object cache code needs to do allocation tracking.
#define SLAB_MEMORY_MANAGER_ALLOCATION_TRACKING (SLAB_ALLOCATION_TRACKING != 0 \
	&& SLAB_MEMORY_MANAGER_TRACING != 0 \
	&& SLAB_MEMORY_MANAGER_TRACING_STACK_TRACE > 0)
	// The memory manager code needs to do allocation tracking.
#define SLAB_ALLOCATION_TRACKING_AVAILABLE \
	(SLAB_OBJECT_CACHE_ALLOCATION_TRACKING \
		|| SLAB_MEMORY_MANAGER_ALLOCATION_TRACKING)
	// Guards code that is needed for either object cache or memory manager
	// allocation tracking.


struct object_depot;


#if SLAB_ALLOCATION_TRACKING_AVAILABLE

namespace BKernel {

class AllocationTrackingCallback {
public:
	virtual						~AllocationTrackingCallback();

	virtual	bool				ProcessTrackingInfo(
									AllocationTrackingInfo* info,
									void* allocation,
									size_t allocationSize) = 0;
};

}

using BKernel::AllocationTrackingCallback;

#endif // SLAB_ALLOCATION_TRACKING_AVAILABLE


void		dump_object_depot(object_depot* depot);
int			dump_object_depot(int argCount, char** args);
int			dump_depot_magazine(int argCount, char** args);


#if PARANOID_KERNEL_MALLOC || PARANOID_KERNEL_FREE
static inline void*
fill_block(void* buffer, size_t size, uint32 pattern)
{
	if (buffer == NULL)
		return NULL;

	size &= ~(sizeof(pattern) - 1);
	for (size_t i = 0; i < size / sizeof(pattern); i++)
		((uint32*)buffer)[i] = pattern;

	return buffer;
}
#endif


static inline void*
fill_allocated_block(void* buffer, size_t size)
{
#if PARANOID_KERNEL_MALLOC
	return fill_block(buffer, size, 0xcccccccc);
#else
	return buffer;
#endif
}


static inline void*
fill_freed_block(void* buffer, size_t size)
{
#if PARANOID_KERNEL_FREE
	return fill_block(buffer, size, 0xdeadbeef);
#else
	return buffer;
#endif
}


#endif	// SLAB_DEBUG_H
