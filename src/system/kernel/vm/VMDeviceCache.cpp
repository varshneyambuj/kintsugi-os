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
 *   Copyright 2004-2007, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file VMDeviceCache.cpp
 * @brief VMCache subclass backed by a hardware device — maps device memory
 *        into the page cache.
 *
 * @see VMCache
 */

#include "VMDeviceCache.h"

#include <slab/Slab.h>


/**
 * @brief Initialise the device cache with the given physical base address.
 *
 * Stores @p baseAddress and delegates to VMCache::Init() to set up the
 * common cache infrastructure with the @c CACHE_TYPE_DEVICE type.
 *
 * @param baseAddress     Physical base address of the device memory region.
 * @param allocationFlags Flags forwarded to the slab allocator (e.g.
 *                        @c HEAP_DONT_WAIT_FOR_MEMORY).
 * @return @c B_OK on success, or a negative error code on failure.
 */
status_t
VMDeviceCache::Init(addr_t baseAddress, uint32 allocationFlags)
{
	fBaseAddress = baseAddress;
	return VMCache::Init("VMDeviceCache", CACHE_TYPE_DEVICE, allocationFlags);
}


/**
 * @brief Read pages from the device cache — always panics.
 *
 * Device memory is not readable through the generic page-cache I/O path.
 * Calling this function indicates a programming error and triggers a kernel
 * panic.
 *
 * @param offset    Byte offset within the cache (unused).
 * @param vecs      I/O vector array describing target buffers (unused).
 * @param count     Number of vectors in @p vecs (unused).
 * @param flags     I/O flags (unused).
 * @param _numBytes In/out byte count (unused).
 * @return Never returns normally; always panics.
 */
status_t
VMDeviceCache::Read(off_t offset, const generic_io_vec *vecs, size_t count,
	uint32 flags, generic_size_t *_numBytes)
{
	panic("device_store: read called. Invalid!\n");
	return B_ERROR;
}


/**
 * @brief Write pages to the device cache — silently succeeds.
 *
 * Device-backed pages are managed directly by hardware; there is no backing
 * store to write to. Returning @c B_OK causes the page daemon to treat these
 * pages as already clean and skip them during reclaim.
 *
 * @param offset    Byte offset within the cache (unused).
 * @param vecs      I/O vector array describing source buffers (unused).
 * @param count     Number of vectors in @p vecs (unused).
 * @param flags     I/O flags (unused).
 * @param _numBytes In/out byte count (unused).
 * @return Always @c B_OK.
 */
status_t
VMDeviceCache::Write(off_t offset, const generic_io_vec* vecs, size_t count,
	uint32 flags, generic_size_t* _numBytes)
{
	// no place to write, this will cause the page daemon to skip this store
	return B_OK;
}


/**
 * @brief Return this object to the device-cache object cache.
 *
 * Must be called instead of @c delete because the object was allocated from
 * @c gDeviceCacheObjectCache via the slab allocator.
 */
void
VMDeviceCache::DeleteObject()
{
	object_cache_delete(gDeviceCacheObjectCache, this);
}
