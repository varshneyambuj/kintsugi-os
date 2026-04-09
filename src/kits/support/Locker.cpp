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
 *   Copyright 2001-2009 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Erik Jaesler, erik@cgsoftware.com
 */


/**
 * @file Locker.cpp
 * @brief Implementation of BLocker, a recursive mutual-exclusion lock.
 *
 * BLocker supports two locking strategies: a classic semaphore style and a
 * benaphore style. In benaphore mode an atomic counter is tested first so
 * that an uncontended acquire never touches the kernel semaphore, giving
 * better performance under low contention. Setting the compile-time flag
 * BLOCKER_ALWAYS_SEMAPHORE_STYLE forces semaphore mode regardless of the
 * constructor argument.
 *
 * Data member summary:
 * - fBenaphoreCount: in benaphore mode, tracks the number of waiters
 *   (initialised to 0); forced to 1 in semaphore mode so that every
 *   acquire falls through to acquire_sem().
 * - fSemaphoreID: the kernel semaphore used for blocking when the lock
 *   is already held.
 * - fLockOwner: thread_id of the current holder, or B_ERROR when free.
 * - fRecursiveCount: number of times the owning thread has acquired the
 *   lock without a matching Unlock(); the thread must call Unlock() this
 *   many times before the lock is released to other threads.
 */


#include <OS.h>
#include <Locker.h>
#include <SupportDefs.h>

#include <stdio.h>

#include "support_kit_config.h"


/**
 * @brief Construct a benaphore-style BLocker with a default name.
 *
 * Equivalent to BLocker(NULL, true). The underlying semaphore is named
 * "some BLocker".
 */
BLocker::BLocker()
{
	InitLocker(NULL, true);
}


/**
 * @brief Construct a benaphore-style BLocker with a custom name.
 *
 * @param name The name given to the underlying kernel semaphore. If NULL,
 *             "some BLocker" is used.
 */
BLocker::BLocker(const char *name)
{
	InitLocker(name, true);
}


/**
 * @brief Construct a BLocker choosing between benaphore and semaphore style.
 *
 * @param benaphoreStyle Pass \c true for benaphore mode (default Haiku
 *                       behaviour) or \c false to use plain semaphore mode.
 */
BLocker::BLocker(bool benaphoreStyle)
{
	InitLocker(NULL, benaphoreStyle);
}


/**
 * @brief Construct a BLocker with a custom name and explicit locking style.
 *
 * @param name           The name given to the underlying kernel semaphore.
 * @param benaphoreStyle \c true for benaphore mode, \c false for semaphore mode.
 */
BLocker::BLocker(const char *name, bool benaphoreStyle)
{
	InitLocker(name, benaphoreStyle);
}


/*!	This constructor is not documented.  The final argument is ignored for
	now.  In Be's headers, its called "for_IPC".  DO NOT USE THIS
	CONSTRUCTOR!
*/
BLocker::BLocker(const char *name, bool benaphoreStyle,
	bool)
{
	InitLocker(name, benaphoreStyle);
}


/**
 * @brief Destroy the BLocker and release the underlying kernel semaphore.
 *
 * Any threads blocked inside Lock() or LockWithTimeout() will be unblocked
 * with an error once the semaphore is deleted.
 */
BLocker::~BLocker()
{
	delete_sem(fSemaphoreID);
}


/**
 * @brief Return the initialisation status of the BLocker.
 *
 * @return B_OK if the underlying semaphore was created successfully, or a
 *         negative error code (the value of fSemaphoreID) otherwise.
 */
status_t
BLocker::InitCheck() const
{
	return fSemaphoreID >= 0 ? B_OK : fSemaphoreID;
}


/**
 * @brief Acquire the lock, blocking indefinitely if it is already held.
 *
 * If the calling thread already owns the lock the recursive count is
 * incremented and the call returns immediately.
 *
 * @return \c true if the lock was acquired, \c false if an error occurred.
 */
bool
BLocker::Lock()
{
	status_t result;
	return AcquireLock(B_INFINITE_TIMEOUT, &result);
}


/**
 * @brief Attempt to acquire the lock, giving up after \a timeout microseconds.
 *
 * Like Lock(), but returns B_TIMED_OUT if the lock cannot be acquired within
 * the specified duration. Note that a timed-out attempt does not decrement
 * the benaphore count; subsequent lock attempts therefore fall through to
 * semaphore style, accepting a small performance overhead in exchange for
 * safety (see inline comments in AcquireLock() for a detailed discussion).
 *
 * @param timeout Maximum time to wait, in microseconds.
 * @return B_OK on success, B_TIMED_OUT if the timeout elapsed, or another
 *         error code on failure.
 */
status_t
BLocker::LockWithTimeout(bigtime_t timeout)
{
	status_t result;

	AcquireLock(timeout, &result);
	return result;
}


/**
 * @brief Release one level of lock ownership.
 *
 * Decrements the recursive count. When it reaches zero the lock is fully
 * released and, if any thread is waiting, the kernel semaphore is posted.
 *
 * The Be Book explicitly allows any thread, not just the lock owner, to
 * call Unlock(). This is bad practice, but is permitted for compatibility.
 * A warning is printed to stderr when this occurs.
 */
void
BLocker::Unlock()
{
	// The Be Book explicitly allows any thread, not just the lock owner, to
	// unlock. This is bad practice, but we must allow it for compatibility
	// reasons. We can at least warn the developer that something is probably
	// wrong.
	if (!IsLocked()) {
		fprintf(stderr, "Unlocking BLocker with sem %" B_PRId32
			" from wrong thread %" B_PRId32 ", current holder %" B_PRId32
			" (see issue #6400).\n", fSemaphoreID, find_thread(NULL),
			fLockOwner);
	}

	// Decrement the number of outstanding locks this thread holds
	// on this BLocker.
	fRecursiveCount--;

	// If the recursive count is now at 0, that means the BLocker has
	// been released by the thread.
	if (fRecursiveCount == 0) {
		// The BLocker is no longer owned by any thread.
		fLockOwner = B_ERROR;

		// Decrement the benaphore count and store the undecremented
		// value in oldBenaphoreCount.
		int32 oldBenaphoreCount = atomic_add(&fBenaphoreCount, -1);

		// If the oldBenaphoreCount is greater than 1, then there is
		// at least one thread waiting for the lock in the case of a
		// benaphore.
		if (oldBenaphoreCount > 1) {
			// Since there are threads waiting for the lock, it must
			// be released.  Note, the old benaphore count will always be
			// greater than 1 for a semaphore so the release is always done.
			release_sem(fSemaphoreID);
		}
	}
}


/**
 * @brief Return the thread_id of the thread that currently holds the lock.
 *
 * @return The thread_id of the owning thread, or B_ERROR if the lock is free.
 */
thread_id
BLocker::LockingThread() const
{
	return fLockOwner;
}


/**
 * @brief Test whether the calling thread currently holds the lock.
 *
 * @return \c true if find_thread(NULL) == fLockOwner, \c false otherwise.
 */
bool
BLocker::IsLocked() const
{
	// This member returns true if the calling thread holds the lock.
	// The easiest way to determine this is to compare the result of
	// find_thread() to the fLockOwner.
	return find_thread(NULL) == fLockOwner;
}


/**
 * @brief Return the number of times the owning thread has acquired the lock
 *        without a matching Unlock().
 *
 * This equals the number of additional Unlock() calls needed before the lock
 * becomes available to other threads. Returns 0 when the lock is free.
 *
 * @return The current recursive acquisition depth.
 */
int32
BLocker::CountLocks() const
{
	return fRecursiveCount;
}


/**
 * @brief Return the current value of the benaphore counter.
 *
 * In benaphore mode this reflects the number of threads that have atomically
 * incremented the counter (i.e., the holder plus all waiters). In semaphore
 * mode it is always >= 1 because the counter was initialised to 1.
 *
 * @return The current benaphore counter value.
 */
int32
BLocker::CountLockRequests() const
{
	return fBenaphoreCount;
}


/**
 * @brief Return the sem_id of the underlying kernel semaphore.
 *
 * @return The sem_id, which is negative if initialisation failed.
 */
sem_id
BLocker::Sem() const
{
	return fSemaphoreID;
}


/**
 * @brief Initialise all data members and create the kernel semaphore.
 *
 * Called from every constructor. In benaphore mode the semaphore is created
 * with a count of 0 (so the first contending thread blocks immediately) and
 * fBenaphoreCount starts at 0. In semaphore mode the semaphore starts at 1
 * and fBenaphoreCount is set to 1 so that every acquire falls through to
 * acquire_sem().
 *
 * @param name      Name for the underlying semaphore; "some BLocker" is
 *                  substituted when NULL.
 * @param benaphore \c true for benaphore mode, \c false for semaphore mode.
 */
void
BLocker::InitLocker(const char *name, bool benaphore)
{
	if (name == NULL)
		name = "some BLocker";

	if (benaphore && !BLOCKER_ALWAYS_SEMAPHORE_STYLE) {
		// Because this is a benaphore, initialize the benaphore count and
		// create the semaphore.  Because this is a benaphore, the semaphore
		// count starts at 0 (ie acquired).
		fBenaphoreCount = 0;
		fSemaphoreID = create_sem(0, name);
	} else {
		// Because this is a semaphore, initialize the benaphore count to -1
		// and create the semaphore.  Because this is semaphore style, the
		// semaphore count starts at 1 so that one thread can acquire it and
		// the next thread to acquire it will block.
		fBenaphoreCount = 1;
		fSemaphoreID = create_sem(1, name);
	}

	// The lock is currently not acquired so there is no owner.
	fLockOwner = B_ERROR;

	// The lock is currently not acquired so the recursive count is zero.
	fRecursiveCount = 0;
}


/**
 * @brief Internal helper that performs the actual lock acquisition.
 *
 * If the calling thread already owns the lock the recursive count is
 * incremented and the function returns immediately with B_OK. Otherwise the
 * benaphore counter is incremented atomically; if it was already > 0 a
 * kernel semaphore acquire (with the given \a timeout) is performed.
 *
 * On success fLockOwner and fRecursiveCount are updated. On timeout failure
 * the benaphore counter is intentionally NOT decremented — this is a
 * deliberate design choice (mirroring Be's implementation) that avoids a
 * race between the timing-out thread and a concurrent Unlock(); the
 * trade-off is that the lock degrades to semaphore style from that point on
 * (see the lengthy inline comment in the implementation for the full
 * rationale from Trey at Be).
 *
 * @param timeout  Maximum wait in microseconds; pass B_INFINITE_TIMEOUT to
 *                 block forever.
 * @param error    Output parameter filled with the final status code
 *                 (B_OK, B_TIMED_OUT, etc.). Must not be NULL when the caller
 *                 needs the status (Lock() passes a stack variable; the
 *                 parameter is never NULL in practice).
 * @return \c true if the lock was acquired, \c false otherwise.
 */
bool
BLocker::AcquireLock(bigtime_t timeout, status_t *error)
{
	// By default, return no error.
	status_t status = B_OK;

	// Only try to acquire the lock if the thread doesn't already own it.
	if (!IsLocked()) {
		// Increment the benaphore count and test to see if it was already
		// greater than 0. If it is greater than 0, then some thread already has
		// the benaphore or the style is a semaphore. Either way, we need to
		// acquire the semaphore in this case.
		int32 oldBenaphoreCount = atomic_add(&fBenaphoreCount, 1);
		if (oldBenaphoreCount > 0) {
			do {
				status = acquire_sem_etc(fSemaphoreID, 1, B_RELATIVE_TIMEOUT,
					timeout);
			} while (status == B_INTERRUPTED);

			// Note, if the lock here does time out, the benaphore count
			// is not decremented.  By doing this, the benaphore count will
			// never go back to zero.  This means that the locking essentially
			// changes to semaphore style if this was a benaphore.
			//
			// Doing the decrement of the benaphore count when the acquisition
			// fails is a risky thing to do.  If you decrement the counter at
			// the same time the thread which holds the benaphore does an
			// Unlock(), there is serious risk of a race condition.
			//
			// If the Unlock() sees a positive count and releases the semaphore
			// and then the timed out thread decrements the count to 0, there
			// is no one to take the semaphore.  The next two threads will be
			// able to acquire the benaphore at the same time!  The first will
			// increment the counter and acquire the lock.  The second will
			// acquire the semaphore and therefore the lock.  Not good.
			//
			// This has been discussed on the becodetalk mailing list and
			// Trey from Be had this to say:
			//
			// I looked at the LockWithTimeout() code, and it does not have
			// _this_ (ie the race condition) problem.  It circumvents it by
			// NOT doing the atomic_add(&count, -1) if the semaphore
			// acquisition fails.  This means that if a
			// BLocker::LockWithTimeout() times out, all other Lock*() attempts
			// turn into guaranteed semaphore grabs, _with_ the overhead of a
			// (now) useless atomic_add().
			//
			// Given Trey's comments, it looks like Be took the same approach
			// I did.  The output of CountLockRequests() of Be's implementation
			// confirms Trey's comments also.
			//
			// Finally some thoughts for the future with this code:
			//   - If 2^31 timeouts occur on a 32-bit machine (ie today),
			//     the benaphore count will wrap to a negative number.  This
			//     would have unknown consequences on the ability of the BLocker
			//     to continue to function.
			//
		}
	}

	// If the lock has successfully been acquired.
	if (status == B_OK) {
		// Set the lock owner to this thread and increment the recursive count
		// by one.  The recursive count is incremented because one more Unlock()
		// is now required to release the lock (ie, 0 => 1, 1 => 2 etc).
		if (fLockOwner < 0) {
			fLockOwner = find_thread(NULL);
			fRecursiveCount = 1;
		} else
			fRecursiveCount++;
	}

	if (error != NULL)
		*error = status;

	// Return true if the lock has been acquired.
	return (status == B_OK);
}
