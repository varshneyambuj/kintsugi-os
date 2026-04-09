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
 *   Distributed under the terms of the MIT License.
 */

/** @file RWLockManager.cpp
 *  @brief Implementation of RWLockManager and RWLockable.
 *
 *  RWLockManager is a centralized readers-writer lock coordinator that
 *  manages a set of RWLockable objects.  It supports recursive write
 *  locking, non-recursive read locking, try-lock variants, and
 *  timeout-based variants.  Waiting threads are tracked in per-lockable
 *  waiter queues and unblocked via kernel thread-unblock syscalls.
 */

#include <RWLockManager.h>

#include <AutoLocker.h>

#include <syscalls.h>
#include <user_thread.h>


/** @brief Constructs an RWLockable in its unlocked, unowned state.
 *
 *  fOwner is initialised to -1 (no owner), fOwnerCount and fReaderCount
 *  are both zero, and the waiter list is empty.
 */
RWLockable::RWLockable()
	:
	fOwner(-1),
	fOwnerCount(0),
	fReaderCount(0)
{
}


/** @brief Constructs an RWLockManager with an internal BLocker named
 *         "r/w lock manager".
 */
RWLockManager::RWLockManager()
	:
	fLock("r/w lock manager")
{
}


/** @brief Destructor. */
RWLockManager::~RWLockManager()
{
}


/** @brief Acquires a read lock on @p lockable, blocking indefinitely.
 *
 *  If no waiter is queued the read lock is granted immediately.  Otherwise
 *  the calling thread is added to the waiter queue and blocks until the
 *  lock becomes available.
 *
 *  @param lockable  The RWLockable to read-lock.
 *  @return true if the read lock was acquired, false on error.
 */
bool
RWLockManager::ReadLock(RWLockable* lockable)
{
	AutoLocker<RWLockManager> locker(this);

	if (lockable->fWaiters.IsEmpty()) {
		lockable->fReaderCount++;
		return true;
	}

	return _Wait(lockable, false, B_INFINITE_TIMEOUT) == B_OK;
}


/** @brief Tries to acquire a read lock on @p lockable without blocking.
 *
 *  Succeeds only if there are no waiters currently queued for the lockable.
 *
 *  @param lockable  The RWLockable to attempt to read-lock.
 *  @return true if the lock was acquired immediately, false otherwise.
 */
bool
RWLockManager::TryReadLock(RWLockable* lockable)
{
	AutoLocker<RWLockManager> locker(this);

	if (lockable->fWaiters.IsEmpty()) {
		lockable->fReaderCount++;
		return true;
	}

	return false;
}


/** @brief Tries to acquire a read lock on @p lockable within @p timeout
 *         microseconds.
 *
 *  If no waiter is queued the lock is granted immediately.  Otherwise the
 *  calling thread waits up to @p timeout microseconds.
 *
 *  @param lockable  The RWLockable to read-lock.
 *  @param timeout   Maximum wait time in microseconds (relative).
 *  @return B_OK on success or B_TIMED_OUT if the timeout elapses.
 */
status_t
RWLockManager::ReadLockWithTimeout(RWLockable* lockable, bigtime_t timeout)
{
	AutoLocker<RWLockManager> locker(this);

	if (lockable->fWaiters.IsEmpty()) {
		lockable->fReaderCount++;
		return B_OK;
	}

	return _Wait(lockable, false, timeout);
}


/** @brief Releases a read lock previously acquired on @p lockable.
 *
 *  Decrements the reader count; when it reaches zero _Unblock() is called
 *  to wake up any waiting writer.
 *
 *  @param lockable  The RWLockable to read-unlock.
 */
void
RWLockManager::ReadUnlock(RWLockable* lockable)
{
	AutoLocker<RWLockManager> locker(this);

	if (lockable->fReaderCount <= 0) {
		debugger("RWLockManager::ReadUnlock(): Not read-locked!");
		return;
	}

	if (--lockable->fReaderCount == 0)
		_Unblock(lockable);
}


/** @brief Acquires a write lock on @p lockable, blocking indefinitely.
 *
 *  Write locking is recursive for the owning thread: if the calling thread
 *  already holds the write lock the owner count is incremented.  A fresh
 *  write lock is granted immediately when there are no readers and no
 *  waiters.
 *
 *  @param lockable  The RWLockable to write-lock.
 *  @return true if the write lock was acquired, false on error.
 */
bool
RWLockManager::WriteLock(RWLockable* lockable)
{
	AutoLocker<RWLockManager> locker(this);

	thread_id thread = find_thread(NULL);

	if (lockable->fOwner == thread) {
		lockable->fOwnerCount++;
		return true;
	}

	if (lockable->fReaderCount == 0 && lockable->fWaiters.IsEmpty()) {
		lockable->fOwnerCount = 1;
		lockable->fOwner = find_thread(NULL);
		return true;
	}

	return _Wait(lockable, true, B_INFINITE_TIMEOUT) == B_OK;
}


/** @brief Tries to acquire a write lock on @p lockable without blocking.
 *
 *  Succeeds immediately for the existing owner (recursive) or when no
 *  readers or waiters are present.
 *
 *  @param lockable  The RWLockable to attempt to write-lock.
 *  @return true if the lock was acquired immediately, false otherwise.
 */
bool
RWLockManager::TryWriteLock(RWLockable* lockable)
{
	AutoLocker<RWLockManager> locker(this);

	thread_id thread = find_thread(NULL);

	if (lockable->fOwner == thread) {
		lockable->fOwnerCount++;
		return true;
	}

	if (lockable->fReaderCount == 0 && lockable->fWaiters.IsEmpty()) {
		lockable->fOwnerCount++;
		lockable->fOwner = thread;
		return true;
	}

	return false;
}


/** @brief Tries to acquire a write lock on @p lockable within @p timeout
 *         microseconds.
 *
 *  @param lockable  The RWLockable to write-lock.
 *  @param timeout   Maximum wait time in microseconds (relative).
 *  @return B_OK on success or B_TIMED_OUT if the timeout elapses.
 */
status_t
RWLockManager::WriteLockWithTimeout(RWLockable* lockable, bigtime_t timeout)
{
	AutoLocker<RWLockManager> locker(this);

	thread_id thread = find_thread(NULL);

	if (lockable->fOwner == thread) {
		lockable->fOwnerCount++;
		return B_OK;
	}

	if (lockable->fReaderCount == 0 && lockable->fWaiters.IsEmpty()) {
		lockable->fOwnerCount++;
		lockable->fOwner = thread;
		return B_OK;
	}

	return _Wait(lockable, true, timeout);
}


/** @brief Releases one level of the write lock previously acquired on
 *         @p lockable.
 *
 *  Only the owning thread may call this.  When the owner count reaches
 *  zero the owner is cleared and _Unblock() is called to wake up waiters.
 *
 *  @param lockable  The RWLockable to write-unlock.
 */
void
RWLockManager::WriteUnlock(RWLockable* lockable)
{
	AutoLocker<RWLockManager> locker(this);

	if (find_thread(NULL) != lockable->fOwner) {
		debugger("RWLockManager::WriteUnlock(): Not write-locked by calling "
			"thread!");
		return;
	}

	if (--lockable->fOwnerCount == 0) {
		lockable->fOwner = -1;
		_Unblock(lockable);
	}
}


/** @brief Internal helper that enqueues the calling thread as a waiter and
 *         blocks it until the lock becomes available or the timeout expires.
 *
 *  The manager lock must be held by the caller before entering this method.
 *  The method temporarily drops the manager lock while the thread sleeps
 *  and reacquires it upon waking.
 *
 *  @param lockable  The lockable being waited upon.
 *  @param writer    true if waiting for a write lock, false for a read lock.
 *  @param timeout   Relative timeout in microseconds, or B_INFINITE_TIMEOUT.
 *  @return B_OK if the lock was granted, B_TIMED_OUT on timeout, or another
 *          error code on interruption / failure.
 */
status_t
RWLockManager::_Wait(RWLockable* lockable, bool writer, bigtime_t timeout)
{
	if (timeout == 0)
		return B_TIMED_OUT;

	// enqueue a waiter
	RWLockable::Waiter waiter(writer);
	lockable->fWaiters.Add(&waiter);
	waiter.queued = true;
	get_user_thread()->wait_status = 1;

	// wait
	Unlock();

	status_t error;
	do {
		error = _kern_block_thread(
			timeout >= 0 ? B_RELATIVE_TIMEOUT : 0, timeout);
			// TODO: When interrupted we should adjust the timeout, respectively
			// convert to an absolute timeout in the first place!
	} while (error == B_INTERRUPTED);

	Lock();

	if (!waiter.queued)
		return waiter.status;

	// we're still queued, which means an error (timeout, interrupt)
	// occurred
	lockable->fWaiters.Remove(&waiter);

	_Unblock(lockable);

	return error;
}


/** @brief Wakes up the next eligible waiter(s) after a lock release.
 *
 *  If the head waiter is a writer and no readers currently hold the lock,
 *  only that writer is unblocked.  If the head waiter is a reader, up to
 *  kMaxReaderUnblockCount consecutive reader waiters are batched and
 *  unblocked with a single kernel call to minimise context switches.
 *
 *  @param lockable  The RWLockable whose waiters should be considered for
 *                   unblocking.
 */
void
RWLockManager::_Unblock(RWLockable* lockable)
{
	// Check whether there any waiting threads at all and whether anyone
	// has the write lock
	RWLockable::Waiter* waiter = lockable->fWaiters.Head();
	if (waiter == NULL || lockable->fOwner >= 0)
		return;

	// writer at head of queue?
	if (waiter->writer) {
		if (lockable->fReaderCount == 0) {
			waiter->status = B_OK;
			waiter->queued = false;
			lockable->fWaiters.Remove(waiter);
			lockable->fOwner = waiter->thread;
			lockable->fOwnerCount = 1;

			_kern_unblock_thread(waiter->thread, B_OK);
		}
		return;
	}

	// wake up one or more readers -- we unblock more than one reader at
	// a time to save trips to the kernel
	while (!lockable->fWaiters.IsEmpty()
			&& !lockable->fWaiters.Head()->writer) {
		static const int kMaxReaderUnblockCount = 128;
		thread_id readers[kMaxReaderUnblockCount];
		int readerCount = 0;

		while (readerCount < kMaxReaderUnblockCount
				&& (waiter = lockable->fWaiters.Head()) != NULL
				&& !waiter->writer) {
			waiter->status = B_OK;
			waiter->queued = false;
			lockable->fWaiters.Remove(waiter);

			readers[readerCount++] = waiter->thread;
			lockable->fReaderCount++;
		}

		if (readerCount > 0)
			_kern_unblock_threads(readers, readerCount, B_OK);
	}
}
