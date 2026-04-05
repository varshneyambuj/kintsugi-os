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
 *   Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 *   Copyright 2005-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file thread.cpp
 * @brief Kernel thread lifecycle management — creation, scheduling, and termination.
 *
 * Implements thread creation (spawn_kernel_thread, _kern_spawn_thread),
 * thread exit (thread_exit), blocking/unblocking, priority management,
 * thread info queries, and the per-thread kernel/user stack setup.
 * Thread objects are ref-counted and stored in a global hash table.
 *
 * @see team.cpp, scheduler/scheduler.cpp, sem.cpp
 */


#include <thread.h>

#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include <algorithm>

#include <OS.h>

#include <util/AutoLock.h>
#include <util/ThreadAutoLock.h>

#include <arch/debug.h>
#include <boot/kernel_args.h>
#include <condition_variable.h>
#include <cpu.h>
#include <interrupts.h>
#include <kimage.h>
#include <kscheduler.h>
#include <ksignal.h>
#include <Notifications.h>
#include <real_time_clock.h>
#include <slab/Slab.h>
#include <smp.h>
#include <syscalls.h>
#include <syscall_restart.h>
#include <team.h>
#include <tls.h>
#include <user_runtime.h>
#include <user_thread.h>
#include <user_mutex.h>
#include <vfs.h>
#include <vm/vm.h>
#include <vm/VMAddressSpace.h>
#include <wait_for_objects.h>

#include "TeamThreadTables.h"


//#define TRACE_THREAD
#ifdef TRACE_THREAD
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


#define THREAD_MAX_MESSAGE_SIZE		65536


// #pragma mark - ThreadHashTable


typedef BKernel::TeamThreadTable<Thread> ThreadHashTable;


// thread list
static Thread sIdleThreads[SMP_MAX_CPUS];
static ThreadHashTable sThreadHash;
static rw_spinlock sThreadHashLock = B_RW_SPINLOCK_INITIALIZER;
static thread_id sNextThreadID = 2;
	// ID 1 is allocated for the kernel by Team::Team() behind our back

// some arbitrarily chosen limits -- should probably depend on the available
// memory
static int32 sMaxThreads = 4096;
static int32 sUsedThreads = 0;

spinlock gThreadCreationLock = B_SPINLOCK_INITIALIZER;


struct UndertakerEntry : DoublyLinkedListLinkImpl<UndertakerEntry> {
	Thread*	thread;
	team_id	teamID;

	UndertakerEntry(Thread* thread, team_id teamID)
		:
		thread(thread),
		teamID(teamID)
	{
	}
};


struct ThreadEntryArguments {
	status_t	(*kernelFunction)(void* argument);
	void*		argument;
	bool		enterUserland;
};

struct UserThreadEntryArguments : ThreadEntryArguments {
	addr_t			userlandEntry;
	void*			userlandArgument1;
	void*			userlandArgument2;
	pthread_t		pthread;
	arch_fork_arg*	forkArgs;
	uint32			flags;
};


class ThreadNotificationService : public DefaultNotificationService {
public:
	ThreadNotificationService()
		: DefaultNotificationService("threads")
	{
	}

	void Notify(uint32 eventCode, team_id teamID, thread_id threadID,
		Thread* thread = NULL)
	{
		char eventBuffer[180];
		KMessage event;
		event.SetTo(eventBuffer, sizeof(eventBuffer), THREAD_MONITOR);
		event.AddInt32("event", eventCode);
		event.AddInt32("team", teamID);
		event.AddInt32("thread", threadID);
		if (thread != NULL)
			event.AddPointer("threadStruct", thread);

		DefaultNotificationService::Notify(event, eventCode);
	}

	void Notify(uint32 eventCode, Thread* thread)
	{
		return Notify(eventCode, thread->id, thread->team->id, thread);
	}
};


static DoublyLinkedList<UndertakerEntry> sUndertakerEntries;
static spinlock sUndertakerLock = B_SPINLOCK_INITIALIZER;
static ConditionVariable sUndertakerCondition;
static ThreadNotificationService sNotificationService;


// object cache to allocate thread structures from
static object_cache* sThreadCache;


// #pragma mark - kernel stack cache


struct CachedKernelStack : public DoublyLinkedListLinkImpl<CachedKernelStack> {
	VMArea* area;
	addr_t base;
	addr_t top;
};


static spinlock sCachedKernelStacksLock = B_SPINLOCK_INITIALIZER;
static DoublyLinkedList<CachedKernelStack> sCachedKernelStacks;


/**
 * @brief Allocate a kernel stack from the cache, renaming its area.
 *
 * Removes the head entry from the cached kernel stack list and renames
 * its underlying VMArea to @p name.  Panics if the cache is empty.
 *
 * @param name   Name to assign to the kernel stack area.
 * @param base   Receives the base address of the allocated stack.
 * @param top    Receives the top address of the allocated stack.
 * @retval >= 0  area_id of the allocated stack area.
 * @retval B_NO_MEMORY  Cache was empty (also triggers panic).
 */
static area_id
allocate_kernel_stack(const char* name, addr_t* base, addr_t* top)
{
	InterruptsSpinLocker locker(sCachedKernelStacksLock);
	CachedKernelStack* cachedStack = sCachedKernelStacks.RemoveHead();
	locker.Unlock();

	if (cachedStack == NULL) {
		panic("out of cached stacks!");
		return B_NO_MEMORY;
	}

	memcpy(cachedStack->area->name, name, B_OS_NAME_LENGTH);

	*base = cachedStack->base;
	*top = cachedStack->top;

	return cachedStack->area->id;
}


/**
 * @brief Return a kernel stack area to the cache.
 *
 * Places the stack described by @p areaID / @p base / @p top back onto the
 * head of the cached kernel stack list and renames the area to
 * "cached kstack".
 *
 * @param areaID  area_id of the kernel stack area being freed.
 * @param base    Base address of the kernel stack.
 * @param top     Top address of the kernel stack.
 */
static void
free_kernel_stack(area_id areaID, addr_t base, addr_t top)
{
#if defined(STACK_GROWS_DOWNWARDS)
	const addr_t stackStart = base + KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE;
#else
	const addr_t stackStart = base;
#endif

	CachedKernelStack* cachedStack = (CachedKernelStack*)stackStart;
	cachedStack->area = VMAreas::Lookup(areaID);
	cachedStack->base = base;
	cachedStack->top = top;

	strcpy(cachedStack->area->name, "cached kstack");

	InterruptsSpinLocker locker(sCachedKernelStacksLock);
	sCachedKernelStacks.Add(cachedStack);
}


/**
 * @brief Object-cache constructor callback: allocate one kernel stack area.
 *
 * Creates a fully-locked VMArea for a kernel stack (plus guard pages) and
 * immediately hands it to free_kernel_stack() so it enters the cache.
 * Called by the slab allocator when a new slab of Thread objects is prepared.
 *
 * @param cookie  Unused cookie passed by the object cache.
 * @param object  Unused pointer to the object being constructed.
 * @retval B_OK   Stack area was created and cached successfully.
 * @retval < B_OK An area creation error occurred.
 */
static status_t
create_kernel_stack(void* cookie, void* object)
{
	// We could theoretically cast the passed object to Thread* and assign
	// the kstack to it directly, but this would be incompatible with the
	// debug filling of blocks on allocate/free. It also is probably good
	// to not reuse kstacks quite so rapidly, anyway.

	virtual_address_restrictions virtualRestrictions = {};
	virtualRestrictions.address_specification = B_ANY_KERNEL_ADDRESS;
	physical_address_restrictions physicalRestrictions = {};

	addr_t base;
	area_id area = create_area_etc(B_SYSTEM_TEAM, "",
		KERNEL_STACK_SIZE + KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE,
		B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA
			| B_KERNEL_STACK_AREA, 0, KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE,
		&virtualRestrictions, &physicalRestrictions, (void**)&base);
	if (area < 0)
		return area;

	addr_t top = base + KERNEL_STACK_SIZE
		+ KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE;
	free_kernel_stack(area, base, top);
	return B_OK;
}


/**
 * @brief Object-cache destructor callback: delete one cached kernel stack.
 *
 * Removes the head entry from the cached stack list and deletes its
 * underlying VMArea.  Called by the slab allocator when a slab of Thread
 * objects is destroyed.
 *
 * @param cookie  Unused cookie passed by the object cache.
 * @param object  Unused pointer to the object being destroyed.
 */
static void
destroy_kernel_stack(void* cookie, void* object)
{
	InterruptsSpinLocker locker(sCachedKernelStacksLock);
	CachedKernelStack* cachedStack = sCachedKernelStacks.RemoveHead();
	locker.Unlock();

	delete_area(cachedStack->area->id);
}


// #pragma mark - Thread


/*!	Constructs a thread.

	\param name The thread's name.
	\param threadID The ID to be assigned to the new thread. If
		  \code < 0 \endcode a fresh one is allocated.
	\param cpu The CPU the thread shall be assigned.
*/
/**
 * @brief Construct a Thread object and insert it (invisible) into the hash table.
 *
 * Initialises all fields to safe defaults, allocates synchronisation
 * primitives (mutex, spinlocks), copies or generates the thread name, and
 * inserts the new thread into @c sThreadHash with @c visible = false so it
 * is not yet discoverable.
 *
 * @param name      Human-readable name for the thread (may be NULL).
 * @param threadID  Desired thread ID, or -1 to allocate a fresh one.
 * @param cpu       CPU structure the thread is pinned to (may be NULL).
 */
Thread::Thread(const char* name, thread_id threadID, struct cpu_ent* cpu)
	:
	flags(0),
	serial_number(-1),
	hash_next(NULL),
	priority(-1),
	io_priority(-1),
	cpu(cpu),
	previous_cpu(NULL),
	cpumask(),
	pinned_to_cpu(0),
	sig_block_mask(0),
	sigsuspend_original_unblocked_mask(0),
	user_signal_context(NULL),
	signal_stack_base(0),
	signal_stack_size(0),
	signal_stack_enabled(false),
	in_kernel(true),
	has_yielded(false),
	user_thread(NULL),
	fault_handler(0),
	page_faults_allowed(1),
	page_fault_waits_allowed(1),
	team(NULL),
	select_infos(NULL),
	kernel_stack_area(-1),
	kernel_stack_base(0),
	user_stack_area(-1),
	user_stack_base(0),
	user_local_storage(0),
	kernel_errno(0),
	user_time(0),
	kernel_time(0),
	last_time(0),
	cpu_clock_offset(0),
	post_interrupt_callback(NULL),
	post_interrupt_data(NULL)
{
	id = threadID >= 0 ? threadID : allocate_thread_id();
	visible = false;

	// init locks
	char lockName[32];
	snprintf(lockName, sizeof(lockName), "Thread:%" B_PRId32, id);
	mutex_init_etc(&fLock, lockName, MUTEX_FLAG_CLONE_NAME);

	B_INITIALIZE_SPINLOCK(&time_lock);
	B_INITIALIZE_SPINLOCK(&scheduler_lock);
	B_INITIALIZE_RW_SPINLOCK(&team_lock);

	// init name
	if (name != NULL)
		strlcpy(this->name, name, B_OS_NAME_LENGTH);
	else
		strcpy(this->name, "unnamed thread");

	exit.status = 0;

	exit.sem = -1;
	msg.write_sem = -1;
	msg.read_sem = -1;

	// add to thread table -- yet invisible
	InterruptsWriteSpinLocker threadHashLocker(sThreadHashLock);
	sThreadHash.Insert(this);
}


/**
 * @brief Destroy a Thread object, releasing all remaining resources.
 *
 * Deletes the user stack area (if any), user timers, kernel stack, pending
 * signals, inter-thread messaging semaphores, and the exit semaphore.
 * Also calls scheduler_on_thread_destroy() and removes the thread from the
 * global hash table.
 *
 * @note The thread must already have exited (or never been started) before
 *       the destructor runs.
 */
Thread::~Thread()
{
	// Delete resources that should actually be deleted by the thread itself,
	// when it exited, but that might still exist, if the thread was never run.

	if (user_stack_area >= 0)
		delete_area(user_stack_area);

	DeleteUserTimers(false);

	// delete the resources, that may remain in either case

	if (kernel_stack_area >= 0)
		free_kernel_stack(kernel_stack_area, kernel_stack_base, kernel_stack_top);

	fPendingSignals.Clear();

	if (exit.sem >= 0)
		delete_sem(exit.sem);
	if (msg.write_sem >= 0)
		delete_sem(msg.write_sem);
	if (msg.read_sem >= 0)
		delete_sem(msg.read_sem);

	scheduler_on_thread_destroy(this);

	mutex_destroy(&fLock);

	// remove from thread table
	InterruptsWriteSpinLocker threadHashLocker(sThreadHashLock);
	sThreadHash.Remove(this);
}


/**
 * @brief Allocate and fully initialise a new Thread object.
 *
 * Convenience factory that combines construction and Thread::Init() into
 * a single call.  On success the caller receives a reference-counted
 * Thread pointer.
 *
 * @param name     Human-readable name for the new thread.
 * @param _thread  Receives the newly created Thread on success.
 * @retval B_OK       Thread created successfully.
 * @retval B_NO_MEMORY Allocation failed.
 * @retval other      Init() returned an error.
 */
/*static*/ status_t
Thread::Create(const char* name, Thread*& _thread)
{
	Thread* thread = new Thread(name, -1, NULL);
	if (thread == NULL)
		return B_NO_MEMORY;

	status_t error = thread->Init(false);
	if (error != B_OK) {
		delete thread;
		return error;
	}

	_thread = thread;
	return B_OK;
}


/**
 * @brief Look up a thread by ID and return a referenced pointer to it.
 *
 * If @p id refers to the calling thread, AcquireReference() is called on
 * it directly.  Otherwise the global hash table is searched under a read
 * spinlock.
 *
 * @param id  ID of the thread to find.
 * @return    Referenced Thread pointer, or NULL if not found.
 */
/*static*/ Thread*
Thread::Get(thread_id id)
{
	if (id == thread_get_current_thread_id()) {
		Thread* thread = thread_get_current_thread();
		thread->AcquireReference();
		return thread;
	}

	InterruptsReadSpinLocker threadHashLocker(sThreadHashLock);
	Thread* thread = sThreadHash.Lookup(id);
	if (thread != NULL)
		thread->AcquireReference();
	return thread;
}


/**
 * @brief Look up a thread by ID, acquire a reference, and lock it.
 *
 * Combines Thread::Get() with locking, re-checking that the thread is still
 * present in the hash table after the lock is acquired (to avoid a TOCTOU
 * race).  Returns NULL if the thread has been removed between the lookup and
 * the lock.
 *
 * @param id  ID of the thread to find and lock.
 * @return    Referenced and locked Thread pointer, or NULL if not found.
 */
/*static*/ Thread*
Thread::GetAndLock(thread_id id)
{
	if (id == thread_get_current_thread_id()) {
		Thread* thread = thread_get_current_thread();
		thread->AcquireReference();
		thread->Lock();
		return thread;
	}

	// look it up and acquire a reference
	InterruptsReadSpinLocker threadHashLocker(sThreadHashLock);
	Thread* thread = sThreadHash.Lookup(id);
	if (thread == NULL)
		return NULL;

	thread->AcquireReference();
	threadHashLocker.Unlock();

	// lock and check, if it is still in the hash table
	thread->Lock();
	threadHashLocker.Lock();

	if (sThreadHash.Lookup(id) == thread)
		return thread;

	threadHashLocker.Unlock();

	// nope, the thread is no longer in the hash table
	thread->UnlockAndReleaseReference();

	return NULL;
}


/**
 * @brief Look up a thread by ID without acquiring a reference (debug only).
 *
 * Intended exclusively for use inside the kernel debugger where normal
 * locking and reference counting are not available.
 *
 * @param id  ID of the thread to find.
 * @return    Raw Thread pointer (unreferenced), or NULL if not found.
 * @note Must only be called from the kernel debugger.
 */
/*static*/ Thread*
Thread::GetDebug(thread_id id)
{
	return sThreadHash.Lookup(id, false);
}


/**
 * @brief Check whether a thread with the given ID currently exists.
 *
 * Performs a hash-table lookup under a read spinlock.
 *
 * @param id  Thread ID to check.
 * @return    true if the thread is in the hash table, false otherwise.
 */
/*static*/ bool
Thread::IsAlive(thread_id id)
{
	InterruptsReadSpinLocker threadHashLocker(sThreadHashLock);
	return sThreadHash.Lookup(id) != NULL;
}


/**
 * @brief Allocate a Thread object from the slab cache.
 *
 * @param size  Size requested by the C++ runtime (ignored; slab controls size).
 * @return      Pointer to raw memory for the Thread object.
 */
void*
Thread::operator new(size_t size)
{
	return object_cache_alloc(sThreadCache, 0);
}


/**
 * @brief Placement new — return the supplied pointer unchanged.
 *
 * Used when constructing idle threads into pre-allocated storage
 * (e.g. @c sIdleThreads[]).
 *
 * @param size     Size of the object (unused).
 * @param pointer  Pre-allocated memory to construct the object into.
 * @return         @p pointer unchanged.
 */
void*
Thread::operator new(size_t, void* pointer)
{
	return pointer;
}


/**
 * @brief Return a Thread object to the slab cache.
 *
 * @param pointer  Pointer to the Thread object being freed.
 * @param size     Size of the object (unused by the slab allocator).
 */
void
Thread::operator delete(void* pointer, size_t size)
{
	object_cache_free(sThreadCache, pointer, 0);
}


/**
 * @brief Finish initialisation of a newly constructed Thread.
 *
 * Creates the exit semaphore, message write/read semaphores, and calls
 * arch_thread_init_thread_struct() and scheduler_on_thread_create().
 * Must be called once, right after construction, before the thread is
 * inserted into any team or made runnable.
 *
 * @param idleThread  Pass true when initialising an idle thread to suppress
 *                    scheduler bookkeeping that is inappropriate for idles.
 * @retval B_OK    Initialisation succeeded.
 * @retval < B_OK  A semaphore or architecture init call failed.
 */
status_t
Thread::Init(bool idleThread)
{
	status_t error = scheduler_on_thread_create(this, idleThread);
	if (error != B_OK)
		return error;

	char temp[64];
	snprintf(temp, sizeof(temp), "thread_%" B_PRId32 "_retcode_sem", id);
	exit.sem = create_sem(0, temp);
	if (exit.sem < 0)
		return exit.sem;

	snprintf(temp, sizeof(temp), "%s send", name);
	msg.write_sem = create_sem(1, temp);
	if (msg.write_sem < 0)
		return msg.write_sem;

	snprintf(temp, sizeof(temp), "%s receive", name);
	msg.read_sem = create_sem(0, temp);
	if (msg.read_sem < 0)
		return msg.read_sem;

	error = arch_thread_init_thread_struct(this);
	if (error != B_OK)
		return error;

	return B_OK;
}


/*!	Checks whether the thread is still in the thread hash table.
*/
/**
 * @brief Check whether this thread instance is still live (instance method).
 *
 * Searches the hash table for a Thread whose ID matches @c this->id.
 *
 * @return true if the thread is still registered in the hash table.
 */
bool
Thread::IsAlive() const
{
	InterruptsReadSpinLocker threadHashLocker(sThreadHashLock);

	return sThreadHash.Lookup(id) != NULL;
}


/**
 * @brief Reset signal-related state after an exec().
 *
 * Clears the alternate signal stack and the sigsuspend mask override.
 * The pending signal set and signal block mask are intentionally preserved
 * across exec() as required by POSIX.
 */
void
Thread::ResetSignalsOnExec()
{
	// We are supposed keep the pending signals and the signal mask. Only the
	// signal stack, if set, shall be unset.

	sigsuspend_original_unblocked_mask = 0;
	user_signal_context = NULL;
	signal_stack_base = 0;
	signal_stack_size = 0;
	signal_stack_enabled = false;
}


/*!	Adds the given user timer to the thread and, if user-defined, assigns it an
	ID.

	The caller must hold the thread's lock.

	\param timer The timer to be added. If it doesn't have an ID yet, it is
		considered user-defined and will be assigned an ID.
	\return \c B_OK, if the timer was added successfully, another error code
		otherwise.
*/
/**
 * @brief Add a user timer to this thread, enforcing the per-team limit.
 *
 * If the timer has no ID it is treated as user-defined and the team's
 * user-defined timer count is checked and incremented.
 *
 * @param timer  Timer to add.  If @c timer->ID() < 0 it will be assigned
 *               a new user-defined ID.
 * @retval B_OK   Timer added successfully.
 * @retval EAGAIN The per-team user-defined timer limit was reached.
 * @note The caller must hold the thread's lock.
 */
status_t
Thread::AddUserTimer(UserTimer* timer)
{
	// If the timer is user-defined, check timer limit and increment
	// user-defined count.
	if (timer->ID() < 0 && !team->CheckAddUserDefinedTimer())
		return EAGAIN;

	fUserTimers.AddTimer(timer);

	return B_OK;
}


/*!	Removes the given user timer from the thread.

	The caller must hold the thread's lock.

	\param timer The timer to be removed.

*/
/**
 * @brief Remove a user timer from this thread.
 *
 * Removes the timer from the internal list and, if it is user-defined
 * (ID >= USER_TIMER_FIRST_USER_DEFINED_ID), decrements the team's
 * user-defined timer count.
 *
 * @param timer  Timer to remove.
 * @note The caller must hold the thread's lock.
 */
void
Thread::RemoveUserTimer(UserTimer* timer)
{
	fUserTimers.RemoveTimer(timer);

	if (timer->ID() >= USER_TIMER_FIRST_USER_DEFINED_ID)
		team->UserDefinedTimersRemoved(1);
}


/*!	Deletes all (or all user-defined) user timers of the thread.

	The caller must hold the thread's lock.

	\param userDefinedOnly If \c true, only the user-defined timers are deleted,
		otherwise all timers are deleted.
*/
/**
 * @brief Delete all (or all user-defined) user timers of the thread.
 *
 * @param userDefinedOnly  If true, only timers with IDs >=
 *                         USER_TIMER_FIRST_USER_DEFINED_ID are removed;
 *                         otherwise all timers are removed.
 * @note The caller must hold the thread's lock.
 */
void
Thread::DeleteUserTimers(bool userDefinedOnly)
{
	int32 count = fUserTimers.DeleteTimers(userDefinedOnly);
	if (count > 0)
		team->UserDefinedTimersRemoved(count);
}


/**
 * @brief Deactivate all CPU-time user timers associated with this thread.
 *
 * Iterates the fCPUTimeUserTimers list and calls Deactivate() on each
 * entry, stopping any running CPU-time interval timers.
 */
void
Thread::DeactivateCPUTimeUserTimers()
{
	while (ThreadTimeUserTimer* timer = fCPUTimeUserTimers.Head())
		timer->Deactivate();
}


// #pragma mark - ThreadListIterator


/**
 * @brief Construct a ThreadListIterator and register it with the hash table.
 *
 * Inserts an iterator entry into the global thread hash table so that
 * concurrent structural modifications can be handled safely.
 */
ThreadListIterator::ThreadListIterator()
{
	// queue the entry
	InterruptsWriteSpinLocker locker(sThreadHashLock);
	sThreadHash.InsertIteratorEntry(&fEntry);
}


/**
 * @brief Destroy the ThreadListIterator and unregister it from the hash table.
 */
ThreadListIterator::~ThreadListIterator()
{
	// remove the entry
	InterruptsWriteSpinLocker locker(sThreadHashLock);
	sThreadHash.RemoveIteratorEntry(&fEntry);
}


/**
 * @brief Advance to and return the next thread in the global list.
 *
 * Acquires a reference on the returned Thread.  Returns NULL when the
 * iteration is exhausted.
 *
 * @return Referenced pointer to the next Thread, or NULL if done.
 */
Thread*
ThreadListIterator::Next()
{
	// get the next team -- if there is one, get reference for it
	InterruptsWriteSpinLocker locker(sThreadHashLock);
	Thread* thread = sThreadHash.NextElement(&fEntry);
	if (thread != NULL)
		thread->AcquireReference();

	return thread;
}


// #pragma mark - ThreadCreationAttributes


/**
 * @brief Initialise ThreadCreationAttributes for a kernel thread.
 *
 * Sets sensible defaults for all fields and configures the kernel entry
 * point and argument.  The @p team parameter defaults to the kernel team
 * when < 0.
 *
 * @param function  Kernel entry function for the thread.
 * @param name      Human-readable thread name.
 * @param priority  Scheduling priority.
 * @param arg       Argument passed to @p function.
 * @param team      Team ID that will own the thread (< 0 for kernel team).
 * @param thread    Optional pre-existing Thread object to reuse (may be NULL).
 */
ThreadCreationAttributes::ThreadCreationAttributes(thread_func function,
	const char* name, int32 priority, void* arg, team_id team,
	Thread* thread)
{
	this->entry = NULL;
	this->name = name;
	this->priority = priority;
	this->args1 = NULL;
	this->args2 = NULL;
	this->stack_address = NULL;
	this->stack_size = 0;
	this->guard_size = 0;
	this->pthread = NULL;
	this->flags = 0;
	this->team = team >= 0 ? team : team_get_kernel_team()->id;
	this->thread = thread;
	this->signal_mask = 0;
	this->additional_stack_size = 0;
	this->kernelEntry = function;
	this->kernelArgument = arg;
	this->forkArgs = NULL;
}


/*!	Initializes the structure from a userland structure.
	\param userAttributes The userland structure (must be a userland address).
	\param nameBuffer A character array of at least size B_OS_NAME_LENGTH,
		which will be used for the \c name field, if the userland structure has
		a name. The buffer must remain valid as long as this structure is in
		use afterwards (or until it is reinitialized).
	\return \c B_OK, if the initialization went fine, another error code
		otherwise.
*/
/**
 * @brief Populate ThreadCreationAttributes from a userland thread_creation_attributes struct.
 *
 * Copies the userland structure, validates all pointer fields, copies the
 * thread name into @p nameBuffer, and fills in kernel-only fields (team,
 * signal mask, fork args) from the current thread's context.
 *
 * @param userAttributes  Pointer to the userland structure (must be a
 *                        valid user-space address).
 * @param nameBuffer      Caller-supplied buffer of at least B_OS_NAME_LENGTH
 *                        bytes used to hold the copied name string.
 * @retval B_OK          Structure initialised successfully.
 * @retval B_BAD_ADDRESS A pointer field was NULL or pointed outside
 *                       user-space.
 * @retval B_BAD_VALUE   stack_size was out of the allowed range.
 */
status_t
ThreadCreationAttributes::InitFromUserAttributes(
	const thread_creation_attributes* userAttributes, char* nameBuffer)
{
	if (userAttributes == NULL || !IS_USER_ADDRESS(userAttributes)
		|| user_memcpy((thread_creation_attributes*)this, userAttributes,
				sizeof(thread_creation_attributes)) != B_OK) {
		return B_BAD_ADDRESS;
	}

	if (stack_size != 0
		&& (stack_size < MIN_USER_STACK_SIZE
			|| stack_size > MAX_USER_STACK_SIZE)) {
		return B_BAD_VALUE;
	}

	if (entry == NULL || !IS_USER_ADDRESS(entry)
		|| (stack_address != NULL && !IS_USER_ADDRESS(stack_address))
		|| (name != NULL && (!IS_USER_ADDRESS(name)
			|| user_strlcpy(nameBuffer, name, B_OS_NAME_LENGTH) < 0))) {
		return B_BAD_ADDRESS;
	}

	name = name != NULL ? nameBuffer : "user thread";

	// kernel only attributes (not in thread_creation_attributes):
	Thread* currentThread = thread_get_current_thread();
	team = currentThread->team->id;
	thread = NULL;
	signal_mask = currentThread->sig_block_mask;
		// inherit the current thread's signal mask
	additional_stack_size = 0;
	kernelEntry = NULL;
	kernelArgument = NULL;
	forkArgs = NULL;

	return B_OK;
}


// #pragma mark - private functions


/*!	Inserts a thread into a team.
	The caller must hold the team's lock, the thread's lock, and the scheduler
	lock.
*/
/**
 * @brief Insert a thread into a team's thread list.
 *
 * Appends @p thread to @p team's thread_list, increments the thread count,
 * and, if this is the first thread, sets it as the main thread.
 *
 * @param team    Team to insert the thread into.
 * @param thread  Thread to insert.
 * @note Caller must hold the team lock, the thread lock, and the scheduler lock.
 */
static void
insert_thread_into_team(Team *team, Thread *thread)
{
	team->thread_list.Add(thread, false);
	team->num_threads++;

	if (team->num_threads == 1) {
		// this was the first thread
		team->main_thread = thread;
	}
	thread->team = team;
}


/*!	Removes a thread from a team.
	The caller must hold the team's lock, the thread's lock, and the scheduler
	lock.
*/
/**
 * @brief Remove a thread from a team's thread list.
 *
 * Removes @p thread from @p team's thread_list and decrements the thread count.
 *
 * @param team    Team to remove the thread from.
 * @param thread  Thread to remove.
 * @note Caller must hold the team lock, the thread lock, and the scheduler lock.
 */
static void
remove_thread_from_team(Team *team, Thread *thread)
{
	team->thread_list.Remove(thread);
	team->num_threads--;
}


/**
 * @brief Transition a new user thread into userland for the first time.
 *
 * Initialises TLS, the user_thread struct, and default TLS slots, then
 * either restores a fork frame (for fork()ed threads) or calls
 * arch_thread_enter_userspace() to jump to the userland entry point.
 * Only returns on error.
 *
 * @param thread  The thread entering userland (must be the current thread).
 * @param args    Entry arguments including the userland entry address,
 *                arguments, pthread descriptor, and optional fork args.
 * @retval B_OK          Never returned on the normal (fork restore) path.
 * @retval B_BAD_ADDRESS A TLS copy to userland failed.
 * @retval other         arch_thread_enter_userspace() error.
 */
static status_t
enter_userspace(Thread* thread, UserThreadEntryArguments* args)
{
	status_t error = arch_thread_init_tls(thread);
	if (error != B_OK) {
		dprintf("Failed to init TLS for new userland thread \"%s\" (%" B_PRId32
			")\n", thread->name, thread->id);
		free(args->forkArgs);
		return error;
	}

	user_debug_update_new_thread_flags(thread);

	// init the thread's user_thread
	user_thread* userThread = thread->user_thread;
	arch_cpu_enable_user_access();
	userThread->pthread = args->pthread;
	userThread->flags = 0;
	userThread->wait_status = B_OK;
	userThread->defer_signals
		= (args->flags & THREAD_CREATION_FLAG_DEFER_SIGNALS) != 0 ? 1 : 0;
	userThread->pending_signals = 0;
	arch_cpu_disable_user_access();

	// initialize default TLS fields
	addr_t tls[TLS_FIRST_FREE_SLOT];
	memset(tls, 0, sizeof(tls));
	tls[TLS_BASE_ADDRESS_SLOT] = thread->user_local_storage;
	tls[TLS_THREAD_ID_SLOT] = thread->id;
	tls[TLS_USER_THREAD_SLOT] = (addr_t)thread->user_thread;

	if (args->forkArgs == NULL) {
		if (user_memcpy((void*)thread->user_local_storage, tls, sizeof(tls)) != B_OK)
			return B_BAD_ADDRESS;
	} else {
		// This is a fork()ed thread.

		// Update select TLS values, do not clear the whole array.
		arch_cpu_enable_user_access();
		addr_t* userTls = (addr_t*)thread->user_local_storage;
		ASSERT(userTls[TLS_BASE_ADDRESS_SLOT] == thread->user_local_storage);
		userTls[TLS_THREAD_ID_SLOT] = tls[TLS_THREAD_ID_SLOT];
		userTls[TLS_USER_THREAD_SLOT] = tls[TLS_USER_THREAD_SLOT];
		arch_cpu_disable_user_access();

		// Copy the fork args onto the stack and free them.
		arch_fork_arg archArgs = *args->forkArgs;
		free(args->forkArgs);

		arch_restore_fork_frame(&archArgs);
			// this one won't return here
		return B_ERROR;
	}

	// Jump to the entry point in user space. Only returns, if something fails.
	return arch_thread_enter_userspace(thread, args->userlandEntry,
		args->userlandArgument1, args->userlandArgument2);
}


/**
 * @brief Enter userland on behalf of the current thread as part of a new team.
 *
 * Called from team creation paths to transition the first (main) thread of a
 * newly exec()'ed or spawned team into user space.  Constructs a minimal
 * UserThreadEntryArguments and delegates to enter_userspace().
 *
 * @param thread         The current kernel thread that will become the
 *                       new team's main userland thread.
 * @param entryFunction  Userland entry point address.
 * @param argument1      First argument for the userland entry function.
 * @param argument2      Second argument for the userland entry function.
 * @return               Result of enter_userspace() — only returns on error.
 */
status_t
thread_enter_userspace_new_team(Thread* thread, addr_t entryFunction,
	void* argument1, void* argument2)
{
	UserThreadEntryArguments entryArgs;
	entryArgs.kernelFunction = NULL;
	entryArgs.argument = NULL;
	entryArgs.enterUserland = true;
	entryArgs.userlandEntry = (addr_t)entryFunction;
	entryArgs.userlandArgument1 = argument1;
	entryArgs.userlandArgument2 = argument2;
	entryArgs.pthread = NULL;
	entryArgs.forkArgs = NULL;
	entryArgs.flags = 0;

	return enter_userspace(thread, &entryArgs);
}


/**
 * @brief Universal thread entry trampoline executed on every new thread's first schedule.
 *
 * Called by the architecture-specific context-switch code when a thread
 * runs for the very first time.  Releases the scheduler lock, enables
 * interrupts, calls the optional kernel entry function, optionally enters
 * userland, and finally calls thread_exit().
 *
 * @param _args  Pointer to a ThreadEntryArguments (or UserThreadEntryArguments)
 *               that was copied onto the thread's kernel stack by
 *               init_thread_kernel_stack().
 */
static void
common_thread_entry(void* _args)
{
	Thread* thread = thread_get_current_thread();

	// The thread is new and has been scheduled the first time.

	scheduler_new_thread_entry(thread);

	// unlock the scheduler lock and enable interrupts
	release_spinlock(&thread->scheduler_lock);
	enable_interrupts();

	// call the kernel function, if any
	ThreadEntryArguments* args = (ThreadEntryArguments*)_args;
	if (args->kernelFunction != NULL)
		args->kernelFunction(args->argument);

	// If requested, enter userland, now.
	if (args->enterUserland) {
		enter_userspace(thread, (UserThreadEntryArguments*)args);
			// only returns or error

		// If that's the team's main thread, init the team exit info.
		if (thread == thread->team->main_thread)
			team_init_exit_info_on_error(thread->team);
	}

	// we're done
	thread_exit();
}


/*!	Prepares the given thread's kernel stack for executing its entry function.

	The data pointed to by \a data of size \a dataSize are copied to the
	thread's kernel stack. A pointer to the copy's data is passed to the entry
	function. The entry function is common_thread_entry().

	\param thread The thread.
	\param data Pointer to data to be copied to the thread's stack and passed
		to the entry function.
	\param dataSize The size of \a data.
 */
/**
 * @brief Copy entry arguments onto a thread's kernel stack and set up the initial frame.
 *
 * Copies @p dataSize bytes from @p data onto the thread's kernel stack
 * (with 16-byte alignment) and calls arch_thread_init_kthread_stack() so
 * that the thread will start executing common_thread_entry() with a pointer
 * to the copied data.
 *
 * @param thread    Thread whose kernel stack is being prepared.
 * @param data      Entry argument block to copy (ThreadEntryArguments or
 *                  UserThreadEntryArguments).
 * @param dataSize  Size of the entry argument block in bytes.
 */
static void
init_thread_kernel_stack(Thread* thread, const void* data, size_t dataSize)
{
	uint8* stack = (uint8*)thread->kernel_stack_base;
	uint8* stackTop = (uint8*)thread->kernel_stack_top;

	// clear (or rather invalidate) the kernel stack contents, if compiled with
	// debugging
#if KDEBUG > 0
#	if defined(DEBUG_KERNEL_STACKS) && defined(STACK_GROWS_DOWNWARDS)
	memset((void*)(stack + KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE), 0xcc,
		KERNEL_STACK_SIZE);
#	else
	memset(stack, 0xcc, KERNEL_STACK_SIZE);
#	endif
#endif

	// copy the data onto the stack, with 16-byte alignment to be on the safe
	// side
	void* clonedData;
#ifdef STACK_GROWS_DOWNWARDS
	clonedData = (void*)ROUNDDOWN((addr_t)stackTop - dataSize, 16);
	stackTop = (uint8*)clonedData;
#else
	clonedData = (void*)ROUNDUP((addr_t)stack, 16);
	stack = (uint8*)clonedData + ROUNDUP(dataSize, 16);
#endif

	memcpy(clonedData, data, dataSize);

	arch_thread_init_kthread_stack(thread, stack, stackTop,
		&common_thread_entry, clonedData);
}


/**
 * @brief Create and map a user-space stack area for a thread.
 *
 * If @p _stackBase is non-NULL it must point to an already-mapped region of
 * at least MIN_USER_STACK_SIZE bytes; TLS_SIZE is subtracted from the
 * available size.  Otherwise a new area is created in USER_STACK_REGION with
 * appropriate guard pages.
 *
 * @param team            Team that owns the stack area.
 * @param thread          Thread that will own the stack.
 * @param _stackBase      Caller-supplied base address, or NULL to auto-allocate.
 * @param stackSize       Requested stack size (0 for default).
 * @param additionalSize  Extra bytes appended after the stack (for TLS etc.).
 * @param guardSize       Guard page size in bytes.
 * @param nameBuffer      B_OS_NAME_LENGTH buffer used to build the area name.
 * @retval B_OK        Stack created and thread fields updated.
 * @retval B_BAD_VALUE @p stackSize is below MIN_USER_STACK_SIZE.
 * @retval < B_OK      Area creation failed.
 */
static status_t
create_thread_user_stack(Team* team, Thread* thread, void* _stackBase,
	size_t stackSize, size_t additionalSize, size_t guardSize,
	char* nameBuffer)
{
	area_id stackArea = -1;
	uint8* stackBase = (uint8*)_stackBase;

	if (stackBase != NULL) {
		// A stack has been specified. It must be large enough to hold the
		// TLS space at least. Guard pages are ignored for existing stacks.
		STATIC_ASSERT(TLS_SIZE < MIN_USER_STACK_SIZE);
		if (stackSize < MIN_USER_STACK_SIZE)
			return B_BAD_VALUE;

		stackSize -= TLS_SIZE;
	} else {
		// No user-defined stack -- allocate one. For non-main threads the stack
		// will be between USER_STACK_REGION and the main thread stack area. For
		// a main thread the position is fixed.

		guardSize = PAGE_ALIGN(guardSize);

		if (stackSize == 0) {
			// Use the default size (a different one for a main thread).
			stackSize = thread->id == team->id
				? USER_MAIN_THREAD_STACK_SIZE : USER_STACK_SIZE;
		} else {
			// Verify that the given stack size is large enough.
			if (stackSize < MIN_USER_STACK_SIZE)
				return B_BAD_VALUE;

			stackSize = PAGE_ALIGN(stackSize);
		}

		size_t areaSize = PAGE_ALIGN(guardSize + stackSize + TLS_SIZE
			+ additionalSize);

		snprintf(nameBuffer, B_OS_NAME_LENGTH, "%s_%" B_PRId32 "_stack",
			thread->name, thread->id);

		stackBase = (uint8*)USER_STACK_REGION;

		virtual_address_restrictions virtualRestrictions = {};
		virtualRestrictions.address_specification = B_RANDOMIZED_BASE_ADDRESS;
		virtualRestrictions.address = (void*)stackBase;

		physical_address_restrictions physicalRestrictions = {};

		stackArea = create_area_etc(team->id, nameBuffer,
			areaSize, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA | B_STACK_AREA,
			0, guardSize, &virtualRestrictions, &physicalRestrictions,
			(void**)&stackBase);
		if (stackArea < 0)
			return stackArea;
	}

	// set the stack
	ThreadLocker threadLocker(thread);
#ifdef STACK_GROWS_DOWNWARDS
	thread->user_stack_base = (addr_t)stackBase + guardSize;
#else
	thread->user_stack_base = (addr_t)stackBase;
#endif
	thread->user_stack_size = stackSize;
	thread->user_stack_area = stackArea;

	return B_OK;
}


/**
 * @brief Public wrapper to create a user stack for a thread with default guard size.
 *
 * Delegates to create_thread_user_stack() using USER_STACK_GUARD_SIZE.
 *
 * @param team            Team that owns the stack.
 * @param thread          Thread that will use the stack.
 * @param stackBase       Caller-supplied base address, or NULL to auto-allocate.
 * @param stackSize       Requested stack size (0 for default).
 * @param additionalSize  Extra bytes appended after the stack.
 * @retval B_OK           Stack created successfully.
 * @retval < B_OK         Stack creation failed.
 */
status_t
thread_create_user_stack(Team* team, Thread* thread, void* stackBase,
	size_t stackSize, size_t additionalSize)
{
	char nameBuffer[B_OS_NAME_LENGTH];
	return create_thread_user_stack(team, thread, stackBase, stackSize,
		additionalSize, USER_STACK_GUARD_SIZE, nameBuffer);
}


/*!	Creates a new thread.

	\param attributes The thread creation attributes, specifying the team in
		which to create the thread, as well as a whole bunch of other arguments.
	\param kernel \c true, if a kernel-only thread shall be created, \c false,
		if the thread shall also be able to run in userland.
	\return The ID of the newly created thread (>= 0) or an error code on
		failure.
*/
/**
 * @brief Allocate, configure, and register a new kernel or user thread.
 *
 * This is the central thread-creation routine.  It allocates (or reuses) a
 * Thread object, sets its priority and state, allocates a kernel stack,
 * optionally allocates a user stack, initialises the kernel stack frame,
 * inserts the thread into its owning team, and notifies listeners.
 *
 * @param attributes  Fully populated ThreadCreationAttributes describing the
 *                    new thread (name, priority, entry point, team, etc.).
 * @param kernel      true to create a kernel-only thread; false to create a
 *                    thread that can run in userland.
 * @retval >= 0       Thread ID of the newly created (suspended) thread.
 * @retval B_BAD_TEAM_ID   The specified team does not exist or is shutting down.
 * @retval B_NO_MORE_THREADS The global thread limit has been reached.
 * @retval B_NO_MEMORY Allocation failure.
 * @retval < B_OK     Other error from stack or timer initialisation.
 */
thread_id
thread_create_thread(const ThreadCreationAttributes& attributes, bool kernel)
{
	status_t status = B_OK;

	TRACE(("thread_create_thread(%s, thread = %p, %s)\n", attributes.name,
		attributes.thread, kernel ? "kernel" : "user"));

	// get the team
	Team* team = Team::Get(attributes.team);
	if (team == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> teamReference(team, true);

	// If a thread object is given, acquire a reference to it, otherwise create
	// a new thread object with the given attributes.
	Thread* thread = attributes.thread;
	if (thread != NULL) {
		thread->AcquireReference();
	} else {
		status = Thread::Create(attributes.name, thread);
		if (status != B_OK)
			return status;
	}
	BReference<Thread> threadReference(thread, true);

	thread->team = team;
		// set already, so, if something goes wrong, the team pointer is
		// available for deinitialization
	thread->priority = attributes.priority == -1
		? B_NORMAL_PRIORITY : attributes.priority;
	thread->priority = std::max(thread->priority,
			(int32)THREAD_MIN_SET_PRIORITY);
	thread->priority = std::min(thread->priority,
			(int32)THREAD_MAX_SET_PRIORITY);
	thread->state = B_THREAD_SUSPENDED;

	thread->sig_block_mask = attributes.signal_mask;

	// init debug structure
	init_thread_debug_info(&thread->debug_info);

	// create the kernel stack
	char stackName[B_OS_NAME_LENGTH];
	snprintf(stackName, B_OS_NAME_LENGTH, "%s_%" B_PRId32 "_kstack",
		thread->name, thread->id);

	thread->kernel_stack_area = allocate_kernel_stack(stackName,
		&thread->kernel_stack_base, &thread->kernel_stack_top);
	if (thread->kernel_stack_area < 0) {
		// we're not yet part of a team, so we can just bail out
		status = thread->kernel_stack_area;

		dprintf("create_thread: error creating kernel stack: %s!\n",
			strerror(status));

		return status;
	}

	if (kernel) {
		// Init the thread's kernel stack. It will start executing
		// common_thread_entry() with the arguments we prepare here.
		ThreadEntryArguments entryArgs;
		entryArgs.kernelFunction = attributes.kernelEntry;
		entryArgs.argument = attributes.kernelArgument;
		entryArgs.enterUserland = false;

		init_thread_kernel_stack(thread, &entryArgs, sizeof(entryArgs));
	} else {
		// create the userland stack, if the thread doesn't have one yet
		if (thread->user_stack_base == 0) {
			status = create_thread_user_stack(team, thread,
				attributes.stack_address, attributes.stack_size,
				attributes.additional_stack_size, attributes.guard_size,
				stackName);
			if (status != B_OK)
				return status;
		}

		// Init the thread's kernel stack. It will start executing
		// common_thread_entry() with the arguments we prepare here.
		UserThreadEntryArguments entryArgs;
		entryArgs.kernelFunction = attributes.kernelEntry;
		entryArgs.argument = attributes.kernelArgument;
		entryArgs.enterUserland = true;
		entryArgs.userlandEntry = (addr_t)attributes.entry;
		entryArgs.userlandArgument1 = attributes.args1;
		entryArgs.userlandArgument2 = attributes.args2;
		entryArgs.pthread = attributes.pthread;
		entryArgs.forkArgs = attributes.forkArgs;
		entryArgs.flags = attributes.flags;

		init_thread_kernel_stack(thread, &entryArgs, sizeof(entryArgs));

		// create the pre-defined thread timers
		status = user_timer_create_thread_timers(team, thread);
		if (status != B_OK)
			return status;
	}

	// lock the team and see, if it is still alive
	TeamLocker teamLocker(team);
	if (team->state >= TEAM_STATE_SHUTDOWN)
		return B_BAD_TEAM_ID;

	bool debugNewThread = false;
	if (!kernel) {
		// ensure there's a user_mutex_context, if this isn't the main thread
		if (team->main_thread != NULL && team->user_mutex_context == NULL) {
			status_t status = allocate_team_user_mutex_context(team);
			if (status != B_OK)
				return status;
		}

		// allocate the user_thread structure, if not already allocated
		if (thread->user_thread == NULL) {
			thread->user_thread = team_allocate_user_thread(team);
			if (thread->user_thread == NULL)
				return B_NO_MEMORY;
		}

		// If the new thread belongs to the same team as the current thread, it
		// may inherit some of the thread debug flags.
		Thread* currentThread = thread_get_current_thread();
		if (currentThread != NULL && currentThread->team == team) {
			// inherit all user flags...
			int32 debugFlags = atomic_get(&currentThread->debug_info.flags)
				& B_THREAD_DEBUG_USER_FLAG_MASK;

			// ... save the syscall tracing flags, unless explicitely specified
			if (!(debugFlags & B_THREAD_DEBUG_SYSCALL_TRACE_CHILD_THREADS)) {
				debugFlags &= ~(B_THREAD_DEBUG_PRE_SYSCALL
					| B_THREAD_DEBUG_POST_SYSCALL);
			}

			thread->debug_info.flags = debugFlags;

			// stop the new thread, if desired
			debugNewThread = debugFlags & B_THREAD_DEBUG_STOP_CHILD_THREADS;
		}
	}

	// We're going to make the thread live, now. The thread itself will take
	// over a reference to its Thread object. We'll acquire another reference
	// for our own use (and threadReference remains armed).

	ThreadLocker threadLocker(thread);

	InterruptsSpinLocker threadCreationLocker(gThreadCreationLock);
	WriteSpinLocker threadHashLocker(sThreadHashLock);

	// check the thread limit
	if (sUsedThreads >= sMaxThreads) {
		// Clean up the user_thread structure. It's a bit unfortunate that the
		// Thread destructor cannot do that, so we have to do that explicitly.
		threadHashLocker.Unlock();
		threadCreationLocker.Unlock();

		user_thread* userThread = thread->user_thread;
		thread->user_thread = NULL;

		threadLocker.Unlock();
		teamLocker.Unlock();

		if (userThread != NULL)
			team_free_user_thread(team, userThread);

		return B_NO_MORE_THREADS;
	}

	// make thread visible in global hash/list
	thread->visible = true;
	sUsedThreads++;

	scheduler_on_thread_init(thread);

	thread->AcquireReference();

	// Debug the new thread, if the parent thread required that (see above),
	// or the respective global team debug flag is set. But only, if a
	// debugger is installed for the team.
	if (!kernel) {
		int32 teamDebugFlags = atomic_get(&team->debug_info.flags);
		debugNewThread |= (teamDebugFlags & B_TEAM_DEBUG_STOP_NEW_THREADS) != 0;
		if (debugNewThread
			&& (teamDebugFlags & B_TEAM_DEBUG_DEBUGGER_INSTALLED) != 0) {
			thread->debug_info.flags |= B_THREAD_DEBUG_STOP;
		}
	}

	{
		SpinLocker signalLocker(team->signal_lock);
		SpinLocker timeLocker(team->time_lock);

		// insert thread into team
		insert_thread_into_team(team, thread);
	}

	threadHashLocker.Unlock();
	threadCreationLocker.Unlock();
	threadLocker.Unlock();
	teamLocker.Unlock();

	// notify listeners
	sNotificationService.Notify(THREAD_ADDED, thread);

	return thread->id;
}


/**
 * @brief Kernel thread that performs final cleanup for exited threads.
 *
 * Waits on @c sUndertakerCondition for entries posted by thread_exit(),
 * then removes each dying thread from the kernel team and releases its
 * last reference, allowing the Thread destructor to run.
 *
 * @param args  Unused.
 * @return      Never returns (B_OK placeholder).
 */
static status_t
undertaker(void* /*args*/)
{
	while (true) {
		// wait for a thread to bury
		InterruptsSpinLocker locker(sUndertakerLock);

		while (sUndertakerEntries.IsEmpty()) {
			ConditionVariableEntry conditionEntry;
			sUndertakerCondition.Add(&conditionEntry);
			locker.Unlock();

			conditionEntry.Wait();

			locker.Lock();
		}

		UndertakerEntry* _entry = sUndertakerEntries.RemoveHead();
		locker.Unlock();

		UndertakerEntry entry = *_entry;
			// we need a copy, since the original entry is on the thread's stack

		// we've got an entry
		Thread* thread = entry.thread;

		// make sure the thread isn't running anymore
		InterruptsSpinLocker schedulerLocker(thread->scheduler_lock);
		ASSERT(thread->state == THREAD_STATE_FREE_ON_RESCHED);
		schedulerLocker.Unlock();

		// remove this thread from from the kernel team -- this makes it
		// unaccessible
		Team* kernelTeam = team_get_kernel_team();
		TeamLocker kernelTeamLocker(kernelTeam);
		thread->Lock();

		InterruptsSpinLocker threadCreationLocker(gThreadCreationLock);
		SpinLocker signalLocker(kernelTeam->signal_lock);
		SpinLocker timeLocker(kernelTeam->time_lock);

		remove_thread_from_team(kernelTeam, thread);

		timeLocker.Unlock();
		signalLocker.Unlock();
		threadCreationLocker.Unlock();

		kernelTeamLocker.Unlock();

		// free the thread structure
		thread->UnlockAndReleaseReference();
	}

	// can never get here
	return B_OK;
}


/*!	Returns the semaphore the thread is currently waiting on.

	The return value is purely informative.
	The caller must hold the scheduler lock.

	\param thread The thread.
	\return The ID of the semaphore the thread is currently waiting on or \c -1,
		if it isn't waiting on a semaphore.
*/
/**
 * @brief Return the semaphore ID the thread is currently blocked on (informational).
 *
 * @param thread  Thread to inspect.
 * @return        The sem_id the thread is waiting on, or -1 if it is not
 *                blocked on a semaphore.
 * @note The caller must hold the scheduler lock.
 */
static sem_id
get_thread_wait_sem(Thread* thread)
{
	if (thread->state == B_THREAD_WAITING
		&& thread->wait.type == THREAD_BLOCK_TYPE_SEMAPHORE) {
		return (sem_id)(addr_t)thread->wait.object;
	}
	return -1;
}


/*!	Fills the thread_info structure with information from the specified thread.
	The caller must hold the thread's lock and the scheduler lock.
*/
/**
 * @brief Populate a thread_info structure from a Thread object.
 *
 * Copies the thread ID, team ID, name, state, priority, stack boundaries,
 * and accumulated CPU times into @p info.
 *
 * @param thread  Source thread.
 * @param info    Destination thread_info structure to fill.
 * @param size    Size of the thread_info structure (for forward compatibility).
 * @note The caller must hold the thread's lock and the scheduler lock.
 */
static void
fill_thread_info(Thread *thread, thread_info *info, size_t size)
{
	info->thread = thread->id;
	info->team = thread->team->id;

	strlcpy(info->name, thread->name, B_OS_NAME_LENGTH);

	info->sem = -1;

	if (thread->state == B_THREAD_WAITING) {
		info->state = B_THREAD_WAITING;

		switch (thread->wait.type) {
			case THREAD_BLOCK_TYPE_SNOOZE:
				info->state = B_THREAD_ASLEEP;
				break;

			case THREAD_BLOCK_TYPE_SEMAPHORE:
			{
				sem_id sem = (sem_id)(addr_t)thread->wait.object;
				if (sem == thread->msg.read_sem)
					info->state = B_THREAD_RECEIVING;
				else
					info->sem = sem;
				break;
			}

			case THREAD_BLOCK_TYPE_CONDITION_VARIABLE:
			default:
				break;
		}
	} else
		info->state = (thread_state)thread->state;

	info->priority = thread->priority;
	info->stack_base = (void *)thread->user_stack_base;
	info->stack_end = (void *)(thread->user_stack_base
		+ thread->user_stack_size);

	InterruptsSpinLocker threadTimeLocker(thread->time_lock);
	info->user_time = thread->user_time;
	info->kernel_time = thread->kernel_time;
	if (thread->last_time != 0) {
		const bigtime_t current = system_time() - thread->last_time;
		if (thread->in_kernel)
			info->kernel_time += current;
		else
			info->user_time += current;
	}
}


/**
 * @brief Send a data message to another thread, with flags.
 *
 * Acquires the target thread's write semaphore, copies the payload, and
 * releases the read semaphore to wake any waiting receiver.
 *
 * @param id          Target thread ID.
 * @param code        Integer message code delivered with the data.
 * @param buffer      Pointer to the data payload (may be a user address).
 * @param bufferSize  Size of the payload in bytes (0 is allowed).
 * @param flags       Semaphore acquisition flags (e.g. B_KILL_CAN_INTERRUPT).
 * @retval B_OK             Message delivered.
 * @retval B_BAD_THREAD_ID  Target thread not found or already exited.
 * @retval B_NO_MEMORY      Payload allocation failed or size too large.
 * @retval B_INTERRUPTED    Acquisition was interrupted by a signal.
 * @retval B_BAD_DATA       user_memcpy of the payload failed.
 */
static status_t
send_data_etc(thread_id id, int32 code, const void *buffer, size_t bufferSize,
	int32 flags)
{
	// get the thread
	Thread *target = Thread::Get(id);
	if (target == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> targetReference(target, true);

	// get the write semaphore
	ThreadLocker targetLocker(target);
	sem_id cachedSem = target->msg.write_sem;
	targetLocker.Unlock();

	if (bufferSize > THREAD_MAX_MESSAGE_SIZE)
		return B_NO_MEMORY;

	status_t status = acquire_sem_etc(cachedSem, 1, flags, 0);
	if (status == B_INTERRUPTED) {
		// we got interrupted by a signal
		return status;
	}
	if (status != B_OK) {
		// Any other acquisition problems may be due to thread deletion
		return B_BAD_THREAD_ID;
	}

	void* data;
	if (bufferSize > 0) {
		data = malloc(bufferSize);
		if (data == NULL)
			return B_NO_MEMORY;
		if (user_memcpy(data, buffer, bufferSize) != B_OK) {
			free(data);
			return B_BAD_DATA;
		}
	} else
		data = NULL;

	targetLocker.Lock();

	// The target thread could have been deleted at this point.
	if (!target->IsAlive()) {
		targetLocker.Unlock();
		free(data);
		return B_BAD_THREAD_ID;
	}

	// Save message informations
	target->msg.sender = thread_get_current_thread()->id;
	target->msg.code = code;
	target->msg.size = bufferSize;
	target->msg.buffer = data;
	cachedSem = target->msg.read_sem;

	targetLocker.Unlock();

	release_sem(cachedSem);
	return B_OK;
}


/**
 * @brief Receive a data message sent to the current thread, with flags.
 *
 * Blocks (subject to @p flags) until a message is available, copies at most
 * @p bufferSize bytes to @p buffer, sets @p _sender to the sender's thread ID,
 * and returns the message code.
 *
 * @param _sender     Receives the sender's thread ID.
 * @param buffer      Destination buffer for the message payload
 *                    (may be a user address).
 * @param bufferSize  Maximum bytes to copy into @p buffer.
 * @param flags       Semaphore acquisition flags.
 * @return            Message code on success, or a negative error code.
 */
static int32
receive_data_etc(thread_id *_sender, void *buffer, size_t bufferSize,
	int32 flags)
{
	Thread *thread = thread_get_current_thread();
	size_t size;
	int32 code;

	status_t status = acquire_sem_etc(thread->msg.read_sem, 1, flags, 0);
	if (status != B_OK) {
		// Actually, we're not supposed to return error codes
		// but since the only reason this can fail is that we
		// were killed, it's probably okay to do so (but also
		// meaningless).
		return status;
	}

	if (buffer != NULL && bufferSize != 0 && thread->msg.buffer != NULL) {
		size = min_c(bufferSize, thread->msg.size);
		status = user_memcpy(buffer, thread->msg.buffer, size);
		if (status != B_OK) {
			free(thread->msg.buffer);
			release_sem(thread->msg.write_sem);
			return status;
		}
	}

	*_sender = thread->msg.sender;
	code = thread->msg.code;

	free(thread->msg.buffer);
	release_sem(thread->msg.write_sem);

	return code;
}


/**
 * @brief Kernel-internal implementation of getrlimit().
 *
 * Fills @p rlp for the requested @p resource.  Supports RLIMIT_AS,
 * RLIMIT_CORE, RLIMIT_DATA, RLIMIT_NOFILE, RLIMIT_NOVMON, and RLIMIT_STACK.
 *
 * @param resource  POSIX resource identifier (e.g. RLIMIT_STACK).
 * @param rlp       Destination rlimit structure; must be non-NULL.
 * @retval B_OK     Limit returned successfully.
 * @retval EINVAL   Unknown or unsupported resource.
 * @retval B_BAD_ADDRESS @p rlp is NULL.
 */
static status_t
common_getrlimit(int resource, struct rlimit * rlp)
{
	if (!rlp)
		return B_BAD_ADDRESS;

	switch (resource) {
		case RLIMIT_AS:
			rlp->rlim_cur = __HAIKU_ADDR_MAX;
			rlp->rlim_max = __HAIKU_ADDR_MAX;
			return B_OK;

		case RLIMIT_CORE:
			rlp->rlim_cur = 0;
			rlp->rlim_max = 0;
			return B_OK;

		case RLIMIT_DATA:
			rlp->rlim_cur = RLIM_INFINITY;
			rlp->rlim_max = RLIM_INFINITY;
			return B_OK;

		case RLIMIT_NOFILE:
		case RLIMIT_NOVMON:
			return vfs_getrlimit(resource, rlp);

		case RLIMIT_STACK:
		{
			rlp->rlim_cur = USER_MAIN_THREAD_STACK_SIZE;
			rlp->rlim_max = USER_MAIN_THREAD_STACK_SIZE;
			return B_OK;
		}

		default:
			return EINVAL;
	}

	return B_OK;
}


/**
 * @brief Kernel-internal implementation of setrlimit().
 *
 * Applies the limit described by @p rlp for @p resource.  Only
 * RLIMIT_CORE (to 0/0), RLIMIT_NOFILE, and RLIMIT_NOVMON are supported.
 *
 * @param resource  POSIX resource identifier.
 * @param rlp       New limit to apply; must be non-NULL.
 * @retval B_OK     Limit applied successfully.
 * @retval EINVAL   Unsupported resource or invalid value.
 * @retval B_BAD_ADDRESS @p rlp is NULL.
 */
static status_t
common_setrlimit(int resource, const struct rlimit * rlp)
{
	if (!rlp)
		return B_BAD_ADDRESS;

	switch (resource) {
		case RLIMIT_CORE:
			// We don't support core file, so allow settings to 0/0 only.
			if (rlp->rlim_cur != 0 || rlp->rlim_max != 0)
				return EINVAL;
			return B_OK;

		case RLIMIT_NOFILE:
		case RLIMIT_NOVMON:
			return vfs_setrlimit(resource, rlp);

		default:
			return EINVAL;
	}

	return B_OK;
}


/**
 * @brief Common implementation for snooze(), snooze_etc(), and _user_snooze_etc().
 *
 * Puts the current thread to sleep for the specified @p timeout on the given
 * @p clockID.  Supports CLOCK_REALTIME and CLOCK_MONOTONIC with relative or
 * absolute timeouts.  If interrupted, the remaining time is written to
 * @p _remainingTime when non-NULL.
 *
 * @param timeout         Sleep duration or absolute wake time.
 * @param clockID         POSIX clock to use (CLOCK_REALTIME or CLOCK_MONOTONIC).
 * @param flags           B_RELATIVE_TIMEOUT / B_ABSOLUTE_TIMEOUT /
 *                        B_TIMEOUT_REAL_TIME_BASE flags.
 * @param _remainingTime  If non-NULL and the sleep was interrupted, receives
 *                        the remaining time.
 * @retval B_OK           Sleep completed normally (or timed out as requested).
 * @retval B_INTERRUPTED  Sleep was interrupted by a signal.
 * @retval B_BAD_VALUE    clockID was CLOCK_THREAD_CPUTIME_ID.
 * @retval ENOTSUP        clockID is not supported for sleeping.
 * @note Must be called with interrupts enabled.
 */
static status_t
common_snooze_etc(bigtime_t timeout, clockid_t clockID, uint32 flags,
	bigtime_t* _remainingTime)
{
#if KDEBUG
	if (!are_interrupts_enabled()) {
		panic("common_snooze_etc(): called with interrupts disabled, timeout "
			"%" B_PRIdBIGTIME, timeout);
	}
#endif

	switch (clockID) {
		case CLOCK_REALTIME:
			// make sure the B_TIMEOUT_REAL_TIME_BASE flag is set and fall
			// through
			flags |= B_TIMEOUT_REAL_TIME_BASE;
		case CLOCK_MONOTONIC:
		{
			// Store the start time, for the case that we get interrupted and
			// need to return the remaining time. For absolute timeouts we can
			// still get he time later, if needed.
			bigtime_t startTime
				= _remainingTime != NULL && (flags & B_RELATIVE_TIMEOUT) != 0
					? system_time() : 0;

			Thread* thread = thread_get_current_thread();

			thread_prepare_to_block(thread, flags, THREAD_BLOCK_TYPE_SNOOZE,
				NULL);
			status_t status = thread_block_with_timeout(flags, timeout);

			if (status == B_TIMED_OUT || status == B_WOULD_BLOCK)
				return B_OK;

			// If interrupted, compute the remaining time, if requested.
			if (status == B_INTERRUPTED && _remainingTime != NULL) {
				if ((flags & B_RELATIVE_TIMEOUT) != 0) {
					*_remainingTime = std::max(
						startTime + timeout - system_time(), (bigtime_t)0);
				} else {
					bigtime_t now = (flags & B_TIMEOUT_REAL_TIME_BASE) != 0
						? real_time_clock_usecs() : system_time();
					*_remainingTime = std::max(timeout - now, (bigtime_t)0);
				}
			}

			return status;
		}

		case CLOCK_THREAD_CPUTIME_ID:
			// Waiting for ourselves to do something isn't particularly
			// productive.
			return B_BAD_VALUE;

		case CLOCK_PROCESS_CPUTIME_ID:
		default:
			// We don't have to support those, but we are allowed to. Could be
			// done be creating a UserTimer on the fly with a custom UserEvent
			// that would just wake us up.
			return ENOTSUP;
	}
}


//	#pragma mark - debugger calls


/**
 * @brief Debugger command: demote all (or one) real-time threads to normal priority.
 *
 * Usage: unreal [<id>]
 *
 * @param argc  Argument count.
 * @param argv  Argument vector.
 * @return      0 always.
 */
static int
make_thread_unreal(int argc, char **argv)
{
	int32 id = -1;

	if (argc > 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	if (argc > 1)
		id = strtoul(argv[1], NULL, 0);

	for (ThreadHashTable::Iterator it = sThreadHash.GetIterator();
			Thread* thread = it.Next();) {
		if (id != -1 && thread->id != id)
			continue;

		if (thread->priority > B_DISPLAY_PRIORITY) {
			scheduler_set_thread_priority(thread, B_NORMAL_PRIORITY);
			kprintf("thread %" B_PRId32 " made unreal\n", thread->id);
		}
	}

	return 0;
}


/**
 * @brief Debugger command: set the scheduling priority of a thread.
 *
 * Usage: priority <prio> [<id>]
 *
 * @param argc  Argument count.
 * @param argv  Argument vector.
 * @return      0 always.
 */
static int
set_thread_prio(int argc, char **argv)
{
	int32 id;
	int32 prio;

	if (argc > 3 || argc < 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	prio = strtoul(argv[1], NULL, 0);
	if (prio > THREAD_MAX_SET_PRIORITY)
		prio = THREAD_MAX_SET_PRIORITY;
	if (prio < THREAD_MIN_SET_PRIORITY)
		prio = THREAD_MIN_SET_PRIORITY;

	if (argc > 2)
		id = strtoul(argv[2], NULL, 0);
	else
		id = thread_get_current_thread()->id;

	bool found = false;
	for (ThreadHashTable::Iterator it = sThreadHash.GetIterator();
			Thread* thread = it.Next();) {
		if (thread->id != id)
			continue;
		scheduler_set_thread_priority(thread, prio);
		kprintf("thread %" B_PRId32 " set to priority %" B_PRId32 "\n", id, prio);
		found = true;
		break;
	}
	if (!found)
		kprintf("thread %" B_PRId32 " (%#" B_PRIx32 ") not found\n", id, id);

	return 0;
}


/**
 * @brief Debugger command: send SIGSTOP to a thread (suspend it).
 *
 * Usage: suspend [<id>]
 *
 * @param argc  Argument count.
 * @param argv  Argument vector.
 * @return      0 always.
 */
static int
make_thread_suspended(int argc, char **argv)
{
	int32 id;

	if (argc > 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	if (argc == 1)
		id = thread_get_current_thread()->id;
	else
		id = strtoul(argv[1], NULL, 0);

	bool found = false;
	for (ThreadHashTable::Iterator it = sThreadHash.GetIterator();
			Thread* thread = it.Next();) {
		if (thread->id != id)
			continue;

		Signal signal(SIGSTOP, SI_USER, B_OK, team_get_kernel_team()->id);
		send_signal_to_thread(thread, signal, B_DO_NOT_RESCHEDULE);

		kprintf("thread %" B_PRId32 " suspended\n", id);
		found = true;
		break;
	}
	if (!found)
		kprintf("thread %" B_PRId32 " (%#" B_PRIx32 ") not found\n", id, id);

	return 0;
}


/**
 * @brief Debugger command: enqueue a suspended/sleeping/waiting thread back into the run queue.
 *
 * Usage: resume <id>
 *
 * @param argc  Argument count.
 * @param argv  Argument vector (must include a thread ID).
 * @return      0 always.
 */
static int
make_thread_resumed(int argc, char **argv)
{
	int32 id;

	if (argc != 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	// force user to enter a thread id, as using
	// the current thread is usually not intended
	id = strtoul(argv[1], NULL, 0);

	bool found = false;
	for (ThreadHashTable::Iterator it = sThreadHash.GetIterator();
			Thread* thread = it.Next();) {
		if (thread->id != id)
			continue;

		if (thread->state == B_THREAD_SUSPENDED || thread->state == B_THREAD_ASLEEP
				|| thread->state == B_THREAD_WAITING) {
			scheduler_enqueue_in_run_queue(thread);
			kprintf("thread %" B_PRId32 " resumed\n", thread->id);
		} else
			kprintf("thread %" B_PRId32 " is already running\n", thread->id);
		found = true;
		break;
	}
	if (!found)
		kprintf("thread %" B_PRId32 " (%#" B_PRIx32 ") not found\n", id, id);

	return 0;
}


/**
 * @brief Debugger command: drop a userland thread into the userland debugger.
 *
 * Usage: drop [<id>]
 *
 * @param argc  Argument count.
 * @param argv  Argument vector.
 * @return      0 always.
 * @note Uses _user_debug_thread() which performs locking — use with caution
 *       from the kernel debugger.
 */
static int
drop_into_debugger(int argc, char **argv)
{
	status_t err;
	int32 id;

	if (argc > 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	if (argc == 1)
		id = thread_get_current_thread()->id;
	else
		id = strtoul(argv[1], NULL, 0);

	err = _user_debug_thread(id);
		// TODO: This is a non-trivial syscall doing some locking, so this is
		// really nasty and may go seriously wrong.
	if (err)
		kprintf("drop failed\n");
	else
		kprintf("thread %" B_PRId32 " dropped into user debugger\n", id);

	return 0;
}


/*!	Returns a user-readable string for a thread state.
	Only for use in the kernel debugger.
*/
/**
 * @brief Return a human-readable string for a thread state (debugger use only).
 *
 * Handles sub-states for B_THREAD_WAITING (snooze, receive, etc.).
 *
 * @param thread  Thread whose wait sub-state may refine the output (may be NULL).
 * @param state   Raw thread state value.
 * @return        Static string describing the state.
 * @note Must only be called from the kernel debugger.
 */
static const char *
state_to_text(Thread *thread, int32 state)
{
	switch (state) {
		case B_THREAD_READY:
			return "ready";

		case B_THREAD_RUNNING:
			return "running";

		case B_THREAD_WAITING:
		{
			if (thread != NULL) {
				switch (thread->wait.type) {
					case THREAD_BLOCK_TYPE_SNOOZE:
						return "zzz";

					case THREAD_BLOCK_TYPE_SEMAPHORE:
					{
						sem_id sem = (sem_id)(addr_t)thread->wait.object;
						if (sem == thread->msg.read_sem)
							return "receive";
						break;
					}
				}
			}

			return "waiting";
		}

		case B_THREAD_SUSPENDED:
			return "suspended";

		case THREAD_STATE_FREE_ON_RESCHED:
			return "death";

		default:
			return "UNKNOWN";
	}
}


/**
 * @brief Print the column header for the compact thread list table (debugger).
 */
static void
print_thread_list_table_head()
{
	kprintf("%-*s       id  state     wait for  %-*s    cpu pri  %-*s   team  "
		"name\n",
		B_PRINTF_POINTER_WIDTH, "thread", B_PRINTF_POINTER_WIDTH, "object",
		B_PRINTF_POINTER_WIDTH, "stack");
}


/**
 * @brief Print debug information for a single thread.
 *
 * In short mode (@p shortInfo = true) prints a single compact table row.
 * In long mode prints all fields of the Thread structure.
 *
 * @param thread     Thread to dump.
 * @param shortInfo  true for the compact one-line format, false for verbose.
 */
static void
_dump_thread_info(Thread *thread, bool shortInfo)
{
	if (shortInfo) {
		kprintf("%p %6" B_PRId32 "  %-10s", thread, thread->id,
			state_to_text(thread, thread->state));

		// does it block on a semaphore or a condition variable?
		if (thread->state == B_THREAD_WAITING) {
			switch (thread->wait.type) {
				case THREAD_BLOCK_TYPE_SEMAPHORE:
				{
					sem_id sem = (sem_id)(addr_t)thread->wait.object;
					if (sem == thread->msg.read_sem)
						kprintf("%*s", B_PRINTF_POINTER_WIDTH + 15, "");
					else {
						kprintf("sem       %-*" B_PRId32,
							B_PRINTF_POINTER_WIDTH + 5, sem);
					}
					break;
				}

				case THREAD_BLOCK_TYPE_CONDITION_VARIABLE:
				{
					char name[5];
					ssize_t length = ConditionVariable::DebugGetType(
						(ConditionVariable*)thread->wait.object, name, sizeof(name));
					if (length > 0)
						kprintf("cvar:%*s %p   ", 4, name, thread->wait.object);
					else
						kprintf("cvar      %p   ", thread->wait.object);
					break;
				}

				case THREAD_BLOCK_TYPE_SNOOZE:
					kprintf("%*s", B_PRINTF_POINTER_WIDTH + 15, "");
					break;

				case THREAD_BLOCK_TYPE_SIGNAL:
					kprintf("signal%*s", B_PRINTF_POINTER_WIDTH + 9, "");
					break;

				case THREAD_BLOCK_TYPE_MUTEX:
					kprintf("mutex     %p   ", thread->wait.object);
					break;

				case THREAD_BLOCK_TYPE_RW_LOCK:
					kprintf("rwlock    %p   ", thread->wait.object);
					break;

				case THREAD_BLOCK_TYPE_USER:
					kprintf("user%*s", B_PRINTF_POINTER_WIDTH + 11, "");
					break;

				case THREAD_BLOCK_TYPE_OTHER:
					kprintf("other%*s", B_PRINTF_POINTER_WIDTH + 10, "");
					break;

				case THREAD_BLOCK_TYPE_OTHER_OBJECT:
					kprintf("other     %p   ", thread->wait.object);
					break;

				default:
					kprintf("???       %p   ", thread->wait.object);
					break;
			}
		} else
			kprintf("-%*s", B_PRINTF_POINTER_WIDTH + 14, "");

		// on which CPU does it run?
		if (thread->cpu)
			kprintf("%2d", thread->cpu->cpu_num);
		else
			kprintf(" -");

		kprintf("%4" B_PRId32 "  %p%5" B_PRId32 "  %s\n", thread->priority,
			(void *)thread->kernel_stack_base, thread->team->id, thread->name);

		return;
	}

	// print the long info

	kprintf("THREAD: %p\n", thread);
	kprintf("id:                 %" B_PRId32 " (%#" B_PRIx32 ")\n", thread->id,
		thread->id);
	kprintf("serial_number:      %" B_PRId64 "\n", thread->serial_number);
	kprintf("name:               \"%s\"\n", thread->name);
	kprintf("hash_next:          %p\nteam_next:          %p\n",
		thread->hash_next, thread->team_link.next);
	kprintf("priority:           %" B_PRId32 " (I/O: %" B_PRId32 ")\n",
		thread->priority, thread->io_priority);
	kprintf("state:              %s\n", state_to_text(thread, thread->state));
	kprintf("cpu:                %p ", thread->cpu);
	if (thread->cpu)
		kprintf("(%d)\n", thread->cpu->cpu_num);
	else
		kprintf("\n");
	kprintf("cpumask:            %#" B_PRIx32 "\n", thread->cpumask.Bits(0));
	kprintf("sig_pending:        %#" B_PRIx64 " (blocked: %#" B_PRIx64
		", before sigsuspend(): %#" B_PRIx64 ")\n",
		(int64)thread->ThreadPendingSignals(),
		(int64)thread->sig_block_mask,
		(int64)thread->sigsuspend_original_unblocked_mask);
	kprintf("in_kernel:          %d\n", thread->in_kernel);

	if (thread->state == B_THREAD_WAITING) {
		kprintf("waiting for:        ");

		switch (thread->wait.type) {
			case THREAD_BLOCK_TYPE_SEMAPHORE:
			{
				sem_id sem = (sem_id)(addr_t)thread->wait.object;
				if (sem == thread->msg.read_sem)
					kprintf("data\n");
				else
					kprintf("semaphore %" B_PRId32 "\n", sem);
				break;
			}

			case THREAD_BLOCK_TYPE_CONDITION_VARIABLE:
				kprintf("condition variable %p\n", thread->wait.object);
				break;

			case THREAD_BLOCK_TYPE_SNOOZE:
				kprintf("snooze()\n");
				break;

			case THREAD_BLOCK_TYPE_SIGNAL:
				kprintf("signal\n");
				break;

			case THREAD_BLOCK_TYPE_MUTEX:
				kprintf("mutex %p\n", thread->wait.object);
				break;

			case THREAD_BLOCK_TYPE_RW_LOCK:
				kprintf("rwlock %p\n", thread->wait.object);
				break;

			case THREAD_BLOCK_TYPE_USER:
				kprintf("user\n");
				break;

			case THREAD_BLOCK_TYPE_OTHER:
				kprintf("other (%s)\n", (char*)thread->wait.object);
				break;

			case THREAD_BLOCK_TYPE_OTHER_OBJECT:
				kprintf("other (%p)\n", thread->wait.object);
				break;

			default:
				kprintf("unknown (%p)\n", thread->wait.object);
				break;
		}
	}

	kprintf("fault_handler:      %p\n", (void *)thread->fault_handler);
	kprintf("team:               %p, \"%s\"\n", thread->team,
		thread->team->Name());
	kprintf("  exit.sem:         %" B_PRId32 "\n", thread->exit.sem);
	kprintf("  exit.status:      %#" B_PRIx32 " (%s)\n", thread->exit.status,
		strerror(thread->exit.status));
	kprintf("  exit.waiters:\n");
	for (thread_death_entry* death = thread->exit.waiters.First(); death != NULL;
			death = thread->exit.waiters.GetNext(death)) {
		kprintf("\t%p (thread %" B_PRId32 ")\n", death, death->thread);
	}

	kprintf("kernel_stack_area:  %" B_PRId32 "\n", thread->kernel_stack_area);
	kprintf("kernel_stack_base:  %p\n", (void *)thread->kernel_stack_base);
	kprintf("user_stack_area:    %" B_PRId32 "\n", thread->user_stack_area);
	kprintf("user_stack_base:    %p\n", (void *)thread->user_stack_base);
	kprintf("user_local_storage: %p\n", (void *)thread->user_local_storage);
	kprintf("user_thread:        %p\n", (void *)thread->user_thread);
	kprintf("kernel_errno:       %#x (%s)\n", thread->kernel_errno,
		strerror(thread->kernel_errno));
	kprintf("kernel_time:        %" B_PRId64 "\n", thread->kernel_time);
	kprintf("user_time:          %" B_PRId64 "\n", thread->user_time);
	kprintf("flags:              0x%" B_PRIx32 "\n", thread->flags);
	kprintf("architecture dependant section:\n");
	arch_thread_dump_info(&thread->arch_info);
	kprintf("scheduler data:\n");
	scheduler_dump_thread_data(thread);
}


/**
 * @brief Debugger command: dump detailed or compact info about one or more threads.
 *
 * Usage: thread [-s] [<id|address|name>...]
 *
 * @param argc  Argument count.
 * @param argv  Argument vector.
 * @return      0 always.
 */
static int
dump_thread_info(int argc, char **argv)
{
	bool shortInfo = false;
	int argi = 1;
	if (argi < argc && strcmp(argv[argi], "-s") == 0) {
		shortInfo = true;
		print_thread_list_table_head();
		argi++;
	}

	if (argi == argc) {
		_dump_thread_info(thread_get_current_thread(), shortInfo);
		return 0;
	}

	for (; argi < argc; argi++) {
		const char *name = argv[argi];
		ulong arg = strtoul(name, NULL, 0);

		if (IS_KERNEL_ADDRESS(arg)) {
			// semi-hack
			_dump_thread_info((Thread *)arg, shortInfo);
			continue;
		}

		// walk through the thread list, trying to match name or id
		bool found = false;
		for (ThreadHashTable::Iterator it = sThreadHash.GetIterator();
				Thread* thread = it.Next();) {
			if (!strcmp(name, thread->name) || thread->id == (thread_id)arg) {
				_dump_thread_info(thread, shortInfo);
				found = true;
				break;
			}
		}

		if (!found)
			kprintf("thread \"%s\" (%" B_PRId32 ") doesn't exist!\n", name, (thread_id)arg);
	}

	return 0;
}


/**
 * @brief Debugger command: list threads filtered by state, team, semaphore, or call address.
 *
 * Handles the "threads", "ready", "running", "waiting", "realtime", and
 * "calling" debugger commands.
 *
 * @param argc  Argument count.
 * @param argv  Argument vector (argv[0] selects the filter mode).
 * @return      0 always.
 */
static int
dump_thread_list(int argc, char **argv)
{
	bool realTimeOnly = false;
	bool calling = false;
	const char *callSymbol = NULL;
	addr_t callStart = 0;
	addr_t callEnd = 0;
	int32 requiredState = 0;
	team_id team = -1;
	sem_id sem = -1;

	if (!strcmp(argv[0], "realtime"))
		realTimeOnly = true;
	else if (!strcmp(argv[0], "ready"))
		requiredState = B_THREAD_READY;
	else if (!strcmp(argv[0], "running"))
		requiredState = B_THREAD_RUNNING;
	else if (!strcmp(argv[0], "waiting")) {
		requiredState = B_THREAD_WAITING;

		if (argc > 1) {
			sem = strtoul(argv[1], NULL, 0);
			if (sem == 0)
				kprintf("ignoring invalid semaphore argument.\n");
		}
	} else if (!strcmp(argv[0], "calling")) {
		if (argc < 2) {
			kprintf("Need to give a symbol name or start and end arguments.\n");
			return 0;
		} else if (argc == 3) {
			callStart = parse_expression(argv[1]);
			callEnd = parse_expression(argv[2]);
		} else
			callSymbol = argv[1];

		calling = true;
	} else if (argc > 1) {
		team = strtoul(argv[1], NULL, 0);
		if (team == 0)
			kprintf("ignoring invalid team argument.\n");
	}

	print_thread_list_table_head();

	for (ThreadHashTable::Iterator it = sThreadHash.GetIterator();
			Thread* thread = it.Next();) {
		// filter out threads not matching the search criteria
		if ((requiredState && thread->state != requiredState)
			|| (calling && !arch_debug_contains_call(thread, callSymbol,
					callStart, callEnd))
			|| (sem > 0 && get_thread_wait_sem(thread) != sem)
			|| (team > 0 && thread->team->id != team)
			|| (realTimeOnly && thread->priority < B_REAL_TIME_DISPLAY_PRIORITY))
			continue;

		_dump_thread_info(thread, true);
	}
	return 0;
}


/**
 * @brief Restore the saved signal mask if THREAD_FLAGS_OLD_SIGMASK is set.
 *
 * Called during the kernel-exit path to apply a signal mask that was stashed
 * before a sigsuspend() or similar operation.
 *
 * @param thread  Thread whose signal mask may need restoring.
 */
static void
update_thread_sigmask_on_exit(Thread* thread)
{
	if ((thread->flags & THREAD_FLAGS_OLD_SIGMASK) != 0) {
		thread->flags &= ~THREAD_FLAGS_OLD_SIGMASK;
		sigprocmask(SIG_SETMASK, &thread->old_sig_block_mask, NULL);
	}
}


//	#pragma mark - private kernel API


/**
 * @brief Terminate the current thread, notify waiters, and hand off to the undertaker.
 *
 * Performs all cleanup required when a thread finishes execution:
 * - Boosts priority to expedite cleanup.
 * - Frees user timers and user_thread for non-main threads.
 * - Shuts down the entire team if this is the main thread (including killing
 *   all sibling threads and notifying the parent with SIGCHLD).
 * - Moves the thread to the kernel team while its stack is still valid.
 * - Destroys debug info, messaging semaphores, and the exit semaphore.
 * - Posts an UndertakerEntry and calls scheduler_reschedule() with
 *   THREAD_STATE_FREE_ON_RESCHED — never returns.
 *
 * @note Must be called with interrupts enabled.
 * @note This function never returns.
 */
void
thread_exit(void)
{
	cpu_status state;
	Thread* thread = thread_get_current_thread();
	Team* team = thread->team;
	Team* kernelTeam = team_get_kernel_team();
	status_t status;
	struct thread_debug_info debugInfo;
	team_id teamID = team->id;

	TRACE(("thread %" B_PRId32 " exiting w/return code %#" B_PRIx32 "\n",
		thread->id, thread->exit.status));

	if (!are_interrupts_enabled())
		panic("thread_exit() called with interrupts disabled!\n");

	// boost our priority to get this over with
	scheduler_set_thread_priority(thread, B_URGENT_DISPLAY_PRIORITY);

	if (team != kernelTeam) {
		// Delete all user timers associated with the thread.
		ThreadLocker threadLocker(thread);
		thread->DeleteUserTimers(false);

		// detach the thread's user thread
		user_thread* userThread = thread->user_thread;
		thread->user_thread = NULL;

		threadLocker.Unlock();

		// Delete the thread's user thread, if it's not the main thread. If it
		// is, we can save the work, since it will be deleted with the team's
		// address space.
		if (thread != team->main_thread)
			team_free_user_thread(team, userThread);
	}

	// remember the user stack area -- we will delete it below
	area_id userStackArea = -1;
	if (team->address_space != NULL && thread->user_stack_area >= 0) {
		userStackArea = thread->user_stack_area;
		thread->user_stack_area = -1;
	}

	struct job_control_entry *death = NULL;
	struct thread_death_entry* threadDeathEntry = NULL;
	bool deleteTeam = false;
	port_id debuggerPort = -1;

	if (team != kernelTeam) {
		user_debug_thread_exiting(thread);

		if (team->main_thread == thread) {
			// The main thread is exiting. Shut down the whole team.
			deleteTeam = true;

			// kill off all other threads and the user debugger facilities
			debuggerPort = team_shutdown_team(team);

			// acquire necessary locks, which are: process group lock, kernel
			// team lock, parent team lock, and the team lock
			team->LockProcessGroup();
			kernelTeam->Lock();
			team->LockTeamAndParent(true);
		} else {
			threadDeathEntry
				= (thread_death_entry*)malloc(sizeof(thread_death_entry));

			// acquire necessary locks, which are: kernel team lock and the team
			// lock
			kernelTeam->Lock();
			team->Lock();
		}

		ThreadLocker threadLocker(thread);

		state = disable_interrupts();

		// swap address spaces, to make sure we're running on the kernel's pgdir
		vm_swap_address_space(team->address_space, VMAddressSpace::Kernel());

		WriteSpinLocker teamLocker(thread->team_lock);
		SpinLocker threadCreationLocker(gThreadCreationLock);
			// removing the thread and putting its death entry to the parent
			// team needs to be an atomic operation

		// remember how long this thread lasted
		bigtime_t now = system_time();

		InterruptsSpinLocker signalLocker(kernelTeam->signal_lock);
		SpinLocker teamTimeLocker(kernelTeam->time_lock);
		SpinLocker threadTimeLocker(thread->time_lock);

		thread->kernel_time += now - thread->last_time;
		thread->last_time = now;

		team->dead_threads_kernel_time += thread->kernel_time;
		team->dead_threads_user_time += thread->user_time;

		// stop/update thread/team CPU time user timers
		if (thread->HasActiveCPUTimeUserTimers()
			|| team->HasActiveCPUTimeUserTimers()) {
			user_timer_stop_cpu_timers(thread, NULL);
		}

		// deactivate CPU time user timers for the thread
		if (thread->HasActiveCPUTimeUserTimers())
			thread->DeactivateCPUTimeUserTimers();

		threadTimeLocker.Unlock();

		// put the thread into the kernel team until it dies
		remove_thread_from_team(team, thread);
		insert_thread_into_team(kernelTeam, thread);

		teamTimeLocker.Unlock();
		signalLocker.Unlock();

		teamLocker.Unlock();

		if (team->death_entry != NULL) {
			if (--team->death_entry->remaining_threads == 0)
				team->death_entry->condition.NotifyOne();
		}

		if (deleteTeam) {
			Team* parent = team->parent;

			// Set the team job control state to "dead" and detach the job
			// control entry from our team struct.
			team_set_job_control_state(team, JOB_CONTROL_STATE_DEAD, NULL);
			death = team->job_control_entry;
			team->job_control_entry = NULL;

			if (death != NULL) {
				death->InitDeadState();

				// team_set_job_control_state() already moved our entry
				// into the parent's list. We just check the soft limit of
				// death entries.
				if (parent->dead_children.count > MAX_DEAD_CHILDREN) {
					death = parent->dead_children.entries.RemoveHead();
					parent->dead_children.count--;
				} else
					death = NULL;
			}

			threadCreationLocker.Unlock();
			restore_interrupts(state);

			threadLocker.Unlock();

			// Get a temporary reference to the team's process group
			// -- team_remove_team() removes the team from the group, which
			// might destroy it otherwise and we wouldn't be able to unlock it.
			ProcessGroup* group = team->group;
			group->AcquireReference();

			pid_t foregroundGroupToSignal;
			team_remove_team(team, foregroundGroupToSignal);

			// unlock everything but the parent team
			team->Unlock();
			if (parent != kernelTeam)
				kernelTeam->Unlock();
			group->Unlock();
			group->ReleaseReference();

			// Send SIGCHLD to the parent as long as we still have its lock.
			// This makes job control state change + signalling atomic.
			Signal childSignal(SIGCHLD, team->exit.reason, B_OK, team->id);
			if (team->exit.reason == CLD_EXITED) {
				childSignal.SetStatus(team->exit.status);
			} else {
				childSignal.SetStatus(team->exit.signal);
				childSignal.SetSendingUser(team->exit.signaling_user);
			}
			send_signal_to_team(parent, childSignal, B_DO_NOT_RESCHEDULE);

			// also unlock the parent
			parent->Unlock();

			// If the team was a session leader with controlling TTY, we have
			// to send SIGHUP to the foreground process group.
			if (foregroundGroupToSignal >= 0) {
				Signal groupSignal(SIGHUP, SI_USER, B_OK, team->id);
				send_signal_to_process_group(foregroundGroupToSignal,
					groupSignal, B_DO_NOT_RESCHEDULE);
			}
		} else {
			// The thread is not the main thread. We store a thread death entry
			// for it, unless someone is already waiting for it.
			if (threadDeathEntry != NULL) {
				if (thread->exit.waiters.IsEmpty()) {
					threadDeathEntry->thread = thread->id;
					threadDeathEntry->status = thread->exit.status;

					// add entry to dead thread list
					team->dead_threads.Add(threadDeathEntry);
				} else {
					deferred_free(threadDeathEntry);
					threadDeathEntry = NULL;
				}
			}

			threadCreationLocker.Unlock();
			restore_interrupts(state);

			threadLocker.Unlock();
			team->Unlock();
			kernelTeam->Unlock();
		}

		TRACE(("thread_exit: thread %" B_PRId32 " now a kernel thread!\n",
			thread->id));
	}

	// delete the team if we're its main thread
	if (deleteTeam) {
		team_delete_team(team, debuggerPort);

		// we need to delete any death entry that made it to here
		delete death;
	}

	ThreadLocker threadLocker(thread);

	state = disable_interrupts();
	SpinLocker threadCreationLocker(gThreadCreationLock);

	// mark invisible in global hash/list, so it's no longer accessible
	WriteSpinLocker threadHashLocker(sThreadHashLock);
	thread->visible = false;
	sUsedThreads--;
	threadHashLocker.Unlock();

	// Stop debugging for this thread
	SpinLocker threadDebugInfoLocker(thread->debug_info.lock);
	debugInfo = thread->debug_info;
	clear_thread_debug_info(&thread->debug_info, true);
	threadDebugInfoLocker.Unlock();

	// Remove the select infos. We notify them a little later.
	select_info* selectInfos = thread->select_infos;
	thread->select_infos = NULL;

	threadCreationLocker.Unlock();
	restore_interrupts(state);

	threadLocker.Unlock();

	destroy_thread_debug_info(&debugInfo);

	// notify select infos
	select_info* info = selectInfos;
	while (info != NULL) {
		select_sync* sync = info->sync;

		select_info* next = info->next;
		notify_select_events(info, B_EVENT_INVALID);
		put_select_sync(sync);
		info = next;
	}

	// notify listeners
	sNotificationService.Notify(THREAD_REMOVED, thread);

	// shutdown the thread messaging

	status = acquire_sem_etc(thread->msg.write_sem, 1, B_RELATIVE_TIMEOUT, 0);
	if (status == B_WOULD_BLOCK) {
		// there is data waiting for us, so let us eat it
		thread_id sender;

		delete_sem(thread->msg.write_sem);
			// first, let's remove all possibly waiting writers
		receive_data_etc(&sender, NULL, 0, B_RELATIVE_TIMEOUT);
	} else {
		// we probably own the semaphore here, and we're the last to do so
		delete_sem(thread->msg.write_sem);
	}
	// now we can safely remove the msg.read_sem
	delete_sem(thread->msg.read_sem);

	// fill all death entries and delete the sem that others will use to wait
	// for us
	{
		sem_id cachedExitSem = thread->exit.sem;

		ThreadLocker threadLocker(thread);

		// make sure no one will grab this semaphore again
		thread->exit.sem = -1;

		// fill all death entries
		for (thread_death_entry* entry = thread->exit.waiters.First(); entry != NULL;
				entry = thread->exit.waiters.GetNext(entry)) {
			entry->status = thread->exit.status;
		}

		threadLocker.Unlock();

		delete_sem(cachedExitSem);
	}

	// delete the user stack, if this was a user thread
	if (!deleteTeam && userStackArea >= 0) {
		// We postponed deleting the user stack until now, since this way all
		// notifications for the thread's death are out already and all other
		// threads waiting for this thread's death and some object on its stack
		// will wake up before we (try to) delete the stack area. Of most
		// relevance is probably the case where this is the main thread and
		// other threads use objects on its stack -- so we want them terminated
		// first.
		// When the team is deleted, all areas are deleted anyway, so we don't
		// need to do that explicitly in that case.
		vm_delete_area(teamID, userStackArea, true);
	}

	// notify the debugger
	if (teamID != kernelTeam->id)
		user_debug_thread_deleted(teamID, thread->id, thread->exit.status);

	// enqueue in the undertaker list and reschedule for the last time
	UndertakerEntry undertakerEntry(thread, teamID);

	disable_interrupts();

	SpinLocker schedulerLocker(thread->scheduler_lock);

	SpinLocker undertakerLocker(sUndertakerLock);
	sUndertakerEntries.Add(&undertakerEntry);
	sUndertakerCondition.NotifyOne();
	undertakerLocker.Unlock();

	scheduler_reschedule(THREAD_STATE_FREE_ON_RESCHED);

	panic("never can get here\n");
}


/*!	Called in the interrupt handler code when a thread enters
	the kernel for any reason.
	Only tracks time for now.
	Interrupts are disabled.
*/
/**
 * @brief Record the transition of the current thread from user space into the kernel.
 *
 * Accumulates elapsed user-space time and marks the thread as being in the
 * kernel.  Called by interrupt/syscall entry stubs with interrupts disabled.
 *
 * @param now  Current system time (passed in to avoid a second system_time() call).
 * @note Interrupts must be disabled on entry.
 */
void
thread_at_kernel_entry(bigtime_t now)
{
	Thread *thread = thread_get_current_thread();

	TRACE(("thread_at_kernel_entry: entry thread %" B_PRId32 "\n", thread->id));

	// track user time
	SpinLocker threadTimeLocker(thread->time_lock);
	thread->user_time += now - thread->last_time;
	thread->last_time = now;
	thread->in_kernel = true;
	threadTimeLocker.Unlock();
}


/*!	Called whenever a thread exits kernel space to user space.
	Tracks time, handles signals, ...
	Interrupts must be enabled. When the function returns, interrupts will be
	disabled.
	The function may not return. This e.g. happens when the thread has received
	a deadly signal.
*/
/**
 * @brief Handle the transition of the current thread from the kernel back to user space.
 *
 * Processes pending signals, disables interrupts, applies any saved signal
 * mask change, and accumulates kernel time.  May not return if a fatal signal
 * is delivered.
 *
 * @note Interrupts must be enabled on entry; they will be disabled on return.
 */
void
thread_at_kernel_exit(void)
{
	Thread *thread = thread_get_current_thread();

	TRACE(("thread_at_kernel_exit: exit thread %" B_PRId32 "\n", thread->id));

	handle_signals(thread);

	disable_interrupts();

	update_thread_sigmask_on_exit(thread);

	// track kernel time
	bigtime_t now = system_time();
	SpinLocker threadTimeLocker(thread->time_lock);
	thread->in_kernel = false;
	thread->kernel_time += now - thread->last_time;
	thread->last_time = now;
}


/*!	The quick version of thread_kernel_exit(), in case no signals are pending
	and no debugging shall be done.
	Interrupts must be disabled.
*/
/**
 * @brief Fast kernel-exit path used when no signals are pending.
 *
 * Applies any saved signal mask change and accumulates kernel time without
 * going through the full signal-handling path.
 *
 * @note Interrupts must be disabled on entry.
 */
void
thread_at_kernel_exit_no_signals(void)
{
	Thread *thread = thread_get_current_thread();

	TRACE(("thread_at_kernel_exit_no_signals: exit thread %" B_PRId32 "\n",
		thread->id));

	update_thread_sigmask_on_exit(thread);

	// track kernel time
	bigtime_t now = system_time();
	SpinLocker threadTimeLocker(thread->time_lock);
	thread->in_kernel = false;
	thread->kernel_time += now - thread->last_time;
	thread->last_time = now;
}


/**
 * @brief Reset per-thread state in preparation for an exec() call.
 *
 * Deletes user-defined timers, cancels the real-time pre-defined timer,
 * clears user stack and user_thread pointers, resets signal state, and
 * zeroes the CPU-time clock offset so the new image starts from time 0.
 */
void
thread_reset_for_exec(void)
{
	Thread* thread = thread_get_current_thread();

	ThreadLocker threadLocker(thread);

	// delete user-defined timers
	thread->DeleteUserTimers(true);

	// cancel pre-defined timer
	if (UserTimer* timer = thread->UserTimerFor(USER_TIMER_REAL_TIME_ID))
		timer->Cancel();

	// reset user_thread and user stack
	thread->user_thread = NULL;
	thread->user_stack_area = -1;
	thread->user_stack_base = 0;
	thread->user_stack_size = 0;

	// reset signals
	thread->ResetSignalsOnExec();

	// reset thread CPU time clock
	InterruptsSpinLocker timeLocker(thread->time_lock);
	thread->cpu_clock_offset = -thread->CPUTime(false);
}


/**
 * @brief Allocate the next available unique thread ID.
 *
 * Scans @c sNextThreadID forward (with wrap-around past INT_MAX) until an
 * ID not present in @c sThreadHash is found.
 *
 * @return  A thread_id that is not currently in use.
 * @note    The caller must hold @c sThreadHashLock in write mode, or call
 *          this function only while the thread hash write lock is held.
 */
thread_id
allocate_thread_id()
{
	InterruptsWriteSpinLocker threadHashLocker(sThreadHashLock);

	// find the next unused ID
	thread_id id;
	do {
		id = sNextThreadID++;

		// deal with integer overflow
		if (sNextThreadID < 0)
			sNextThreadID = 2;

		// check whether the ID is already in use
	} while (sThreadHash.Lookup(id, false) != NULL);

	return id;
}


/**
 * @brief Peek at the next thread ID that would be allocated (without consuming it).
 *
 * @return  The value of @c sNextThreadID under a read lock.
 */
thread_id
peek_next_thread_id()
{
	InterruptsReadSpinLocker threadHashLocker(sThreadHashLock);
	return sNextThreadID;
}


/*!	Yield the CPU to other threads.
	Thread will continue to run, if there's no other thread in ready
	state, and if it has a higher priority than the other ready threads, it
	still has a good chance to continue.
*/
/**
 * @brief Voluntarily yield the CPU to other ready threads.
 *
 * Sets the has_yielded flag and requests a reschedule to B_THREAD_READY,
 * allowing lower-priority threads a chance to run.  If no other thread is
 * ready, the calling thread may resume immediately.
 */
void
thread_yield(void)
{
	Thread *thread = thread_get_current_thread();
	if (thread == NULL)
		return;

	InterruptsSpinLocker _(thread->scheduler_lock);

	thread->has_yielded = true;
	scheduler_reschedule(B_THREAD_READY);
}


/**
 * @brief Iterate over all threads in the hash table and invoke a callback.
 *
 * Holds the thread hash write lock while iterating; the callback must not
 * attempt to acquire the same lock.
 *
 * @param function  Callback invoked for every live thread.
 * @param data      Opaque pointer forwarded to @p function.
 */
void
thread_map(void (*function)(Thread* thread, void* data), void* data)
{
	InterruptsWriteSpinLocker threadHashLocker(sThreadHashLock);

	for (ThreadHashTable::Iterator it = sThreadHash.GetIterator();
		Thread* thread = it.Next();) {
		function(thread, data);
	}
}


/*!	Kernel private thread creation function.
*/
/**
 * @brief Create and register a kernel thread in the specified team.
 *
 * Convenience wrapper around thread_create_thread() for kernel threads.
 *
 * @param function  Kernel-space entry function.
 * @param name      Thread name.
 * @param priority  Scheduling priority.
 * @param arg       Argument passed to @p function.
 * @param team      Team ID that will own the thread.
 * @retval >= 0  Thread ID of the newly created (suspended) thread.
 * @retval < 0   Error from thread_create_thread().
 */
thread_id
spawn_kernel_thread_etc(thread_func function, const char *name, int32 priority,
	void *arg, team_id team)
{
	return thread_create_thread(
		ThreadCreationAttributes(function, name, priority, arg, team),
		true);
}


/**
 * @brief Wait for a thread to exit, with timeout and flags.
 *
 * Locates the thread, registers a death-entry waiter, resumes the thread if
 * it is suspended, then blocks on the exit semaphore until the thread exits,
 * the timeout fires, or the wait is interrupted.  If the thread has already
 * exited, its death entry is found in the team's dead-thread or dead-children
 * list.
 *
 * @param id           Thread ID to wait for.
 * @param flags        Standard timeout/interrupt flags
 *                     (B_RELATIVE_TIMEOUT, B_CAN_INTERRUPT, etc.).
 * @param timeout      Timeout value in microseconds (0 for none).
 * @param _returnCode  If non-NULL, receives the thread's exit status.
 * @retval B_OK            Thread exited; @p _returnCode filled.
 * @retval B_BAD_THREAD_ID No such thread and no death entry found.
 * @retval EDEADLK         @p id is the calling thread's own ID.
 * @retval B_INTERRUPTED   Wait was interrupted.
 * @retval B_TIMED_OUT     Timeout expired before the thread exited.
 */
status_t
wait_for_thread_etc(thread_id id, uint32 flags, bigtime_t timeout,
	status_t *_returnCode)
{
	if (id < 0)
		return B_BAD_THREAD_ID;
	if (id == thread_get_current_thread_id())
		return EDEADLK;

	// get the thread, queue our death entry, and fetch the semaphore we have to
	// wait on
	sem_id exitSem = B_BAD_THREAD_ID;
	struct thread_death_entry death;

	Thread* thread = Thread::GetAndLock(id);
	if (thread != NULL) {
		// remember the semaphore we have to wait on and place our death entry
		exitSem = thread->exit.sem;
		if (exitSem >= 0)
			thread->exit.waiters.Add(&death, false);

		thread->UnlockAndReleaseReference();

		if (exitSem < 0)
			return B_BAD_THREAD_ID;
	} else {
		// we couldn't find this thread -- maybe it's already gone, and we'll
		// find its death entry in our team
		Team* team = thread_get_current_thread()->team;
		TeamLocker teamLocker(team);

		// check the child death entries first (i.e. main threads of child
		// teams)
		bool deleteEntry;
		job_control_entry* freeDeath
			= team_get_death_entry(team, id, &deleteEntry);
		if (freeDeath != NULL) {
			death.status = freeDeath->status;
			if (deleteEntry)
				delete freeDeath;
		} else {
			// check the thread death entries of the team (non-main threads)
			thread_death_entry* threadDeathEntry = NULL;
			for (threadDeathEntry = team->dead_threads.First(); threadDeathEntry != NULL;
					threadDeathEntry = team->dead_threads.GetNext(threadDeathEntry)) {
				if (threadDeathEntry->thread == id) {
					team->dead_threads.Remove(threadDeathEntry);
					death.status = threadDeathEntry->status;
					free(threadDeathEntry);
					break;
				}
			}

			if (threadDeathEntry == NULL)
				return B_BAD_THREAD_ID;
		}

		// we found the thread's death entry in our team
		if (_returnCode)
			*_returnCode = death.status;

		return B_OK;
	}

	// we need to wait for the death of the thread

	resume_thread(id);
		// make sure we don't wait forever on a suspended thread

	status_t status = acquire_sem_etc(exitSem, 1, flags, timeout);

	if (status == B_OK) {
		// this should never happen as the thread deletes the semaphore on exit
		panic("could acquire exit_sem for thread %" B_PRId32 "\n", id);
	} else if (status == B_BAD_SEM_ID) {
		// this is the way the thread normally exits
		status = B_OK;
	} else {
		// We were probably interrupted or the timeout occurred; we need to
		// remove our death entry now.
		thread = Thread::GetAndLock(id);
		if (thread != NULL) {
			thread->exit.waiters.Remove(&death);
			thread->UnlockAndReleaseReference();
		} else {
			// The thread is already gone, so we need to wait uninterruptibly
			// for its exit semaphore to make sure our death entry stays valid.
			// It won't take long, since the thread is apparently already in the
			// middle of the cleanup.
			acquire_sem(exitSem);
			status = B_OK;
		}
	}

	if (status == B_OK && _returnCode != NULL)
		*_returnCode = death.status;

	return status;
}


/**
 * @brief Register a select_info for the B_EVENT_INVALID event on a thread.
 *
 * @param id      Thread ID to monitor.
 * @param info    select_info structure to register.
 * @param kernel  true if the caller is kernel code (unused currently).
 * @retval B_OK            Registered successfully.
 * @retval B_BAD_THREAD_ID Thread not found.
 */
status_t
select_thread(int32 id, struct select_info* info, bool kernel)
{
	// get and lock the thread
	Thread* thread = Thread::GetAndLock(id);
	if (thread == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> threadReference(thread, true);
	ThreadLocker threadLocker(thread, true);

	// We support only B_EVENT_INVALID at the moment.
	info->selected_events &= B_EVENT_INVALID;

	// add info to list
	if (info->selected_events != 0) {
		info->next = thread->select_infos;
		thread->select_infos = info;

		// we need a sync reference
		acquire_select_sync(info->sync);
	}

	return B_OK;
}


/**
 * @brief Deregister a previously registered select_info from a thread.
 *
 * @param id      Thread ID from which to deregister.
 * @param info    select_info structure to remove.
 * @param kernel  true if the caller is kernel code (unused currently).
 * @retval B_OK            Deregistered (or already gone).
 * @retval B_BAD_THREAD_ID Thread not found.
 */
status_t
deselect_thread(int32 id, struct select_info* info, bool kernel)
{
	// get and lock the thread
	Thread* thread = Thread::GetAndLock(id);
	if (thread == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> threadReference(thread, true);
	ThreadLocker threadLocker(thread, true);

	// remove info from list
	select_info** infoLocation = &thread->select_infos;
	while (*infoLocation != NULL && *infoLocation != info)
		infoLocation = &(*infoLocation)->next;

	if (*infoLocation != info)
		return B_OK;

	*infoLocation = info->next;

	threadLocker.Unlock();

	// surrender sync reference
	put_select_sync(info->sync);

	return B_OK;
}


/**
 * @brief Return the maximum number of threads allowed system-wide.
 *
 * @return  Value of @c sMaxThreads.
 */
int32
thread_max_threads(void)
{
	return sMaxThreads;
}


/**
 * @brief Return the number of threads currently in use.
 *
 * @return  Value of @c sUsedThreads (read under the hash read lock).
 */
int32
thread_used_threads(void)
{
	InterruptsReadSpinLocker threadHashLocker(sThreadHashLock);
	return sUsedThreads;
}


/*!	Returns a user-readable string for a thread state.
	Only for use in the kernel debugger.
*/
/**
 * @brief Public wrapper exposing state_to_text() for use outside this file.
 *
 * @param thread  Thread whose wait sub-state may refine the string (may be NULL).
 * @param state   Raw thread state value.
 * @return        Human-readable state string.
 * @note For kernel debugger use only.
 */
const char*
thread_state_to_text(Thread* thread, int32 state)
{
	return state_to_text(thread, state);
}


/**
 * @brief Retrieve the effective I/O priority of a thread.
 *
 * If the thread's io_priority is negative the CPU scheduling priority is
 * returned instead.
 *
 * @param id  Thread ID.
 * @return    I/O priority value, or B_BAD_THREAD_ID if the thread is not found.
 */
int32
thread_get_io_priority(thread_id id)
{
	Thread* thread = Thread::GetAndLock(id);
	if (thread == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> threadReference(thread, true);
	ThreadLocker threadLocker(thread, true);

	int32 priority = thread->io_priority;
	if (priority < 0) {
		// negative I/O priority means using the (CPU) priority
		priority = thread->priority;
	}

	return priority;
}


/**
 * @brief Set the I/O priority of the current thread.
 *
 * @param priority  New I/O priority value (negative means "use CPU priority").
 */
void
thread_set_io_priority(int32 priority)
{
	Thread* thread = thread_get_current_thread();
	ThreadLocker threadLocker(thread);

	thread->io_priority = priority;
}


/**
 * @brief Initialise the kernel thread subsystem.
 *
 * Sets up the global thread hash table, the slab object cache (which also
 * pre-populates the kernel stack cache via create_kernel_stack), the
 * architecture thread layer, idle threads for each CPU, the undertaker
 * thread, and all kernel debugger commands related to threads.
 *
 * @param args  Kernel boot arguments (used for CPU count).
 * @retval B_OK  Initialisation completed successfully.
 * @note Panics on any critical failure rather than returning an error.
 */
status_t
thread_init(kernel_args *args)
{
	TRACE(("thread_init: entry\n"));

	// create the thread hash table
	new(&sThreadHash) ThreadHashTable();
	if (sThreadHash.Init(128) != B_OK)
		panic("thread_init(): failed to init thread hash table!");

	// create the thread structure object cache
	sThreadCache = create_object_cache_etc("threads", sizeof(Thread), 64,
		0, 0, 0, 0, NULL, create_kernel_stack, destroy_kernel_stack, NULL);
		// Note: The x86 port requires 64 byte alignment of thread structures.
	if (sThreadCache == NULL)
		panic("thread_init(): failed to allocate thread object cache!");

	if (arch_thread_init(args) < B_OK)
		panic("arch_thread_init() failed!\n");

	// skip all thread IDs including B_SYSTEM_TEAM, which is reserved
	sNextThreadID = B_SYSTEM_TEAM + 1;

	// create an idle thread for each cpu
	for (uint32 i = 0; i < args->num_cpus; i++) {
		Thread *thread;
		area_info info;
		char name[64];

		sprintf(name, "idle thread %" B_PRIu32, i + 1);
		thread = new(&sIdleThreads[i]) Thread(name,
			i == 0 ? team_get_kernel_team_id() : -1, &gCPU[i]);
		if (thread == NULL || thread->Init(true) != B_OK) {
			panic("error creating idle thread struct\n");
			return B_NO_MEMORY;
		}

		gCPU[i].running_thread = thread;

		thread->team = team_get_kernel_team();
		thread->priority = B_IDLE_PRIORITY;
		thread->state = B_THREAD_RUNNING;

		sprintf(name, "idle thread %" B_PRIu32 " kstack", i + 1);
		thread->kernel_stack_area = find_area(name);

		if (get_area_info(thread->kernel_stack_area, &info) != B_OK)
			panic("error finding idle kstack area\n");

		thread->kernel_stack_base = (addr_t)info.address;
		thread->kernel_stack_top = thread->kernel_stack_base + info.size;

		thread->visible = true;
		insert_thread_into_team(thread->team, thread);

		scheduler_on_thread_init(thread);
	}
	sUsedThreads = args->num_cpus;

	// init the notification service
	new(&sNotificationService) ThreadNotificationService();

	sNotificationService.Register();

	// start the undertaker thread
	new(&sUndertakerEntries) DoublyLinkedList<UndertakerEntry>();
	sUndertakerCondition.Init(&sUndertakerEntries, "undertaker entries");

	thread_id undertakerThread = spawn_kernel_thread(&undertaker, "undertaker",
		B_DISPLAY_PRIORITY, NULL);
	if (undertakerThread < 0)
		panic("Failed to create undertaker thread!");
	resume_thread(undertakerThread);

	// set up some debugger commands
	add_debugger_command_etc("threads", &dump_thread_list, "List all threads",
		"[ <team> ]\n"
		"Prints a list of all existing threads, or, if a team ID is given,\n"
		"all threads of the specified team.\n"
		"  <team>  - The ID of the team whose threads shall be listed.\n", 0);
	add_debugger_command_etc("ready", &dump_thread_list,
		"List all ready threads",
		"\n"
		"Prints a list of all threads in ready state.\n", 0);
	add_debugger_command_etc("running", &dump_thread_list,
		"List all running threads",
		"\n"
		"Prints a list of all threads in running state.\n", 0);
	add_debugger_command_etc("waiting", &dump_thread_list,
		"List all waiting threads (optionally for a specific semaphore)",
		"[ <sem> ]\n"
		"Prints a list of all threads in waiting state. If a semaphore is\n"
		"specified, only the threads waiting on that semaphore are listed.\n"
		"  <sem>  - ID of the semaphore.\n", 0);
	add_debugger_command_etc("realtime", &dump_thread_list,
		"List all realtime threads",
		"\n"
		"Prints a list of all threads with realtime priority.\n", 0);
	add_debugger_command_etc("thread", &dump_thread_info,
		"Dump info about a particular thread",
		"[ -s ] ( <id> | <address> | <name> )*\n"
		"Prints information about the specified thread. If no argument is\n"
		"given the current thread is selected.\n"
		"  -s         - Print info in compact table form (like \"threads\").\n"
		"  <id>       - The ID of the thread.\n"
		"  <address>  - The address of the thread structure.\n"
		"  <name>     - The thread's name.\n", 0);
	add_debugger_command_etc("calling", &dump_thread_list,
		"Show all threads that have a specific address in their call chain",
		"{ <symbol-pattern> | <start> <end> }\n", 0);
	add_debugger_command_etc("unreal", &make_thread_unreal,
		"Set realtime priority threads to normal priority",
		"[ <id> ]\n"
		"Sets the priority of all realtime threads or, if given, the one\n"
		"with the specified ID to \"normal\" priority.\n"
		"  <id>  - The ID of the thread.\n", 0);
	add_debugger_command_etc("suspend", &make_thread_suspended,
		"Suspend a thread",
		"[ <id> ]\n"
		"Suspends the thread with the given ID. If no ID argument is given\n"
		"the current thread is selected.\n"
		"  <id>  - The ID of the thread.\n", 0);
	add_debugger_command_etc("resume", &make_thread_resumed, "Resume a thread",
		"<id>\n"
		"Resumes the specified thread, if it is currently suspended.\n"
		"  <id>  - The ID of the thread.\n", 0);
	add_debugger_command_etc("drop", &drop_into_debugger,
		"Drop a thread into the userland debugger",
		"<id>\n"
		"Drops the specified (userland) thread into the userland debugger\n"
		"after leaving the kernel debugger.\n"
		"  <id>  - The ID of the thread.\n", 0);
	add_debugger_command_etc("priority", &set_thread_prio,
		"Set a thread's priority",
		"<priority> [ <id> ]\n"
		"Sets the priority of the thread with the specified ID to the given\n"
		"priority. If no thread ID is given, the current thread is selected.\n"
		"  <priority>  - The thread's new priority (0 - 120)\n"
		"  <id>        - The ID of the thread.\n", 0);

	return B_OK;
}


/**
 * @brief Per-CPU pre-boot initialisation: wire the idle thread to its CPU.
 *
 * Sets the cpu pointer on the not-yet-fully-initialised idle thread for
 * @p cpuNum so that get_current_cpu() and related helpers work correctly
 * during early boot before thread_init() completes.
 *
 * @param args    Kernel boot arguments (unused here).
 * @param cpuNum  Zero-based CPU index being initialised.
 * @retval B_OK  Always.
 */
status_t
thread_preboot_init_percpu(struct kernel_args *args, int32 cpuNum)
{
	// set up the cpu pointer in the not yet initialized per-cpu idle thread
	// so that get_current_cpu and friends will work, which is crucial for
	// a lot of low level routines
	sIdleThreads[cpuNum].cpu = &gCPU[cpuNum];
	arch_thread_set_current_thread(&sIdleThreads[cpuNum]);
	return B_OK;
}


//	#pragma mark - thread blocking API


/**
 * @brief Timer callback that unblocks a thread with B_TIMED_OUT status.
 *
 * Installed as a one-shot timer by thread_block_with_timeout().  Clears
 * @c timer->user_data to signal the timer fired before the wait returned.
 *
 * @param timer  The kernel timer that fired; user_data points to the Thread.
 * @return       B_HANDLED_INTERRUPT.
 */
static int32
thread_block_timeout(timer* timer)
{
	Thread* thread = (Thread*)timer->user_data;
	thread_unblock(thread, B_TIMED_OUT);

	timer->user_data = NULL;
	return B_HANDLED_INTERRUPT;
}


/*!	Blocks the current thread.

	The thread is blocked until someone else unblock it. Must be called after a
	call to thread_prepare_to_block(). If the thread has already been unblocked
	after the previous call to thread_prepare_to_block(), this function will
	return immediately. Cf. the documentation of thread_prepare_to_block() for
	more details.

	The caller must hold the scheduler lock.

	\param thread The current thread.
	\return The error code passed to the unblocking function. thread_interrupt()
		uses \c B_INTERRUPTED. By convention \c B_OK means that the wait was
		successful while another error code indicates a failure (what that means
		depends on the client code).
*/
/**
 * @brief Block the current thread on the scheduler lock (internal).
 *
 * If the wait status is still 1 (meaning no one has unblocked us yet) this
 * function checks for pending interruptible signals and, if none, calls
 * scheduler_reschedule(B_THREAD_WAITING).
 *
 * @param thread  The current thread (must equal thread_get_current_thread()).
 * @return        The status code set by thread_unblock_locked() or
 *                B_INTERRUPTED if a signal was pending.
 * @note The caller must hold @c thread->scheduler_lock.
 */
static inline status_t
thread_block_locked(Thread* thread)
{
	if (thread->wait.status == 1) {
		// check for signals, if interruptible
		if (thread_is_interrupted(thread, thread->wait.flags)) {
			thread->wait.status = B_INTERRUPTED;
		} else
			scheduler_reschedule(B_THREAD_WAITING);
	}

	return thread->wait.status;
}


/*!	Blocks the current thread.

	The function acquires the scheduler lock and calls thread_block_locked().
	See there for more information.
*/
/**
 * @brief Block the current thread, acquiring the scheduler lock internally.
 *
 * @return  Status code from thread_block_locked().
 */
status_t
thread_block()
{
	InterruptsSpinLocker _(thread_get_current_thread()->scheduler_lock);
	return thread_block_locked(thread_get_current_thread());
}


/*!	Blocks the current thread with a timeout.

	The current thread is blocked until someone else unblock it or the specified
	timeout occurs. Must be called after a call to thread_prepare_to_block(). If
	the thread has already been unblocked after the previous call to
	thread_prepare_to_block(), this function will return immediately. See
	thread_prepare_to_block() for more details.

	The caller must not hold the scheduler lock.

	\param timeoutFlags The standard timeout flags:
		- \c B_RELATIVE_TIMEOUT: \a timeout specifies the time to wait.
		- \c B_ABSOLUTE_TIMEOUT: \a timeout specifies the absolute end time when
			the timeout shall occur.
		- \c B_TIMEOUT_REAL_TIME_BASE: Only relevant when \c B_ABSOLUTE_TIMEOUT
			is specified, too. Specifies that \a timeout is a real time, not a
			system time.
		If neither \c B_RELATIVE_TIMEOUT nor \c B_ABSOLUTE_TIMEOUT are
		specified, an infinite timeout is implied and the function behaves like
		thread_block_locked().
	\return The error code passed to the unblocking function. thread_interrupt()
		uses \c B_INTERRUPTED. When the timeout occurred, \c B_TIMED_OUT is
		returned. By convention \c B_OK means that the wait was successful while
		another error code indicates a failure (what that means depends on the
		client code).
*/
/**
 * @brief Block the current thread with a timeout.
 *
 * Installs a kernel timer for the specified timeout (if any), calls
 * thread_block_locked(), then cancels the timer if it did not fire.
 *
 * @param timeoutFlags  B_RELATIVE_TIMEOUT / B_ABSOLUTE_TIMEOUT /
 *                      B_TIMEOUT_REAL_TIME_BASE flags.
 * @param timeout       Timeout duration or absolute deadline in microseconds.
 * @retval B_OK         Unblocked successfully before the timeout.
 * @retval B_TIMED_OUT  Timeout expired.
 * @retval B_INTERRUPTED Wait was interrupted by a signal.
 * @note The caller must not hold the scheduler lock.
 */
status_t
thread_block_with_timeout(uint32 timeoutFlags, bigtime_t timeout)
{
	Thread* thread = thread_get_current_thread();

	InterruptsSpinLocker locker(thread->scheduler_lock);

	if (thread->wait.status != 1)
		return thread->wait.status;

	bool useTimer = (timeoutFlags & (B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT)) != 0
		&& timeout != B_INFINITE_TIMEOUT;

	if (useTimer) {
		// Timer flags: absolute/relative.
		uint32 timerFlags;
		if ((timeoutFlags & B_RELATIVE_TIMEOUT) != 0) {
			timerFlags = B_ONE_SHOT_RELATIVE_TIMER;
		} else {
			timerFlags = B_ONE_SHOT_ABSOLUTE_TIMER;
			if ((timeoutFlags & B_TIMEOUT_REAL_TIME_BASE) != 0)
				timerFlags |= B_TIMER_REAL_TIME_BASE;
		}

		// install the timer
		thread->wait.unblock_timer.user_data = thread;
		add_timer(&thread->wait.unblock_timer, &thread_block_timeout, timeout,
			timerFlags);
	}

	status_t error = thread_block_locked(thread);

	locker.Unlock();

	// cancel timer, if it didn't fire
	if (useTimer && thread->wait.unblock_timer.user_data != NULL)
		cancel_timer(&thread->wait.unblock_timer);

	return error;
}


/*!	Unblocks a thread.

	Acquires the scheduler lock and calls thread_unblock_locked().
	See there for more information.
*/
/**
 * @brief Unblock a thread, acquiring the scheduler lock internally.
 *
 * @param thread  Thread to unblock.
 * @param status  Status code to set as the wait result (e.g. B_OK, B_INTERRUPTED).
 */
void
thread_unblock(Thread* thread, status_t status)
{
	InterruptsSpinLocker locker(thread->scheduler_lock);
	thread_unblock_locked(thread, status);
}


/*!	Unblocks a userland-blocked thread.
	The caller must not hold any locks.
*/
/**
 * @brief Unblock a thread that is blocked via the userland _user_block_thread() syscall.
 *
 * Reads wait_status from the thread's user_thread structure; if > 0 the
 * thread is still waiting and is unblocked with @p status.
 *
 * @param threadID  ID of the thread to unblock.
 * @param status    Status code to deliver to the blocked thread.
 * @retval B_OK            Thread unblocked (or already unblocked).
 * @retval B_BAD_THREAD_ID Thread not found.
 * @retval B_NOT_ALLOWED   Thread has no user_thread (is a kernel thread).
 * @retval B_BAD_ADDRESS   user_memcpy of wait_status failed.
 * @note The caller must not hold any locks.
 */
static status_t
user_unblock_thread(thread_id threadID, status_t status)
{
	// get the thread
	Thread* thread = Thread::GetAndLock(threadID);
	if (thread == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> threadReference(thread, true);
	ThreadLocker threadLocker(thread, true);

	if (thread->user_thread == NULL)
		return B_NOT_ALLOWED;

	InterruptsSpinLocker locker(thread->scheduler_lock);

	status_t waitStatus;
	if (user_memcpy(&waitStatus, &thread->user_thread->wait_status,
			sizeof(waitStatus)) < B_OK) {
		return B_BAD_ADDRESS;
	}
	if (waitStatus > 0) {
		if (user_memcpy(&thread->user_thread->wait_status, &status,
				sizeof(status)) < B_OK) {
			return B_BAD_ADDRESS;
		}

		// Even if the user_thread->wait_status was > 0, it may be the
		// case that this thread is actually blocked on something else.
		if (thread->wait.status > 0
				&& thread->wait.type == THREAD_BLOCK_TYPE_USER) {
			thread_unblock_locked(thread, status);
		}
	}
	return B_OK;
}


/**
 * @brief Check whether the calling thread is allowed to manipulate another thread.
 *
 * Kernel callers always pass; userland callers must be in the same team as
 * the target, be root, or share the same real UID.  Kernel team threads are
 * never accessible from userland.
 *
 * @param currentThread  The thread attempting the operation.
 * @param thread         The thread being operated on.
 * @param kernel         true if the call originates from kernel code.
 * @return               true if the operation is permitted.
 */
static bool
thread_check_permissions(const Thread* currentThread, const Thread* thread,
	bool kernel)
{
	if (kernel)
		return true;

	if (thread->team->id == team_get_kernel_team_id())
		return false;

	if (thread->team == currentThread->team
			|| currentThread->team->effective_uid == 0
			|| thread->team->real_uid == currentThread->team->real_uid)
		return true;

	return false;
}


/**
 * @brief Send a signal to a thread, checking permissions.
 *
 * @param id          Target thread ID.
 * @param number      Signal number to send.
 * @param signalCode  SI_* signal code (e.g. SI_USER).
 * @param errorCode   errno value associated with the signal.
 * @param kernel      true if the caller is kernel code.
 * @retval B_OK            Signal sent.
 * @retval B_BAD_THREAD_ID Thread not found.
 * @retval B_BAD_VALUE     @p id is <= 0.
 * @retval B_NOT_ALLOWED   Permission check failed.
 */
static status_t
thread_send_signal(thread_id id, uint32 number, int32 signalCode,
	int32 errorCode, bool kernel)
{
	if (id <= 0)
		return B_BAD_VALUE;

	Thread* currentThread = thread_get_current_thread();
	Thread* thread = Thread::Get(id);
	if (thread == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> threadReference(thread, true);

	// check whether sending the signal is allowed
	if (!thread_check_permissions(currentThread, thread, kernel))
		return B_NOT_ALLOWED;

	Signal signal(number, signalCode, errorCode, currentThread->team->id);
	return send_signal_to_thread(thread, signal, 0);
}


//	#pragma mark - public kernel API


/**
 * @brief Set the exit status of the current thread and initiate thread termination.
 *
 * For user threads, sets the team exit info if this is the main thread and
 * sends SIGKILLTHR to itself.  For kernel threads, calls thread_exit()
 * directly.
 *
 * @param returnValue  Exit status code to store in thread->exit.status.
 */
void
exit_thread(status_t returnValue)
{
	Thread *thread = thread_get_current_thread();
	Team* team = thread->team;

	thread->exit.status = returnValue;

	// if called from a kernel thread, we don't deliver the signal,
	// we just exit directly to keep the user space behaviour of
	// this function
	if (team != team_get_kernel_team()) {
		// If this is its main thread, set the team's exit status.
		if (thread == team->main_thread) {
			TeamLocker teamLocker(team);

			if (!team->exit.initialized) {
				team->exit.reason = CLD_EXITED;
				team->exit.signal = 0;
				team->exit.signaling_user = 0;
				team->exit.status = returnValue;
				team->exit.initialized = true;
			}

			teamLocker.Unlock();
		}

		Signal signal(SIGKILLTHR, SI_USER, B_OK, team->id);
		send_signal_to_thread(thread, signal, B_DO_NOT_RESCHEDULE);
	} else
		thread_exit();
}


/**
 * @brief Send the kill signal to a thread, optionally checking permissions.
 *
 * @param id      Target thread ID.
 * @param kernel  true if the caller is kernel code (bypasses permission check).
 * @return        Result of thread_send_signal().
 */
static status_t
thread_kill_thread(thread_id id, bool kernel)
{
	return thread_send_signal(id, SIGKILLTHR, SI_USER, B_OK, kernel);
}


/**
 * @brief Kill a thread by ID (kernel API, no permission check).
 *
 * @param id  Thread ID to kill.
 * @return    B_OK on success, or an error code.
 */
status_t
kill_thread(thread_id id)
{
	return thread_kill_thread(id, true);
}


/**
 * @brief Send a data message to another thread (kernel API, non-blocking variant).
 *
 * @param thread      Target thread ID.
 * @param code        Integer message code.
 * @param buffer      Pointer to the message payload.
 * @param bufferSize  Size of the payload in bytes.
 * @return            B_OK on success, or an error code.
 */
status_t
send_data(thread_id thread, int32 code, const void *buffer, size_t bufferSize)
{
	return send_data_etc(thread, code, buffer, bufferSize, 0);
}


/**
 * @brief Receive a data message sent to the current thread (kernel API).
 *
 * @param sender      Receives the sender's thread ID.
 * @param buffer      Destination buffer for the message payload.
 * @param bufferSize  Maximum bytes to copy.
 * @return            Message code, or a negative error code.
 */
int32
receive_data(thread_id *sender, void *buffer, size_t bufferSize)
{
	return receive_data_etc(sender, buffer, bufferSize, 0);
}


/**
 * @brief Check whether a thread has a pending data message.
 *
 * @param id      Thread ID to inspect.
 * @param kernel  true if the caller is kernel code (relaxes team check).
 * @return        true if a message is pending.
 */
static bool
thread_has_data(thread_id id, bool kernel)
{
	Thread* currentThread = thread_get_current_thread();
	Thread* thread;
	BReference<Thread> threadReference;
	if (id == currentThread->id) {
		thread = currentThread;
	} else {
		thread = Thread::Get(id);
		if (thread == NULL)
			return false;

		threadReference.SetTo(thread, true);
	}

	if (!kernel && thread->team != currentThread->team)
		return false;

	int32 count;
	if (get_sem_count(thread->msg.read_sem, &count) != B_OK)
		return false;

	return count == 0 ? false : true;
}


/**
 * @brief Check whether the specified thread has a pending data message (kernel API).
 *
 * @param thread  Thread ID to inspect.
 * @return        true if a message is pending.
 */
bool
has_data(thread_id thread)
{
	return thread_has_data(thread, true);
}


/**
 * @brief Fill a thread_info structure for the thread with the given ID.
 *
 * @param id    Thread ID to query.
 * @param info  Destination thread_info structure.
 * @param size  Must equal sizeof(thread_info).
 * @retval B_OK            Structure filled.
 * @retval B_BAD_VALUE     @p info is NULL, @p size is wrong, or @p id < 0.
 * @retval B_BAD_THREAD_ID Thread not found.
 */
status_t
_get_thread_info(thread_id id, thread_info *info, size_t size)
{
	if (info == NULL || size != sizeof(thread_info) || id < B_OK)
		return B_BAD_VALUE;

	// get the thread
	Thread* thread = Thread::GetAndLock(id);
	if (thread == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> threadReference(thread, true);
	ThreadLocker threadLocker(thread, true);

	// fill the info -- also requires the scheduler lock to be held
	InterruptsSpinLocker locker(thread->scheduler_lock);

	fill_thread_info(thread, info, size);

	return B_OK;
}


/**
 * @brief Iterate over all threads in a team, returning one thread_info per call.
 *
 * @param teamID   Team ID whose threads are being enumerated.
 * @param _cookie  In/out iteration cookie; set to 0 before the first call.
 * @param info     Destination thread_info structure.
 * @param size     Must equal sizeof(thread_info).
 * @retval B_OK       Structure filled; advance @p _cookie for the next call.
 * @retval B_BAD_VALUE Iteration complete, or invalid arguments.
 */
status_t
_get_next_thread_info(team_id teamID, int32 *_cookie, thread_info *info,
	size_t size)
{
	if (info == NULL || size != sizeof(thread_info) || teamID < 0)
		return B_BAD_VALUE;

	int32 lastID = *_cookie;

	// get the team
	Team* team = Team::GetAndLock(teamID);
	if (team == NULL)
		return B_BAD_VALUE;
	BReference<Team> teamReference(team, true);
	TeamLocker teamLocker(team, true);

	Thread* thread = NULL;

	if (lastID == 0) {
		// We start with the main thread.
		thread = team->main_thread;
	} else {
		// Find the previous thread after the one with the last ID.
		bool found = false;
		for (Thread* previous = team->thread_list.Last(); previous != NULL;
				previous = team->thread_list.GetPrevious(previous)) {
			if (previous->id == lastID) {
				found = true;
				thread = team->thread_list.GetPrevious(previous);
				break;
			}
		}

		if (!found) {
			// Fall back to finding the thread with the next greatest ID (as long
			// as IDs don't wrap, they are always sorted from highest to lowest).
			// This won't work properly if IDs wrap, or for the kernel team (to
			// which threads are added when they are dying), but this is only a
			// fallback for when the previous thread wasn't found, anyway.
			for (Thread* next = team->thread_list.First(); next != NULL;
					next = team->thread_list.GetNext(next)) {
				if (next->id <= lastID)
					break;

				thread = next;
			}
		}
	}

	if (thread == NULL)
		return B_BAD_VALUE;

	lastID = thread->id;
	*_cookie = lastID;

	ThreadLocker threadLocker(thread);
	InterruptsSpinLocker locker(thread->scheduler_lock);

	fill_thread_info(thread, info, size);

	return B_OK;
}


/**
 * @brief Find a thread by name, returning its ID.
 *
 * If @p name is NULL, returns the calling thread's ID.  Scans the global
 * hash table under a read spinlock.
 *
 * @param name  Thread name to search for, or NULL for the current thread.
 * @return      Thread ID, or B_NAME_NOT_FOUND if no match.
 */
thread_id
find_thread(const char* name)
{
	if (name == NULL)
		return thread_get_current_thread_id();

	InterruptsReadSpinLocker threadHashLocker(sThreadHashLock);

	// Scanning the whole hash with the thread hash lock held isn't exactly
	// cheap, but since this function is probably used very rarely, and we
	// only need a read lock, it's probably acceptable.

	for (ThreadHashTable::Iterator it = sThreadHash.GetIterator();
			Thread* thread = it.Next();) {
		if (!thread->visible)
			continue;

		if (strcmp(thread->name, name) == 0)
			return thread->id;
	}

	return B_NAME_NOT_FOUND;
}


/**
 * @brief Rename a thread.
 *
 * Only threads within the same team as the calling thread may be renamed.
 * Notifies THREAD_NAME_CHANGED listeners after the rename.
 *
 * @param id    Thread ID to rename.
 * @param name  New name string (must be non-NULL).
 * @retval B_OK            Thread renamed.
 * @retval B_BAD_VALUE     @p name is NULL.
 * @retval B_BAD_THREAD_ID Thread not found.
 * @retval B_NOT_ALLOWED   Target thread belongs to a different team.
 */
status_t
rename_thread(thread_id id, const char* name)
{
	if (name == NULL)
		return B_BAD_VALUE;

	// get the thread
	Thread* thread = Thread::GetAndLock(id);
	if (thread == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> threadReference(thread, true);
	ThreadLocker threadLocker(thread, true);

	// check whether the operation is allowed
	if (thread->team != thread_get_current_thread()->team)
		return B_NOT_ALLOWED;

	strlcpy(thread->name, name, B_OS_NAME_LENGTH);

	team_id teamID = thread->team->id;

	threadLocker.Unlock();

	// notify listeners
	sNotificationService.Notify(THREAD_NAME_CHANGED, teamID, id);
		// don't pass the thread structure, as it's unsafe, if it isn't ours

	return B_OK;
}


/**
 * @brief Change the scheduling priority of a thread, with permission checking.
 *
 * Clamps @p priority to [THREAD_MIN_SET_PRIORITY, THREAD_MAX_SET_PRIORITY]
 * before applying.  Idle threads cannot be reprioritised.
 *
 * @param id       Thread ID.
 * @param priority New scheduling priority.
 * @param kernel   true if the caller is kernel code (bypasses permission check).
 * @retval B_OK            Priority changed.
 * @retval B_BAD_THREAD_ID Thread not found.
 * @retval B_NOT_ALLOWED   Permission denied or target is an idle thread.
 */
static status_t
thread_set_thread_priority(thread_id id, int32 priority, bool kernel)
{
	// make sure the passed in priority is within bounds
	if (priority > THREAD_MAX_SET_PRIORITY)
		priority = THREAD_MAX_SET_PRIORITY;
	if (priority < THREAD_MIN_SET_PRIORITY)
		priority = THREAD_MIN_SET_PRIORITY;

	// get the thread
	Thread* thread = Thread::GetAndLock(id);
	if (thread == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> threadReference(thread, true);
	ThreadLocker threadLocker(thread, true);

	// check whether the change is allowed
	if (thread_is_idle_thread(thread) || !thread_check_permissions(
			thread_get_current_thread(), thread, kernel))
		return B_NOT_ALLOWED;

	return scheduler_set_thread_priority(thread, priority);
}


/**
 * @brief Set the scheduling priority of a thread (kernel API, no permission check).
 *
 * @param id       Thread ID.
 * @param priority New scheduling priority.
 * @return         Result of thread_set_thread_priority().
 */
status_t
set_thread_priority(thread_id id, int32 priority)
{
	return thread_set_thread_priority(id, priority, true);
}


/**
 * @brief Sleep for a given duration on the specified clock (kernel API).
 *
 * @param timeout   Sleep duration or absolute deadline in microseconds.
 * @param timebase  POSIX clock ID (e.g. B_SYSTEM_TIMEBASE / CLOCK_MONOTONIC).
 * @param flags     B_RELATIVE_TIMEOUT or B_ABSOLUTE_TIMEOUT.
 * @return          B_OK on normal completion, or an error/interrupt code.
 */
status_t
snooze_etc(bigtime_t timeout, int timebase, uint32 flags)
{
	return common_snooze_etc(timeout, timebase, flags, NULL);
}


/*!	snooze() for internal kernel use only; doesn't interrupt on signals. */
/**
 * @brief Sleep for a relative duration (kernel-only, not interruptible by signals).
 *
 * @param timeout  Duration to sleep in microseconds.
 * @return         B_OK on normal completion.
 */
status_t
snooze(bigtime_t timeout)
{
	return snooze_etc(timeout, B_SYSTEM_TIMEBASE, B_RELATIVE_TIMEOUT);
}


/*!	snooze_until() for internal kernel use only; doesn't interrupt on
	signals.
*/
/**
 * @brief Sleep until an absolute time on the given clock (kernel-only).
 *
 * @param timeout   Absolute wake-up time in microseconds.
 * @param timebase  POSIX clock ID.
 * @return          B_OK on normal completion.
 */
status_t
snooze_until(bigtime_t timeout, int timebase)
{
	return snooze_etc(timeout, timebase, B_ABSOLUTE_TIMEOUT);
}


/**
 * @brief Wait for a thread to exit (kernel API, no timeout, no flags).
 *
 * @param thread       Thread ID to wait for.
 * @param _returnCode  Receives the thread's exit status (may be NULL).
 * @return             B_OK, or an error code from wait_for_thread_etc().
 */
status_t
wait_for_thread(thread_id thread, status_t *_returnCode)
{
	return wait_for_thread_etc(thread, 0, 0, _returnCode);
}


/**
 * @brief Suspend a thread via SIGSTOP, with permission checking.
 *
 * @param id      Thread ID.
 * @param kernel  true if the caller is kernel code.
 * @return        Result of thread_send_signal().
 */
static status_t
thread_suspend_thread(thread_id id, bool kernel)
{
	return thread_send_signal(id, SIGSTOP, SI_USER, B_OK, kernel);
}


/**
 * @brief Suspend a thread (kernel API, no permission check).
 *
 * @param id  Thread ID to suspend.
 * @return    B_OK on success, or an error code.
 */
status_t
suspend_thread(thread_id id)
{
	return thread_suspend_thread(id, true);
}


/**
 * @brief Resume a thread via SIGNAL_CONTINUE_THREAD, with permission checking.
 *
 * Using the kernel-internal SIGNAL_CONTINUE_THREAD retains BeOS compatibility
 * in that the combination of suspend_thread()/resume_thread() interrupts
 * threads waiting on semaphores.
 *
 * @param id      Thread ID.
 * @param kernel  true if the caller is kernel code.
 * @return        Result of thread_send_signal().
 */
static status_t
thread_resume_thread(thread_id id, bool kernel)
{
	// Using the kernel internal SIGNAL_CONTINUE_THREAD signal retains
	// compatibility to BeOS which documents the combination of suspend_thread()
	// and resume_thread() to interrupt threads waiting on semaphores.
	return thread_send_signal(id, SIGNAL_CONTINUE_THREAD, SI_USER, B_OK, kernel);
}


/**
 * @brief Resume a thread (kernel API, no permission check).
 *
 * @param id  Thread ID to resume.
 * @return    B_OK on success, or an error code.
 */
status_t
resume_thread(thread_id id)
{
	return thread_resume_thread(id, true);
}


/**
 * @brief Create and register a kernel thread in the kernel team.
 *
 * Convenience wrapper around thread_create_thread() that targets the
 * kernel team.
 *
 * @param function  Kernel entry function.
 * @param name      Thread name.
 * @param priority  Scheduling priority.
 * @param arg       Argument passed to @p function.
 * @retval >= 0  Thread ID of the newly created (suspended) thread.
 * @retval < 0   Error from thread_create_thread().
 */
thread_id
spawn_kernel_thread(thread_func function, const char *name, int32 priority,
	void *arg)
{
	return thread_create_thread(
		ThreadCreationAttributes(function, name, priority, arg),
		true);
}


/**
 * @brief POSIX getrlimit() — retrieve a resource limit.
 *
 * @param resource  POSIX resource identifier.
 * @param rlp       Destination rlimit structure.
 * @return          0 on success, -1 on error (errno set).
 */
int
getrlimit(int resource, struct rlimit * rlp)
{
	status_t error = common_getrlimit(resource, rlp);
	if (error != B_OK) {
		errno = error;
		return -1;
	}

	return 0;
}


/**
 * @brief POSIX setrlimit() — set a resource limit.
 *
 * @param resource  POSIX resource identifier.
 * @param rlp       New resource limit to apply.
 * @return          0 on success, -1 on error (errno set).
 */
int
setrlimit(int resource, const struct rlimit * rlp)
{
	status_t error = common_setrlimit(resource, rlp);
	if (error != B_OK) {
		errno = error;
		return -1;
	}

	return 0;
}


//	#pragma mark - syscalls


/**
 * @brief Syscall: exit the current thread with the given return value.
 *
 * @param returnValue  Exit status to store in thread->exit.status.
 */
void
_user_exit_thread(status_t returnValue)
{
	exit_thread(returnValue);
}


/**
 * @brief Syscall: kill (send SIGKILLTHR to) a thread by ID.
 *
 * @param thread  Target thread ID.
 * @return        B_OK on success, or an error code.
 */
status_t
_user_kill_thread(thread_id thread)
{
	return thread_kill_thread(thread, false);
}


/**
 * @brief Syscall: register a cancel function and send SIGNAL_CANCEL_THREAD.
 *
 * The cancel function is stored on the target thread and will be called
 * when the thread processes the cancellation signal.  Only threads within
 * the calling team may be cancelled.
 *
 * @param threadID        Target thread ID.
 * @param cancelFunction  Userland cancel function pointer (must be a valid
 *                        user-space address).
 * @retval B_OK            Cancel signal sent.
 * @retval B_BAD_VALUE     @p cancelFunction is NULL or not a user address.
 * @retval B_BAD_THREAD_ID Thread not found.
 * @retval B_NOT_ALLOWED   Thread belongs to a different team.
 */
status_t
_user_cancel_thread(thread_id threadID, void (*cancelFunction)(int))
{
	// check the cancel function
	if (cancelFunction == NULL || !IS_USER_ADDRESS(cancelFunction))
		return B_BAD_VALUE;

	// get and lock the thread
	Thread* thread = Thread::GetAndLock(threadID);
	if (thread == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> threadReference(thread, true);
	ThreadLocker threadLocker(thread, true);

	// only threads of the same team can be canceled
	if (thread->team != thread_get_current_thread()->team)
		return B_NOT_ALLOWED;

	// set the cancel function
	thread->cancel_function = cancelFunction;

	// send the cancellation signal to the thread
	InterruptsReadSpinLocker teamLocker(thread->team_lock);
	SpinLocker locker(thread->team->signal_lock);
	return send_signal_to_thread_locked(thread, SIGNAL_CANCEL_THREAD, NULL, 0);
}


/**
 * @brief Syscall: resume a suspended thread (with permission check).
 *
 * @param thread  Thread ID to resume.
 * @return        B_OK on success, or an error code.
 */
status_t
_user_resume_thread(thread_id thread)
{
	return thread_resume_thread(thread, false);
}


/**
 * @brief Syscall: suspend a thread (with permission check).
 *
 * @param thread  Thread ID to suspend.
 * @return        B_OK on success, or an error code.
 */
status_t
_user_suspend_thread(thread_id thread)
{
	return thread_suspend_thread(thread, false);
}


/**
 * @brief Syscall: rename a thread, copying the name from userland.
 *
 * @param thread    Thread ID to rename.
 * @param userName  User-space pointer to the new name string.
 * @retval B_OK          Thread renamed.
 * @retval B_BAD_ADDRESS @p userName is not a valid user-space address.
 */
status_t
_user_rename_thread(thread_id thread, const char *userName)
{
	char name[B_OS_NAME_LENGTH];

	if (!IS_USER_ADDRESS(userName)
		|| userName == NULL
		|| user_strlcpy(name, userName, B_OS_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	// rename_thread() forbids thread renames across teams, so we don't
	// need a "kernel" flag here.
	return rename_thread(thread, name);
}


/**
 * @brief Syscall: set the scheduling priority of a thread (with permission check).
 *
 * @param thread      Thread ID.
 * @param newPriority New priority value.
 * @return            New priority, or an error code.
 */
int32
_user_set_thread_priority(thread_id thread, int32 newPriority)
{
	return thread_set_thread_priority(thread, newPriority, false);
}


/**
 * @brief Syscall: create a new userland thread from a userland thread_creation_attributes.
 *
 * Copies and validates the userland attributes, calls thread_create_thread(),
 * and notifies the debugger of the new thread.
 *
 * @param userAttributes  User-space pointer to thread_creation_attributes.
 * @retval >= 0  Thread ID of the new thread.
 * @retval < 0   Error from attribute validation or thread_create_thread().
 */
thread_id
_user_spawn_thread(thread_creation_attributes* userAttributes)
{
	// copy the userland structure to the kernel
	char nameBuffer[B_OS_NAME_LENGTH];
	ThreadCreationAttributes attributes;
	status_t error = attributes.InitFromUserAttributes(userAttributes,
		nameBuffer);
	if (error != B_OK)
		return error;

	// create the thread
	thread_id threadID = thread_create_thread(attributes, false);

	if (threadID >= 0)
		user_debug_thread_created(threadID);

	return threadID;
}


/**
 * @brief Syscall: sleep for a duration, with restart support on interruption.
 *
 * Converts relative timeouts to absolute ones (to survive clock changes),
 * calls common_snooze_etc(), and, if interrupted, stores restart parameters
 * and requests a syscall restart.
 *
 * @param timeout           Sleep duration or absolute deadline.
 * @param timebase          POSIX clock ID.
 * @param flags             B_RELATIVE_TIMEOUT / B_ABSOLUTE_TIMEOUT etc.
 * @param userRemainingTime If non-NULL and interrupted, receives remaining time.
 * @retval B_OK          Sleep completed normally.
 * @retval B_INTERRUPTED Sleep was interrupted; syscall restart was requested.
 * @retval B_BAD_ADDRESS @p userRemainingTime is not a valid user address.
 */
status_t
_user_snooze_etc(bigtime_t timeout, int timebase, uint32 flags,
	bigtime_t* userRemainingTime)
{
	// We need to store more syscall restart parameters than usual and need a
	// somewhat different handling. Hence we can't use
	// syscall_restart_handle_timeout_pre() but do the job ourselves.
	struct restart_parameters {
		bigtime_t	timeout;
		clockid_t	timebase;
		uint32		flags;
	};

	Thread* thread = thread_get_current_thread();

	if ((thread->flags & THREAD_FLAGS_SYSCALL_RESTARTED) != 0) {
		// The syscall was restarted. Fetch the parameters from the stored
		// restart parameters.
		restart_parameters* restartParameters
			= (restart_parameters*)thread->syscall_restart.parameters;
		timeout = restartParameters->timeout;
		timebase = restartParameters->timebase;
		flags = restartParameters->flags;
	} else {
		// convert relative timeouts to absolute ones
		if ((flags & B_RELATIVE_TIMEOUT) != 0) {
			// not restarted yet and the flags indicate a relative timeout

			// Make sure we use the system time base, so real-time clock changes
			// won't affect our wait.
			flags &= ~(uint32)B_TIMEOUT_REAL_TIME_BASE;
			if (timebase == CLOCK_REALTIME)
				timebase = CLOCK_MONOTONIC;

			// get the current time and make the timeout absolute
			bigtime_t now;
			status_t error = user_timer_get_clock(timebase, now);
			if (error != B_OK)
				return error;

			timeout += now;

			// deal with overflow
			if (timeout < 0)
				timeout = B_INFINITE_TIMEOUT;

			flags = (flags & ~B_RELATIVE_TIMEOUT) | B_ABSOLUTE_TIMEOUT;
		} else
			flags |= B_ABSOLUTE_TIMEOUT;
	}

	// snooze
	bigtime_t remainingTime;
	status_t error = common_snooze_etc(timeout, timebase,
		flags | B_CAN_INTERRUPT | B_CHECK_PERMISSION,
		userRemainingTime != NULL ? &remainingTime : NULL);

	// If interrupted, copy the remaining time back to userland and prepare the
	// syscall restart.
	if (error == B_INTERRUPTED) {
		if (userRemainingTime != NULL
			&& (!IS_USER_ADDRESS(userRemainingTime)
				|| user_memcpy(userRemainingTime, &remainingTime,
					sizeof(remainingTime)) != B_OK)) {
			return B_BAD_ADDRESS;
		}

		// store the normalized values in the restart parameters
		restart_parameters* restartParameters
			= (restart_parameters*)thread->syscall_restart.parameters;
		restartParameters->timeout = timeout;
		restartParameters->timebase = timebase;
		restartParameters->flags = flags;

		// restart the syscall, if possible
		atomic_or(&thread->flags, THREAD_FLAGS_RESTART_SYSCALL);
	}

	return error;
}


/**
 * @brief Syscall: voluntarily yield the CPU.
 */
void
_user_thread_yield(void)
{
	thread_yield();
}


/**
 * @brief Syscall: get thread_info for a thread by ID, copying to userland.
 *
 * @param id       Thread ID to query.
 * @param userInfo User-space destination pointer for the thread_info struct.
 * @retval B_OK          Info copied to userland.
 * @retval B_BAD_ADDRESS @p userInfo is not a valid user-space address.
 * @retval other         Error from _get_thread_info().
 */
status_t
_user_get_thread_info(thread_id id, thread_info *userInfo)
{
	thread_info info;
	status_t status;

	if (!IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	status = _get_thread_info(id, &info, sizeof(thread_info));

	if (status >= B_OK
		&& user_memcpy(userInfo, &info, sizeof(thread_info)) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


/**
 * @brief Syscall: iterate over threads in a team, returning one thread_info per call.
 *
 * @param team        Team ID.
 * @param userCookie  User-space in/out iteration cookie (set to 0 initially).
 * @param userInfo    User-space destination for the thread_info struct.
 * @retval B_OK          Info copied; advance cookie for the next call.
 * @retval B_BAD_ADDRESS A pointer is not a valid user address.
 * @retval B_BAD_VALUE   Iteration complete.
 */
status_t
_user_get_next_thread_info(team_id team, int32 *userCookie,
	thread_info *userInfo)
{
	status_t status;
	thread_info info;
	int32 cookie;

	if (!IS_USER_ADDRESS(userCookie) || !IS_USER_ADDRESS(userInfo)
		|| user_memcpy(&cookie, userCookie, sizeof(int32)) < B_OK)
		return B_BAD_ADDRESS;

	status = _get_next_thread_info(team, &cookie, &info, sizeof(thread_info));
	if (status < B_OK)
		return status;

	if (user_memcpy(userCookie, &cookie, sizeof(int32)) < B_OK
		|| user_memcpy(userInfo, &info, sizeof(thread_info)) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


/**
 * @brief Syscall: find a thread by name, copying the name from userland.
 *
 * @param userName  User-space pointer to the name string, or NULL for the
 *                  current thread.
 * @return          Thread ID, or B_BAD_ADDRESS / B_NAME_NOT_FOUND on error.
 */
thread_id
_user_find_thread(const char *userName)
{
	char name[B_OS_NAME_LENGTH];

	if (userName == NULL)
		return find_thread(NULL);

	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, sizeof(name)) < B_OK)
		return B_BAD_ADDRESS;

	return find_thread(name);
}


/**
 * @brief Syscall: wait for a thread to exit, with timeout and restart support.
 *
 * @param id               Thread ID to wait for.
 * @param flags            Standard timeout/interrupt flags.
 * @param timeout          Timeout value in microseconds.
 * @param userReturnCode   User-space pointer to receive the thread's exit status.
 * @retval B_OK            Thread exited; exit status copied to userland.
 * @retval B_BAD_ADDRESS   @p userReturnCode is not a valid user address.
 * @retval B_INTERRUPTED   Wait was interrupted.
 * @retval B_TIMED_OUT     Timeout expired.
 */
status_t
_user_wait_for_thread_etc(thread_id id, uint32 flags, bigtime_t timeout, status_t *userReturnCode)
{
	status_t returnCode;
	status_t status;

	if (userReturnCode != NULL && !IS_USER_ADDRESS(userReturnCode))
		return B_BAD_ADDRESS;

	syscall_restart_handle_timeout_pre(flags, timeout);

	status = wait_for_thread_etc(id, flags | B_CAN_INTERRUPT, timeout, &returnCode);

	if (status == B_OK && userReturnCode != NULL
		&& user_memcpy(userReturnCode, &returnCode, sizeof(status_t)) < B_OK) {
		return B_BAD_ADDRESS;
	}

	return syscall_restart_handle_timeout_post(status, timeout);
}


/**
 * @brief Syscall: check whether a thread has a pending data message.
 *
 * @param thread  Thread ID.
 * @return        true if a message is pending.
 */
bool
_user_has_data(thread_id thread)
{
	return thread_has_data(thread, false);
}


/**
 * @brief Syscall: send a data message to another thread from userland.
 *
 * @param thread      Target thread ID.
 * @param code        Integer message code.
 * @param buffer      User-space pointer to the message payload.
 * @param bufferSize  Size of the payload in bytes.
 * @retval B_OK           Message sent.
 * @retval B_BAD_ADDRESS  @p buffer is not a valid user address.
 */
status_t
_user_send_data(thread_id thread, int32 code, const void *buffer,
	size_t bufferSize)
{
	if (buffer != NULL && !IS_USER_ADDRESS(buffer))
		return B_BAD_ADDRESS;

	return send_data_etc(thread, code, buffer, bufferSize,
		B_KILL_CAN_INTERRUPT);
		// supports userland buffers
}


/**
 * @brief Syscall: receive a data message sent to the current thread from userland.
 *
 * @param _userSender  User-space pointer to receive the sender's thread ID.
 * @param buffer       User-space destination buffer for the message payload.
 * @param bufferSize   Maximum bytes to copy.
 * @retval >= 0        Message code; sender ID written to @p _userSender.
 * @retval B_BAD_ADDRESS A pointer is not a valid user address.
 */
status_t
_user_receive_data(thread_id *_userSender, void *buffer, size_t bufferSize)
{
	thread_id sender;
	status_t code;

	if ((!IS_USER_ADDRESS(_userSender) && _userSender != NULL)
		|| (!IS_USER_ADDRESS(buffer) && buffer != NULL)) {
		return B_BAD_ADDRESS;
	}

	code = receive_data_etc(&sender, buffer, bufferSize, B_KILL_CAN_INTERRUPT);
		// supports userland buffers

	if (_userSender != NULL)
		if (user_memcpy(_userSender, &sender, sizeof(thread_id)) < B_OK)
			return B_BAD_ADDRESS;

	return code;
}


/**
 * @brief Syscall: block the current thread until explicitly unblocked or timed out.
 *
 * Reads the wait_status from the thread's user_thread structure; if already
 * set (<=0) returns immediately.  Otherwise calls thread_block_with_timeout()
 * and reconciles the result with any concurrent unblock from another thread.
 *
 * @param flags    B_RELATIVE_TIMEOUT / B_ABSOLUTE_TIMEOUT / B_CAN_INTERRUPT flags.
 * @param timeout  Timeout duration or absolute deadline in microseconds.
 * @retval B_OK          Unblocked by another thread.
 * @retval B_TIMED_OUT   Timeout expired.
 * @retval B_INTERRUPTED Wait was interrupted by a signal.
 * @retval B_BAD_ADDRESS user_thread->wait_status is not accessible.
 */
status_t
_user_block_thread(uint32 flags, bigtime_t timeout)
{
	syscall_restart_handle_timeout_pre(flags, timeout);
	flags |= B_CAN_INTERRUPT;

	Thread* thread = thread_get_current_thread();
	ThreadLocker threadLocker(thread);

	// check, if already done
	status_t waitStatus;
	if (user_memcpy(&waitStatus, &thread->user_thread->wait_status,
			sizeof(waitStatus)) < B_OK) {
		return B_BAD_ADDRESS;
	}
	if (waitStatus <= 0)
		return waitStatus;

	// nope, so wait
	// Note: GCC 13 marks the following call as potentially overflowing, since it thinks `thread`
	//       may be `nullptr`. This cannot be the case in reality, therefore ignore this specific
	//       error.
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wstringop-overflow"
	thread_prepare_to_block(thread, flags, THREAD_BLOCK_TYPE_USER, NULL);
	#pragma GCC diagnostic pop

	threadLocker.Unlock();

	status_t status = thread_block_with_timeout(flags, timeout);

	threadLocker.Lock();

	// Interruptions or timeouts can race with other threads unblocking us.
	// Favor a wake-up by another thread, i.e. if someone changed the wait
	// status, use that.
	status_t oldStatus;
	if (user_memcpy(&oldStatus, &thread->user_thread->wait_status,
		sizeof(oldStatus)) < B_OK) {
		return B_BAD_ADDRESS;
	}
	if (oldStatus > 0) {
		if (user_memcpy(&thread->user_thread->wait_status, &status,
				sizeof(status)) < B_OK) {
			return B_BAD_ADDRESS;
		}
	} else {
		status = oldStatus;
	}

	threadLocker.Unlock();

	return syscall_restart_handle_timeout_post(status, timeout);
}


/**
 * @brief Syscall: unblock a single thread that is blocked via _user_block_thread().
 *
 * @param threadID  Thread ID to unblock.
 * @param status    Status code to deliver to the blocked thread.
 * @retval B_OK            Thread unblocked; reschedule requested if needed.
 * @retval B_BAD_THREAD_ID Thread not found.
 */
status_t
_user_unblock_thread(thread_id threadID, status_t status)
{
	status_t error = user_unblock_thread(threadID, status);

	if (error == B_OK)
		scheduler_reschedule_if_necessary();

	return error;
}


/**
 * @brief Syscall: unblock up to 128 userland-blocked threads in a single call.
 *
 * Copies a list of thread IDs from userland and calls user_unblock_thread()
 * for each, then triggers a reschedule if any were unblocked.
 *
 * @param userThreads  User-space array of thread IDs.
 * @param count        Number of thread IDs in @p userThreads (max 128).
 * @param status       Status code to deliver to each unblocked thread.
 * @retval B_OK          All unblock attempts completed.
 * @retval B_BAD_ADDRESS @p userThreads is not a valid user address.
 * @retval B_BAD_VALUE   @p count exceeds the maximum.
 */
status_t
_user_unblock_threads(thread_id* userThreads, uint32 count, status_t status)
{
	enum {
		MAX_USER_THREADS_TO_UNBLOCK	= 128
	};

	if (userThreads == NULL || !IS_USER_ADDRESS(userThreads))
		return B_BAD_ADDRESS;
	if (count > MAX_USER_THREADS_TO_UNBLOCK)
		return B_BAD_VALUE;

	thread_id threads[MAX_USER_THREADS_TO_UNBLOCK];
	if (user_memcpy(threads, userThreads, count * sizeof(thread_id)) != B_OK)
		return B_BAD_ADDRESS;

	for (uint32 i = 0; i < count; i++)
		user_unblock_thread(threads[i], status);

	scheduler_reschedule_if_necessary();

	return B_OK;
}


// TODO: the following two functions don't belong here


/**
 * @brief Syscall: getrlimit() — retrieve a resource limit, copying to userland.
 *
 * @param resource  POSIX resource identifier.
 * @param urlp      User-space destination rlimit pointer.
 * @return          0 on success, EINVAL or B_BAD_ADDRESS on error.
 */
int
_user_getrlimit(int resource, struct rlimit *urlp)
{
	struct rlimit rl;
	int ret;

	if (urlp == NULL)
		return EINVAL;

	if (!IS_USER_ADDRESS(urlp))
		return B_BAD_ADDRESS;

	ret = common_getrlimit(resource, &rl);

	if (ret == 0) {
		ret = user_memcpy(urlp, &rl, sizeof(struct rlimit));
		if (ret < 0)
			return ret;

		return 0;
	}

	return ret;
}


/**
 * @brief Syscall: setrlimit() — set a resource limit, copying from userland.
 *
 * @param resource            POSIX resource identifier.
 * @param userResourceLimit   User-space source rlimit pointer.
 * @return                    0 on success, EINVAL or B_BAD_ADDRESS on error.
 */
int
_user_setrlimit(int resource, const struct rlimit *userResourceLimit)
{
	struct rlimit resourceLimit;

	if (userResourceLimit == NULL)
		return EINVAL;

	if (!IS_USER_ADDRESS(userResourceLimit)
		|| user_memcpy(&resourceLimit, userResourceLimit,
			sizeof(struct rlimit)) < B_OK)
		return B_BAD_ADDRESS;

	return common_setrlimit(resource, &resourceLimit);
}


/**
 * @brief Syscall: return the CPU number the current thread is running on.
 *
 * @return  Zero-based CPU index.
 */
int
_user_get_cpu()
{
	Thread* thread = thread_get_current_thread();
	return thread->cpu->cpu_num;
}


/**
 * @brief Syscall: retrieve the CPU affinity mask of a thread.
 *
 * Copies the CPUSet affinity mask to a userland buffer.  If @p id is 0 the
 * current thread is used.
 *
 * @param id       Thread ID (0 for current thread).
 * @param userMask User-space destination buffer for the CPUSet.
 * @param size     Size of the user buffer in bytes.
 * @retval B_OK          Mask copied.
 * @retval B_BAD_VALUE   @p userMask is NULL or @p id < 0.
 * @retval B_BAD_ADDRESS @p userMask is not a valid user address.
 * @retval B_BAD_THREAD_ID Thread not found.
 */
status_t
_user_get_thread_affinity(thread_id id, void* userMask, size_t size)
{
	if (userMask == NULL || id < B_OK)
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userMask))
		return B_BAD_ADDRESS;

	CPUSet mask;

	if (id == 0)
		id = thread_get_current_thread_id();
	// get the thread
	Thread* thread = Thread::GetAndLock(id);
	if (thread == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> threadReference(thread, true);
	ThreadLocker threadLocker(thread, true);
	memcpy(&mask, &thread->cpumask, sizeof(mask));

	if (user_memcpy(userMask, &mask, min_c(sizeof(mask), size)) < B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}

/**
 * @brief Syscall: set the CPU affinity mask of a thread.
 *
 * Copies a CPUSet from userland, validates that at least one online CPU is
 * included, and stores the mask on the target thread.  If the thread is
 * currently running on a CPU excluded by the new mask, thread_yield() is
 * called to force a migration.
 *
 * @param id       Thread ID (0 for current thread).
 * @param userMask User-space source buffer containing the new CPUSet.
 * @param size     Size of the user buffer (must be >= sizeof(CPUSet)).
 * @retval B_OK            Mask applied.
 * @retval B_BAD_VALUE     @p userMask is NULL, @p id < 0, @p size is too
 *                         small, or the mask excludes all online CPUs.
 * @retval B_BAD_ADDRESS   @p userMask is not a valid user address.
 * @retval B_BAD_THREAD_ID Thread not found.
 */
status_t
_user_set_thread_affinity(thread_id id, const void* userMask, size_t size)
{
	if (userMask == NULL || id < B_OK || size < sizeof(CPUSet))
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userMask))
		return B_BAD_ADDRESS;

	CPUSet mask;
	if (user_memcpy(&mask, userMask, min_c(sizeof(CPUSet), size)) < B_OK)
		return B_BAD_ADDRESS;

	CPUSet cpus;
	cpus.SetAll();
	for (int i = 0; i < smp_get_num_cpus(); i++)
		cpus.ClearBit(i);
	if (mask.Matches(cpus))
		return B_BAD_VALUE;

	if (id == 0)
		id = thread_get_current_thread_id();

	// get the thread
	Thread* thread = Thread::GetAndLock(id);
	if (thread == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> threadReference(thread, true);
	ThreadLocker threadLocker(thread, true);
	memcpy(&thread->cpumask, &mask, sizeof(mask));

	// check if running on masked cpu
	if (!thread->cpumask.GetBit(thread->cpu->cpu_num))
		thread_yield();

	return B_OK;
}
