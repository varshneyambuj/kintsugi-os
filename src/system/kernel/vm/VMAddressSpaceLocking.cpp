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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file VMAddressSpaceLocking.cpp
 * @brief RAII helpers and multi-address-space locking utilities.
 *
 * Provides AddressSpaceReadLocker, AddressSpaceWriteLocker, and
 * MultiAddressSpaceLocker — scoped lock guards used throughout the VM
 * subsystem to safely lock one or more VMAddressSpace objects without
 * deadlock.
 *
 * @see VMAddressSpace.cpp
 */


#include "VMAddressSpaceLocking.h"

#include <AutoDeleter.h>

#include <vm/vm.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>
#include <vm/VMCache.h>


//	#pragma mark - AddressSpaceLockerBase


/*static*/ VMAddressSpace*
AddressSpaceLockerBase::GetAddressSpaceByAreaID(area_id id)
{
	VMAddressSpace* addressSpace = NULL;

	VMAreas::ReadLock();

	VMArea* area = VMAreas::LookupLocked(id);
	if (area != NULL) {
		addressSpace = area->address_space;
		addressSpace->Get();
	}

	VMAreas::ReadUnlock();

	return addressSpace;
}


//	#pragma mark - AddressSpaceReadLocker


/**
 * @brief Constructs a read locker and immediately acquires a read lock for
 *        the address space of the given team.
 *
 * @param team  The team whose address space should be locked for reading.
 *
 * @note If the team does not exist the locker is left in an unlocked,
 *       unset state.  Check IsLocked() after construction if needed.
 */
AddressSpaceReadLocker::AddressSpaceReadLocker(team_id team)
	:
	fSpace(NULL),
	fLocked(false)
{
	SetTo(team);
}


/*! Takes over the reference of the address space, if \a getNewReference is
	\c false.
*/
/**
 * @brief Constructs a read locker from an existing VMAddressSpace pointer.
 *
 * @param space             The address space to lock.
 * @param getNewReference   If @c true an additional reference is acquired via
 *                          Get(); if @c false the locker takes ownership of an
 *                          existing reference.
 */
AddressSpaceReadLocker::AddressSpaceReadLocker(VMAddressSpace* space,
		bool getNewReference)
	:
	fSpace(NULL),
	fLocked(false)
{
	SetTo(space, getNewReference);
}


/**
 * @brief Constructs an empty read locker not associated with any address space.
 *
 * Use SetTo() or SetFromArea() to bind it to a space before calling Lock().
 */
AddressSpaceReadLocker::AddressSpaceReadLocker()
	:
	fSpace(NULL),
	fLocked(false)
{
}


/**
 * @brief Destroys the locker, releasing the lock and address-space reference.
 */
AddressSpaceReadLocker::~AddressSpaceReadLocker()
{
	Unset();
}


void
AddressSpaceReadLocker::Unset()
{
	Unlock();
	if (fSpace != NULL)
		fSpace->Put();
	fSpace = NULL;
}


/**
 * @brief Binds the locker to the address space of \a team and read-locks it.
 *
 * The locker must not already be bound to an address space (asserted).
 *
 * @param team  ID of the team whose address space to lock.
 * @retval B_OK           Lock acquired.
 * @retval B_BAD_TEAM_ID  No address space found for the given team ID.
 */
status_t
AddressSpaceReadLocker::SetTo(team_id team)
{
	ASSERT(fSpace == NULL);

	fSpace = VMAddressSpace::Get(team);
	if (fSpace == NULL)
		return B_BAD_TEAM_ID;

	fSpace->ReadLock();
	fLocked = true;
	return B_OK;
}


/*! Takes over the reference of the address space, if \a getNewReference is
	\c false.
*/
/**
 * @brief Binds the locker to an existing VMAddressSpace and read-locks it.
 *
 * @param space             Address space to lock; must not be @c NULL.
 * @param getNewReference   @c true to acquire a new reference, @c false to
 *                          take ownership of the caller's existing reference.
 */
void
AddressSpaceReadLocker::SetTo(VMAddressSpace* space, bool getNewReference)
{
	ASSERT(fSpace == NULL);

	fSpace = space;

	if (getNewReference)
		fSpace->Get();

	fSpace->ReadLock();
	fLocked = true;
}


status_t
AddressSpaceReadLocker::SetFromArea(area_id areaID, VMArea*& area)
{
	ASSERT(fSpace == NULL);

	fSpace = GetAddressSpaceByAreaID(areaID);
	if (fSpace == NULL)
		return B_BAD_TEAM_ID;

	fSpace->ReadLock();

	area = VMAreas::Lookup(areaID);

	if (area == NULL || area->address_space != fSpace) {
		fSpace->ReadUnlock();
		return B_BAD_VALUE;
	}

	fLocked = true;
	return B_OK;
}


bool
AddressSpaceReadLocker::Lock()
{
	if (fLocked)
		return true;
	if (fSpace == NULL)
		return false;

	fSpace->ReadLock();
	fLocked = true;

	return true;
}


void
AddressSpaceReadLocker::Unlock()
{
	if (fLocked) {
		fSpace->ReadUnlock();
		fLocked = false;
	}
}


//	#pragma mark - AddressSpaceWriteLocker


/**
 * @brief Constructs a write locker and immediately acquires a write lock for
 *        the address space of the given team.
 *
 * @param team  The team whose address space should be locked for writing.
 */
AddressSpaceWriteLocker::AddressSpaceWriteLocker(team_id team)
	:
	fSpace(NULL),
	fLocked(false),
	fDegraded(false)
{
	SetTo(team);
}


/**
 * @brief Constructs a write locker from an existing VMAddressSpace pointer.
 *
 * @param space             The address space to write-lock.
 * @param getNewReference   If @c true an additional reference is acquired;
 *                          if @c false ownership of the existing reference
 *                          is transferred to the locker.
 */
AddressSpaceWriteLocker::AddressSpaceWriteLocker(VMAddressSpace* space,
	bool getNewReference)
	:
	fSpace(NULL),
	fLocked(false),
	fDegraded(false)
{
	SetTo(space, getNewReference);
}


/**
 * @brief Constructs an empty write locker not associated with any address space.
 */
AddressSpaceWriteLocker::AddressSpaceWriteLocker()
	:
	fSpace(NULL),
	fLocked(false),
	fDegraded(false)
{
}


/**
 * @brief Destroys the locker, releasing the lock and address-space reference.
 */
AddressSpaceWriteLocker::~AddressSpaceWriteLocker()
{
	Unset();
}


void
AddressSpaceWriteLocker::Unset()
{
	Unlock();
	if (fSpace != NULL)
		fSpace->Put();
	fSpace = NULL;
}


/**
 * @brief Binds the locker to the address space of \a team and write-locks it.
 *
 * @param team  ID of the team whose address space to lock for writing.
 * @retval B_OK           Lock acquired.
 * @retval B_BAD_TEAM_ID  No address space found for the given team ID.
 */
status_t
AddressSpaceWriteLocker::SetTo(team_id team)
{
	ASSERT(fSpace == NULL);

	fSpace = VMAddressSpace::Get(team);
	if (fSpace == NULL)
		return B_BAD_TEAM_ID;

	fSpace->WriteLock();
	fLocked = true;
	return B_OK;
}


/**
 * @brief Binds the locker to an existing VMAddressSpace and write-locks it.
 *
 * @param space             Address space to lock; must not be @c NULL.
 * @param getNewReference   @c true to acquire a new reference, @c false to
 *                          take ownership of the caller's existing reference.
 */
void
AddressSpaceWriteLocker::SetTo(VMAddressSpace* space, bool getNewReference)
{
	ASSERT(fSpace == NULL);

	fSpace = space;

	if (getNewReference)
		fSpace->Get();

	fSpace->WriteLock();
	fLocked = true;
}


status_t
AddressSpaceWriteLocker::SetFromArea(area_id areaID, VMArea*& area)
{
	ASSERT(fSpace == NULL);

	fSpace = GetAddressSpaceByAreaID(areaID);
	if (fSpace == NULL)
		return B_BAD_VALUE;

	fSpace->WriteLock();

	area = VMAreas::Lookup(areaID);

	if (area == NULL || area->address_space != fSpace) {
		fSpace->WriteUnlock();
		return B_BAD_VALUE;
	}

	fLocked = true;
	return B_OK;
}


status_t
AddressSpaceWriteLocker::SetFromArea(team_id team, area_id areaID,
	bool allowKernel, VMArea*& area)
{
	ASSERT(fSpace == NULL);

	VMAreas::ReadLock();

	area = VMAreas::LookupLocked(areaID);
	if (area != NULL
		&& (area->address_space->ID() == team
			|| (allowKernel && team == VMAddressSpace::KernelID()))) {
		fSpace = area->address_space;
		fSpace->Get();
	}

	VMAreas::ReadUnlock();

	if (fSpace == NULL)
		return B_BAD_VALUE;

	// Second try to get the area -- this time with the address space
	// write lock held

	fSpace->WriteLock();

	area = VMAreas::Lookup(areaID);

	if (area == NULL) {
		fSpace->WriteUnlock();
		return B_BAD_VALUE;
	}

	fLocked = true;
	return B_OK;
}


status_t
AddressSpaceWriteLocker::SetFromArea(team_id team, area_id areaID,
	VMArea*& area)
{
	return SetFromArea(team, areaID, false, area);
}


void
AddressSpaceWriteLocker::Unlock()
{
	if (fLocked) {
		if (fDegraded)
			fSpace->ReadUnlock();
		else
			fSpace->WriteUnlock();
		fLocked = false;
		fDegraded = false;
	}
}


/**
 * @brief Atomically downgrades the held write lock to a read lock.
 *
 * Acquires a read lock before releasing the write lock so there is no window
 * during which the address space is completely unlocked.  After this call
 * Unlock() will call ReadUnlock() instead of WriteUnlock().
 *
 * @note Only valid when the locker currently holds a write lock (not already
 *       degraded).
 */
void
AddressSpaceWriteLocker::DegradeToReadLock()
{
	fSpace->ReadLock();
	fSpace->WriteUnlock();
	fDegraded = true;
}


//	#pragma mark - MultiAddressSpaceLocker


/**
 * @brief Constructs an empty MultiAddressSpaceLocker with no address spaces.
 *
 * Address spaces are added via AddTeam() or AddArea() before Lock() is called.
 */
MultiAddressSpaceLocker::MultiAddressSpaceLocker()
	:
	fItems(NULL),
	fCapacity(0),
	fCount(0),
	fLocked(false)
{
}


/**
 * @brief Destroys the locker, unlocking all spaces and releasing all references.
 *
 * Calls Unset() to release locks and references, then frees the item array.
 */
MultiAddressSpaceLocker::~MultiAddressSpaceLocker()
{
	Unset();
	free(fItems);
}


/*static*/ int
MultiAddressSpaceLocker::_CompareItems(const void* _a, const void* _b)
{
	lock_item* a = (lock_item*)_a;
	lock_item* b = (lock_item*)_b;
	return b->space->ID() - a->space->ID();
		// descending order, i.e. kernel address space last
}


bool
MultiAddressSpaceLocker::_ResizeIfNeeded()
{
	if (fCount == fCapacity) {
		lock_item* items = (lock_item*)realloc(fItems,
			(fCapacity + 4) * sizeof(lock_item));
		if (items == NULL)
			return false;

		fCapacity += 4;
		fItems = items;
	}

	return true;
}


int32
MultiAddressSpaceLocker::_IndexOfAddressSpace(VMAddressSpace* space) const
{
	for (int32 i = 0; i < fCount; i++) {
		if (fItems[i].space == space)
			return i;
	}

	return -1;
}


status_t
MultiAddressSpaceLocker::_AddAddressSpace(VMAddressSpace* space,
	bool writeLock, VMAddressSpace** _space)
{
	if (!space)
		return B_BAD_VALUE;

	int32 index = _IndexOfAddressSpace(space);
	if (index < 0) {
		if (!_ResizeIfNeeded()) {
			space->Put();
			return B_NO_MEMORY;
		}

		lock_item& item = fItems[fCount++];
		item.space = space;
		item.write_lock = writeLock;
	} else {

		// one reference is enough
		space->Put();

		fItems[index].write_lock |= writeLock;
	}

	if (_space != NULL)
		*_space = space;

	return B_OK;
}


void
MultiAddressSpaceLocker::Unset()
{
	Unlock();

	for (int32 i = 0; i < fCount; i++)
		fItems[i].space->Put();

	fCount = 0;
}


/**
 * @brief Locks all registered address spaces in a deadlock-safe order.
 *
 * Sorts the address spaces by descending ID (kernel space last) before
 * acquiring locks, ensuring a consistent global lock ordering.  If any
 * individual lock acquisition fails all previously acquired locks are
 * released before returning the error.
 *
 * @retval B_OK     All address spaces locked successfully.
 * @retval other    A lock acquisition failed; no spaces are left locked.
 *
 * @note The locker must not already be locked (asserted internally).
 */
status_t
MultiAddressSpaceLocker::Lock()
{
	ASSERT(!fLocked);

	qsort(fItems, fCount, sizeof(lock_item), &_CompareItems);

	for (int32 i = 0; i < fCount; i++) {
		status_t status;
		if (fItems[i].write_lock)
			status = fItems[i].space->WriteLock();
		else
			status = fItems[i].space->ReadLock();

		if (status < B_OK) {
			while (--i >= 0) {
				if (fItems[i].write_lock)
					fItems[i].space->WriteUnlock();
				else
					fItems[i].space->ReadUnlock();
			}
			return status;
		}
	}

	fLocked = true;
	return B_OK;
}


void
MultiAddressSpaceLocker::Unlock()
{
	if (!fLocked)
		return;

	for (int32 i = 0; i < fCount; i++) {
		if (fItems[i].write_lock)
			fItems[i].space->WriteUnlock();
		else
			fItems[i].space->ReadUnlock();
	}

	fLocked = false;
}


/*!	Adds all address spaces of the areas associated with the given area's cache,
	locks them, and locks the cache (including a reference to it). It retries
	until the situation is stable (i.e. the neither cache nor cache's areas
	changed) or an error occurs.
*/
status_t
MultiAddressSpaceLocker::AddAreaCacheAndLock(area_id areaID,
	bool writeLockThisOne, bool writeLockOthers, VMArea*& _area,
	VMCache** _cache)
{
	// remember the original state
	int originalCount = fCount;
	lock_item* originalItems = NULL;
	if (fCount > 0) {
		originalItems = new(nothrow) lock_item[fCount];
		if (originalItems == NULL)
			return B_NO_MEMORY;
		memcpy(originalItems, fItems, fCount * sizeof(lock_item));
	}
	ArrayDeleter<lock_item> _(originalItems);

	// get the cache
	VMCache* cache;
	VMArea* area;
	status_t error;
	{
		AddressSpaceReadLocker locker;
		error = locker.SetFromArea(areaID, area);
		if (error != B_OK)
			return error;

		cache = vm_area_get_locked_cache(area);
	}

	while (true) {
		// add all areas
		VMArea* firstArea = cache->areas.First();
		for (VMArea* current = firstArea; current;
				current = cache->areas.GetNext(current)) {
			error = AddArea(current,
				current == area ? writeLockThisOne : writeLockOthers);
			if (error != B_OK) {
				vm_area_put_locked_cache(cache);
				return error;
			}
		}

		// unlock the cache and attempt to lock the address spaces
		vm_area_put_locked_cache(cache);

		error = Lock();
		if (error != B_OK)
			return error;

		// lock the cache again and check whether anything has changed

		// check whether the area is gone in the meantime
		area = VMAreas::Lookup(areaID);

		if (area == NULL) {
			Unlock();
			return B_BAD_VALUE;
		}

		// lock the cache
		VMCache* oldCache = cache;
		cache = vm_area_get_locked_cache(area);

		// If neither the area's cache has changed nor its area list we're
		// done.
		if (cache == oldCache && firstArea == cache->areas.First()) {
			_area = area;
			if (_cache != NULL)
				*_cache = cache;
			return B_OK;
		}

		// Restore the original state and try again.

		// Unlock the address spaces, but keep the cache locked for the next
		// iteration.
		Unlock();

		// Get an additional reference to the original address spaces.
		for (int32 i = 0; i < originalCount; i++)
			originalItems[i].space->Get();

		// Release all references to the current address spaces.
		for (int32 i = 0; i < fCount; i++)
			fItems[i].space->Put();

		// Copy over the original state.
		fCount = originalCount;
		if (originalItems != NULL)
			memcpy(fItems, originalItems, fCount * sizeof(lock_item));
	}
}
