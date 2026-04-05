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
 *   Copyright 2008-2010, Michael Lotz, mmlr@mlotz.ch.
 *   Copyright 2002-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file heap.cpp
 * @brief Kernel heap bootstrap and slab allocator integration.
 *
 * Provides the early kernel heap (used before the slab allocator is fully
 * initialized) and wires up new/delete to the slab-based object allocator.
 *
 * @see slab/Slab.cpp, slab/MemoryManager.cpp
 */


#include <stdlib.h>
#include <string.h>

#include <heap.h>
#include <util/AutoLock.h>
#include <util/SinglyLinkedList.h>


struct DeferredFreeListEntry : SinglyLinkedListLinkImpl<DeferredFreeListEntry> {
};

typedef SinglyLinkedList<DeferredFreeListEntry> DeferredFreeList;
typedef SinglyLinkedList<DeferredDeletable> DeferredDeletableList;

static DeferredFreeList sDeferredFreeList;
static DeferredDeletableList sDeferredDeletableList;
static spinlock sDeferredFreeListLock;


/**
 * @brief Allocate zero-initialised memory for an array of objects.
 *
 * Multiplies \a numElements by \a size with overflow protection, then
 * delegates to malloc() and zero-fills the returned block.
 *
 * @param numElements Number of elements to allocate.
 * @param size        Size in bytes of each element.
 * @return Pointer to the zero-filled allocation, or @c NULL on overflow or
 *         allocation failure.
 */
void *
calloc(size_t numElements, size_t size)
{
	if (size != 0 && numElements > (((size_t)(-1)) / size))
		return NULL;

	void *address = malloc(numElements * size);
	if (address != NULL)
		memset(address, 0, numElements * size);

	return address;
}


/**
 * @brief Allocate memory with an explicit alignment requirement.
 *
 * Verifies that \a size is a multiple of \a alignment (as required by the
 * C11 standard) and then forwards to memalign().
 *
 * @param alignment Desired alignment; must be a power of two and a divisor
 *                  of \a size.
 * @param size      Number of bytes to allocate.
 * @return Pointer to the aligned allocation, or @c NULL if the size/alignment
 *         constraint is violated or the allocation fails.
 */
void *
aligned_alloc(size_t alignment, size_t size)
{
	if ((size % alignment) != 0)
		return NULL;

	return memalign(alignment, size);
}


//	#pragma mark -


/**
 * @brief Kernel daemon callback that drains the deferred-free and
 *        deferred-delete lists.
 *
 * Registered with register_kernel_daemon() to run approximately once per
 * second. Atomically swaps both lists under the spinlock, then releases the
 * lock before performing any actual frees or virtual destructor calls, thereby
 * avoiding holding the spinlock during potentially slow operations.
 *
 * @param arg       Unused; present to match the kernel daemon callback
 *                  signature.
 * @param iteration Current daemon iteration count (unused).
 */
static void
deferred_deleter(void *arg, int iteration)
{
	// move entries and deletables to on-stack lists
	InterruptsSpinLocker locker(sDeferredFreeListLock);
	if (sDeferredFreeList.IsEmpty() && sDeferredDeletableList.IsEmpty())
		return;

	DeferredFreeList entries;
	entries.TakeFrom(&sDeferredFreeList);

	DeferredDeletableList deletables;
	deletables.TakeFrom(&sDeferredDeletableList);

	locker.Unlock();

	// free the entries
	while (DeferredFreeListEntry* entry = entries.RemoveHead())
		free(entry);

	// delete the deletables
	while (DeferredDeletable* deletable = deletables.RemoveHead())
		delete deletable;
}


/**
 * @brief Schedule a heap block for release outside of interrupt context.
 *
 * Constructs a @c DeferredFreeListEntry in-place at \a block and enqueues it
 * on the global deferred-free list protected by @c sDeferredFreeListLock.
 * The block will be released by the next deferred_deleter() invocation.
 *
 * @note Safe to call from interrupt context; the caller must not access
 *       \a block after this call returns.
 *
 * @param block Pointer to the heap block to free, or @c NULL (no-op).
 */
void
deferred_free(void *block)
{
	if (block == NULL)
		return;

	DeferredFreeListEntry *entry = new(block) DeferredFreeListEntry;

	InterruptsSpinLocker _(sDeferredFreeListLock);
	sDeferredFreeList.Add(entry);
}


/**
 * @brief Virtual destructor for the DeferredDeletable base class.
 *
 * Provides a well-defined virtual destructor so that concrete subclasses
 * are correctly destroyed when deferred_deleter() invokes @c delete on a
 * @c DeferredDeletable pointer.
 */
DeferredDeletable::~DeferredDeletable()
{
}


/**
 * @brief Schedule a C++ object for deletion outside of interrupt context.
 *
 * Enqueues \a deletable on the global deferred-deletable list protected by
 * @c sDeferredFreeListLock. The object's destructor will be called and its
 * storage freed by the next deferred_deleter() invocation.
 *
 * @note Safe to call from interrupt context; the caller must not access
 *       \a deletable after this call returns.
 *
 * @param deletable Object to delete, or @c NULL (no-op).
 */
void
deferred_delete(DeferredDeletable *deletable)
{
	if (deletable == NULL)
		return;

	InterruptsSpinLocker _(sDeferredFreeListLock);
	sDeferredDeletableList.Add(deletable);
}


/**
 * @brief Initialise the deferred-free subsystem.
 *
 * Registers deferred_deleter() as a kernel daemon that fires approximately
 * once per second (every 10 scheduler ticks). Must be called during kernel
 * initialisation before any code that may call deferred_free() or
 * deferred_delete() from interrupt context.
 *
 * @note Calls panic() if the daemon cannot be registered, as this is a
 *       non-recoverable initialisation failure.
 */
void
deferred_free_init()
{
	// run the deferred deleter roughly once a second
	if (register_kernel_daemon(deferred_deleter, NULL, 10) != B_OK)
		panic("failed to init deferred deleter");
}
