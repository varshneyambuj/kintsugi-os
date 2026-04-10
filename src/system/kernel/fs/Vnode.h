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

/** @file Vnode.h
 *  @brief In-kernel vnode object — the VFS-side cache entry for an inode. */

#ifndef VNODE_H
#define VNODE_H


#include <fs_interface.h>

#include <util/DoublyLinkedList.h>
#include <util/list.h>

#include <lock.h>
#include <thread.h>


struct advisory_locking;
struct file_descriptor;
struct fs_mount;
struct VMCache;

typedef struct vnode Vnode;


/** @brief Kernel representation of an open inode in the VFS cache.
 *
 * Each vnode maps a (device, inode) pair to its file system, page cache,
 * and any active advisory locks. State bits (busy, removed, unpublished,
 * unused, hot, covered, covering) are packed into @c fFlags and accessed
 * lock-free with atomics. The structural pointers (mount, cache, covered_by,
 * covers) require sVnodeLock to be held while being mutated; reading them
 * is generally lock-free under the assumption that the vnode is referenced. */
struct vnode : fs_vnode, DoublyLinkedListLinkImpl<vnode> {
			struct vnode*		hash_next;             /**< Next vnode in the global hash bucket. */
			VMCache*			cache;                 /**< Page cache backing this vnode's data. */
			struct fs_mount*	mount;                 /**< Mount this vnode belongs to. */
			struct vnode*		covered_by;            /**< Vnode mounted on top of this one, if any. */
			struct vnode*		covers;                /**< Vnode this one is mounted on top of, if any. */
			struct advisory_locking* advisory_locking; /**< Advisory range lock state, lazily allocated. */
			struct file_descriptor* mandatory_locked_by; /**< File descriptor holding a mandatory lock. */
			DoublyLinkedListLink<struct vnode> unused_link; /**< Link in the LRU unused list. */
			ino_t				id;                    /**< Inode number within the file system. */
			dev_t				device;                /**< Device id of the owning file system. */
			int32				ref_count;             /**< Reference count; the vnode is freed at zero. */

public:
	/** @brief Returns true if the vnode is currently marked busy. */
	inline	bool				IsBusy() const;
	/** @brief Sets or clears the busy flag atomically. */
	inline	void				SetBusy(bool busy);

	/** @brief Returns true if the underlying inode has been removed. */
	inline	bool				IsRemoved() const;
	/** @brief Sets or clears the removed flag atomically. */
	inline	void				SetRemoved(bool removed);

	/** @brief Returns true if the vnode has not yet been published to user space. */
	inline	bool				IsUnpublished() const;
	/** @brief Sets or clears the unpublished flag atomically. */
	inline	void				SetUnpublished(bool unpublished);

	/** @brief Returns true if the vnode is on the unused list. */
	inline	bool				IsUnused() const;
	/** @brief Sets or clears the unused flag atomically. */
	inline	void				SetUnused(bool unused);

	/** @brief Returns true if the vnode is marked hot (recently accessed). */
	inline	bool				IsHot() const;
	/** @brief Sets or clears the hot flag atomically. */
	inline	void				SetHot(bool hot);

	/** @brief Returns true if this vnode is covered by another mount. */
	// setter requires sVnodeLock write-locked, getter is lockless
	inline	bool				IsCovered() const;
	/** @brief Marks this vnode as covered by another mount. */
	inline	void				SetCovered(bool covered);

	/** @brief Returns true if this vnode covers another vnode. */
	// setter requires sVnodeLock write-locked, getter is lockless
	inline	bool				IsCovering() const;
	/** @brief Marks this vnode as covering another vnode. */
	inline	void				SetCovering(bool covering);

	/** @brief Returns the cached file type bits. */
	inline	uint32				Type() const;
	/** @brief Stores the file type bits, replacing any previous value. */
	inline	void				SetType(uint32 type);

	/** @brief Acquires the per-vnode lock; always returns true. */
	inline	bool				Lock();
	/** @brief Releases the per-vnode lock. */
	inline	void				Unlock();

	/** @brief Initialises shared static state used by every vnode instance. */
	static	void				StaticInit();

private:
	static	const uint32		kFlagsLocked		= 0x00000001;  /**< Per-vnode lock held bit. */
	static	const uint32		kFlagsWaitingLocker	= 0x00000002;  /**< At least one thread is waiting for the per-vnode lock. */
	static	const uint32		kFlagsBusy			= 0x00000004;  /**< Vnode is undergoing initialisation or removal. */
	static	const uint32		kFlagsRemoved		= 0x00000008;  /**< Underlying inode has been deleted. */
	static	const uint32		kFlagsUnpublished	= 0x00000010;  /**< Vnode is not yet visible to user space. */
	static	const uint32		kFlagsUnused		= 0x00000020;  /**< Vnode lives on the LRU unused list. */
	static	const uint32		kFlagsHot			= 0x00000040;  /**< Recently accessed; resists eviction. */
	static	const uint32		kFlagsCovered		= 0x00000080;  /**< Has another mount covering it. */
	static	const uint32		kFlagsCovering		= 0x00000100;  /**< Is itself a cover for another vnode. */
	static	const uint32		kFlagsType			= 0xfffff000;  /**< Mask of bits storing the file type. */

	static	const uint32		kBucketCount		= 32;          /**< Number of per-vnode lock buckets. */

	/** @brief One queued thread waiting for a vnode's lock. */
			struct LockWaiter : DoublyLinkedListLinkImpl<LockWaiter> {
				Thread*			thread;  /**< Thread that is blocking. */
				struct vnode*	vnode;   /**< Vnode it is waiting on. */
			};

			typedef DoublyLinkedList<LockWaiter> LockWaiterList;

	/** @brief Hash bucket holding a mutex and the waiter list for several vnodes. */
			struct Bucket {
				mutex			lock;     /**< Mutex serialising lock contention in this bucket. */
				LockWaiterList	waiters;  /**< Threads currently waiting for any vnode in this bucket. */

				Bucket();
			};

private:
	inline	Bucket&				_Bucket() const;

			void				_WaitForLock();
			void				_WakeUpLocker();

private:
			int32				fFlags;  /**< Atomic state bits — see the kFlags* constants above. */

	static	Bucket				sBuckets[kBucketCount];  /**< Hash table of buckets keyed by vnode address. */
};


bool
vnode::IsBusy() const
{
	return (fFlags & kFlagsBusy) != 0;
}


void
vnode::SetBusy(bool busy)
{
	if (busy)
		atomic_or(&fFlags, kFlagsBusy);
	else
		atomic_and(&fFlags, ~kFlagsBusy);
}


bool
vnode::IsRemoved() const
{
	return (fFlags & kFlagsRemoved) != 0;
}


void
vnode::SetRemoved(bool removed)
{
	if (removed)
		atomic_or(&fFlags, kFlagsRemoved);
	else
		atomic_and(&fFlags, ~kFlagsRemoved);
}


bool
vnode::IsUnpublished() const
{
	return (fFlags & kFlagsUnpublished) != 0;
}


void
vnode::SetUnpublished(bool unpublished)
{
	if (unpublished)
		atomic_or(&fFlags, kFlagsUnpublished);
	else
		atomic_and(&fFlags, ~kFlagsUnpublished);
}


bool
vnode::IsUnused() const
{
	return (fFlags & kFlagsUnused) != 0;
}


void
vnode::SetUnused(bool unused)
{
	if (unused)
		atomic_or(&fFlags, kFlagsUnused);
	else
		atomic_and(&fFlags, ~kFlagsUnused);
}


bool
vnode::IsHot() const
{
	return (fFlags & kFlagsHot) != 0;
}


void
vnode::SetHot(bool hot)
{
	if (hot)
		atomic_or(&fFlags, kFlagsHot);
	else
		atomic_and(&fFlags, ~kFlagsHot);
}


bool
vnode::IsCovered() const
{
	return (fFlags & kFlagsCovered) != 0;
}


void
vnode::SetCovered(bool covered)
{
	if (covered)
		atomic_or(&fFlags, kFlagsCovered);
	else
		atomic_and(&fFlags, ~kFlagsCovered);
}


bool
vnode::IsCovering() const
{
	return (fFlags & kFlagsCovering) != 0;
}


void
vnode::SetCovering(bool covering)
{
	if (covering)
		atomic_or(&fFlags, kFlagsCovering);
	else
		atomic_and(&fFlags, ~kFlagsCovering);
}


uint32
vnode::Type() const
{
	return (uint32)fFlags & kFlagsType;
}


void
vnode::SetType(uint32 type)
{
	atomic_and(&fFlags, ~kFlagsType);
	atomic_or(&fFlags, type & kFlagsType);
}


/*!	Locks the vnode.
	The caller must hold sVnodeLock (at least read locked) and must continue to
	hold it until calling Unlock(). After acquiring the lock the caller is
	allowed to write access the vnode's mutable fields, if it hasn't been marked
	busy by someone else.
	Due to the condition of holding sVnodeLock at least read locked, write
	locking it grants the same write access permission to *any* vnode.

	The vnode's lock should be held only for a short time. It can be held over
	sUnusedVnodesLock.

	\return Always \c true.
*/
bool
vnode::Lock()
{
	if ((atomic_or(&fFlags, kFlagsLocked)
			& (kFlagsLocked | kFlagsWaitingLocker)) != 0) {
		_WaitForLock();
	}

	return true;
}

void
vnode::Unlock()
{
	if ((atomic_and(&fFlags, ~kFlagsLocked) & kFlagsWaitingLocker) != 0)
		_WakeUpLocker();
}


vnode::Bucket&
vnode::_Bucket() const
{
	return sBuckets[((addr_t)this / 64) % kBucketCount];
		// The vnode structure is somewhat larger than 64 bytes (on 32 bit
		// archs), so subsequently allocated vnodes fall into different
		// buckets. How exactly the vnodes are distributed depends on the
		// allocator -- a dedicated slab would be perfect.
}


#endif	// VNODE_H
