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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2007-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2019-2023, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file condition_variable.cpp
 * @brief Kernel condition variable implementation for thread synchronization.
 *
 * Provides ConditionVariable and ConditionVariableEntry used to block threads
 * until a condition is met. Threads wait on a ConditionVariable and are woken
 * when another thread calls NotifyOne() or NotifyAll().
 *
 * @see ConditionVariable, ConditionVariableEntry
 */

#include <condition_variable.h>

#include <new>
#include <stdlib.h>
#include <string.h>

#include <debug.h>
#include <kscheduler.h>
#include <ksignal.h>
#include <interrupts.h>
#include <listeners.h>
#include <scheduling_analysis.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <util/atomic.h>


#define STATUS_ADDED	1
#define STATUS_WAITING	2


static const int kConditionVariableHashSize = 512;


struct ConditionVariableHashDefinition {
	typedef const void* KeyType;
	typedef	ConditionVariable ValueType;

	size_t HashKey(const void* key) const
		{ return (size_t)key; }
	size_t Hash(ConditionVariable* variable) const
		{ return (size_t)variable->fObject; }
	bool Compare(const void* key, ConditionVariable* variable) const
		{ return key == variable->fObject; }
	ConditionVariable*& GetLink(ConditionVariable* variable) const
		{ return variable->fNext; }
};

typedef BOpenHashTable<ConditionVariableHashDefinition> ConditionVariableHash;
static ConditionVariableHash sConditionVariableHash;
static rw_spinlock sConditionVariableHashLock;


// #pragma mark - ConditionVariableEntry


/**
 * @brief Default constructor; initialises the entry with no associated variable.
 */
ConditionVariableEntry::ConditionVariableEntry()
	: fVariable(NULL)
{
}


/**
 * @brief Destructor; removes the entry from its variable if still attached.
 */
ConditionVariableEntry::~ConditionVariableEntry()
{
	// We can use an "unsafe" non-atomic access of fVariable here, since we only
	// care whether it is non-NULL, not what its specific value is.
	if (fVariable != NULL)
		_RemoveFromVariable();
}


/**
 * @brief Looks up the condition variable for @p object and adds this entry to it.
 *
 * Acquires the hash read-lock, locates the published ConditionVariable keyed
 * by @p object, and atomically links this entry into that variable's wait list.
 *
 * @param object  Non-NULL kernel object pointer used as the variable key.
 * @return @c true if the variable was found and the entry was added;
 *         @c false if no variable is published for @p object.
 */
bool
ConditionVariableEntry::Add(const void* object)
{
	ASSERT(object != NULL);

	InterruptsLocker _;
	ReadSpinLocker hashLocker(sConditionVariableHashLock);

	ConditionVariable* variable = sConditionVariableHash.Lookup(object);

	if (variable == NULL) {
		fWaitStatus = B_ENTRY_NOT_FOUND;
		return false;
	}

	SpinLocker variableLocker(variable->fLock);
	hashLocker.Unlock();

	_AddToLockedVariable(variable);

	return true;
}


/**
 * @brief Returns a pointer to the ConditionVariable this entry is waiting on.
 *
 * @return Pointer to the current ConditionVariable, or @c NULL if the entry
 *         is not attached to any variable.
 */
ConditionVariable*
ConditionVariableEntry::Variable() const
{
	return atomic_pointer_get(&fVariable);
}


/**
 * @brief Attaches this entry to @p variable, which must already be locked.
 *
 * Records the current thread, sets the wait status to STATUS_ADDED, appends
 * the entry to the variable's list, and increments the entries reference count.
 * Must be called with @p variable->fLock held and interrupts disabled.
 *
 * @param variable  The already-locked ConditionVariable to attach to.
 */
inline void
ConditionVariableEntry::_AddToLockedVariable(ConditionVariable* variable)
{
	ASSERT(fVariable == NULL);

	fThread = thread_get_current_thread();
	fVariable = variable;
	fWaitStatus = STATUS_ADDED;
	fVariable->fEntries.Add(this);
	atomic_add(&fVariable->fEntriesCount, 1);
}


/**
 * @brief Removes this entry from its associated ConditionVariable.
 *
 * Handles the race between a waiting thread removing itself and the variable's
 * notify path clearing the entry. After this call returns, fVariable is NULL
 * and the variable's fEntriesCount has been decremented exactly once.
 */
void
ConditionVariableEntry::_RemoveFromVariable()
{
	// This section is critical because it can race with _NotifyLocked on the
	// variable's thread, so we must not be interrupted during it.
	InterruptsLocker _;

	ConditionVariable* variable = atomic_pointer_get(&fVariable);
	if (atomic_pointer_get_and_set(&fThread, (Thread*)NULL) == NULL) {
		// If fThread was already NULL, that means the variable is already
		// in the process of clearing us out (or already has finished doing so.)
		// We thus cannot access fVariable, and must spin until it is cleared.
		int32 tries = 0;
		while (atomic_pointer_get(&fVariable) != NULL) {
			tries++;
			if ((tries % 10000) == 0)
				dprintf("variable pointer was not unset for a long time!\n");
			cpu_pause();
		}

		return;
	}

	while (true) {
		if (atomic_pointer_get(&fVariable) == NULL) {
			// The variable must have cleared us out. Acknowledge this and return.
			atomic_add(&variable->fEntriesCount, -1);
			return;
		}

		// There is of course a small race between checking the pointer and then
		// the try_acquire in which the variable might clear out our fVariable.
		// However, in the case where we were the ones to clear fThread, the
		// variable will notice that and then wait for us to acknowledge the
		// removal by decrementing fEntriesCount, as we do above; and until
		// we do that, we may validly use our cached pointer to the variable.
		if (try_acquire_spinlock(&variable->fLock))
			break;

		cpu_pause();
	}

	// We now hold the variable's lock. Remove ourselves.
	if (fVariable->fEntries.Contains(this))
		fVariable->fEntries.Remove(this);

	atomic_pointer_set(&fVariable, (ConditionVariable*)NULL);
	atomic_add(&variable->fEntriesCount, -1);
	release_spinlock(&variable->fLock);
}


/**
 * @brief Blocks the calling thread until the condition is signalled or the
 *        operation times out.
 *
 * The thread must already have been added to a ConditionVariable via Add()
 * before calling this overload. If @p flags includes B_RELATIVE_TIMEOUT and
 * @p timeout is non-positive, the call returns immediately with B_WOULD_BLOCK.
 *
 * @param flags    Wait flags (e.g. B_RELATIVE_TIMEOUT, B_ABSOLUTE_TIMEOUT,
 *                 B_CAN_INTERRUPT).
 * @param timeout  Timeout in microseconds; ignored unless a timeout flag is set.
 * @return The status code delivered by the notifying thread, or a timeout /
 *         interrupt error from the scheduler.
 * @retval B_OK             Woken normally by a notify call.
 * @retval B_WOULD_BLOCK    Zero-or-negative relative timeout was requested.
 * @retval B_TIMED_OUT      The timeout elapsed before the variable was signalled.
 * @retval B_INTERRUPTED    The wait was interrupted by a signal.
 */
status_t
ConditionVariableEntry::Wait(uint32 flags, bigtime_t timeout)
{
#if KDEBUG
	if (!are_interrupts_enabled()) {
		panic("ConditionVariableEntry::Wait() called with interrupts "
			"disabled, entry: %p, variable: %p", this, fVariable);
		return B_ERROR;
	}
#endif

	ConditionVariable* variable = atomic_pointer_get(&fVariable);
	if (variable == NULL)
		return fWaitStatus;

	if ((flags & B_RELATIVE_TIMEOUT) != 0 && timeout <= 0) {
		_RemoveFromVariable();

		if (fWaitStatus <= 0)
			return fWaitStatus;
		return B_WOULD_BLOCK;
	}

	InterruptsLocker _;
	SpinLocker schedulerLocker(thread_get_current_thread()->scheduler_lock);

	if (fWaitStatus <= 0)
		return fWaitStatus;
	fWaitStatus = STATUS_WAITING;

	thread_prepare_to_block(thread_get_current_thread(), flags,
		THREAD_BLOCK_TYPE_CONDITION_VARIABLE, variable);

	schedulerLocker.Unlock();

	status_t error;
	if ((flags & (B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT)) != 0)
		error = thread_block_with_timeout(flags, timeout);
	else
		error = thread_block();

	_RemoveFromVariable();

	// We need to always return the actual wait status, if we received one.
	if (fWaitStatus <= 0)
		return fWaitStatus;

	return error;
}


/**
 * @brief Convenience overload: adds the entry to the variable for @p object,
 *        then immediately waits.
 *
 * Equivalent to calling Add(@p object) followed by Wait(@p flags, @p timeout).
 * Returns B_ENTRY_NOT_FOUND immediately if no variable is published for
 * @p object.
 *
 * @param object   Kernel object whose published condition variable to wait on.
 * @param flags    Wait flags forwarded to Wait(uint32, bigtime_t).
 * @param timeout  Timeout in microseconds forwarded to Wait(uint32, bigtime_t).
 * @return Status code from Wait(), or B_ENTRY_NOT_FOUND.
 * @retval B_ENTRY_NOT_FOUND  No condition variable is published for @p object.
 */
status_t
ConditionVariableEntry::Wait(const void* object, uint32 flags,
	bigtime_t timeout)
{
	if (Add(object))
		return Wait(flags, timeout);
	return B_ENTRY_NOT_FOUND;
}


// #pragma mark - ConditionVariable


/**
 * @brief Initialises the condition variable fields without publishing it.
 *
 * Sets up the object pointer, type string, entry list, entries count, and
 * spinlock. Also fires scheduling-analysis and wait-object listener hooks.
 *
 * @param object      Kernel object this variable is associated with.
 * @param objectType  Human-readable string describing the object type.
 */
void
ConditionVariable::Init(const void* object, const char* objectType)
{
	fObject = object;
	fObjectType = objectType;
	new(&fEntries) EntryList;
	fEntriesCount = 0;
	B_INITIALIZE_SPINLOCK(&fLock);

	T_SCHEDULING_ANALYSIS(InitConditionVariable(this, object, objectType));
	NotifyWaitObjectListeners(&WaitObjectListener::ConditionVariableInitialized,
		this);
}


/**
 * @brief Initialises and registers the condition variable in the global hash.
 *
 * Calls Init() then inserts the variable into @c sConditionVariableHash so
 * that threads can discover it via Add(const void*).
 *
 * @param object      Non-NULL kernel object pointer used as the hash key.
 * @param objectType  Human-readable string describing the object type.
 */
void
ConditionVariable::Publish(const void* object, const char* objectType)
{
	ASSERT(object != NULL);

	Init(object, objectType);

	InterruptsWriteSpinLocker _(sConditionVariableHashLock);

	ASSERT_PRINT(sConditionVariableHash.Lookup(object) == NULL,
		"condition variable: %p\n", sConditionVariableHash.Lookup(object));

	sConditionVariableHash.InsertUnchecked(this);
}


/**
 * @brief Removes the variable from the global hash and notifies any waiters.
 *
 * Acquires both the hash write-lock and the variable's own spinlock, removes
 * the variable from the hash, then wakes all waiting entries with
 * B_ENTRY_NOT_FOUND. After this call the variable must not be used as a
 * publish target again without a fresh Publish() call.
 */
void
ConditionVariable::Unpublish()
{
	ASSERT(fObject != NULL);

	InterruptsLocker _;
	WriteSpinLocker hashLocker(sConditionVariableHashLock);
	SpinLocker selfLocker(fLock);

#if KDEBUG
	ConditionVariable* variable = sConditionVariableHash.Lookup(fObject);
	if (variable != this) {
		panic("Condition variable %p not published, found: %p", this, variable);
		return;
	}
#endif

	sConditionVariableHash.RemoveUnchecked(this);
	fObject = NULL;
	fObjectType = NULL;

	hashLocker.Unlock();

	if (!fEntries.IsEmpty())
		_NotifyLocked(true, B_ENTRY_NOT_FOUND);
}


/**
 * @brief Adds a pre-allocated entry to this condition variable.
 *
 * @param entry  Entry to attach; must not already be attached to a variable.
 */
void
ConditionVariable::Add(ConditionVariableEntry* entry)
{
	InterruptsSpinLocker _(fLock);
	entry->_AddToLockedVariable(this);
}


/**
 * @brief Allocates a temporary entry, adds it, and blocks until signalled.
 *
 * @param flags    Wait flags (e.g. B_CAN_INTERRUPT, B_RELATIVE_TIMEOUT).
 * @param timeout  Timeout in microseconds; used only when a timeout flag is set.
 * @return Status code from ConditionVariableEntry::Wait().
 * @retval B_OK  The variable was signalled successfully.
 */
status_t
ConditionVariable::Wait(uint32 flags, bigtime_t timeout)
{
	ConditionVariableEntry entry;
	Add(&entry);
	return entry.Wait(flags, timeout);
}


/**
 * @brief Releases @p lock, waits on the condition, then re-acquires @p lock.
 *
 * Provides the standard monitor-style wait: the mutex must be held before
 * calling this function; it will be released while waiting and re-acquired
 * before returning.
 *
 * @param lock     Mutex to release during the wait and re-acquire on wakeup.
 * @param flags    Wait flags forwarded to ConditionVariableEntry::Wait().
 * @param timeout  Timeout in microseconds; used only when a timeout flag is set.
 * @return Status code from ConditionVariableEntry::Wait().
 * @retval B_OK  The variable was signalled successfully.
 */
status_t
ConditionVariable::Wait(mutex* lock, uint32 flags, bigtime_t timeout)
{
	ConditionVariableEntry entry;
	Add(&entry);
	mutex_unlock(lock);
	status_t res = entry.Wait(flags, timeout);
	mutex_lock(lock);
	return res;
}


/**
 * @brief Fully unlocks @p lock, waits on the condition, then restores all
 *        recursion levels.
 *
 * Records the current recursion depth, unlocks @p lock completely, waits,
 * then re-locks @p lock the same number of times to restore the depth.
 *
 * @param lock     Recursive lock to release during the wait.
 * @param flags    Wait flags forwarded to ConditionVariableEntry::Wait().
 * @param timeout  Timeout in microseconds; used only when a timeout flag is set.
 * @return Status code from ConditionVariableEntry::Wait().
 * @retval B_OK  The variable was signalled successfully.
 */
status_t
ConditionVariable::Wait(recursive_lock* lock, uint32 flags, bigtime_t timeout)
{
	ConditionVariableEntry entry;
	Add(&entry);
	int32 recursion = recursive_lock_get_recursion(lock);

	for (int32 i = 0; i < recursion; i++)
		recursive_lock_unlock(lock);

	status_t res = entry.Wait(flags, timeout);

	for (int32 i = 0; i < recursion; i++)
		recursive_lock_lock(lock);

	return res;
}


/**
 * @brief Static helper: wakes the first thread waiting on the variable
 *        published for @p object.
 *
 * @param object  Kernel object whose published condition variable to signal.
 * @param result  Status code delivered to the woken thread.
 * @return The number of threads woken (0 or 1).
 */
/*static*/ int32
ConditionVariable::NotifyOne(const void* object, status_t result)
{
	return _Notify(object, false, result);
}


/**
 * @brief Static helper: wakes all threads waiting on the variable published
 *        for @p object.
 *
 * @param object  Kernel object whose published condition variable to signal.
 * @param result  Status code delivered to each woken thread.
 * @return The number of threads woken.
 */
/*static*/ int32
ConditionVariable::NotifyAll(const void* object, status_t result)
{
	return _Notify(object, true, result);
}


/**
 * @brief Internal static notify dispatcher: looks up the variable for @p object
 *        and delegates to _NotifyLocked().
 *
 * @param object  Kernel object key used to find the variable in the hash.
 * @param all     If @c true, wake all waiters; otherwise wake only one.
 * @param result  Status code delivered to woken threads.
 * @return The number of threads woken, or 0 if no variable was found.
 */
/*static*/ int32
ConditionVariable::_Notify(const void* object, bool all, status_t result)
{
	InterruptsLocker ints;
	ReadSpinLocker hashLocker(sConditionVariableHashLock);
	ConditionVariable* variable = sConditionVariableHash.Lookup(object);
	if (variable == NULL)
		return 0;
	SpinLocker variableLocker(variable->fLock);
	hashLocker.Unlock();

	return variable->_NotifyLocked(all, result);
}


/**
 * @brief Non-static instance notify: acquires the variable's own lock and
 *        calls _NotifyLocked().
 *
 * @param all     If @c true, wake all waiting entries; otherwise wake one.
 * @param result  Status code delivered to woken threads; must be <= B_OK.
 * @return The number of threads woken.
 */
int32
ConditionVariable::_Notify(bool all, status_t result)
{
	InterruptsSpinLocker _(fLock);
	if (!fEntries.IsEmpty()) {
		if (result > B_OK) {
			panic("tried to notify with invalid result %" B_PRId32 "\n", result);
			result = B_ERROR;
		}

		return _NotifyLocked(all, result);
	}
	return 0;
}


/*! Called with interrupts disabled and the condition variable's spinlock held.
 */
/**
 * @brief Dequeues and unblocks waiting entries while the variable's lock is held.
 *
 * For each entry dequeued from fEntries, if the waiting thread is still active
 * it is unblocked with @p result. If the thread has already begun removing
 * itself, the function waits for the acknowledgement counter before continuing.
 * Must be called with interrupts disabled and fLock held.
 *
 * @param all     If @c true, process every entry; if @c false, stop after
 *                the first successfully unblocked thread.
 * @param result  Status code to deliver to each woken thread.
 * @return The number of threads actually unblocked.
 */
int32
ConditionVariable::_NotifyLocked(bool all, status_t result)
{
	int32 notified = 0;

	// Dequeue and wake up the blocked threads.
	while (ConditionVariableEntry* entry = fEntries.RemoveHead()) {
		Thread* thread = atomic_pointer_get_and_set(&entry->fThread, (Thread*)NULL);
		if (thread == NULL) {
			// The entry must be in the process of trying to remove itself from us.
			// Clear its variable and wait for it to acknowledge this in fEntriesCount,
			// as it is the one responsible for decrementing that.
			const int32 removedCount = atomic_get(&fEntriesCount) - 1;
			atomic_pointer_set(&entry->fVariable, (ConditionVariable*)NULL);

			// As fEntriesCount is only modified while our lock is held, nothing else
			// will modify it while we are spinning, since we hold it at present.
			int32 tries = 0;
			while (atomic_get(&fEntriesCount) != removedCount) {
				tries++;
				if ((tries % 10000) == 0)
					dprintf("entries count was not decremented for a long time!\n");
				cpu_wait(&fEntriesCount, removedCount);
			}
		} else {
			SpinLocker schedulerLocker(thread->scheduler_lock);
			status_t lastWaitStatus = entry->fWaitStatus;
			entry->fWaitStatus = result;
			if (lastWaitStatus == STATUS_WAITING && thread->state != B_THREAD_WAITING) {
				// The thread is not in B_THREAD_WAITING state, so we must unblock it early,
				// in case it tries to re-block itself immediately after we unset fVariable.
				thread_unblock_locked(thread, result);
				lastWaitStatus = result;
			}

			// No matter what the thread is doing, as we were the ones to clear its
			// fThread, so we are the ones responsible for decrementing fEntriesCount.
			// (We may not validly access the entry once we unset its fVariable.)
			atomic_pointer_set(&entry->fVariable, (ConditionVariable*)NULL);
			atomic_add(&fEntriesCount, -1);

			// If the thread was in B_THREAD_WAITING state, we unblock it after unsetting
			// fVariable, because otherwise it will wake up before thread_unblock returns
			// and spin while waiting for us to do so.
			if (lastWaitStatus == STATUS_WAITING)
				thread_unblock_locked(thread, result);

			notified++;
			if (!all)
				break;
		}
	}

	return notified;
}


// #pragma mark -


/**
 * @brief Prints a summary table of all published condition variables to the
 *        kernel debugger console.
 */
/*static*/ void
ConditionVariable::ListAll()
{
	kprintf("  variable      object (type)                waiting threads\n");
	kprintf("------------------------------------------------------------\n");
	ConditionVariableHash::Iterator it(&sConditionVariableHash);
	while (ConditionVariable* variable = it.Next()) {
		// count waiting threads
		int count = variable->fEntries.Count();

		kprintf("%p  %p  %-20s %15d\n", variable, variable->fObject,
			variable->fObjectType, count);
	}
}


/**
 * @brief Prints detailed state of this condition variable to the kernel debugger.
 *
 * Outputs the variable's address, associated object pointer and type string,
 * and the thread IDs of all currently waiting entries.
 */
void
ConditionVariable::Dump() const
{
	kprintf("condition variable %p\n", this);
	kprintf("  object:  %p (%s)\n", fObject, fObjectType);
	kprintf("  threads:");

	for (EntryList::ConstIterator it = fEntries.GetIterator();
		 ConditionVariableEntry* entry = it.Next();) {
		kprintf(" %" B_PRId32, entry->fThread->id);
	}
	kprintf("\n");
}


/**
 * @brief Safely copies the object-type string of @p cvar into @p name.
 *
 * Uses debug_memcpy() and debug_strlcpy() to tolerate faults when the
 * ConditionVariable structure may be corrupt (e.g. called from the debugger).
 *
 * @param cvar   Pointer to the ConditionVariable to inspect.
 * @param name   Destination buffer for the type string.
 * @param size   Size of the destination buffer in bytes.
 * @return B_OK on success, or an error code if the memory access faulted.
 * @retval B_OK  Type string was copied successfully.
 */
/*static*/ status_t
ConditionVariable::DebugGetType(ConditionVariable* cvar, char* name, size_t size)
{
	// Use debug_memcpy to handle faults in case the structure is corrupt.
	const char* pointer;
	status_t status = debug_memcpy(B_CURRENT_TEAM, &pointer,
		(int8*)cvar + offsetof(ConditionVariable, fObjectType), sizeof(const char*));
	if (status != B_OK)
		return status;

	return debug_strlcpy(B_CURRENT_TEAM, name, pointer, size);
}


/**
 * @brief Kernel debugger command handler: lists all published condition variables.
 *
 * @param argc  Argument count (unused).
 * @param argv  Argument vector (unused).
 * @return Always 0.
 */
static int
list_condition_variables(int argc, char** argv)
{
	ConditionVariable::ListAll();
	return 0;
}


/**
 * @brief Kernel debugger command handler: dumps one condition variable by address.
 *
 * Accepts either the address of a ConditionVariable object directly, or the
 * address of the kernel object it was published for. Sets the debugger variables
 * @c _cvar and @c _object on success.
 *
 * @param argc  Must be 2 (command name + address argument).
 * @param argv  argv[1] is the address expression to parse.
 * @return Always 0.
 */
static int
dump_condition_variable(int argc, char** argv)
{
	if (argc != 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	addr_t address = parse_expression(argv[1]);
	if (address == 0)
		return 0;

	ConditionVariable* variable = sConditionVariableHash.Lookup((void*)address);

	if (variable == NULL) {
		// It must be a direct pointer to a condition variable.
		variable = (ConditionVariable*)address;
	}

	if (variable != NULL) {
		variable->Dump();

		set_debug_variable("_cvar", (addr_t)variable);
		set_debug_variable("_object", (addr_t)variable->Object());
	} else
		kprintf("no condition variable at or with key %p\n", (void*)address);

	return 0;
}


// #pragma mark -


/**
 * @brief Module initialisation: sets up the global condition-variable hash and
 *        registers kernel debugger commands.
 *
 * Initialises the hash table with kConditionVariableHashSize buckets and
 * panics on allocation failure. Registers the @c cvar and @c cvars debugger
 * commands.
 */
void
condition_variable_init()
{
	new(&sConditionVariableHash) ConditionVariableHash;

	status_t error = sConditionVariableHash.Init(kConditionVariableHashSize);
	if (error != B_OK) {
		panic("condition_variable_init(): Failed to init hash table: %s",
			strerror(error));
	}

	add_debugger_command_etc("cvar", &dump_condition_variable,
		"Dump condition variable info",
		"<address>\n"
		"Prints info for the specified condition variable.\n"
		"  <address>  - Address of the condition variable or the object it is\n"
		"               associated with.\n", 0);
	add_debugger_command_etc("cvars", &list_condition_variables,
		"List condition variables",
		"\n"
		"Lists all published condition variables\n", 0);
}
