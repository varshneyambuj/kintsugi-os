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
 *   Copyright 2002-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file VMUserAddressSpace.cpp
 * @brief Virtual address space management for user-space processes.
 *
 * Manages the layout of virtual memory regions (VMArea objects) within
 * the user portion of the address space. Handles area insertion, removal,
 * and the placement heuristics used when no explicit base address is given.
 *
 * @see VMKernelAddressSpace.cpp, VMAddressSpace.cpp
 */


#include "VMUserAddressSpace.h"

#include <stdlib.h>

#include <algorithm>

#include <KernelExport.h>

#include <heap.h>
#include <thread.h>
#include <util/atomic.h>
#include <util/Random.h>
#include <vm/vm.h>
#include <vm/VMArea.h>


//#define TRACE_VM
#ifdef TRACE_VM
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


#ifdef B_HAIKU_64_BIT
const addr_t VMUserAddressSpace::kMaxRandomize			=  0x8000000000ul;
const addr_t VMUserAddressSpace::kMaxInitialRandomize	= 0x20000000000ul;
#else
const addr_t VMUserAddressSpace::kMaxRandomize			=  0x800000ul;
const addr_t VMUserAddressSpace::kMaxInitialRandomize	= 0x2000000ul;
#endif


/**
 * @brief Verify that an aligned base address and size fit within a slot.
 *
 * Checks that \a alignedBase is not below \a base, that the region does not
 * wrap around the address space, and that the last byte of the region is no
 * greater than \a limit.
 *
 * @param base        Minimum acceptable start address for the region.
 * @param alignedBase Proposed (aligned) start address of the region.
 * @param size        Size in bytes of the region.
 * @param limit       Maximum acceptable last-byte address of the region.
 * @return @c true if the region fits, @c false otherwise.
 */
static inline bool
is_valid_spot(addr_t base, addr_t alignedBase, addr_t size, addr_t limit)
{
	return (alignedBase >= base && alignedBase + (size - 1) > alignedBase
		&& alignedBase + (size - 1) <= limit);
}


/**
 * @brief Return whether an address specification is a base-address hint.
 *
 * @param addressSpec One of the B_*ADDRESS constants.
 * @return @c true for B_BASE_ADDRESS or B_RANDOMIZED_BASE_ADDRESS.
 */
static inline bool
is_base_address_spec(uint32 addressSpec)
{
	return addressSpec == B_BASE_ADDRESS
		|| addressSpec == B_RANDOMIZED_BASE_ADDRESS;
}


/**
 * @brief Round \a address up to the next multiple of \a alignment.
 *
 * @param address   Address to align.
 * @param alignment Alignment in bytes; must be a power of two.
 * @return The smallest aligned address that is >= \a address.
 */
static inline addr_t
align_address(addr_t address, size_t alignment)
{
	return ROUNDUP(address, alignment);
}


/**
 * @brief Align \a address, honouring a base-address hint when applicable.
 *
 * When \a addressSpec is a base-address specification, the address is first
 * clamped to be no less than \a baseAddress, then rounded up to the next
 * aligned boundary.
 *
 * @param address     Address to align.
 * @param alignment   Alignment in bytes; must be a power of two.
 * @param addressSpec Address specification constant (B_BASE_ADDRESS etc.).
 * @param baseAddress Hint base address applied when addressSpec is a
 *                    base-address variant.
 * @return Aligned address satisfying both the hint and the alignment.
 */
static inline addr_t
align_address(addr_t address, size_t alignment, uint32 addressSpec,
	addr_t baseAddress)
{
	if (is_base_address_spec(addressSpec))
		address = std::max(address, baseAddress);
	return align_address(address, alignment);
}


// #pragma mark - VMUserAddressSpace


/**
 * @brief Construct a VMUserAddressSpace for the given team.
 *
 * Delegates to the VMAddressSpace base constructor and initialises the
 * next-insert hint to zero. The hint is updated lazily as areas are inserted.
 *
 * @param id   Team identifier that owns this address space.
 * @param base Lowest virtual address belonging to the address space.
 * @param size Total size in bytes of the address space.
 */
VMUserAddressSpace::VMUserAddressSpace(team_id id, addr_t base, size_t size)
	:
	VMAddressSpace(id, base, size, "address space"),
	fNextInsertHint(0)
{
}


/**
 * @brief Destroy a VMUserAddressSpace.
 *
 * All VMArea objects must have been removed before this destructor is called.
 * The base class destructor handles lock teardown.
 */
VMUserAddressSpace::~VMUserAddressSpace()
{
}


/**
 * @brief Return the first non-reserved area in the address space.
 *
 * Walks from the left-most node in the area tree and skips any entries whose
 * ID equals RESERVED_AREA_ID.
 *
 * @note The caller must hold at least the read lock on the address space.
 *
 * @return Pointer to the first real VMArea, or @c NULL if the address space
 *         contains no non-reserved areas.
 */
inline VMArea*
VMUserAddressSpace::FirstArea() const
{
	VMUserArea* area = fAreas.LeftMost();
	while (area != NULL && area->id == RESERVED_AREA_ID)
		area = fAreas.Next(area);
	return area;
}


/**
 * @brief Return the next non-reserved area after \a _area.
 *
 * Advances to the in-order successor of \a _area in the area tree and skips
 * any entries with RESERVED_AREA_ID.
 *
 * @note The caller must hold at least the read lock on the address space.
 *
 * @param _area The current area; must not be @c NULL.
 * @return Pointer to the next real VMArea, or @c NULL if there is none.
 */
inline VMArea*
VMUserAddressSpace::NextArea(VMArea* _area) const
{
	VMUserArea* area = static_cast<VMUserArea*>(_area);
	area = fAreas.Next(area);
	while (area != NULL && area->id == RESERVED_AREA_ID)
		area = fAreas.Next(area);
	return area;
}


/**
 * @brief Allocate a new VMUserArea object.
 *
 * Delegates to VMUserArea::Create() with the supplied parameters.
 *
 * @param name             Human-readable name for the area.
 * @param wiring           Wiring type (B_NO_LOCK, B_FULL_LOCK, etc.).
 * @param protection       Page-protection flags (B_READ_AREA, etc.).
 * @param allocationFlags  Heap allocation flags (e.g. HEAP_DONT_WAIT_FOR_MEMORY).
 * @return Newly allocated VMUserArea, or @c NULL on allocation failure.
 */
VMArea*
VMUserAddressSpace::CreateArea(const char* name, uint32 wiring,
	uint32 protection, uint32 allocationFlags)
{
	return VMUserArea::Create(this, name, wiring, protection, allocationFlags);
}


/**
 * @brief Destroy a VMUserArea and free its storage.
 *
 * Calls the VMUserArea destructor explicitly (rather than via @c delete) and
 * then releases the backing memory using free_etc() with \a allocationFlags.
 *
 * @param _area           Area to destroy; must be a VMUserArea.
 * @param allocationFlags Flags controlling how the backing memory is freed.
 */
void
VMUserAddressSpace::DeleteArea(VMArea* _area, uint32 allocationFlags)
{
	VMUserArea* area = static_cast<VMUserArea*>(_area);
	area->~VMUserArea();
	free_etc(area, allocationFlags);
}


/**
 * @brief Find the area that contains \a address.
 *
 * Uses the red-black tree to locate the closest area whose base is <= \a
 * address, then verifies that \a address falls within that area's extent.
 * Reserved areas (RESERVED_AREA_ID) are excluded from the result.
 *
 * @note The caller must hold at least the read lock on the address space.
 *
 * @param address Virtual address to look up.
 * @return Pointer to the containing VMArea, or @c NULL if no non-reserved
 *         area contains \a address.
 */
VMArea*
VMUserAddressSpace::LookupArea(addr_t address) const
{
	VMUserArea* area = fAreas.FindClosest(address, true);
	if (area == NULL || area->id == RESERVED_AREA_ID)
		return NULL;

	return area->ContainsAddress(address) ? area : NULL;
}


/**
 * @brief Find the closest non-reserved area to \a address.
 *
 * Searches the tree for the nearest area relative to \a address in the
 * direction specified by \a less. Reserved areas are skipped by walking
 * further in the same direction.
 *
 * @note The caller must hold at least the read lock on the address space.
 *
 * @param address Virtual address used as the search key.
 * @param less    If @c true, find the closest area with base <= \a address;
 *                if @c false, find the closest area with base >= \a address.
 * @return Pointer to the closest non-reserved VMArea, or @c NULL if none
 *         exists in the requested direction.
 */
VMArea*
VMUserAddressSpace::FindClosestArea(addr_t address, bool less) const
{
	VMUserArea* area = fAreas.FindClosest(address, less);
	while (area != NULL && area->id == RESERVED_AREA_ID)
		area = less ? fAreas.Previous(area) : fAreas.Next(area);
	return area;
}


/**
 * @brief Insert \a area into the address space at a location chosen according
 *        to \a addressRestrictions.
 *
 * Computes a valid search range from \a addressRestrictions, then delegates to
 * _InsertAreaSlot() to find and claim a free virtual slot of \a size bytes.
 * On success, \a *_address is set to the area's base address and the
 * address space's free-space counter is decremented.
 *
 * @note The caller must hold the write lock on the address space
 *       (ASSERT_WRITE_LOCKED_RW_LOCK is checked internally).
 *
 * @param _area                  Area object to insert (must be a VMUserArea).
 * @param size                   Size in bytes of the area.
 * @param addressRestrictions    Placement constraints (specification, hint
 *                               address, alignment).
 * @param allocationFlags        Heap flags forwarded to sub-allocations.
 * @param[out] _address          Receives the chosen base address on success;
 *                               may be @c NULL if the caller does not need it.
 * @retval B_OK          Area inserted successfully.
 * @retval B_BAD_VALUE   Invalid address specification or exact address is
 *                       already occupied.
 * @retval B_NO_MEMORY   No sufficiently large free slot exists.
 */
status_t
VMUserAddressSpace::InsertArea(VMArea* _area, size_t size,
	const virtual_address_restrictions* addressRestrictions,
	uint32 allocationFlags, void** _address)
{
	ASSERT_WRITE_LOCKED_RW_LOCK(&fLock);

	VMUserArea* area = static_cast<VMUserArea*>(_area);

	addr_t searchBase, searchEnd;
	status_t status;

	switch (addressRestrictions->address_specification) {
		case B_EXACT_ADDRESS:
			searchBase = (addr_t)addressRestrictions->address;
			searchEnd = (addr_t)addressRestrictions->address + (size - 1);
			break;

		case B_BASE_ADDRESS:
		case B_RANDOMIZED_BASE_ADDRESS:
			searchBase = std::max(fBase, (addr_t)addressRestrictions->address);
			searchEnd = fEndAddress;
			break;

		case B_ANY_ADDRESS:
		case B_ANY_KERNEL_ADDRESS:
		case B_ANY_KERNEL_BLOCK_ADDRESS:
		case B_RANDOMIZED_ANY_ADDRESS:
			searchBase = std::max(fBase, (addr_t)USER_BASE_ANY);
			searchEnd = fEndAddress;
			break;

		default:
			return B_BAD_VALUE;
	}

	status = _InsertAreaSlot(searchBase, size, searchEnd,
		addressRestrictions->address_specification,
		addressRestrictions->alignment, area, allocationFlags);
	if (status == B_OK) {
		if (_address != NULL)
			*_address = (void*)area->Base();
		fFreeSpace -= area->Size();
	}

	return status;
}


/**
 * @brief Remove \a area from the address space tree.
 *
 * Removes the area from the internal red-black tree, increments the change
 * counter for non-reserved areas, restores the freed bytes to the free-space
 * counter, and retracts the next-insert hint if the removed area was at its
 * tip.
 *
 * @note The caller must hold the write lock on the address space.
 *
 * @param _area           Area to remove; must be currently inserted.
 * @param allocationFlags Forwarded to potential sub-operations (currently
 *                        unused in this function body).
 */
void
VMUserAddressSpace::RemoveArea(VMArea* _area, uint32 allocationFlags)
{
	ASSERT_WRITE_LOCKED_RW_LOCK(&fLock);

	VMUserArea* area = static_cast<VMUserArea*>(_area);

	fAreas.Remove(area);

	if (area->id != RESERVED_AREA_ID) {
		IncrementChangeCount();
		fFreeSpace += area->Size();
	}

	if ((area->Base() + area->Size()) == fNextInsertHint)
		fNextInsertHint -= area->Size();
}


/**
 * @brief Check whether an area can be resized to \a newSize in place.
 *
 * Walks the areas that immediately follow \a area (in address order) to
 * determine whether the new extent overlaps only reserved areas (which may be
 * consumed) or empty space, stopping at the first non-reserved area or the
 * end of the address space.
 *
 * @param area    Area whose resize feasibility is to be tested.
 * @param newSize Proposed new size in bytes.
 * @return @c true if the area can be resized to \a newSize without evicting
 *         any non-reserved area, @c false otherwise.
 */
bool
VMUserAddressSpace::CanResizeArea(VMArea* area, size_t newSize)
{
	const addr_t newEnd = area->Base() + (newSize - 1);
	if (newEnd < area->Base())
		return false;

	VMUserArea* next = fAreas.Next(static_cast<VMUserArea*>(area));
	while (next != NULL) {
		if (next->Base() > newEnd)
			return true;

		// If the next area is a reservation, then we can resize into it.
		if (next->id != RESERVED_AREA_ID)
			return false;
		if ((next->Base() + (next->Size() - 1)) >= newEnd)
			return true;

		// This "next" area is a reservation, but it's not large enough.
		// See if we can resize past it as well.
		next = fAreas.Next(next);
	}

	return fEndAddress >= newEnd;
}


/**
 * @brief Resize \a area to \a newSize, adjusting or removing reserved regions.
 *
 * When growing, any reserved areas that fall within the new extent are either
 * shrunk (via ShrinkAreaHead()) or entirely removed. When shrinking, an
 * adjacent reserved area at the old tail is expanded back towards its
 * original base (as stored in cache_offset).
 *
 * @note CanResizeArea() should be called first to verify the resize is safe.
 *
 * @param _area           Area to resize.
 * @param newSize         Desired new size in bytes.
 * @param allocationFlags Heap flags used when freeing consumed reserved areas.
 * @retval B_OK    Resize completed successfully.
 * @retval B_ERROR An internal consistency check failed (should not occur if
 *                 CanResizeArea() returned @c true).
 */
status_t
VMUserAddressSpace::ResizeArea(VMArea* _area, size_t newSize,
	uint32 allocationFlags)
{
	VMUserArea* area = static_cast<VMUserArea*>(_area);

	const addr_t oldEnd = area->Base() + (area->Size() - 1);
	const addr_t newEnd = area->Base() + (newSize - 1);

	VMUserArea* next = fAreas.Next(area);
	if (oldEnd < newEnd) {
		while (next != NULL) {
			if (next->Base() > newEnd)
				break;

			if (next->id != RESERVED_AREA_ID && next->Base() < newEnd) {
				panic("resize situation for area %p has changed although we "
					"should have the address space lock", area);
				return B_ERROR;
			}

			// shrink reserved area
			addr_t offset = (area->Base() + newSize) - next->Base();
			if (next->Size() <= offset) {
				VMUserArea* nextNext = fAreas.Next(next);
				RemoveArea(next, allocationFlags);
				Put();
				next->~VMUserArea();
				free_etc(next, allocationFlags);
				next = nextNext;
			} else {
				status_t error = ShrinkAreaHead(next, next->Size() - offset,
					allocationFlags);
				if (error != B_OK)
					return error;
				break;
			}
		}
	} else {
		if (next != NULL && next->id == RESERVED_AREA_ID
				&& next->Base() == (oldEnd + 1)) {
			// expand reserved area (at most to its original size)
			const addr_t oldNextBase = next->Base();
			addr_t newNextBase = oldNextBase - (oldEnd - newEnd);
			if (newNextBase < (addr_t)next->cache_offset)
				newNextBase = next->cache_offset;

			next->SetBase(newNextBase);
			next->SetSize(next->Size() + (oldNextBase - newNextBase));
		}
	}

	area->SetSize(newSize);
	return B_OK;
}


/**
 * @brief Shrink \a area by removing bytes from its head (lowest addresses).
 *
 * Advances the base address by @c (oldSize - \a size) and sets the new size,
 * effectively discarding the low portion of the area's virtual range.
 *
 * @param area            Area to shrink.
 * @param size            New (smaller) size in bytes.
 * @param allocationFlags Unused; present for API uniformity.
 * @retval B_OK Always succeeds (no-op when \a size equals current size).
 */
status_t
VMUserAddressSpace::ShrinkAreaHead(VMArea* area, size_t size,
	uint32 allocationFlags)
{
	size_t oldSize = area->Size();
	if (size == oldSize)
		return B_OK;

	area->SetBase(area->Base() + oldSize - size);
	area->SetSize(size);

	return B_OK;
}


/**
 * @brief Shrink \a area by removing bytes from its tail (highest addresses).
 *
 * Reduces the size of the area, discarding the high portion of its virtual
 * range. The base address is unchanged.
 *
 * @param area            Area to shrink.
 * @param size            New (smaller) size in bytes.
 * @param allocationFlags Unused; present for API uniformity.
 * @retval B_OK Always succeeds (no-op when \a size equals current size).
 */
status_t
VMUserAddressSpace::ShrinkAreaTail(VMArea* area, size_t size,
	uint32 allocationFlags)
{
	size_t oldSize = area->Size();
	if (size == oldSize)
		return B_OK;

	area->SetSize(size);

	return B_OK;
}


/**
 * @brief Reserve a range of virtual addresses, preventing their use by
 *        subsequent area allocations.
 *
 * Creates a VMUserArea with RESERVED_AREA_ID and inserts it via InsertArea().
 * The original base address is stored in @c cache_offset so that a later
 * ResizeArea() shrink can restore the reservation to its original boundary.
 *
 * @param size                 Number of bytes to reserve.
 * @param addressRestrictions  Placement constraints for the reservation.
 * @param flags                Protection flags stored on the reserved area.
 * @param allocationFlags      Heap flags for the VMUserArea allocation.
 * @param[out] _address        Receives the reserved base address on success.
 * @retval B_OK         Reservation placed successfully.
 * @retval B_BAD_TEAM_ID The address space is being deleted.
 * @retval B_NO_MEMORY  No sufficiently large free slot exists or allocation
 *                      failed.
 */
status_t
VMUserAddressSpace::ReserveAddressRange(size_t size,
	const virtual_address_restrictions* addressRestrictions,
	uint32 flags, uint32 allocationFlags, void** _address)
{
	// check to see if this address space has entered DELETE state
	if (fDeleting) {
		// okay, someone is trying to delete this address space now, so we
		// can't insert the area, let's back out
		return B_BAD_TEAM_ID;
	}

	VMUserArea* area = VMUserArea::CreateReserved(this, flags, allocationFlags);
	if (area == NULL)
		return B_NO_MEMORY;

	status_t status = InsertArea(area, size, addressRestrictions,
		allocationFlags, _address);
	if (status != B_OK) {
		area->~VMUserArea();
		free_etc(area, allocationFlags);
		return status;
	}

	area->cache_offset = area->Base();
		// we cache the original base address here

	Get();
	return B_OK;
}


/**
 * @brief Release all reserved areas that overlap [\a address, \a address +
 *        \a size).
 *
 * Iterates over reserved areas whose last byte falls within the specified
 * range, removes each from the tree, decrements the address-space reference
 * count, and frees the VMUserArea.
 *
 * @param address         Start of the range to unreserve.
 * @param size            Length of the range in bytes.
 * @param allocationFlags Heap flags used when freeing VMUserArea objects.
 * @retval B_OK         Always (errors abort silently if the space is deleting).
 * @retval B_BAD_TEAM_ID The address space is being deleted.
 */
status_t
VMUserAddressSpace::UnreserveAddressRange(addr_t address, size_t size,
	uint32 allocationFlags)
{
	// check to see if this address space has entered DELETE state
	if (fDeleting) {
		// okay, someone is trying to delete this address space now, so we can't
		// remove the area, so back out
		return B_BAD_TEAM_ID;
	}

	// the area must be completely part of the reserved range
	VMUserArea* area = fAreas.FindClosest(address, false);
	if (area == NULL)
		return B_OK;

	addr_t endAddress = address + size - 1;
	for (VMUserAreaTree::Iterator it = fAreas.GetIterator(area);
		(area = it.Next()) != NULL
			&& area->Base() + area->Size() - 1 <= endAddress;) {

		if (area->id == RESERVED_AREA_ID) {
			// remove reserved range
			RemoveArea(area, allocationFlags);
			Put();
			area->~VMUserArea();
			free_etc(area, allocationFlags);
		}
	}

	return B_OK;
}


/**
 * @brief Remove all reserved areas from the address space.
 *
 * Walks the full area tree and removes every entry with RESERVED_AREA_ID,
 * freeing the associated VMUserArea objects and decrementing the address-space
 * reference count for each.
 *
 * @param allocationFlags Heap flags used when freeing VMUserArea objects.
 */
void
VMUserAddressSpace::UnreserveAllAddressRanges(uint32 allocationFlags)
{
	for (VMUserAreaTree::Iterator it = fAreas.GetIterator();
			VMUserArea* area = it.Next();) {
		if (area->id == RESERVED_AREA_ID) {
			RemoveArea(area, allocationFlags);
			Put();
			area->~VMUserArea();
			free_etc(area, allocationFlags);
		}
	}
}


/**
 * @brief Print a summary of the address space and all its areas to the kernel
 *        debugger.
 *
 * Calls the base-class Dump() for header information, then iterates over all
 * areas (including reserved ranges) and prints each area's ID, base address,
 * size, name and protection flags via kprintf().
 *
 * @note Intended for use from a KDL command only.
 */
void
VMUserAddressSpace::Dump() const
{
	VMAddressSpace::Dump();
	kprintf("area_list:\n");

	for (VMUserAreaTree::ConstIterator it = fAreas.GetIterator();
			VMUserArea* area = it.Next();) {
		kprintf(" area 0x%" B_PRIx32 ": ", area->id);
		kprintf("base_addr = 0x%lx ", area->Base());
		kprintf("size = 0x%lx ", area->Size());
		kprintf("name = '%s' ",
			area->id != RESERVED_AREA_ID ? area->name : "reserved");
		kprintf("protection = 0x%" B_PRIx32 "\n", area->protection);
	}
}


/**
 * @brief Return whether the given address specification requests randomization.
 *
 * Checks both the global randomization-enabled flag and whether \a addressSpec
 * is one of the randomized placement constants.
 *
 * @param addressSpec Address specification constant to test.
 * @return @c true if randomization is both globally enabled and requested by
 *         \a addressSpec.
 */
inline bool
VMUserAddressSpace::_IsRandomized(uint32 addressSpec) const
{
	return fRandomizingEnabled
		&& (addressSpec == B_RANDOMIZED_ANY_ADDRESS
			|| addressSpec == B_RANDOMIZED_BASE_ADDRESS);
}


/**
 * @brief Pick a random aligned address within [\a start, \a end].
 *
 * Draws a cryptographically random value, reduces it modulo the effective
 * range (clamped to kMaxInitialRandomize or kMaxRandomize depending on
 * \a initial), and aligns it to \a alignment.
 *
 * @param start     Inclusive lower bound; must already be aligned.
 * @param end       Inclusive upper bound.
 * @param alignment Alignment in bytes (must be a power of two).
 * @param initial   If @c true, use kMaxInitialRandomize to limit the range
 *                  (suitable for the initial stack/heap placement); otherwise
 *                  use kMaxRandomize.
 * @return A randomly chosen address in [\a start, \a start + range) that is
 *         aligned to \a alignment.
 */
addr_t
VMUserAddressSpace::_RandomizeAddress(addr_t start, addr_t end,
	size_t alignment, bool initial)
{
	ASSERT((start & addr_t(alignment - 1)) == 0);
	ASSERT(start <= end);

	if (start == end)
		return start;

	addr_t range = end - start + 1;
	if (initial)
		range = std::min(range, kMaxInitialRandomize);
	else
		range = std::min(range, kMaxRandomize);

	addr_t random = secure_get_random<addr_t>();
	random %= range;
	random &= ~addr_t(alignment - 1);

	return start + random;
}


/**
 * @brief Insert \a area at exactly [\a start, \a start + \a size) within an
 *        existing reserved region.
 *
 * Finds the reserved area that fully contains the requested range, removes or
 * splits it as necessary to make room, sets \a area's base and size, and
 * inserts it into the tree.
 *
 * Three sub-cases are handled:
 *  - The new area covers the entire reserved range: the reserved area is removed.
 *  - The new area starts at the beginning: the reserved area is shrunk from its head.
 *  - The new area ends at the tail: the reserved area is shrunk from its tail.
 *  - The new area is in the middle: the reserved area is split into two.
 *
 * @param start           Exact virtual base address for the new area.
 * @param size            Size in bytes of the new area.
 * @param area            VMUserArea to insert.
 * @param allocationFlags Heap flags used when allocating the split reservation.
 * @retval B_OK             Insertion succeeded.
 * @retval B_ENTRY_NOT_FOUND No reserved region covers the full requested range.
 * @retval B_BAD_VALUE      The covering region is not a reserved area.
 * @retval B_NO_MEMORY      Could not allocate a new reserved area for the split.
 */
status_t
VMUserAddressSpace::_InsertAreaIntoReservedRegion(addr_t start, size_t size,
	VMUserArea* area, uint32 allocationFlags)
{
	VMUserArea* reserved = fAreas.FindClosest(start, true);
	if (reserved == NULL
		|| !reserved->ContainsAddress(start)
		|| !reserved->ContainsAddress(start + size - 1)) {
		return B_ENTRY_NOT_FOUND;
	}

	// This area covers the requested range
	if (reserved->id != RESERVED_AREA_ID) {
		// but it's not reserved space, it's a real area
		return B_BAD_VALUE;
	}

	// Now we have to transfer the requested part of the reserved
	// range to the new area - and remove, resize or split the old
	// reserved area.

	if (start == reserved->Base()) {
		// the area starts at the beginning of the reserved range

		if (size == reserved->Size()) {
			// the new area fully covers the reserved range
			fAreas.Remove(reserved);
			Put();
			reserved->~VMUserArea();
			free_etc(reserved, allocationFlags);
		} else {
			// resize the reserved range behind the area
			reserved->SetBase(reserved->Base() + size);
			reserved->SetSize(reserved->Size() - size);
		}
	} else if (start + size == reserved->Base() + reserved->Size()) {
		// the area is at the end of the reserved range
		// resize the reserved range before the area
		reserved->SetSize(start - reserved->Base());
	} else {
		// the area splits the reserved range into two separate ones
		// we need a new reserved area to cover this space
		VMUserArea* newReserved = VMUserArea::CreateReserved(this,
			reserved->protection, allocationFlags);
		if (newReserved == NULL)
			return B_NO_MEMORY;

		Get();

		// resize regions
		newReserved->SetBase(start + size);
		newReserved->SetSize(
			reserved->Base() + reserved->Size() - start - size);
		newReserved->cache_offset = reserved->cache_offset;

		reserved->SetSize(start - reserved->Base());

		fAreas.Insert(newReserved);
	}

	area->SetBase(start);
	area->SetSize(size);
	fAreas.Insert(area);
	IncrementChangeCount();

	return B_OK;
}


/**
 * @brief Core placement engine: find a free virtual slot and insert \a area.
 *
 * Searches the address space for a free region of \a size bytes that satisfies
 * \a addressSpec (and optionally \a alignment) within [\a start, \a end].
 * The function implements several placement strategies:
 *
 *  - **B_EXACT_ADDRESS**: first tries _InsertAreaIntoReservedRegion(); if that
 *    finds no reservation, checks for a genuinely empty slot.
 *  - **B_ANY_ADDRESS / B_ANY_KERNEL_***: linear scan from \a start, optionally
 *    using the fNextInsertHint to skip already-used space.
 *  - **B_BASE_ADDRESS / B_RANDOMIZED_BASE_ADDRESS**: like B_ANY_ADDRESS but
 *    clamped to start no lower than the specified base. Falls back to a full
 *    scan if no slot is found above the base.
 *  - **B_RANDOMIZED_ANY_ADDRESS**: same as B_ANY_ADDRESS but randomises the
 *    chosen offset within each candidate range.
 *
 * If no slot is found in the primary range, reserved areas are also scanned
 * as a last resort for B_ANY_* specifications.
 *
 * On success, sets \a area's base address and size, inserts the area into the
 * tree, increments the change counter, and updates fNextInsertHint.
 *
 * @note Must be called with the address space write lock held.
 *
 * @param start           First acceptable virtual address.
 * @param size            Required size in bytes.
 * @param end             Last acceptable virtual address (inclusive).
 * @param addressSpec     One of the B_*ADDRESS constants controlling placement.
 * @param alignment       Required alignment in bytes (0 means B_PAGE_SIZE).
 * @param area            VMUserArea to place; its base and size are set on
 *                        success.
 * @param allocationFlags Heap flags forwarded to _InsertAreaIntoReservedRegion().
 * @retval B_OK          Area placed and inserted successfully.
 * @retval B_BAD_ADDRESS Input range is inconsistent or outside the address space.
 * @retval B_BAD_VALUE   B_EXACT_ADDRESS requested but the slot is occupied.
 * @retval B_NO_MEMORY   No suitable free region was found.
 */
status_t
VMUserAddressSpace::_InsertAreaSlot(addr_t start, addr_t size, addr_t end,
	uint32 addressSpec, size_t alignment, VMUserArea* area,
	uint32 allocationFlags)
{
	TRACE(("VMUserAddressSpace::_InsertAreaSlot: address space %p, start "
		"0x%lx, size %ld, end 0x%lx, addressSpec %" B_PRIu32 ", area %p\n",
		this, start, size, end, addressSpec, area));

	// do some sanity checking
	if (start < fBase || size == 0 || end > fEndAddress
		|| start + (size - 1) > end)
		return B_BAD_ADDRESS;

	if (addressSpec == B_EXACT_ADDRESS && area->id != RESERVED_AREA_ID) {
		// search for a reserved area
		status_t status = _InsertAreaIntoReservedRegion(start, size, area,
			allocationFlags);
		if (status == B_OK || status == B_BAD_VALUE)
			return status;

		// There was no reserved area, and the slot doesn't seem to be used
		// already
		// TODO: this could be further optimized.
	}

	if (alignment == 0)
		alignment = B_PAGE_SIZE;
	if (addressSpec == B_ANY_KERNEL_BLOCK_ADDRESS) {
		// align the memory to the next power of two of the size
		while (alignment < size)
			alignment <<= 1;
	}

	start = align_address(start, alignment);

	bool useHint = addressSpec != B_EXACT_ADDRESS
		&& !is_base_address_spec(addressSpec)
		&& fFreeSpace > (Size() / 2);

	addr_t originalStart = 0;
	if (fRandomizingEnabled && addressSpec == B_RANDOMIZED_BASE_ADDRESS) {
		originalStart = start;
		start = _RandomizeAddress(start, end - size + 1, alignment, true);
	} else if (useHint
			&& start <= fNextInsertHint && fNextInsertHint <= (end - size + 1)) {
		originalStart = start;
		start = fNextInsertHint;
	}

	// walk up to the spot where we should start searching
second_chance:
	VMUserArea* next = fAreas.FindClosest(start + size, false);
	VMUserArea* last = next != NULL
		? fAreas.Previous(next) : fAreas.FindClosest(start + size, true);

	// find the right spot depending on the address specification - the area
	// will be inserted directly after "last" ("next" is not referenced anymore)

	bool foundSpot = false;
	switch (addressSpec) {
		case B_ANY_ADDRESS:
		case B_ANY_KERNEL_ADDRESS:
		case B_ANY_KERNEL_BLOCK_ADDRESS:
		case B_RANDOMIZED_ANY_ADDRESS:
		case B_BASE_ADDRESS:
		case B_RANDOMIZED_BASE_ADDRESS:
		{
			VMUserAreaTree::Iterator it = fAreas.GetIterator(
				next != NULL ? next : fAreas.LeftMost());

			// find a hole big enough for a new area
			if (last == NULL) {
				// see if we can build it at the beginning of the virtual map
				addr_t alignedBase = align_address(start, alignment);
				addr_t nextBase = next == NULL
					? end : std::min(next->Base() - 1, end);
				if (is_valid_spot(start, alignedBase, size, nextBase)) {
					addr_t rangeEnd = std::min(nextBase - size + 1, end);
					if (_IsRandomized(addressSpec)) {
						alignedBase = _RandomizeAddress(alignedBase, rangeEnd,
							alignment);
					}

					foundSpot = true;
					area->SetBase(alignedBase);
					break;
				}

				last = next;
				next = it.Next();
			}

			// keep walking
			while (next != NULL && next->Base() + next->Size() - 1 <= end) {
				addr_t alignedBase = align_address(last->Base() + last->Size(),
					alignment, addressSpec, start);
				addr_t nextBase = std::min(end, next->Base() - 1);

				if (is_valid_spot(last->Base() + (last->Size() - 1),
						alignedBase, size, nextBase)) {
					addr_t rangeEnd = std::min(nextBase - size + 1, end);
					if (_IsRandomized(addressSpec)) {
						alignedBase = _RandomizeAddress(alignedBase,
							rangeEnd, alignment);
					}

					foundSpot = true;
					area->SetBase(alignedBase);
					break;
				}

				last = next;
				next = it.Next();
			}

			if (foundSpot)
				break;

			addr_t alignedBase = align_address(last->Base() + last->Size(),
				alignment, addressSpec, start);

			if (next == NULL && is_valid_spot(last->Base() + (last->Size() - 1),
					alignedBase, size, end)) {
				if (_IsRandomized(addressSpec)) {
					alignedBase = _RandomizeAddress(alignedBase, end - size + 1,
						alignment);
				}

				// got a spot
				foundSpot = true;
				area->SetBase(alignedBase);
				break;
			} else if (is_base_address_spec(addressSpec)) {
				// we didn't find a free spot in the requested range, so we'll
				// try again without any restrictions
				if (!_IsRandomized(addressSpec)) {
					start = USER_BASE_ANY;
					addressSpec = B_ANY_ADDRESS;
				} else if (start == originalStart) {
					start = USER_BASE_ANY;
					addressSpec = B_RANDOMIZED_ANY_ADDRESS;
				} else {
					start = originalStart;
					addressSpec = B_RANDOMIZED_BASE_ADDRESS;
				}

				goto second_chance;
			} else if (useHint
					&& originalStart != 0 && start != originalStart) {
				start = originalStart;
				goto second_chance;
			} else if (area->id != RESERVED_AREA_ID) {
				// We didn't find a free spot - if there are any reserved areas,
				// we can now test those for free space
				// TODO: it would make sense to start with the biggest of them
				it = fAreas.GetIterator();
				next = it.Next();
				for (last = NULL; next != NULL; next = it.Next()) {
					if (next->id != RESERVED_AREA_ID) {
						last = next;
						continue;
					} else if (next->Base() + size - 1 > end)
						break;

					// TODO: take free space after the reserved area into
					// account!
					addr_t alignedBase = align_address(next->Base(), alignment);
					if (next->Base() == alignedBase && next->Size() == size) {
						// The reserved area is entirely covered, and thus,
						// removed
						fAreas.Remove(next);

						foundSpot = true;
						area->SetBase(alignedBase);
						next->~VMUserArea();
						free_etc(next, allocationFlags);
						break;
					}

					if ((next->protection & RESERVED_AVOID_BASE) == 0
						&& alignedBase == next->Base()
						&& next->Size() >= size) {
						addr_t rangeEnd = std::min(
							next->Base() + next->Size() - size, end);
						if (_IsRandomized(addressSpec)) {
							alignedBase = _RandomizeAddress(next->Base(),
								rangeEnd, alignment);
						}
						addr_t offset = alignedBase - next->Base();

						// The new area will be placed at the beginning of the
						// reserved area and the reserved area will be offset
						// and resized
						foundSpot = true;
						next->SetBase(next->Base() + offset + size);
						next->SetSize(next->Size() - offset - size);
						area->SetBase(alignedBase);
						break;
					}

					if (is_valid_spot(next->Base(), alignedBase, size,
							std::min(next->Base() + next->Size() - 1, end))) {
						// The new area will be placed at the end of the
						// reserved area, and the reserved area will be resized
						// to make space

						if (_IsRandomized(addressSpec)) {
							addr_t alignedNextBase = align_address(next->Base(),
								alignment);

							addr_t startRange = next->Base() + next->Size();
							startRange -= size + kMaxRandomize;
							startRange = ROUNDDOWN(startRange, alignment);
							startRange = std::max(startRange, alignedNextBase);

							addr_t rangeEnd
								= std::min(next->Base() + next->Size() - size,
									end);
							alignedBase = _RandomizeAddress(startRange,
								rangeEnd, alignment);
						} else {
							alignedBase = ROUNDDOWN(
								next->Base() + next->Size() - size, alignment);
						}

						foundSpot = true;
						next->SetSize(alignedBase - next->Base());
						area->SetBase(alignedBase);
						break;
					}

					last = next;
				}
			}

			break;
		}

		case B_EXACT_ADDRESS:
			// see if we can create it exactly here
			if ((last == NULL || last->Base() + (last->Size() - 1) < start)
				&& (next == NULL || next->Base() > start + (size - 1))) {
				foundSpot = true;
				area->SetBase(start);
				break;
			}
			break;
		default:
			return B_BAD_VALUE;
	}

	if (!foundSpot)
		return addressSpec == B_EXACT_ADDRESS ? B_BAD_VALUE : B_NO_MEMORY;

	if (useHint)
		fNextInsertHint = area->Base() + size;

	area->SetSize(size);
	fAreas.Insert(area);
	IncrementChangeCount();
	return B_OK;
}
