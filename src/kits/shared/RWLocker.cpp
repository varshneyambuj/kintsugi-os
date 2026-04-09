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
 *   Copyright 2006, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */

/** @file RWLocker.cpp
 *  @brief Implementation of RWLocker, a readers-writer lock supporting
 *         recursive locking and priority-ordered queueing via benaphores.
 *
 *  RWLocker allows multiple simultaneous readers or a single exclusive
 *  writer.  Both read and write locks may be acquired recursively by the
 *  same thread.  A thread holding a read lock may upgrade to a write lock
 *  under certain conditions.  Internal ordering is maintained by a
 *  queueing benaphore so that writers are not starved indefinitely.
 */

#include "RWLocker.h"

#include <String.h>

/** @brief Per-thread read-lock tracking record.
 *
 *  Stored in the fReadLockInfos list, one entry per reading thread.
 */
// info about a read lock owner
struct RWLocker::ReadLockInfo {
	thread_id	reader; ///< The thread that holds this read lock.
	int32		count;  ///< Recursion depth for this thread's read locks.
};


/** @brief Default constructor. Creates an RWLocker with no name. */
// constructor
RWLocker::RWLocker()
	: fLock(),
	  fMutex(),
	  fQueue(),
	  fReaderCount(0),
	  fWriterCount(0),
	  fReadLockInfos(8),
	  fWriter(B_ERROR),
	  fWriterWriterCount(0),
	  fWriterReaderCount(0)
{
	_Init(NULL);
}

/** @brief Named constructor. Creates an RWLocker whose underlying semaphores
 *         carry a descriptive name for debugging.
 *
 *  @param name  Human-readable name used to label internal semaphores.
 */
// constructor
RWLocker::RWLocker(const char* name)
	: fLock(name),
	  fMutex(),
	  fQueue(),
	  fReaderCount(0),
	  fWriterCount(0),
	  fReadLockInfos(8),
	  fWriter(B_ERROR),
	  fWriterWriterCount(0),
	  fWriterReaderCount(0)
{
	_Init(name);
}

/** @brief Destructor. Deletes internal semaphores and all read-lock info
 *         records.
 */
// destructor
RWLocker::~RWLocker()
{
	fLock.Lock();
	delete_sem(fMutex.semaphore);
	delete_sem(fQueue.semaphore);
	for (int32 i = 0; ReadLockInfo* info = _ReadLockInfoAt(i); i++)
		delete info;
}

/** @brief Acquires a read lock, blocking indefinitely if necessary.
 *
 *  Multiple threads may hold the read lock simultaneously. A thread that
 *  already holds a write lock can also acquire a read lock recursively.
 *
 *  @return true if the lock was acquired, false on error.
 */
// ReadLock
bool
RWLocker::ReadLock()
{
	status_t error = _ReadLock(B_INFINITE_TIMEOUT);
	return (error == B_OK);
}

/** @brief Tries to acquire a read lock within the specified relative timeout.
 *
 *  @param timeout  Maximum wait time in microseconds (relative).
 *  @return B_OK on success, B_TIMED_OUT if the timeout elapses, or another
 *          error code on failure.
 */
// ReadLockWithTimeout
status_t
RWLocker::ReadLockWithTimeout(bigtime_t timeout)
{
	bigtime_t absoluteTimeout = system_time() + timeout;
	// take care of overflow
	if (timeout > 0 && absoluteTimeout < 0)
		absoluteTimeout = B_INFINITE_TIMEOUT;
	return _ReadLock(absoluteTimeout);
}

/** @brief Releases one level of the calling thread's read lock.
 *
 *  If the thread also holds a write lock its write-time reader count is
 *  decremented instead.  When the last reader releases the lock the mutex
 *  benaphore is released so waiting writers may proceed.
 */
// ReadUnlock
void
RWLocker::ReadUnlock()
{
	if (fLock.Lock()) {
		thread_id thread = find_thread(NULL);
		if (thread == fWriter) {
			// We (also) have a write lock.
			if (fWriterReaderCount > 0)
				fWriterReaderCount--;
			// else: error: unmatched ReadUnlock()
		} else {
			int32 index = _IndexOf(thread);
			if (ReadLockInfo* info = _ReadLockInfoAt(index)) {
				fReaderCount--;
				if (--info->count == 0) {
					// The outer read lock bracket for the thread has been
					// reached. Dispose the info.
					_DeleteReadLockInfo(index);
				}
				if (fReaderCount == 0) {
					// The last reader needs to unlock the mutex.
					_ReleaseBenaphore(fMutex);
				}
			}	// else: error: caller has no read lock
		}
		fLock.Unlock();
	}	// else: we are probably going to be destroyed
}

/** @brief Returns whether the calling thread currently holds a read or write
 *         lock.
 *
 *  @return true if the calling thread holds at least a read lock (including
 *          if it holds a write lock, which implicitly subsumes read access).
 */
// IsReadLocked
//
// Returns whether or not the calling thread owns a read lock or even a
// write lock.
bool
RWLocker::IsReadLocked() const
{
	bool result = false;
	if (fLock.Lock()) {
		thread_id thread = find_thread(NULL);
		result = (thread == fWriter || _IndexOf(thread) >= 0);
		fLock.Unlock();
	}
	return result;
}

/** @brief Acquires the write lock, blocking indefinitely if necessary.
 *
 *  Only one thread may hold the write lock at a time. A thread that already
 *  holds the write lock can acquire it again recursively.
 *
 *  @return true if the lock was acquired, false on error.
 */
// WriteLock
bool
RWLocker::WriteLock()
{
	status_t error = _WriteLock(B_INFINITE_TIMEOUT);
	return (error == B_OK);
}

/** @brief Tries to acquire the write lock within the specified relative
 *         timeout.
 *
 *  @param timeout  Maximum wait time in microseconds (relative).
 *  @return B_OK on success, B_TIMED_OUT if the timeout elapses,
 *          B_WOULD_BLOCK if the request cannot be satisfied within the
 *          timeout due to other lock holders, or another error code.
 */
// WriteLockWithTimeout
status_t
RWLocker::WriteLockWithTimeout(bigtime_t timeout)
{
	bigtime_t absoluteTimeout = system_time() + timeout;
	// take care of overflow
	if (timeout > 0 && absoluteTimeout < 0)
		absoluteTimeout = B_INFINITE_TIMEOUT;
	return _WriteLock(absoluteTimeout);
}

/** @brief Releases one level of the calling thread's write lock.
 *
 *  When the outermost write lock bracket is reached, the thread's writer
 *  identity is cleared.  If the thread also holds read locks those are
 *  reinstated as regular reader entries so subsequent readers can proceed.
 *  If no read locks remain, the mutex benaphore is released.
 */
// WriteUnlock
void
RWLocker::WriteUnlock()
{
	if (fLock.Lock()) {
		thread_id thread = find_thread(NULL);
		if (thread == fWriter) {
			fWriterCount--;
			if (--fWriterWriterCount == 0) {
				// The outer write lock bracket for the thread has been
				// reached.
				fWriter = B_ERROR;
				if (fWriterReaderCount > 0) {
					// We still own read locks.
					_NewReadLockInfo(thread, fWriterReaderCount);
					// A reader that expects to be the first reader may wait
					// at the mutex semaphore. We need to wake it up.
					if (fReaderCount > 0)
						_ReleaseBenaphore(fMutex);
					fReaderCount += fWriterReaderCount;
					fWriterReaderCount = 0;
				} else {
					// We don't own any read locks. So we have to release the
					// mutex benaphore.
					_ReleaseBenaphore(fMutex);
				}
			}
		}	// else: error: unmatched WriteUnlock()
		fLock.Unlock();
	}	// else: We're probably going to die.
}

/** @brief Returns whether the calling thread currently holds the write lock.
 *
 *  @return true if the calling thread is the current writer.
 */
// IsWriteLocked
//
// Returns whether or not the calling thread owns a write lock.
bool
RWLocker::IsWriteLocked() const
{
	return (fWriter == find_thread(NULL));
}

/** @brief Initializes the internal mutex and queueing benaphores.
 *
 *  Creates two semaphores named "<name>_RWLocker_mutex" and
 *  "<name>_RWLocker_queue".
 *
 *  @param name  Base name to use for the semaphores.  May be NULL.
 */
// _Init
void
RWLocker::_Init(const char* name)
{
	// init the mutex benaphore
	BString mutexName(name);
	mutexName += "_RWLocker_mutex";
	fMutex.semaphore = create_sem(0, mutexName.String());
	fMutex.counter = 0;
	// init the queueing benaphore
	BString queueName(name);
	queueName += "_RWLocker_queue";
	fQueue.semaphore = create_sem(0, queueName.String());
	fQueue.counter = 0;
}

/** @brief Internal read-lock implementation using an absolute timeout.
 *
 *  If the calling thread already owns a read or write lock it is handled
 *  recursively.  Otherwise the thread acquires the queueing benaphore, then
 *  the mutex benaphore (for the first reader only), and finally releases
 *  the queueing benaphore so the next candidate can proceed.
 *
 *  @param timeout  Absolute timeout in system_time() units.
 *                  Pass B_INFINITE_TIMEOUT to wait indefinitely.
 *  @return B_OK on success, B_TIMED_OUT if the timeout elapses, or another
 *          error code on failure.
 */
// _ReadLock
//
// /timeout/ -- absolute timeout
status_t
RWLocker::_ReadLock(bigtime_t timeout)
{
	status_t error = B_OK;
	thread_id thread = find_thread(NULL);
	bool locked = false;
	if (fLock.Lock()) {
		// Check, if we already own a read (or write) lock. In this case we
		// can skip the usual locking procedure.
		if (thread == fWriter) {
			// We already own a write lock.
			fWriterReaderCount++;
			locked = true;
		} else if (ReadLockInfo* info = _ReadLockInfoAt(_IndexOf(thread))) {
			// We already own a read lock.
			info->count++;
			fReaderCount++;
			locked = true;
		}
		fLock.Unlock();
	} else	// failed to lock the data
		error = B_ERROR;
	// Usual locking, i.e. we do not already own a read or write lock.
	if (error == B_OK && !locked) {
		error = _AcquireBenaphore(fQueue, timeout);
		if (error == B_OK) {
			if (fLock.Lock()) {
				bool firstReader = false;
				if (++fReaderCount == 1) {
					// We are the first reader.
					_NewReadLockInfo(thread);
					firstReader = true;
				} else
					_NewReadLockInfo(thread);
				fLock.Unlock();
				// The first reader needs to lock the mutex.
				if (firstReader) {
					error = _AcquireBenaphore(fMutex, timeout);
					switch (error) {
						case B_OK:
							// fine
							break;
						case B_TIMED_OUT: {
							// clean up
							if (fLock.Lock()) {
								_DeleteReadLockInfo(_IndexOf(thread));
								fReaderCount--;
								fLock.Unlock();
							}
							break;
						}
						default:
							// Probably we are going to be destroyed.
							break;
					}
				}
				// Let the next candidate enter the game.
				_ReleaseBenaphore(fQueue);
			} else {
				// We couldn't lock the data, which can only happen, if
				// we're going to be destroyed.
				error = B_ERROR;
			}
		}
	}
	return error;
}

/** @brief Internal write-lock implementation using an absolute timeout.
 *
 *  Handles all cases: the caller already holds a write lock (recursive),
 *  holds only a read lock (upgrade attempt), or holds neither.  In the
 *  upgrade and fresh-lock cases the function acquires the queueing
 *  benaphore followed by the mutex benaphore.
 *
 *  @param timeout  Absolute timeout in system_time() units.
 *                  Pass B_INFINITE_TIMEOUT to wait indefinitely.
 *  @return B_OK on success, B_TIMED_OUT on timeout, B_WOULD_BLOCK if a
 *          finite-timeout upgrade is not immediately possible, or another
 *          error code on failure.
 */
// _WriteLock
//
// /timeout/ -- absolute timeout
status_t
RWLocker::_WriteLock(bigtime_t timeout)
{
	status_t error = B_ERROR;
	if (fLock.Lock()) {
		bool infiniteTimeout = (timeout == B_INFINITE_TIMEOUT);
		bool locked = false;
		int32 readerCount = 0;
		thread_id thread = find_thread(NULL);
		int32 index = _IndexOf(thread);
		if (ReadLockInfo* info = _ReadLockInfoAt(index)) {
			// We already own a read lock.
			if (fWriterCount > 0) {
				// There are writers before us.
				if (infiniteTimeout) {
					// Timeout is infinite and there are writers before us.
					// Unregister the read locks and lock as usual.
					readerCount = info->count;
					fWriterCount++;
					fReaderCount -= readerCount;
					_DeleteReadLockInfo(index);
					error = B_OK;
				} else {
					// The timeout is finite and there are readers before us:
					// let the write lock request fail.
					error = B_WOULD_BLOCK;
				}
			} else if (info->count == fReaderCount) {
				// No writers before us.
				// We are the only read lock owners. Just move the read lock
				// info data to the special writer fields and then we are done.
				// Note: At this point we may overtake readers that already
				// have acquired the queueing benaphore, but have not yet
				// locked the data. But that doesn't harm.
				fWriter = thread;
				fWriterCount++;
				fWriterWriterCount = 1;
				fWriterReaderCount = info->count;
				fReaderCount -= fWriterReaderCount;
				_DeleteReadLockInfo(index);
				locked = true;
				error = B_OK;
			} else {
				// No writers before us, but other readers.
				// Note, we're quite restrictive here. If there are only
				// readers before us, we could reinstall our readers, if
				// our request times out. Unfortunately it is not easy
				// to ensure, that no writer overtakes us between unlocking
				// the data and acquiring the queuing benaphore.
				if (infiniteTimeout) {
					// Unregister the readers and lock as usual.
					readerCount = info->count;
					fWriterCount++;
					fReaderCount -= readerCount;
					_DeleteReadLockInfo(index);
					error = B_OK;
				} else
					error = B_WOULD_BLOCK;
			}
		} else {
			// We don't own a read lock.
			if (fWriter == thread) {
				// ... but a write lock.
				fWriterCount++;
				fWriterWriterCount++;
				locked = true;
				error = B_OK;
			} else {
				// We own neither read nor write locks.
				// Lock as usual.
				fWriterCount++;
				error = B_OK;
			}
		}
		fLock.Unlock();
		// Usual locking...
		// First step: acquire the queueing benaphore.
		if (!locked && error == B_OK) {
			error = _AcquireBenaphore(fQueue, timeout);
			switch (error) {
				case B_OK:
					break;
				case B_TIMED_OUT: {
					// clean up
					if (fLock.Lock()) {
						fWriterCount--;
						fLock.Unlock();
					}	// else: failed to lock the data: we're probably going
						// to die.
					break;
				}
				default:
					// Probably we're going to die.
					break;
			}
		}
		// Second step: acquire the mutex benaphore.
		if (!locked && error == B_OK) {
			error = _AcquireBenaphore(fMutex, timeout);
			switch (error) {
				case B_OK: {
					// Yeah, we made it. Set the special writer fields.
					fWriter = thread;
					fWriterWriterCount = 1;
					fWriterReaderCount = readerCount;
					break;
				}
				case B_TIMED_OUT: {
					// clean up
					if (fLock.Lock()) {
						fWriterCount--;
						fLock.Unlock();
					}	// else: failed to lock the data: we're probably going
						// to die.
					break;
				}
				default:
					// Probably we're going to die.
					break;
			}
			// Whatever happened, we have to release the queueing benaphore.
			_ReleaseBenaphore(fQueue);
		}
	} else	// failed to lock the data
		error = B_ERROR;
	return error;
}

/** @brief Appends a ReadLockInfo pointer to fReadLockInfos.
 *
 *  @param info  The heap-allocated ReadLockInfo to add.
 *  @return The index at which the info was inserted.
 */
// _AddReadLockInfo
int32
RWLocker::_AddReadLockInfo(ReadLockInfo* info)
{
	int32 index = fReadLockInfos.CountItems();
	fReadLockInfos.AddItem(info, index);
	return index;
}

/** @brief Allocates a new ReadLockInfo for @p thread with initial count
 *         @p count and appends it to the list.
 *
 *  @param thread  The thread acquiring the read lock.
 *  @param count   The initial recursion depth (usually 1).
 *  @return The index of the newly inserted ReadLockInfo.
 */
// _NewReadLockInfo
//
// Create a new read lock info for the supplied thread and add it to the
// list. Returns the index of the info.
int32
RWLocker::_NewReadLockInfo(thread_id thread, int32 count)
{
	ReadLockInfo* info = new ReadLockInfo;
	info->reader = thread;
	info->count = count;
	return _AddReadLockInfo(info);
}

/** @brief Removes and deletes the ReadLockInfo at @p index.
 *
 *  @param index  Zero-based index into fReadLockInfos.
 */
// _DeleteReadLockInfo
void
RWLocker::_DeleteReadLockInfo(int32 index)
{
	if (ReadLockInfo* info = (ReadLockInfo*)fReadLockInfos.RemoveItem(index))
		delete info;
}

/** @brief Returns the ReadLockInfo at @p index without removing it.
 *
 *  @param index  Zero-based index into fReadLockInfos.
 *  @return Pointer to the ReadLockInfo, or NULL if the index is out of range.
 */
// _ReadLockInfoAt
RWLocker::ReadLockInfo*
RWLocker::_ReadLockInfoAt(int32 index) const
{
	return (ReadLockInfo*)fReadLockInfos.ItemAt(index);
}

/** @brief Finds the fReadLockInfos index for the given thread.
 *
 *  @param thread  The thread whose read-lock entry is sought.
 *  @return The zero-based index of the entry, or -1 if not found.
 */
// _IndexOf
int32
RWLocker::_IndexOf(thread_id thread) const
{
	int32 count = fReadLockInfos.CountItems();
	for (int32 i = 0; i < count; i++) {
		if (_ReadLockInfoAt(i)->reader == thread)
			return i;
	}
	return -1;
}

/** @brief Acquires a benaphore-style semaphore with an absolute timeout.
 *
 *  Atomically increments the counter; if the previous value was already
 *  positive, the semaphore must be acquired to avoid a race.
 *
 *  @param benaphore  The Benaphore (counter + semaphore pair) to acquire.
 *  @param timeout    Absolute timeout for the semaphore acquisition.
 *  @return B_OK on success, B_TIMED_OUT if the timeout elapses, or a
 *          system error code.
 */
// _AcquireBenaphore
status_t
RWLocker::_AcquireBenaphore(Benaphore& benaphore, bigtime_t timeout)
{
	status_t error = B_OK;
	if (atomic_add(&benaphore.counter, 1) > 0) {
		error = acquire_sem_etc(benaphore.semaphore, 1, B_ABSOLUTE_TIMEOUT,
								timeout);
	}
	return error;
}

/** @brief Releases a benaphore-style semaphore.
 *
 *  Atomically decrements the counter; if other threads are waiting
 *  (previous counter > 1) the semaphore is released to wake one of them.
 *
 *  @param benaphore  The Benaphore to release.
 */
// _ReleaseBenaphore
void
RWLocker::_ReleaseBenaphore(Benaphore& benaphore)
{
	if (atomic_add(&benaphore.counter, -1) > 1)
		release_sem(benaphore.semaphore);
}
