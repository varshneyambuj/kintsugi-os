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
 *   Copyright 2009-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file VMKernelArea.cpp
 * @brief Represents a single mapped region within the kernel address space.
 *
 * @see VMArea
 * @see VMAddressSpace
 */

#include "VMKernelArea.h"

#include <heap.h>
#include <slab/Slab.h>
#include <vm/vm_priv.h>


/**
 * @brief Construct a VMKernelArea and initialise the base VMArea fields.
 *
 * @param addressSpace  The kernel address space that owns this area.
 * @param wiring        Wiring type (e.g. @c B_NO_LOCK, @c B_FULL_LOCK).
 * @param protection    Page-protection flags (e.g. @c B_KERNEL_READ_AREA).
 */
VMKernelArea::VMKernelArea(VMAddressSpace* addressSpace, uint32 wiring,
	uint32 protection)
	:
	VMArea(addressSpace, wiring, protection)
{
}


/** @brief Destroy a VMKernelArea; base class VMArea handles resource release. */
VMKernelArea::~VMKernelArea()
{
}


/**
 * @brief Allocate and fully initialise a new VMKernelArea.
 *
 * The object is placed into @p objectCache via the slab allocator so that
 * kernel-area objects are managed in a dedicated pool. If construction or
 * initialisation fails the partially constructed object is returned to the
 * cache and @c NULL is returned.
 *
 * @param addressSpace    The kernel address space that will own the area.
 * @param name            Human-readable name for the area (copied internally).
 * @param wiring          Wiring policy for pages in the region.
 * @param protection      Read/write/execute protection flags.
 * @param objectCache     Slab object cache from which to allocate the area.
 * @param allocationFlags Flags forwarded to the slab allocator.
 * @return Pointer to the newly created VMKernelArea, or @c NULL on failure.
 */
/*static*/ VMKernelArea*
VMKernelArea::Create(VMAddressSpace* addressSpace, const char* name,
	uint32 wiring, uint32 protection, ObjectCache* objectCache,
	uint32 allocationFlags)
{
	VMKernelArea* area = new(objectCache, allocationFlags) VMKernelArea(
		addressSpace, wiring, protection);
	if (area == NULL)
		return NULL;

	if (area->Init(name, allocationFlags) != B_OK) {
		object_cache_delete(objectCache, area);
		return NULL;
	}

	return area;
}
