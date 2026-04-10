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
 *   Copyright 2004-2007, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/** @file VMAnonymousNoSwapCache.h
 *  @brief Anonymous memory cache used when swap support is disabled. */

#ifndef _KERNEL_VM_STORE_ANONYMOUS_NO_SWAP_H
#define _KERNEL_VM_STORE_ANONYMOUS_NO_SWAP_H


#include <vm/VMCache.h>


/** @brief Anonymous VMCache for builds compiled without swap support.
 *
 * Behaves like VMAnonymousCache but cannot page out: every committed byte
 * must be backed by physical memory at all times. Used on systems where
 * swap is intentionally disabled (e.g. embedded targets) and on early-boot
 * paths before swap is initialised. */
class VMAnonymousNoSwapCache : public VMCache {
public:
	/** @brief Releases per-instance state. */
	virtual						~VMAnonymousNoSwapCache();

	/** @brief Initialises the cache.
	 *  @param canOvercommit       Permit overcommit if true.
	 *  @param numPrecommittedPages Pages to commit up front.
	 *  @param numGuardPages       Guard pages reserved at the start of the cache.
	 *  @param allocationFlags     Allocation flags forwarded to the base class.
	 *  @return B_OK on success, or an error code on failure. */
			status_t			Init(bool canOvercommit,
									int32 numPrecommittedPages,
									int32 numGuardPages,
									uint32 allocationFlags);

	/** @brief Moves a sub-range of pages and commitment from @p source. */
	virtual	status_t			Adopt(VMCache* source, off_t offset, off_t size,
									off_t newOffset);
	/** @brief Discards a range of pages, releasing the underlying memory. */
	virtual	ssize_t				Discard(off_t offset, off_t size);

	/** @brief Returns the cache's current commitment in bytes. */
	virtual	off_t				Commitment() const;
	/** @brief Returns true if overcommit is permitted. */
	virtual	bool				CanOvercommit();
	/** @brief Adjusts the cache commitment to at least @p size. */
	virtual	status_t			Commit(off_t size, int priority);
	/** @brief Transfers commitment from another cache into this one. */
	virtual	void				TakeCommitmentFrom(VMCache* from, off_t commitment);

	/** @brief Returns true if the page at @p offset is held by this cache. */
	virtual	bool				StoreHasPage(off_t offset);

	/** @brief Returns the configured guard region size in bytes. */
	virtual	int32				GuardSize()	{ return fGuardedSize; }
	/** @brief Sets the guard region size in bytes. */
	virtual	void				SetGuardSize(int32 guardSize)
									{ fGuardedSize = guardSize; }

	/** @brief Read is unsupported on a no-swap anonymous cache. */
	virtual	status_t			Read(off_t offset, const generic_io_vec *vecs,
									size_t count,uint32 flags,
									generic_size_t *_numBytes);
	/** @brief Write is unsupported on a no-swap anonymous cache. */
	virtual	status_t			Write(off_t offset, const generic_io_vec *vecs,
									size_t count, uint32 flags,
									generic_size_t *_numBytes);

	/** @brief Handles a page fault by allocating a fresh anonymous page. */
	virtual	status_t			Fault(struct VMAddressSpace* aspace,
									off_t offset);

	/** @brief Merges @p source into this cache. */
	virtual	void				Merge(VMCache* source);

protected:
	/** @brief Releases per-instance resources before the object is freed. */
	virtual	void				DeleteObject();

public:
			off_t				committed_size;       /**< Bytes currently committed by this cache. */

private:
			bool				fCanOvercommit;       /**< True if overcommit is permitted. */
			bool				fHasPrecommitted;     /**< True if precommitted pages have been reserved. */
			uint8				fPrecommittedPages;   /**< Number of precommitted pages, if @c fHasPrecommitted. */
			int32				fGuardedSize;         /**< Bytes of guard region at the cache's start. */
};


#endif	/* _KERNEL_VM_STORE_ANONYMOUS_NO_SWAP_H */
