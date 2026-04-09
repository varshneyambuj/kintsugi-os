/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2005-2009, Haiku, Inc. All Rights Reserved.
 * Copyright 1999, Be Incorporated. All Rights Reserved.
 * This file may be used under the terms of the Be Sample Code License.
 */

/** @file MultiLocker.h
    @brief Multiple-reader / single-writer lock with RAII helpers. */

#ifndef MULTI_LOCKER_H
#define MULTI_LOCKER_H


/*! multiple-reader single-writer locking class

	IMPORTANT:
	 * nested read locks are not supported
	 * a reader becoming the write is not supported
	 * nested write locks are supported
	 * a writer can do read locks, even nested ones
	 * in case of problems, #define DEBUG 1 in the .cpp
*/


#include <OS.h>
#include <locks.h>


/** @brief Controls timing instrumentation; set to 1 to collect lock timing statistics. */
#define MULTI_LOCKER_TIMING	0
#if DEBUG
#	include <assert.h>
#	define MULTI_LOCKER_DEBUG	DEBUG
#endif

#if MULTI_LOCKER_DEBUG
#	define ASSERT_MULTI_LOCKED(x) assert((x).IsWriteLocked() || (x).IsReadLocked())
#	define ASSERT_MULTI_READ_LOCKED(x) assert((x).IsReadLocked())
#	define ASSERT_MULTI_WRITE_LOCKED(x) assert((x).IsWriteLocked())
#else
#	define MULTI_LOCKER_DEBUG	0
#	define ASSERT_MULTI_LOCKED(x) ;
#	define ASSERT_MULTI_READ_LOCKED(x) ;
#	define ASSERT_MULTI_WRITE_LOCKED(x) ;
#endif


/** @brief Multiple-reader single-writer lock. Multiple threads may hold the
           read lock simultaneously; the write lock grants exclusive access. */
class MultiLocker {
public:
								MultiLocker(const char* baseName);
	virtual						~MultiLocker();

			/** @brief Returns B_OK if the lock was successfully initialised.
			    @return B_OK on success, or an error code. */
			status_t			InitCheck();

			// locking for reading or writing
			/** @brief Acquires the read lock; blocks while a writer holds the lock.
			    @return true if the lock was acquired. */
			bool				ReadLock();

			/** @brief Acquires the write lock; blocks until all readers and writers release.
			    @return true if the lock was acquired. */
			bool				WriteLock();

			// unlocking after reading or writing
			/** @brief Releases a previously acquired read lock.
			    @return true on success. */
			bool				ReadUnlock();

			/** @brief Releases a previously acquired write lock.
			    @return true on success. */
			bool				WriteUnlock();

			// does the current thread hold a write lock?
			/** @brief Returns true if the calling thread currently holds the write lock.
			    @return true if write-locked by the current thread. */
			bool				IsWriteLocked() const;

#if MULTI_LOCKER_DEBUG
			// in DEBUG mode returns whether the lock is held
			// in non-debug mode returns true
			/** @brief Returns true if any thread holds the read lock (debug mode only).
			    @return true if read-locked. */
			bool				IsReadLocked() const;
#endif

private:
								MultiLocker();
								MultiLocker(const MultiLocker& other);
			MultiLocker&		operator=(const MultiLocker& other);
									// not implemented

#if !MULTI_LOCKER_DEBUG
			rw_lock				fLock;
#else
			// functions for managing the DEBUG reader array
			void				_RegisterThread();
			void				_UnregisterThread();

			sem_id				fLock;
			int32*				fDebugArray;
			int32				fMaxThreads;
			int32				fWriterNest;
			thread_id			fWriterThread;
#endif	// MULTI_LOCKER_DEBUG

			status_t			fInit;

#if MULTI_LOCKER_TIMING
			uint32 				rl_count;
			bigtime_t 			rl_time;
			uint32 				ru_count;
			bigtime_t	 		ru_time;
			uint32				wl_count;
			bigtime_t			wl_time;
			uint32				wu_count;
			bigtime_t			wu_time;
			uint32				islock_count;
			bigtime_t			islock_time;
#endif
};


/** @brief RAII guard that acquires the write lock on construction and releases it on destruction. */
class AutoWriteLocker {
public:
	/** @brief Acquires the write lock from a pointer.
	    @param lock Pointer to the MultiLocker to lock. */
	AutoWriteLocker(MultiLocker* lock)
		:
		fLock(*lock)
	{
		fLocked = fLock.WriteLock();
	}

	/** @brief Acquires the write lock from a reference.
	    @param lock Reference to the MultiLocker to lock. */
	AutoWriteLocker(MultiLocker& lock)
		:
		fLock(lock)
	{
		fLocked = fLock.WriteLock();
	}

	~AutoWriteLocker()
	{
		if (fLocked)
			fLock.WriteUnlock();
	}

	/** @brief Returns true if the write lock is currently held.
	    @return true if write-locked. */
	bool IsLocked() const
	{
		return fLock.IsWriteLocked();
	}

	/** @brief Releases the write lock early, before the destructor runs. */
	void Unlock()
	{
		if (fLocked) {
			fLock.WriteUnlock();
			fLocked = false;
		}
	}

private:
 	MultiLocker&	fLock;
	bool			fLocked;
};


/** @brief RAII guard that acquires the read lock on construction and releases it on destruction. */
class AutoReadLocker {
public:
	/** @brief Acquires the read lock from a pointer.
	    @param lock Pointer to the MultiLocker to lock. */
	AutoReadLocker(MultiLocker* lock)
		:
		fLock(*lock)
	{
		fLocked = fLock.ReadLock();
	}

	/** @brief Acquires the read lock from a reference.
	    @param lock Reference to the MultiLocker to lock. */
	AutoReadLocker(MultiLocker& lock)
		:
		fLock(lock)
	{
		fLocked = fLock.ReadLock();
	}

	~AutoReadLocker()
	{
		Unlock();
	}

	/** @brief Releases the read lock early, before the destructor runs. */
	void Unlock()
	{
		if (fLocked) {
			fLock.ReadUnlock();
			fLocked = false;
		}
	}

private:
	MultiLocker&	fLock;
	bool			fLocked;
};

#endif	// MULTI_LOCKER_H
