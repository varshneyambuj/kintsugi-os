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
 *   Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file VMKernelAddressSpace.cpp
 * @brief Virtual address space management for the kernel.
 *
 * Implements the kernel address space using a red-black tree of free address
 * ranges (Range objects). Optimizes kernel area placement for speed and
 * minimizes fragmentation. Handles insertion, removal, and resizing of
 * VMKernelArea objects within the kernel's virtual address range.
 *
 * @see VMUserAddressSpace.cpp, VMKernelArea.cpp
 */


#include "VMKernelAddressSpace.h"

#include <stdlib.h>

#include <KernelExport.h>

#include <heap.h>
#include <slab/Slab.h>
#include <thread.h>
#include <vm/vm.h>
#include <vm/VMArea.h>


//#define TRACE_VM
#ifdef TRACE_VM
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif


//#define PARANOIA_CHECKS
#ifdef PARANOIA_CHECKS
#	define PARANOIA_CHECK_STRUCTURES()	_CheckStructures()
#else
#	define PARANOIA_CHECK_STRUCTURES()	do {} while (false)
#endif


/**
 * @brief Computes floor(log2(value)).
 *
 * Returns the index of the highest set bit in @p value, which is equivalent
 * to floor(log2(value)). Used to determine the free-list bucket index for a
 * range of a given size.
 *
 * @param value The value whose base-2 logarithm is computed. Must be > 0.
 * @return The index of the highest set bit (0-based), or -1 if value is 0.
 */
static int
ld(size_t value)
{
	int index = -1;
	while (value > 0) {
		value >>= 1;
		index++;
	}

	return index;
}


/**
 * @brief Checks whether a candidate range fits within the allowed spot.
 *
 * Verifies that @p alignedBase is at least @p base, that the range
 * [@p alignedBase, @p alignedBase + @p size - 1] does not overflow, and that
 * the range's last address does not exceed @p limit.
 *
 * @param base       Minimum permissible base address.
 * @param alignedBase Candidate aligned start address of the area.
 * @param size       Size of the area in bytes.
 * @param limit      Maximum permissible last (inclusive) address.
 * @return @c true if the range fits, @c false otherwise.
 */
static inline bool
is_valid_spot(addr_t base, addr_t alignedBase, addr_t size, addr_t limit)
{
	return (alignedBase >= base && alignedBase + (size - 1) > alignedBase
		&& alignedBase + (size - 1) <= limit);
}


// #pragma mark - VMKernelAddressSpace


/**
 * @brief Constructs the kernel address space object.
 *
 * Initialises the base class VMAddressSpace for the given team @p id over the
 * virtual range [@p base, @p base + @p size - 1]. The slab object caches for
 * areas and ranges are left NULL until InitObject() is called.
 *
 * @param id   Team identifier that owns this address space (typically the
 *             kernel team ID).
 * @param base First valid virtual address of the kernel address space.
 * @param size Total byte size of the kernel virtual address range.
 *
 * @note Must be followed by a successful call to InitObject() before the
 *       address space can be used.
 */
VMKernelAddressSpace::VMKernelAddressSpace(team_id id, addr_t base, size_t size)
	:
	VMAddressSpace(id, base, size, "kernel address space"),
	fAreaObjectCache(NULL),
	fRangesObjectCache(NULL)
{
}


/**
 * @brief Destructor — intentionally panics.
 *
 * The kernel address space must never be deleted. Reaching this destructor
 * indicates a serious kernel bug, so a panic is triggered immediately.
 */
VMKernelAddressSpace::~VMKernelAddressSpace()
{
	panic("deleting the kernel aspace!\n");
}


/**
 * @brief Initialises slab caches and the initial free range covering the
 *        entire kernel address space.
 *
 * Creates two slab object caches (one for VMKernelArea objects, one for Range
 * objects), allocates the free-list array sized to cover all possible
 * power-of-two page buckets within the address space, and inserts a single
 * free Range spanning [fBase, fEndAddress].
 *
 * @return @c B_OK on success.
 * @retval B_NO_MEMORY if any allocation (cache creation, free-list array, or
 *         initial Range) fails.
 *
 * @note Must be called exactly once, immediately after construction, before
 *       any other method is invoked.
 */
status_t
VMKernelAddressSpace::InitObject()
{
	fAreaObjectCache = create_object_cache("kernel areas",
		sizeof(VMKernelArea), 0);
	if (fAreaObjectCache == NULL)
		return B_NO_MEMORY;

	fRangesObjectCache = create_object_cache("kernel address ranges",
		sizeof(Range), 0);
	if (fRangesObjectCache == NULL)
		return B_NO_MEMORY;

	// create the free lists
	size_t size = fEndAddress - fBase + 1;
	fFreeListCount = ld(size) - PAGE_SHIFT + 1;
	fFreeLists = new(std::nothrow) RangeFreeList[fFreeListCount];
	if (fFreeLists == NULL)
		return B_NO_MEMORY;

	Range* range = new(fRangesObjectCache, 0) Range(fBase, size,
		Range::RANGE_FREE);
	if (range == NULL)
		return B_NO_MEMORY;

	_InsertRange(range);

	TRACE("VMKernelAddressSpace::InitObject(): address range: %#" B_PRIxADDR
		" - %#" B_PRIxADDR ", free lists: %d\n", fBase, fEndAddress,
		fFreeListCount);

	return B_OK;
}


/**
 * @brief Returns the first VMArea in the kernel address space.
 *
 * Walks the ordered range list from the head and returns the area pointer of
 * the first Range whose type is RANGE_AREA.
 *
 * @return Pointer to the first VMArea, or @c NULL if there are none.
 *
 * @note Caller must hold at least the address space read lock.
 */
inline VMArea*
VMKernelAddressSpace::FirstArea() const
{
	Range* range = fRangeList.Head();
	while (range != NULL && range->type != Range::RANGE_AREA)
		range = fRangeList.GetNext(range);
	return range != NULL ? range->area : NULL;
}


/**
 * @brief Returns the VMArea immediately following @p _area in address order.
 *
 * Starting from the Range associated with @p _area, advances through the
 * range list until the next RANGE_AREA entry is found.
 *
 * @param _area The current area whose successor is requested. Must not be
 *              @c NULL and must belong to this address space.
 * @return Pointer to the next VMArea, or @c NULL if @p _area is the last one.
 *
 * @note Caller must hold at least the address space read lock.
 */
inline VMArea*
VMKernelAddressSpace::NextArea(VMArea* _area) const
{
	Range* range = static_cast<VMKernelArea*>(_area)->Range();
	do {
		range = fRangeList.GetNext(range);
	} while (range != NULL && range->type != Range::RANGE_AREA);
	return range != NULL ? range->area : NULL;
}


/**
 * @brief Allocates a new VMKernelArea object from the slab cache.
 *
 * Delegates to VMKernelArea::Create(), passing the address space pointer,
 * area attributes, the area slab cache, and the allocation flags.
 *
 * @param name            Descriptive name for the area.
 * @param wiring          Wiring type (e.g. B_NO_LOCK, B_FULL_LOCK).
 * @param protection      Page-level protection flags.
 * @param allocationFlags Slab allocation flags (e.g. CACHE_DONT_WAIT_FOR_MEMORY).
 * @return Pointer to the newly created VMKernelArea cast as VMArea, or @c NULL
 *         on allocation failure.
 */
VMArea*
VMKernelAddressSpace::CreateArea(const char* name, uint32 wiring,
	uint32 protection, uint32 allocationFlags)
{
	return VMKernelArea::Create(this, name, wiring, protection,
		fAreaObjectCache, allocationFlags);
}


/**
 * @brief Returns a VMKernelArea object to its slab cache.
 *
 * Casts @p _area to VMKernelArea and releases the object back to
 * @c fAreaObjectCache.
 *
 * @param _area           The area to delete. Must have been created by
 *                        CreateArea() on this address space.
 * @param allocationFlags Slab deallocation flags forwarded to
 *                        object_cache_delete().
 */
void
VMKernelAddressSpace::DeleteArea(VMArea* _area, uint32 allocationFlags)
{
	TRACE("VMKernelAddressSpace::DeleteArea(%p)\n", _area);

	VMKernelArea* area = static_cast<VMKernelArea*>(_area);
	object_cache_delete(fAreaObjectCache, area);
}


/**
 * @brief Looks up the VMArea that contains @p address.
 *
 * Searches the range tree for the closest range at or below @p address and
 * verifies that the range is of type RANGE_AREA and that @p address actually
 * falls within the area's base/size.
 *
 * @param address Virtual address to look up.
 * @return Pointer to the containing VMKernelArea, or @c NULL if no area
 *         contains @p address.
 *
 * @note Caller must hold the address space read lock.
 */
//! You must hold the address space's read lock.
VMArea*
VMKernelAddressSpace::LookupArea(addr_t address) const
{
	Range* range = fRangeTree.FindClosest(address, true);
	if (range == NULL || range->type != Range::RANGE_AREA)
		return NULL;

	VMKernelArea* area = range->area;
	return area->ContainsAddress(address) ? area : NULL;
}


/**
 * @brief Finds the VMArea closest to @p address in the given direction.
 *
 * Walks the range tree starting at the closest range to @p address, moving
 * in the direction indicated by @p less, until a RANGE_AREA range is found.
 *
 * @param address Virtual address used as the search anchor.
 * @param less    If @c true, search for the closest area whose base is less
 *                than or equal to @p address; if @c false, greater or equal.
 * @return Pointer to the closest matching VMArea, or @c NULL.
 *
 * @note Caller must hold the address space read lock.
 */
//! You must hold the address space's read lock.
VMArea*
VMKernelAddressSpace::FindClosestArea(addr_t address, bool less) const
{
	Range* range = fRangeTree.FindClosest(address, less);
	while (range != NULL && range->type != Range::RANGE_AREA)
		range = less ? fRangeTree.Previous(range) : fRangeTree.Next(range);

	return range != NULL ? range->area : NULL;
}


/**
 * @brief Inserts an area into the kernel address space.
 *
 * Allocates a virtual address range of @p size bytes according to
 * @p addressRestrictions, assigns it to @p _area, and updates bookkeeping.
 * On success @p *_address receives the area's base address and fFreeSpace is
 * decremented accordingly.
 *
 * @param _area                 The VMKernelArea to insert. Must have been
 *                              created by CreateArea().
 * @param size                  Requested size in bytes (will be page-rounded
 *                              internally).
 * @param addressRestrictions   Address specification and alignment constraints.
 * @param allocationFlags       Slab allocation flags for internal Range splits.
 * @param[out] _address         Receives the assigned virtual base address if
 *                              not @c NULL.
 * @return @c B_OK on success.
 * @retval B_BAD_VALUE if B_EXACT_ADDRESS is requested but the spot is occupied
 *                     or misaligned.
 * @retval B_NO_MEMORY if no suitable virtual range is available or an
 *                     internal allocation fails.
 *
 * @note Caller must hold the address space write lock.
 */
/*!	This inserts the area you pass into the address space.
	It will also set the "_address" argument to its base address when
	the call succeeds.
	You need to hold the VMAddressSpace write lock.
*/
status_t
VMKernelAddressSpace::InsertArea(VMArea* _area, size_t size,
	const virtual_address_restrictions* addressRestrictions,
	uint32 allocationFlags, void** _address)
{
	TRACE("VMKernelAddressSpace::InsertArea(%p, %" B_PRIu32 ", %#" B_PRIxSIZE
		", %p \"%s\")\n", addressRestrictions->address,
		addressRestrictions->address_specification, size, _area, _area->name);
	ASSERT_WRITE_LOCKED_RW_LOCK(&fLock);

	VMKernelArea* area = static_cast<VMKernelArea*>(_area);

	Range* range;
	status_t error = _AllocateRange(addressRestrictions, size,
		addressRestrictions->address_specification == B_EXACT_ADDRESS,
		allocationFlags, range);
	if (error != B_OK)
		return error;

	range->type = Range::RANGE_AREA;
	range->area = area;
	area->SetRange(range);
	area->SetBase(range->base);
	area->SetSize(range->size);

	if (_address != NULL)
		*_address = (void*)area->Base();
	fFreeSpace -= area->Size();

	PARANOIA_CHECK_STRUCTURES();

	return B_OK;
}


/**
 * @brief Removes an area from the kernel address space.
 *
 * Frees the virtual range held by @p _area (coalescing with any adjacent free
 * ranges) and adds the freed bytes back to fFreeSpace.
 *
 * @param _area           The area to remove. Must currently be inserted in
 *                        this address space.
 * @param allocationFlags Slab deallocation flags forwarded during Range
 *                        coalescing.
 *
 * @note Caller must hold the address space write lock.
 */
//! You must hold the address space's write lock.
void
VMKernelAddressSpace::RemoveArea(VMArea* _area, uint32 allocationFlags)
{
	TRACE("VMKernelAddressSpace::RemoveArea(%p)\n", _area);
	ASSERT_WRITE_LOCKED_RW_LOCK(&fLock);

	VMKernelArea* area = static_cast<VMKernelArea*>(_area);

	_FreeRange(area->Range(), allocationFlags);

	fFreeSpace += area->Size();

	PARANOIA_CHECK_STRUCTURES();
}


/**
 * @brief Tests whether an area can be resized to @p newSize bytes.
 *
 * If @p newSize is smaller than or equal to the current size the answer is
 * always @c true. For growth, checks that the immediately following Range is
 * not another area and that the free (or non-base-reserved) space after the
 * area is sufficient.
 *
 * @param area    The area to query.
 * @param newSize The desired new size in bytes.
 * @return @c true if ResizeArea() would succeed, @c false otherwise.
 *
 * @note Does not modify any state. Does not require a lock (but the caller
 *       should hold at least the read lock for a stable answer).
 */
bool
VMKernelAddressSpace::CanResizeArea(VMArea* area, size_t newSize)
{
	Range* range = static_cast<VMKernelArea*>(area)->Range();

	if (newSize <= range->size)
		return true;

	Range* nextRange = fRangeList.GetNext(range);
	if (nextRange == NULL || nextRange->type == Range::RANGE_AREA)
		return false;

	if (nextRange->type == Range::RANGE_RESERVED
		&& nextRange->reserved.base > range->base) {
		return false;
	}

	// TODO: If there is free space after a reserved range (or vice versa), it
	// could be used as well.
	return newSize - range->size <= nextRange->size;
}


/**
 * @brief Resizes an area to @p newSize bytes by adjusting the tail boundary.
 *
 * For shrinking, the freed tail is either merged with a subsequent free Range
 * or turned into a new free Range. For growing, the immediately following free
 * or non-base-reserved Range is consumed (wholly or partially).
 *
 * @param _area           The area to resize.
 * @param newSize         New size in bytes. Must be page-aligned; must not
 *                        exceed available space if growing.
 * @param allocationFlags Slab allocation flags used when new Range objects
 *                        must be allocated.
 * @return @c B_OK on success.
 * @retval B_BAD_VALUE if @p newSize is larger than the available adjacent
 *                     space or if no suitable following range exists.
 * @retval B_NO_MEMORY if a new Range object cannot be allocated.
 *
 * @note Caller must hold the address space write lock.
 */
status_t
VMKernelAddressSpace::ResizeArea(VMArea* _area, size_t newSize,
	uint32 allocationFlags)
{
	TRACE("VMKernelAddressSpace::ResizeArea(%p, %#" B_PRIxSIZE ")\n", _area,
		newSize);

	VMKernelArea* area = static_cast<VMKernelArea*>(_area);
	Range* range = area->Range();

	if (newSize == range->size)
		return B_OK;

	Range* nextRange = fRangeList.GetNext(range);

	if (newSize < range->size) {
		if (nextRange != NULL && nextRange->type == Range::RANGE_FREE) {
			// a free range is following -- just enlarge it
			_FreeListRemoveRange(nextRange, nextRange->size);
			nextRange->size += range->size - newSize;
			nextRange->base = range->base + newSize;
			_FreeListInsertRange(nextRange, nextRange->size);
		} else {
			// no free range following -- we need to allocate a new one and
			// insert it
			nextRange = new(fRangesObjectCache, allocationFlags) Range(
				range->base + newSize, range->size - newSize,
				Range::RANGE_FREE);
			if (nextRange == NULL)
				return B_NO_MEMORY;
			_InsertRange(nextRange);
		}
	} else {
		if (nextRange == NULL
			|| (nextRange->type == Range::RANGE_RESERVED
				&& nextRange->reserved.base > range->base)) {
			return B_BAD_VALUE;
		}
		// TODO: If there is free space after a reserved range (or vice versa),
		// it could be used as well.
		size_t sizeDiff = newSize - range->size;
		if (sizeDiff > nextRange->size)
			return B_BAD_VALUE;

		if (sizeDiff == nextRange->size) {
			// The next range is completely covered -- remove and delete it.
			_RemoveRange(nextRange);
			object_cache_delete(fRangesObjectCache, nextRange, allocationFlags);
		} else {
			// The next range is only partially covered -- shrink it.
			if (nextRange->type == Range::RANGE_FREE)
				_FreeListRemoveRange(nextRange, nextRange->size);
			nextRange->size -= sizeDiff;
			nextRange->base = range->base + newSize;
			if (nextRange->type == Range::RANGE_FREE)
				_FreeListInsertRange(nextRange, nextRange->size);
		}
	}

	range->size = newSize;
	area->SetSize(newSize);

	IncrementChangeCount();
	PARANOIA_CHECK_STRUCTURES();
	return B_OK;
}


/**
 * @brief Shrinks an area from its head (lower address) end.
 *
 * Reduces the area's size by @p (range->size - @p newSize) bytes from the
 * beginning: the freed prefix is either merged with a preceding free Range or
 * becomes a new free Range, and the area's base address advances accordingly.
 *
 * @param _area           The area to shrink.
 * @param newSize         New (smaller) size in bytes.
 * @param allocationFlags Slab allocation flags for potential new Range.
 * @return @c B_OK on success.
 * @retval B_BAD_VALUE if @p newSize is larger than the current area size.
 * @retval B_NO_MEMORY if a new Range object cannot be allocated.
 *
 * @note Caller must hold the address space write lock.
 */
status_t
VMKernelAddressSpace::ShrinkAreaHead(VMArea* _area, size_t newSize,
	uint32 allocationFlags)
{
	TRACE("VMKernelAddressSpace::ShrinkAreaHead(%p, %#" B_PRIxSIZE ")\n", _area,
		newSize);

	VMKernelArea* area = static_cast<VMKernelArea*>(_area);
	Range* range = area->Range();

	if (newSize == range->size)
		return B_OK;

	if (newSize > range->size)
		return B_BAD_VALUE;

	Range* previousRange = fRangeList.GetPrevious(range);

	size_t sizeDiff = range->size - newSize;
	if (previousRange != NULL && previousRange->type == Range::RANGE_FREE) {
		// the previous range is free -- just enlarge it
		_FreeListRemoveRange(previousRange, previousRange->size);
		previousRange->size += sizeDiff;
		_FreeListInsertRange(previousRange, previousRange->size);
		range->base += sizeDiff;
		range->size = newSize;
	} else {
		// no free range before -- we need to allocate a new one and
		// insert it
		previousRange = new(fRangesObjectCache, allocationFlags) Range(
			range->base, sizeDiff, Range::RANGE_FREE);
		if (previousRange == NULL)
			return B_NO_MEMORY;
		range->base += sizeDiff;
		range->size = newSize;
		_InsertRange(previousRange);
	}

	area->SetBase(range->base);
	area->SetSize(range->size);

	IncrementChangeCount();
	PARANOIA_CHECK_STRUCTURES();
	return B_OK;
}


/**
 * @brief Shrinks an area from its tail (higher address) end.
 *
 * Delegates to ResizeArea(), which handles tail shrinking as a special case
 * of resizing.
 *
 * @param area            The area to shrink.
 * @param newSize         New (smaller) size in bytes.
 * @param allocationFlags Slab allocation flags forwarded to ResizeArea().
 * @return Return value of ResizeArea().
 *
 * @note Caller must hold the address space write lock.
 */
status_t
VMKernelAddressSpace::ShrinkAreaTail(VMArea* area, size_t newSize,
	uint32 allocationFlags)
{
	return ResizeArea(area, newSize, allocationFlags);
}


/**
 * @brief Reserves a virtual address range without mapping an area into it.
 *
 * Allocates a Range of type RANGE_RESERVED to prevent the address range from
 * being handed out by subsequent InsertArea() calls. Increments the address
 * space reference count via Get().
 *
 * @param size                  Size of the range to reserve in bytes.
 * @param addressRestrictions   Placement and alignment constraints.
 * @param flags                 Reservation flags (e.g. RESERVED_AVOID_BASE).
 * @param allocationFlags       Slab allocation flags for internal Range splits.
 * @param[out] _address         Receives the reserved base address if not NULL.
 * @return @c B_OK on success.
 * @retval B_BAD_TEAM_ID if the address space is being deleted.
 * @retval B_NO_MEMORY / B_BAD_VALUE from _AllocateRange().
 *
 * @note Caller must hold the address space write lock.
 */
status_t
VMKernelAddressSpace::ReserveAddressRange(size_t size,
	const virtual_address_restrictions* addressRestrictions,
	uint32 flags, uint32 allocationFlags, void** _address)
{
	TRACE("VMKernelAddressSpace::ReserveAddressRange(%p, %" B_PRIu32 ", %#"
		B_PRIxSIZE ", %#" B_PRIx32 ")\n", addressRestrictions->address,
		addressRestrictions->address_specification, size, flags);

	// Don't allow range reservations, if the address space is about to be
	// deleted.
	if (fDeleting)
		return B_BAD_TEAM_ID;

	Range* range;
	status_t error = _AllocateRange(addressRestrictions, size, false,
		allocationFlags, range);
	if (error != B_OK)
		return error;

	range->type = Range::RANGE_RESERVED;
	range->reserved.base = range->base;
	range->reserved.flags = flags;

	if (_address != NULL)
		*_address = (void*)range->base;

	Get();
	PARANOIA_CHECK_STRUCTURES();
	return B_OK;
}


/**
 * @brief Releases all reserved ranges that overlap [@p address, @p address+@p size).
 *
 * Iterates through the range tree starting at @p address and frees every
 * RANGE_RESERVED range whose last address is within the specified region.
 * Free ranges are skipped during iteration because _FreeRange() may coalesce
 * them. Decrements the address space reference count via Put() for each freed
 * reservation.
 *
 * @param address         Start of the address region to unreserve.
 * @param size            Length of the region in bytes.
 * @param allocationFlags Slab deallocation flags forwarded to _FreeRange().
 * @return @c B_OK always (errors are ignored silently).
 * @retval B_BAD_TEAM_ID if the address space is being deleted.
 *
 * @note Caller must hold the address space write lock.
 */
status_t
VMKernelAddressSpace::UnreserveAddressRange(addr_t address, size_t size,
	uint32 allocationFlags)
{
	TRACE("VMKernelAddressSpace::UnreserveAddressRange(%#" B_PRIxADDR ", %#"
		B_PRIxSIZE ")\n", address, size);

	// Don't allow range unreservations, if the address space is about to be
	// deleted. UnreserveAllAddressRanges() must be used.
	if (fDeleting)
		return B_BAD_TEAM_ID;

	// search range list and remove any matching reserved ranges
	addr_t endAddress = address + (size - 1);
	Range* range = fRangeTree.FindClosest(address, false);
	while (range != NULL && range->base + (range->size - 1) <= endAddress) {
		// Get the next range for the iteration -- we need to skip free ranges,
		// since _FreeRange() might join them with the current range and delete
		// them.
		Range* nextRange = fRangeList.GetNext(range);
		while (nextRange != NULL && nextRange->type == Range::RANGE_FREE)
			nextRange = fRangeList.GetNext(nextRange);

		if (range->type == Range::RANGE_RESERVED) {
			_FreeRange(range, allocationFlags);
			Put();
		}

		range = nextRange;
	}

	PARANOIA_CHECK_STRUCTURES();
	return B_OK;
}


/**
 * @brief Releases all reserved ranges in the kernel address space.
 *
 * Iterates the entire range list from the head, freeing every RANGE_RESERVED
 * range encountered. Free ranges are skipped during iteration to avoid
 * iterator invalidation caused by _FreeRange() coalescing. Decrements the
 * address space reference count via Put() for each freed reservation.
 *
 * @param allocationFlags Slab deallocation flags forwarded to _FreeRange().
 *
 * @note Caller must hold the address space write lock. Intended for use during
 *       address space teardown.
 */
void
VMKernelAddressSpace::UnreserveAllAddressRanges(uint32 allocationFlags)
{
	Range* range = fRangeList.Head();
	while (range != NULL) {
		// Get the next range for the iteration -- we need to skip free ranges,
		// since _FreeRange() might join them with the current range and delete
		// them.
		Range* nextRange = fRangeList.GetNext(range);
		while (nextRange != NULL && nextRange->type == Range::RANGE_FREE)
			nextRange = fRangeList.GetNext(nextRange);

		if (range->type == Range::RANGE_RESERVED) {
			_FreeRange(range, allocationFlags);
			Put();
		}

		range = nextRange;
	}

	PARANOIA_CHECK_STRUCTURES();
}


/**
 * @brief Dumps the kernel address space state to the kernel debugger.
 *
 * Calls the base-class VMAddressSpace::Dump() and then iterates the
 * ordered range list, printing one line per Range with its type (area,
 * reserved, or free), base address, size, and—for areas—name and
 * protection flags.
 *
 * @note Safe to call from the kernel debugger (KDL) without any locks, as
 *       it only reads state.
 */
void
VMKernelAddressSpace::Dump() const
{
	VMAddressSpace::Dump();

	kprintf("range list:\n");

	for (RangeList::ConstIterator it = fRangeList.GetIterator();
			Range* range = it.Next();) {

		switch (range->type) {
			case Range::RANGE_AREA:
			{
				VMKernelArea* area = range->area;
				kprintf(" area %" B_PRId32 ": ", area->id);
				kprintf("base_addr = %#" B_PRIxADDR " ", area->Base());
				kprintf("size = %#" B_PRIxSIZE " ", area->Size());
				kprintf("name = '%s' ", area->name);
				kprintf("protection = %#" B_PRIx32 "\n", area->protection);
				break;
			}

			case Range::RANGE_RESERVED:
				kprintf(" reserved: base_addr = %#" B_PRIxADDR
					" reserved_base = %#" B_PRIxADDR " size = %#"
					B_PRIxSIZE " flags = %#" B_PRIx32 "\n", range->base,
					range->reserved.base, range->size, range->reserved.flags);
				break;

			case Range::RANGE_FREE:
				kprintf(" free: base_addr = %#" B_PRIxADDR " size = %#"
					B_PRIxSIZE "\n", range->base, range->size);
				break;
		}
	}
}


/**
 * @brief Inserts @p range into the free list whose bucket covers @p size.
 *
 * The bucket index is computed as ld(@p size) - PAGE_SHIFT. The Range is
 * appended to that bucket's list without further checks.
 *
 * @param range Pointer to the Range to insert. Must not already be in a free
 *              list.
 * @param size  Size value used to determine the free-list bucket. Typically
 *              equal to @c range->size.
 *
 * @note Internal helper. Caller is responsible for correct @p size and for
 *       updating range->type before calling.
 */
inline void
VMKernelAddressSpace::_FreeListInsertRange(Range* range, size_t size)
{
	TRACE("    VMKernelAddressSpace::_FreeListInsertRange(%p (%#" B_PRIxADDR
		", %#" B_PRIxSIZE ", %d), %#" B_PRIxSIZE " (%d))\n", range, range->base,
		range->size, range->type, size, ld(size) - PAGE_SHIFT);

	fFreeLists[ld(size) - PAGE_SHIFT].Add(range);
}


/**
 * @brief Removes @p range from the free list whose bucket covers @p size.
 *
 * The bucket index is computed as ld(@p size) - PAGE_SHIFT. The Range is
 * removed from that bucket's list.
 *
 * @param range Pointer to the Range to remove. Must be present in the free
 *              list corresponding to @p size.
 * @param size  Size value used to locate the free-list bucket. Must match the
 *              size that was passed to _FreeListInsertRange() for this range.
 *
 * @note Internal helper. The caller must remove the Range from the free list
 *       before modifying its size.
 */
inline void
VMKernelAddressSpace::_FreeListRemoveRange(Range* range, size_t size)
{
	TRACE("    VMKernelAddressSpace::_FreeListRemoveRange(%p (%#" B_PRIxADDR
		", %#" B_PRIxSIZE ", %d), %#" B_PRIxSIZE " (%d))\n", range, range->base,
		range->size, range->type, size, ld(size) - PAGE_SHIFT);

	fFreeLists[ld(size) - PAGE_SHIFT].Remove(range);
}


/**
 * @brief Inserts @p range into the range list and tree, and (if free) its
 *        free-list bucket.
 *
 * Finds the correct sorted position in fRangeList using fRangeTree, inserts
 * there, inserts into fRangeTree, then—if range->type is RANGE_FREE—calls
 * _FreeListInsertRange().
 *
 * @param range Pointer to the fully initialised Range to insert. Must not
 *              overlap any existing range.
 *
 * @note Internal helper. Does not modify fFreeSpace or the change counter.
 */
void
VMKernelAddressSpace::_InsertRange(Range* range)
{
	TRACE("    VMKernelAddressSpace::_InsertRange(%p (%#" B_PRIxADDR ", %#"
		B_PRIxSIZE ", %d))\n", range, range->base, range->size, range->type);

	// insert at the correct position in the range list
	Range* insertBeforeRange = fRangeTree.FindClosest(range->base, true);
	fRangeList.InsertBefore(
		insertBeforeRange != NULL
			? fRangeList.GetNext(insertBeforeRange) : fRangeList.Head(),
		range);

	// insert into tree
	fRangeTree.Insert(range);

	// insert in the free ranges list, if the range is free
	if (range->type == Range::RANGE_FREE)
		_FreeListInsertRange(range, range->size);
}


/**
 * @brief Removes @p range from the range list, tree, and (if free) its
 *        free-list bucket.
 *
 * Removes the range from fRangeTree and fRangeList. If range->type is
 * RANGE_FREE, also removes it from the corresponding free-list bucket via
 * _FreeListRemoveRange().
 *
 * @param range Pointer to the Range to remove. Must currently be present in
 *              both the range list and tree.
 *
 * @note Internal helper. Does not delete the Range object; the caller is
 *       responsible for returning it to fRangesObjectCache.
 */
void
VMKernelAddressSpace::_RemoveRange(Range* range)
{
	TRACE("    VMKernelAddressSpace::_RemoveRange(%p (%#" B_PRIxADDR ", %#"
		B_PRIxSIZE ", %d))\n", range, range->base, range->size, range->type);

	// remove from tree and range list
	// insert at the correct position in the range list
	fRangeTree.Remove(range);
	fRangeList.Remove(range);

	// if it is a free range, also remove it from the free list
	if (range->type == Range::RANGE_FREE)
		_FreeListRemoveRange(range, range->size);
}


/**
 * @brief Allocates a virtual address Range satisfying the given constraints.
 *
 * Rounds @p size up to the next page boundary, selects the allocation
 * strategy from @p addressRestrictions->address_specification, and delegates
 * to _FindFreeRange() to locate a suitable candidate. The found range is
 * split as necessary (head, tail, or both) to produce an exact-fit Range
 * for the allocation, and the leftover pieces are re-inserted as new free
 * or reserved Ranges. The allocated Range is removed from its free-list
 * bucket and returned in @p _range.
 *
 * @param addressRestrictions   Placement constraints (address, spec, alignment).
 * @param size                  Requested allocation size in bytes.
 * @param allowReservedRange    If @c true, reserved ranges may be used as
 *                              allocation targets (used by InsertArea() for
 *                              B_EXACT_ADDRESS).
 * @param allocationFlags       Slab allocation flags for splitting Range objects.
 * @param[out] _range           On success receives the allocated Range pointer.
 * @return @c B_OK on success.
 * @retval B_BAD_VALUE if B_EXACT_ADDRESS is specified and the address is not
 *                     page-aligned or the spot is unavailable.
 * @retval B_NO_MEMORY if no suitable range exists or a split allocation fails.
 *
 * @note Internal method. Caller must hold the address space write lock.
 */
status_t
VMKernelAddressSpace::_AllocateRange(
	const virtual_address_restrictions* addressRestrictions,
	size_t size, bool allowReservedRange, uint32 allocationFlags,
	Range*& _range)
{
	TRACE("  VMKernelAddressSpace::_AllocateRange(address: %p, size: %#"
		B_PRIxSIZE ", addressSpec: %#" B_PRIx32 ", reserved allowed: %d)\n",
		addressRestrictions->address, size,
		addressRestrictions->address_specification, allowReservedRange);

	// prepare size, alignment and the base address for the range search
	addr_t address = (addr_t)addressRestrictions->address;
	size = ROUNDUP(size, B_PAGE_SIZE);
	size_t alignment = addressRestrictions->alignment != 0
		? addressRestrictions->alignment : B_PAGE_SIZE;

	switch (addressRestrictions->address_specification) {
		case B_EXACT_ADDRESS:
		{
			if (address % B_PAGE_SIZE != 0)
				return B_BAD_VALUE;
			break;
		}

		case B_BASE_ADDRESS:
			address = ROUNDUP(address, B_PAGE_SIZE);
			break;

		case B_ANY_KERNEL_BLOCK_ADDRESS:
			// align the memory to the next power of two of the size
			while (alignment < size)
				alignment <<= 1;

			// fall through...

		case B_ANY_ADDRESS:
		case B_ANY_KERNEL_ADDRESS:
			address = fBase;
			break;

		default:
			return B_BAD_VALUE;
	}

	// find a range
	Range* range = _FindFreeRange(address, size, alignment,
		addressRestrictions->address_specification, allowReservedRange,
		address);
	if (range == NULL) {
		return addressRestrictions->address_specification == B_EXACT_ADDRESS
			? B_BAD_VALUE : B_NO_MEMORY;
	}

	TRACE("  VMKernelAddressSpace::_AllocateRange() found range:(%p (%#"
		B_PRIxADDR ", %#" B_PRIxSIZE ", %d)\n", range, range->base, range->size,
		range->type);

	// We have found a range. It might not be a perfect fit, in which case
	// we have to split the range.
	size_t rangeSize = range->size;

	if (address == range->base) {
		// allocation at the beginning of the range
		if (range->size > size) {
			// only partial -- split the range
			Range* leftOverRange = new(fRangesObjectCache, allocationFlags)
				Range(address + size, range->size - size, range);
			if (leftOverRange == NULL)
				return B_NO_MEMORY;

			range->size = size;
			_InsertRange(leftOverRange);
		}
	} else if (address + size == range->base + range->size) {
		// allocation at the end of the range -- split the range
		Range* leftOverRange = new(fRangesObjectCache, allocationFlags) Range(
			range->base, range->size - size, range);
		if (leftOverRange == NULL)
			return B_NO_MEMORY;

		range->base = address;
		range->size = size;
		_InsertRange(leftOverRange);
	} else {
		// allocation in the middle of the range -- split the range in three
		Range* leftOverRange1 = new(fRangesObjectCache, allocationFlags) Range(
			range->base, address - range->base, range);
		if (leftOverRange1 == NULL)
			return B_NO_MEMORY;
		Range* leftOverRange2 = new(fRangesObjectCache, allocationFlags) Range(
			address + size, range->size - size - leftOverRange1->size, range);
		if (leftOverRange2 == NULL) {
			object_cache_delete(fRangesObjectCache, leftOverRange1,
				allocationFlags);
			return B_NO_MEMORY;
		}

		range->base = address;
		range->size = size;
		_InsertRange(leftOverRange1);
		_InsertRange(leftOverRange2);
	}

	// If the range is a free range, remove it from the respective free list.
	if (range->type == Range::RANGE_FREE)
		_FreeListRemoveRange(range, rangeSize);

	IncrementChangeCount();

	TRACE("  VMKernelAddressSpace::_AllocateRange() -> %p (%#" B_PRIxADDR ", %#"
		B_PRIxSIZE ")\n", range, range->base, range->size);

	_range = range;
	return B_OK;
}


/**
 * @brief Finds a free (or optionally reserved) Range that can satisfy an
 *        allocation request.
 *
 * Implements the core placement policy for the kernel address space:
 * - B_BASE_ADDRESS: linear scan from @p start, falls through to B_ANY_ADDRESS
 *   if no match is found.
 * - B_ANY_ADDRESS / B_ANY_KERNEL_ADDRESS / B_ANY_KERNEL_BLOCK_ADDRESS: O(1)
 *   free-list search starting at the smallest bucket guaranteed to hold @p size,
 *   followed by a reserved-range scan if @p allowReservedRange is set.
 * - B_EXACT_ADDRESS: single tree lookup; the range must cover the exact spot
 *   and must be free (or reserved when @p allowReservedRange is set).
 *
 * @param start              Minimum base address (used for B_BASE_ADDRESS and
 *                           as a lower bound in B_ANY_* modes).
 * @param size               Allocation size in bytes (already page-rounded).
 * @param alignment          Required alignment of the returned base address.
 * @param addressSpec        One of B_EXACT_ADDRESS, B_BASE_ADDRESS,
 *                           B_ANY_ADDRESS, B_ANY_KERNEL_ADDRESS,
 *                           B_ANY_KERNEL_BLOCK_ADDRESS.
 * @param allowReservedRange If @c true, reserved ranges are also considered.
 * @param[out] _foundAddress On success receives the aligned base address
 *                           within the returned Range where the allocation
 *                           should be placed.
 * @return Pointer to the selected Range, or @c NULL if no suitable range
 *         was found.
 *
 * @note Internal method. Caller must hold the address space write lock.
 */
VMKernelAddressSpace::Range*
VMKernelAddressSpace::_FindFreeRange(addr_t start, size_t size,
	size_t alignment, uint32 addressSpec, bool allowReservedRange,
	addr_t& _foundAddress)
{
	TRACE("  VMKernelAddressSpace::_FindFreeRange(start: %#" B_PRIxADDR
		", size: %#" B_PRIxSIZE ", alignment: %#" B_PRIxSIZE ", addressSpec: %#"
		B_PRIx32 ", reserved allowed: %d)\n", start, size, alignment,
		addressSpec, allowReservedRange);

	switch (addressSpec) {
		case B_BASE_ADDRESS:
		{
			// We have to iterate through the range list starting at the given
			// address. This is the most inefficient case.
			Range* range = fRangeTree.FindClosest(start, true);
			while (range != NULL) {
				if (range->type == Range::RANGE_FREE) {
					addr_t alignedBase = ROUNDUP(range->base, alignment);
					if (is_valid_spot(start, alignedBase, size,
							range->base + (range->size - 1))) {
						_foundAddress = alignedBase;
						return range;
					}
				}
				range = fRangeList.GetNext(range);
			}

			// We didn't find a free spot in the requested range, so we'll
			// try again without any restrictions.
			start = fBase;
			addressSpec = B_ANY_ADDRESS;
			// fall through...
		}

		case B_ANY_ADDRESS:
		case B_ANY_KERNEL_ADDRESS:
		case B_ANY_KERNEL_BLOCK_ADDRESS:
		{
			// We want to allocate from the first non-empty free list that is
			// guaranteed to contain the size. Finding a free range is O(1),
			// unless there are constraints (min base address, alignment).
			int freeListIndex = ld((size * 2 - 1) >> PAGE_SHIFT);

			for (int32 i = freeListIndex; i < fFreeListCount; i++) {
				RangeFreeList& freeList = fFreeLists[i];
				if (freeList.IsEmpty())
					continue;

				for (RangeFreeList::Iterator it = freeList.GetIterator();
						Range* range = it.Next();) {
					addr_t alignedBase = ROUNDUP(range->base, alignment);
					if (is_valid_spot(start, alignedBase, size,
							range->base + (range->size - 1))) {
						_foundAddress = alignedBase;
						return range;
					}
				}
			}

			if (!allowReservedRange)
				return NULL;

			// We haven't found any free ranges, but we're supposed to look
			// for reserved ones, too. Iterate through the range list starting
			// at the given address.
			Range* range = fRangeTree.FindClosest(start, true);
			while (range != NULL) {
				if (range->type == Range::RANGE_RESERVED) {
					addr_t alignedBase = ROUNDUP(range->base, alignment);
					if (is_valid_spot(start, alignedBase, size,
							range->base + (range->size - 1))) {
						// allocation from the back might be preferred
						// -- adjust the base accordingly
						if ((range->reserved.flags & RESERVED_AVOID_BASE)
								!= 0) {
							alignedBase = ROUNDDOWN(
								range->base + (range->size - size), alignment);
						}

						_foundAddress = alignedBase;
						return range;
					}
				}
				range = fRangeList.GetNext(range);
			}

			return NULL;
		}

		case B_EXACT_ADDRESS:
		{
			Range* range = fRangeTree.FindClosest(start, true);
TRACE("    B_EXACT_ADDRESS: range: %p\n", range);
			if (range == NULL || range->type == Range::RANGE_AREA
				|| range->base + (range->size - 1) < start + (size - 1)) {
				// TODO: Support allocating if the area range covers multiple
				// free and reserved ranges!
TRACE("    -> no suitable range\n");
				return NULL;
			}

			if (range->type != Range::RANGE_FREE && !allowReservedRange)
{
TRACE("    -> reserved range not allowed\n");
				return NULL;
}

			_foundAddress = start;
			return range;
		}

		default:
			return NULL;
	}
}


/**
 * @brief Frees a Range back to the free pool, coalescing adjacent free ranges.
 *
 * Marks @p range as RANGE_FREE and merges it with any immediately preceding
 * or following free Range to minimise fragmentation. The merged result is
 * inserted into the appropriate free-list bucket. Surplus Range objects
 * (consumed during merging) are returned to fRangesObjectCache.
 *
 * @param range           The Range to free. Must currently be of type
 *                        RANGE_AREA or RANGE_RESERVED.
 * @param allocationFlags Slab deallocation flags forwarded to
 *                        object_cache_delete().
 *
 * @note Internal method. Does not update fFreeSpace; callers (RemoveArea,
 *       UnreserveAddressRange, etc.) are responsible for that. Caller must
 *       hold the address space write lock.
 */
void
VMKernelAddressSpace::_FreeRange(Range* range, uint32 allocationFlags)
{
	TRACE("  VMKernelAddressSpace::_FreeRange(%p (%#" B_PRIxADDR ", %#"
		B_PRIxSIZE ", %d))\n", range, range->base, range->size, range->type);

	// Check whether one or both of the neighboring ranges are free already,
	// and join them, if so.
	Range* previousRange = fRangeList.GetPrevious(range);
	Range* nextRange = fRangeList.GetNext(range);

	if (previousRange != NULL && previousRange->type == Range::RANGE_FREE) {
		if (nextRange != NULL && nextRange->type == Range::RANGE_FREE) {
			// join them all -- keep the first one, delete the others
			_FreeListRemoveRange(previousRange, previousRange->size);
			_RemoveRange(range);
			_RemoveRange(nextRange);
			previousRange->size += range->size + nextRange->size;
			object_cache_delete(fRangesObjectCache, range, allocationFlags);
			object_cache_delete(fRangesObjectCache, nextRange, allocationFlags);
			_FreeListInsertRange(previousRange, previousRange->size);
		} else {
			// join with the previous range only, delete the supplied one
			_FreeListRemoveRange(previousRange, previousRange->size);
			_RemoveRange(range);
			previousRange->size += range->size;
			object_cache_delete(fRangesObjectCache, range, allocationFlags);
			_FreeListInsertRange(previousRange, previousRange->size);
		}
	} else {
		if (nextRange != NULL && nextRange->type == Range::RANGE_FREE) {
			// join with the next range and delete it
			_RemoveRange(nextRange);
			range->size += nextRange->size;
			object_cache_delete(fRangesObjectCache, nextRange, allocationFlags);
		}

		// mark the range free and add it to the respective free list
		range->type = Range::RANGE_FREE;
		_FreeListInsertRange(range, range->size);
	}

	IncrementChangeCount();
}


#ifdef PARANOIA_CHECKS

/**
 * @brief Validates the internal consistency of the address space data
 *        structures (paranoia mode only).
 *
 * Performs a comprehensive structural audit:
 * - Verifies fRangeTree structural integrity via CheckTree().
 * - Walks fRangeList and fRangeTree in parallel, ensuring they contain the
 *   same Ranges in the same order.
 * - Checks that each Range starts exactly where the previous one ended,
 *   covering the complete address space without gaps.
 * - Ensures no two adjacent Ranges are both of type RANGE_FREE (they should
 *   have been coalesced).
 * - Ensures all Range sizes are nonzero and page-aligned.
 * - Validates each free-list bucket: all entries are of type RANGE_FREE,
 *   present in the range tree, and in the correct bucket for their size.
 *
 * Triggers a kernel panic with a descriptive message on the first detected
 * inconsistency.
 *
 * @note Compiled in only when PARANOIA_CHECKS is defined. Called via the
 *       PARANOIA_CHECK_STRUCTURES() macro after every mutating operation.
 */
void
VMKernelAddressSpace::_CheckStructures() const
{
	// general tree structure check
	fRangeTree.CheckTree();

	// check range list and tree
	size_t spaceSize = fEndAddress - fBase + 1;
	addr_t nextBase = fBase;
	Range* previousRange = NULL;
	int previousRangeType = Range::RANGE_AREA;
	uint64 freeRanges = 0;

	RangeList::ConstIterator listIt = fRangeList.GetIterator();
	RangeTree::ConstIterator treeIt = fRangeTree.GetIterator();
	while (true) {
		Range* range = listIt.Next();
		Range* treeRange = treeIt.Next();
		if (range != treeRange) {
			panic("VMKernelAddressSpace::_CheckStructures(): list/tree range "
				"mismatch: %p vs %p", range, treeRange);
		}
		if (range == NULL)
			break;

		if (range->base != nextBase) {
			panic("VMKernelAddressSpace::_CheckStructures(): range base %#"
				B_PRIxADDR ", expected: %#" B_PRIxADDR, range->base, nextBase);
		}

		if (range->size == 0) {
			panic("VMKernelAddressSpace::_CheckStructures(): empty range %p",
				range);
		}

		if (range->size % B_PAGE_SIZE != 0) {
			panic("VMKernelAddressSpace::_CheckStructures(): range %p (%#"
				B_PRIxADDR ", %#" B_PRIxSIZE ") not page aligned", range,
				range->base, range->size);
		}

		if (range->size > spaceSize - (range->base - fBase)) {
			panic("VMKernelAddressSpace::_CheckStructures(): range too large: "
				"(%#" B_PRIxADDR ", %#" B_PRIxSIZE "), address space end: %#"
				B_PRIxADDR, range->base, range->size, fEndAddress);
		}

		if (range->type == Range::RANGE_FREE) {
			freeRanges++;

			if (previousRangeType == Range::RANGE_FREE) {
				panic("VMKernelAddressSpace::_CheckStructures(): adjoining "
					"free ranges: %p (%#" B_PRIxADDR ", %#" B_PRIxSIZE
					"), %p (%#" B_PRIxADDR ", %#" B_PRIxSIZE ")", previousRange,
					previousRange->base, previousRange->size, range,
					range->base, range->size);
			}
		}

		previousRange = range;
		nextBase = range->base + range->size;
		previousRangeType = range->type;
	}

	if (nextBase - 1 != fEndAddress) {
		panic("VMKernelAddressSpace::_CheckStructures(): space not fully "
			"covered by ranges: last: %#" B_PRIxADDR ", expected %#" B_PRIxADDR,
			nextBase - 1, fEndAddress);
	}

	// check free lists
	uint64 freeListRanges = 0;
	for (int i = 0; i < fFreeListCount; i++) {
		RangeFreeList& freeList = fFreeLists[i];
		if (freeList.IsEmpty())
			continue;

		for (RangeFreeList::Iterator it = freeList.GetIterator();
				Range* range = it.Next();) {
			if (range->type != Range::RANGE_FREE) {
				panic("VMKernelAddressSpace::_CheckStructures(): non-free "
					"range %p (%#" B_PRIxADDR ", %#" B_PRIxSIZE ", %d) in "
					"free list %d", range, range->base, range->size,
					range->type, i);
			}

			if (fRangeTree.Find(range->base) != range) {
				panic("VMKernelAddressSpace::_CheckStructures(): unknown "
					"range %p (%#" B_PRIxADDR ", %#" B_PRIxSIZE ", %d) in "
					"free list %d", range, range->base, range->size,
					range->type, i);
			}

			if (ld(range->size) - PAGE_SHIFT != i) {
				panic("VMKernelAddressSpace::_CheckStructures(): "
					"range %p (%#" B_PRIxADDR ", %#" B_PRIxSIZE ", %d) in "
					"wrong free list %d", range, range->base, range->size,
					range->type, i);
			}

			freeListRanges++;
		}
	}
}

#endif	// PARANOIA_CHECKS
