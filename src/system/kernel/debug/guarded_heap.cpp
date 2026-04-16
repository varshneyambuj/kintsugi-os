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
 *   Copyright 2011-2020, Michael Lotz <mmlr@mlotz.ch>.
 *   Copyright 2025, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file guarded_heap.cpp
 * @brief Debug-only kernel heap that places a guard page after each allocation.
 *
 * The guarded heap satisfies requests by allocating N+1 contiguous pages of
 * kernel virtual address space: N data pages that hold the allocation, plus
 * one trailing unmapped guard page. Each returned pointer is positioned so
 * the allocation ends exactly on the last mapped byte, meaning any linear
 * overrun immediately faults through GuardedHeapCache::Fault() and triggers
 * a panic. Freed chunks are kept quarantined in a `dead_chunks` tree (their
 * pages fully unmapped) unless the `R` safemode option has re-enabled memory
 * reuse, in which case they move to `free_chunks` and may back later
 * allocations. Meta chunks are allocated via a separate SimpleAllocator so
 * bookkeeping never competes with user data for heap pages.
 *
 * The module also exports three KDL debugger commands -- `guarded_heap`,
 * `guarded_heap_chunk` and `allocations` -- for runtime inspection, and is
 * compiled only when DEBUG_HEAPS is defined.
 */


#include <stdio.h>
#include <string.h>

#include <arch/debug.h>
#include <elf.h>
#include <debug.h>
#include <heap.h>
#include <malloc.h>
#include <safemode.h>
#include <slab/Slab.h>
#include <team.h>
#include <util/SimpleAllocator.h>
#include <util/AutoLock.h>
#include <vm/VMAddressSpace.h>
#include <vm/vm.h>
#include <vm/vm_page.h>

#include "heaps.h"
#include "../vm/VMAddressSpaceLocking.h"
#include "../vm/VMAnonymousNoSwapCache.h"


#if DEBUG_HEAPS


#define GUARDED_HEAP_STACK_TRACE_DEPTH	0


/**
 * @brief Per-allocation bookkeeping record for the guarded heap.
 *
 * Tracks the page run backing one allocation: its base, page count, the
 * actual returned pointer (allocation_base), the caller-visible size and
 * alignment, and the team/thread that last touched the chunk. Acts as a
 * splay-tree node via the SplayTreeLink base.
 */
struct GuardedHeapChunk : public SplayTreeLink<GuardedHeapChunk> {
	GuardedHeapChunk*	tree_list_link;

	addr_t				base;
	size_t				pages_count;
	addr_t				allocation_base;
	size_t				allocation_size;
	size_t				alignment;
	team_id				team;
	thread_id			thread;
#if GUARDED_HEAP_STACK_TRACE_DEPTH > 0
	size_t				stack_trace_depth;
	addr_t				stack_trace[GUARDED_HEAP_STACK_TRACE_DEPTH];
#endif
};

/**
 * @brief IteratableSplayTree traits for indexing GuardedHeapChunk by base address.
 */
struct GuardedHeapChunksTreeDefinition {
	typedef addr_t		KeyType;
	typedef GuardedHeapChunk NodeType;

	/**
	 * @brief Extract the splay-tree key (base virtual address) of a chunk.
	 * @param node Chunk to inspect.
	 * @return The chunk's base virtual address.
	 */
	static addr_t GetKey(const GuardedHeapChunk* node)
	{
		return node->base;
	}

	/**
	 * @brief Return the SplayTreeLink embedded in a chunk.
	 * @param node Chunk whose link is needed.
	 * @return Pointer to the chunk's splay-tree link (the chunk itself).
	 */
	static SplayTreeLink<GuardedHeapChunk>* GetLink(GuardedHeapChunk* node)
	{
		return node;
	}

	/**
	 * @brief Order a chunk relative to an address key.
	 * @param key Address being searched for.
	 * @param node Chunk to compare against.
	 * @return 0 on equal keys, negative if @p key is below @p node, positive if above.
	 */
	static int Compare(const addr_t& key, const GuardedHeapChunk* node)
	{
		if (key == node->base)
			return 0;
		return (key < node->base) ? -1 : 1;
	}

	/**
	 * @brief Return the list-of-duplicates link pointer for a chunk.
	 * @param node Chunk to inspect.
	 * @return Address of the chunk's tree_list_link field.
	 */
	static GuardedHeapChunk** GetListLink(GuardedHeapChunk* node)
	{
		return &node->tree_list_link;
	}
};
typedef IteratableSplayTree<GuardedHeapChunksTreeDefinition> GuardedHeapChunksTree;


/**
 * @brief VM cache that backs every guarded-heap area.
 *
 * Behaves like VMAnonymousNoSwapCache for the pages that are intentionally
 * mapped (allocated data pages), but escalates any page fault through
 * Fault() -- since only unmapped guard pages can raise one, a fault always
 * indicates an out-of-bounds access and trips a panic.
 */
class GuardedHeapCache final : public VMAnonymousNoSwapCache {
public:
	/**
	 * @brief Initialize the backing VM cache used by the guarded heap.
	 * @return B_OK on success, or an error from VMAnonymousNoSwapCache::Init().
	 */
	status_t Init()
	{
		return VMAnonymousNoSwapCache::Init(false, 0, 0, 0);
	}

	status_t Fault(VMAddressSpace* aspace, off_t offset) override;

protected:
	/**
	 * @brief Prevent destruction of the singleton cache; fires an assertion.
	 */
	virtual	void DeleteObject() override
	{
		ASSERT_UNREACHABLE();
	}
};


/**
 * @brief Top-level guarded heap state.
 *
 * Holds the lock protecting the three chunk trees (live, free, dead), the
 * meta allocator used for bookkeeping records, and the sentinels that
 * serialise concurrent attempts to grow the meta or page pools.
 */
struct guarded_heap {
	mutex				lock;
	bool				reuse_memory;
	ConditionVariable	memory_added_condition;

	GuardedHeapCache*	cache;
	SimpleAllocator<>	meta_allocator;

	GuardedHeapChunksTree dead_chunks;
	GuardedHeapChunksTree free_chunks;
	GuardedHeapChunksTree live_chunks;
	addr_t				last_allocated;
	size_t				free_pages_count;

	thread_id			acquiring_pages;
	thread_id			acquiring_meta;
};


static GuardedHeapCache sGuardedHeapCache;
static guarded_heap sGuardedHeap = {
	MUTEX_INITIALIZER("guarded heap lock")
};
static addr_t sGuardedHeapEarlyMetaBase, sGuardedHeapEarlyBase;
static size_t sGuardedHeapEarlySize;


/**
 * @brief Allocate a GuardedHeapChunk-sized bookkeeping record from meta storage.
 *
 * Grows the meta SimpleAllocator by creating a new kernel area when the
 * remaining capacity falls below half a grow-step. Only one thread may grow
 * meta at a time; others wait on heap.memory_added_condition.
 *
 * @param heap Heap instance to allocate from.
 * @param size Byte count requested (normally sizeof(GuardedHeapChunk)).
 * @param flags Allocation flags, e.g. HEAP_DONT_LOCK_KERNEL_SPACE.
 * @return Pointer to zero-initialised memory, or NULL on failure.
 */
static void*
guarded_heap_allocate_meta(guarded_heap& heap, size_t size, uint32 flags)
{
	size_t growSize = ROUNDUP(((kernel_guarded_heap.grow_size / B_PAGE_SIZE) / 2)
			* sizeof(GuardedHeapChunk),
		B_PAGE_SIZE);
	while (heap.meta_allocator.Available() < (growSize / 2)) {
		if ((flags & HEAP_DONT_LOCK_KERNEL_SPACE) != 0)
			break;
		if (heap.acquiring_meta == thread_get_current_thread_id())
			break;

		bool waited = false;
		while (heap.acquiring_meta >= 0) {
			heap.memory_added_condition.Wait(&heap.lock);
			waited = true;
		}
		if (waited)
			continue;

		heap.acquiring_meta = thread_get_current_thread_id();
		mutex_unlock(&heap.lock);

		void* meta = NULL;
		create_area("guarded heap meta", &meta, B_ANY_KERNEL_ADDRESS, growSize, B_FULL_LOCK,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (meta == NULL)
			panic("guarded_heap: failed to allocate meta area");

		mutex_lock(&heap.lock);

		heap.meta_allocator.AddChunk(meta, growSize);
		heap.acquiring_meta = -1;
		heap.memory_added_condition.NotifyAll();
	}

	void* meta = heap.meta_allocator.Allocate(size);
	if (meta == NULL)
		return NULL;

	memset(meta, 0, size);
	return meta;
}


/**
 * @brief Reserve additional kernel virtual address space for the heap.
 *
 * Creates a new VMArea backed by the guarded-heap cache, inserts it into the
 * kernel address space, and adds its pages to the `free_chunks` tree. The
 * request may be serialised against other concurrent growers via the
 * `acquiring_pages` sentinel.
 *
 * @param heap Heap instance to grow.
 * @param minimumPages Minimum page count the new area must supply, or 0 to
 *        top up to the default grow size.
 * @param flags Allocation flags; HEAP_DONT_LOCK_KERNEL_SPACE forbids growth.
 * @return true if the heap grew (or already had enough), false otherwise.
 */
static bool
guarded_heap_add_area(guarded_heap& heap, size_t minimumPages, uint32 flags)
{
	if (heap.cache == NULL) {
		panic("guarded_heap_add_area: too early in the boot!");
		return false;
	}
	if ((flags & HEAP_DONT_LOCK_KERNEL_SPACE) != 0)
		return false;
	if (heap.acquiring_pages == thread_get_current_thread_id())
		return false;

	size_t growPages = kernel_guarded_heap.grow_size / B_PAGE_SIZE;
	if (minimumPages > growPages)
		growPages = minimumPages;

	while (heap.acquiring_pages >= 0)
		heap.memory_added_condition.Wait(&heap.lock);
	if (minimumPages == 0 && heap.free_pages_count >= growPages) {
		// Nothing more to do.
		return true;
	}

	// Allocate the meta chunk first, in case that triggers allocation of more.
	GuardedHeapChunk* chunk = (GuardedHeapChunk*)
		guarded_heap_allocate_meta(heap, sizeof(GuardedHeapChunk), flags);

	heap.acquiring_pages = thread_get_current_thread_id();
	mutex_unlock(&heap.lock);

	VMAddressSpace* addressSpace = VMAddressSpace::Kernel();
	AddressSpaceWriteLocker aspaceLocker(addressSpace, true);

	VMArea* area = addressSpace->CreateArea("guarded heap area", B_LAZY_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, flags | HEAP_PRIORITY_VIP);
	VMAreas::Insert(area);

	heap.cache->Lock();
	heap.cache->InsertAreaLocked(area);
	area->cache = heap.cache;
	heap.cache->Unlock();

	void* address = NULL;
	virtual_address_restrictions restrictions = {};
	if (addressSpace->InsertArea(area, growPages * B_PAGE_SIZE,
			&restrictions, 0, &address) != B_OK) {
		panic("guarded_heap: out of virtual memory");
		return false;
	}
	area->cache_offset = area->Base();

	aspaceLocker.Unlock();
	mutex_lock(&heap.lock);

	chunk->base = area->Base();
	chunk->pages_count = area->Size() / B_PAGE_SIZE;
	heap.free_chunks.Insert(chunk);
	heap.free_pages_count += chunk->pages_count;
	heap.acquiring_pages = -1;
	heap.memory_added_condition.NotifyAll();

	return true;
}


/**
 * @brief Locate the chunk in a tree that covers a given virtual address.
 * @param tree Chunks tree to search (live, free or dead).
 * @param address Virtual address to look up.
 * @return The covering chunk, or NULL if @p address falls outside every chunk.
 */
static GuardedHeapChunk*
guarded_heap_find_chunk(GuardedHeapChunksTree& tree, addr_t address)
{
	GuardedHeapChunk* chunk = tree.FindClosest((addr_t)address, false, true);
	if (chunk == NULL)
		return chunk;
	if (address >= chunk->base && address
			< (chunk->base + chunk->pages_count * B_PAGE_SIZE))
		return chunk;
	return NULL;
}


/**
 * @brief Allocate @p size bytes with a trailing guard page from the heap.
 *
 * Carves a (rounded-up + guard) run of pages out of `free_chunks`, maps the
 * data pages through the guarded-heap cache, and positions the returned
 * pointer so the allocation ends exactly at the last mapped byte -- any
 * linear overrun then faults against the unmapped guard page.
 *
 * @param heap Heap instance to allocate from.
 * @param size Requested allocation size in bytes.
 * @param alignment Required alignment in bytes (0 means sizeof(void*)).
 * @param flags Allocation flags, e.g. HEAP_DONT_WAIT_FOR_MEMORY.
 * @return Pointer to the allocation, or NULL on failure.
 */
static void*
guarded_heap_allocate(guarded_heap& heap, size_t size, size_t alignment,
	uint32 flags)
{
	MutexLocker locker(heap.lock);

	if (alignment == 0)
		alignment = sizeof(void*);

	// If we need more address space, allocate some now, while
	// there's still space to allocate bookkeeping structures.
	if (heap.free_pages_count <= (kernel_guarded_heap.grow_size / B_PAGE_SIZE / 2))
		guarded_heap_add_area(heap, 0, flags);

	// Allocate a spare meta chunk up-front, since this also might
	// need to unlock the heap to allocate more.
	GuardedHeapChunk* spareChunk = (GuardedHeapChunk*)
		guarded_heap_allocate_meta(heap, sizeof(GuardedHeapChunk), flags);
	if (spareChunk == NULL) {
		// Various flags can cause this even if there's still memory left.
		return NULL;
	}

	// Round up to the page size, plus the guard pages on either end.
	const size_t guardPages = 1;
	const size_t alignedSize = ROUNDUP(size, alignment);
	const size_t neededPages = ((alignedSize + B_PAGE_SIZE - 1) / B_PAGE_SIZE) + guardPages;

	// Try to allocate the next address up, to avoid rapid memory reuse.
	addr_t searchBase = heap.last_allocated;

	GuardedHeapChunk* freeChunk = NULL;
	bool restarted = false;
	while (freeChunk == NULL) {
		freeChunk = heap.free_chunks.FindClosest(searchBase, true, false);
		// Most allocations will be smaller than a page, so don't optimize for size.
		while (freeChunk != NULL && freeChunk->pages_count < neededPages)
			freeChunk = freeChunk->tree_list_link;
		if (freeChunk != NULL)
			break;

		if (freeChunk == NULL && !restarted) {
			// Start over at the beginning.
			searchBase = 0;
			restarted = true;
			continue;
		}

		// We need to reserve more virtual memory.
		if (!guarded_heap_add_area(heap, neededPages, flags)) {
			heap.meta_allocator.Free(spareChunk);
			return NULL;
		}
		searchBase = heap.last_allocated;
		restarted = false;
	}

	// We have a chunk of a suitable size. See if it can be split.
	GuardedHeapChunk* chunk = NULL;
	if (freeChunk->pages_count > neededPages) {
		chunk = spareChunk;
		chunk->base = freeChunk->base;
		chunk->pages_count = neededPages;
		freeChunk->base += neededPages * B_PAGE_SIZE;
		freeChunk->pages_count -= neededPages;
	} else {
		heap.meta_allocator.Free(spareChunk);
		heap.free_chunks.Remove(freeChunk);
		chunk = freeChunk;
	}
	heap.free_pages_count -= chunk->pages_count;

	chunk->allocation_size = size;
	chunk->alignment = alignment;

	chunk->allocation_base = chunk->base
		+ (chunk->pages_count - guardPages) * B_PAGE_SIZE
		- size;
	if (alignment > 1) {
		chunk->allocation_base -= (chunk->allocation_base % alignment);
		ASSERT(chunk->allocation_base >= chunk->base);
	}

#if GUARDED_HEAP_STACK_TRACE_DEPTH > 0
	allocatingChunk->stack_trace_depth = arch_debug_get_stack_trace(
		allocatingChunk->stack_trace, GUARDED_HEAP_STACK_TRACE_DEPTH,
		0, 3, STACK_TRACE_KERNEL);
#endif

	if (heap.cache == NULL) {
		// We must be early in the boot. Just hand out the memory directly.
		heap.live_chunks.Insert(chunk);
		heap.last_allocated = chunk->allocation_base;
		return (void*)chunk->allocation_base;
	}

	chunk->team = (gKernelStartup ? 0 : team_get_current_team_id());
	chunk->thread = (gKernelStartup ? 0 : thread_get_current_thread_id());

	// Reserve, allocate, and map pages.
	size_t mapPages = neededPages - guardPages;
	if (vm_try_reserve_memory(mapPages * B_PAGE_SIZE, VM_PRIORITY_SYSTEM,
			(flags & HEAP_DONT_WAIT_FOR_MEMORY) != 0 ? 0 : 1000000) != B_OK) {
		panic("out of memory");
		return NULL;
	}

	VMTranslationMap* map = VMAddressSpace::Kernel()->TranslationMap();
	size_t toMap = map->MaxPagesNeededToMap(chunk->base,
		chunk->base + mapPages * B_PAGE_SIZE);

	vm_page_reservation reservation = {};
	vm_page_reserve_pages(&reservation, mapPages + toMap, VM_PRIORITY_SYSTEM);

	heap.cache->Lock();
	map->Lock();
	for (size_t i = 0; i < mapPages; i++) {
		addr_t mapAddress = chunk->base + i * B_PAGE_SIZE;
		vm_page* page = vm_page_allocate_page(&reservation,
			PAGE_STATE_WIRED | VM_PAGE_ALLOC_CLEAR);
		heap.cache->InsertPage(page, mapAddress);
		map->Map(mapAddress, page->physical_page_number * B_PAGE_SIZE,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
			heap.cache->areas.First()->MemoryType(), &reservation);
		page->IncrementWiredCount();
		DEBUG_PAGE_ACCESS_END(page);
	}
	map->Unlock();
	heap.cache->Unlock();

	vm_page_unreserve_pages(&reservation);

	heap.live_chunks.Insert(chunk);
	heap.last_allocated = chunk->allocation_base;
	return (void*)chunk->allocation_base;
}


/**
 * @brief Detach and free a single physical page backing a heap address.
 * @param heap Heap whose cache owns the page.
 * @param pageAddress Virtual address of the page to free.
 * @param reservation Optional reservation to return the page into.
 * @return true if a page was found and freed, false if nothing was mapped.
 */
static bool
guarded_heap_free_page(guarded_heap& heap, addr_t pageAddress,
	vm_page_reservation* reservation = NULL)
{
	vm_page* page = heap.cache->LookupPage(pageAddress);
	if (page == NULL)
		return false;

	DEBUG_PAGE_ACCESS_START(page);
	page->DecrementWiredCount();
	heap.cache->RemovePage(page);
	vm_page_free_etc(heap.cache, page, reservation);
	return true;
}


/**
 * @brief Release a previously allocated block back to the guarded heap.
 *
 * Unmaps the chunk's data pages, moves it to either `free_chunks` (when
 * memory reuse is enabled) or `dead_chunks` (quarantined), and merges with
 * neighbouring chunks where possible. Freeing a non-live, misaligned or
 * unknown pointer panics -- this is a debugging allocator.
 *
 * @param heap Heap instance that owns the allocation.
 * @param address Pointer previously returned by guarded_heap_allocate().
 * @param flags Flags inherited from the malloc API; currently unused here.
 */
static void
guarded_heap_free(guarded_heap& heap, void* address, uint32 flags)
{
	if (address == NULL)
		return;

	MutexLocker locker(heap.lock);

	GuardedHeapChunk* chunk = guarded_heap_find_chunk(heap.live_chunks, (addr_t)address);
	if (chunk == NULL) {
		// See if it's in the dead or free chunks.
		GuardedHeapChunk* deadChunk = guarded_heap_find_chunk(heap.dead_chunks, (addr_t)address);
		GuardedHeapChunk* freeChunk = guarded_heap_find_chunk(heap.free_chunks, (addr_t)address);
		if (deadChunk != NULL || freeChunk != NULL) {
			chunk = deadChunk != NULL ? deadChunk : freeChunk;
			panic("tried to free %p, which is a %s chunk (last accessor: team %d, thread %d)",
				address, chunk == deadChunk ? "dead" : "free", chunk->team, chunk->thread);
		} else {
			panic("tried to free %p, but can't find a heap chunk for it", address);
		}
		return;
	} else {
		heap.live_chunks.Remove(chunk);
	}

	if (chunk->allocation_base != (addr_t)address) {
		panic("tried to free %p, but allocation base is really %p",
			address, (void*)chunk->allocation_base);
	}

	if (heap.cache == NULL) {
		// We must be early in the boot. Deal with this later.
		heap.dead_chunks.Insert(chunk);
		return;
	}

	// Unmap and free pages.
	VMTranslationMap* map = VMAddressSpace::Kernel()->TranslationMap();
	map->Lock();
	map->Unmap(chunk->base, chunk->base
		+ chunk->pages_count * B_PAGE_SIZE);
	map->Unlock();

	heap.cache->Lock();
	// Early boot allocations may have actual pages in the guard slot(s),
	// so iterate over the whole range just to be sure.
	vm_page_reservation reservation = {};
	for (size_t i = 0; i < chunk->pages_count; i++)
		guarded_heap_free_page(heap, chunk->base + i * B_PAGE_SIZE, &reservation);
	heap.cache->Unlock();

	vm_unreserve_memory(reservation.count * B_PAGE_SIZE);
	vm_page_unreserve_pages(&reservation);

	GuardedHeapChunksTree& tree = heap.reuse_memory ?
		heap.free_chunks : heap.dead_chunks;
	if (heap.reuse_memory)
		heap.free_pages_count += chunk->pages_count;

	GuardedHeapChunk* joinLower = guarded_heap_find_chunk(tree, chunk->base - 1),
		*joinUpper = guarded_heap_find_chunk(tree,
			chunk->base + chunk->pages_count * B_PAGE_SIZE);

	if (joinLower != NULL) {
		joinLower->pages_count += chunk->pages_count;
		joinLower->allocation_base = chunk->allocation_base;
		joinLower->allocation_size = chunk->allocation_size;
		joinLower->alignment = chunk->alignment;

		heap.meta_allocator.Free(chunk);
		chunk = joinLower;
	} else {
		tree.Insert(chunk);
	}

	if (joinUpper != NULL) {
		tree.Remove(joinUpper);
		chunk->pages_count += joinUpper->pages_count;
		heap.meta_allocator.Free(joinUpper);
	}

	chunk->team = (gKernelStartup ? 0 : team_get_current_team_id());
	chunk->thread = (gKernelStartup ? 0 : thread_get_current_thread_id());
#if GUARDED_HEAP_STACK_TRACE_DEPTH > 0
	chunk->stack_trace_depth = arch_debug_get_stack_trace(
		chunk->stack_trace, GUARDED_HEAP_STACK_TRACE_DEPTH,
		0, 3, STACK_TRACE_KERNEL);
#endif
}


/**
 * @brief Resize a live allocation, moving it to a new chunk if size changes.
 *
 * A resize to the same size is a no-op. Otherwise allocates a fresh chunk,
 * copies min(oldSize, newSize) bytes, and frees the old chunk. Panics if
 * @p address is not a live allocation.
 *
 * @param heap Heap instance that owns the allocation.
 * @param address Pointer to resize (must not be NULL).
 * @param newSize Requested new size in bytes.
 * @param flags Allocation flags forwarded to guarded_heap_allocate().
 * @return Pointer to the resized allocation, or NULL on allocation failure.
 */
static void*
guarded_heap_realloc(guarded_heap& heap, void* address, size_t newSize, uint32 flags)
{
	MutexLocker locker(heap.lock);

	GuardedHeapChunk* chunk = guarded_heap_find_chunk(heap.live_chunks, (addr_t)address);
	if (chunk == NULL) {
		panic("realloc(%p): no such allocation", address);
		return NULL;
	}
	if ((addr_t)address != chunk->allocation_base) {
		panic("realloc(%p): chunk base is really %p", address,
			  (void*)chunk->allocation_base);
	}

	size_t oldSize = chunk->allocation_size;
	locker.Unlock();

	if (oldSize == newSize)
		return address;

	void* newBlock = guarded_heap_allocate(heap, newSize, 0, flags);
	if (newBlock == NULL)
		return NULL;

	memcpy(newBlock, address, min_c(oldSize, newSize));

	guarded_heap_free(heap, address, flags);
	return newBlock;
}


/**
 * @brief VM fault handler for guarded-heap cache pages.
 *
 * Any page fault against the guarded-heap cache indicates access to a guard
 * or dead page, so we panic immediately with the offending offset.
 *
 * @param aspace Address space that took the fault (unused).
 * @param offset Offset within the cache that was faulted on.
 * @return B_BAD_ADDRESS (never returns normally; panics first).
 */
status_t
GuardedHeapCache::Fault(VMAddressSpace* aspace, off_t offset)
{
	panic("guarded_heap: invalid access to %p @! guarded_heap_chunk %p",
		(void*)offset, (void*)offset);
	return B_BAD_ADDRESS;
}


// #pragma mark - Debugger commands


/**
 * @brief Print the recorded allocation/free stack trace for a chunk.
 *
 * Only produces output when GUARDED_HEAP_STACK_TRACE_DEPTH > 0; otherwise
 * compiles to a no-op. Addresses are resolved through the ELF debug symbol
 * tables when possible.
 *
 * @param chunk Chunk whose stack trace should be printed.
 */
static void
dump_guarded_heap_stack_trace(GuardedHeapChunk& chunk)
{
#if GUARDED_HEAP_STACK_TRACE_DEPTH > 0
	kprintf("stack trace:\n");
	for (size_t i = 0; i < chunk.stack_trace_depth; i++) {
		addr_t address = chunk.stack_trace[i];

		const char* symbol;
		const char* imageName;
		bool exactMatch;
		addr_t baseAddress;

		if (elf_debug_lookup_symbol_address(address, &baseAddress, &symbol,
				&imageName, &exactMatch) == B_OK) {
			kprintf("  %p  %s + 0x%lx (%s)%s\n", (void*)address, symbol,
				address - baseAddress, imageName,
				exactMatch ? "" : " (nearest)");
		} else
			kprintf("  %p\n", (void*)address);
	}
#endif
}


/**
 * @brief KDL command `guarded_heap_chunk`: dump info about one heap chunk.
 *
 * Searches live, free and dead chunk trees for the chunk covering the given
 * address and prints its base, page count, allocation size/base, alignment
 * and last-accessor team/thread, plus any recorded stack trace.
 *
 * @param argc Argument count (must be 2).
 * @param argv Argument vector; argv[1] is the address expression.
 * @return 0 on success, 1 if no covering chunk was found.
 */
static int
dump_guarded_heap_chunk(int argc, char** argv)
{
	if (argc != 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	addr_t address = parse_expression(argv[1]);

	const char* state = NULL;
	const char* prefix = "last ";
	GuardedHeapChunk* chunk = guarded_heap_find_chunk(sGuardedHeap.live_chunks, address);
	if (chunk != NULL) {
		state = "live";
		prefix = "";
	} else {
		chunk = guarded_heap_find_chunk(sGuardedHeap.free_chunks, address);
		if (chunk != NULL) {
			state = "free";
		} else {
			chunk = guarded_heap_find_chunk(sGuardedHeap.dead_chunks, address);
			if (chunk != NULL)
				state = "dead";
		}
	}

	if (chunk == NULL) {
		kprintf("didn't find chunk for address\n");
		return 1;
	}

	kprintf("address %p: %s chunk %p\n", (void*)address, state, chunk);

	kprintf("base: %p\n", (void*)chunk->base);
	kprintf("pages count: %" B_PRIuSIZE "\n", chunk->pages_count);
	kprintf("%sallocation size: %" B_PRIuSIZE "\n", prefix, chunk->allocation_size);
	kprintf("%sallocation base: %p\n", prefix, (void*)chunk->allocation_base);
	kprintf("%salignment: %" B_PRIuSIZE "\n", prefix, chunk->alignment);
	kprintf("%sallocating team: %" B_PRId32 "\n", prefix, chunk->team);
	kprintf("%sallocating thread: %" B_PRId32 "\n", prefix, chunk->thread);

	dump_guarded_heap_stack_trace(*chunk);
	return 0;
}


/**
 * @brief KDL command `guarded_heap`: dump summary info about the heap.
 *
 * With no arguments, prints the global sGuardedHeap; with one argument, the
 * specified heap address expression. Reports the heap pointer, lock address
 * and current backing page count.
 *
 * @param argc Argument count (1 or 2).
 * @param argv Argument vector.
 * @return 0 on success.
 */
static int
dump_guarded_heap(int argc, char** argv)
{
	guarded_heap* heap = &sGuardedHeap;
	if (argc != 1) {
		if (argc == 2)
			heap = (guarded_heap*)parse_expression(argv[1]);
		else {
			print_debugger_command_usage(argv[0]);
			return 0;
		}
	}

	kprintf("guarded heap: %p\n", heap);
	kprintf("rw lock: %p\n", &heap->lock);
	kprintf("page count: %" B_PRIu32 "\n", heap->cache->page_count);

	return 0;
}


/**
 * @brief KDL command `allocations`: dump live allocations, optionally filtered.
 *
 * Accepts any of the optional filters `team <id>`, `thread <id>`,
 * `address <addr>`, and the flags `stats` (counts only) and `trace` (include
 * stack traces, only when built with stack tracing).
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success (including "no matches").
 */
static int
dump_guarded_heap_allocations(int argc, char** argv)
{
	team_id team = -1;
	thread_id thread = -1;
	addr_t address = 0;
	bool statsOnly = false;
	bool stackTrace = false;

	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "team") == 0)
			team = parse_expression(argv[++i]);
		else if (strcmp(argv[i], "thread") == 0)
			thread = parse_expression(argv[++i]);
		else if (strcmp(argv[i], "address") == 0)
			address = parse_expression(argv[++i]);
		else if (strcmp(argv[i], "stats") == 0)
			statsOnly = true;
#if GUARDED_HEAP_STACK_TRACE_DEPTH > 0
		else if (strcmp(argv[i], "trace") == 0)
			stackTrace = true;
#endif
		else {
			print_debugger_command_usage(argv[0]);
			return 0;
		}
	}

	size_t totalSize = 0;
	uint32 totalCount = 0;

	GuardedHeapChunk* chunk = sGuardedHeap.live_chunks.FindMin();
	while (chunk != NULL) {
		if ((team < 0 || chunk->team == team)
			&& (thread < 0 || chunk->thread == thread)
			&& (address == 0 || (addr_t)chunk->allocation_base == address)) {

			if (!statsOnly) {
				kprintf("team: % 6" B_PRId32 "; thread: % 6" B_PRId32 "; "
					"address: 0x%08" B_PRIxADDR "; size: %" B_PRIuSIZE
					" bytes\n", chunk->team, chunk->thread,
					(addr_t)chunk->allocation_base, chunk->allocation_size);

				if (stackTrace)
					dump_guarded_heap_stack_trace(*chunk);
			}

			totalSize += chunk->allocation_size;
			totalCount++;
		}

		chunk = chunk->tree_list_link;
	}

	kprintf("total allocations: %" B_PRIu32 "; total bytes: %" B_PRIuSIZE
		"\n", totalCount, totalSize);
	return 0;
}


// #pragma mark - Malloc API


/**
 * @brief Early-boot initialisation of the guarded heap.
 *
 * Reads the `guarded_heap_options` safemode string (the `r`/`R` characters
 * toggle memory reuse), carves the supplied region into a meta area and a
 * free-data area, and installs the initial free chunk.
 *
 * @param args Kernel boot arguments (used for safemode options).
 * @param address Base of the region reserved for heap use.
 * @param size Size of the reserved region in bytes.
 * @return B_OK on success.
 */
static status_t
guarded_heap_init(struct kernel_args* args, addr_t address, size_t size)
{
	sGuardedHeap.reuse_memory = DEBUG_GUARDED_HEAP_MEMORY_REUSE_DEFAULT;

	char options[32];
	size_t optionsSize = sizeof(options);
	if (get_safemode_option_early(args, "guarded_heap_options", options, &optionsSize) == B_OK) {
		if (strchr(options, 'r') != NULL)
			sGuardedHeap.reuse_memory = false;
		else if (strchr(options, 'R') != NULL)
			sGuardedHeap.reuse_memory = true;
	}
	dprintf("guarded heap settings: %s\n",
		sGuardedHeap.reuse_memory ? "R" : "r");

	sGuardedHeap.memory_added_condition.Init(&sGuardedHeap, "guarded heap");
	sGuardedHeap.acquiring_pages = sGuardedHeap.acquiring_meta = -1;

	size_t metaSize = ROUNDUP(((size / B_PAGE_SIZE) / 2) * sizeof(GuardedHeapChunk), B_PAGE_SIZE);
	sGuardedHeapEarlyMetaBase = address;
	sGuardedHeapEarlyBase = address + metaSize;
	sGuardedHeapEarlySize = size - metaSize;

	sGuardedHeap.meta_allocator.AddChunk((void*)address, metaSize);

	GuardedHeapChunk* chunk = (GuardedHeapChunk*)
		guarded_heap_allocate_meta(sGuardedHeap, sizeof(GuardedHeapChunk), 0);
	chunk->base = sGuardedHeapEarlyBase;
	chunk->pages_count = sGuardedHeapEarlySize / B_PAGE_SIZE;
	sGuardedHeap.free_chunks.Insert(chunk);
	sGuardedHeap.free_pages_count += chunk->pages_count;

	return B_OK;
}


/**
 * @brief Post-area heap initialisation; wraps the early region in VMAreas.
 *
 * Once the area subsystem is up, covers the early meta and data regions
 * with B_EXACT_ADDRESS / B_ALREADY_WIRED areas so their pages are tracked.
 *
 * @return B_OK on success.
 */
static status_t
guarded_heap_init_post_area()
{
	void* address = (void*)sGuardedHeapEarlyMetaBase;
	area_id metaAreaId = create_area("guarded heap meta", &address, B_EXACT_ADDRESS,
		sGuardedHeapEarlyBase - sGuardedHeapEarlyMetaBase, B_ALREADY_WIRED,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	ASSERT_ALWAYS(metaAreaId >= 0);

	address = (void*)sGuardedHeapEarlyBase;
	area_id areaId = create_area("guarded heap", &address, B_EXACT_ADDRESS,
		sGuardedHeapEarlySize, B_ALREADY_WIRED,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	VMAreas::Lookup(areaId)->cache_offset = (addr_t)address;

	return B_OK;
}


/**
 * @brief Post-semaphore initialisation: switch to the proper guarded cache
 *        and register KDL commands.
 *
 * Constructs the real GuardedHeapCache, re-homes the early area under it,
 * unmaps and frees guard and free-chunk pages that no longer need backing
 * storage, replays any dead chunks accumulated during early boot, and
 * registers the `guarded_heap`, `guarded_heap_chunk` and `allocations`
 * debugger commands.
 *
 * @return B_OK on success.
 */
static status_t
guarded_heap_init_post_sem()
{
	new(&sGuardedHeapCache) GuardedHeapCache;
	sGuardedHeapCache.Init();
	sGuardedHeapCache.virtual_base = VMAddressSpace::Kernel()->Base();
	sGuardedHeapCache.virtual_end = VMAddressSpace::Kernel()->EndAddress();
	sGuardedHeap.cache = &sGuardedHeapCache;

	// Switch to using the GuardedHeapCache and release the early one.
	VMArea* area = VMAreas::Lookup(area_for((void*)sGuardedHeapEarlyBase));
	VMCache* initialCache = area->cache;
	initialCache->Lock();
	sGuardedHeap.cache->Lock();
	sGuardedHeap.cache->Adopt(initialCache, 0, sGuardedHeapEarlySize, sGuardedHeapEarlyBase);
	area->cache = sGuardedHeap.cache;
	initialCache->Unlock();
	initialCache->RemoveArea(area);
	sGuardedHeap.cache->InsertAreaLocked(area);
	sGuardedHeap.cache->Unlock();

	MutexLocker locker(sGuardedHeap.lock);
	sGuardedHeap.cache->Lock();

	// Adjust all page protections, and free all guard and unused pages.
	vm_page_reservation reservation = {};
	VMTranslationMap* map = area->address_space->TranslationMap();
	map->Lock();

	GuardedHeapChunk* chunk = sGuardedHeap.live_chunks.FindMin();
	while (chunk != NULL) {
		addr_t unmap = chunk->base + (chunk->pages_count - 1) * B_PAGE_SIZE;
		map->Unmap(unmap, unmap + B_PAGE_SIZE);
		guarded_heap_free_page(sGuardedHeap, unmap, &reservation);

		chunk = chunk->tree_list_link;
	}

	chunk = sGuardedHeap.free_chunks.FindMin();
	while (chunk != NULL) {
		for (size_t i = 0; i < chunk->pages_count; i++) {
			addr_t pageAddress = chunk->base + i * B_PAGE_SIZE;
			map->Unmap(pageAddress, pageAddress + B_PAGE_SIZE);
			guarded_heap_free_page(sGuardedHeap, pageAddress, &reservation);
		}

		chunk = chunk->tree_list_link;
	}

	map->Unlock();
	sGuardedHeap.cache->Unlock();

	// Initial dead chunks are allocations that were freed during early boot.
	// Re-add them to the live chunks and actually free them.
	GuardedHeapChunk* linkChunk = NULL;
	while ((chunk = sGuardedHeap.dead_chunks.FindMax()) != NULL) {
		sGuardedHeap.dead_chunks.Remove(chunk);
		chunk->tree_list_link = linkChunk;
		linkChunk = chunk;
	}
	while (linkChunk != NULL) {
		chunk = linkChunk;
		linkChunk = linkChunk->tree_list_link;
		sGuardedHeap.live_chunks.Insert(chunk);
		locker.Unlock();
		guarded_heap_free(sGuardedHeap, (void*)chunk->allocation_base, 0);
		locker.Lock();
	}

	locker.Unlock();

	// Release the initial cache reference here, so that if the free triggers
	// malloc (e.g. for magazines in the object_caches), we're ready.
	initialCache->ReleaseRef();

	vm_unreserve_memory(reservation.count * B_PAGE_SIZE);
	vm_page_unreserve_pages(&reservation);

	add_debugger_command("guarded_heap", &dump_guarded_heap,
		"Dump info about the guarded heap");
	add_debugger_command_etc("guarded_heap_chunk", &dump_guarded_heap_chunk,
		"Dump info about a guarded heap chunk",
		"<address>\nDump info about guarded heap chunk.\n",
		0);
	add_debugger_command_etc("allocations", &dump_guarded_heap_allocations,
		"Dump current heap allocations",
#if GUARDED_HEAP_STACK_TRACE_DEPTH == 0
		"[\"stats\"] [team] [thread] [address]\n"
#else
		"[\"stats\"|\"trace\"] [team] [thread] [address]\n"
#endif
		"If no parameters are given, all current alloactions are dumped.\n"
		"If the optional argument \"stats\" is specified, only the allocation\n"
		"counts and no individual allocations are printed.\n"
#if GUARDED_HEAP_STACK_TRACE_DEPTH > 0
		"If the optional argument \"trace\" is specified, a stack trace for\n"
		"each allocation is printed.\n"
#endif
		"If a specific allocation address is given, only this allocation is\n"
		"dumped.\n"
		"If a team and/or thread is specified, only allocations of this\n"
		"team/thread are dumped.\n", 0);

	return B_OK;
}


/**
 * @brief kernel_heap_implementation memalign entry point.
 * @param alignment Required alignment in bytes (0 picks a pointer-sized default).
 * @param size Allocation size in bytes.
 * @param flags Allocation flags.
 * @return Pointer to the new allocation, or NULL on failure.
 */
static void*
guarded_heap_memalign(size_t alignment, size_t size, uint32 flags)
{
	return guarded_heap_allocate(sGuardedHeap, size, alignment, flags);
}


/**
 * @brief kernel_heap_implementation free entry point.
 * @param address Pointer previously returned by the guarded heap.
 * @param flags Allocation flags passed through to guarded_heap_free().
 */
static void
guarded_heap_free_etc(void *address, uint32 flags)
{
	guarded_heap_free(sGuardedHeap, address, flags);
}


/**
 * @brief kernel_heap_implementation realloc entry point.
 *
 * A size of 0 frees @p address and returns NULL. A NULL @p address with a
 * non-zero size is equivalent to malloc.
 *
 * @param address Existing allocation to resize, or NULL to allocate fresh.
 * @param newSize Requested new size in bytes.
 * @param flags Allocation flags.
 * @return Pointer to the resulting allocation, or NULL if freed/failed.
 */
static void*
guarded_heap_realloc_etc(void* address, size_t newSize, uint32 flags)
{
	if (newSize == 0) {
		guarded_heap_free(sGuardedHeap, address, flags);
		return NULL;
	}

	if (address == NULL)
		return guarded_heap_memalign(0, newSize, flags);

	return guarded_heap_realloc(sGuardedHeap, address, newSize, flags);
}


/**
 * @brief Vtable binding the guarded heap to the kernel's heap dispatch layer.
 *
 * The reserve sizes differ based on whether the object-cache layer is also
 * routed through the debug heaps (much larger budgets are then required).
 */
kernel_heap_implementation kernel_guarded_heap = {
	"guarded_heap",
#if USE_DEBUG_HEAPS_FOR_ALL_OBJECT_CACHES
	// This requires a lot of up-front memory to boot at all...
	/* initial_size */	128 * 1024 * 1024,
	// ... and a lot of reserves to keep running.
	/* grow_size */		128 * 1024 * 1024,
#else
	/* initial_size */	32 * 1024 * 1024,
	/* grow size */		32 * 1024 * 1024,
#endif

	guarded_heap_init,
	guarded_heap_init_post_area,
	guarded_heap_init_post_sem,
	NULL,

	guarded_heap_memalign,
	guarded_heap_realloc_etc,
	guarded_heap_free_etc,
};


#endif	// DEBUG_HEAPS
