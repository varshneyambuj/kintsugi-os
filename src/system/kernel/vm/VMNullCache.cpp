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
 *   Copyright 2008-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file VMNullCache.cpp
 * @brief No-op VMCache subclass used as a placeholder when no backing store
 *        is needed.
 *
 * @see VMCache
 */

#include "VMNullCache.h"

#include <slab/Slab.h>


/**
 * @brief Initialise the null cache with the common VMCache infrastructure.
 *
 * Delegates to VMCache::Init() using the @c CACHE_TYPE_NULL type, which
 * signals that this cache has no physical backing store.
 *
 * @param allocationFlags Flags forwarded to the slab allocator (e.g.
 *                        @c HEAP_DONT_WAIT_FOR_MEMORY).
 * @return @c B_OK on success, or a negative error code on failure.
 */
status_t
VMNullCache::Init(uint32 allocationFlags)
{
	return VMCache::Init("VMNullCache", CACHE_TYPE_NULL, allocationFlags);
}


/**
 * @brief Return this object to the null-cache object cache.
 *
 * Must be called instead of @c delete because the object was allocated from
 * @c gNullCacheObjectCache via the slab allocator.
 */
void
VMNullCache::DeleteObject()
{
	object_cache_delete(gNullCacheObjectCache, this);
}
