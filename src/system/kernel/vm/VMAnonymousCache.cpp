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
 *   Copyright 2008, Zhao Shuai, upczhsh@163.com.
 *   Copyright 2008-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2011-2012 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors (Haiku):
 *       Hamish Morrison, hamish@lavabit.com
 *       Alexander von Gluck IV, kallisti5@unixzen.com
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file VMAnonymousCache.cpp
 * @brief Anonymous virtual memory cache with optional swap backing.
 *
 * VMAnonymousCache backs anonymous memory mappings (heap, stack, MAP_ANONYMOUS).
 * Pages start in RAM; under memory pressure they can be written to a swap file
 * and later faulted back in. Implements copy-on-write by sharing pages with
 * a source cache until a write fault triggers a private copy.
 *
 * @see VMAnonymousNoSwapCache.cpp, VMCache.cpp
 */


#include "VMAnonymousCache.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <FindDirectory.h>
#include <KernelExport.h>
#include <NodeMonitor.h>

#include <arch_config.h>
#include <boot_device.h>
#include <disk_device_manager/KDiskDevice.h>
#include <disk_device_manager/KDiskDeviceManager.h>
#include <disk_device_manager/KDiskSystem.h>
#include <disk_device_manager/KPartitionVisitor.h>
#include <driver_settings.h>
#include <fs/fd.h>
#include <fs/KPath.h>
#include <fs_info.h>
#include <fs_interface.h>
#include <heap.h>
#include <kernel_daemon.h>
#include <slab/Slab.h>
#include <syscalls.h>
#include <system_info.h>
#include <thread.h>
#include <tracing.h>
#include <util/AutoLock.h>
#include <util/Bitmap.h>
#include <util/DoublyLinkedList.h>
#include <util/OpenHashTable.h>
#include <util/RadixBitmap.h>
#include <vfs.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_priv.h>
#include <vm/VMAddressSpace.h>

#include "IORequest.h"


#if	ENABLE_SWAP_SUPPORT

//#define TRACE_VM_ANONYMOUS_CACHE
#ifdef TRACE_VM_ANONYMOUS_CACHE
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) do { } while (false)
#endif


// number of free swap blocks the object cache shall minimally have
#define MIN_SWAP_BLOCK_RESERVE	4096

// interval the has resizer is triggered (in 0.1s)
#define SWAP_HASH_RESIZE_INTERVAL	5

#define INITIAL_SWAP_HASH_SIZE		1024

#define SWAP_SLOT_NONE	RADIX_SLOT_NONE

#define SWAP_BLOCK_PAGES 32
#define SWAP_BLOCK_SHIFT 5		/* 1 << SWAP_BLOCK_SHIFT == SWAP_BLOCK_PAGES */
#define SWAP_BLOCK_MASK  (SWAP_BLOCK_PAGES - 1)


static const char* const kDefaultSwapPath = "/var/swap";

struct swap_file : DoublyLinkedListLinkImpl<swap_file> {
	int				fd;
	struct vnode*	vnode;
	void*			cookie;
	swap_addr_t		first_slot;
	swap_addr_t		last_slot;
	radix_bitmap*	bmp;
};

struct swap_hash_key {
	VMAnonymousCache	*cache;
	off_t				page_index;  // page index in the cache
};

// Each swap block contains swap address information for
// SWAP_BLOCK_PAGES continuous pages from the same cache
struct swap_block {
	swap_block*		hash_link;
	swap_hash_key	key;
	uint32			used;
	swap_addr_t		swap_slots[SWAP_BLOCK_PAGES];
};

struct SwapHashTableDefinition {
	typedef swap_hash_key KeyType;
	typedef swap_block ValueType;

	SwapHashTableDefinition() {}

	size_t HashKey(const swap_hash_key& key) const
	{
		off_t blockIndex = key.page_index >> SWAP_BLOCK_SHIFT;
		VMAnonymousCache* cache = key.cache;
		return blockIndex ^ (size_t)(int*)cache;
	}

	size_t Hash(const swap_block* value) const
	{
		return HashKey(value->key);
	}

	bool Compare(const swap_hash_key& key, const swap_block* value) const
	{
		return (key.page_index & ~(off_t)SWAP_BLOCK_MASK)
				== (value->key.page_index & ~(off_t)SWAP_BLOCK_MASK)
			&& key.cache == value->key.cache;
	}

	swap_block*& GetLink(swap_block* value) const
	{
		return value->hash_link;
	}
};

typedef BOpenHashTable<SwapHashTableDefinition> SwapHashTable;
typedef DoublyLinkedList<swap_file> SwapFileList;

static SwapHashTable sSwapHashTable;
static rw_lock sSwapHashLock;

static SwapFileList sSwapFileList;
static mutex sSwapFileListLock;
static swap_file* sSwapFileAlloc = NULL; // allocate from here
static uint32 sSwapFileCount = 0;

static off_t sAvailableSwapSpace = 0;
static mutex sAvailableSwapSpaceLock;

static object_cache* sSwapBlockCache;


#if SWAP_TRACING
namespace SwapTracing {

class SwapTraceEntry : public AbstractTraceEntry {
public:
	SwapTraceEntry(VMAnonymousCache* cache)
		:
		fCache(cache)
	{
	}

protected:
	VMAnonymousCache*	fCache;
};


class ReadPage : public SwapTraceEntry {
public:
	ReadPage(VMAnonymousCache* cache, page_num_t pageIndex,
		swap_addr_t swapSlotIndex)
		:
		SwapTraceEntry(cache),
		fPageIndex(pageIndex),
		fSwapSlotIndex(swapSlotIndex)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("swap read:  cache %p, page index: %lu <- swap slot: %lu",
			fCache, fPageIndex, fSwapSlotIndex);
	}

private:
	page_num_t		fPageIndex;
	swap_addr_t		fSwapSlotIndex;
};


class WritePage : public SwapTraceEntry {
public:
	WritePage(VMAnonymousCache* cache, page_num_t pageIndex,
		swap_addr_t swapSlotIndex)
		:
		SwapTraceEntry(cache),
		fPageIndex(pageIndex),
		fSwapSlotIndex(swapSlotIndex)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("swap write: cache %p, page index: %lu -> swap slot: %lu",
			fCache, fPageIndex, fSwapSlotIndex);
	}

private:
	page_num_t		fPageIndex;
	swap_addr_t		fSwapSlotIndex;
};

}	// namespace SwapTracing

#	define T(x) new(std::nothrow) SwapTracing::x;
#else
#	define T(x) ;
#endif


/**
 * @brief Kernel debugger command that prints swap file and usage statistics.
 *
 * @param argc Argument count from the debugger command line.
 * @param argv Argument vector from the debugger command line.
 * @retval 0 Always returns 0.
 * @note Safe to call from the kernel debugger; does not acquire any locks.
 */
static int
dump_swap_info(int argc, char** argv)
{
	swap_addr_t totalSwapPages = 0;
	swap_addr_t freeSwapPages = 0;

	kprintf("swap files:\n");

	for (SwapFileList::Iterator it = sSwapFileList.GetIterator();
		swap_file* file = it.Next();) {
		swap_addr_t total = file->last_slot - file->first_slot;
		kprintf("  vnode: %p, pages: total: %" B_PRIu32 ", free: %" B_PRIu32
			"\n", file->vnode, total, file->bmp->free_slots);

		totalSwapPages += total;
		freeSwapPages += file->bmp->free_slots;
	}

	kprintf("\n");
	kprintf("swap space in pages:\n");
	kprintf("total:     %9" B_PRIu32 "\n", totalSwapPages);
	kprintf("available: %9" B_PRIdOFF "\n", sAvailableSwapSpace / B_PAGE_SIZE);
	kprintf("reserved:  %9" B_PRIdOFF "\n",
		totalSwapPages - sAvailableSwapSpace / B_PAGE_SIZE);
	kprintf("used:      %9" B_PRIu32 "\n", totalSwapPages - freeSwapPages);
	kprintf("free:      %9" B_PRIu32 "\n", freeSwapPages);

	return 0;
}


/**
 * @brief Allocate @a count contiguous swap slots from the active swap file.
 *
 * Iterates over all registered swap files until it finds one with enough
 * contiguous free slots. If a swap file is more than 90% full the allocator
 * round-robins to the next file.
 *
 * @param count Number of contiguous page slots to allocate.
 * @return First slot index of the allocated run, or SWAP_SLOT_NONE when
 *         no swap file has sufficient space or @a count exceeds BITMAP_RADIX.
 * @note Acquires sSwapFileListLock internally; must not be called with that
 *       lock already held.
 */
static swap_addr_t
swap_slot_alloc(uint32 count)
{
	mutex_lock(&sSwapFileListLock);

	if (sSwapFileList.IsEmpty()) {
		mutex_unlock(&sSwapFileListLock);
		panic("swap_slot_alloc(): no swap file in the system\n");
		return SWAP_SLOT_NONE;
	}

	// since radix bitmap could not handle more than 32 pages, we return
	// SWAP_SLOT_NONE, this forces Write() adjust allocation amount
	if (count > BITMAP_RADIX) {
		mutex_unlock(&sSwapFileListLock);
		return SWAP_SLOT_NONE;
	}

	swap_addr_t j, addr = SWAP_SLOT_NONE;
	for (j = 0; j < sSwapFileCount; j++) {
		if (sSwapFileAlloc == NULL)
			sSwapFileAlloc = sSwapFileList.First();

		addr = radix_bitmap_alloc(sSwapFileAlloc->bmp, count);
		if (addr != SWAP_SLOT_NONE) {
			addr += sSwapFileAlloc->first_slot;
			break;
		}

		// this swap_file is full, find another
		sSwapFileAlloc = sSwapFileList.GetNext(sSwapFileAlloc);
	}

	if (j == sSwapFileCount) {
		mutex_unlock(&sSwapFileListLock);
		panic("swap_slot_alloc: swap space exhausted!\n");
		return SWAP_SLOT_NONE;
	}

	// if this swap file has used more than 90% percent of its space
	// switch to another
	if (sSwapFileAlloc->bmp->free_slots
		< (sSwapFileAlloc->last_slot - sSwapFileAlloc->first_slot) / 10) {
		sSwapFileAlloc = sSwapFileList.GetNext(sSwapFileAlloc);
	}

	mutex_unlock(&sSwapFileListLock);

	return addr;
}


/**
 * @brief Find the swap_file that owns the given global slot index.
 *
 * @param slotIndex Global swap slot address to look up.
 * @return Pointer to the owning swap_file, or NULL (after panic) if not found.
 * @note Caller must hold sSwapFileListLock.
 */
static swap_file*
find_swap_file(swap_addr_t slotIndex)
{
	for (SwapFileList::Iterator it = sSwapFileList.GetIterator();
		swap_file* swapFile = it.Next();) {
		if (slotIndex >= swapFile->first_slot
			&& slotIndex < swapFile->last_slot) {
			return swapFile;
		}
	}

	panic("find_swap_file(): can't find swap file for slot %" B_PRIu32 "\n",
		slotIndex);
	return NULL;
}


/**
 * @brief Return @a count contiguous swap slots starting at @a slotIndex.
 *
 * Locates the owning swap file for @a slotIndex and marks those slots free
 * in its radix bitmap.
 *
 * @param slotIndex First global slot index to free; no-op if SWAP_SLOT_NONE.
 * @param count     Number of contiguous slots to release.
 * @note Acquires sSwapFileListLock internally.
 */
static void
swap_slot_dealloc(swap_addr_t slotIndex, uint32 count)
{
	if (slotIndex == SWAP_SLOT_NONE)
		return;

	mutex_lock(&sSwapFileListLock);
	swap_file* swapFile = find_swap_file(slotIndex);
	slotIndex -= swapFile->first_slot;
	radix_bitmap_dealloc(swapFile->bmp, slotIndex, count);
	mutex_unlock(&sSwapFileListLock);
}


/**
 * @brief Reserve up to @a amount bytes of swap space from the global pool.
 *
 * Atomically decrements sAvailableSwapSpace by the requested amount, or by
 * whatever remains if the pool is smaller.
 *
 * @param amount Bytes of swap space to reserve.
 * @return Actual number of bytes reserved (may be less than @a amount).
 * @note Acquires sAvailableSwapSpaceLock internally.
 */
static off_t
swap_space_reserve(off_t amount)
{
	mutex_lock(&sAvailableSwapSpaceLock);
	if (sAvailableSwapSpace >= amount)
		sAvailableSwapSpace -= amount;
	else {
		amount = sAvailableSwapSpace;
		sAvailableSwapSpace = 0;
	}
	mutex_unlock(&sAvailableSwapSpaceLock);

	return amount;
}


/**
 * @brief Return previously reserved swap space back to the global pool.
 *
 * @param amount Bytes to return; silently ignored when zero.
 * @note Acquires sAvailableSwapSpaceLock internally.
 */
static void
swap_space_unreserve(off_t amount)
{
	if (amount == 0)
		return;

	mutex_lock(&sAvailableSwapSpaceLock);
	sAvailableSwapSpace += amount;
	mutex_unlock(&sAvailableSwapSpaceLock);
}


/**
 * @brief Kernel daemon callback that resizes the swap hash table when needed.
 *
 * Called periodically by the resource-resizer daemon. Determines the required
 * table size, drops the write lock to perform the allocation, then tries to
 * commit the resize. Retries if another thread raced and changed the required
 * size while the lock was released.
 *
 * @param data   Unused user data pointer (always NULL).
 * @param iteration Daemon iteration counter (unused).
 * @note Acquires and releases sSwapHashLock (write) internally.
 */
static void
swap_hash_resizer(void*, int)
{
	WriteLocker locker(sSwapHashLock);

	size_t size;
	void* allocation;

	do {
		size = sSwapHashTable.ResizeNeeded();
		if (size == 0)
			return;

		locker.Unlock();

		allocation = malloc(size);
		if (allocation == NULL)
			return;

		locker.Lock();

	} while (!sSwapHashTable.Resize(allocation, size));
}


// #pragma mark -


class VMAnonymousCache::WriteCallback : public StackableAsyncIOCallback {
public:
	WriteCallback(VMAnonymousCache* cache, AsyncIOCallback* callback)
		:
		StackableAsyncIOCallback(callback),
		fCache(cache)
	{
	}

	void SetTo(page_num_t pageIndex, swap_addr_t slotIndex, bool newSlot)
	{
		fPageIndex = pageIndex;
		fSlotIndex = slotIndex;
		fNewSlot = newSlot;
	}

	virtual void IOFinished(status_t status, bool partialTransfer,
		generic_size_t bytesTransferred)
	{
		if (fNewSlot) {
			if (status == B_OK) {
				fCache->_SwapBlockBuild(fPageIndex, fSlotIndex, 1);
			} else {
				AutoLocker<VMCache> locker(fCache);
				fCache->fAllocatedSwapSize -= B_PAGE_SIZE;
				locker.Unlock();

				swap_slot_dealloc(fSlotIndex, 1);
			}
		}

		fNextCallback->IOFinished(status, partialTransfer, bytesTransferred);
		delete this;
	}

private:
	VMAnonymousCache*	fCache;
	page_num_t			fPageIndex;
	swap_addr_t			fSlotIndex;
	bool				fNewSlot;
};


// #pragma mark -


/**
 * @brief Destructor — releases all swap slots and committed memory held by
 *        this anonymous cache.
 *
 * Frees the no-swap bitmap, walks the entire virtual range to release any
 * swap slots, returns reserved swap space, and unreserves RAM commitment.
 *
 * @note The cache lock must NOT be held by the caller; the destructor is
 *       invoked only when the last reference is dropped.
 */
VMAnonymousCache::~VMAnonymousCache()
{
	delete fNoSwapPages;
	fNoSwapPages = NULL;

	_FreeSwapPageRange(virtual_base, virtual_end, false);
	swap_space_unreserve(fReservedSwapSize);
	vm_unreserve_memory_or_swap(fCommittedSize);
}


/**
 * @brief Initialise a newly allocated VMAnonymousCache.
 *
 * Sets up overcommit policy, pre-committed page count, guard-page size, and
 * zeroes all swap-related accounting fields before delegating to VMCache::Init.
 *
 * @param canOvercommit         Allow the cache to commit memory lazily on fault.
 * @param numPrecommittedPages  Number of pages pre-committed without a fault
 *                              (capped at 255).
 * @param numGuardPages         Number of guard pages at the low/high end of the
 *                              mapping (stack overflow detection).
 * @param allocationFlags       Flags forwarded to the slab allocator.
 * @retval B_OK          Initialisation succeeded.
 * @retval B_NO_MEMORY   Underlying VMCache::Init failed to allocate resources.
 * @note Must be called exactly once after object construction and before any
 *       other method.
 */
status_t
VMAnonymousCache::Init(bool canOvercommit, int32 numPrecommittedPages,
	int32 numGuardPages, uint32 allocationFlags)
{
	TRACE("%p->VMAnonymousCache::Init(canOvercommit = %s, "
		"numPrecommittedPages = %" B_PRId32 ", numGuardPages = %" B_PRId32
		")\n", this, canOvercommit ? "yes" : "no", numPrecommittedPages,
		numGuardPages);

	status_t error = VMCache::Init("VMAnonymousCache", CACHE_TYPE_RAM, allocationFlags);
	if (error != B_OK)
		return error;

	fCommittedSize = 0;
	fCanOvercommit = canOvercommit;
	fHasPrecommitted = false;
	fPrecommittedPages = min_c(numPrecommittedPages, 255);
	fNoSwapPages = NULL;
	fGuardedSize = numGuardPages * B_PAGE_SIZE;
	fReservedSwapSize = 0;
	fAllocatedSwapSize = 0;

	return B_OK;
}


/**
 * @brief Mark a page range as swappable or non-swappable.
 *
 * Lazily allocates fNoSwapPages (a Bitmap) on the first call that disables
 * swapping. Each page in [@a base, @a base + @a size) is set or cleared in
 * the bitmap according to @a canSwap. The bitmap is freed when no non-swappable
 * pages remain.
 *
 * @param base    Byte offset within the cache of the first page to configure.
 * @param size    Length in bytes of the range to configure.
 * @param canSwap @c true to permit swapping; @c false to prohibit it.
 * @retval B_OK        Range configured successfully.
 * @retval B_NO_MEMORY Failed to allocate or resize the no-swap bitmap.
 * @note The cache lock must be held by the caller.
 */
status_t
VMAnonymousCache::SetCanSwapPages(off_t base, size_t size, bool canSwap)
{
	const page_num_t first = base >> PAGE_SHIFT;
	const size_t count = PAGE_ALIGN(size + ((first << PAGE_SHIFT) - base)) >> PAGE_SHIFT;

	if (count == 0)
		return B_OK;
	if (canSwap && fNoSwapPages == NULL)
		return B_OK;

	if (fNoSwapPages == NULL)
		fNoSwapPages = new(std::nothrow) Bitmap(0);
	if (fNoSwapPages == NULL)
		return B_NO_MEMORY;

	const page_num_t pageCount = PAGE_ALIGN(virtual_end) >> PAGE_SHIFT;

	if (fNoSwapPages->Resize(pageCount) != B_OK)
		return B_NO_MEMORY;

	for (size_t i = 0; i < count; i++) {
		if (canSwap)
			fNoSwapPages->Clear(first + i);
		else
			fNoSwapPages->Set(first + i);
	}

	if (fNoSwapPages->GetHighestSet() < 0) {
		delete fNoSwapPages;
		fNoSwapPages = NULL;
	}
	return B_OK;
}


/**
 * @brief Free all swap slots allocated for pages in the byte range
 *        [@a fromOffset, @a toOffset).
 *
 * Iterates page-by-page through the swap hash table, skipping entire
 * swap blocks that have no allocated slots. Busy pages can optionally be
 * skipped (their swap space is leaked) to avoid deadlocks during teardown.
 *
 * @param fromOffset    Start byte offset of the range (inclusive).
 * @param toOffset      End byte offset of the range (exclusive).
 * @param skipBusyPages When @c true, pages with @c busy set are not freed.
 * @note Acquires sSwapHashLock (write) on each iteration; the cache lock
 *       must be held by the caller.
 */
void
VMAnonymousCache::_FreeSwapPageRange(off_t fromOffset, off_t toOffset,
	bool skipBusyPages)
{
	swap_block* swapBlock = NULL;
	off_t toIndex = toOffset >> PAGE_SHIFT;
	for (off_t pageIndex = fromOffset >> PAGE_SHIFT;
		pageIndex < toIndex && fAllocatedSwapSize > 0; pageIndex++) {

		WriteLocker locker(sSwapHashLock);

		// Get the swap slot index for the page.
		swap_addr_t blockIndex = pageIndex & SWAP_BLOCK_MASK;
		if (swapBlock == NULL || blockIndex == 0) {
			swap_hash_key key = { this, pageIndex };
			swapBlock = sSwapHashTable.Lookup(key);

			if (swapBlock == NULL) {
				pageIndex = ROUNDUP(pageIndex + 1, SWAP_BLOCK_PAGES) - 1;
				continue;
			}
		}

		swap_addr_t slotIndex = swapBlock->swap_slots[blockIndex];
		if (slotIndex == SWAP_SLOT_NONE)
			continue;

		if (skipBusyPages) {
			vm_page* page = LookupPage(pageIndex * B_PAGE_SIZE);
			if (page != NULL && page->busy) {
				// TODO: We skip (i.e. leak) swap space of busy pages, since
				// there could be I/O going on (paging in/out). Waiting is
				// not an option as 1. unlocking the cache means that new
				// swap pages could be added in a range we've already
				// cleared (since the cache still has the old size) and 2.
				// we'd risk a deadlock in case we come from the file cache
				// and the FS holds the node's write-lock. We should mark
				// the page invalid and let the one responsible clean up.
				// There's just no such mechanism yet.
				continue;
			}
		}

		swap_slot_dealloc(slotIndex, 1);
		fAllocatedSwapSize -= B_PAGE_SIZE;

		swapBlock->swap_slots[blockIndex] = SWAP_SLOT_NONE;
		if (--swapBlock->used == 0) {
			// All swap pages have been freed -- we can discard the swap block.
			sSwapHashTable.RemoveUnchecked(swapBlock);
			object_cache_free(sSwapBlockCache, swapBlock,
				CACHE_DONT_WAIT_FOR_MEMORY | CACHE_DONT_LOCK_KERNEL_SPACE);

			// There are no swap pages for possibly remaining pages, skip to the
			// next block.
			pageIndex = ROUNDUP(pageIndex + 1, SWAP_BLOCK_PAGES) - 1;
			swapBlock = NULL;
		}
	}
}


/**
 * @brief Resize the cache to @a newSize bytes, freeing swap slots for
 *        truncated pages.
 *
 * Resizes fNoSwapPages if present, frees swap slots for any pages beyond
 * @a newSize, then delegates to VMCache::Resize for the page-tree update.
 *
 * @param newSize  New cache size in bytes.
 * @param priority VM priority used by VMCache::Resize for memory reservation.
 * @retval B_OK        Resize succeeded.
 * @retval B_NO_MEMORY Insufficient memory to resize fNoSwapPages.
 * @note The cache lock must be held by the caller.
 */
status_t
VMAnonymousCache::Resize(off_t newSize, int priority)
{
	if (fNoSwapPages != NULL) {
		if (fNoSwapPages->Resize(PAGE_ALIGN(newSize) >> PAGE_SHIFT) != B_OK)
			return B_NO_MEMORY;
	}

	_FreeSwapPageRange(newSize + B_PAGE_SIZE - 1,
		virtual_end + B_PAGE_SIZE - 1);
	return VMCache::Resize(newSize, priority);
}


/**
 * @brief Move the cache's virtual base to @a newBase, releasing swap slots
 *        for pages that fall below the new base.
 *
 * Shifts fNoSwapPages by the page-count difference between the old and new
 * base before freeing the now-unreachable swap range.
 *
 * @param newBase  New virtual base offset in bytes.
 * @param priority VM priority forwarded to VMCache::Rebase.
 * @retval B_OK        Rebase succeeded.
 * @retval B_NO_MEMORY VMCache::Rebase could not satisfy memory requirements.
 * @note The cache lock must be held by the caller.
 */
status_t
VMAnonymousCache::Rebase(off_t newBase, int priority)
{
	if (fNoSwapPages != NULL) {
		const ssize_t sizeDifference = (newBase >> PAGE_SHIFT) - (virtual_base >> PAGE_SHIFT);
		fNoSwapPages->Shift(sizeDifference);
	}

	_FreeSwapPageRange(virtual_base, newBase);
	return VMCache::Rebase(newBase, priority);
}


/**
 * @brief Discard pages and their swap slots in the range
 *        [@a offset, @a offset + @a size).
 *
 * Frees swap slots for the discarded range via _FreeSwapPageRange, then calls
 * VMCache::Discard to remove the physical pages. For overcommitting caches the
 * committed size is reduced by the amount actually discarded.
 *
 * @param offset  Byte offset within the cache of the first page to discard.
 * @param size    Length in bytes of the range to discard.
 * @return Number of bytes actually discarded, or a negative error code.
 * @note The cache lock must be held by the caller.
 */
ssize_t
VMAnonymousCache::Discard(off_t offset, off_t size)
{
	_FreeSwapPageRange(offset, offset + size);
	const ssize_t discarded = VMCache::Discard(offset, size);
	if (discarded > 0 && fCanOvercommit)
		Commit(fCommittedSize - discarded, VM_PRIORITY_USER);
	return discarded;
}


/*!	Moves the swap pages for the given range from the source cache into this
	cache. Both caches must be locked.
*/
/**
 * @brief Adopt pages and swap slots from @a _source in the given byte range.
 *
 * Transfers swap blocks from the source VMAnonymousCache to this cache for
 * the byte range [@a offset, @a offset + @a size), re-keyed to @a newOffset
 * in the consumer. For overcommitting caches the corresponding RAM commitment
 * is also transferred.
 *
 * @param _source    Source VMCache; must be a VMAnonymousCache.
 * @param offset     Byte offset in @a _source of the first page to adopt.
 * @param size       Length in bytes of the range to adopt.
 * @param newOffset  Byte offset in this cache where adopted pages land.
 * @retval B_OK        All pages and swap slots adopted successfully.
 * @retval B_NO_MEMORY Could not allocate a new swap block for the consumer.
 * @retval B_ERROR     @a _source is not a VMAnonymousCache (triggers panic).
 * @note Both the source and consumer cache locks must be held by the caller.
 *       sSwapHashLock (write) is acquired internally.
 */
status_t
VMAnonymousCache::Adopt(VMCache* _source, off_t offset, off_t size,
	off_t newOffset)
{
	VMAnonymousCache* source = dynamic_cast<VMAnonymousCache*>(_source);
	if (source == NULL) {
		panic("VMAnonymousCache::Adopt(): adopt from incompatible cache %p "
			"requested", _source);
		return B_ERROR;
	}

	off_t pageIndex = newOffset >> PAGE_SHIFT;
	off_t sourcePageIndex = offset >> PAGE_SHIFT;
	off_t sourceEndPageIndex = (offset + size + B_PAGE_SIZE - 1) >> PAGE_SHIFT;
	swap_block* swapBlock = NULL;

	WriteLocker locker(sSwapHashLock);

	while (sourcePageIndex < sourceEndPageIndex
			&& source->fAllocatedSwapSize > 0) {
		swap_addr_t left
			= SWAP_BLOCK_PAGES - (sourcePageIndex & SWAP_BLOCK_MASK);

		swap_hash_key sourceKey = { source, sourcePageIndex };
		swap_block* sourceSwapBlock = sSwapHashTable.Lookup(sourceKey);
		if (sourceSwapBlock == NULL || sourceSwapBlock->used == 0) {
			sourcePageIndex += left;
			pageIndex += left;
			swapBlock = NULL;
			continue;
		}

		for (; left > 0 && sourceSwapBlock->used > 0;
				left--, sourcePageIndex++, pageIndex++) {

			swap_addr_t blockIndex = pageIndex & SWAP_BLOCK_MASK;
			if (swapBlock == NULL || blockIndex == 0) {
				swap_hash_key key = { this, pageIndex };
				swapBlock = sSwapHashTable.Lookup(key);

				if (swapBlock == NULL) {
					swapBlock = (swap_block*)object_cache_alloc(sSwapBlockCache,
						CACHE_DONT_WAIT_FOR_MEMORY
							| CACHE_DONT_LOCK_KERNEL_SPACE);
					if (swapBlock == NULL)
						return B_NO_MEMORY;

					swapBlock->key.cache = this;
					swapBlock->key.page_index
						= pageIndex & ~(off_t)SWAP_BLOCK_MASK;
					swapBlock->used = 0;
					for (uint32 i = 0; i < SWAP_BLOCK_PAGES; i++)
						swapBlock->swap_slots[i] = SWAP_SLOT_NONE;

					sSwapHashTable.InsertUnchecked(swapBlock);
				}
			}

			swap_addr_t sourceBlockIndex = sourcePageIndex & SWAP_BLOCK_MASK;
			swap_addr_t slotIndex
				= sourceSwapBlock->swap_slots[sourceBlockIndex];
			if (slotIndex == SWAP_SLOT_NONE)
				continue;

			ASSERT(swapBlock->swap_slots[blockIndex] == SWAP_SLOT_NONE);

			swapBlock->swap_slots[blockIndex] = slotIndex;
			swapBlock->used++;
			fAllocatedSwapSize += B_PAGE_SIZE;
			fReservedSwapSize += B_PAGE_SIZE;

			sourceSwapBlock->swap_slots[sourceBlockIndex] = SWAP_SLOT_NONE;
			sourceSwapBlock->used--;
			source->fAllocatedSwapSize -= B_PAGE_SIZE;
			source->fReservedSwapSize -= B_PAGE_SIZE;

			TRACE("adopted slot %#" B_PRIx32 " from %p at page %" B_PRIdOFF
				" to %p at page %" B_PRIdOFF "\n", slotIndex, source,
				sourcePageIndex, this, pageIndex);
		}

		if (left > 0) {
			sourcePageIndex += left;
			pageIndex += left;
			swapBlock = NULL;
		}

		if (sourceSwapBlock->used == 0) {
			// All swap pages have been adopted, we can discard the swap block.
			sSwapHashTable.RemoveUnchecked(sourceSwapBlock);
			object_cache_free(sSwapBlockCache, sourceSwapBlock,
				CACHE_DONT_WAIT_FOR_MEMORY | CACHE_DONT_LOCK_KERNEL_SPACE);
		}
	}

	locker.Unlock();

	uint32 initialPageCount = page_count;
	status_t status = VMCache::Adopt(source, offset, size, newOffset);

	if (fCanOvercommit) {
		// We need to adopt the commitment for these pages.
		uint32 newPages = page_count - initialPageCount;
		off_t pagesCommitment = newPages * B_PAGE_SIZE;
		source->fCommittedSize -= pagesCommitment;
		fCommittedSize += pagesCommitment;
	}

	return status;
}


/**
 * @brief Return the current committed size of this cache in bytes.
 *
 * @return fCommittedSize — the amount of RAM (or swap) reserved for this cache.
 * @note The cache lock should be held by the caller for a consistent read.
 */
off_t
VMAnonymousCache::Commitment() const
{
	return fCommittedSize;
}


/**
 * @brief Query whether this cache was created with overcommit enabled.
 *
 * @return @c true if the cache may commit memory lazily on page fault.
 */
bool
VMAnonymousCache::CanOvercommit()
{
	return fCanOvercommit;
}


/**
 * @brief Commit or release physical memory (RAM or swap) for this cache.
 *
 * For overcommitting caches a real reservation is deferred until Fault().
 * Only a small pre-committed region is reserved eagerly, and only on the
 * first call. For non-overcommitting caches the difference between @a size
 * and fCommittedSize is reserved or released immediately.
 *
 * @param size     Desired total committed size in bytes.
 * @param priority VM priority (e.g. VM_PRIORITY_USER, VM_PRIORITY_SYSTEM)
 *                 used when reserving memory.
 * @retval B_OK        Commitment adjusted to @a size.
 * @retval B_NO_MEMORY Insufficient physical memory or swap to honour the
 *                     request.
 * @note The cache lock must be held by the caller (AssertLocked is called
 *       internally).
 */
status_t
VMAnonymousCache::Commit(off_t size, int priority)
{
	TRACE("%p->VMAnonymousCache::Commit(%" B_PRIdOFF ")\n", this, size);

	AssertLocked();
	ASSERT_PRINT(size >= (page_count * B_PAGE_SIZE),
		"cache %p @! cache %p", this, this);

	// If we can overcommit, we don't commit here, but in Fault(). We always
	// unreserve memory, if we're asked to shrink our commitment, though.
	if (fCanOvercommit && size > fCommittedSize) {
		if (fHasPrecommitted)
			return B_OK;

		// pre-commit some pages to make a later failure less probable
		fHasPrecommitted = true;
		uint32 precommitted = (fPrecommittedPages * B_PAGE_SIZE);
		if (size > precommitted)
			size = precommitted;

		// pre-commit should not shrink existing commitment
		size += fCommittedSize;
	}

	// Check to see how much we could commit - we need real memory

	if (size > fCommittedSize) {
		// try to commit
		if (vm_try_reserve_memory_or_swap(size - fCommittedSize, priority, 1000000)
				!= B_OK) {
			return B_NO_MEMORY;
		}
	} else {
		// we can release some
		vm_unreserve_memory_or_swap(fCommittedSize - size);
	}

	fCommittedSize = size;
	return B_OK;
}


/**
 * @brief Transfer @a commitment bytes of committed memory from cache @a _from
 *        to this cache without touching the global reservations.
 *
 * Used when splitting or cloning mappings so that the total committed memory
 * in the system remains unchanged.
 *
 * @param _from      Source VMAnonymousCache that currently owns the commitment.
 * @param commitment Bytes of committed memory to transfer; must not exceed
 *                   @a _from->fCommittedSize.
 * @note Both cache locks must be held by the caller (AssertLocked is called on
 *       both caches internally).
 */
void
VMAnonymousCache::TakeCommitmentFrom(VMCache* _from, off_t commitment)
{
	VMAnonymousCache* from = dynamic_cast<VMAnonymousCache*>(_from);
	ASSERT(from != NULL && from->fCommittedSize >= commitment);
	AssertLocked();
	from->AssertLocked();

	from->fCommittedSize -= commitment;
	fCommittedSize += commitment;
}


/**
 * @brief Check whether the backing store (swap) has data for the page at
 *        @a offset.
 *
 * Fast path: returns @c false immediately if no swap space has been allocated
 * for this cache at all. Otherwise queries the swap hash table for the page's
 * slot index.
 *
 * @param offset Byte offset within the cache to query.
 * @return @c true if the page at @a offset has a valid swap slot assigned.
 * @note Acquires sSwapHashLock (read) internally via _SwapBlockGetAddress.
 */
bool
VMAnonymousCache::StoreHasPage(off_t offset)
{
	if (fAllocatedSwapSize == 0)
		return false;

	if (_SwapBlockGetAddress(offset >> PAGE_SHIFT) != SWAP_SLOT_NONE)
		return true;

	return false;
}


/**
 * @brief Debug-only variant of StoreHasPage that bypasses the fast path.
 *
 * Directly looks up the swap hash table without the fAllocatedSwapSize guard,
 * intended for use from the kernel debugger where normal locking is not
 * available.
 *
 * @param offset Byte offset within the cache to query.
 * @return @c true if the swap hash table records a slot for this page.
 * @note Must only be called from the kernel debugger.
 */
bool
VMAnonymousCache::DebugStoreHasPage(off_t offset)
{
	off_t pageIndex = offset >> PAGE_SHIFT;
	swap_hash_key key = { this, pageIndex };
	swap_block* swap = sSwapHashTable.Lookup(key);
	if (swap == NULL)
		return false;

	return swap->swap_slots[pageIndex & SWAP_BLOCK_MASK] != SWAP_SLOT_NONE;
}


/**
 * @brief Read one or more pages from swap into the provided I/O vectors.
 *
 * Groups contiguous swap slots into a single vfs_read_pages call for
 * efficiency. Each group of consecutive slots is dispatched in one I/O
 * operation.
 *
 * @param offset    Byte offset within the cache of the first page to read.
 * @param vecs      Array of generic I/O vectors to fill.
 * @param count     Number of entries in @a vecs.
 * @param flags     I/O flags forwarded to vfs_read_pages.
 * @param _numBytes In/out: on entry the maximum number of bytes to read; on
 *                  exit the number of bytes actually read.
 * @retval B_OK    All pages read successfully.
 * @retval other   VFS error code from the underlying vfs_read_pages call.
 * @note The cache lock must be held by the caller. Swap slots must already be
 *       assigned (i.e. StoreHasPage returned @c true) for each requested page.
 */
status_t
VMAnonymousCache::Read(off_t offset, const generic_io_vec* vecs, size_t count,
	uint32 flags, generic_size_t* _numBytes)
{
	off_t pageIndex = offset >> PAGE_SHIFT;

	for (uint32 i = 0, j = 0; i < count; i = j) {
		swap_addr_t startSlotIndex = _SwapBlockGetAddress(pageIndex + i);
		for (j = i + 1; j < count; j++) {
			swap_addr_t slotIndex = _SwapBlockGetAddress(pageIndex + j);
			if (slotIndex != startSlotIndex + j - i)
				break;
		}

		T(ReadPage(this, pageIndex, startSlotIndex));
			// TODO: Assumes that only one page is read.

		swap_file* swapFile = find_swap_file(startSlotIndex);

		off_t pos = (off_t)(startSlotIndex - swapFile->first_slot)
			* B_PAGE_SIZE;

		status_t status = vfs_read_pages(swapFile->vnode, swapFile->cookie, pos,
			vecs + i, j - i, flags, _numBytes);
		if (status != B_OK)
			return status;
	}

	return B_OK;
}


/**
 * @brief Synchronously write one or more pages to swap.
 *
 * Deallocates any existing swap slots for the affected pages, then reserves
 * new swap space and writes the data. Slot allocation is attempted in
 * power-of-two decreasing chunk sizes until successful. On failure the
 * fAllocatedSwapSize counter is corrected and the partially allocated slots
 * are released.
 *
 * @param offset    Byte offset within the cache of the first page to write.
 * @param vecs      Array of generic I/O vectors containing the page data.
 * @param count     Number of entries in @a vecs.
 * @param flags     I/O flags forwarded to vfs_write_pages.
 * @param _numBytes In/out: on entry the number of bytes to write; on exit the
 *                  number of bytes actually written.
 * @retval B_OK    All pages written to swap successfully.
 * @retval B_ERROR Insufficient unreserved swap space.
 * @retval other   VFS error code from the underlying vfs_write_pages call.
 * @note Acquires and releases the cache lock internally around accounting
 *       updates. The caller must NOT hold the cache lock.
 */
status_t
VMAnonymousCache::Write(off_t offset, const generic_io_vec* vecs, size_t count,
	uint32 flags, generic_size_t* _numBytes)
{
	off_t pageIndex = offset >> PAGE_SHIFT;

	AutoLocker<VMCache> locker(this);

	page_num_t totalPages = 0;
	for (uint32 i = 0; i < count; i++) {
		page_num_t pageCount = (vecs[i].length + B_PAGE_SIZE - 1) >> PAGE_SHIFT;
		swap_addr_t slotIndex = _SwapBlockGetAddress(pageIndex + totalPages);
		if (slotIndex != SWAP_SLOT_NONE) {
			swap_slot_dealloc(slotIndex, pageCount);
			_SwapBlockFree(pageIndex + totalPages, pageCount);
			fAllocatedSwapSize -= pageCount * B_PAGE_SIZE;
		}

		totalPages += pageCount;
	}

	off_t totalSize = totalPages * B_PAGE_SIZE;
	if ((fAllocatedSwapSize + totalSize) > fReservedSwapSize) {
		// Reserve swap space for these pages.
		const off_t difference = fReservedSwapSize - (fAllocatedSwapSize + totalSize);
		const off_t reserved = swap_space_reserve(difference);
		if (reserved != difference) {
			swap_space_unreserve(reserved);
			return B_ERROR;
		}

		fReservedSwapSize += reserved;
	}

	fAllocatedSwapSize += totalSize;
	locker.Unlock();

	page_num_t pagesLeft = totalPages;
	totalPages = 0;

	for (uint32 i = 0; i < count; i++) {
		page_num_t pageCount = (vecs[i].length + B_PAGE_SIZE - 1) >> PAGE_SHIFT;

		generic_addr_t vectorBase = vecs[i].base;
		generic_size_t vectorLength = vecs[i].length;
		page_num_t n = pageCount;

		for (page_num_t j = 0; j < pageCount; j += n) {
			swap_addr_t slotIndex;
			// try to allocate n slots, if fail, try to allocate n/2
			while ((slotIndex = swap_slot_alloc(n)) == SWAP_SLOT_NONE && n >= 2)
				n >>= 1;

			if (slotIndex == SWAP_SLOT_NONE)
				panic("VMAnonymousCache::Write(): can't allocate swap space\n");

			T(WritePage(this, pageIndex, slotIndex));
				// TODO: Assumes that only one page is written.

			swap_file* swapFile = find_swap_file(slotIndex);

			off_t pos = (off_t)(slotIndex - swapFile->first_slot) * B_PAGE_SIZE;

			generic_size_t length = (phys_addr_t)n * B_PAGE_SIZE;
			generic_io_vec vector[1];
			vector->base = vectorBase;
			vector->length = length;

			status_t status = vfs_write_pages(swapFile->vnode, swapFile->cookie,
				pos, vector, 1, flags, &length);
			if (status != B_OK) {
				locker.Lock();
				fAllocatedSwapSize -= (off_t)pagesLeft * B_PAGE_SIZE;
				locker.Unlock();

				swap_slot_dealloc(slotIndex, n);
				return status;
			}

			_SwapBlockBuild(pageIndex + totalPages, slotIndex, n);
			pagesLeft -= n;

			if (n != pageCount) {
				vectorBase = vectorBase + n * B_PAGE_SIZE;
				vectorLength -= n * B_PAGE_SIZE;
			}
		}

		totalPages += pageCount;
	}

	ASSERT(pagesLeft == 0);
	return B_OK;
}


/**
 * @brief Initiate an asynchronous write of a single page to swap.
 *
 * Allocates a swap slot if the page does not already have one, constructs a
 * WriteCallback to record the slot assignment on I/O completion, and submits
 * the write via vfs_asynchronous_write_pages. If allocation or callback
 * construction fails the original @a _callback is invoked with an error so
 * the caller is never left waiting indefinitely.
 *
 * @param offset     Byte offset within the cache of the page to write.
 * @param vecs       I/O vector array (must contain exactly one entry).
 * @param count      Number of I/O vectors (must be 1).
 * @param numBytes   Number of bytes to write (must be <= B_PAGE_SIZE).
 * @param flags      I/O flags; B_VIP_IO_REQUEST selects a higher-priority
 *                   heap allocation for the callback object.
 * @param _callback  Completion callback invoked when the I/O finishes.
 * @retval B_OK        I/O submitted successfully.
 * @retval B_NO_MEMORY Could not allocate the WriteCallback object.
 * @retval B_ERROR     No unreserved swap space available for a new slot.
 * @note The cache lock must NOT be held on entry; it is acquired internally
 *       when updating fAllocatedSwapSize and fReservedSwapSize.
 */
status_t
VMAnonymousCache::WriteAsync(off_t offset, const generic_io_vec* vecs,
	size_t count, generic_size_t numBytes, uint32 flags,
	AsyncIOCallback* _callback)
{
	// TODO: Currently this method is only used for single pages. Either make
	// more flexible use of it or change the interface!
	// This implementation relies on the current usage!
	ASSERT(count == 1);
	ASSERT(numBytes <= B_PAGE_SIZE);

	page_num_t pageIndex = offset >> PAGE_SHIFT;
	swap_addr_t slotIndex = _SwapBlockGetAddress(pageIndex);
	bool newSlot = slotIndex == SWAP_SLOT_NONE;

	// If the page doesn't have any swap space yet, allocate it.
	if (newSlot) {
		AutoLocker<VMCache> locker(this);
		if ((fAllocatedSwapSize + B_PAGE_SIZE) > fReservedSwapSize) {
			if (swap_space_reserve(B_PAGE_SIZE) != B_PAGE_SIZE) {
				_callback->IOFinished(B_ERROR, true, 0);
				return B_ERROR;
			}
			fReservedSwapSize += B_PAGE_SIZE;
		}

		fAllocatedSwapSize += B_PAGE_SIZE;
		slotIndex = swap_slot_alloc(1);
	}

	// create our callback
	WriteCallback* callback = (flags & B_VIP_IO_REQUEST) != 0
		? new(malloc_flags(HEAP_PRIORITY_VIP)) WriteCallback(this, _callback)
		: new(std::nothrow) WriteCallback(this, _callback);
	if (callback == NULL) {
		if (newSlot) {
			AutoLocker<VMCache> locker(this);
			fAllocatedSwapSize -= B_PAGE_SIZE;
			locker.Unlock();

			swap_slot_dealloc(slotIndex, 1);
		}
		_callback->IOFinished(B_NO_MEMORY, true, 0);
		return B_NO_MEMORY;
	}
	// TODO: If the page already had swap space assigned, we don't need an own
	// callback.

	callback->SetTo(pageIndex, slotIndex, newSlot);

	T(WritePage(this, pageIndex, slotIndex));

	// write the page asynchrounously
	swap_file* swapFile = find_swap_file(slotIndex);
	off_t pos = (off_t)(slotIndex - swapFile->first_slot) * B_PAGE_SIZE;

	return vfs_asynchronous_write_pages(swapFile->vnode, swapFile->cookie, pos,
		vecs, 1, numBytes, flags, callback);
}


/**
 * @brief Determine whether the page at @a offset may currently be written to
 *        swap.
 *
 * Returns @c false for pages explicitly marked non-swappable via
 * SetCanSwapPages(). Otherwise returns @c true when reserved or already
 * swapped, or when there is still unreserved global swap space remaining.
 *
 * @param offset Byte offset within the cache of the page to check.
 * @return @c true if the page is eligible for swap-out.
 * @note This is an optimistic check; a later Write() may still fail if the
 *       remaining swap space was concurrently exhausted.
 */
bool
VMAnonymousCache::CanWritePage(off_t offset)
{
	const off_t pageIndex = offset >> PAGE_SHIFT;
	if (fNoSwapPages != NULL && fNoSwapPages->Get(pageIndex))
		return false;

	// We can definitely write the page if we have not used all of our
	// reserved swap space, or the page already has a swap slot assigned.
	if (fAllocatedSwapSize < fReservedSwapSize
			|| _SwapBlockGetAddress(pageIndex) != SWAP_SLOT_NONE)
		 return true;

	// If there is unreserved swap space remaining, assume we'll be able
	// to reserve it. If we can't, we'll just fail later on.
	return atomic_get64(&sAvailableSwapSpace) > B_PAGE_SIZE;
}


/**
 * @brief Return the maximum number of pages that may be written in a single
 *        async write operation.
 *
 * @return Always 1; WriteAsync is currently limited to single-page I/O.
 */
int32
VMAnonymousCache::MaxPagesPerAsyncWrite() const
{
	return 1;
}


/**
 * @brief Handle a page fault for an anonymous mapping.
 *
 * First checks for a guard-page violation (stack overflow). Then, for
 * overcommitting caches, tries to reserve one page of RAM or swap on demand.
 * Pre-committed page credits are consumed before attempting a real reservation.
 * Returning B_BAD_HANDLER tells vm_soft_fault() to proceed with its normal
 * zero-fill or COW logic.
 *
 * @param aspace  Address space in which the fault occurred; used to determine
 *                allocation priority (kernel vs. user).
 * @param offset  Byte offset within the cache at which the fault occurred.
 * @retval B_BAD_HANDLER Normal completion — vm_soft_fault() should continue.
 * @retval B_BAD_ADDRESS Guard page hit (stack overflow).
 * @retval B_NO_MEMORY   Cannot reserve the required page of memory.
 * @note The cache lock must be held by the caller.
 */
status_t
VMAnonymousCache::Fault(struct VMAddressSpace* aspace, off_t offset)
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

	if (fCanOvercommit && LookupPage(offset) == NULL && !StoreHasPage(offset)) {
		if (fPrecommittedPages == 0) {
			// never commit more than needed
			if (fCommittedSize / B_PAGE_SIZE > page_count)
				return B_BAD_HANDLER;

			// try to commit additional memory
			int priority = aspace == VMAddressSpace::Kernel()
				? VM_PRIORITY_SYSTEM : VM_PRIORITY_USER;
			if (vm_try_reserve_memory_or_swap(B_PAGE_SIZE, priority, 0) != B_OK) {
				dprintf("%p->VMAnonymousCache::Fault(): Failed to "
					"reserve %d bytes of RAM.\n", this, (int)B_PAGE_SIZE);
				return B_NO_MEMORY;
			}

			fCommittedSize += B_PAGE_SIZE;
		} else
			fPrecommittedPages--;
	}

	// This will cause vm_soft_fault() to handle the fault
	return B_BAD_HANDLER;
}


/**
 * @brief Merge the source cache into this (consumer) cache after COW is no
 *        longer needed.
 *
 * Called when the source cache has only one consumer left and the area is not
 * shared. Transfers fCommittedSize from the source, trims any excess
 * commitment, moves swap pages via _MergeSwapPages, and finally merges the
 * physical page trees. Uses the faster _MergePagesSmallerConsumer path when
 * the consumer has fewer pages than the source.
 *
 * @param _source Source VMCache to merge into this cache; must be a
 *                VMAnonymousCache.
 * @note Both cache locks must be held. Triggers a panic if @a _source is not
 *       a VMAnonymousCache.
 */
void
VMAnonymousCache::Merge(VMCache* _source)
{
	VMAnonymousCache* source = dynamic_cast<VMAnonymousCache*>(_source);
	if (source == NULL) {
		panic("VMAnonymousCache::Merge(): merge with incompatible cache "
			"%p requested", _source);
		return;
	}

	// take over the source's committed size
	fCommittedSize += source->fCommittedSize;
	source->fCommittedSize = 0;

	off_t actualSize = PAGE_ALIGN(virtual_end - virtual_base);
	if (fCommittedSize > actualSize) {
		vm_unreserve_memory_or_swap(fCommittedSize - actualSize);
		fCommittedSize = actualSize;
	}

	// Move all not shadowed swap pages from the source to the consumer cache.
	// Also remove all source pages that are shadowed by consumer swap pages.
	_MergeSwapPages(source);

	// Move all not shadowed pages from the source to the consumer cache.
	if (source->page_count > page_count
			&& source->virtual_base == virtual_base
			&& source->virtual_end == virtual_end) {
		_MergePagesSmallerConsumer(source);
	} else {
		VMCache::Merge(source);
	}
}


/**
 * @brief Acquire an unreferenced store reference (no-op for anonymous caches).
 *
 * Anonymous caches do not require a persistent backing-store reference, so
 * this method always succeeds immediately.
 *
 * @retval B_OK Always.
 */
status_t
VMAnonymousCache::AcquireUnreferencedStoreRef()
{
	// No reference needed.
	return B_OK;
}


/**
 * @brief Return this cache object to the anonymous-cache object cache.
 *
 * Called by the last VMCache::ReleaseRef when the reference count reaches
 * zero. Delegates to object_cache_delete on gAnonymousCacheObjectCache.
 *
 * @note Must not be called directly; it is invoked by the VMCache reference-
 *       counting machinery.
 */
void
VMAnonymousCache::DeleteObject()
{
	object_cache_delete(gAnonymousCacheObjectCache, this);
}


/**
 * @brief Record swap slot assignments for @a count pages starting at
 *        @a startPageIndex.
 *
 * Looks up (or allocates) the swap_block covering each page in the range and
 * stores the corresponding slot index. If the object cache is temporarily
 * exhausted the function drops and re-acquires sSwapHashLock and retries.
 *
 * @param startPageIndex First page index in the cache to record.
 * @param startSlotIndex First swap slot index assigned to @a startPageIndex.
 * @param count          Number of consecutive pages/slots to record.
 * @note Acquires sSwapHashLock (write) internally.
 */
void
VMAnonymousCache::_SwapBlockBuild(off_t startPageIndex,
	swap_addr_t startSlotIndex, uint32 count)
{
	WriteLocker locker(sSwapHashLock);

	uint32 left = count;
	for (uint32 i = 0, j = 0; i < count; i += j) {
		off_t pageIndex = startPageIndex + i;
		swap_addr_t slotIndex = startSlotIndex + i;

		swap_hash_key key = { this, pageIndex };

		swap_block* swap = sSwapHashTable.Lookup(key);
		while (swap == NULL) {
			swap = (swap_block*)object_cache_alloc(sSwapBlockCache,
				CACHE_DONT_WAIT_FOR_MEMORY | CACHE_DONT_LOCK_KERNEL_SPACE);
			if (swap == NULL) {
				// Wait a short time until memory is available again.
				locker.Unlock();
				snooze(10000);
				locker.Lock();
				swap = sSwapHashTable.Lookup(key);
				continue;
			}

			swap->key.cache = this;
			swap->key.page_index = pageIndex & ~(off_t)SWAP_BLOCK_MASK;
			swap->used = 0;
			for (uint32 i = 0; i < SWAP_BLOCK_PAGES; i++)
				swap->swap_slots[i] = SWAP_SLOT_NONE;

			sSwapHashTable.InsertUnchecked(swap);
		}

		swap_addr_t blockIndex = pageIndex & SWAP_BLOCK_MASK;
		for (j = 0; blockIndex < SWAP_BLOCK_PAGES && left > 0; j++) {
			swap->swap_slots[blockIndex++] = slotIndex + j;
			left--;
		}

		swap->used += j;
	}
}


/**
 * @brief Clear swap slot records for @a count pages starting at
 *        @a startPageIndex.
 *
 * Walks the affected swap_blocks, sets each slot to SWAP_SLOT_NONE, and frees
 * any block whose used count drops to zero.
 *
 * @param startPageIndex First page index whose swap record should be cleared.
 * @param count          Number of consecutive page records to clear.
 * @note Acquires sSwapHashLock (write) internally. The corresponding physical
 *       swap slots must be freed separately via swap_slot_dealloc.
 */
void
VMAnonymousCache::_SwapBlockFree(off_t startPageIndex, uint32 count)
{
	WriteLocker locker(sSwapHashLock);

	uint32 left = count;
	for (uint32 i = 0, j = 0; i < count; i += j) {
		off_t pageIndex = startPageIndex + i;
		swap_hash_key key = { this, pageIndex };
		swap_block* swap = sSwapHashTable.Lookup(key);

		ASSERT(swap != NULL);

		swap_addr_t blockIndex = pageIndex & SWAP_BLOCK_MASK;
		for (j = 0; blockIndex < SWAP_BLOCK_PAGES && left > 0; j++) {
			swap->swap_slots[blockIndex++] = SWAP_SLOT_NONE;
			left--;
		}

		swap->used -= j;
		if (swap->used == 0) {
			sSwapHashTable.RemoveUnchecked(swap);
			object_cache_free(sSwapBlockCache, swap,
				CACHE_DONT_WAIT_FOR_MEMORY | CACHE_DONT_LOCK_KERNEL_SPACE);
		}
	}
}


/**
 * @brief Look up the swap slot index assigned to page @a pageIndex.
 *
 * @param pageIndex Page index (cache offset >> PAGE_SHIFT) to look up.
 * @return The swap slot index, or SWAP_SLOT_NONE if no slot is assigned.
 * @note Acquires sSwapHashLock (read) internally.
 */
swap_addr_t
VMAnonymousCache::_SwapBlockGetAddress(off_t pageIndex)
{
	ReadLocker locker(sSwapHashLock);

	swap_hash_key key = { this, pageIndex };
	swap_block* swap = sSwapHashTable.Lookup(key);
	swap_addr_t slotIndex = SWAP_SLOT_NONE;

	if (swap != NULL) {
		swap_addr_t blockIndex = pageIndex & SWAP_BLOCK_MASK;
		slotIndex = swap->swap_slots[blockIndex];
	}

	return slotIndex;
}


/**
 * @brief Merge source pages into the consumer when the consumer has fewer
 *        pages (optimised path).
 *
 * Moves each consumer page to the source (freeing any shadowed source page),
 * then bulk-moves all source pages back to the consumer. This avoids an O(n)
 * scan of the larger source tree.
 *
 * @param source Source VMAnonymousCache whose pages are being merged into
 *               this cache.
 * @note Both cache locks must be held. Some pages may be busy during the move;
 *       this is safe because all pages will belong to the consumer once the
 *       lock is released.
 */
void
VMAnonymousCache::_MergePagesSmallerConsumer(VMAnonymousCache* source)
{
	// The consumer (this cache) has less pages than the source, so we move the
	// consumer's pages to the source (freeing shadowed ones) and finally just
	// all pages of the source back to the consumer.

	// It is possible that some of the pages we are moving here are actually "busy".
	// Since all the pages that belong to this cache will belong to it again by
	// the time we unlock, that should be fine.

	VMCachePagesTree::Iterator it = pages.GetIterator();
	vm_page_reservation reservation = {};
	while (vm_page* page = it.Next()) {
		// If a source page is in the way, remove and free it.
		vm_page* sourcePage = source->LookupPage(
			(off_t)page->cache_offset << PAGE_SHIFT);
		if (sourcePage != NULL) {
			DEBUG_PAGE_ACCESS_START(sourcePage);
			ASSERT_PRINT(!sourcePage->busy, "page: %p", sourcePage);
			ASSERT_PRINT(sourcePage->WiredCount() == 0
					&& sourcePage->mappings.IsEmpty(),
				"sourcePage: %p, page: %p", sourcePage, page);
			source->RemovePage(sourcePage);
			vm_page_free_etc(source, sourcePage, &reservation);
		}

		// Note: Removing the current node while iterating through a
		// IteratableSplayTree is safe.
		source->MovePage(page);
	}
	vm_page_unreserve_pages(&reservation);

	MoveAllPages(source);
}


/**
 * @brief Merge swap pages from @a source into this (consumer) cache.
 *
 * For each swap block in the source, any source swap page that is shadowed by
 * a consumer page or consumer swap page is freed. Remaining source swap pages
 * are transferred to the consumer by re-keying the source swap block or by
 * copying individual slot entries into an existing consumer block.
 *
 * @param source Source VMAnonymousCache whose swap pages are merged into this
 *               cache.
 * @note Both cache locks must be held. sSwapHashLock (write) is acquired
 *       per swap-block iteration.
 */
void
VMAnonymousCache::_MergeSwapPages(VMAnonymousCache* source)
{
	// If neither source nor consumer have swap pages, we don't have to do
	// anything.
	if (source->fAllocatedSwapSize == 0 && fAllocatedSwapSize == 0)
		return;

	for (off_t offset = source->virtual_base
		& ~(off_t)(B_PAGE_SIZE * SWAP_BLOCK_PAGES - 1);
		offset < source->virtual_end;
		offset += B_PAGE_SIZE * SWAP_BLOCK_PAGES) {

		WriteLocker locker(sSwapHashLock);

		off_t swapBlockPageIndex = offset >> PAGE_SHIFT;
		swap_hash_key key = { source, swapBlockPageIndex };
		swap_block* sourceSwapBlock = sSwapHashTable.Lookup(key);

		// remove the source swap block -- we will either take over the swap
		// space (and the block) or free it
		if (sourceSwapBlock != NULL)
			sSwapHashTable.RemoveUnchecked(sourceSwapBlock);

		key.cache = this;
		swap_block* swapBlock = sSwapHashTable.Lookup(key);

		locker.Unlock();

		// remove all source pages that are shadowed by consumer swap pages
		if (swapBlock != NULL) {
			for (uint32 i = 0; i < SWAP_BLOCK_PAGES; i++) {
				if (swapBlock->swap_slots[i] != SWAP_SLOT_NONE) {
					vm_page* page = source->LookupPage(
						(off_t)(swapBlockPageIndex + i) << PAGE_SHIFT);
					if (page != NULL) {
						DEBUG_PAGE_ACCESS_START(page);
						ASSERT_PRINT(!page->busy, "page: %p", page);
						source->RemovePage(page);
						vm_page_free(source, page);
					}
				}
			}
		}

		if (sourceSwapBlock == NULL)
			continue;

		for (uint32 i = 0; i < SWAP_BLOCK_PAGES; i++) {
			off_t pageIndex = swapBlockPageIndex + i;
			swap_addr_t sourceSlotIndex = sourceSwapBlock->swap_slots[i];

			if (sourceSlotIndex == SWAP_SLOT_NONE)
				continue;

			if ((swapBlock != NULL
					&& swapBlock->swap_slots[i] != SWAP_SLOT_NONE)
				|| LookupPage((off_t)pageIndex << PAGE_SHIFT) != NULL) {
				// The consumer already has a page or a swapped out page
				// at this index. So we can free the source swap space.
				swap_slot_dealloc(sourceSlotIndex, 1);
				sourceSwapBlock->swap_slots[i] = SWAP_SLOT_NONE;
				sourceSwapBlock->used--;
			}

			// We've either freed the source swap page or are going to move it
			// to the consumer. At any rate, the source cache doesn't own it
			// anymore.
			source->fAllocatedSwapSize -= B_PAGE_SIZE;
		}

		// All source swap pages that have not been freed yet are taken over by
		// the consumer.
		const off_t adoptedSwapSize = B_PAGE_SIZE * (off_t)sourceSwapBlock->used;
		fAllocatedSwapSize += adoptedSwapSize;
		fReservedSwapSize += adoptedSwapSize;
		source->fReservedSwapSize -= adoptedSwapSize;

		if (sourceSwapBlock->used == 0) {
			// All swap pages have been freed -- we can discard the source swap
			// block.
			object_cache_free(sSwapBlockCache, sourceSwapBlock,
				CACHE_DONT_WAIT_FOR_MEMORY | CACHE_DONT_LOCK_KERNEL_SPACE);
		} else if (swapBlock == NULL) {
			// We need to take over some of the source's swap pages and there's
			// no swap block in the consumer cache. Just take over the source
			// swap block.
			sourceSwapBlock->key.cache = this;
			locker.Lock();
			sSwapHashTable.InsertUnchecked(sourceSwapBlock);
			locker.Unlock();
		} else {
			// We need to take over some of the source's swap pages and there's
			// already a swap block in the consumer cache. Copy the respective
			// swap addresses and discard the source swap block.
			for (uint32 i = 0; i < SWAP_BLOCK_PAGES; i++) {
				if (sourceSwapBlock->swap_slots[i] != SWAP_SLOT_NONE)
					swapBlock->swap_slots[i] = sourceSwapBlock->swap_slots[i];
			}

			object_cache_free(sSwapBlockCache, sourceSwapBlock,
				CACHE_DONT_WAIT_FOR_MEMORY | CACHE_DONT_LOCK_KERNEL_SPACE);
		}
	}
}


// #pragma mark -


// TODO: This can be removed if we get BFS uuid's
struct VolumeInfo {
	char name[B_FILE_NAME_LENGTH];
	char device[B_FILE_NAME_LENGTH];
	char filesystem[B_OS_NAME_LENGTH];
	off_t capacity;
};


class PartitionScorer : public KPartitionVisitor {
public:
	PartitionScorer(VolumeInfo& volumeInfo)
		:
		fBestPartition(NULL),
		fBestScore(-1),
		fVolumeInfo(volumeInfo)
	{
	}

	virtual bool VisitPre(KPartition* partition)
	{
		if (!partition->ContainsFileSystem())
			return false;

		KPath path;
		partition->GetPath(&path);

		int score = 0;
		if (strcmp(fVolumeInfo.name, partition->ContentName()) == 0)
			score += 4;
		if (strcmp(fVolumeInfo.device, path.Path()) == 0)
			score += 3;
		if (fVolumeInfo.capacity == partition->Size())
			score += 2;
		if (strcmp(fVolumeInfo.filesystem,
			partition->DiskSystem()->ShortName()) == 0) {
			score += 1;
		}
		if (score >= 4 && score > fBestScore) {
			fBestPartition = partition;
			fBestScore = score;
		}

		return false;
	}

	KPartition* fBestPartition;

private:
	int32		fBestScore;
	VolumeInfo&	fVolumeInfo;
};


/**
 * @brief Register a file or block device as an active swap area.
 *
 * Opens @a path with O_NOCACHE, validates that it is a regular file, character
 * device, or block device with at least one page of capacity, then allocates a
 * swap_file descriptor and a radix bitmap for slot tracking. The file is
 * appended to sSwapFileList and its capacity is added to sAvailableSwapSpace.
 *
 * @param path Filesystem path of the swap file or device.
 * @retval B_OK           Swap file registered and available for use.
 * @retval B_BAD_VALUE    @a path is not a supported file type or is too small.
 * @retval B_NO_MEMORY    Allocation of the swap_file or radix_bitmap failed.
 * @retval errno          An OS error returned by open() or fstat().
 * @note Must not be called at interrupt level. Acquires sSwapFileListLock and
 *       sAvailableSwapSpaceLock internally.
 */
status_t
swap_file_add(const char* path)
{
	// open the file
	int fd = open(path, O_RDWR | O_NOCACHE, S_IRUSR | S_IWUSR);
	if (fd < 0)
		return errno;

	// fstat() it and check whether we can use it
	struct stat st;
	if (fstat(fd, &st) < 0) {
		close(fd);
		return errno;
	}

	if (!(S_ISREG(st.st_mode) || S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))) {
		close(fd);
		return B_BAD_VALUE;
	}

	if (st.st_size < B_PAGE_SIZE) {
		close(fd);
		return B_BAD_VALUE;
	}

	// get file descriptor, vnode, and cookie
	file_descriptor* descriptor = get_fd(get_current_io_context(true), fd);
	put_fd(descriptor);

	vnode* node = fd_vnode(descriptor);
	if (node == NULL) {
		close(fd);
		return B_BAD_VALUE;
	}

	// do the allocations and prepare the swap_file structure
	swap_file* swap = new(std::nothrow) swap_file;
	if (swap == NULL) {
		close(fd);
		return B_NO_MEMORY;
	}

	swap->fd = fd;
	swap->vnode = node;
	swap->cookie = descriptor->cookie;

	uint32 pageCount = st.st_size >> PAGE_SHIFT;
	swap->bmp = radix_bitmap_create(pageCount);
	if (swap->bmp == NULL) {
		delete swap;
		close(fd);
		return B_NO_MEMORY;
	}

	// set slot index and add this file to swap file list
	mutex_lock(&sSwapFileListLock);
	// TODO: Also check whether the swap file is already registered!
	if (sSwapFileList.IsEmpty()) {
		swap->first_slot = 0;
		swap->last_slot = pageCount;
	} else {
		// leave one page gap between two swap files
		swap->first_slot = sSwapFileList.Last()->last_slot + 1;
		swap->last_slot = swap->first_slot + pageCount;
	}
	sSwapFileList.Add(swap);
	sSwapFileCount++;
	mutex_unlock(&sSwapFileListLock);

	mutex_lock(&sAvailableSwapSpaceLock);
	sAvailableSwapSpace += (off_t)pageCount * B_PAGE_SIZE;
	mutex_unlock(&sAvailableSwapSpaceLock);

	vm_unreserve_memory_or_swap((off_t)pageCount * B_PAGE_SIZE);
	return B_OK;
}


/**
 * @brief Unregister and remove a previously added swap file.
 *
 * Locates the swap_file by resolving @a path to a vnode, verifies that all
 * of its slots are free, reserves back the equivalent RAM, removes the entry
 * from sSwapFileList, truncates the file to zero, and closes the descriptor.
 *
 * @param path Filesystem path of the swap file to remove.
 * @retval B_OK        Swap file successfully deregistered and closed.
 * @retval B_ERROR     @a path is not a registered swap file, or the file still
 *                     has pages in use.
 * @retval B_NO_MEMORY Could not reserve back the equivalent RAM.
 * @note Acquires sSwapFileListLock and sAvailableSwapSpaceLock internally.
 */
status_t
swap_file_delete(const char* path)
{
	vnode* node = NULL;
	status_t status = vfs_get_vnode_from_path(path, true, &node);
	if (status != B_OK)
		return status;

	MutexLocker locker(sSwapFileListLock);

	swap_file* swapFile = NULL;
	for (SwapFileList::Iterator it = sSwapFileList.GetIterator();
			(swapFile = it.Next()) != NULL;) {
		if (swapFile->vnode == node)
			break;
	}

	vfs_put_vnode(node);

	if (swapFile == NULL)
		return B_ERROR;

	// if this file is currently used, we can't delete
	// TODO: mark this swap file deleting, and remove it after releasing
	// all the swap space
	const off_t swapFileSize = (off_t)(swapFile->last_slot - swapFile->first_slot);
	if (swapFile->bmp->free_slots < swapFileSize)
		return B_ERROR;

	if (vm_try_reserve_memory_or_swap(swapFileSize, VM_PRIORITY_SYSTEM, 5 * 1000 * 1000) != B_OK)
		return B_NO_MEMORY;

	sSwapFileList.Remove(swapFile);
	sSwapFileCount--;
	locker.Unlock();

	mutex_lock(&sAvailableSwapSpaceLock);
	sAvailableSwapSpace -= swapFileSize * B_PAGE_SIZE;
	mutex_unlock(&sAvailableSwapSpaceLock);

	truncate(path, 0);
	close(swapFile->fd);
	radix_bitmap_destroy(swapFile->bmp);
	delete swapFile;

	return B_OK;
}


/**
 * @brief Initialise the swap subsystem during early kernel boot.
 *
 * Creates the sSwapBlockCache object cache, initialises the swap hash table
 * and its reader-writer lock, registers the periodic hash resizer daemon,
 * initialises the swap file list and available-space tracking, and registers
 * the "swap" kernel debugger command.
 *
 * @note Called once during kernel initialisation before any swap files are
 *       added. Must not be called again.
 */
void
swap_init(void)
{
	// create swap block cache
	sSwapBlockCache = create_object_cache("swapblock", sizeof(swap_block), 0);
	if (sSwapBlockCache == NULL)
		panic("swap_init(): can't create object cache for swap blocks\n");

	status_t error = object_cache_set_minimum_reserve(sSwapBlockCache,
		MIN_SWAP_BLOCK_RESERVE);
	if (error != B_OK) {
		panic("swap_init(): object_cache_set_minimum_reserve() failed: %s",
			strerror(error));
	}

	// init swap hash table
	sSwapHashTable.Init(INITIAL_SWAP_HASH_SIZE);
	rw_lock_init(&sSwapHashLock, "swaphash");

	error = register_resource_resizer(swap_hash_resizer, NULL,
		SWAP_HASH_RESIZE_INTERVAL);
	if (error != B_OK) {
		panic("swap_init(): Failed to register swap hash resizer: %s",
			strerror(error));
	}

	// init swap file list
	mutex_init(&sSwapFileListLock, "swaplist");
	sSwapFileAlloc = NULL;
	sSwapFileCount = 0;

	// init available swap space
	mutex_init(&sAvailableSwapSpaceLock, "avail swap space");
	sAvailableSwapSpace = 0;

	add_debugger_command_etc("swap", &dump_swap_info,
		"Print infos about the swap usage",
		"\n"
		"Print infos about the swap usage.\n", 0);
}


/**
 * @brief Create and register the default swap file after kernel modules are
 *        loaded.
 *
 * Reads virtual_memory driver settings to determine whether swap is enabled,
 * automatic or manually configured, the target device, and the desired size.
 * For automatic swap the size is proportional to physical RAM (doubled when
 * RAM is under 1 GB, capped at 25% of free space on the target device). For
 * manual swap the configured partition is located via PartitionScorer. The
 * swap file is created (or resized) at the determined path and registered with
 * swap_file_add().
 *
 * @note Called once, after all disk-device modules have been initialised.
 *       Silently returns if booted from a read-only device.
 */
void
swap_init_post_modules()
{
	// Never try to create a swap file on a read-only device - when booting
	// from CD, the write overlay is used.
	if (gReadOnlyBootDevice)
		return;

	bool swapEnabled = true;
	bool swapAutomatic = true;
	off_t swapSize = 0;

	dev_t swapDeviceID = -1;
	VolumeInfo selectedVolume = {};

	void* settings = load_driver_settings("virtual_memory");

	if (settings != NULL) {
		// We pass a lot of information on the swap device, this is mostly to
		// ensure that we are dealing with the same device that was configured.

		// TODO: Some kind of BFS uuid would be great here :)
		const char* enabled = get_driver_parameter(settings, "vm", NULL, NULL);

		if (enabled != NULL) {
			swapEnabled = get_driver_boolean_parameter(settings, "vm",
				true, false);
			swapAutomatic = get_driver_boolean_parameter(settings, "swap_auto",
				true, false);

			if (swapEnabled && !swapAutomatic) {
				const char* size = get_driver_parameter(settings, "swap_size",
					NULL, NULL);
				const char* volume = get_driver_parameter(settings,
					"swap_volume_name", NULL, NULL);
				const char* device = get_driver_parameter(settings,
					"swap_volume_device", NULL, NULL);
				const char* filesystem = get_driver_parameter(settings,
					"swap_volume_filesystem", NULL, NULL);
				const char* capacity = get_driver_parameter(settings,
					"swap_volume_capacity", NULL, NULL);

				if (size != NULL && device != NULL && volume != NULL
					&& filesystem != NULL && capacity != NULL) {
					// User specified a size / volume that seems valid
					swapAutomatic = false;
					swapSize = atoll(size);
					strlcpy(selectedVolume.name, volume,
						sizeof(selectedVolume.name));
					strlcpy(selectedVolume.device, device,
						sizeof(selectedVolume.device));
					strlcpy(selectedVolume.filesystem, filesystem,
						sizeof(selectedVolume.filesystem));
					selectedVolume.capacity = atoll(capacity);
				} else {
					// Something isn't right with swap config, go auto
					swapAutomatic = true;
					dprintf("%s: virtual_memory configuration is invalid, "
						"using automatic swap\n", __func__);
				}
			}
		}
		unload_driver_settings(settings);
	}

	if (swapAutomatic) {
		swapSize = (off_t)vm_page_num_pages() * B_PAGE_SIZE;
		if (swapSize <= (1024 * 1024 * 1024)) {
			// Memory under 1GB? double the swap
			swapSize *= 2;
		}
		// Automatic swap defaults to the boot device
		swapDeviceID = gBootDevice;
	}

	if (!swapEnabled || swapSize < B_PAGE_SIZE) {
		dprintf("%s: virtual_memory is disabled\n", __func__);
		truncate(kDefaultSwapPath, 0);
		return;
	}

	if (!swapAutomatic && swapDeviceID < 0) {
		// If user-specified swap, and no swap device has been chosen yet...
		KDiskDeviceManager::CreateDefault();
		KDiskDeviceManager* manager = KDiskDeviceManager::Default();
		PartitionScorer visitor(selectedVolume);

		KDiskDevice* device;
		int32 cookie = 0;
		while ((device = manager->NextDevice(&cookie)) != NULL) {
			if (device->IsReadOnlyMedia() || device->IsWriteOnce()
				|| device->IsRemovable()) {
				continue;
			}
			device->VisitEachDescendant(&visitor);
		}

		if (!visitor.fBestPartition) {
			dprintf("%s: Can't find configured swap partition '%s'\n",
				__func__, selectedVolume.name);
		} else {
			if (visitor.fBestPartition->IsMounted())
				swapDeviceID = visitor.fBestPartition->VolumeID();
			else {
				KPath devPath, mountPoint;
				visitor.fBestPartition->GetPath(&devPath);
				visitor.fBestPartition->GetMountPoint(&mountPoint);
				const char* mountPath = mountPoint.Path();
				mkdir(mountPath, S_IRWXU | S_IRWXG | S_IRWXO);
				swapDeviceID = _kern_mount(mountPath, devPath.Path(),
					NULL, 0, NULL, 0);
				if (swapDeviceID < 0) {
					dprintf("%s: Can't mount configured swap partition '%s'\n",
						__func__, selectedVolume.name);
				}
			}
		}
	}

	if (swapDeviceID < 0)
		swapDeviceID = gBootDevice;

	// We now have a swapDeviceID which is used for the swap file

	KPath path;
	struct fs_info info;
	_kern_read_fs_info(swapDeviceID, &info);
	if (swapDeviceID == gBootDevice)
		path = kDefaultSwapPath;
	else {
		vfs_entry_ref_to_path(info.dev, info.root, ".", true, path.LockBuffer(),
			path.BufferSize());
		path.UnlockBuffer();
		path.Append("swap");
	}

	const char* swapPath = path.Path();

	// Swap size limits prevent oversized swap files
	if (swapAutomatic) {
		off_t existingSwapSize = 0;
		struct stat existingSwapStat;
		if (stat(swapPath, &existingSwapStat) == 0)
			existingSwapSize = existingSwapStat.st_size;

		off_t freeSpace = info.free_blocks * info.block_size + existingSwapSize;

		// Adjust automatic swap to a maximum of 25% of the free space
		if (swapSize > (freeSpace / 4))
			swapSize = (freeSpace / 4);
	}

	// Create swap file
	int fd = open(swapPath, O_RDWR | O_CREAT | O_NOCACHE, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		dprintf("%s: Can't open/create %s: %s\n", __func__,
			swapPath, strerror(errno));
		return;
	}

	struct stat stat;
	stat.st_size = swapSize;
	status_t error = _kern_write_stat(fd, NULL, false, &stat,
		sizeof(struct stat), B_STAT_SIZE | B_STAT_SIZE_INSECURE);
	if (error != B_OK) {
		dprintf("%s: Failed to resize %s to %" B_PRIdOFF " bytes: %s\n",
			__func__, swapPath, swapSize, strerror(error));
	}

	close(fd);

	error = swap_file_add(swapPath);
	if (error != B_OK) {
		dprintf("%s: Failed to add swap file %s: %s\n", __func__, swapPath,
			strerror(error));
	}
}


/**
 * @brief Free the swap slot of a single physical page (called by the page
 *        daemon).
 *
 * If @a page belongs to a VMAnonymousCache and has a swap slot assigned,
 * deallocates that slot and removes the record from the swap hash table,
 * allowing the daemon to reclaim the page from RAM without data loss.
 *
 * @param page Physical page whose swap slot should be freed.
 * @return @c true  if a swap slot was found and freed.
 * @return @c false if @a page does not belong to a VMAnonymousCache or has no
 *                  swap slot.
 * @note Called with the page's cache lock held by the page daemon.
 */
bool
swap_free_page_swap_space(vm_page* page)
{
	VMAnonymousCache* cache = dynamic_cast<VMAnonymousCache*>(page->Cache());
	if (cache == NULL)
		return false;

	swap_addr_t slotIndex = cache->_SwapBlockGetAddress(page->cache_offset);
	if (slotIndex == SWAP_SLOT_NONE)
		return false;

	swap_slot_dealloc(slotIndex, 1);
	cache->fAllocatedSwapSize -= B_PAGE_SIZE;
	cache->_SwapBlockFree(page->cache_offset, 1);

	return true;
}


/**
 * @brief Return the total number of swap slots across all registered swap
 *        files.
 *
 * @return Total page-sized slot count summed over all swap_file entries.
 * @note Acquires sSwapFileListLock internally.
 */
uint32
swap_total_swap_pages()
{
	mutex_lock(&sSwapFileListLock);

	uint32 totalSwapSlots = 0;
	for (SwapFileList::Iterator it = sSwapFileList.GetIterator();
		swap_file* swapFile = it.Next();) {
		totalSwapSlots += swapFile->last_slot - swapFile->first_slot;
	}

	mutex_unlock(&sSwapFileListLock);

	return totalSwapSlots;
}


#endif	// ENABLE_SWAP_SUPPORT


/**
 * @brief Populate the swap-related fields of a system_info structure.
 *
 * Accumulates max_swap_pages and free_swap_pages from all registered swap
 * files. When swap support is compiled out both fields are set to zero.
 *
 * @param info Pointer to the system_info structure to fill; the swap fields
 *             are updated in place (not zeroed first).
 * @note Acquires sSwapFileListLock internally when ENABLE_SWAP_SUPPORT is set.
 */
void
swap_get_info(system_info* info)
{
#if ENABLE_SWAP_SUPPORT
	MutexLocker locker(sSwapFileListLock);
	for (SwapFileList::Iterator it = sSwapFileList.GetIterator();
		swap_file* swapFile = it.Next();) {
		info->max_swap_pages += swapFile->last_slot - swapFile->first_slot;
		info->free_swap_pages += swapFile->bmp->free_slots;
	}
#else
	info->max_swap_pages = 0;
	info->free_swap_pages = 0;
#endif
}
