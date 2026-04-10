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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file VMUserAddressSpace.h
 *  @brief Per-team user address space with optional ASLR. */

#ifndef VM_USER_ADDRESS_SPACE_H
#define VM_USER_ADDRESS_SPACE_H


#include <vm/VMAddressSpace.h>

#include "VMUserArea.h"


/** @brief Address space implementation for user teams.
 *
 * Each team owns one VMUserAddressSpace instance. Areas are stored in an
 * AVL tree keyed by base address. Allocation supports address space layout
 * randomization for fresh mappings, while still honouring base/exact address
 * requests for compatibility. */
struct VMUserAddressSpace final : VMAddressSpace {
public:
	/** @brief Constructs the user address space spanning [@p base, @p base + @p size). */
								VMUserAddressSpace(team_id id, addr_t base,
									size_t size);
	/** @brief Releases tracking structures owned by the address space. */
	virtual						~VMUserAddressSpace();

	/** @brief Returns the area with the lowest base address, or NULL. */
	virtual	VMArea*				FirstArea() const;
	/** @brief Returns the area immediately after @p area in address order. */
	virtual	VMArea*				NextArea(VMArea* area) const;

	/** @brief Returns the area containing @p address, or NULL if none. */
	virtual	VMArea*				LookupArea(addr_t address) const;
	/** @brief Returns the area whose base is closest to @p address. */
	virtual	VMArea*				FindClosestArea(addr_t address, bool less)
									const;
	/** @brief Allocates a new VMUserArea descriptor for this address space. */
	virtual	VMArea*				CreateArea(const char* name, uint32 wiring,
									uint32 protection, uint32 allocationFlags);
	/** @brief Frees a previously created area descriptor. */
	virtual	void				DeleteArea(VMArea* area,
									uint32 allocationFlags);
	/** @brief Inserts @p area at a free location selected by @p addressRestrictions. */
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

	/** @brief Reserves a user-virtual range for later use without an area. */
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
	inline	bool				_IsRandomized(uint32 addressSpec) const;
	static	addr_t				_RandomizeAddress(addr_t start, addr_t end,
									size_t alignment, bool initial = false);

			status_t			_InsertAreaIntoReservedRegion(addr_t start,
									size_t size, VMUserArea* area,
									uint32 allocationFlags);
			status_t			_InsertAreaSlot(addr_t start, addr_t size,
									addr_t end, uint32 addressSpec,
									size_t alignment, VMUserArea* area,
									uint32 allocationFlags);

private:
	static	const addr_t		kMaxRandomize;         /**< Max ASLR offset for normal allocations. */
	static	const addr_t		kMaxInitialRandomize;  /**< Max ASLR offset for the first allocation. */

			VMUserAreaTree		fAreas;            /**< AVL tree of areas keyed by base address. */
			addr_t				fNextInsertHint;   /**< Cached hint for the next allocation scan. */
};


#endif	/* VM_USER_ADDRESS_SPACE_H */
