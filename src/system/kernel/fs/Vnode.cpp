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

/** @file Vnode.cpp
 * @brief Kernel vnode object — represents an open file system node with reference counting and locking.
 */


#include "Vnode.h"

#include <util/AutoLock.h>


vnode::Bucket vnode::sBuckets[kBucketCount];


/** @brief Constructor for a vnode hash bucket.
 *
 * Initialises the per-bucket mutex that serialises waiter list manipulation
 * for all vnodes that hash into this bucket.
 */
vnode::Bucket::Bucket()
{
	mutex_init(&lock, "vnode bucket");
}


/** @brief One-time static initialisation of all vnode hash buckets.
 *
 * Must be called exactly once during kernel VFS initialisation, before any
 * vnode locking operations are performed.  Placement-constructs each Bucket
 * object in the global @c sBuckets array.
 */
/*static*/ void
vnode::StaticInit()
{
	for (uint32 i = 0; i < kBucketCount; i++)
		new(&sBuckets[i]) Bucket;
}


/** @brief Block the calling thread until the vnode lock becomes available.
 *
 * Creates a LockWaiter on the stack, registers it with the vnode's hash
 * bucket, and calls thread_block() to sleep.  If the lock was released
 * between the atomic flag-set and the bucket lock acquisition the function
 * claims the lock immediately without sleeping.
 *
 * Must be called only from vnode::Lock() when a contention is detected.
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
		// The lock holder dropped it in the meantime and no-one else was faster
		// than us, so it's ours now. Just mark the node locked and clear the
		// waiting flag again.
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


/** @brief Wake the next thread waiting to acquire this vnode's lock.
 *
 * Re-marks the vnode as locked, removes the first matching waiter from the
 * bucket's waiter list, clears the @c kFlagsWaitingLocker flag if that waiter
 * was the only one, and unblocks the waiter's thread.
 *
 * Must be called only from vnode::Unlock() when the waiter flag is set.
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
