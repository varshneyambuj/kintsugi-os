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
 *   Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file VMAnonymousNoSwapCache.cpp
 * @brief Anonymous VM cache that does not support swapping pages to disk.
 *
 * A simplified VMAnonymousCache subclass used for wired memory and other
 * regions where pages must remain in physical RAM and cannot be swapped out.
 *
 * @see VMAnonymousCache.cpp, VMCache.cpp
 */

#include "VMAnonymousNoSwapCache.h"

#include <stdlib.h>

#include <arch_config.h>
#include <heap.h>
#include <KernelExport.h>
#include <slab/Slab.h>
#include <vm/vm_priv.h>
#include <vm/VMAddressSpace.h>


//#define TRACE_STORE
#ifdef TRACE_STORE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

// The stack functionality looks like a good candidate to put into its own
// store. I have not done this because once we have a swap file backing up
// the memory, it would probably not be a good idea to separate this
// anymore.


/**
 * @brief Destroys the cache and releases all reserved physical memory.
 *
 * Calls vm_unreserve_memory() for the entire committed_size so that the
 * global physical-memory reservation counter stays accurate.
 */
VMAnonymousNoSwapCache::~VMAnonymousNoSwapCache()
{
	vm_unreserve_memory(committed_size);
}


/**
 * @brief Initialises the cache with overcommit and guard-page settings.
 *
 * Delegates to VMCache::Init() for base-class setup, then records the
 * overcommit policy, precommitted-page count, and guarded region size.
 *
 * @param canOvercommit          If @c true the cache defers physical-memory
 *                               reservation until Fault() time.
 * @param numPrecommittedPages   Number of pages to speculatively reserve on
 *                               the first Commit() call (capped at 255).
 * @param numGuardPages          Number of guard pages at the appropriate end
 *                               of a stack region.
 * @param allocationFlags        Heap allocation flags forwarded to VMCache::Init().
 * @retval B_OK        Initialisation succeeded.
 * @retval B_NO_MEMORY Base-class initialisation failed to allocate memory.
 */
status_t
VMAnonymousNoSwapCache::Init(bool canOvercommit, int32 numPrecommittedPages,
	int32 numGuardPages, uint32 allocationFlags)
{
	TRACE(("VMAnonymousNoSwapCache::Init(canOvercommit = %s, numGuardPages = %ld) "
		"at %p\n", canOvercommit ? "yes" : "no", numGuardPages, store));

	status_t error = VMCache::Init("VMAnonymousNoSwapCache", CACHE_TYPE_RAM, allocationFlags);
	if (error != B_OK)
		return error;

	committed_size = 0;
	fCanOvercommit = canOvercommit;
	fHasPrecommitted = false;
	fPrecommittedPages = min_c(numPrecommittedPages, 255);
	fGuardedSize = numGuardPages * B_PAGE_SIZE;

	return B_OK;
}


status_t
VMAnonymousNoSwapCache::Adopt(VMCache* _from, off_t offset, off_t size,
	off_t newOffset)
{
	VMAnonymousNoSwapCache* from = dynamic_cast<VMAnonymousNoSwapCache*>(_from);
	ASSERT(from != NULL);

	uint32 initialPageCount = page_count;
	status_t status = VMCache::Adopt(from, offset, size, newOffset);

	if (fCanOvercommit) {
		// We need to adopt the commitment for these pages.
		ASSERT(from->fCanOvercommit);

		uint32 newPages = page_count - initialPageCount;
		off_t pagesCommitment = newPages * B_PAGE_SIZE;
		from->committed_size -= pagesCommitment;
		committed_size += pagesCommitment;
	}

	return status;
}


ssize_t
VMAnonymousNoSwapCache::Discard(off_t offset, off_t size)
{
	const ssize_t discarded = VMCache::Discard(offset, size);
	if (discarded > 0 && fCanOvercommit)
		Commit(committed_size - discarded, VM_PRIORITY_USER);
	return discarded;
}


off_t
VMAnonymousNoSwapCache::Commitment() const
{
	return committed_size;
}


/**
 * @brief Reports whether this cache allows memory overcommitment.
 *
 * When @c true, physical pages are not reserved until a page fault occurs
 * (lazy commitment via Fault()).
 *
 * @return @c true if overcommit is enabled, @c false otherwise.
 */
bool
VMAnonymousNoSwapCache::CanOvercommit()
{
	return fCanOvercommit;
}


/**
 * @brief Adjusts the physical-memory reservation to match \a size bytes.
 *
 * For overcommitting caches the first call pre-reserves a small number of
 * pages; subsequent page-level commits are handled in Fault().  For
 * non-overcommitting caches every call to Commit() reserves or releases
 * memory immediately.
 *
 * The cache lock must be held by the caller (AssertLocked() is called
 * internally).
 *
 * @param size      Desired total committed size in bytes.  Must be at least
 *                  @c page_count * B_PAGE_SIZE.
 * @param priority  VM priority (VM_PRIORITY_USER or VM_PRIORITY_SYSTEM) used
 *                  when trying to reserve additional memory.
 * @retval B_OK        Commitment adjusted successfully.
 * @retval B_NO_MEMORY Insufficient physical memory available to satisfy the
 *                     request.
 */
status_t
VMAnonymousNoSwapCache::Commit(off_t size, int priority)
{
	AssertLocked();
	ASSERT(size >= (page_count * B_PAGE_SIZE));

	// If we can overcommit, we don't commit here, but in Fault(). We always
	// unreserve memory, if we're asked to shrink our commitment, though.
	if (fCanOvercommit && size > committed_size) {
		if (fHasPrecommitted)
			return B_OK;

		// pre-commit some pages to make a later failure less probable
		fHasPrecommitted = true;
		uint32 precommitted = (fPrecommittedPages * B_PAGE_SIZE);
		if (size > precommitted)
			size = precommitted;

		// pre-commit should not shrink existing commitment
		size += committed_size;
	}

	// Check to see how much we could commit - we need real memory

	if (size > committed_size) {
		// try to commit
		if (vm_try_reserve_memory(size - committed_size, priority, 1000000)
				!= B_OK) {
			return B_NO_MEMORY;
		}
	} else {
		// we can release some
		vm_unreserve_memory(committed_size - size);
	}

	committed_size = size;
	return B_OK;
}


void
VMAnonymousNoSwapCache::TakeCommitmentFrom(VMCache* _from, off_t commitment)
{
	VMAnonymousNoSwapCache* from = dynamic_cast<VMAnonymousNoSwapCache*>(_from);
	ASSERT(from != NULL && from->committed_size >= commitment);
	AssertLocked();
	from->AssertLocked();

	from->committed_size -= commitment;
	committed_size += commitment;
}


/**
 * @brief Indicates that this cache never has pages in a backing store.
 *
 * Because this cache type has no swap backing, every page that is not
 * already in the page cache must be zero-filled on demand.
 *
 * @param offset  Byte offset within the cache (unused).
 * @return        Always @c false.
 */
bool
VMAnonymousNoSwapCache::StoreHasPage(off_t offset)
{
	return false;
}


status_t
VMAnonymousNoSwapCache::Read(off_t offset, const generic_io_vec* vecs, size_t count,
	uint32 flags, generic_size_t* _numBytes)
{
	panic("anonymous_store: read called. Invalid!\n");
	return B_ERROR;
}


status_t
VMAnonymousNoSwapCache::Write(off_t offset, const generic_io_vec* vecs, size_t count,
	uint32 flags, generic_size_t* _numBytes)
{
	// no place to write, this will cause the page daemon to skip this store
	return B_ERROR;
}


/**
 * @brief Handles a page fault for an anonymous, non-swappable region.
 *
 * Checks for stack guard-page violations first.  For overcommitting caches
 * it then tries to reserve one additional page of physical memory.
 * Returns B_BAD_HANDLER to let vm_soft_fault() allocate and zero-fill the
 * new page.
 *
 * @param aspace   The VMAddressSpace in which the fault occurred.  Used to
 *                 select the correct VM priority for memory reservation.
 * @param offset   Byte offset within the cache at which the fault occurred.
 * @retval B_BAD_HANDLER  Normal path — vm_soft_fault() should handle the fault.
 * @retval B_BAD_ADDRESS  The fault hit a guard page (stack overflow).
 * @retval B_NO_MEMORY    Overcommit reservation failed; no RAM available.
 */
status_t
VMAnonymousNoSwapCache::Fault(struct VMAddressSpace* aspace, off_t offset)
{
	if (fGuardedSize > 0) {
		uint32 guardOffset;

#ifdef STACK_GROWS_DOWNWARDS
		guardOffset = 0;
#elif defined(STACK_GROWS_UPWARDS)
		guardOffset = virtual_size - fGuardedSize;
#else
#	error Stack direction has not been defined in arch_config.h
#endif
		// report stack fault, guard page hit!
		if (offset >= guardOffset && offset < guardOffset + fGuardedSize) {
			TRACE(("stack overflow!\n"));
			return B_BAD_ADDRESS;
		}
	}

	if (fCanOvercommit) {
		if (fPrecommittedPages == 0) {
			// never commit more than needed
			if (committed_size / B_PAGE_SIZE > page_count)
				return B_BAD_HANDLER;

			// try to commit additional memory
			int priority = aspace == VMAddressSpace::Kernel()
				? VM_PRIORITY_SYSTEM : VM_PRIORITY_USER;
			if (vm_try_reserve_memory(B_PAGE_SIZE, priority, 0) != B_OK) {
				dprintf("%p->VMAnonymousNoSwapCache::Fault(): Failed to "
					"reserve %d bytes of RAM.\n", this, (int)B_PAGE_SIZE);
				return B_NO_MEMORY;
			}

			committed_size += B_PAGE_SIZE;
		} else
			fPrecommittedPages--;
	}

	// This will cause vm_soft_fault() to handle the fault
	return B_BAD_HANDLER;
}


/**
 * @brief Merges the source cache's committed memory into this cache.
 *
 * Transfers the source's committed_size to this cache, then trims the
 * combined commitment to the actual virtual size of the merged region.
 * Any excess reservation is returned to the system via vm_unreserve_memory().
 *
 * @param _source  The cache to merge from.  Must be a VMAnonymousNoSwapCache;
 *                 a panic is triggered otherwise.
 *
 * @note Both caches must be locked before calling this method.
 */
void
VMAnonymousNoSwapCache::Merge(VMCache* _source)
{
	VMAnonymousNoSwapCache* source
		= dynamic_cast<VMAnonymousNoSwapCache*>(_source);
	if (source == NULL) {
		panic("VMAnonymousNoSwapCache::Merge(): merge with incompatible "
			"cache %p requested", _source);
		return;
	}

	// take over the source's committed size
	committed_size += source->committed_size;
	source->committed_size = 0;

	off_t actualSize = PAGE_ALIGN(virtual_end - virtual_base);
	if (committed_size > actualSize) {
		vm_unreserve_memory(committed_size - actualSize);
		committed_size = actualSize;
	}

	VMCache::Merge(source);
}


void
VMAnonymousNoSwapCache::DeleteObject()
{
	object_cache_delete(gAnonymousNoSwapCacheObjectCache, this);
}
