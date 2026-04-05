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
 *   Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file VMTranslationMap.cpp
 * @brief Architecture-independent base for page-table management.
 *
 * VMTranslationMap is the abstract interface for mapping virtual addresses
 * to physical pages in the hardware page tables. Concrete implementations
 * live in arch/x86/ (and other arch directories). This file provides the
 * default implementations and the physical-page reference-counting helpers.
 *
 * @see arch/x86/arch_vm_translation_map.cpp
 */


#include <vm/VMTranslationMap.h>

#include <slab/Slab.h>
#include <vm/vm_page.h>
#include <vm/vm_priv.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>
#include <vm/VMCache.h>


// #pragma mark - VMTranslationMap


/**
 * @brief Construct a VMTranslationMap and initialise the recursive lock.
 *
 * Sets the initial mapping count to zero and initialises @c fLock as a
 * recursive lock named "translation map". Concrete architecture-specific
 * subclasses must call this constructor.
 */
VMTranslationMap::VMTranslationMap()
	:
	fMapCount(0)
{
	recursive_lock_init(&fLock, "translation map");
}


/**
 * @brief Destroy a VMTranslationMap and release the recursive lock.
 *
 * Destroys the recursive lock acquired by the constructor. All mapped pages
 * must have been removed by the architecture-specific subclass before the
 * destructor chain reaches this point.
 */
VMTranslationMap::~VMTranslationMap()
{
	recursive_lock_destroy(&fLock);
}


/**
 * @brief Unmap a contiguous range of virtual pages belonging to an area.
 *
 * The default implementation iterates over every page-sized address in
 * [\a base, \a base + \a size) and calls UnmapPage() for each one that has
 * a present translation. This is not particularly efficient; architecture
 * ports should override this method when a bulk TLB shootdown is cheaper.
 *
 * @param area                    The VMArea that owns the virtual range.
 * @param base                    First virtual address to unmap; must be
 *                                page-aligned.
 * @param size                    Number of bytes to unmap; must be a multiple
 *                                of B_PAGE_SIZE.
 * @param updatePageQueue         If @c true, move freed pages to the
 *                                appropriate page queue.
 * @param deletingAddressSpace    If @c true, the owning address space is being
 *                                torn down; some architectures may skip TLB
 *                                invalidation in this case.
 */
void
VMTranslationMap::UnmapPages(VMArea* area, addr_t base, size_t size,
	bool updatePageQueue, bool deletingAddressSpace)
{
	ASSERT(base % B_PAGE_SIZE == 0);
	ASSERT(size % B_PAGE_SIZE == 0);

	addr_t address = base;
	addr_t end = address + size;
#if DEBUG_PAGE_ACCESS
	for (; address != end; address += B_PAGE_SIZE) {
		phys_addr_t physicalAddress;
		uint32 flags;
		if (Query(address, &physicalAddress, &flags) == B_OK
			&& (flags & PAGE_PRESENT) != 0) {
			vm_page* page = vm_lookup_page(physicalAddress / B_PAGE_SIZE);
			if (page != NULL) {
				DEBUG_PAGE_ACCESS_START(page);
				UnmapPage(area, address, updatePageQueue, deletingAddressSpace);
				DEBUG_PAGE_ACCESS_END(page);
			} else
				UnmapPage(area, address, updatePageQueue, deletingAddressSpace);
		}
	}
#else
	for (; address != end; address += B_PAGE_SIZE)
		UnmapPage(area, address, updatePageQueue, deletingAddressSpace);
#endif
}


/**
 * @brief Unmap all pages belonging to a VMArea and free the mapping objects.
 *
 * Handles both device and wired areas (delegating to UnmapPages()) and the
 * common case of demand-paged areas. For the common case the function:
 *  -# Acquires the translation map lock.
 *  -# Iterates over all vm_page_mapping objects for the area, removing each
 *     from both the page and area mapping lists.
 *  -# Calls UnmapPage() (with a flags output to avoid calling PageUnmapped)
 *     for pages that must remain visible to other address spaces.
 *  -# Propagates accessed/modified bits back to each vm_page.
 *  -# Requeues fully-unmapped pages to the appropriate page queue.
 *  -# Releases the lock, then frees all mapping objects.
 *
 * If \a deletingAddressSpace is @c true, the address space is already dead;
 * if \a ignoreTopCachePageFlags is additionally @c true, all top-cache pages
 * are about to be freed and their accessed/dirty flags need not be saved.
 *
 * @param area                    The VMArea whose pages are to be unmapped.
 * @param deletingAddressSpace    Pass @c true when the address space is being
 *                                destroyed, enabling possible TLB short-cuts.
 * @param ignoreTopCachePageFlags Pass @c true when the top cache of the area
 *                                is also being destroyed, so that
 *                                accessed/modified flag propagation can be
 *                                skipped for those pages.
 */
void
VMTranslationMap::UnmapArea(VMArea* area, bool deletingAddressSpace,
	bool ignoreTopCachePageFlags)
{
	if (area->cache_type == CACHE_TYPE_DEVICE || area->wiring != B_NO_LOCK) {
		UnmapPages(area, area->Base(), area->Size(), true, deletingAddressSpace);
		return;
	}

	const bool unmapPages = !deletingAddressSpace || !ignoreTopCachePageFlags;

	Lock();

	VMAreaMappings mappings;
	mappings.TakeFrom(&area->mappings);

	for (VMAreaMappings::Iterator it = mappings.GetIterator();
			vm_page_mapping* mapping = it.Next();) {
		vm_page* page = mapping->page;
		page->mappings.Remove(mapping);

		VMCache* cache = page->Cache();

		bool pageFullyUnmapped = false;
		if (!page->IsMapped()) {
			atomic_add(&gMappedPagesCount, -1);
			pageFullyUnmapped = true;
		}

		if (unmapPages || cache != area->cache) {
			const addr_t address = area->Base()
				+ ((page->cache_offset * B_PAGE_SIZE) - area->cache_offset);

			// UnmapPage should skip flushing and calling PageUnmapped when we pass &flags.
			uint32 flags = 0;
			status_t status = UnmapPage(area, address, false, deletingAddressSpace, &flags);
			if (status == B_ENTRY_NOT_FOUND) {
				panic("page %p has mapping for area %p (%#" B_PRIxADDR "), but "
					"has no translation map entry", page, area, address);
				continue;
			}
			if (status != B_OK) {
				panic("unmapping page %p for area %p (%#" B_PRIxADDR ") failed: %x",
					page, area, address, status);
				continue;
			}

			// Transfer the accessed/dirty flags to the page.
			if ((flags & PAGE_ACCESSED) != 0)
				page->accessed = true;
			if ((flags & PAGE_MODIFIED) != 0)
				page->modified = true;

			if (pageFullyUnmapped) {
				DEBUG_PAGE_ACCESS_START(page);

				if (cache->temporary)
					vm_page_set_state(page, PAGE_STATE_INACTIVE);
				else if (page->modified)
					vm_page_set_state(page, PAGE_STATE_MODIFIED);
				else
					vm_page_set_state(page, PAGE_STATE_CACHED);

				DEBUG_PAGE_ACCESS_END(page);
			}
		}
	}

	// This should Flush(), if necessary.
	Unlock();

	bool isKernelSpace = area->address_space == VMAddressSpace::Kernel();
	uint32 freeFlags = CACHE_DONT_WAIT_FOR_MEMORY
		| (isKernelSpace ? CACHE_DONT_LOCK_KERNEL_SPACE : 0);
	while (vm_page_mapping* mapping = mappings.RemoveHead())
		vm_free_page_mapping(mapping->page->physical_page_number, mapping, freeFlags);
}


/**
 * @brief Print paging-structure information for a virtual address (KDL helper).
 *
 * Navigates the hardware paging structures for \a virtualAddress and prints
 * all relevant intermediate entries to the kernel debugger output. The
 * default implementation prints a "not implemented" message; architecture
 * ports should override this method.
 *
 * @note This method is intended to be called only from a KDL command and
 *       must not acquire locks.
 *
 * @param virtualAddress The virtual address whose mapping information is to
 *                       be printed.
 */
void
VMTranslationMap::DebugPrintMappingInfo(addr_t virtualAddress)
{
#if KDEBUG
	kprintf("VMTranslationMap::DebugPrintMappingInfo not implemented\n");
#endif
}


/**
 * @brief Search for all virtual addresses mapped to a given physical address
 *        (KDL helper).
 *
 * For each virtual address that maps \a physicalAddress the method invokes
 * \a callback.HandleVirtualAddress(). The search stops as soon as the
 * callback returns @c true. The default implementation prints a "not
 * implemented" message and returns @c false; architecture ports should
 * override this method.
 *
 * @note Intended to be called only from a KDL command; must not acquire locks.
 *
 * @param physicalAddress The physical address to search for.
 * @param callback        Callback object notified for each virtual address
 *                        found; returning @c true from
 *                        HandleVirtualAddress() terminates the search.
 * @return @c true if the callback returned @c true for any virtual address,
 *         @c false otherwise.
 */
bool
VMTranslationMap::DebugGetReverseMappingInfo(phys_addr_t physicalAddress,
	ReverseMappingInfoCallback& callback)
{
#if KDEBUG
	kprintf("VMTranslationMap::DebugGetReverseMappingInfo not implemented\n");
#endif
	return false;
}


/**
 * @brief Post-unmap page bookkeeping called by architecture-specific
 *        UnmapPage() implementations.
 *
 * After the architecture-specific portion of an UnmapPage() call has removed
 * the hardware translation entry, this helper:
 *  -# Retrieves the vm_page for \a pageNumber.
 *  -# Propagates the \a accessed and \a modified flags to the page.
 *  -# Removes the vm_page_mapping object from both the page and area mapping
 *     lists (or decrements the wired count for wired areas).
 *  -# If \a mappingsQueue is @c NULL, unlocks the translation map and
 *     immediately frees the mapping object; otherwise appends it to
 *     \a mappingsQueue and leaves the map locked.
 *  -# Requeues a fully-unmapped page to the appropriate page queue when
 *     \a updatePageQueue is @c true.
 *
 * @param area            The VMArea from which the page was unmapped.
 * @param pageNumber      Physical page number that was unmapped.
 * @param accessed        Whether the hardware accessed bit was set.
 * @param modified        Whether the hardware dirty bit was set.
 * @param updatePageQueue If @c true, move a fully-unmapped page to the
 *                        correct page queue.
 * @param mappingsQueue   Optional queue to collect mapping objects for batch
 *                        freeing. When non-NULL the translation map lock is
 *                        NOT released by this function.
 */
void
VMTranslationMap::PageUnmapped(VMArea* area, page_num_t pageNumber,
	bool accessed, bool modified, bool updatePageQueue, VMAreaMappings* mappingsQueue)
{
	if (area->cache_type == CACHE_TYPE_DEVICE) {
		if (mappingsQueue == NULL)
			recursive_lock_unlock(&fLock);
		return;
	}

	// get the page
	vm_page* page = vm_lookup_page(pageNumber);
	ASSERT_PRINT(page != NULL, "page number: %#" B_PRIxPHYSADDR
		", accessed: %d, modified: %d", pageNumber, accessed, modified);

	if (mappingsQueue != NULL) {
		DEBUG_PAGE_ACCESS_START(page);
	} else {
		DEBUG_PAGE_ACCESS_CHECK(page);
	}

	// transfer the accessed/dirty flags to the page
	page->accessed |= accessed;
	page->modified |= modified;

	// remove the mapping object/decrement the wired_count of the page
	vm_page_mapping* mapping = NULL;
	if (area->wiring == B_NO_LOCK) {
		vm_page_mappings::Iterator iterator = page->mappings.GetIterator();
		while ((mapping = iterator.Next()) != NULL) {
			if (mapping->area == area) {
				area->mappings.Remove(mapping);
				page->mappings.Remove(mapping);
				break;
			}
		}

		ASSERT_PRINT(mapping != NULL, "page: %p, page number: %#"
			B_PRIxPHYSADDR ", accessed: %d, modified: %d", page,
			pageNumber, accessed, modified);
	} else
		page->DecrementWiredCount();

	if (mappingsQueue == NULL)
		recursive_lock_unlock(&fLock);

	if (!page->IsMapped()) {
		atomic_add(&gMappedPagesCount, -1);

		if (updatePageQueue) {
			if (page->Cache()->temporary)
				vm_page_set_state(page, PAGE_STATE_INACTIVE);
			else if (page->modified)
				vm_page_set_state(page, PAGE_STATE_MODIFIED);
			else
				vm_page_set_state(page, PAGE_STATE_CACHED);
		}
	}

	if (mappingsQueue != NULL) {
		DEBUG_PAGE_ACCESS_END(page);
	}

	if (mapping != NULL) {
		if (mappingsQueue == NULL) {
			bool isKernelSpace = area->address_space == VMAddressSpace::Kernel();
			vm_free_page_mapping(pageNumber, mapping,
				CACHE_DONT_WAIT_FOR_MEMORY
					| (isKernelSpace ? CACHE_DONT_LOCK_KERNEL_SPACE : 0));
		} else {
			mappingsQueue->Add(mapping);
		}
	}
}


/**
 * @brief Post-unmap bookkeeping for ClearAccessedAndModified() callers.
 *
 * Called by architecture-specific ClearAccessedAndModified() implementations
 * after the hardware accessed/modified bits have been cleared without a full
 * page unmap. Removes the vm_page_mapping object from the page and area
 * mapping lists (or decrements the wired count), unlocks the translation map,
 * and decrements the global mapped-pages counter if the page is now fully
 * unmapped.
 *
 * @note Because this is called by the page daemon, CACHE_DONT_LOCK_KERNEL_SPACE
 *       is always passed to vm_free_page_mapping() to prevent deadlocks.
 *
 * @param area       The VMArea associated with the mapping being cleared.
 * @param pageNumber Physical page number whose mapping object is to be removed.
 */
void
VMTranslationMap::UnaccessedPageUnmapped(VMArea* area, page_num_t pageNumber)
{
	if (area->cache_type == CACHE_TYPE_DEVICE) {
		recursive_lock_unlock(&fLock);
		return;
	}

	// get the page
	vm_page* page = vm_lookup_page(pageNumber);
	ASSERT_PRINT(page != NULL, "page number: %#" B_PRIxPHYSADDR, pageNumber);

	// remove the mapping object/decrement the wired_count of the page
	vm_page_mapping* mapping = NULL;
	if (area->wiring == B_NO_LOCK) {
		vm_page_mappings::Iterator iterator = page->mappings.GetIterator();
		while ((mapping = iterator.Next()) != NULL) {
			if (mapping->area == area) {
				area->mappings.Remove(mapping);
				page->mappings.Remove(mapping);
				break;
			}
		}

		ASSERT_PRINT(mapping != NULL, "page: %p, page number: %#"
			B_PRIxPHYSADDR, page, pageNumber);
	} else
		page->DecrementWiredCount();

	recursive_lock_unlock(&fLock);

	if (!page->IsMapped())
		atomic_add(&gMappedPagesCount, -1);

	if (mapping != NULL) {
		vm_free_page_mapping(pageNumber, mapping,
			CACHE_DONT_WAIT_FOR_MEMORY | CACHE_DONT_LOCK_KERNEL_SPACE);
			// Since this is called by the page daemon, we never want to lock
			// the kernel address space.
	}
}


/**
 * @brief Invalidate user-space TLB entries on one or more CPUs.
 *
 * Broadcasts a user-TLB invalidation ICI (inter-CPU interrupt) to all CPUs
 * in \a cpus except the calling CPU, then performs the invalidation locally
 * on the calling CPU (with interrupts disabled). If \a cpus does not include
 * the current CPU, only the broadcast is issued.
 *
 * @param cpus    Bitmask of CPUs on which the TLB invalidation must occur.
 * @param context Architecture-specific context token (e.g., address-space ID
 *                or CR3 value) passed through to arch_cpu_user_tlb_invalidate().
 */
void
VMTranslationMap::InvalidateUserTLB(CPUSet cpus, intptr_t context)
{
	int32 cpu = smp_get_current_cpu();
	const bool current = cpus.GetBit(cpu);
	cpus.ClearBit(cpu);
	if (!cpus.IsEmpty()) {
		if (current)
			cpus.SetBit(cpu);
		smp_multicast_ici(cpus, SMP_MSG_USER_INVALIDATE_PAGES,
			context, 0, 0, NULL, SMP_MSG_FLAG_SYNC);
	} else if (current) {
		cpu_status state = disable_interrupts();
		arch_cpu_user_tlb_invalidate(context);
		restore_interrupts(state);
	}
}


/**
 * @brief Invalidate a specific list of virtual addresses in the TLB on one
 *        or more CPUs.
 *
 * Broadcasts an SMP_MSG_INVALIDATE_PAGE_LIST ICI to all CPUs in \a cpus
 * except the calling CPU, then calls arch_cpu_invalidate_tlb_list() locally
 * for the calling CPU (if present in \a cpus).
 *
 * @param cpus         Bitmask of CPUs that must process the invalidation.
 * @param context      Architecture-specific context token forwarded to
 *                     arch_cpu_invalidate_tlb_list().
 * @param invalidPages Array of virtual addresses to invalidate.
 * @param count        Number of entries in \a invalidPages.
 */
void
VMTranslationMap::InvalidateTLBList(CPUSet cpus, intptr_t context,
	addr_t* invalidPages, int32 count)
{
	int32 cpu = smp_get_current_cpu();
	const bool current = cpus.GetBit(cpu);
	cpus.ClearBit(cpu);
	if (!cpus.IsEmpty()) {
		if (current)
			cpus.SetBit(cpu);
		smp_multicast_ici(cpus, SMP_MSG_INVALIDATE_PAGE_LIST,
			context, (addr_t)invalidPages, count, NULL,
			SMP_MSG_FLAG_SYNC);
	} else if (current) {
		arch_cpu_invalidate_tlb_list(context, invalidPages, count);
	}
}


// #pragma mark - ReverseMappingInfoCallback


/**
 * @brief Virtual destructor for ReverseMappingInfoCallback.
 *
 * Ensures correct destruction of concrete callback subclasses when deleted
 * through a base-class pointer.
 */
VMTranslationMap::ReverseMappingInfoCallback::~ReverseMappingInfoCallback()
{
}


// #pragma mark - VMPhysicalPageMapper


/**
 * @brief Construct a VMPhysicalPageMapper.
 *
 * Base constructor for the physical-page mapper abstraction. Architecture
 * ports subclass this to provide temporary mappings of physical pages into
 * the kernel virtual address space.
 */
VMPhysicalPageMapper::VMPhysicalPageMapper()
{
}


/**
 * @brief Destroy a VMPhysicalPageMapper.
 *
 * Virtual destructor ensuring proper cleanup of architecture-specific
 * subclass resources.
 */
VMPhysicalPageMapper::~VMPhysicalPageMapper()
{
}
