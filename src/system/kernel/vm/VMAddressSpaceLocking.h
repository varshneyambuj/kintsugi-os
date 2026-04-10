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
 *   Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file VMAddressSpaceLocking.h
 *  @brief RAII lockers that acquire VMAddressSpace read/write locks safely. */

#ifndef VM_ADDRESS_SPACE_LOCKING_H
#define VM_ADDRESS_SPACE_LOCKING_H


#include <OS.h>

#include <vm/VMAddressSpace.h>


struct VMAddressSpace;
struct VMArea;
struct VMCache;


/** @brief Helper base providing shared lookup utilities for the locker classes. */
class AddressSpaceLockerBase {
public:
	/** @brief Resolves an area id to its owning address space.
	 *  @param id The area id to look up.
	 *  @return The owning VMAddressSpace with an extra reference, or NULL. */
	static	VMAddressSpace*		GetAddressSpaceByAreaID(area_id id);
};


/** @brief Scoped read locker for a VMAddressSpace. */
class AddressSpaceReadLocker : private AddressSpaceLockerBase {
public:
	/** @brief Locks the address space owned by @p team. */
								AddressSpaceReadLocker(team_id team);
	/** @brief Locks @p space, optionally taking a new reference. */
								AddressSpaceReadLocker(VMAddressSpace* space,
									bool getNewReference);
	/** @brief Constructs an unattached locker. */
								AddressSpaceReadLocker();
	/** @brief Releases the lock and any held reference. */
								~AddressSpaceReadLocker();

	/** @brief Attaches to and read-locks the address space of @p team. */
			status_t			SetTo(team_id team);
	/** @brief Attaches to and read-locks @p space. */
			void				SetTo(VMAddressSpace* space,
									bool getNewReference);
	/** @brief Attaches to the address space owning @p areaID and returns the area.
	 *  @param areaID The area whose owning address space should be locked.
	 *  @param area   On success, set to the resolved VMArea pointer.
	 *  @return B_OK on success or an error code on failure. */
			status_t			SetFromArea(area_id areaID, VMArea*& area);

	/** @brief Returns true if a read lock is currently held. */
			bool				IsLocked() const { return fLocked; }
	/** @brief Re-acquires the read lock after Unlock().
	 *  @return True on success, false on failure. */
			bool				Lock();
	/** @brief Releases the read lock without detaching from the space. */
			void				Unlock();

	/** @brief Releases the lock and detaches from the address space. */
			void				Unset();

	/** @brief Returns the address space currently attached to. */
			VMAddressSpace*		AddressSpace() const { return fSpace; }

private:
			VMAddressSpace*		fSpace;   /**< Attached address space, or NULL. */
			bool				fLocked;  /**< True while the read lock is held. */
};


/** @brief Scoped write locker for a VMAddressSpace, with optional degradation to a read lock. */
class AddressSpaceWriteLocker : private AddressSpaceLockerBase {
public:
	/** @brief Write-locks the address space owned by @p team. */
								AddressSpaceWriteLocker(team_id team);
	/** @brief Write-locks @p space, optionally taking a new reference. */
								AddressSpaceWriteLocker(VMAddressSpace* space,
									bool getNewReference);
	/** @brief Constructs an unattached locker. */
								AddressSpaceWriteLocker();
	/** @brief Releases the lock and any held reference. */
								~AddressSpaceWriteLocker();

	/** @brief Attaches to and write-locks the address space of @p team. */
			status_t			SetTo(team_id team);
	/** @brief Attaches to and write-locks @p space. */
			void				SetTo(VMAddressSpace* space,
									bool getNewReference);
	/** @brief Attaches to the address space owning @p areaID and returns the area. */
			status_t			SetFromArea(area_id areaID, VMArea*& area);
	/** @brief Attaches to a team's address space via an area id, with kernel checks.
	 *  @param team        The expected owning team.
	 *  @param areaID      The area id to look up.
	 *  @param allowKernel If false, fail when the area belongs to the kernel.
	 *  @param area        On success, set to the resolved VMArea pointer.
	 *  @return B_OK on success or an error code on failure. */
			status_t			SetFromArea(team_id team, area_id areaID,
									bool allowKernel, VMArea*& area);
	/** @brief Convenience overload that disallows kernel areas. */
			status_t			SetFromArea(team_id team, area_id areaID,
									VMArea*& area);

	/** @brief Returns true if the write lock is currently held. */
			bool				IsLocked() const { return fLocked; }
	/** @brief Releases the write lock without detaching from the space. */
			void				Unlock();

	/** @brief Atomically downgrades the held write lock to a read lock. */
			void				DegradeToReadLock();
	/** @brief Releases the lock and detaches from the address space. */
			void				Unset();

	/** @brief Returns the address space currently attached to. */
			VMAddressSpace*		AddressSpace() const { return fSpace; }

private:
			VMAddressSpace*		fSpace;     /**< Attached address space, or NULL. */
			bool				fLocked;    /**< True while a lock is held. */
			bool				fDegraded;  /**< True if the lock has been downgraded to read. */
};


/** @brief Locks several address spaces at once in a deadlock-free order.
 *
 * Areas and teams are added to the set with AddTeam()/AddArea(); calling
 * Lock() then sorts them and acquires every per-space lock in canonical order
 * to avoid lock-ordering deadlocks. */
class MultiAddressSpaceLocker : private AddressSpaceLockerBase {
public:
	/** @brief Constructs an empty multi-locker. */
								MultiAddressSpaceLocker();
	/** @brief Releases all held locks and detached references. */
								~MultiAddressSpaceLocker();

	/** @brief Adds the address space of @p team to the lock set. */
	inline	status_t			AddTeam(team_id team, bool writeLock,
									VMAddressSpace** _space = NULL);
	/** @brief Adds the address space owning @p area to the lock set. */
	inline	status_t			AddArea(area_id area, bool writeLock,
									VMAddressSpace** _space = NULL);
	/** @brief Adds the address space of an already-resolved VMArea to the lock set. */
	inline	status_t			AddArea(VMArea* area, bool writeLock,
									VMAddressSpace** _space = NULL);

	/** @brief Adds an area's address space, locks it, and returns the area + cache.
	 *
	 * Used by code paths that need both an area's address space lock and its
	 * cache lock to be held simultaneously while still avoiding deadlocks
	 * against other concurrent address-space operations. */
			status_t			AddAreaCacheAndLock(area_id areaID,
									bool writeLockThisOne, bool writeLockOthers,
									VMArea*& _area, VMCache** _cache = NULL);

	/** @brief Locks every address space added to the set in canonical order. */
			status_t			Lock();
	/** @brief Releases every previously acquired lock. */
			void				Unlock();
	/** @brief Returns true if the locks are currently held. */
			bool				IsLocked() const { return fLocked; }

	/** @brief Releases the locks, detaches from every space, and clears the set. */
			void				Unset();

private:
	/** @brief One address-space entry in the locker's working set. */
			struct lock_item {
				VMAddressSpace*	space;       /**< Address space referenced by this entry. */
				bool			write_lock;  /**< True if this entry should be write-locked. */
			};

			bool				_ResizeIfNeeded();
			int32				_IndexOfAddressSpace(VMAddressSpace* space)
									const;
			status_t			_AddAddressSpace(VMAddressSpace* space,
									bool writeLock, VMAddressSpace** _space);

	static	int					_CompareItems(const void* _a, const void* _b);

			lock_item*			fItems;     /**< Address spaces in the working set. */
			int32				fCapacity;  /**< Allocated capacity of @c fItems. */
			int32				fCount;     /**< Number of valid entries in @c fItems. */
			bool				fLocked;    /**< True while every entry's lock is held. */
};


inline status_t
MultiAddressSpaceLocker::AddTeam(team_id team, bool writeLock,
	VMAddressSpace** _space)
{
	return _AddAddressSpace(VMAddressSpace::Get(team), writeLock, _space);
}


inline status_t
MultiAddressSpaceLocker::AddArea(area_id area, bool writeLock,
	VMAddressSpace** _space)
{
	return _AddAddressSpace(GetAddressSpaceByAreaID(area), writeLock, _space);
}


inline status_t
MultiAddressSpaceLocker::AddArea(VMArea* area, bool writeLock,
	VMAddressSpace** _space)
{
	area->address_space->Get();
	return _AddAddressSpace(area->address_space, writeLock, _space);
}


#endif	// VM_ADDRESS_SPACE_LOCKING_H
