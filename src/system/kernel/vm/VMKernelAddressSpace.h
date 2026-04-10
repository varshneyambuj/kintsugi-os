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
 *   Copyright 2009-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file VMKernelAddressSpace.h
 *  @brief Kernel-side address space using a free-list / tree allocator. */

#ifndef VM_KERNEL_ADDRESS_SPACE_H
#define VM_KERNEL_ADDRESS_SPACE_H


#include <vm/VMAddressSpace.h>

#include "VMKernelArea.h"


struct ObjectCache;


/** @brief Address space implementation used for the single shared kernel space.
 *
 * Tracks every kernel virtual address range — areas, reserved ranges, and
 * free holes — in an AVL tree keyed by base address, plus per-size buckets
 * of free ranges to make allocation O(1) on the common case. Object caches
 * are used for the area and range descriptors themselves so that VM-level
 * allocations don't recursively call back into the address space. */
struct VMKernelAddressSpace final : VMAddressSpace {
public:
	/** @brief Constructs the kernel address space spanning [@p base, @p base + @p size). */
								VMKernelAddressSpace(team_id id, addr_t base,
									size_t size);
	/** @brief Releases tracking structures owned by the address space. */
	virtual						~VMKernelAddressSpace();

	/** @brief Lazy initialiser called after construction once VM is up. */
	virtual	status_t			InitObject();

	/** @brief Returns the area with the lowest base address, or NULL. */
	virtual	VMArea*				FirstArea() const;
	/** @brief Returns the area immediately after @p area in address order. */
	virtual	VMArea*				NextArea(VMArea* area) const;

	/** @brief Returns the area containing @p address, or NULL if none. */
	virtual	VMArea*				LookupArea(addr_t address) const;
	/** @brief Returns the area whose base is closest to @p address.
	 *  @param less If true, look for the closest area at or below @p address. */
	virtual	VMArea*				FindClosestArea(addr_t address, bool less)
									const;
	/** @brief Allocates a new area descriptor for this address space. */
	virtual	VMArea*				CreateArea(const char* name, uint32 wiring,
									uint32 protection, uint32 allocationFlags);
	/** @brief Frees an area descriptor. */
	virtual	void				DeleteArea(VMArea* area,
									uint32 allocationFlags);
	/** @brief Inserts @p area at a free location selected by @p addressRestrictions.
	 *  @param _address On success, the chosen base address of the inserted area. */
	virtual	status_t			InsertArea(VMArea* area, size_t size,
									const virtual_address_restrictions*
										addressRestrictions,
									uint32 allocationFlags, void** _address);
	/** @brief Detaches @p area from the address space without freeing it. */
	virtual	void				RemoveArea(VMArea* area,
									uint32 allocationFlags);

	/** @brief Returns true if @p area can be resized to @p newSize in place. */
	virtual	bool				CanResizeArea(VMArea* area, size_t newSize);
	/** @brief Resizes @p area in place to @p newSize. */
	virtual	status_t			ResizeArea(VMArea* area, size_t newSize,
									uint32 allocationFlags);
	/** @brief Shrinks @p area at the front so that its remaining size is @p newSize. */
	virtual	status_t			ShrinkAreaHead(VMArea* area, size_t newSize,
									uint32 allocationFlags);
	/** @brief Shrinks @p area at the tail so that its remaining size is @p newSize. */
	virtual	status_t			ShrinkAreaTail(VMArea* area, size_t newSize,
									uint32 allocationFlags);

	/** @brief Reserves a virtual address range for later use without an area. */
	virtual	status_t			ReserveAddressRange(size_t size,
									const virtual_address_restrictions*
										addressRestrictions,
									uint32 flags, uint32 allocationFlags,
									void** _address);
	/** @brief Releases a previously reserved address range. */
	virtual	status_t			UnreserveAddressRange(addr_t address,
									size_t size, uint32 allocationFlags);
	/** @brief Releases every reserved range owned by this address space. */
	virtual	void				UnreserveAllAddressRanges(
									uint32 allocationFlags);

	/** @brief Dumps the address space layout to the kernel debugger. */
	virtual	void				Dump() const;

private:
			typedef VMKernelAddressRange Range;
			typedef VMKernelAddressRangeTree RangeTree;
			typedef DoublyLinkedList<Range,
				DoublyLinkedListMemberGetLink<Range, &Range::listLink> >
					RangeList;
			typedef DoublyLinkedList<Range, VMKernelAddressRangeGetFreeListLink>
				RangeFreeList;

private:
	inline	void				_FreeListInsertRange(Range* range, size_t size);
	inline	void				_FreeListRemoveRange(Range* range, size_t size);

			void				_InsertRange(Range* range);
			void				_RemoveRange(Range* range);

			status_t			_AllocateRange(
									const virtual_address_restrictions*
										addressRestrictions,
									size_t size, bool allowReservedRange,
									uint32 allocationFlags, Range*& _range);
			Range*				_FindFreeRange(addr_t start, size_t size,
									size_t alignment, uint32 addressSpec,
									bool allowReservedRange,
									addr_t& _foundAddress);
			void				_FreeRange(Range* range,
									uint32 allocationFlags);

			void				_CheckStructures() const;

private:
			RangeTree			fRangeTree;          /**< AVL tree of every range, keyed by base address. */
			RangeList			fRangeList;          /**< In-order linked list of every range. */
			RangeFreeList*		fFreeLists;          /**< Per-size buckets of free ranges. */
			int					fFreeListCount;      /**< Number of size buckets in @c fFreeLists. */
			ObjectCache*		fAreaObjectCache;    /**< Object cache for VMKernelArea allocations. */
			ObjectCache*		fRangesObjectCache;  /**< Object cache for VMKernelAddressRange allocations. */
};


#endif	/* VM_KERNEL_ADDRESS_SPACE_H */
