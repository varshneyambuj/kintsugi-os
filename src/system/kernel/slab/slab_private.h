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
 *   Copyright 2007, Hugo Santos. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Hugo Santos, hugosantos@gmail.com
 */

/** @file slab_private.h
 *  @brief Internal helpers shared between slab subsystem source files. */

#ifndef SLAB_PRIVATE_H
#define SLAB_PRIVATE_H


#include <stddef.h>

#include <slab/Slab.h>


/** @brief Minimum alignment guaranteed for objects returned by the slab allocator. */
static const size_t kMinObjectAlignment = 8;


/** @brief Schedules a maintenance pass on the memory manager. */
void		request_memory_manager_maintenance();

/** @brief Allocates @p size bytes through the early-boot block allocator. */
void*		block_alloc_early(size_t size);


/** @brief Slab-internal allocator that picks the right backend for the boot phase. */
static inline void*
slab_internal_alloc(size_t size, uint32 flags)
{
	if (flags & CACHE_DURING_BOOT)
		return block_alloc_early(size);

	return malloc_etc(size, flags);
}


/** @brief Slab-internal free counterpart to @c slab_internal_alloc(). */
static inline void
slab_internal_free(void* buffer, uint32 flags)
{
	free_etc(buffer, flags);
}


#endif	// SLAB_PRIVATE_H
