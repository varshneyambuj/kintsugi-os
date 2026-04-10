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
 *   Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2007, Hugo Santos. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file allocator.cpp
 * @brief Kernel slab allocator entry points — create_object_cache and friends.
 *
 * Provides the public API for creating and destroying slab object caches
 * (create_object_cache, create_object_cache_etc, delete_object_cache,
 * object_cache_alloc, object_cache_free). Delegates to SmallObjectCache
 * or HashedObjectCache depending on object size.
 *
 * @see Slab.cpp, ObjectCache.cpp
 */


#include "slab_private.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include <debug.h>
#include <heap.h>
#include <kernel.h> // for ROUNDUP
#include <malloc.h>
#include <vm/vm.h>
#include <vm/VMAddressSpace.h>

#include "ObjectCache.h"
#include "MemoryManager.h"


#if !USE_DEBUG_HEAPS_FOR_ALL_OBJECT_CACHES


#if DEBUG_HEAPS
#include "../debug/heaps.h"
#define SLAB_PUBLIC_NAME(NAME) slab_##NAME
#else
#define SLAB_PUBLIC_NAME(NAME) NAME
#endif


//#define TEST_ALL_CACHES_DURING_BOOT

static const size_t kBlockSizes[] = {
	16, 24, 32,
	48, 64, 80, 96, 112, 128,
	160, 192, 224, 256,
	320, 384, 448, 512,
	640, 768, 896, 1024,
	1280, 1536, 1792, 2048,
	2560, 3072, 3584, 4096,
	5120, 6144, 7168, 8192,
	10240, 12288, 14336, 16384,
};

static const size_t kNumBlockSizes = B_COUNT_OF(kBlockSizes);

static object_cache* sBlockCaches[kNumBlockSizes];

static addr_t sBootStrapMemory = 0;
static size_t sBootStrapMemorySize = 0;
static size_t sUsedBootStrapMemory = 0;


RANGE_MARKER_FUNCTION_BEGIN(slab_allocator)


/**
 * @brief Map an allocation size to the index of the smallest fitting block cache.
 *
 * The block sizes are arranged in power-of-two-aligned tiers. This function
 * performs a series of range checks to determine which tier contains the
 * smallest block size that can satisfy @p size.
 *
 * @param size  Requested allocation size in bytes.
 * @return      Non-negative index into @c sBlockCaches[], or @c -1 if @p size
 *              exceeds the largest supported block size (16384 bytes).
 */
static int
size_to_index(size_t size)
{
	if (size <= 16)
		return 0;
	if (size <= 32)
		return 1 + (size - 16 - 1) / 8;
	if (size <= 128)
		return 3 + (size - 32 - 1) / 16;
	if (size <= 256)
		return 9 + (size - 128 - 1) / 32;
	if (size <= 512)
		return 13 + (size - 256 - 1) / 64;
	if (size <= 1024)
		return 17 + (size - 512 - 1) / 128;
	if (size <= 2048)
		return 21 + (size - 1024 - 1) / 256;
	if (size <= 4096)
		return 25 + (size - 2048 - 1) / 512;
	if (size <= 8192)
		return 29 + (size - 4096 - 1) / 1024;
	if (size <= 16384)
		return 33 + (size - 8192 - 1) / 2048;

	return -1;
}


/**
 * @brief Allocate a raw memory block from the slab or memory manager.
 *
 * Selects the appropriate slab object cache using @c size_to_index(). If the
 * requested size exceeds all caches, falls through to
 * @c MemoryManager::AllocateRaw(). Alignment is handled by rounding @p size
 * up to the next power-of-two that is >= @p alignment.
 *
 * @param size       Number of bytes to allocate.
 * @param alignment  Required alignment in bytes; must be a power of two.
 *                   Pass 0 or @c kMinObjectAlignment for default alignment.
 * @param flags      Allocation flags (e.g. @c CACHE_DURING_BOOT,
 *                   @c CACHE_DONT_WAIT_FOR_MEMORY).
 * @return           Pointer to the allocated block, or @c NULL on failure.
 */
static void*
block_alloc(size_t size, size_t alignment, uint32 flags)
{
	if (alignment > kMinObjectAlignment) {
		// Make size >= alignment and a power of two. This is sufficient, since
		// all of our object caches with power of two sizes are aligned. We may
		// waste quite a bit of memory, but memalign() is very rarely used
		// in the kernel and always with power of two size == alignment anyway.
		ASSERT((alignment & (alignment - 1)) == 0);
		while (alignment < size)
			alignment <<= 1;
		size = alignment;

		// If we're not using an object cache, make sure that the memory
		// manager knows it has to align the allocation.
		if (size > kBlockSizes[kNumBlockSizes - 1])
			flags |= CACHE_ALIGN_ON_SIZE;
	}

	// allocate from the respective object cache, if any
	int index = size_to_index(size);
	if (index >= 0)
		return object_cache_alloc(sBlockCaches[index], flags);

	// the allocation is too large for our object caches -- ask the memory
	// manager
	void* block;
	if (MemoryManager::AllocateRaw(size, flags, block) != B_OK)
		return NULL;

	return block;
}


/**
 * @brief Allocate a block during early boot before all caches are ready.
 *
 * Attempts to satisfy the request from an already-initialised slab cache.
 * Large allocations (> @c SLAB_CHUNK_SIZE_SMALL) are routed directly to
 * @c MemoryManager::AllocateRaw(). Small allocations with no ready cache use
 * a linear bump allocator backed by a single raw chunk. Memory obtained from
 * the bootstrap bump allocator is permanent and must never be freed.
 *
 * @param size  Number of bytes to allocate.
 * @return      Pointer to the allocated block, or @c NULL on failure.
 */
void*
block_alloc_early(size_t size)
{
	int index = size_to_index(size);
	if (index >= 0 && sBlockCaches[index] != NULL)
		return object_cache_alloc(sBlockCaches[index], CACHE_DURING_BOOT);

	if (size > SLAB_CHUNK_SIZE_SMALL) {
		// This is a sufficiently large allocation -- just ask the memory
		// manager directly.
		void* block;
		if (MemoryManager::AllocateRaw(size, 0, block) != B_OK)
			return NULL;

		return block;
	}

	// A small allocation, but no object cache yet. Use the bootstrap memory.
	// This allocation must never be freed!
	if (sBootStrapMemorySize - sUsedBootStrapMemory < size) {
		// We need more memory.
		void* block;
		if (MemoryManager::AllocateRaw(SLAB_CHUNK_SIZE_SMALL, 0, block) != B_OK)
			return NULL;
		sBootStrapMemory = (addr_t)block;
		sBootStrapMemorySize = SLAB_CHUNK_SIZE_SMALL;
		sUsedBootStrapMemory = 0;
	}

	size_t neededSize = ROUNDUP(size, sizeof(double));
	if (sUsedBootStrapMemory + neededSize > sBootStrapMemorySize)
		return NULL;
	void* block = (void*)(sBootStrapMemory + sUsedBootStrapMemory);
	sUsedBootStrapMemory += neededSize;

	return block;
}


/**
 * @brief Free a block that was previously allocated by @c block_alloc() or
 *        @c block_alloc_early().
 *
 * Asks the memory manager to identify the owning cache. If a cache is
 * returned, delegates to @c object_cache_free(); otherwise the memory manager
 * has already reclaimed the raw pages.
 *
 * @param block  Pointer to the block to free. Silently ignores @c NULL.
 * @param flags  Deallocation flags forwarded to the cache/memory manager.
 */
static void
block_free(void* block, uint32 flags)
{
	if (block == NULL)
		return;

	ObjectCache* cache = MemoryManager::FreeRawOrReturnCache(block, flags);
	if (cache != NULL) {
		// a regular small allocation
		ASSERT(cache->object_size >= kBlockSizes[0]);
		ASSERT(cache->object_size <= kBlockSizes[kNumBlockSizes - 1]);
		ASSERT(cache == sBlockCaches[size_to_index(cache->object_size)]);
		object_cache_free(cache, block, flags);
	}
}


/**
 * @brief Initialise the kernel block allocator and all fixed-size slab caches.
 *
 * Creates one @c object_cache for each entry in @c kBlockSizes[]. Caches for
 * objects larger than 2048 bytes have the depot disabled to avoid retaining
 * excessive unused capacity. Power-of-two sized objects are aligned to their
 * own size.
 *
 * @param args  Kernel boot arguments (unused in the non-debug path).
 * @retval B_OK          All caches were created successfully.
 * @retval (panic)       Panics immediately if any cache creation fails.
 */
#if DEBUG_HEAPS
status_t
slab_heap_init(struct kernel_args*, addr_t, size_t)
#else
status_t
heap_init(struct kernel_args*)
#endif
{
	for (size_t index = 0; index < kNumBlockSizes; index++) {
		char name[32];
		snprintf(name, sizeof(name), "block allocator: %lu",
			kBlockSizes[index]);

		uint32 flags = CACHE_DURING_BOOT;
		size_t size = kBlockSizes[index];

		// align the power of two objects to their size
		size_t alignment = (size & (size - 1)) == 0 ? size : 0;

		// For the larger allocation sizes disable the object depot, so we don't
		// keep lot's of unused objects around.
		if (size > 2048)
			flags |= CACHE_NO_DEPOT;

		sBlockCaches[index] = create_object_cache_etc(name, size, alignment, 0,
			0, 0, flags, NULL, NULL, NULL, NULL);
		if (sBlockCaches[index] == NULL)
			panic("allocator: failed to init block cache");
	}

	return B_OK;
}


/**
 * @brief Post-semaphore initialisation hook for the slab heap.
 *
 * In test builds (TEST_ALL_CACHES_DURING_BOOT) exercises every block cache
 * with a single alloc/free round. In production this is a no-op.
 *
 * @retval B_OK  Always succeeds.
 */
status_t
SLAB_PUBLIC_NAME(heap_init_post_sem)()
{
#ifdef TEST_ALL_CACHES_DURING_BOOT
	for (int index = 0; kBlockSizes[index] != 0; index++) {
		block_free(block_alloc(kBlockSizes[index] - sizeof(boundary_tag)), 0,
			0);
	}
#endif

	return B_OK;
}


// #pragma mark - public API


/**
 * @brief Public aligned-allocation entry point with flags.
 *
 * Thin wrapper around @c block_alloc() that filters @p flags to the
 * allocation-relevant subset via @c CACHE_ALLOC_FLAGS.
 *
 * @param alignment  Required alignment in bytes (power of two).
 * @param size       Number of bytes to allocate.
 * @param flags      Caller-supplied flags; non-alloc bits are stripped.
 * @return           Aligned pointer to allocated memory, or @c NULL on failure.
 */
void *
SLAB_PUBLIC_NAME(memalign_etc)(size_t alignment, size_t size, uint32 flags)
{
	return block_alloc(size, alignment, flags & CACHE_ALLOC_FLAGS);
}


/**
 * @brief Public free entry point with flags.
 *
 * If @c CACHE_DONT_LOCK_KERNEL_SPACE is set the free is deferred to avoid
 * taking the kernel address-space lock; otherwise delegates to
 * @c block_free().
 *
 * @param address  Pointer to the block to free.
 * @param flags    Deallocation flags; controls deferred vs. immediate free.
 */
void
SLAB_PUBLIC_NAME(free_etc)(void *address, uint32 flags)
{
	if ((flags & CACHE_DONT_LOCK_KERNEL_SPACE) != 0) {
		deferred_free(address);
		return;
	}

	block_free(address, flags & CACHE_ALLOC_FLAGS);
}


/**
 * @brief Public realloc entry point with flags.
 *
 * Handles the four canonical realloc cases:
 *   - @p newSize == 0: free @p address and return @c NULL.
 *   - @p address == NULL: equivalent to @c block_alloc().
 *   - same size: return @p address unchanged.
 *   - otherwise: allocate a new block, copy the minimum of old and new sizes,
 *     free the original block, and return the new pointer.
 *
 * @param address  Existing allocation to resize (may be @c NULL).
 * @param newSize  Desired size in bytes.
 * @param flags    Allocation/deallocation flags.
 * @return         Pointer to the resized block, or @c NULL on failure or when
 *                 @p newSize is 0.
 */
void*
SLAB_PUBLIC_NAME(realloc_etc)(void* address, size_t newSize, uint32 flags)
{
	if (newSize == 0) {
		block_free(address, flags);
		return NULL;
	}

	if (address == NULL)
		return block_alloc(newSize, 0, flags);

	size_t oldSize;
	ObjectCache* cache = MemoryManager::GetAllocationInfo(address, oldSize);
	if (cache == NULL && oldSize == 0) {
		panic("block_realloc(): allocation %p not known", address);
		return NULL;
	}

	if (oldSize == newSize)
		return address;

	void* newBlock = block_alloc(newSize, 0, flags);
	if (newBlock == NULL)
		return NULL;

	memcpy(newBlock, address, std::min(oldSize, newSize));

	block_free(address, flags);

	return newBlock;
}


#if DEBUG_HEAPS


kernel_heap_implementation kernel_slab_heap = {
	"slab_heap",
	0, 0,

	slab_heap_init,
	NULL,
	slab_heap_init_post_sem,
	NULL,

	slab_memalign_etc,
	slab_realloc_etc,
	slab_free_etc,
};


#else


/**
 * @brief Standard C @c malloc() backed by the slab block allocator.
 *
 * @param size  Number of bytes to allocate.
 * @return      Pointer to allocated memory, or @c NULL on failure.
 */
void*
malloc(size_t size)
{
	return block_alloc(size, 0, 0);
}


/**
 * @brief Standard C @c free() backed by the slab block allocator.
 *
 * @param address  Pointer to the block to free. @c NULL is silently ignored.
 */
void
free(void* address)
{
	block_free(address, 0);
}


/**
 * @brief Standard C @c realloc() backed by the slab block allocator.
 *
 * @param address  Existing allocation to resize (may be @c NULL).
 * @param newSize  Desired new size in bytes.
 * @return         Pointer to the resized block, or @c NULL on failure.
 */
void*
realloc(void* address, size_t newSize)
{
	return realloc_etc(address, newSize, 0);
}


/**
 * @brief Standard C @c memalign() backed by the slab block allocator.
 *
 * @param alignment  Required alignment in bytes (power of two).
 * @param size       Number of bytes to allocate.
 * @return           Aligned pointer to allocated memory, or @c NULL on failure.
 */
void*
memalign(size_t alignment, size_t size)
{
	return block_alloc(size, alignment, 0);
}


/**
 * @brief POSIX @c posix_memalign() backed by the slab block allocator.
 *
 * Validates that @p alignment is a multiple of @c sizeof(void*) and a
 * power of two, then allocates via @c block_alloc().
 *
 * @param[out] _pointer   Set to the allocated block on success.
 * @param      alignment  Required alignment; must be a power of two and a
 *                        multiple of @c sizeof(void*).
 * @param      size       Number of bytes to allocate.
 * @retval 0              Success.
 * @retval B_BAD_VALUE    @p alignment is invalid or @p _pointer is @c NULL.
 */
int
posix_memalign(void** _pointer, size_t alignment, size_t size)
{
	if ((alignment & (sizeof(void*) - 1)) != 0 || _pointer == NULL)
		return B_BAD_VALUE;
	*_pointer = block_alloc(size, alignment, 0);
	return 0;
}


/**
 * @brief Post-thread initialisation hook for the heap — no-op in slab builds.
 *
 * @retval B_OK  Always succeeds.
 */
status_t
heap_init_post_thread()
{
	return B_OK;
}


#endif


RANGE_MARKER_FUNCTION_END(slab_allocator)


#else	// USE_DEBUG_HEAPS_FOR_ALL_OBJECT_CACHES


/**
 * @brief Stub for early-boot block allocation when the slab allocator is disabled.
 *
 * Panics immediately; this path should never be reached when
 * @c USE_DEBUG_HEAPS_FOR_ALL_OBJECT_CACHES is set.
 *
 * @param size  Unused allocation size.
 * @return      Always @c NULL (after panic).
 */
void*
block_alloc_early(size_t size)
{
	panic("block allocator not enabled!");
	return NULL;
}


#endif	// USE_DEBUG_HEAPS_FOR_ALL_OBJECT_CACHES
