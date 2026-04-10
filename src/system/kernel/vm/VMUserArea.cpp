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
 *   Copyright 2009-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file VMUserArea.cpp
 * @brief Represents a single mapped region within a user process address space.
 *
 * @see VMArea
 * @see VMAddressSpace
 */

#include "VMUserArea.h"

#include <heap.h>
#include <vm/vm_priv.h>


/**
 * @brief Construct a VMUserArea and initialise the base VMArea fields.
 *
 * @param addressSpace  The user address space that owns this area.
 * @param wiring        Wiring type (e.g. @c B_NO_LOCK, @c B_FULL_LOCK).
 * @param protection    Page-protection flags (e.g. @c B_READ_AREA |
 *                      @c B_WRITE_AREA).
 */
VMUserArea::VMUserArea(VMAddressSpace* addressSpace, uint32 wiring,
	uint32 protection)
	:
	VMArea(addressSpace, wiring, protection)
{
}


/** @brief Destroy a VMUserArea; base class VMArea handles resource release. */
VMUserArea::~VMUserArea()
{
}


/**
 * @brief Allocate and fully initialise a new VMUserArea.
 *
 * The object is heap-allocated using @p allocationFlags. If construction or
 * initialisation fails the partially constructed object is freed and @c NULL
 * is returned.
 *
 * @param addressSpace    The user address space that will own the area.
 * @param name            Human-readable name for the area (copied internally).
 * @param wiring          Wiring policy for pages in the region.
 * @param protection      Read/write/execute protection flags.
 * @param allocationFlags Flags forwarded to the heap allocator (e.g.
 *                        @c HEAP_DONT_WAIT_FOR_MEMORY).
 * @return Pointer to the newly created VMUserArea, or @c NULL on failure.
 */
/*static*/ VMUserArea*
VMUserArea::Create(VMAddressSpace* addressSpace, const char* name,
	uint32 wiring, uint32 protection, uint32 allocationFlags)
{
	VMUserArea* area = new(malloc_flags(allocationFlags)) VMUserArea(
		addressSpace, wiring, protection);
	if (area == NULL)
		return NULL;

	if (area->Init(name, allocationFlags) != B_OK) {
		area->~VMUserArea();
		free_etc(area, allocationFlags);
		return NULL;
	}

	return area;
}


/**
 * @brief Allocate a reservation placeholder in a user address space.
 *
 * Creates a VMUserArea that acts as an address-range reservation without a
 * real mapping. The area's @c id is set to @c RESERVED_AREA_ID and
 * @c protection is repurposed to store the caller-supplied @p flags.
 *
 * @param addressSpace    The user address space in which the range is reserved.
 * @param flags           Reservation flags stored in the @c protection field.
 * @param allocationFlags Flags forwarded to the heap allocator.
 * @return Pointer to the reservation area, or @c NULL if allocation failed.
 */
/*static*/ VMUserArea*
VMUserArea::CreateReserved(VMAddressSpace* addressSpace, uint32 flags,
	uint32 allocationFlags)
{
	VMUserArea* area = new(malloc_flags(allocationFlags)) VMUserArea(
		addressSpace, 0, 0);
	if (area != NULL) {
		area->id = RESERVED_AREA_ID;
		area->protection = flags;
	}
	return area;
}
