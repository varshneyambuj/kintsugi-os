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
 *   Copyright (c) 2003 Marcus Overhagen
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BlockCache.cpp
 * @brief Implementation of BBlockCache, a fixed-size memory block pool.
 *
 * BBlockCache maintains a free-list of identically-sized heap blocks so that
 * frequently-allocated objects can be recycled without repeated calls to the
 * system allocator. Two allocation strategies are supported: B_MALLOC_CACHE
 * (uses malloc/free) and B_OBJECT_CACHE (uses operator new[]/delete[]).
 *
 * Because the BeBook allows callers to dispose of returned blocks directly
 * (rather than always returning them via Save()), the implementation cannot
 * use a single large slab; each block is independently allocated.
 *
 * When a debug heap is active (heap_debug_get_allocation_info != NULL), the
 * cache is bypassed entirely so that the debug heap can track every
 * individual allocation.
 */


#include <BlockCache.h>

#include <Debug.h>
#include <string.h>
#include <stdlib.h>
#include <new>
#include <pthread.h>


#ifdef __HAIKU__
extern "C" void heap_debug_get_allocation_info() __attribute__((weak));
#else
static const void* heap_debug_get_allocation_info = NULL;
#endif


#define MAGIC1		0x9183f4d9
#define MAGIC2		0xa6b3c87d

struct BBlockCache::_FreeBlock {
	DEBUG_ONLY(	uint32		magic1;	)
				_FreeBlock *next;
	DEBUG_ONLY(	uint32		magic2;	)
};


/**
 * @brief Construct the block cache and pre-allocate the initial pool.
 *
 * The constructor allocates \a blockCount blocks of at least \a blockSize
 * bytes each and places them on the internal free-list. The actual block
 * size may be silently enlarged to fit the internal _FreeBlock bookkeeping
 * structure when \a blockSize is smaller than sizeof(_FreeBlock).
 *
 * If a debug heap is detected the pool is not pre-allocated so that the
 * debug heap can track every allocation individually.
 *
 * @param blockCount     Number of blocks to pre-allocate. Must be >= 1; a
 *                       value of 0 is treated as 1.
 * @param blockSize      Size in bytes of each block. Values smaller than
 *                       sizeof(_FreeBlock) are rounded up.
 * @param allocationType Allocation strategy: B_OBJECT_CACHE or B_MALLOC_CACHE
 *                       (default for any unrecognised value).
 */
BBlockCache::BBlockCache(uint32 blockCount, size_t blockSize,
	uint32 allocationType)
	:
	fFreeList(0),
	fBlockSize(blockSize),
	fAlloc(NULL),
	fFree(NULL),
	fFreeBlocks(0),
	fBlockCount(blockCount)
{
	pthread_mutex_init(&fLock, NULL);

	switch (allocationType) {
		case B_OBJECT_CACHE:
			fAlloc = &operator new[];
			fFree = &operator delete[];
			break;
		case B_MALLOC_CACHE:
		default:
			fAlloc = &malloc;
			fFree = &free;
			break;
	}

	// If a debug heap is in use, don't cache anything.
	if (heap_debug_get_allocation_info != NULL)
		return;

	// To properly maintain a list of free buffers, a buffer must be
	// large enough to contain the _FreeBlock struct that is used.
	if (blockSize < sizeof(_FreeBlock))
		blockSize = sizeof(_FreeBlock);

	// should have at least one block
	if (blockCount == 0)
		blockCount = 1;

	// create blocks and put them into the free list
	while (blockCount--) {
		_FreeBlock *block = reinterpret_cast<_FreeBlock *>(fAlloc(blockSize));
		if (!block)
			break;
		fFreeBlocks++;
		block->next = fFreeList;
		fFreeList = block;
		DEBUG_ONLY(block->magic1 = MAGIC1);
		DEBUG_ONLY(block->magic2 = MAGIC2 + (uint32)(addr_t)block->next);
	}
}


/**
 * @brief Destroy the cache and release all pooled blocks.
 *
 * Only the blocks currently on the free-list are released here. Blocks
 * that were obtained via Get() and not yet returned via Save() are owned
 * by the caller and must be freed independently.
 */
BBlockCache::~BBlockCache()
{
	// walk the free list and deallocate all blocks
	pthread_mutex_lock(&fLock);
	while (fFreeList) {
		ASSERT(fFreeList->magic1 == MAGIC1);
		ASSERT(fFreeList->magic2 == MAGIC2 + (uint32)(addr_t)fFreeList->next);
		void *pointer = fFreeList;
		fFreeList = fFreeList->next;
		DEBUG_ONLY(memset(pointer, 0xCC, sizeof(_FreeBlock)));
		fFree(pointer);
	}

	pthread_mutex_destroy(&fLock);
}


/**
 * @brief Obtain a memory block of the requested size.
 *
 * When \a blockSize matches the pool's block size and the free-list is
 * non-empty a cached block is returned in O(1) without a system allocation.
 * Otherwise a fresh block is allocated directly from the heap.
 *
 * The caller may dispose of the returned block by passing it to Save() or
 * by calling free()/delete[] directly; the cache does not track outstanding
 * blocks.
 *
 * @param blockSize The desired allocation size in bytes.
 * @return A pointer to a memory block of at least \a blockSize bytes, or
 *         NULL if allocation fails.
 */
void *
BBlockCache::Get(size_t blockSize)
{
	if (heap_debug_get_allocation_info != NULL)
		return fAlloc(blockSize);

	pthread_mutex_lock(&fLock);
	void *pointer;
	if (blockSize == fBlockSize && fFreeList != 0) {
		// we can take a block from the list
		ASSERT(fFreeList->magic1 == MAGIC1);
		ASSERT(fFreeList->magic2 == MAGIC2 + (uint32)(addr_t)fFreeList->next);
		pointer = fFreeList;
		fFreeList = fFreeList->next;
		fFreeBlocks--;
		DEBUG_ONLY(memset(pointer, 0xCC, sizeof(_FreeBlock)));
		pthread_mutex_unlock(&fLock);
		return pointer;
	} else {
		pthread_mutex_unlock(&fLock);
		if (blockSize < sizeof(_FreeBlock))
			blockSize = sizeof(_FreeBlock);
		pointer = fAlloc(blockSize);
		DEBUG_ONLY(if (pointer) memset(pointer, 0xCC, sizeof(_FreeBlock)));
		return pointer;
	}
}


/**
 * @brief Return a block to the cache or free it if the pool is full.
 *
 * When \a blockSize matches the pool's block size and the number of free
 * blocks has not yet reached fBlockCount, the block is placed back on the
 * free-list for reuse. Otherwise the block is released to the heap
 * immediately.
 *
 * Passing NULL as \a pointer or a mismatched \a blockSize when the pool is
 * not full results in undefined behaviour (mirroring the original Be API
 * contract).
 *
 * @param pointer   The block previously obtained from Get(). Must not be NULL.
 * @param blockSize The size that was passed to the corresponding Get() call.
 */
void
BBlockCache::Save(void *pointer, size_t blockSize)
{
	if (heap_debug_get_allocation_info != NULL) {
		fFree(pointer);
		return;
	}

	pthread_mutex_lock(&fLock);
	if (blockSize == fBlockSize && fFreeBlocks < fBlockCount) {
		// the block needs to be returned to the cache
		_FreeBlock *block = reinterpret_cast<_FreeBlock *>(pointer);
		block->next = fFreeList;
		fFreeList = block;
		fFreeBlocks++;
		DEBUG_ONLY(block->magic1 = MAGIC1);
		DEBUG_ONLY(block->magic2 = MAGIC2 + (uint32)(addr_t)block->next);
		pthread_mutex_unlock(&fLock);
	} else {
		pthread_mutex_unlock(&fLock);
		DEBUG_ONLY(memset(pointer, 0xCC, sizeof(_FreeBlock)));
		fFree(pointer);
		return;
	}
}


void BBlockCache::_ReservedBlockCache1() {}
void BBlockCache::_ReservedBlockCache2() {}
