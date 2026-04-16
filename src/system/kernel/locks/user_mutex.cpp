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
 *   Copyright 2023, Haiku, Inc. All rights reserved.
 *   Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 *   Copyright 2015, Hamish Morrison, hamishm53@gmail.com.
 *   Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file user_mutex.cpp
 * @brief Kernel backend for userspace shared mutexes and semaphores (futexes).
 *
 * Implements the _user_mutex_* and _user_mutex_sem_* syscalls. The userspace
 * side owns a single 32-bit word per mutex that encodes the fast-path state:
 *  - B_USER_MUTEX_LOCKED: mutex is held by some thread.
 *  - B_USER_MUTEX_WAITING: at least one waiter may be blocked in the kernel.
 *  - B_USER_MUTEX_DISABLED: owner died / mutex abandoned; all waiters are
 *    released with B_OK and future locks succeed without contention.
 * Userspace performs uncontended acquire/release entirely with atomic ops
 * on the shared word; it only enters the kernel when the WAITING bit must
 * be set (on lock) or acted upon (on unlock).
 *
 * The kernel keeps a side table of UserMutexEntry structures keyed by the
 * mutex's address (virtual, per-team, for process-private mutexes; physical,
 * via vm_wire_page(), for B_USER_MUTEX_SHARED mutexes that may be mapped at
 * different virtual addresses in different teams). Each entry carries a
 * ConditionVariable that the kernel uses to park and wake blocked threads,
 * plus an rw_lock that serialises the state of that single address: waiters
 * take a read lock, while a releaser takes the write lock so it can safely
 * observe an empty waiter set and clear the WAITING bit without racing
 * against a new arrival. Entries are refcounted; the last put removes the
 * entry from the hash table and destroys it.
 *
 * All entry points validate that @p mutex is a 4-byte-aligned user address,
 * may sleep (interrupts must be enabled), and honour B_CAN_INTERRUPT plus
 * the usual syscall-restart semantics for relative timeouts.
 */


#include <user_mutex.h>
#include <user_mutex_defs.h>

#include <condition_variable.h>
#include <kernel.h>
#include <lock.h>
#include <smp.h>
#include <syscall_restart.h>
#include <util/AutoLock.h>
#include <util/ThreadAutoLock.h>
#include <util/OpenHashTable.h>
#include <vm/vm.h>
#include <vm/VMArea.h>
#include <arch/generic/user_memory.h>


/**
 * @brief Per-address kernel state for a user-space mutex or semaphore.
 *
 * One UserMutexEntry corresponds to one mutex address. The mutex's
 * "waiting" state is controlled by the rw_lock: a waiter acquires a
 * "read" lock before initiating a wait, and an unblocker acquires a
 * "write" lock. That way, unblockers can be sure that no waiters will
 * start waiting during unblock, and they can thus safely (without races)
 * unset WAITING. Entries are reference-counted; the last put removes them
 * from the owning hash table.
 */
struct UserMutexEntry {
	generic_addr_t		address;
	UserMutexEntry*		hash_next;
	int32				ref_count;

	rw_lock				lock;
	ConditionVariable	condition;
};

/**
 * @brief BOpenHashTable policy for UserMutexEntry.
 *
 * Keys the table by the mutex's address and threads entries through
 * UserMutexEntry::hash_next.
 */
struct UserMutexHashDefinition {
	typedef generic_addr_t	KeyType;
	typedef UserMutexEntry	ValueType;

	/**
	 * @brief Hashes a mutex address.
	 *
	 * Right-shifts by 2 because mutex addresses are always 4-byte aligned,
	 * dropping the guaranteed-zero low bits for better distribution.
	 *
	 * @param key Mutex address (virtual or physical, 4-byte aligned).
	 * @return Hash bucket index.
	 */
	size_t HashKey(generic_addr_t key) const
	{
		return key >> 2;
	}

	/**
	 * @brief Hashes an existing entry using its stored address.
	 *
	 * @param value Entry to hash.
	 * @return Hash bucket index for the entry's address.
	 */
	size_t Hash(const UserMutexEntry* value) const
	{
		return HashKey(value->address);
	}

	/**
	 * @brief Compares a lookup key against an existing entry.
	 *
	 * @param key   Mutex address being looked up.
	 * @param value Entry currently in the bucket.
	 * @return true if they refer to the same mutex address.
	 */
	bool Compare(generic_addr_t key, const UserMutexEntry* value) const
	{
		return value->address == key;
	}

	/**
	 * @brief Returns the storage used to thread entries together in a bucket.
	 *
	 * @param value Entry whose hash link to expose.
	 * @return Reference to the entry's next-pointer field.
	 */
	UserMutexEntry*& GetLink(UserMutexEntry* value) const
	{
		return value->hash_next;
	}
};

typedef BOpenHashTable<UserMutexHashDefinition> UserMutexTable;


/**
 * @brief Container for a set of UserMutexEntry objects.
 *
 * Each Team has a private instance; a single global instance
 * (sSharedUserMutexContext) holds entries for B_USER_MUTEX_SHARED
 * mutexes keyed by physical address.
 */
struct user_mutex_context {
	UserMutexTable table;
	rw_lock lock;
};
/**
 * @brief Global context for B_USER_MUTEX_SHARED mutexes across teams.
 *
 * Keyed by physical address so the same underlying page is resolved no
 * matter which team's virtual mapping is used.
 */
static user_mutex_context sSharedUserMutexContext;

/**
 * @brief Type tag stored on UserMutexEntry condition variables.
 *
 * Allows the "user_mutex" KDL command to distinguish condition variables
 * owned by this subsystem from other condition variables a thread may be
 * blocked on.
 */
static const char* kUserMutexEntryType = "umtx entry";


// #pragma mark - user atomics


/**
 * @brief Performs an atomic OR on a userspace word, with safe access setup.
 *
 * If @p isWired is true the page is already wired (shared-mutex path) and
 * user-access is enabled around the atomic op directly. Otherwise the
 * operation runs inside user_access() so a user page fault can be
 * recovered from and reported as failure.
 *
 * @param value   User address of the word.
 * @param orValue Bits to OR in.
 * @param isWired true if the page is wired via vm_wire_page().
 * @return The previous value of the word, or INT32_MIN if the user access
 *         faulted.
 */
static int32
user_atomic_or(int32* value, int32 orValue, bool isWired)
{
	int32 result;
	if (isWired) {
		arch_cpu_enable_user_access();
		result = atomic_or(value, orValue);
		arch_cpu_disable_user_access();
		return result;
	}

	return user_access([=, &result] {
		result = atomic_or(value, orValue);
	}) ? result : INT32_MIN;
}


/**
 * @brief Performs an atomic AND on a userspace word.
 *
 * Companion of user_atomic_or(); used primarily to clear the WAITING or
 * LOCKED bits in the shared mutex word. Same faulting semantics: returns
 * INT32_MIN on a user-access fault.
 *
 * @param value    User address of the word.
 * @param andValue Mask to AND with.
 * @param isWired  true if the page is wired.
 * @return Previous value, or INT32_MIN on fault.
 */
static int32
user_atomic_and(int32* value, int32 andValue, bool isWired)
{
	int32 result;
	if (isWired) {
		arch_cpu_enable_user_access();
		result = atomic_and(value, andValue);
		arch_cpu_disable_user_access();
		return result;
	}

	return user_access([=, &result] {
		result = atomic_and(value, andValue);
	}) ? result : INT32_MIN;
}


/**
 * @brief Atomically reads a userspace word.
 *
 * Uses the same wired/user_access dispatch as the other user_atomic_*
 * helpers.
 *
 * @param value   User address of the word.
 * @param isWired true if the page is wired.
 * @return Current value, or INT32_MIN on fault.
 */
static int32
user_atomic_get(int32* value, bool isWired)
{
	int32 result;
	if (isWired) {
		arch_cpu_enable_user_access();
		result = atomic_get(value);
		arch_cpu_disable_user_access();
		return result;
	}

	return user_access([=, &result] {
		result = atomic_get(value);
	}) ? result : INT32_MIN;
}


/**
 * @brief Atomic compare-and-swap on a userspace word.
 *
 * Replaces *value with @p newValue iff the current value equals
 * @p testAgainst. Returns the previous value (whether or not the swap
 * happened) in the usual atomic_test_and_set convention, or INT32_MIN on
 * a user-access fault.
 *
 * @param value       User address of the word.
 * @param newValue    Value to install on success.
 * @param testAgainst Expected current value.
 * @param isWired     true if the page is wired.
 * @return Previous value, or INT32_MIN on fault.
 */
static int32
user_atomic_test_and_set(int32* value, int32 newValue, int32 testAgainst,
	bool isWired)
{
	int32 result;
	if (isWired) {
		arch_cpu_enable_user_access();
		result = atomic_test_and_set(value, newValue, testAgainst);
		arch_cpu_disable_user_access();
		return result;
	}

	return user_access([=, &result] {
		result = atomic_test_and_set(value, newValue, testAgainst);
	}) ? result : INT32_MIN;
}


// #pragma mark - user mutex context


/**
 * @brief Kernel-debugger command that prints user-mutex state for a thread.
 *
 * Given a thread id, finds the condition-variable entry it is blocked on,
 * confirms it belongs to a UserMutexEntry, and prints the address (marked
 * physical if it is in the shared table), refcount, entry lock pointer,
 * and - for process-private mutexes - the current value of the userspace
 * word read via debug_memcpy.
 *
 * @param argc Argument count; must be 2.
 * @param argv argv[1] parses to a thread id.
 * @return 0 (debugger commands ignore the return value).
 */
static int
dump_user_mutex(int argc, char** argv)
{
	if (argc != 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	addr_t threadID = parse_expression(argv[1]);
	if (threadID == 0)
		return 0;

	Thread* thread = Thread::GetDebug(threadID);
	if (thread == NULL) {
		kprintf("no such thread\n");
		return 0;
	}

	if (thread->wait.type != THREAD_BLOCK_TYPE_CONDITION_VARIABLE) {
		kprintf("thread is not blocked on cvar (thus not user_mutex)\n");
		return 0;
	}

	ConditionVariable* variable = (ConditionVariable*)thread->wait.object;
	if (variable->ObjectType() != kUserMutexEntryType) {
		kprintf("thread is not blocked on user_mutex\n");
		return 0;
	}

	UserMutexEntry* entry = (UserMutexEntry*)variable->Object();

	const bool physical = (sSharedUserMutexContext.table.Lookup(entry->address) == entry);
	kprintf("user mutex entry %p\n", entry);
	kprintf("  address:  0x%" B_PRIxPHYSADDR " (%s)\n", entry->address,
		physical ? "physical" : "virtual");
	kprintf("  refcount: %" B_PRId32 "\n", entry->ref_count);
	kprintf("  lock:     %p\n", &entry->lock);

	int32 mutex = 0;
	status_t status = B_ERROR;
	if (!physical) {
		status = debug_memcpy(thread->team->id, &mutex,
			(void*)entry->address, sizeof(mutex));
	}

	if (status == B_OK)
		kprintf("  mutex:    0x%" B_PRIx32 "\n", mutex);

	entry->condition.Dump();

	return 0;
}


/**
 * @brief Initializes the global shared-user-mutex context.
 *
 * Initializes the rw_lock guarding the shared table and the table itself,
 * then registers the "user_mutex" debugger command. Panics if the hash
 * table cannot be initialized, because the subsystem is mandatory for
 * cross-team pthread primitives.
 */
void
user_mutex_init()
{
	sSharedUserMutexContext.lock = RW_LOCK_INITIALIZER("shared user mutex table");
	if (sSharedUserMutexContext.table.Init() != B_OK)
		panic("user_mutex_init(): Failed to init table!");

	add_debugger_command_etc("user_mutex", &dump_user_mutex,
		"Dump user-mutex info",
		"<thread>\n"
		"Prints info about the user-mutex a thread is blocked on.\n"
		"  <thread>  - Thread ID that is blocked on a user mutex\n", 0);
}


/**
 * @brief Lazily allocates a per-team context for process-private user mutexes.
 *
 * A team only needs this context once it uses a private user mutex, which
 * is why it is not created at team birth. Must be called with the team
 * lock held. Idempotent: returns B_OK immediately if a context already
 * exists.
 *
 * @param team Team that needs a private user-mutex context.
 * @return B_OK on success; B_NO_MEMORY or a hash-table init error otherwise.
 */
status_t
allocate_team_user_mutex_context(Team* team)
{
	team->AssertLocked();
	if (team->user_mutex_context != NULL)
		return B_OK;

	struct user_mutex_context* context = new(std::nothrow) user_mutex_context;
	if (context == NULL)
		return B_NO_MEMORY;

	context->lock = RW_LOCK_INITIALIZER("user mutex table");
	status_t status = context->table.Init();
	if (status != B_OK) {
		delete context;
		return status;
	}

	team->user_mutex_context = context;
	return B_OK;
}


/**
 * @brief Destroys a per-team user-mutex context at team teardown.
 *
 * Accepts NULL as a no-op. ASSERTs the hash table is empty, which must be
 * the case during team destruction because every outstanding entry holds
 * a reference via a blocked thread.
 *
 * @param context Context to destroy; may be NULL.
 */
void
delete_user_mutex_context(struct user_mutex_context* context)
{
	if (context == NULL)
		return;

	// This should be empty at this point in team destruction.
	ASSERT(context->table.IsEmpty());
	delete context;
}


/**
 * @brief Looks up or allocates a UserMutexEntry for a given mutex address.
 *
 * Uses a two-phase read-then-write locking strategy: a read lock is tried
 * first (the common case where the entry already exists), and only on a
 * miss is the write lock taken for insertion. The double-lookup after
 * upgrading handles the race where another thread inserted the entry
 * while we were waiting for the write lock. Every returned entry has had
 * its reference count incremented and must be released with
 * put_user_mutex_entry().
 *
 * @param context       Per-team or shared context.
 * @param address       Mutex address (virtual for private, physical for
 *                      shared).
 * @param noInsert      If true, do not create a new entry on miss.
 * @param alreadyLocked If true, the caller already holds @p context->lock
 *                      for reading and this routine leaves it held.
 * @return Referenced entry, or NULL on lookup miss with @p noInsert or on
 *         allocation failure (panics in the latter case).
 */
static UserMutexEntry*
get_user_mutex_entry(struct user_mutex_context* context,
	generic_addr_t address, bool noInsert = false, bool alreadyLocked = false)
{
	ReadLocker tableReadLocker;
	if (!alreadyLocked)
		tableReadLocker.SetTo(context->lock, false);

	UserMutexEntry* entry = context->table.Lookup(address);
	if (entry != NULL) {
		atomic_add(&entry->ref_count, 1);
		return entry;
	} else if (noInsert)
		return entry;

	tableReadLocker.Unlock();
	WriteLocker tableWriteLocker(context->lock);

	entry = context->table.Lookup(address);
	if (entry != NULL) {
		atomic_add(&entry->ref_count, 1);
		return entry;
	}

	entry = new(std::nothrow) UserMutexEntry;
	if (entry == NULL) {
		panic("UserMutexEntry allocation failed!");
		return entry;
	}

	entry->address = address;
	entry->ref_count = 1;
	rw_lock_init(&entry->lock, "UserMutexEntry lock");
	entry->condition.Init(entry, kUserMutexEntryType);

	context->table.Insert(entry);
	return entry;
}


/**
 * @brief Drops a reference on a UserMutexEntry, destroying it on zero.
 *
 * Fast-path decrements atomically; only when we drop the last reference
 * do we take the context's write lock and remove/destroy. A second
 * post-lock check covers the window where another thread both found and
 * released the entry while we were racing to acquire the write lock, or
 * where someone else already removed it.
 *
 * @param context Context the entry belongs to.
 * @param entry   Entry to release; may be NULL (no-op).
 */
static void
put_user_mutex_entry(struct user_mutex_context* context, UserMutexEntry* entry)
{
	if (entry == NULL)
		return;

	const generic_addr_t address = entry->address;
	if (atomic_add(&entry->ref_count, -1) != 1)
		return;

	WriteLocker tableWriteLocker(context->lock);

	// Was it removed & deleted while we were waiting for the lock?
	if (context->table.Lookup(address) != entry)
		return;

	// Or did someone else acquire a reference to it?
	if (atomic_get(&entry->ref_count) > 0)
		return;

	context->table.Remove(entry);
	tableWriteLocker.Unlock();

	rw_lock_destroy(&entry->lock);
	delete entry;
}


/**
 * @brief Parks the caller on a UserMutexEntry's condition variable and sleeps.
 *
 * Adds a ConditionVariableEntry to the entry's condition, releases the
 * entry's read lock so the releaser can take the write lock, then blocks
 * in the condition variable. The caller remains responsible for releasing
 * its reference on @p entry after this returns.
 *
 * @param entry   User mutex entry to wait on.
 * @param flags   Wait flags (B_CAN_INTERRUPT and timeout mode bits).
 * @param timeout Timeout; B_INFINITE_TIMEOUT for no timeout.
 * @param locker  Read-locker on entry->lock; will be unlocked by this call.
 * @return B_OK on wakeup by the releaser; B_TIMED_OUT / B_INTERRUPTED etc.
 */
static status_t
user_mutex_wait_locked(UserMutexEntry* entry,
	uint32 flags, bigtime_t timeout, ReadLocker& locker)
{
	ConditionVariableEntry waiter;
	entry->condition.Add(&waiter);
	locker.Unlock();

	return waiter.Wait(flags, timeout);
}


/**
 * @brief Attempts a kernel-assisted acquisition of a user mutex.
 *
 * Atomically ORs LOCKED | WAITING into the userspace word. If the prior
 * value was not LOCKED (we just acquired it) or the mutex is DISABLED
 * (owner died, every acquire succeeds), returns true. If we just added
 * the WAITING bit spuriously (because the lock was actually free and no
 * one else was waiting), we must clear it so unlockers don't do a useless
 * syscall; that requires briefly upgrading the entry's read lock to a
 * write lock. Must be called with the entry read-locked.
 *
 * @param entry   UserMutexEntry associated with @p mutex.
 * @param mutex   User address of the mutex word.
 * @param isWired true if the page is wired (shared-mutex path).
 * @return true if the caller acquired the mutex; false if it should block.
 */
static bool
user_mutex_prepare_to_lock(UserMutexEntry* entry, int32* mutex, bool isWired)
{
	ASSERT_READ_LOCKED_RW_LOCK(&entry->lock);

	int32 oldValue = user_atomic_or(mutex,
		B_USER_MUTEX_LOCKED | B_USER_MUTEX_WAITING, isWired);
	if ((oldValue & B_USER_MUTEX_LOCKED) == 0
			|| (oldValue & B_USER_MUTEX_DISABLED) != 0) {
		// possibly unset waiting flag
		if ((oldValue & B_USER_MUTEX_WAITING) == 0) {
			rw_lock_read_unlock(&entry->lock);
			rw_lock_write_lock(&entry->lock);
			if (entry->condition.EntriesCount() == 0)
				user_atomic_and(mutex, ~(int32)B_USER_MUTEX_WAITING, isWired);
			rw_lock_write_unlock(&entry->lock);
			rw_lock_read_lock(&entry->lock);
		}
		return true;
	}

	return false;
}


/**
 * @brief Complete acquire path for a user mutex with the entry read-locked.
 *
 * Tries the kernel-assisted fast path via user_mutex_prepare_to_lock();
 * if that says the lock is not available, blocks on the entry's condition
 * variable. On timeout/interrupt, clears the WAITING bit if no other
 * waiters remain so future releasers won't enter the kernel needlessly.
 *
 * @param entry   UserMutexEntry for @p mutex.
 * @param mutex   User address of the mutex word.
 * @param flags   Wait flags (B_CAN_INTERRUPT, timeout mode).
 * @param timeout Timeout value.
 * @param locker  Read-locker on entry->lock.
 * @param isWired true if the page is wired.
 * @return B_OK on acquisition, wait error otherwise.
 */
static status_t
user_mutex_lock_locked(UserMutexEntry* entry, int32* mutex,
	uint32 flags, bigtime_t timeout, ReadLocker& locker, bool isWired)
{
	if (user_mutex_prepare_to_lock(entry, mutex, isWired))
		return B_OK;

	status_t error = user_mutex_wait_locked(entry, flags, timeout, locker);

	// possibly unset waiting flag
	if (error != B_OK && entry->condition.EntriesCount() == 0) {
		WriteLocker writeLocker(entry->lock);
		if (entry->condition.EntriesCount() == 0)
			user_atomic_and(mutex, ~(int32)B_USER_MUTEX_WAITING, isWired);
	}

	return error;
}


/**
 * @brief Wakes waiters on a user mutex.
 *
 * Takes the entry's write lock so it can safely observe an empty waiter
 * set and clear WAITING. Two modes:
 *  - Handoff (default): sets LOCKED atomically and wakes exactly one
 *    waiter so the lock passes directly from releaser to next owner,
 *    preventing barging by freshly arriving threads. If no one was
 *    actually queued, LOCKED is cleared again.
 *  - B_USER_MUTEX_UNBLOCK_ALL: all waiters are released; also used in
 *    the DISABLED (owner-died) path where every blocked thread should
 *    wake and observe the disabled state.
 * Clears WAITING once the queue is drained.
 *
 * @param entry   UserMutexEntry for @p mutex.
 * @param mutex   User address of the mutex word.
 * @param flags   Unblock mode flags.
 * @param isWired true if the page is wired.
 */
static void
user_mutex_unblock(UserMutexEntry* entry, int32* mutex, uint32 flags, bool isWired)
{
	WriteLocker entryLocker(entry->lock);
	if (entry->condition.EntriesCount() == 0) {
		// Nobody is actually waiting at present.
		user_atomic_and(mutex, ~(int32)B_USER_MUTEX_WAITING, isWired);
		return;
	}

	int32 oldValue = 0;
	if ((flags & B_USER_MUTEX_UNBLOCK_ALL) == 0) {
		// This is not merely an unblock, but a hand-off.
		oldValue = user_atomic_or(mutex, B_USER_MUTEX_LOCKED, isWired);
		if ((oldValue & B_USER_MUTEX_LOCKED) != 0)
			return;
	}

	if ((flags & B_USER_MUTEX_UNBLOCK_ALL) != 0
			|| (oldValue & B_USER_MUTEX_DISABLED) != 0) {
		// unblock all waiting threads
		entry->condition.NotifyAll(B_OK);
	} else {
		if (!entry->condition.NotifyOne(B_OK))
			user_atomic_and(mutex, ~(int32)B_USER_MUTEX_LOCKED, isWired);
	}

	if (entry->condition.EntriesCount() == 0)
		user_atomic_and(mutex, ~(int32)B_USER_MUTEX_WAITING, isWired);
}


/**
 * @brief Acquire path for a counting user semaphore.
 *
 * The semaphore's shared word holds a positive count of available
 * permits, 0 for empty-uncontended, or a negative number for
 * empty-contended (the negative magnitude encodes that waiters exist).
 * Spins on CAS decrementing the count while it is positive; if we read
 * zero-or-negative we fall through and wait on the condition variable.
 *
 * @param entry   UserMutexEntry for @p sem.
 * @param sem     User address of the semaphore word.
 * @param flags   Wait flags.
 * @param timeout Timeout value.
 * @param locker  Read-locker on entry->lock.
 * @param isWired true if the page is wired.
 * @return B_OK on success; B_TIMED_OUT / B_INTERRUPTED on failure.
 */
static status_t
user_mutex_sem_acquire_locked(UserMutexEntry* entry, int32* sem,
	uint32 flags, bigtime_t timeout, ReadLocker& locker, bool isWired)
{
	// The semaphore may have been released in the meantime, and we also
	// need to mark it as contended if it isn't already.
	int32 oldValue = user_atomic_get(sem, isWired);
	while (oldValue > -1) {
		int32 value = user_atomic_test_and_set(sem, oldValue - 1, oldValue, isWired);
		if (value == oldValue && value > 0)
			return B_OK;
		oldValue = value;
	}

	return user_mutex_wait_locked(entry, flags,
		timeout, locker);
}


/**
 * @brief Release path for a counting user semaphore.
 *
 * Takes the entry's write lock and wakes one waiter, if any. If no thread
 * was actually queued, CAS-increments the count (by 2 from a negative
 * value to cross back into the positive/uncontended range, by 1 otherwise)
 * until it lands. When the queue drains, marks the semaphore uncontended
 * (0) if it was at -1.
 *
 * @param entry   UserMutexEntry for @p sem.
 * @param sem     User address of the semaphore word.
 * @param isWired true if the page is wired.
 */
static void
user_mutex_sem_release(UserMutexEntry* entry, int32* sem, bool isWired)
{
	WriteLocker entryLocker(entry->lock);
	if (entry->condition.NotifyOne(B_OK) == 0) {
		// no waiters - mark as uncontended and release
		int32 oldValue = user_atomic_get(sem, isWired);
		while (true) {
			int32 inc = oldValue < 0 ? 2 : 1;
			int32 value = user_atomic_test_and_set(sem, oldValue + inc, oldValue, isWired);
			if (value == oldValue)
				return;
			oldValue = value;
		}
	}

	if (entry->condition.EntriesCount() == 0) {
		// mark the semaphore uncontended
		user_atomic_test_and_set(sem, 0, -1, isWired);
	}
}


// #pragma mark - syscalls


/**
 * @brief RAII helper for resolving a user mutex to its kernel-side bookkeeping.
 *
 * Constructed once per syscall; Context() and Address() describe where
 * the entry lives (per-team context with virtual key, or shared context
 * with physical key) and IsWired() tells atomic helpers whether a
 * user_access wrapper is needed.
 */
struct UserMutexContextFetcher {
	/**
	 * @brief RAII helper that resolves a user mutex to a (context, address) pair.
	 *
	 * For process-private mutexes, selects the current team's context
	 * (lazily allocating it on first use) and stores the virtual address
	 * as-is. For shared mutexes, wires the mutex page via vm_wire_page()
	 * so the physical address is stable for the life of the fetcher, then
	 * stores that physical address; the wiring pins the page so kernel
	 * code can atomic_* it directly without extra user_access checks.
	 *
	 * @param mutex User address of the mutex word.
	 * @param flags Mutex flags; B_USER_MUTEX_SHARED selects the shared path.
	 */
	UserMutexContextFetcher(int32* mutex, uint32 flags)
		:
		fInitStatus(B_OK),
		fShared((flags & B_USER_MUTEX_SHARED) != 0),
		fAddress(0)
	{
		if (!fShared) {
			fContext = thread_get_current_thread()->team->user_mutex_context;
			if (fContext == NULL) {
				// This should only happen for single-threaded applications (some
				// appear to use mutexes as a sleep mechanism), as the context gets
				// allocated on the creation of a second thread.
				Team* team = thread_get_current_thread()->team;
				team->Lock();
				fInitStatus = allocate_team_user_mutex_context(team);
				fContext = team->user_mutex_context;
				team->Unlock();
				if (fInitStatus != B_OK)
					return;
			}

			fAddress = (addr_t)mutex;
		} else {
			fContext = &sSharedUserMutexContext;

			// wire the page and get the physical address
			fInitStatus = vm_wire_page(B_CURRENT_TEAM, (addr_t)mutex, true,
				&fWiringInfo);
			if (fInitStatus != B_OK)
				return;
			fAddress = fWiringInfo.physicalAddress;
		}
	}

	/**
	 * @brief Releases resources, in particular unwires a shared mutex page.
	 *
	 * No-op if construction failed (InitCheck() != B_OK).
	 */
	~UserMutexContextFetcher()
	{
		if (fInitStatus != B_OK)
			return;

		if (fShared)
			vm_unwire_page(&fWiringInfo);
	}

	/**
	 * @brief Returns the initialization status of the fetcher.
	 *
	 * @return B_OK if the context and address are usable; error code
	 *         otherwise (callers must bail out early).
	 */
	status_t InitCheck() const
		{ return fInitStatus; }

	/**
	 * @brief Returns the resolved user-mutex context.
	 *
	 * @return Either the per-team context or the global shared context.
	 */
	struct user_mutex_context* Context() const
		{ return fContext; }

	/**
	 * @brief Returns the resolved hash-table key for the mutex.
	 *
	 * @return Virtual address for private mutexes, physical address for
	 *         shared mutexes.
	 */
	generic_addr_t Address() const
		{ return fAddress; }

	/**
	 * @brief Indicates whether the mutex page is wired for direct access.
	 *
	 * @return true for shared mutexes, false for private.
	 */
	bool IsWired() const
		{ return fShared; }

private:
	status_t fInitStatus;
	bool fShared;
	struct user_mutex_context* fContext;
	VMPageWiringInfo fWiringInfo;
	generic_addr_t fAddress;
};


/**
 * @brief Internal helper that performs a user-mutex lock.
 *
 * Resolves context via UserMutexContextFetcher, looks up/creates the
 * UserMutexEntry, and completes the acquire via user_mutex_lock_locked()
 * under the entry's read lock. Drops the entry reference on exit.
 *
 * @param mutex   User address of the mutex word.
 * @param name    Informational name (currently unused beyond the ABI).
 * @param flags   B_USER_MUTEX_SHARED, B_CAN_INTERRUPT, timeout-mode bits.
 * @param timeout Timeout value.
 * @return B_OK on acquisition; error code otherwise.
 */
static status_t
user_mutex_lock(int32* mutex, const char* name, uint32 flags, bigtime_t timeout)
{
	UserMutexContextFetcher contextFetcher(mutex, flags);
	if (contextFetcher.InitCheck() != B_OK)
		return contextFetcher.InitCheck();

	// get the lock
	UserMutexEntry* entry = get_user_mutex_entry(contextFetcher.Context(),
		contextFetcher.Address());
	if (entry == NULL)
		return B_NO_MEMORY;
	status_t error = B_OK;
	{
		ReadLocker entryLocker(entry->lock);
		error = user_mutex_lock_locked(entry, mutex,
			flags, timeout, entryLocker, contextFetcher.IsWired());
	}
	put_user_mutex_entry(contextFetcher.Context(), entry);

	return error;
}


/**
 * @brief Atomically unlocks one user mutex and acquires another.
 *
 * The hand-off is staged so there is no window in which another thread
 * could barge onto @p toMutex after it becomes reachable but before the
 * caller is queued: first the destination is prepared (entry locked and,
 * if necessary, a condition-variable waiter registered), then the source
 * LOCKED bit is cleared and its WAITING bit honoured with an unblock if
 * set, and only then does the caller block on the destination's waiter.
 *
 * @param fromMutex User address of the source mutex word.
 * @param fromFlags Flags for the source mutex.
 * @param toMutex   User address of the destination mutex word.
 * @param name      Informational name.
 * @param toFlags   Flags for the destination mutex (incl. B_CAN_INTERRUPT).
 * @param timeout   Timeout for the destination acquire.
 * @return B_OK on acquisition; error from the wait otherwise.
 */
static status_t
user_mutex_switch_lock(int32* fromMutex, uint32 fromFlags,
	int32* toMutex, const char* name, uint32 toFlags, bigtime_t timeout)
{
	UserMutexContextFetcher fromFetcher(fromMutex, fromFlags);
	if (fromFetcher.InitCheck() != B_OK)
		return fromFetcher.InitCheck();

	UserMutexContextFetcher toFetcher(toMutex, toFlags);
	if (toFetcher.InitCheck() != B_OK)
		return toFetcher.InitCheck();

	// unlock the first mutex and lock the second one
	UserMutexEntry* fromEntry = NULL,
		*toEntry = get_user_mutex_entry(toFetcher.Context(), toFetcher.Address());
	if (toEntry == NULL)
		return B_NO_MEMORY;
	status_t error = B_OK;
	{
		ConditionVariableEntry waiter;

		bool alreadyLocked = false;
		{
			ReadLocker entryLocker(toEntry->lock);
			alreadyLocked = user_mutex_prepare_to_lock(toEntry, toMutex,
				toFetcher.IsWired());
			if (!alreadyLocked)
				toEntry->condition.Add(&waiter);
		}

		const int32 oldValue = user_atomic_and(fromMutex, ~(int32)B_USER_MUTEX_LOCKED,
			fromFetcher.IsWired());
		if ((oldValue & B_USER_MUTEX_WAITING) != 0) {
			fromEntry = get_user_mutex_entry(fromFetcher.Context(),
				fromFetcher.Address(), true);
			 if (fromEntry != NULL) {
				 user_mutex_unblock(fromEntry, fromMutex, fromFlags,
					 fromFetcher.IsWired());
			 }
		}

		if (!alreadyLocked)
			error = waiter.Wait(toFlags, timeout);
	}
	put_user_mutex_entry(fromFetcher.Context(), fromEntry);
	put_user_mutex_entry(toFetcher.Context(), toEntry);

	return error;
}


/**
 * @brief Syscall: block until a user mutex can be acquired.
 *
 * Validates that @p mutex is a 4-byte-aligned user address, then calls
 * user_mutex_lock() with B_CAN_INTERRUPT added so it can be interrupted
 * by signals. Uses syscall_restart_handle_timeout_* to convert relative
 * timeouts into absolute ones across signal-driven restarts.
 *
 * @param mutex   User address of the mutex word.
 * @param name    Optional informational name from libroot.
 * @param flags   Caller-supplied flags.
 * @param timeout Timeout value.
 * @return B_OK on acquisition; B_BAD_ADDRESS on invalid input;
 *         B_TIMED_OUT / B_INTERRUPTED etc. on failure.
 */
status_t
_user_mutex_lock(int32* mutex, const char* name, uint32 flags,
	bigtime_t timeout)
{
	if (mutex == NULL || !IS_USER_ADDRESS(mutex) || (addr_t)mutex % 4 != 0)
		return B_BAD_ADDRESS;

	syscall_restart_handle_timeout_pre(flags, timeout);

	status_t error = user_mutex_lock(mutex, name, flags | B_CAN_INTERRUPT,
		timeout);

	return syscall_restart_handle_timeout_post(error, timeout);
}


/**
 * @brief Syscall: wake waiters on a user mutex (userspace unlock slow path).
 *
 * Called by libroot's pthread_mutex_unlock() when WAITING is set in the
 * shared word. Looks up the entry with the table read lock held; if no
 * entry exists, the WAITING bit was stale (no one actually blocked in
 * the kernel), so it is cleared while still holding the read lock to
 * prevent a concurrent waiter from racing in. Otherwise dispatches to
 * user_mutex_unblock() which does the handoff or broadcast.
 *
 * @param mutex User address of the mutex word.
 * @param flags Caller-supplied flags (B_USER_MUTEX_UNBLOCK_ALL, etc.).
 * @return B_OK normally; B_BAD_ADDRESS for invalid input; context init
 *         errors otherwise.
 */
status_t
_user_mutex_unblock(int32* mutex, uint32 flags)
{
	if (mutex == NULL || !IS_USER_ADDRESS(mutex) || (addr_t)mutex % 4 != 0)
		return B_BAD_ADDRESS;

	UserMutexContextFetcher contextFetcher(mutex, flags);
	if (contextFetcher.InitCheck() != B_OK)
		return contextFetcher.InitCheck();
	struct user_mutex_context* context = contextFetcher.Context();

	// In the case where there is no entry, we must hold the read lock until we
	// unset WAITING, because otherwise some other thread could initiate a wait.
	ReadLocker tableReadLocker(context->lock);
	UserMutexEntry* entry = get_user_mutex_entry(context,
		contextFetcher.Address(), true, true);
	if (entry == NULL) {
		user_atomic_and(mutex, ~(int32)B_USER_MUTEX_WAITING, contextFetcher.IsWired());
		tableReadLocker.Unlock();
	} else {
		tableReadLocker.Unlock();
		user_mutex_unblock(entry, mutex, flags, contextFetcher.IsWired());
	}
	put_user_mutex_entry(context, entry);

	return B_OK;
}


/**
 * @brief Syscall: atomically release one user mutex and acquire another.
 *
 * Validates both addresses as 4-byte-aligned user pointers, then forwards
 * to user_mutex_switch_lock() with B_CAN_INTERRUPT forced on for the
 * destination.
 *
 * @param fromMutex User address of the source mutex.
 * @param fromFlags Flags for the source.
 * @param toMutex   User address of the destination mutex.
 * @param name      Informational name from libroot.
 * @param toFlags   Flags for the destination acquire.
 * @param timeout   Timeout value.
 * @return B_OK on acquisition; B_BAD_ADDRESS on invalid input; else wait
 *         error code.
 */
status_t
_user_mutex_switch_lock(int32* fromMutex, uint32 fromFlags,
	int32* toMutex, const char* name, uint32 toFlags, bigtime_t timeout)
{
	if (fromMutex == NULL || !IS_USER_ADDRESS(fromMutex)
			|| (addr_t)fromMutex % 4 != 0 || toMutex == NULL
			|| !IS_USER_ADDRESS(toMutex) || (addr_t)toMutex % 4 != 0) {
		return B_BAD_ADDRESS;
	}

	return user_mutex_switch_lock(fromMutex, fromFlags, toMutex, name,
		toFlags | B_CAN_INTERRUPT, timeout);
}


/**
 * @brief Syscall: acquire one count of a user-shared semaphore.
 *
 * Validates the address, resolves context, looks up/creates the entry
 * and calls user_mutex_sem_acquire_locked() under the entry's read lock.
 * Handles syscall-restart for relative timeouts.
 *
 * @param sem     User address of the semaphore word.
 * @param name    Informational name.
 * @param flags   Caller flags (B_USER_MUTEX_SHARED etc.).
 * @param timeout Timeout value.
 * @return B_OK on acquisition; B_BAD_ADDRESS / B_NO_MEMORY / wait errors.
 */
status_t
_user_mutex_sem_acquire(int32* sem, const char* name, uint32 flags,
	bigtime_t timeout)
{
	if (sem == NULL || !IS_USER_ADDRESS(sem) || (addr_t)sem % 4 != 0)
		return B_BAD_ADDRESS;

	syscall_restart_handle_timeout_pre(flags, timeout);

	UserMutexContextFetcher contextFetcher(sem, flags);
	if (contextFetcher.InitCheck() != B_OK)
		return contextFetcher.InitCheck();
	struct user_mutex_context* context = contextFetcher.Context();

	UserMutexEntry* entry = get_user_mutex_entry(context, contextFetcher.Address());
	if (entry == NULL)
		return B_NO_MEMORY;
	status_t error;
	{
		ReadLocker entryLocker(entry->lock);
		error = user_mutex_sem_acquire_locked(entry, sem,
			flags | B_CAN_INTERRUPT, timeout, entryLocker, contextFetcher.IsWired());
	}
	put_user_mutex_entry(context, entry);

	return syscall_restart_handle_timeout_post(error, timeout);
}


/**
 * @brief Syscall: release one count of a user-shared semaphore.
 *
 * Validates the address, resolves context and entry, and calls
 * user_mutex_sem_release() to either wake a waiter or update the count.
 * Always returns B_OK after the entry is successfully reached; address or
 * context errors are reported before the release.
 *
 * @param sem   User address of the semaphore word.
 * @param flags Caller flags.
 * @return B_OK on success; B_BAD_ADDRESS / context errors otherwise.
 */
status_t
_user_mutex_sem_release(int32* sem, uint32 flags)
{
	if (sem == NULL || !IS_USER_ADDRESS(sem) || (addr_t)sem % 4 != 0)
		return B_BAD_ADDRESS;

	UserMutexContextFetcher contextFetcher(sem, flags);
	if (contextFetcher.InitCheck() != B_OK)
		return contextFetcher.InitCheck();
	struct user_mutex_context* context = contextFetcher.Context();

	UserMutexEntry* entry = get_user_mutex_entry(context,
		contextFetcher.Address());
	{
		user_mutex_sem_release(entry, sem, contextFetcher.IsWired());
	}
	put_user_mutex_entry(context, entry);

	return B_OK;
}
