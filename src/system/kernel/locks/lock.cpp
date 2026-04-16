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
 *   Copyright 2008-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file lock.cpp
 * @brief Kernel blocking-lock primitives: mutex, recursive_lock, rw_lock.
 *
 * Implements the sleeping lock primitives that sit above the scheduler's
 * spinlocks. Each lock has a small per-lock spinlock that guards its waiter
 * queue; contended acquirers enqueue a stack-allocated waiter struct, prepare
 * to block via thread_prepare_to_block(), release the spinlock, and call
 * thread_block() (or thread_block_with_timeout()). The unlock path dequeues
 * the head waiter and calls thread_unblock() to hand off ownership directly,
 * which avoids thundering-herd wakeups and keeps the lock held across the
 * handoff (so a newly arriving contender cannot starve the woken waiter).
 *
 * Three flavours are provided:
 *  - mutex: exclusive FIFO sleeping lock. In non-KDEBUG builds the fast path
 *    is inline (atomic count) and only contended acquires enter this file.
 *    In KDEBUG builds the holder thread_id is tracked for double-lock /
 *    wrong-unlocker detection.
 *  - recursive_lock: mutex plus a holder thread_id and recursion counter so
 *    the same thread may re-enter.
 *  - rw_lock: writer-biased reader/writer lock. A pending writer is noted in
 *    the count so new readers start waiting and active readers drain; this
 *    prevents reader starvation of writers. To avoid priority inversion on
 *    recursive read-locks held by a thread that later becomes the writer,
 *    a thread already holding the write lock is allowed to take read locks
 *    without blocking (owner_count tracks the nested count).
 *
 * All non-trylock acquire paths require interrupts to be enabled at entry
 * (checked via panic() in KDEBUG builds), because they may block. The unlock
 * paths only briefly disable interrupts while holding the internal spinlock.
 */


#include <debug.h>

#if KDEBUG
#define KDEBUG_STATIC static
static status_t _mutex_lock(struct mutex* lock, void* locker);
static void _mutex_unlock(struct mutex* lock);
#else
#define KDEBUG_STATIC
#define mutex_lock		mutex_lock_inline
#define mutex_unlock	mutex_unlock_inline
#define mutex_trylock	mutex_trylock_inline
#define mutex_lock_with_timeout	mutex_lock_with_timeout_inline
#endif

#include <lock.h>

#include <stdlib.h>
#include <string.h>

#include <interrupts.h>
#include <kernel.h>
#include <listeners.h>
#include <scheduling_analysis.h>
#include <thread.h>
#include <util/AutoLock.h>


struct mutex_waiter {
	Thread*			thread;
	mutex_waiter*	next;		// next in queue
	mutex_waiter*	last;		// last in queue (valid for the first in queue)
};

struct rw_lock_waiter {
	Thread*			thread;
	rw_lock_waiter*	next;		// next in queue
	rw_lock_waiter*	last;		// last in queue (valid for the first in queue)
	bool			writer;
};

#define MUTEX_FLAG_RELEASED		0x2


/**
 * @brief Returns the current recursion depth of a recursive lock for the caller.
 *
 * Only meaningful if the current thread is the holder; any other thread gets
 * -1 because they cannot safely observe another holder's nested count.
 *
 * @param lock Recursive lock to query.
 * @return Recursion depth (>= 1) if the caller holds the lock; -1 otherwise.
 */
int32
recursive_lock_get_recursion(recursive_lock *lock)
{
	if (RECURSIVE_LOCK_HOLDER(lock) == thread_get_current_thread_id())
		return lock->recursion;

	return -1;
}


/**
 * @brief Initializes a recursive lock with no special flags.
 *
 * Convenience wrapper around recursive_lock_init_etc() with flags=0.
 *
 * @param lock Storage to initialize.
 * @param name Human-readable name used for debugger dumps (not copied).
 */
void
recursive_lock_init(recursive_lock *lock, const char *name)
{
	recursive_lock_init_etc(lock, name, 0);
}


/**
 * @brief Initializes a recursive lock with custom flags.
 *
 * Initializes the underlying mutex, resets the holder id (non-KDEBUG only;
 * KDEBUG reuses the mutex holder field) and zeros the recursion counter.
 *
 * @param lock Storage to initialize.
 * @param name Human-readable name, or NULL to use the default "recursive lock".
 * @param flags Flags forwarded to mutex_init_etc() (e.g. MUTEX_FLAG_CLONE_NAME).
 */
void
recursive_lock_init_etc(recursive_lock *lock, const char *name, uint32 flags)
{
	mutex_init_etc(&lock->lock, name != NULL ? name : "recursive lock", flags);
#if !KDEBUG
	lock->holder = -1;
#endif
	lock->recursion = 0;
}


/**
 * @brief Destroys a recursive lock, waking any blocked waiters with B_ERROR.
 *
 * Delegates to mutex_destroy(). Accepts a NULL pointer as a no-op for
 * convenient use in cleanup paths.
 *
 * @param lock Lock to destroy; may be NULL.
 */
void
recursive_lock_destroy(recursive_lock *lock)
{
	if (lock == NULL)
		return;

	mutex_destroy(&lock->lock);
}


/**
 * @brief Acquires a recursive lock, blocking if held by another thread.
 *
 * If the current thread already owns the lock, only the recursion counter is
 * bumped. Otherwise acquires the underlying mutex (may sleep) and records the
 * holder. Must be called with interrupts enabled (checked in KDEBUG builds).
 *
 * @param lock Recursive lock to acquire.
 * @return B_OK on success.
 */
status_t
recursive_lock_lock(recursive_lock *lock)
{
#if KDEBUG
	if (!gKernelStartup && !are_interrupts_enabled()) {
		panic("recursive_lock_lock: called with interrupts disabled for lock "
			"%p (\"%s\")\n", lock, lock->lock.name);
	}
#endif

	thread_id thread = thread_get_current_thread_id();

	if (thread != RECURSIVE_LOCK_HOLDER(lock)) {
		mutex_lock(&lock->lock);
#if !KDEBUG
		lock->holder = thread;
#endif
	}

	lock->recursion++;
	return B_OK;
}


/**
 * @brief Non-blocking attempt to acquire a recursive lock.
 *
 * If the current thread already holds the lock, increments recursion and
 * succeeds; otherwise calls mutex_trylock() on the underlying mutex and
 * only bumps the counter on success.
 *
 * @param lock Recursive lock to attempt to acquire.
 * @return B_OK on acquisition; B_WOULD_BLOCK (or other mutex_trylock error)
 *         if the lock is held by another thread.
 */
status_t
recursive_lock_trylock(recursive_lock *lock)
{
	thread_id thread = thread_get_current_thread_id();

#if KDEBUG
	if (!gKernelStartup && !are_interrupts_enabled()) {
		panic("recursive_lock_lock: called with interrupts disabled for lock "
			"%p (\"%s\")\n", lock, lock->lock.name);
	}
#endif

	if (thread != RECURSIVE_LOCK_HOLDER(lock)) {
		status_t status = mutex_trylock(&lock->lock);
		if (status != B_OK)
			return status;

#if !KDEBUG
		lock->holder = thread;
#endif
	}

	lock->recursion++;
	return B_OK;
}


/**
 * @brief Releases one recursion level of a recursive lock.
 *
 * Panics if called by a thread that is not the current holder. The underlying
 * mutex is only released when recursion drops back to zero.
 *
 * @param lock Recursive lock currently held by the calling thread.
 */
void
recursive_lock_unlock(recursive_lock *lock)
{
	if (thread_get_current_thread_id() != RECURSIVE_LOCK_HOLDER(lock))
		panic("recursive_lock %p unlocked by non-holder thread!\n", lock);

	if (--lock->recursion == 0) {
#if !KDEBUG
		lock->holder = -1;
#endif
		mutex_unlock(&lock->lock);
	}
}


/**
 * @brief Atomically releases one recursive_lock and acquires another.
 *
 * If the outgoing lock is still held recursively after the decrement, only
 * the incoming lock is acquired. If the caller already holds 'to', its
 * recursion is bumped and 'from' is released. Otherwise mutex_switch_lock()
 * hands off between the underlying mutexes without a window where the
 * caller holds neither. Must be called with interrupts enabled.
 *
 * @param from Recursive lock to release (one level).
 * @param to   Recursive lock to acquire.
 * @return B_OK on success; on error, 'from' is re-held at the original depth.
 */
status_t
recursive_lock_switch_lock(recursive_lock* from, recursive_lock* to)
{
#if KDEBUG
	if (!gKernelStartup && !are_interrupts_enabled()) {
		panic("recursive_lock_switch_lock(): called with interrupts "
			"disabled for locks %p, %p", from, to);
	}
#endif

	if (--from->recursion > 0)
		return recursive_lock_lock(to);

#if !KDEBUG
	from->holder = -1;
#endif

	thread_id thread = thread_get_current_thread_id();

	if (thread == RECURSIVE_LOCK_HOLDER(to)) {
		to->recursion++;
		mutex_unlock(&from->lock);
		return B_OK;
	}

	status_t status = mutex_switch_lock(&from->lock, &to->lock);
	if (status != B_OK) {
		from->recursion++;
#if !KDEBUG
		from->holder = thread;
#endif
		return status;
	}

#if !KDEBUG
	to->holder = thread;
#endif
	to->recursion++;
	return B_OK;
}


/**
 * @brief Atomically releases a plain mutex and acquires a recursive lock.
 *
 * If the caller already holds 'to' recursively, its counter is bumped and
 * 'from' is simply released. Otherwise mutex_switch_lock() is used so the
 * caller never gives up all locking before picking up the new one. Must be
 * called with interrupts enabled.
 *
 * @param from Mutex currently held by the caller.
 * @param to   Recursive lock to acquire.
 * @return B_OK on success, error from the underlying mutex switch otherwise.
 */
status_t
recursive_lock_switch_from_mutex(mutex* from, recursive_lock* to)
{
#if KDEBUG
	if (!gKernelStartup && !are_interrupts_enabled()) {
		panic("recursive_lock_switch_from_mutex(): called with interrupts "
			"disabled for locks %p, %p", from, to);
	}
#endif

	thread_id thread = thread_get_current_thread_id();

	if (thread == RECURSIVE_LOCK_HOLDER(to)) {
		to->recursion++;
		mutex_unlock(from);
		return B_OK;
	}

	status_t status = mutex_switch_lock(from, &to->lock);
	if (status != B_OK)
		return status;

#if !KDEBUG
	to->holder = thread;
#endif
	to->recursion++;
	return B_OK;
}


/**
 * @brief Atomically releases a read lock and acquires a recursive lock.
 *
 * When the caller already holds 'to' recursively, the read lock on 'from'
 * is simply released and the recursion counter bumped. Otherwise delegates
 * to mutex_switch_from_read_lock() to hand off directly. Must be called
 * with interrupts enabled.
 *
 * @param from Read-locked rw_lock to release.
 * @param to   Recursive lock to acquire.
 * @return B_OK on success; on error, 'from' read-lock is released and
 *         nothing is held.
 */
status_t
recursive_lock_switch_from_read_lock(rw_lock* from, recursive_lock* to)
{
#if KDEBUG
	if (!gKernelStartup && !are_interrupts_enabled()) {
		panic("recursive_lock_switch_from_read_lock(): called with interrupts "
			"disabled for locks %p, %p", from, to);
	}
#endif

	thread_id thread = thread_get_current_thread_id();

	if (thread != RECURSIVE_LOCK_HOLDER(to)) {
		status_t status = mutex_switch_from_read_lock(from, &to->lock);
		if (status != B_OK)
			return status;

#if !KDEBUG
		to->holder = thread;
#endif
	} else {
		rw_lock_read_unlock(from);
	}

	to->recursion++;
	return B_OK;
}


/**
 * @brief Kernel-debugger command that prints the state of a recursive_lock.
 *
 * Dumps the underlying mutex pointer, name, flags, holder thread id,
 * recursion depth, and the list of waiting thread ids.
 *
 * @param argc Argument count; must be at least 2.
 * @param argv argv[1] must parse to the address of a recursive_lock.
 * @return 0 (debugger commands ignore the return value).
 */
static int
dump_recursive_lock_info(int argc, char** argv)
{
	if (argc < 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	recursive_lock* lock = (recursive_lock*)parse_expression(argv[1]);

	if (!IS_KERNEL_ADDRESS(lock)) {
		kprintf("invalid address: %p\n", lock);
		return 0;
	}

	kprintf("recrusive_lock %p:\n", lock);
	kprintf("  mutex:           %p\n", &lock->lock);
	kprintf("  name:            %s\n", lock->lock.name);
	kprintf("  flags:           0x%x\n", lock->lock.flags);
#if KDEBUG
	kprintf("  holder:          %" B_PRId32 "\n", lock->lock.holder);
#else
	kprintf("  holder:          %" B_PRId32 "\n", lock->holder);
#endif
	kprintf("  recursion:       %d\n", lock->recursion);

	kprintf("  waiting threads:");
	mutex_waiter* waiter = lock->lock.waiters;
	while (waiter != NULL) {
		kprintf(" %" B_PRId32, waiter->thread->id);
		waiter = waiter->next;
	}
	kputs("\n");

	return 0;
}


//	#pragma mark -


/**
 * @brief Enqueues the current thread on an rw_lock waiter list and blocks.
 *
 * Appends a stack-allocated waiter to the FIFO queue, drops the lock's
 * internal spinlock (via @p locker), then sleeps in thread_block(). On
 * successful wakeup the unblocker has already updated the lock state,
 * marked the waiter's thread field as NULL, and transferred ownership.
 * The caller's @p locker is re-acquired before returning.
 *
 * @param lock   rw_lock whose spinlock is held by @p locker.
 * @param writer true to enqueue as a writer, false as a reader.
 * @param locker Interrupts-spinlock guard for lock->lock; unlocked and
 *               re-locked across the block.
 * @return B_OK on acquisition, or the error code returned by thread_block().
 */
static status_t
rw_lock_wait(rw_lock* lock, bool writer, InterruptsSpinLocker& locker)
{
	// enqueue in waiter list
	rw_lock_waiter waiter;
	waiter.thread = thread_get_current_thread();
	waiter.next = NULL;
	waiter.writer = writer;

	if (lock->waiters != NULL)
		lock->waiters->last->next = &waiter;
	else
		lock->waiters = &waiter;

	lock->waiters->last = &waiter;

	// block
	thread_prepare_to_block(waiter.thread, 0, THREAD_BLOCK_TYPE_RW_LOCK, lock);
	locker.Unlock();

	status_t result = thread_block();

	locker.Lock();
	ASSERT(result != B_OK || waiter.thread == NULL);
	return result;
}


/**
 * @brief Hands off an rw_lock to the next eligible waiters.
 *
 * Called with the lock's spinlock held. Implements writer-biased wakeup:
 *  - If the head waiter is a writer, it is granted ownership only after
 *    all active and pending readers drain; the writer is then marked as
 *    holder before being unblocked.
 *  - Otherwise, wakes one or more consecutive reader waiters and bumps
 *    active_readers so that readers hold the lock across the handoff.
 *
 * The caller must update lock->count appropriately before/after this call.
 *
 * @param lock rw_lock being released.
 * @return RW_LOCK_WRITER_COUNT_BASE if a writer was unblocked, the number
 *         of readers unblocked otherwise, or 0 if no one could be woken.
 */
static int32
rw_lock_unblock(rw_lock* lock)
{
	// Check whether there are any waiting threads at all and whether anyone
	// has the write lock.
	rw_lock_waiter* waiter = lock->waiters;
	if (waiter == NULL || lock->holder >= 0)
		return 0;

	// writer at head of queue?
	if (waiter->writer) {
		if (lock->active_readers > 0 || lock->pending_readers > 0)
			return 0;

		// dequeue writer
		lock->waiters = waiter->next;
		if (lock->waiters != NULL)
			lock->waiters->last = waiter->last;

		lock->holder = waiter->thread->id;

		// unblock thread
		thread_unblock(waiter->thread, B_OK);
		waiter->thread = NULL;

		return RW_LOCK_WRITER_COUNT_BASE;
	}

	// wake up one or more readers
	uint32 readerCount = 0;
	do {
		// dequeue reader
		lock->waiters = waiter->next;
		if (lock->waiters != NULL)
			lock->waiters->last = waiter->last;

		readerCount++;

		// unblock thread
		thread_unblock(waiter->thread, B_OK);
		waiter->thread = NULL;
	} while ((waiter = lock->waiters) != NULL && !waiter->writer);

	if (lock->count >= RW_LOCK_WRITER_COUNT_BASE)
		lock->active_readers += readerCount;

	return readerCount;
}


/**
 * @brief Initializes an rw_lock with default flags.
 *
 * Equivalent to rw_lock_init_etc() with flags=0. The name string is stored
 * by pointer (not copied).
 *
 * @param lock Storage to initialize.
 * @param name Stable name string used for debugger dumps and analysis.
 */
void
rw_lock_init(rw_lock* lock, const char* name)
{
	lock->name = name;
	lock->waiters = NULL;
	B_INITIALIZE_SPINLOCK(&lock->lock);
	lock->holder = -1;
	lock->count = 0;
	lock->owner_count = 0;
	lock->active_readers = 0;
	lock->pending_readers = 0;
	lock->flags = 0;

	T_SCHEDULING_ANALYSIS(InitRWLock(lock, name));
	NotifyWaitObjectListeners(&WaitObjectListener::RWLockInitialized, lock);
}


/**
 * @brief Initializes an rw_lock with custom flags.
 *
 * If RW_LOCK_FLAG_CLONE_NAME is set the name is strdup()'d; otherwise the
 * pointer is kept as-is. Also notifies scheduler-analysis tracing and any
 * registered WaitObjectListeners.
 *
 * @param lock  Storage to initialize.
 * @param name  Human-readable name.
 * @param flags Currently only RW_LOCK_FLAG_CLONE_NAME is honoured.
 */
void
rw_lock_init_etc(rw_lock* lock, const char* name, uint32 flags)
{
	lock->name = (flags & RW_LOCK_FLAG_CLONE_NAME) != 0 ? strdup(name) : name;
	lock->waiters = NULL;
	B_INITIALIZE_SPINLOCK(&lock->lock);
	lock->holder = -1;
	lock->count = 0;
	lock->owner_count = 0;
	lock->active_readers = 0;
	lock->pending_readers = 0;
	lock->flags = flags & RW_LOCK_FLAG_CLONE_NAME;

	T_SCHEDULING_ANALYSIS(InitRWLock(lock, name));
	NotifyWaitObjectListeners(&WaitObjectListener::RWLockInitialized, lock);
}


/**
 * @brief Destroys an rw_lock and unblocks all waiters with B_ERROR.
 *
 * In KDEBUG builds, if the lock is still in use and the caller is not the
 * writer, it panics and then attempts to acquire the write lock to drain
 * waiters safely. Frees any cloned name string after releasing the
 * internal spinlock.
 *
 * @param lock rw_lock to destroy.
 */
void
rw_lock_destroy(rw_lock* lock)
{
	char* name = (lock->flags & RW_LOCK_FLAG_CLONE_NAME) != 0
		? (char*)lock->name : NULL;

	// unblock all waiters
	InterruptsSpinLocker locker(lock->lock);

#if KDEBUG
	if ((atomic_get(&lock->count) != 0 || lock->waiters != NULL)
			&& thread_get_current_thread_id() != lock->holder) {
		panic("rw_lock_destroy(): lock is in use and the caller "
			"doesn't hold the write lock (%p)", lock);

		locker.Unlock();
		if (rw_lock_write_lock(lock) != B_OK)
			return;
		locker.Lock();
	}
#endif

	while (rw_lock_waiter* waiter = lock->waiters) {
		// dequeue
		lock->waiters = waiter->next;

		// unblock thread
		thread_unblock(waiter->thread, B_ERROR);
	}

	lock->name = NULL;

	locker.Unlock();

	free(name);
}


#if KDEBUG_RW_LOCK_DEBUG

/**
 * @brief KDEBUG_RW_LOCK_DEBUG: checks whether the caller holds a read lock.
 *
 * Returns true if the caller is the writer (writers may recursively read),
 * otherwise scans the per-thread held_read_locks array for the lock.
 *
 * @param lock rw_lock to check.
 * @return true if the calling thread holds the lock for reading or writing.
 */
bool
_rw_lock_is_read_locked(rw_lock* lock)
{
	if (lock->holder == thread_get_current_thread_id())
		return true;

	Thread* thread = thread_get_current_thread();
	for (size_t i = 0; i < B_COUNT_OF(Thread::held_read_locks); i++) {
		if (thread->held_read_locks[i] == lock)
			return true;
	}
	return false;
}


/**
 * @brief KDEBUG_RW_LOCK_DEBUG: records that the caller now holds a read lock.
 *
 * Inserts the lock pointer into the first free slot of the current thread's
 * held_read_locks array. Panics if that array is full.
 *
 * @param lock rw_lock just acquired for reading.
 */
static void
_rw_lock_set_read_locked(rw_lock* lock)
{
	Thread* thread = thread_get_current_thread();
	for (size_t i = 0; i < B_COUNT_OF(Thread::held_read_locks); i++) {
		if (thread->held_read_locks[i] != NULL)
			continue;

		thread->held_read_locks[i] = lock;
		return;
	}

	panic("too many read locks!");
}


/**
 * @brief KDEBUG_RW_LOCK_DEBUG: clears the read-lock bookkeeping entry.
 *
 * Removes the lock pointer from the caller's held_read_locks array; panics
 * if no matching entry is found (unbalanced unlock).
 *
 * @param lock rw_lock being released.
 */
static void
_rw_lock_unset_read_locked(rw_lock* lock)
{
	Thread* thread = thread_get_current_thread();
	for (size_t i = 0; i < B_COUNT_OF(Thread::held_read_locks); i++) {
		if (thread->held_read_locks[i] != lock)
			continue;

		thread->held_read_locks[i] = NULL;
		return;
	}

	panic("_rw_lock_unset_read_locked(): lock %p not read-locked by current thread", lock);
}

#endif


/**
 * @brief Acquires a read lock on an rw_lock, blocking if needed.
 *
 * Fast path (non-debug): the inline wrapper handles uncontended cases via
 * atomic_add on count; only the contended slow path reaches here. If the
 * caller is the writer, owner_count is bumped (no blocking). If a pending
 * reader slot was left behind by a releasing writer, the caller consumes
 * it without blocking. Otherwise enqueues and sleeps via rw_lock_wait().
 * Must be called with interrupts enabled.
 *
 * @param lock rw_lock to read-lock.
 * @return B_OK on success, error from thread_block() otherwise.
 */
status_t
_rw_lock_read_lock(rw_lock* lock)
{
#if KDEBUG
	if (!gKernelStartup && !are_interrupts_enabled()) {
		panic("_rw_lock_read_lock(): called with interrupts disabled for lock %p",
			lock);
	}
#endif
#if KDEBUG_RW_LOCK_DEBUG
	int32 oldCount = atomic_add(&lock->count, 1);
	if (oldCount < RW_LOCK_WRITER_COUNT_BASE) {
		ASSERT_UNLOCKED_RW_LOCK(lock);
		_rw_lock_set_read_locked(lock);
		return B_OK;
	}
#endif

	InterruptsSpinLocker locker(lock->lock);

	// We might be the writer ourselves.
	if (lock->holder == thread_get_current_thread_id()) {
		lock->owner_count++;
		return B_OK;
	}

	// If we hold a read lock already, but some other thread is waiting
	// for a write lock, then trying to read-lock again will deadlock.
	ASSERT_UNLOCKED_RW_LOCK(lock);

	// The writer that originally had the lock when we called atomic_add() might
	// already have gone and another writer could have overtaken us. In this
	// case the original writer set pending_readers, so we know that we don't
	// have to wait.
	if (lock->pending_readers > 0) {
		lock->pending_readers--;

		if (lock->count >= RW_LOCK_WRITER_COUNT_BASE)
			lock->active_readers++;

#if KDEBUG_RW_LOCK_DEBUG
		_rw_lock_set_read_locked(lock);
#endif
		return B_OK;
	}

	ASSERT(lock->count >= RW_LOCK_WRITER_COUNT_BASE);

	// we need to wait
	status_t status = rw_lock_wait(lock, false, locker);

#if KDEBUG_RW_LOCK_DEBUG
	if (status == B_OK)
		_rw_lock_set_read_locked(lock);
#endif

	return status;
}


/**
 * @brief Read-lock an rw_lock with a timeout.
 *
 * Identical to _rw_lock_read_lock() except it sleeps in
 * thread_block_with_timeout() and, on timeout, carefully dequeues the
 * waiter structure from the waiter list and decrements lock->count.
 * If the unblocker overtook us after the timeout fired, we still return
 * B_OK because the lock is ours. Must be called with interrupts enabled.
 *
 * @param lock         rw_lock to read-lock.
 * @param timeoutFlags Timeout flags (absolute/relative, etc.).
 * @param timeout      Timeout value (interpreted per timeoutFlags).
 * @return B_OK on acquisition; B_TIMED_OUT / B_INTERRUPTED on failure.
 */
status_t
_rw_lock_read_lock_with_timeout(rw_lock* lock, uint32 timeoutFlags,
	bigtime_t timeout)
{
#if KDEBUG
	if (!gKernelStartup && !are_interrupts_enabled()) {
		panic("_rw_lock_read_lock_with_timeout(): called with interrupts "
			"disabled for lock %p", lock);
	}
#endif
#if KDEBUG_RW_LOCK_DEBUG
	int32 oldCount = atomic_add(&lock->count, 1);
	if (oldCount < RW_LOCK_WRITER_COUNT_BASE) {
		ASSERT_UNLOCKED_RW_LOCK(lock);
		_rw_lock_set_read_locked(lock);
		return B_OK;
	}
#endif

	InterruptsSpinLocker locker(lock->lock);

	// We might be the writer ourselves.
	if (lock->holder == thread_get_current_thread_id()) {
		lock->owner_count++;
		return B_OK;
	}

	ASSERT_UNLOCKED_RW_LOCK(lock);

	// The writer that originally had the lock when we called atomic_add() might
	// already have gone and another writer could have overtaken us. In this
	// case the original writer set pending_readers, so we know that we don't
	// have to wait.
	if (lock->pending_readers > 0) {
		lock->pending_readers--;

		if (lock->count >= RW_LOCK_WRITER_COUNT_BASE)
			lock->active_readers++;

#if KDEBUG_RW_LOCK_DEBUG
		_rw_lock_set_read_locked(lock);
#endif
		return B_OK;
	}

	ASSERT(lock->count >= RW_LOCK_WRITER_COUNT_BASE);

	// we need to wait

	// enqueue in waiter list
	rw_lock_waiter waiter;
	waiter.thread = thread_get_current_thread();
	waiter.next = NULL;
	waiter.writer = false;

	if (lock->waiters != NULL)
		lock->waiters->last->next = &waiter;
	else
		lock->waiters = &waiter;

	lock->waiters->last = &waiter;

	// block
	thread_prepare_to_block(waiter.thread, 0, THREAD_BLOCK_TYPE_RW_LOCK, lock);
	locker.Unlock();

	status_t error = thread_block_with_timeout(timeoutFlags, timeout);
	if (error == B_OK || waiter.thread == NULL) {
		// We were unblocked successfully -- potentially our unblocker overtook
		// us after we already failed. In either case, we've got the lock, now.
#if KDEBUG_RW_LOCK_DEBUG
		_rw_lock_set_read_locked(lock);
#endif
		return B_OK;
	}

	locker.Lock();
	// We failed to get the lock -- dequeue from waiter list.
	rw_lock_waiter* previous = NULL;
	rw_lock_waiter* other = lock->waiters;
	while (other != &waiter) {
		previous = other;
		other = other->next;
	}

	if (previous == NULL) {
		// we are the first in line
		lock->waiters = waiter.next;
		if (lock->waiters != NULL)
			lock->waiters->last = waiter.last;
	} else {
		// one or more other waiters are before us in the queue
		previous->next = waiter.next;
		if (lock->waiters->last == &waiter)
			lock->waiters->last = previous;
	}

	// Decrement the count. ATM this is all we have to do. There's at least
	// one writer ahead of us -- otherwise the last writer would have unblocked
	// us (writers only manipulate the lock data with thread spinlock being
	// held) -- so our leaving doesn't make a difference to the ones behind us
	// in the queue.
	atomic_add(&lock->count, -1);

	return error;
}


/**
 * @brief Releases one read-lock on an rw_lock.
 *
 * If the caller is the writer, only owner_count is decremented (nested
 * read lock held by the writer). Otherwise decrements active_readers; when
 * it hits zero, calls rw_lock_unblock() to hand off to any waiting writer.
 * Panics if the lock was not read-locked.
 *
 * @param lock rw_lock to release.
 */
void
_rw_lock_read_unlock(rw_lock* lock)
{
#if KDEBUG_RW_LOCK_DEBUG
	int32 oldCount = atomic_add(&lock->count, -1);
	if (oldCount < RW_LOCK_WRITER_COUNT_BASE) {
		_rw_lock_unset_read_locked(lock);
		return;
	}
#endif

	InterruptsSpinLocker locker(lock->lock);

	// If we're still holding the write lock or if there are other readers,
	// no-one can be woken up.
	if (lock->holder == thread_get_current_thread_id()) {
		ASSERT((lock->owner_count % RW_LOCK_WRITER_COUNT_BASE) > 0);
		lock->owner_count--;
		return;
	}

#if KDEBUG_RW_LOCK_DEBUG
	_rw_lock_unset_read_locked(lock);
#endif

	if (--lock->active_readers > 0)
		return;

	if (lock->active_readers < 0) {
		panic("rw_lock_read_unlock(): lock %p not read-locked", lock);
		lock->active_readers = 0;
		return;
	}

	rw_lock_unblock(lock);
}


/**
 * @brief Acquires the write lock on an rw_lock.
 *
 * If the caller is already the writer, owner_count is bumped by
 * RW_LOCK_WRITER_COUNT_BASE (nested write locks). Otherwise the writer
 * count bit is atomically added to lock->count; if no one else held the
 * lock, ownership is taken immediately. Otherwise active_readers is
 * snapshotted so the unblocker knows how many readers must drain, and the
 * thread sleeps via rw_lock_wait(). Must be called with interrupts enabled.
 *
 * @param lock rw_lock to write-lock.
 * @return B_OK on success, error from thread_block() otherwise.
 */
status_t
rw_lock_write_lock(rw_lock* lock)
{
#if KDEBUG
	if (!gKernelStartup && !are_interrupts_enabled()) {
		panic("_rw_lock_write_lock(): called with interrupts disabled for lock %p",
			lock);
	}
#endif

	InterruptsSpinLocker locker(lock->lock);

	// If we're already the lock holder, we just need to increment the owner
	// count.
	thread_id thread = thread_get_current_thread_id();
	if (lock->holder == thread) {
		lock->owner_count += RW_LOCK_WRITER_COUNT_BASE;
		return B_OK;
	}

	ASSERT_UNLOCKED_RW_LOCK(lock);

	// announce our claim
	int32 oldCount = atomic_add(&lock->count, RW_LOCK_WRITER_COUNT_BASE);

	if (oldCount == 0) {
		// No-one else held a read or write lock, so it's ours now.
		lock->holder = thread;
		lock->owner_count = RW_LOCK_WRITER_COUNT_BASE;
		return B_OK;
	}

	// We have to wait. If we're the first writer, note the current reader
	// count.
	if (oldCount < RW_LOCK_WRITER_COUNT_BASE)
		lock->active_readers = oldCount - lock->pending_readers;

	status_t status = rw_lock_wait(lock, true, locker);
	if (status == B_OK) {
		lock->holder = thread;
		lock->owner_count = RW_LOCK_WRITER_COUNT_BASE;
	}

	return status;
}


/**
 * @brief Releases one level of the write lock.
 *
 * If writes were nested (owner_count >= RW_LOCK_WRITER_COUNT_BASE after the
 * decrement), simply returns. On the final release: the remaining
 * owner_count (which counts nested read locks the writer also held) is
 * salvaged; if another writer is waiting it inherits those readers via
 * active_readers, otherwise pending_readers is set so incoming readers
 * know not to block even though they observed a writer bit before.
 *
 * @param lock rw_lock to release.
 */
void
_rw_lock_write_unlock(rw_lock* lock)
{
	InterruptsSpinLocker locker(lock->lock);

	if (thread_get_current_thread_id() != lock->holder) {
		panic("rw_lock_write_unlock(): lock %p not write-locked by this thread",
			lock);
		return;
	}

	ASSERT(lock->owner_count >= RW_LOCK_WRITER_COUNT_BASE);

	lock->owner_count -= RW_LOCK_WRITER_COUNT_BASE;
	if (lock->owner_count >= RW_LOCK_WRITER_COUNT_BASE)
		return;

	// We gave up our last write lock -- clean up and unblock waiters.
	int32 readerCount = lock->owner_count;
	lock->holder = -1;
	lock->owner_count = 0;

#if KDEBUG_RW_LOCK_DEBUG
	if (readerCount != 0)
		_rw_lock_set_read_locked(lock);
#endif

	int32 oldCount = atomic_add(&lock->count, -RW_LOCK_WRITER_COUNT_BASE);
	oldCount -= RW_LOCK_WRITER_COUNT_BASE;

	if (oldCount != 0) {
		// If writers are waiting, take over our reader count.
		if (oldCount >= RW_LOCK_WRITER_COUNT_BASE) {
			lock->active_readers = readerCount;
			rw_lock_unblock(lock);
		} else {
			// No waiting writer, but there are one or more readers. We will
			// unblock all waiting readers -- that's the easy part -- and must
			// also make sure that all readers that haven't entered the critical
			// section yet, won't start to wait. Otherwise a writer overtaking
			// such a reader will correctly start to wait, but the reader,
			// seeing the writer count > 0, would also start to wait. We set
			// pending_readers to the number of readers that are still expected
			// to enter the critical section.
			lock->pending_readers = oldCount - readerCount
				- rw_lock_unblock(lock);
		}
	}
}


/**
 * @brief Kernel-debugger command that prints the state of an rw_lock.
 *
 * Dumps name, holder, count, active/pending readers, owner_count, flags,
 * the per-thread reader list (KDEBUG_RW_LOCK_DEBUG only) and the queue of
 * waiting threads tagged r/w.
 *
 * @param argc Argument count; must be at least 2.
 * @param argv argv[1] must parse to the address of an rw_lock.
 * @return 0 (debugger commands ignore the return value).
 */
static int
dump_rw_lock_info(int argc, char** argv)
{
	if (argc < 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	rw_lock* lock = (rw_lock*)parse_expression(argv[1]);

	if (!IS_KERNEL_ADDRESS(lock)) {
		kprintf("invalid address: %p\n", lock);
		return 0;
	}

	kprintf("rw lock %p:\n", lock);
	kprintf("  name:            %s\n", lock->name);
	kprintf("  holder:          %" B_PRId32 "\n", lock->holder);
	kprintf("  count:           %#" B_PRIx32 "\n", lock->count);
	kprintf("  active readers   %d\n", lock->active_readers);
	kprintf("  pending readers  %d\n", lock->pending_readers);
	kprintf("  owner count:     %#" B_PRIx32 "\n", lock->owner_count);
	kprintf("  flags:           %#" B_PRIx32 "\n", lock->flags);

#if KDEBUG_RW_LOCK_DEBUG
	kprintf("  reader threads:");
	if (lock->active_readers > 0) {
		ThreadListIterator iterator;
		while (Thread* thread = iterator.Next()) {
			for (size_t i = 0; i < B_COUNT_OF(Thread::held_read_locks); i++) {
				if (thread->held_read_locks[i] == lock) {
					kprintf(" %" B_PRId32, thread->id);
					break;
				}
			}
		}
	}
	kprintf("\n");
#endif

	kprintf("  waiting threads:");
	rw_lock_waiter* waiter = lock->waiters;
	while (waiter != NULL) {
		kprintf(" %" B_PRId32 "/%c", waiter->thread->id, waiter->writer ? 'w' : 'r');
		waiter = waiter->next;
	}
	kputs("\n");

	return 0;
}


// #pragma mark -


/**
 * @brief Initializes a mutex with default flags.
 *
 * Convenience wrapper that forwards to mutex_init_etc() with flags=0.
 *
 * @param lock Storage to initialize.
 * @param name Human-readable name used for debugger dumps.
 */
void
mutex_init(mutex* lock, const char *name)
{
	mutex_init_etc(lock, name, 0);
}


/**
 * @brief Initializes a mutex with custom flags.
 *
 * In KDEBUG builds holder is set to -1; otherwise count is zeroed. The
 * internal spinlock is initialized and the lock is reported to the
 * scheduling-analysis subsystem and any wait-object listeners.
 *
 * @param lock  Storage to initialize.
 * @param name  Human-readable name (strdup()'d if MUTEX_FLAG_CLONE_NAME).
 * @param flags Currently only MUTEX_FLAG_CLONE_NAME is honoured.
 */
void
mutex_init_etc(mutex* lock, const char *name, uint32 flags)
{
	lock->name = (flags & MUTEX_FLAG_CLONE_NAME) != 0 ? strdup(name) : name;
	lock->waiters = NULL;
	B_INITIALIZE_SPINLOCK(&lock->lock);
#if KDEBUG
	lock->holder = -1;
#else
	lock->count = 0;
#endif
	lock->flags = flags & MUTEX_FLAG_CLONE_NAME;

	T_SCHEDULING_ANALYSIS(InitMutex(lock, name));
	NotifyWaitObjectListeners(&WaitObjectListener::MutexInitialized, lock);
}


/**
 * @brief Destroys a mutex, unblocking all waiters with B_ERROR.
 *
 * In KDEBUG builds, if the lock is still held by another thread the
 * function panics and then acquires the lock itself to drain waiters
 * safely. Sets holder/count to a poison value so subsequent use is
 * detected. The name string is freed if it was cloned.
 *
 * @param lock Mutex to destroy.
 */
void
mutex_destroy(mutex* lock)
{
	char* name = (lock->flags & MUTEX_FLAG_CLONE_NAME) != 0
		? (char*)lock->name : NULL;

	// unblock all waiters
	InterruptsSpinLocker locker(lock->lock);

#if KDEBUG
	if (lock->holder != -1 && thread_get_current_thread_id() != lock->holder) {
		panic("mutex_destroy(): the lock (%p) is held by %" B_PRId32 ", not "
			"by the caller @! bt %" B_PRId32, lock, lock->holder, lock->holder);
		if (_mutex_lock(lock, &locker) != B_OK)
			return;
		locker.Lock();
	}
#endif

	while (mutex_waiter* waiter = lock->waiters) {
		// dequeue
		lock->waiters = waiter->next;

		// unblock thread
		Thread* thread = waiter->thread;
		waiter->thread = NULL;
		thread_unblock(thread, B_ERROR);
	}

	lock->name = NULL;
	lock->flags = 0;
#if KDEBUG
	lock->holder = 0;
#else
	lock->count = INT16_MIN;
#endif

	locker.Unlock();

	free(name);
}


/**
 * @brief Slow-path helper that acquires a mutex with its spinlock held.
 *
 * The caller has already taken the lock's internal spinlock; this routine
 * tests whether contention exists (non-KDEBUG: atomic count; KDEBUG: via
 * _mutex_lock()) and either returns B_OK immediately or drops into the
 * sleeping slow path.
 *
 * @param lock   Mutex whose spinlock is held.
 * @param locker Spinlock guard; may be temporarily released by _mutex_lock().
 * @return B_OK on acquisition, error from thread_block() otherwise.
 */
static inline status_t
mutex_lock_threads_locked(mutex* lock, InterruptsSpinLocker* locker)
{
#if KDEBUG
	return _mutex_lock(lock, locker);
#else
	if (atomic_add(&lock->count, -1) < 0)
		return _mutex_lock(lock, locker);
	return B_OK;
#endif
}


/**
 * @brief Atomically releases one mutex and acquires another.
 *
 * Takes the 'to' mutex's internal spinlock before releasing 'from', so
 * there is no window in which a newly arriving contender on 'to' can
 * overtake the switching thread. Must be called with interrupts enabled.
 *
 * @param from Mutex currently held by the caller.
 * @param to   Mutex to acquire.
 * @return B_OK on success, error from the sleeping acquire path otherwise.
 */
status_t
mutex_switch_lock(mutex* from, mutex* to)
{
#if KDEBUG
	if (!gKernelStartup && !are_interrupts_enabled()) {
		panic("mutex_switch_lock(): called with interrupts disabled "
			"for locks %p, %p", from, to);
	}
#endif

	InterruptsSpinLocker locker(to->lock);

	mutex_unlock(from);

	return mutex_lock_threads_locked(to, &locker);
}


/**
 * @brief Transfers ownership of a held mutex to another thread (KDEBUG only).
 *
 * Updates the holder id so that KDEBUG wrong-unlocker checks accept the
 * designated thread. Panics if the caller is not the current holder. A
 * no-op in non-KDEBUG builds (where holder tracking is absent).
 *
 * @param lock   Mutex held by the caller.
 * @param thread New holder thread id.
 */
void
mutex_transfer_lock(mutex* lock, thread_id thread)
{
#if KDEBUG
	if (thread_get_current_thread_id() != lock->holder)
		panic("mutex_transfer_lock(): current thread is not the lock holder!");
	lock->holder = thread;
#endif
}


/**
 * @brief Atomically releases a read lock and acquires a mutex.
 *
 * Takes the mutex's internal spinlock, releases the read lock, and
 * completes the mutex acquire via mutex_lock_threads_locked(). ASSERTs
 * the caller is not the writer of @p from. Must be called with interrupts
 * enabled.
 *
 * @param from Read-locked rw_lock to release.
 * @param to   Mutex to acquire.
 * @return B_OK on success, error from the acquire path otherwise.
 */
status_t
mutex_switch_from_read_lock(rw_lock* from, mutex* to)
{
#if KDEBUG
	if (!gKernelStartup && !are_interrupts_enabled()) {
		panic("mutex_switch_from_read_lock(): called with interrupts disabled "
			"for locks %p, %p", from, to);
	}
#endif
	ASSERT(from->holder != thread_get_current_thread_id());

	InterruptsSpinLocker locker(to->lock);

	rw_lock_read_unlock(from);

	return mutex_lock_threads_locked(to, &locker);
}


/**
 * @brief Sleeping slow-path acquire for a mutex.
 *
 * If @p _locker is non-NULL, the caller already holds the lock's internal
 * spinlock; otherwise one is taken here. In KDEBUG builds the holder is
 * updated directly if the lock is free, and double-locks / uninitialized
 * locks are detected. Non-KDEBUG handles the race where the previous
 * holder released the lock between the atomic decrement and this call via
 * MUTEX_FLAG_RELEASED. Enqueues a stack waiter, releases the spinlock,
 * blocks, and on success finds itself as the holder (set by the unlocker).
 *
 * @param lock    Mutex to acquire.
 * @param _locker InterruptsSpinLocker* already guarding lock->lock, or NULL.
 * @return B_OK on acquisition, error from thread_block() otherwise.
 */
KDEBUG_STATIC status_t
_mutex_lock(mutex* lock, void* _locker)
{
#if KDEBUG
	if (!gKernelStartup && _locker == NULL && !are_interrupts_enabled()) {
		panic("_mutex_lock(): called with interrupts disabled for lock %p",
			lock);
	}
#endif

	// lock only, if !lockLocked
	InterruptsSpinLocker* locker
		= reinterpret_cast<InterruptsSpinLocker*>(_locker);

	InterruptsSpinLocker lockLocker;
	if (locker == NULL) {
		lockLocker.SetTo(lock->lock, false);
		locker = &lockLocker;
	}

	// Might have been released after we decremented the count, but before
	// we acquired the spinlock.
#if KDEBUG
	if (lock->holder < 0) {
		lock->holder = thread_get_current_thread_id();
		return B_OK;
	} else if (lock->holder == thread_get_current_thread_id()) {
		panic("_mutex_lock(): double lock of %p by thread %" B_PRId32, lock,
			lock->holder);
	} else if (lock->holder == 0) {
		panic("_mutex_lock(): using uninitialized lock %p", lock);
	}
#else
	if ((lock->flags & MUTEX_FLAG_RELEASED) != 0) {
		lock->flags &= ~MUTEX_FLAG_RELEASED;
		return B_OK;
	}
#endif

	// enqueue in waiter list
	mutex_waiter waiter;
	waiter.thread = thread_get_current_thread();
	waiter.next = NULL;

	if (lock->waiters != NULL) {
		lock->waiters->last->next = &waiter;
	} else
		lock->waiters = &waiter;

	lock->waiters->last = &waiter;

	// block
	thread_prepare_to_block(waiter.thread, 0, THREAD_BLOCK_TYPE_MUTEX, lock);
	locker->Unlock();

	status_t error = thread_block();
#if KDEBUG
	if (error == B_OK) {
		ASSERT(lock->holder == waiter.thread->id);
	} else {
		// This should only happen when the mutex was destroyed.
		ASSERT(waiter.thread == NULL);
	}
#endif
	return error;
}


/**
 * @brief Sleeping slow-path release for a mutex.
 *
 * Called only when there is a waiter (non-KDEBUG fast path sets
 * MUTEX_FLAG_RELEASED inline). Dequeues the head waiter, sets the lock
 * holder to that thread (avoiding a -1 window that would race with
 * incoming lockers), and unblocks it. If no waiters remain, either clears
 * the holder (KDEBUG) or sets MUTEX_FLAG_RELEASED (non-KDEBUG).
 *
 * @param lock Mutex being released; must be held by the caller.
 */
KDEBUG_STATIC void
_mutex_unlock(mutex* lock)
{
	InterruptsSpinLocker locker(lock->lock);

#if KDEBUG
	if (thread_get_current_thread_id() != lock->holder) {
		panic("_mutex_unlock() failure: thread %" B_PRId32 " is trying to "
			"release mutex %p (current holder %" B_PRId32 ")\n",
			thread_get_current_thread_id(), lock, lock->holder);
		return;
	}
#endif

	mutex_waiter* waiter = lock->waiters;
	if (waiter != NULL) {
		// dequeue the first waiter
		lock->waiters = waiter->next;
		if (lock->waiters != NULL)
			lock->waiters->last = waiter->last;

#if KDEBUG
		// Already set the holder to the unblocked thread. Besides that this
		// actually reflects the current situation, setting it to -1 would
		// cause a race condition, since another locker could think the lock
		// is not held by anyone.
		lock->holder = waiter->thread->id;
#endif

		// unblock thread
		thread_unblock(waiter->thread, B_OK);
	} else {
		// There are no waiters, so mark the lock as released.
#if KDEBUG
		lock->holder = -1;
#else
		lock->flags |= MUTEX_FLAG_RELEASED;
#endif
	}
}


/**
 * @brief Sleeping acquire for a mutex with a timeout.
 *
 * Like _mutex_lock() but uses thread_block_with_timeout(). On timeout this
 * routine carefully dequeues its waiter structure (if still present) and,
 * in non-KDEBUG builds, re-increments the lock count that the inline fast
 * path had decremented. If the unblocker removed the waiter before the
 * timeout took effect, the caller owns the lock and B_OK is returned. Must
 * be called with interrupts enabled.
 *
 * @param lock         Mutex to acquire.
 * @param timeoutFlags Timeout flags (absolute/relative, etc.).
 * @param timeout      Timeout value (interpreted per timeoutFlags).
 * @return B_OK on acquisition; B_TIMED_OUT / B_INTERRUPTED / B_ERROR on fail.
 */
KDEBUG_STATIC status_t
_mutex_lock_with_timeout(mutex* lock, uint32 timeoutFlags, bigtime_t timeout)
{
#if KDEBUG
	if (!gKernelStartup && !are_interrupts_enabled()) {
		panic("_mutex_lock(): called with interrupts disabled for lock %p",
			lock);
	}
#endif

	InterruptsSpinLocker locker(lock->lock);

	// Might have been released after we decremented the count, but before
	// we acquired the spinlock.
#if KDEBUG
	if (lock->holder < 0) {
		lock->holder = thread_get_current_thread_id();
		return B_OK;
	} else if (lock->holder == thread_get_current_thread_id()) {
		panic("_mutex_lock(): double lock of %p by thread %" B_PRId32, lock,
			lock->holder);
	} else if (lock->holder == 0) {
		panic("_mutex_lock(): using uninitialized lock %p", lock);
	}
#else
	if ((lock->flags & MUTEX_FLAG_RELEASED) != 0) {
		lock->flags &= ~MUTEX_FLAG_RELEASED;
		return B_OK;
	}
#endif

	// enqueue in waiter list
	mutex_waiter waiter;
	waiter.thread = thread_get_current_thread();
	waiter.next = NULL;

	if (lock->waiters != NULL) {
		lock->waiters->last->next = &waiter;
	} else
		lock->waiters = &waiter;

	lock->waiters->last = &waiter;

	// block
	thread_prepare_to_block(waiter.thread, 0, THREAD_BLOCK_TYPE_MUTEX, lock);
	locker.Unlock();

	status_t error = thread_block_with_timeout(timeoutFlags, timeout);

	if (error == B_OK) {
#if KDEBUG
		ASSERT(lock->holder == waiter.thread->id);
#endif
	} else {
		// If the lock was destroyed, our "thread" entry will be NULL.
		if (waiter.thread == NULL)
			return B_ERROR;

		// TODO: There is still a race condition during mutex destruction,
		// if we resume due to a timeout before our thread is set to NULL.

		locker.Lock();

		// If the timeout occurred, we must remove our waiter structure from
		// the queue.
		mutex_waiter* previousWaiter = NULL;
		mutex_waiter* otherWaiter = lock->waiters;
		while (otherWaiter != NULL && otherWaiter != &waiter) {
			previousWaiter = otherWaiter;
			otherWaiter = otherWaiter->next;
		}
		if (otherWaiter == &waiter) {
			// the structure is still in the list -- dequeue
			if (&waiter == lock->waiters) {
				if (waiter.next != NULL)
					waiter.next->last = waiter.last;
				lock->waiters = waiter.next;
			} else {
				if (waiter.next == NULL)
					lock->waiters->last = previousWaiter;
				previousWaiter->next = waiter.next;
			}

#if !KDEBUG
			// we need to fix the lock count
			atomic_add(&lock->count, 1);
#endif
		} else {
			// the structure is not in the list -- even though the timeout
			// occurred, this means we own the lock now
#if KDEBUG
			ASSERT(lock->holder == waiter.thread->id);
#endif
			return B_OK;
		}
	}

	return error;
}


/**
 * @brief Non-blocking attempt to acquire a mutex.
 *
 * In KDEBUG builds takes the internal spinlock, tests the holder field,
 * sets it to the current thread on success and otherwise returns
 * B_WOULD_BLOCK; panics on an uninitialized lock. In non-KDEBUG builds
 * forwards to the inline fast path.
 *
 * @param lock Mutex to try to acquire.
 * @return B_OK on acquisition; B_WOULD_BLOCK if held by another thread.
 */
#undef mutex_trylock
status_t
mutex_trylock(mutex* lock)
{
#if KDEBUG
	InterruptsSpinLocker _(lock->lock);

	if (lock->holder < 0) {
		lock->holder = thread_get_current_thread_id();
		return B_OK;
	} else if (lock->holder == 0) {
		panic("_mutex_trylock(): using uninitialized lock %p", lock);
	}
	return B_WOULD_BLOCK;
#else
	return mutex_trylock_inline(lock);
#endif
}


/**
 * @brief Acquires a mutex, blocking if necessary.
 *
 * In KDEBUG builds always goes through the slow path so holder tracking
 * runs; in release builds this out-of-line symbol forwards to the inline
 * fast-path wrapper for external callers that take the mutex_lock address.
 *
 * @param lock Mutex to acquire.
 * @return B_OK on success, error from the sleep path otherwise.
 */
#undef mutex_lock
status_t
mutex_lock(mutex* lock)
{
#if KDEBUG
	return _mutex_lock(lock, NULL);
#else
	return mutex_lock_inline(lock);
#endif
}


/**
 * @brief Releases a mutex held by the current thread.
 *
 * In KDEBUG builds delegates to _mutex_unlock() so holder checks run; in
 * release builds forwards to the inline fast path (which only enters the
 * slow path when waiters are queued).
 *
 * @param lock Mutex to release.
 */
#undef mutex_unlock
void
mutex_unlock(mutex* lock)
{
#if KDEBUG
	_mutex_unlock(lock);
#else
	mutex_unlock_inline(lock);
#endif
}


/**
 * @brief Acquires a mutex with a timeout.
 *
 * In KDEBUG builds always goes through the slow-path timeout acquire; in
 * release builds forwards to the inline fast-path wrapper.
 *
 * @param lock         Mutex to acquire.
 * @param timeoutFlags Timeout flags (absolute/relative, etc.).
 * @param timeout      Timeout value (interpreted per timeoutFlags).
 * @return B_OK on acquisition; B_TIMED_OUT / B_INTERRUPTED on failure.
 */
#undef mutex_lock_with_timeout
status_t
mutex_lock_with_timeout(mutex* lock, uint32 timeoutFlags, bigtime_t timeout)
{
#if KDEBUG
	return _mutex_lock_with_timeout(lock, timeoutFlags, timeout);
#else
	return mutex_lock_with_timeout_inline(lock, timeoutFlags, timeout);
#endif
}


/**
 * @brief Kernel-debugger command that prints the state of a mutex.
 *
 * Dumps name, flags, holder (KDEBUG) or count (non-KDEBUG) and the list
 * of waiting thread ids.
 *
 * @param argc Argument count; must be at least 2.
 * @param argv argv[1] must parse to the address of a mutex.
 * @return 0 (debugger commands ignore the return value).
 */
static int
dump_mutex_info(int argc, char** argv)
{
	if (argc < 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	mutex* lock = (mutex*)parse_expression(argv[1]);

	if (!IS_KERNEL_ADDRESS(lock)) {
		kprintf("invalid address: %p\n", lock);
		return 0;
	}

	kprintf("mutex %p:\n", lock);
	kprintf("  name:            %s\n", lock->name);
	kprintf("  flags:           0x%x\n", lock->flags);
#if KDEBUG
	kprintf("  holder:          %" B_PRId32 "\n", lock->holder);
#else
	kprintf("  count:           %" B_PRId32 "\n", lock->count);
#endif

	kprintf("  waiting threads:");
	mutex_waiter* waiter = lock->waiters;
	while (waiter != NULL) {
		kprintf(" %" B_PRId32, waiter->thread->id);
		waiter = waiter->next;
	}
	kputs("\n");

	return 0;
}


// #pragma mark -


/**
 * @brief Registers the mutex / rwlock / recursivelock debugger commands.
 *
 * Called once during kernel bring-up to make the three dump_* helpers
 * available at the KDL prompt.
 */
void
lock_debug_init()
{
	add_debugger_command_etc("mutex", &dump_mutex_info,
		"Dump info about a mutex",
		"<mutex>\n"
		"Prints info about the specified mutex.\n"
		"  <mutex>  - pointer to the mutex to print the info for.\n", 0);
	add_debugger_command_etc("rwlock", &dump_rw_lock_info,
		"Dump info about an rw lock",
		"<lock>\n"
		"Prints info about the specified rw lock.\n"
		"  <lock>  - pointer to the rw lock to print the info for.\n", 0);
	add_debugger_command_etc("recursivelock", &dump_recursive_lock_info,
		"Dump info about a recursive lock",
		"<lock>\n"
		"Prints info about the specified recursive lock.\n"
		"  <lock>  - pointer to the recursive lock to print the info for.\n",
		0);
}
