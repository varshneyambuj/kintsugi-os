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

/**
 * @file Vnode.cpp
 * @brief In-memory handle for a filesystem inode.
 *
 * A vnode is the kernel VFS's per-node object that bridges a file system
 * implementation and the rest of the kernel. It carries a reference count, a
 * bit-flag word used as a lightweight spin/sleep lock, and links to any
 * mount-point / covering vnode relationships that apply when a file system is
 * mounted over it.
 *
 * The few operations implemented in this translation unit handle the slow
 * path of the flag-based lock: when Lock() loses the race on the kFlagsLocked
 * bit it calls into _WaitForLock() here, and Unlock() calls _WakeUpLocker()
 * when it notices a parked waiter. The per-bucket mutex serialises access to
 * the waiter list that hangs off the vnode's hash bucket; the vnode's own
 * reference-count and SetCovering()/SetMountPoint() invariants are maintained
 * via Lock()/ReleaseRef() in the inline accessors declared in Vnode.h.
 */


#include "Vnode.h"

#include <util/AutoLock.h>


vnode::Bucket vnode::sBuckets[kBucketCount];


/**
 * @brief Construct a vnode hash bucket and initialise its waiter-list mutex.
 *
 * Each bucket owns a single mutex that serialises manipulation of the
 * LockWaiter list shared by every vnode hashing into that bucket. The waiter
 * list is only touched on the contended lock / wake paths, so the mutex is
 * effectively uncontended during normal operation.
 */
vnode::Bucket::Bucket()
{
	mutex_init(&lock, "vnode bucket");
}


/**
 * @brief One-time initialiser for the global vnode hash-bucket array.
 *
 * Placement-constructs each Bucket entry in sBuckets so their mutexes are
 * ready before any vnode attempts to acquire its lock. Must be called exactly
 * once during early VFS initialisation, strictly before any vnode locking
 * operation.
 */
/*static*/ void
vnode::StaticInit()
{
	for (uint32 i = 0; i < kBucketCount; i++)
		new(&sBuckets[i]) Bucket;
}


/**
 * @brief Slow path of vnode::Lock() — park the calling thread until the lock
 *        becomes available.
 *
 * Called only when Lock()'s atomic attempt to set kFlagsLocked failed. Builds
 * a stack-local LockWaiter, takes the bucket mutex, and re-checks the flag
 * word under it: if the previous holder dropped the lock in the meantime this
 * function claims it without ever sleeping. Otherwise the waiter is appended
 * to the bucket's waiter list and the thread blocks on it with
 * THREAD_BLOCK_TYPE_OTHER until _WakeUpLocker() unblocks it.
 *
 * Invariants on return: kFlagsLocked is set and this thread is the owner; the
 * waiter structure is no longer referenced by the bucket list.
 */
void
vnode::_WaitForLock()
{
	LockWaiter waiter;
	waiter.thread = thread_get_current_thread();
	waiter.vnode = this;

	Bucket& bucket = _Bucket();
	MutexLocker bucketLocker(bucket.lock);

	if ((atomic_or(&fFlags, kFlagsWaitingLocker)
			& (kFlagsLocked | kFlagsWaitingLocker)) == 0) {
		// The lock holder dropped it in the meantime and no-one else was
		// faster than us, so it's ours now. Just mark the node locked and
		// clear the waiting flag again.
		atomic_or(&fFlags, kFlagsLocked);
		atomic_and(&fFlags, ~kFlagsWaitingLocker);
		return;
	}

	// prepare for waiting
	bucket.waiters.Add(&waiter);
	thread_prepare_to_block(waiter.thread, 0, THREAD_BLOCK_TYPE_OTHER,
		"vnode lock");

	// start waiting
	bucketLocker.Unlock();
	thread_block();
}


/**
 * @brief Slow path of vnode::Unlock() — hand the lock off to the first parked
 *        waiter.
 *
 * Called only when Unlock() observed kFlagsWaitingLocker set. Re-stamps the
 * vnode as locked on behalf of the waiter (direct hand-off avoids a lost
 * wake-up race), scans this bucket's waiter list for the first LockWaiter
 * whose vnode matches, removes it, clears kFlagsWaitingLocker if it was the
 * only one, and finally wakes the waiter's thread.
 *
 * Invariants on return: the selected waiter is no longer on the bucket list
 * and owns the lock; all other waiters for this vnode remain queued.
 */
void
vnode::_WakeUpLocker()
{
	Bucket& bucket = _Bucket();
	MutexLocker bucketLocker(bucket.lock);

	// mark the node locked again
	atomic_or(&fFlags, kFlagsLocked);

	// get the first waiter from the list
	LockWaiter* waiter = NULL;
	bool onlyWaiter = true;
	for (LockWaiterList::Iterator it = bucket.waiters.GetIterator();
			LockWaiter* someWaiter = it.Next();) {
		if (someWaiter->vnode == this) {
			if (waiter != NULL) {
				onlyWaiter = false;
				break;
			}
			waiter = someWaiter;
			it.Remove();
		}
	}

	ASSERT(waiter != NULL);

	// if that's the only waiter, clear the flag
	if (onlyWaiter)
		atomic_and(&fFlags, ~kFlagsWaitingLocker);

	// and wake it up
	thread_unblock(waiter->thread, B_OK);
}
