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
 *   Copyright 2004-2008, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/** @file VMAnonymousCache.h
 *  @brief VMCache subclass for anonymous (heap/stack) memory with swap support. */

#ifndef _KERNEL_VM_STORE_ANONYMOUS_H
#define _KERNEL_VM_STORE_ANONYMOUS_H


#include <vm/VMCache.h>


#if ENABLE_SWAP_SUPPORT

/** @brief Index of a slot in the swap area. */
typedef uint32 swap_addr_t;
	// TODO: Should be wider, but RadixBitmap supports only a 32 bit type ATM!
struct swap_block;
struct system_memory_info;
namespace BKernel { class Bitmap; }


extern "C" {
	/** @brief Initialises the swap subsystem early during VM bring-up. */
	void swap_init(void);
	/** @brief Finishes swap initialisation once the module subsystem is up. */
	void swap_init_post_modules(void);
	/** @brief Releases the swap slot backing @p page, if any.
	 *  @return True if a swap slot was freed. */
	bool swap_free_page_swap_space(vm_page* page);
	/** @brief Returns the total number of pages of swap space currently configured. */
	uint32 swap_total_swap_pages(void);
}


/** @brief Anonymous memory cache (heap, stack, BSS) with optional swap backing.
 *
 * Pages in this cache have no file backing — when memory pressure forces
 * eviction, dirty pages are written to swap if available, otherwise the
 * cache simply commits up front and refuses to overcommit. Supports guard
 * pages, precommitted regions, and exclusion of selected ranges from swap. */
class VMAnonymousCache final : public VMCache {
public:
	/** @brief Releases swap reservations and any other per-instance state. */
	virtual						~VMAnonymousCache();

	/** @brief Initialises the anonymous cache.
	 *  @param canOvercommit       If true, the cache may report uncommitted memory as available.
	 *  @param numPrecommittedPages Pages whose backing store is reserved up front.
	 *  @param numGuardPages       Number of unmapped guard pages reserved at the start of the cache.
	 *  @param allocationFlags     VM allocation flags forwarded to the base class.
	 *  @return B_OK on success, or an error code on failure. */
			status_t			Init(bool canOvercommit,
									int32 numPrecommittedPages,
									int32 numGuardPages,
									uint32 allocationFlags);

	/** @brief Marks a byte range as eligible (or not) for swap-out.
	 *  @param base    First byte of the range.
	 *  @param size    Length of the range in bytes.
	 *  @param canSwap If false, pages in the range will be locked in memory.
	 *  @return B_OK on success, or an error code on failure. */
			status_t			SetCanSwapPages(off_t base, size_t size, bool canSwap);

	/** @brief Grows or shrinks the cache, adjusting commit and swap reservations. */
	virtual	status_t			Resize(off_t newSize, int priority);
	/** @brief Shifts the cache's offset base while preserving its committed size. */
	virtual	status_t			Rebase(off_t newBase, int priority);
	/** @brief Moves a sub-range of pages and commitment from @p source into this cache. */
	virtual	status_t			Adopt(VMCache* source, off_t offset,
									off_t size, off_t newOffset);

	/** @brief Discards a range of pages, releasing both memory and swap. */
	virtual	ssize_t				Discard(off_t offset, off_t size);

	/** @brief Returns the cache's current commitment in bytes. */
	virtual	off_t				Commitment() const;
	/** @brief Returns true if the cache is permitted to overcommit. */
	virtual	bool				CanOvercommit();
	/** @brief Adjusts the cache commitment to at least @p size. */
	virtual	status_t			Commit(off_t size, int priority);
	/** @brief Transfers @p commitment bytes from another cache to this one. */
	virtual	void				TakeCommitmentFrom(VMCache* from, off_t commitment);

	/** @brief Returns true if the page at @p offset has a backing slot. */
	virtual	bool				StoreHasPage(off_t offset);
	/** @brief Debug-only StoreHasPage() variant safe to call without the cache lock. */
	virtual	bool				DebugStoreHasPage(off_t offset);

	/** @brief Returns the configured guard region size in bytes. */
	virtual	int32				GuardSize()	{ return fGuardedSize; }
	/** @brief Sets the guard region size in bytes. */
	virtual	void				SetGuardSize(int32 guardSize)
									{ fGuardedSize = guardSize; }

	/** @brief Reads pages from swap into the supplied I/O vectors. */
	virtual	status_t			Read(off_t offset, const generic_io_vec* vecs,
									size_t count, uint32 flags,
									generic_size_t* _numBytes);
	/** @brief Synchronously writes pages out to swap. */
	virtual	status_t			Write(off_t offset, const generic_io_vec* vecs,
									size_t count, uint32 flags,
									generic_size_t* _numBytes);
	/** @brief Asynchronously writes pages out to swap.
	 *  @param callback Invoked when the asynchronous write completes. */
	virtual	status_t			WriteAsync(off_t offset,
									const generic_io_vec* vecs, size_t count,
									generic_size_t numBytes, uint32 flags,
									AsyncIOCallback* callback);
	/** @brief Returns true if the page at @p offset may be written to swap. */
	virtual	bool				CanWritePage(off_t offset);

	/** @brief Maximum pages that may be batched into a single async swap-out. */
	virtual	int32				MaxPagesPerAsyncWrite() const;

	/** @brief Handles a page fault by allocating or paging in an anonymous page. */
	virtual	status_t			Fault(struct VMAddressSpace* aspace,
									off_t offset);

	/** @brief Merges @p source into this cache, including its swap pages. */
	virtual	void				Merge(VMCache* source);

	/** @brief Acquires a store reference without holding the cache lock. */
	virtual	status_t			AcquireUnreferencedStoreRef();

protected:
	/** @brief Releases per-instance resources before the object is freed. */
	virtual	void				DeleteObject();

private:
			class WriteCallback;
			friend class WriteCallback;

			void				_SwapBlockBuild(off_t pageIndex,
									swap_addr_t slotIndex, uint32 count);
			void				_SwapBlockFree(off_t pageIndex, uint32 count);
			swap_addr_t			_SwapBlockGetAddress(off_t pageIndex);

			void				_MergePagesSmallerConsumer(
									VMAnonymousCache* source);
			void				_MergeSwapPages(VMAnonymousCache* source);

			void				_FreeSwapPageRange(off_t fromOffset,
									off_t toOffset, bool skipBusyPages = true);

private:
	friend bool swap_free_page_swap_space(vm_page* page);

			off_t				fCommittedSize;       /**< Bytes currently committed by this cache. */
			bool				fCanOvercommit;       /**< True if overcommit is permitted. */
			bool				fHasPrecommitted;     /**< True if precommitted pages have been reserved. */
			uint8				fPrecommittedPages;   /**< Number of precommitted pages, if @c fHasPrecommitted. */
			int32				fGuardedSize;         /**< Bytes of guard region at the cache's start. */
			BKernel::Bitmap*	fNoSwapPages;         /**< Bitmap marking pages excluded from swap. */
			off_t				fReservedSwapSize;    /**< Swap bytes reserved but not yet allocated. */
			off_t				fAllocatedSwapSize;   /**< Swap bytes currently in use by this cache. */
};

#endif	// ENABLE_SWAP_SUPPORT


/** @brief Fills @p info with current swap statistics. */
extern "C" void swap_get_info(system_info* info);


#endif	/* _KERNEL_VM_STORE_ANONYMOUS_H */
