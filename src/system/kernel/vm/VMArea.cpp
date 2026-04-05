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
 *   Copyright 2009-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file VMArea.cpp
 * @brief Represents a mapped memory region within a VMAddressSpace.
 *
 * VMArea holds the address range, protection flags, VMCache reference, and
 * page mapping information for a single contiguous virtual memory region.
 * Created by vm_create_anonymous_area, vm_map_file, and related functions.
 *
 * @see VMAddressSpace.cpp, VMCache.cpp
 */


#include <vm/VMArea.h>

#include <new>

#include <heap.h>
#include <vm/VMAddressSpace.h>


rw_lock VMAreas::sLock = RW_LOCK_INITIALIZER("areas tree");
VMAreasTree VMAreas::sTree;
static area_id sNextAreaID = 1;


// #pragma mark - VMArea

/**
 * @brief Constructs a VMArea and initialises all fields to safe defaults.
 *
 * Placement-constructs the VMAreaMappings list in-place so that the
 * object is fully usable before Init() is called.
 *
 * @param addressSpace  The VMAddressSpace that will own this area.
 * @param wiring        Wiring type (e.g. B_NO_LOCK, B_FULL_LOCK).
 * @param protection    Initial page-protection flags (B_READ_AREA, etc.).
 */
VMArea::VMArea(VMAddressSpace* addressSpace, uint32 wiring, uint32 protection)
	:
	protection(protection),
	protection_max(0),
	wiring(wiring),
	memory_type(0),
	cache(NULL),
	cache_offset(0),
	cache_type(0),
	page_protections(NULL),
	address_space(addressSpace)
{
	new (&mappings) VMAreaMappings;
}


/**
 * @brief Destroys a VMArea and releases the per-page protection array.
 *
 * Uses free_etc() with the appropriate heap flags so that kernel-space
 * areas do not recursively trigger memory waits or kernel-space locking.
 */
VMArea::~VMArea()
{
	free_etc(page_protections, address_space == VMAddressSpace::Kernel()
		? HEAP_DONT_WAIT_FOR_MEMORY | HEAP_DONT_LOCK_KERNEL_SPACE : 0);
}


/**
 * @brief Finalises area initialisation by copying the name and assigning an ID.
 *
 * Must be called once after construction before the area is inserted into
 * any global data structure.
 *
 * @param name             Descriptive name for the area (truncated to
 *                         B_OS_NAME_LENGTH - 1 characters).
 * @param allocationFlags  Heap allocation flags (currently unused here but
 *                         forwarded for consistency).
 * @retval B_OK  Always succeeds.
 */
status_t
VMArea::Init(const char* name, uint32 allocationFlags)
{
	// copy the name
	strlcpy(this->name, name, B_OS_NAME_LENGTH);

	id = atomic_add(&sNextAreaID, 1);
	return B_OK;
}


/*!	Returns whether any part of the given address range intersects with a wired
	range of this area.
	The area's top cache must be locked.
*/
bool
VMArea::IsWired(addr_t base, size_t size) const
{
	for (VMAreaWiredRangeList::ConstIterator it = fWiredRanges.GetIterator();
			VMAreaWiredRange* range = it.Next();) {
		if (range->IntersectsWith(base, size))
			return true;
	}

	return false;
}


/*!	Adds the given wired range to this area.
	The area's top cache must be locked.
*/
void
VMArea::Wire(VMAreaWiredRange* range)
{
	ASSERT(range->area == NULL);

	range->area = this;
	fWiredRanges.Add(range);
}


/*!	Removes the given wired range from this area.
	Must balance a previous Wire() call.
	The area's top cache must be locked.
*/
void
VMArea::Unwire(VMAreaWiredRange* range)
{
	ASSERT(range->area == this);

	// remove the range
	range->area = NULL;
	fWiredRanges.Remove(range);

	// wake up waiters
	for (VMAreaUnwiredWaiterList::Iterator it = range->waiters.GetIterator();
			VMAreaUnwiredWaiter* waiter = it.Next();) {
		waiter->condition.NotifyAll();
	}

	range->waiters.MakeEmpty();
}


/*!	Removes a wired range from this area.

	Must balance a previous Wire() call. The first implicit range with matching
	\a base, \a size, and \a writable attributes is removed and returned. It's
	waiters are woken up as well.
	The area's top cache must be locked.
*/
VMAreaWiredRange*
VMArea::Unwire(addr_t base, size_t size, bool writable)
{
	for (VMAreaWiredRangeList::ConstIterator it = fWiredRanges.GetIterator();
			VMAreaWiredRange* range = it.Next();) {
		if (range->implicit && range->base == base && range->size == size
				&& range->writable == writable) {
			Unwire(range);
			return range;
		}
	}

	panic("VMArea::Unwire(%#" B_PRIxADDR ", %#" B_PRIxADDR ", %d): no such "
		"range", base, size, writable);
	return NULL;
}


/*!	If the area has any wired range, the given waiter is added to the range and
	prepared for waiting.

	\return \c true, if the waiter has been added, \c false otherwise.
*/
bool
VMArea::AddWaiterIfWired(VMAreaUnwiredWaiter* waiter)
{
	VMAreaWiredRange* range = fWiredRanges.Head();
	if (range == NULL)
		return false;

	waiter->area = this;
	waiter->base = fBase;
	waiter->size = fSize;
	waiter->condition.Init(this, "area unwired");
	waiter->condition.Add(&waiter->waitEntry);

	range->waiters.Add(waiter);

	return true;
}


/*!	If the given address range intersect with a wired range of this area, the
	given waiter is added to the range and prepared for waiting.

	\param waiter The waiter structure that will be added to the wired range
		that intersects with the given address range.
	\param base The base of the address range to check.
	\param size The size of the address range to check.
	\param flags
		- \c IGNORE_WRITE_WIRED_RANGES: Ignore ranges wired for writing.
	\return \c true, if the waiter has been added, \c false otherwise.
*/
bool
VMArea::AddWaiterIfWired(VMAreaUnwiredWaiter* waiter, addr_t base, size_t size,
	uint32 flags)
{
	for (VMAreaWiredRangeList::ConstIterator it = fWiredRanges.GetIterator();
			VMAreaWiredRange* range = it.Next();) {
		if ((flags & IGNORE_WRITE_WIRED_RANGES) != 0 && range->writable)
			continue;

		if (range->IntersectsWith(base, size)) {
			waiter->area = this;
			waiter->base = base;
			waiter->size = size;
			waiter->condition.Init(this, "area unwired");
			waiter->condition.Add(&waiter->waitEntry);

			range->waiters.Add(waiter);

			return true;
		}
	}

	return false;
}


// #pragma mark - VMAreas

/**
 * @brief Initialises the global VMAreas red-black tree.
 *
 * Must be called once during VM subsystem bootstrap before any area is
 * created.
 *
 * @retval B_OK  Always succeeds.
 */
/*static*/ status_t
VMAreas::Init()
{
	new(&sTree) VMAreasTree;
	return B_OK;
}


/**
 * @brief Looks up an area by ID, acquiring and releasing the read lock.
 *
 * Safe to call from any context that does not already hold sLock for writing.
 *
 * @param id  The area_id to look up.
 * @return    Pointer to the matching VMArea, or @c NULL if not found.
 */
/*static*/ VMArea*
VMAreas::Lookup(area_id id)
{
	ReadLock();
	VMArea* area = LookupLocked(id);
	ReadUnlock();
	return area;
}


/**
 * @brief Searches for the first area whose name matches \a name.
 *
 * Iterates the entire tree while holding the read lock; this can be slow for
 * large area counts.
 *
 * @param name  Null-terminated name string to match.
 * @return      area_id of the first matching area, or @c B_NAME_NOT_FOUND.
 *
 * @note This performs a linear scan. A hash-based secondary index would
 *       improve performance; see the TODO inside the implementation.
 */
/*static*/ area_id
VMAreas::Find(const char* name)
{
	ReadLock();

	area_id id = B_NAME_NOT_FOUND;

	// TODO: Iterating through the whole table can be very slow and the whole
	// time we're holding the lock! Use a second hash table!

	for (VMAreasTree::Iterator it = sTree.GetIterator();
			VMArea* area = it.Next();) {
		if (strcmp(area->name, name) == 0) {
			id = area->id;
			break;
		}
	}

	ReadUnlock();

	return id;
}


/**
 * @brief Inserts an area into the global tree under the write lock.
 *
 * @param area  Fully-initialised VMArea to insert.
 * @retval B_OK           Area inserted successfully.
 * @retval B_NAME_IN_USE  An area with the same ID already exists in the tree.
 */
/*static*/ status_t
VMAreas::Insert(VMArea* area)
{
	WriteLock();
	status_t status = sTree.Insert(area);
	WriteUnlock();
	return status;
}


/**
 * @brief Removes an area from the global tree under the write lock.
 *
 * The caller is responsible for destroying the VMArea object afterwards.
 *
 * @param area  The area to remove.
 */
/*static*/ void
VMAreas::Remove(VMArea* area)
{
	WriteLock();
	sTree.Remove(area);
	WriteUnlock();
}
