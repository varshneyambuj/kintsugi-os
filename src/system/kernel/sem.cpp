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
 *   Copyright 2002-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file sem.cpp
 * @brief Kernel semaphore implementation — the primary synchronization primitive.
 *
 * Implements POSIX-style counting semaphores (create_sem, delete_sem,
 * acquire_sem_etc, release_sem_etc) used throughout the kernel and exposed
 * to user space via syscalls. The semaphore table is a fixed-size array
 * protected by a spinlock; waiting threads are queued on each sem_entry.
 *
 * @see port.cpp, condition_variable.cpp
 */


#include <sem.h>

#include <stdlib.h>
#include <string.h>

#include <OS.h>

#include <arch/int.h>
#include <boot/kernel_args.h>
#include <cpu.h>
#include <debug.h>
#include <interrupts.h>
#include <kernel.h>
#include <ksignal.h>
#include <kscheduler.h>
#include <listeners.h>
#include <scheduling_analysis.h>
#include <smp.h>
#include <syscall_restart.h>
#include <team.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>
#include <vfs.h>
#include <vm/vm_page.h>
#include <wait_for_objects.h>

#include "kernel_debug_config.h"


//#define TRACE_SEM
#ifdef TRACE_SEM
#	define TRACE(x) dprintf_no_syslog x
#else
#	define TRACE(x) ;
#endif

//#define KTRACE_SEM
#ifdef KTRACE_SEM
#	define KTRACE(x...) ktrace_printf(x)
#else
#	define KTRACE(x...) do {} while (false)
#endif


// Locking:
// * sSemsSpinlock: Protects the semaphore free list (sFreeSemsHead,
//   sFreeSemsTail), Team::sem_list, and together with sem_entry::lock
//   write access to sem_entry::owner/team_link.
// * sem_entry::lock: Protects all sem_entry members. owner, team_link
//   additional need sSemsSpinlock for write access.
//   lock itself doesn't need protection -- sem_entry objects are never deleted.
//
// The locking order is sSemsSpinlock -> sem_entry::lock -> scheduler lock. All
// semaphores are in the sSems array (sem_entry[]). Access by sem_id requires
// computing the object index (id % sMaxSems), locking the respective
// sem_entry::lock and verifying that sem_entry::id matches afterwards.


struct queued_thread : DoublyLinkedListLinkImpl<queued_thread> {
	queued_thread(Thread *thread, int32 count)
		:
		thread(thread),
		count(count),
		queued(false)
	{
	}

	Thread	*thread;
	int32	count;
	bool	queued;
};

typedef DoublyLinkedList<queued_thread> ThreadQueue;

struct sem_entry {
	union {
		// when slot in use
		struct {
			struct list_link	team_link;
			int32				count;
			int32				net_count;
									// count + acquisition count of all blocked
									// threads
			char*				name;
			team_id				owner;
			select_info*		select_infos;
			thread_id			last_acquirer;
#if DEBUG_SEM_LAST_ACQUIRER
			int32				last_acquire_count;
			thread_id			last_releaser;
			int32				last_release_count;
#endif
		} used;

		// when slot unused
		struct {
			sem_id				next_id;
			struct sem_entry*	next;
		} unused;
	} u;

	sem_id				id;
	spinlock			lock;	// protects only the id field when unused
	ThreadQueue			queue;	// should be in u.used, but has a constructor
};

static const int32 kMaxSemaphores = 65536;
static int32 sMaxSems = 4096;
	// Final value is computed based on the amount of available memory
static int32 sUsedSems = 0;

static struct sem_entry *sSems = NULL;
static bool sSemsActive = false;
static struct sem_entry	*sFreeSemsHead = NULL;
static struct sem_entry	*sFreeSemsTail = NULL;

static spinlock sSemsSpinlock = B_SPINLOCK_INITIALIZER;


/**
 * @brief Kernel debugger command: list all active semaphores, optionally filtered.
 *
 * Accepts optional filter arguments to restrict output to semaphores owned by
 * a particular team, matching a name substring, or last acquired by a given
 * thread. When called with no arguments all in-use semaphore slots are printed.
 *
 * @param argc Number of command-line arguments (including command name).
 * @param argv Array of argument strings; argv[1] may be "team", "owner",
 *             "name", or "last", with argv[2] holding the filter value.
 * @return Always returns 0.
 * @note Must only be called from the kernel debugger context.
 */
static int
dump_sem_list(int argc, char** argv)
{
	const char* name = NULL;
	team_id owner = -1;
	thread_id last = -1;
	int32 i;

	if (argc > 2) {
		if (!strcmp(argv[1], "team") || !strcmp(argv[1], "owner"))
			owner = strtoul(argv[2], NULL, 0);
		else if (!strcmp(argv[1], "name"))
			name = argv[2];
		else if (!strcmp(argv[1], "last"))
			last = strtoul(argv[2], NULL, 0);
	} else if (argc > 1)
		owner = strtoul(argv[1], NULL, 0);

	kprintf("%-*s       id count   team   last  name\n", B_PRINTF_POINTER_WIDTH,
		"sem");

	for (i = 0; i < sMaxSems; i++) {
		struct sem_entry* sem = &sSems[i];
		if (sem->id < 0
			|| (last != -1 && sem->u.used.last_acquirer != last)
			|| (name != NULL && strstr(sem->u.used.name, name) == NULL)
			|| (owner != -1 && sem->u.used.owner != owner))
			continue;

		kprintf("%p %6" B_PRId32 " %5" B_PRId32 " %6" B_PRId32 " "
			"%6" B_PRId32 " "
			" %s\n", sem, sem->id, sem->u.used.count,
			sem->u.used.owner,
			sem->u.used.last_acquirer > 0 ? sem->u.used.last_acquirer : 0,
			sem->u.used.name);
	}

	return 0;
}


/**
 * @brief Prints detailed information about a single semaphore entry to the kernel console.
 *
 * Displays the semaphore's ID, name, owning team, current count, waiting thread
 * queue, and (when DEBUG_SEM_LAST_ACQUIRER is enabled) last acquirer and releaser
 * information. Also sets kernel debugger convenience variables _sem, _semID,
 * _owner, _acquirer, and _releaser.
 *
 * @param sem Pointer to the sem_entry to dump.
 * @note Must only be called from the kernel debugger context.
 */
static void
dump_sem(struct sem_entry* sem)
{
	kprintf("SEM: %p\n", sem);
	kprintf("id:      %" B_PRId32 " (%#" B_PRIx32 ")\n", sem->id, sem->id);
	if (sem->id >= 0) {
		kprintf("name:    '%s'\n", sem->u.used.name);
		kprintf("owner:   %" B_PRId32 "\n", sem->u.used.owner);
		kprintf("count:   %" B_PRId32 "\n", sem->u.used.count);
		kprintf("queue:  ");
		if (!sem->queue.IsEmpty()) {
			ThreadQueue::Iterator it = sem->queue.GetIterator();
			while (queued_thread* entry = it.Next())
				kprintf(" %" B_PRId32, entry->thread->id);
			kprintf("\n");
		} else
			kprintf(" -\n");

		set_debug_variable("_sem", (addr_t)sem);
		set_debug_variable("_semID", sem->id);
		set_debug_variable("_owner", sem->u.used.owner);

#if DEBUG_SEM_LAST_ACQUIRER
		kprintf("last acquired by: %" B_PRId32 ", count: %" B_PRId32 "\n",
			sem->u.used.last_acquirer, sem->u.used.last_acquire_count);
		kprintf("last released by: %" B_PRId32 ", count: %" B_PRId32 "\n",
			sem->u.used.last_releaser, sem->u.used.last_release_count);

		if (sem->u.used.last_releaser != 0)
			set_debug_variable("_releaser", sem->u.used.last_releaser);
		else
			unset_debug_variable("_releaser");
#else
		kprintf("last acquired by: %" B_PRId32 "\n", sem->u.used.last_acquirer);
#endif

		if (sem->u.used.last_acquirer != 0)
			set_debug_variable("_acquirer", sem->u.used.last_acquirer);
		else
			unset_debug_variable("_acquirer");
	} else {
		kprintf("next:    %p\n", sem->u.unused.next);
		kprintf("next_id: %" B_PRId32 "\n", sem->u.unused.next_id);
	}
}


/**
 * @brief Kernel debugger command: print detailed info for a semaphore by ID, address, or name.
 *
 * Accepts a single argument that may be a numeric semaphore ID, a kernel
 * virtual address of a sem_entry, or a semaphore name string. Delegates the
 * actual printing to dump_sem().
 *
 * @param argc Number of command-line arguments (including command name).
 * @param argv argv[1] must be the semaphore identifier (address, ID, or name).
 * @return Always returns 0.
 * @note Must only be called from the kernel debugger context.
 */
static int
dump_sem_info(int argc, char **argv)
{
	bool found = false;
	addr_t num;
	int32 i;

	if (argc < 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	char* endptr;
	num = strtoul(argv[1], &endptr, 0);

	if (endptr != argv[1]) {
		if (IS_KERNEL_ADDRESS(num)) {
			dump_sem((struct sem_entry *)num);
			return 0;
		} else {
			uint32 slot = num % sMaxSems;
			if (sSems[slot].id != (int)num) {
				kprintf("sem %ld (%#lx) doesn't exist!\n", num, num);
				return 0;
			}

			dump_sem(&sSems[slot]);
			return 0;
		}
	}

	// walk through the sem list, trying to match name
	for (i = 0; i < sMaxSems; i++) {
		if (sSems[i].u.used.name != NULL
			&& strcmp(argv[1], sSems[i].u.used.name) == 0) {
			dump_sem(&sSems[i]);
			found = true;
		}
	}

	if (!found)
		kprintf("sem \"%s\" doesn't exist!\n", argv[1]);
	return 0;
}


/*!	\brief Appends a semaphore slot to the free list.

	The semaphore list must be locked.
	The slot's id field is not changed. It should already be set to -1.

	\param slot The index of the semaphore slot.
	\param nextID The ID the slot will get when reused. If < 0 the \a slot
		   is used.
*/
/**
 * @brief Appends a semaphore table slot to the tail of the free-slot list.
 *
 * @param slot   Index into sSems[] of the slot being freed.
 * @param nextID The sem_id the slot should receive the next time it is
 *               allocated. Pass a value < 0 to reuse the slot index itself.
 * @note Must be called with sSemsSpinlock held and interrupts disabled.
 */
static void
free_sem_slot(int slot, sem_id nextID)
{
	struct sem_entry *sem = sSems + slot;
	// set next_id to the next possible value; for sanity check the current ID
	if (nextID < 0)
		sem->u.unused.next_id = slot;
	else
		sem->u.unused.next_id = nextID;
	// append the entry to the list
	if (sFreeSemsTail)
		sFreeSemsTail->u.unused.next = sem;
	else
		sFreeSemsHead = sem;
	sFreeSemsTail = sem;
	sem->u.unused.next = NULL;
}


/**
 * @brief Notifies any registered select() waiters of the given event on a semaphore.
 *
 * @param sem    The semaphore whose select_infos list should be notified.
 * @param events Bitmask of B_EVENT_* flags to deliver.
 * @note Must be called with the semaphore's spinlock held.
 */
static inline void
notify_sem_select_events(struct sem_entry* sem, uint16 events)
{
	if (sem->u.used.select_infos)
		notify_select_events_list(sem->u.used.select_infos, events);
}


/**
 * @brief Fills a sem_info structure with a snapshot of the given semaphore's state.
 *
 * Copies the semaphore ID, owning team, name, current count, and latest holder
 * into the caller-supplied @p info buffer.
 *
 * @param sem  Pointer to the semaphore entry to read from.
 * @param info Pointer to the sem_info structure to populate.
 * @param size Size of the sem_info buffer (for strlcpy bounds).
 * @note The semaphore's spinlock must be held by the caller.
 */
static void
fill_sem_info(struct sem_entry* sem, sem_info* info, size_t size)
{
	info->sem = sem->id;
	info->team = sem->u.used.owner;
	strlcpy(info->name, sem->u.used.name, sizeof(info->name));
	info->count = sem->u.used.count;
	info->latest_holder = sem->u.used.last_acquirer;
}


/*!	You must call this function with interrupts disabled, and the semaphore's
	spinlock held. Note that it will unlock the spinlock itself.
	Since it cannot free() the semaphore's name with interrupts turned off, it
	will return that one in \a name.
*/
/**
 * @brief Tears down an in-use semaphore slot, waking all waiting threads.
 *
 * Notifies select() waiters of B_EVENT_INVALID, unblocks every thread queued
 * on the semaphore with B_BAD_SEM_ID, marks the slot as unused (id = -1),
 * appends it to the free list, and decrements the global used-semaphore count.
 * Because free() cannot be called with interrupts disabled, the caller's name
 * buffer is returned via @p _name and must be freed by the caller after
 * re-enabling interrupts.
 *
 * @param sem     Reference to the sem_entry to uninitialise.
 * @param _name   Output parameter; receives the heap-allocated name pointer
 *                that the caller must free() after interrupts are re-enabled.
 * @param locker  SpinLocker already holding sem.lock; this function unlocks it.
 * @note Must be called with interrupts disabled and sem.lock held.
 *       The function releases sem.lock before returning.
 */
static void
uninit_sem_locked(struct sem_entry& sem, char** _name, SpinLocker& locker)
{
	KTRACE("delete_sem(sem: %ld)", sem.u.used.id);

	notify_sem_select_events(&sem, B_EVENT_INVALID);
	sem.u.used.select_infos = NULL;

	// free any threads waiting for this semaphore
	while (queued_thread* entry = sem.queue.RemoveHead()) {
		entry->queued = false;
		thread_unblock(entry->thread, B_BAD_SEM_ID);
	}

	int32 id = sem.id;
	sem.id = -1;
	*_name = sem.u.used.name;
	sem.u.used.name = NULL;

	locker.Unlock();

	// append slot to the free list
	SpinLocker _(&sSemsSpinlock);
	free_sem_slot(id % sMaxSems, id + sMaxSems);
	atomic_add(&sUsedSems, -1);
}


/**
 * @brief Deletes a semaphore by ID, optionally enforcing permission checks.
 *
 * Looks up the semaphore, removes it from the owning team's semaphore list,
 * calls uninit_sem_locked() to wake blocked threads and recycle the slot, then
 * frees the name string. Triggers a reschedule if necessary.
 *
 * @param id              The sem_id of the semaphore to delete.
 * @param checkPermission If true, prevents deletion of kernel-owned semaphores
 *                        from non-kernel callers.
 * @retval B_OK           On successful deletion.
 * @retval B_NO_MORE_SEMS If the semaphore subsystem is not yet active.
 * @retval B_BAD_SEM_ID   If @p id is negative or does not match a live semaphore.
 * @retval B_NOT_ALLOWED  If @p checkPermission is true and the semaphore is
 *                        owned by the kernel team.
 * @note Acquires sSemsSpinlock and the semaphore's own spinlock internally.
 */
static status_t
delete_sem_internal(sem_id id, bool checkPermission)
{
	if (sSemsActive == false)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;

	int32 slot = id % sMaxSems;

	InterruptsLocker interruptsLocker;
	SpinLocker listLocker(sSemsSpinlock);
	SpinLocker semLocker(sSems[slot].lock);

	if (sSems[slot].id != id) {
		TRACE(("delete_sem: invalid sem_id %ld\n", id));
		return B_BAD_SEM_ID;
	}

	if (checkPermission
		&& sSems[slot].u.used.owner == team_get_kernel_team_id()) {
		dprintf("thread %" B_PRId32 " tried to delete kernel semaphore "
			"%" B_PRId32 ".\n", thread_get_current_thread_id(), id);
		return B_NOT_ALLOWED;
	}

	if (sSems[slot].u.used.owner >= 0) {
		list_remove_link(&sSems[slot].u.used.team_link);
		sSems[slot].u.used.owner = -1;
	} else
		panic("sem %" B_PRId32 " has no owner", id);

	listLocker.Unlock();

	char* name;
	uninit_sem_locked(sSems[slot], &name, semLocker);

	SpinLocker schedulerLocker(thread_get_current_thread()->scheduler_lock);
	scheduler_reschedule_if_necessary_locked();
	schedulerLocker.Unlock();

	interruptsLocker.Unlock();

	free(name);
	return B_OK;
}


//	#pragma mark - Private Kernel API


// TODO: Name clash with POSIX sem_init()... (we could just use C++)
/**
 * @brief Initializes the kernel semaphore subsystem.
 *
 * Computes the maximum number of semaphores based on available physical memory,
 * allocates and zero-initialises the global semaphore table as a kernel area,
 * chains all slots onto the free list, and registers the "sems" and "sem"
 * kernel debugger commands.
 *
 * @param args Pointer to the kernel_args structure passed by the bootloader
 *             (used indirectly via vm_page_num_pages()).
 * @retval B_OK Always returns 0 on success; panics on fatal allocation failure.
 * @note Must be called exactly once during kernel startup, before any
 *       semaphore operations are performed.
 */
status_t
haiku_sem_init(kernel_args *args)
{
	area_id area;
	int32 i;

	TRACE(("sem_init: entry\n"));

	// compute maximal number of semaphores depending on the available memory
	// 128 MB -> 16384 semaphores, 448 kB fixed array size
	// 256 MB -> 32768, 896 kB
	// 512 MB and more-> 65536, 1.75 MB
	i = vm_page_num_pages() / 2;
	while (sMaxSems < i && sMaxSems < kMaxSemaphores)
		sMaxSems <<= 1;

	// create and initialize semaphore table
	virtual_address_restrictions virtualRestrictions = {};
	virtualRestrictions.address_specification = B_ANY_KERNEL_ADDRESS;
	physical_address_restrictions physicalRestrictions = {};
	area = create_area_etc(B_SYSTEM_TEAM, "sem_table",
		sizeof(struct sem_entry) * sMaxSems, B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, CREATE_AREA_DONT_WAIT, 0,
		&virtualRestrictions, &physicalRestrictions, (void**)&sSems);
	if (area < 0)
		panic("unable to allocate semaphore table!\n");

	memset((void*)sSems, 0, sizeof(struct sem_entry) * sMaxSems);
	for (i = 0; i < sMaxSems; i++) {
		sSems[i].id = -1;
		free_sem_slot(i, i);
	}

	// add debugger commands
	add_debugger_command_etc("sems", &dump_sem_list,
		"Dump a list of all active semaphores (for team, with name, etc.)",
		"[ ([ \"team\" | \"owner\" ] <team>) | (\"name\" <name>) ]"
			" | (\"last\" <last acquirer>)\n"
		"Prints a list of all active semaphores meeting the given\n"
		"requirement. If no argument is given, all sems are listed.\n"
		"  <team>             - The team owning the semaphores.\n"
		"  <name>             - Part of the name of the semaphores.\n"
		"  <last acquirer>    - The thread that last acquired the semaphore.\n"
		, 0);
	add_debugger_command_etc("sem", &dump_sem_info,
		"Dump info about a particular semaphore",
		"<sem>\n"
		"Prints info about the specified semaphore.\n"
		"  <sem>  - pointer to the semaphore structure, semaphore ID, or name\n"
		"           of the semaphore to print info for.\n", 0);

	TRACE(("sem_init: exit\n"));

	sSemsActive = true;

	return 0;
}


/*!	Creates a semaphore with the given parameters.

	This function is only available from within the kernel, and
	should not be made public - if possible, we should remove it
	completely (and have only create_sem() exported).
*/
/**
 * @brief Creates a new semaphore with the specified count, name, and owning team.
 *
 * Allocates a slot from the free list, initialises its fields, links it into
 * the owning team's semaphore list, and notifies wait-object listeners. The
 * name is copied into a heap buffer; if @p name is NULL the semaphore is named
 * "unnamed semaphore".
 *
 * @param count  Initial semaphore count; must be >= 0.
 * @param name   NUL-terminated name string (may be NULL).
 * @param owner  team_id of the team that will own this semaphore.
 * @return       A valid sem_id on success, or a negative error code.
 * @retval B_BAD_VALUE    If @p count is negative.
 * @retval B_NO_MORE_SEMS If the subsystem is inactive or the table is full.
 * @retval B_BAD_TEAM_ID  If @p owner does not refer to a live team.
 * @retval B_NO_MEMORY    If the name buffer could not be allocated.
 * @note Acquires sSemsSpinlock and the chosen slot's spinlock internally.
 */
sem_id
create_sem_etc(int32 count, const char* name, team_id owner)
{
	if (count < 0)
		return B_BAD_VALUE;
	if (!sSemsActive || sUsedSems == sMaxSems)
		return B_NO_MORE_SEMS;

	if (name == NULL)
		name = "unnamed semaphore";

	// get the owning team
	Team* team = Team::Get(owner);
	if (team == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> teamReference(team, true);

	// clone the name
	size_t nameLength = strlen(name) + 1;
	nameLength = min_c(nameLength, B_OS_NAME_LENGTH);
	char* tempName = (char*)malloc(nameLength);
	if (tempName == NULL)
		return B_NO_MEMORY;

	strlcpy(tempName, name, nameLength);

	InterruptsSpinLocker _(&sSemsSpinlock);

	// get the first slot from the free list
	struct sem_entry* sem = sFreeSemsHead;
	sem_id id = B_NO_MORE_SEMS;
	if (sem != NULL) {
		// remove it from the free list
		sFreeSemsHead = sem->u.unused.next;
		if (!sFreeSemsHead)
			sFreeSemsTail = NULL;

		// init the slot
		SpinLocker semLocker(sem->lock);
		sem->id = sem->u.unused.next_id;
		sem->u.used.count = count;
		sem->u.used.net_count = count;
		new(&sem->queue) ThreadQueue;
		sem->u.used.name = tempName;
		sem->u.used.owner = team->id;
		sem->u.used.select_infos = NULL;
		id = sem->id;

		list_add_item(&team->sem_list, &sem->u.used.team_link);

		semLocker.Unlock();

		atomic_add(&sUsedSems, 1);

		KTRACE("create_sem_etc(count: %ld, name: %s, owner: %ld) -> %ld",
			count, name, owner, id);

		T_SCHEDULING_ANALYSIS(CreateSemaphore(id, name));
		NotifyWaitObjectListeners(&WaitObjectListener::SemaphoreCreated, id,
			name);
	}

	if (sem == NULL)
		free(tempName);

	return id;
}


/**
 * @brief Registers a select() interest on a semaphore for B_EVENT_ACQUIRE_SEMAPHORE.
 *
 * Links @p info into the semaphore's select_infos list. If the semaphore is
 * already acquirable (count > 0) an immediate B_EVENT_ACQUIRE_SEMAPHORE
 * notification is delivered.
 *
 * @param id     The sem_id to monitor.
 * @param info   Caller-allocated select_info describing the events of interest.
 * @param kernel Pass true if the caller is kernel code (bypasses ownership check).
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If @p id is invalid.
 * @retval B_NOT_ALLOWED If the semaphore is kernel-owned and @p kernel is false.
 * @note Acquires the semaphore's spinlock with interrupts disabled.
 */
status_t
select_sem(int32 id, struct select_info* info, bool kernel)
{
	if (id < 0)
		return B_BAD_SEM_ID;

	int32 slot = id % sMaxSems;

	InterruptsSpinLocker _(&sSems[slot].lock);

	if (sSems[slot].id != id) {
		// bad sem ID
		return B_BAD_SEM_ID;
	} else if (!kernel
		&& sSems[slot].u.used.owner == team_get_kernel_team_id()) {
		// kernel semaphore, but call from userland
		return B_NOT_ALLOWED;
	} else {
		info->selected_events &= B_EVENT_ACQUIRE_SEMAPHORE | B_EVENT_INVALID;

		if (info->selected_events != 0) {
			info->next = sSems[slot].u.used.select_infos;
			sSems[slot].u.used.select_infos = info;

			if (sSems[slot].u.used.count > 0)
				notify_select_events(info, B_EVENT_ACQUIRE_SEMAPHORE);
		}
	}

	return B_OK;
}


/**
 * @brief Deregisters a previously registered select() interest on a semaphore.
 *
 * Removes @p info from the semaphore's select_infos linked list if it is
 * present. Safe to call even if the semaphore has already been deleted (the
 * id check prevents dereferencing a recycled slot).
 *
 * @param id     The sem_id that was passed to select_sem().
 * @param info   The select_info pointer to remove.
 * @param kernel Unused in the current implementation; kept for API symmetry.
 * @retval B_OK         Always.
 * @retval B_BAD_SEM_ID If @p id is negative (returns early).
 * @note Acquires the semaphore's spinlock with interrupts disabled.
 */
status_t
deselect_sem(int32 id, struct select_info* info, bool kernel)
{
	if (id < 0)
		return B_BAD_SEM_ID;

	if (info->selected_events == 0)
		return B_OK;

	int32 slot = id % sMaxSems;

	InterruptsSpinLocker _(&sSems[slot].lock);

	if (sSems[slot].id == id) {
		select_info** infoLocation = &sSems[slot].u.used.select_infos;
		while (*infoLocation != NULL && *infoLocation != info)
			infoLocation = &(*infoLocation)->next;

		if (*infoLocation == info)
			*infoLocation = info->next;
	}

	return B_OK;
}


/*!	Forcibly removes a thread from a semaphores wait queue. May have to wake up
	other threads in the process.
	Must be called with semaphore lock held. The thread lock must not be held.
*/
/**
 * @brief Forcibly removes a thread from a semaphore's wait queue and rebalances waiters.
 *
 * After removing @p entry the function iterates the head of the queue and
 * unblocks any threads whose requested count can now be satisfied by the
 * restored count. Delivers a B_EVENT_ACQUIRE_SEMAPHORE select notification if
 * the semaphore becomes acquirable.
 *
 * @param entry Pointer to the queued_thread entry to remove.
 * @param sem   Pointer to the semaphore from which the entry is being removed.
 * @note The semaphore's spinlock must be held by the caller.
 *       The caller must NOT hold any thread (scheduler) lock.
 */
static void
remove_thread_from_sem(queued_thread *entry, struct sem_entry *sem)
{
	if (!entry->queued)
		return;

	sem->queue.Remove(entry);
	entry->queued = false;
	sem->u.used.count += entry->count;

	// We're done with this entry. We only have to check, if other threads
	// need unblocking, too.

	// Now see if more threads need to be woken up. We get the scheduler lock
	// for that time, so the blocking state of threads won't change (due to
	// interruption or timeout). We need that lock anyway when unblocking a
	// thread.
	while ((entry = sem->queue.Head()) != NULL) {
		SpinLocker schedulerLocker(entry->thread->scheduler_lock);
		if (thread_is_blocked(entry->thread)) {
			// The thread is still waiting. If its count is satisfied, unblock
			// it. Otherwise we can't unblock any other thread.
			if (entry->count > sem->u.used.net_count)
				break;

			thread_unblock_locked(entry->thread, B_OK);
			sem->u.used.net_count -= entry->count;
		} else {
			// The thread is no longer waiting, but still queued, which means
			// acquiration failed and we can just remove it.
			sem->u.used.count += entry->count;
		}

		sem->queue.Remove(entry);
		entry->queued = false;
	}

	// select notification, if the semaphore is now acquirable
	if (sem->u.used.count > 0)
		notify_sem_select_events(sem, B_EVENT_ACQUIRE_SEMAPHORE);
}


/*!	This function deletes all semaphores belonging to a particular team.
*/
/**
 * @brief Deletes all semaphores owned by the given team.
 *
 * Iterates the team's semaphore list, uninitialising each entry via
 * uninit_sem_locked() (which wakes blocked threads) and freeing the name
 * string. Triggers a reschedule after processing all semaphores. Intended to
 * be called during team teardown.
 *
 * @param team Pointer to the Team whose semaphores should be deleted.
 * @note This function may reschedule; it must not be called with any spinlock held.
 */
void
sem_delete_owned_sems(Team* team)
{
	while (true) {
		char* name;

		{
			// get the next semaphore from the team's sem list
			InterruptsLocker _;
			SpinLocker semListLocker(sSemsSpinlock);
			sem_entry* sem = (sem_entry*)list_remove_head_item(&team->sem_list);
			if (sem == NULL)
				break;

			// delete the semaphore
			SpinLocker semLocker(sem->lock);
			semListLocker.Unlock();
			uninit_sem_locked(*sem, &name, semLocker);
		}

		free(name);
	}

	scheduler_reschedule_if_necessary();
}


/** @brief Returns the compile-time maximum number of semaphore slots. */
int32
sem_max_sems(void)
{
	return sMaxSems;
}


/** @brief Returns the current number of in-use semaphore slots. */
int32
sem_used_sems(void)
{
	return sUsedSems;
}


//	#pragma mark - Public Kernel API


/**
 * @brief Creates a kernel-owned semaphore with the given count and name.
 *
 * Thin wrapper around create_sem_etc() that automatically assigns the kernel
 * team as owner.
 *
 * @param count Initial semaphore count; must be >= 0.
 * @param name  NUL-terminated name string (may be NULL).
 * @return A valid sem_id on success, or a negative error code.
 * @retval B_BAD_VALUE    If @p count is negative.
 * @retval B_NO_MORE_SEMS If the table is full.
 * @retval B_NO_MEMORY    If the name buffer could not be allocated.
 */
sem_id
create_sem(int32 count, const char* name)
{
	return create_sem_etc(count, name, team_get_kernel_team_id());
}


/**
 * @brief Deletes a kernel-owned semaphore without permission checks.
 *
 * @param id The sem_id to delete.
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If the ID is invalid or already deleted.
 * @retval B_NO_MORE_SEMS If the subsystem is inactive.
 */
status_t
delete_sem(sem_id id)
{
	return delete_sem_internal(id, false);
}


/**
 * @brief Acquires a semaphore once, blocking indefinitely if necessary.
 *
 * @param id The sem_id to acquire.
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If @p id is invalid.
 * @retval B_INTERRUPTED If the thread received a signal while blocked.
 */
status_t
acquire_sem(sem_id id)
{
	return switch_sem_etc(-1, id, 1, 0, 0);
}


/**
 * @brief Acquires a semaphore by @p count, with optional timeout and flags.
 *
 * Decrements the semaphore count by @p count. If the resulting count would be
 * negative the calling thread blocks until the count can be satisfied, the
 * optional timeout expires, or the thread is interrupted.
 *
 * @param id      The sem_id to acquire.
 * @param count   Number of units to acquire; must be > 0.
 * @param flags   Combination of B_RELATIVE_TIMEOUT, B_ABSOLUTE_TIMEOUT,
 *                B_CAN_INTERRUPT, B_KILL_CAN_INTERRUPT, etc.
 * @param timeout Timeout value in microseconds (interpretation depends on flags).
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If @p id is invalid.
 * @retval B_WOULD_BLOCK If B_RELATIVE_TIMEOUT was specified with timeout <= 0
 *                       and the semaphore is not immediately acquirable.
 * @retval B_TIMED_OUT   If the timeout expired before acquisition.
 * @retval B_INTERRUPTED If the thread was interrupted by a signal.
 */
status_t
acquire_sem_etc(sem_id id, int32 count, uint32 flags, bigtime_t timeout)
{
	return switch_sem_etc(-1, id, count, flags, timeout);
}


/**
 * @brief Atomically releases one semaphore and acquires another.
 *
 * @param toBeReleased sem_id to release (pass -1 to skip the release step).
 * @param toBeAcquired sem_id to acquire.
 * @retval B_OK On success.
 * @retval B_BAD_SEM_ID  If either ID is invalid.
 * @retval B_INTERRUPTED If interrupted while waiting to acquire.
 */
status_t
switch_sem(sem_id toBeReleased, sem_id toBeAcquired)
{
	return switch_sem_etc(toBeReleased, toBeAcquired, 1, 0, 0);
}


/**
 * @brief Acquires a semaphore by @p count, optionally releasing another semaphore atomically.
 *
 * If @p semToBeReleased >= 0 it is released (with B_DO_NOT_RESCHEDULE) after
 * the calling thread is committed to blocking, providing an atomic hand-off.
 * The acquisition semantics are identical to acquire_sem_etc().
 *
 * @param semToBeReleased sem_id to release before blocking, or -1 to skip.
 * @param id              sem_id to acquire.
 * @param count           Number of units to acquire; must be > 0.
 * @param flags           Timeout/interrupt flags (see acquire_sem_etc()).
 * @param timeout         Timeout in microseconds.
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If @p id is negative or stale.
 * @retval B_BAD_VALUE   If @p count <= 0 or conflicting timeout flags are set.
 * @retval B_WOULD_BLOCK Immediate timeout with semaphore unavailable.
 * @retval B_TIMED_OUT   Timeout expired.
 * @retval B_INTERRUPTED Interrupted by signal.
 * @retval B_NOT_ALLOWED If B_CHECK_PERMISSION is set and sem is kernel-owned.
 * @note Must be called with interrupts enabled.
 */
status_t
switch_sem_etc(sem_id semToBeReleased, sem_id id, int32 count,
	uint32 flags, bigtime_t timeout)
{
	int slot = id % sMaxSems;

	if (gKernelStartup)
		return B_OK;
	if (sSemsActive == false)
		return B_NO_MORE_SEMS;
#if KDEBUG
	if (!are_interrupts_enabled()) {
		panic("switch_sem_etc: called with interrupts disabled for sem "
			"%" B_PRId32 "\n", id);
	}
#endif

	if (id < 0)
		return B_BAD_SEM_ID;
	if (count <= 0
		|| (flags & (B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT)) == (B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT)) {
		return B_BAD_VALUE;
	}

	InterruptsLocker _;
	SpinLocker semLocker(sSems[slot].lock);

	if (sSems[slot].id != id) {
		TRACE(("switch_sem_etc: bad sem %ld\n", id));
		return B_BAD_SEM_ID;
	}

	// TODO: the B_CHECK_PERMISSION flag should be made private, as it
	//	doesn't have any use outside the kernel
	if ((flags & B_CHECK_PERMISSION) != 0
		&& sSems[slot].u.used.owner == team_get_kernel_team_id()) {
		dprintf("thread %" B_PRId32 " tried to acquire kernel semaphore "
			"%" B_PRId32 ".\n", thread_get_current_thread_id(), id);
#if 0
		_user_debugger("Thread tried to acquire kernel semaphore.");
#endif
		return B_NOT_ALLOWED;
	}

	if (sSems[slot].u.used.count - count < 0) {
		if ((flags & B_RELATIVE_TIMEOUT) != 0 && timeout <= 0) {
			// immediate timeout
			return B_WOULD_BLOCK;
		} else if ((flags & B_ABSOLUTE_TIMEOUT) != 0 && timeout < 0) {
			// absolute negative timeout
			return B_TIMED_OUT;
		}
	}

	KTRACE("switch_sem_etc(semToBeReleased: %ld, sem: %ld, count: %ld, "
		"flags: 0x%lx, timeout: %lld)", semToBeReleased, id, count, flags,
		timeout);

	if ((sSems[slot].u.used.count -= count) < 0) {
		// we need to block
		Thread *thread = thread_get_current_thread();

		TRACE(("switch_sem_etc(id = %ld): block name = %s, thread = %p,"
			" name = %s\n", id, sSems[slot].u.used.name, thread, thread->name));

		// do a quick check to see if the thread has any pending signals
		// this should catch most of the cases where the thread had a signal
		SpinLocker schedulerLocker(thread->scheduler_lock);
		if (thread_is_interrupted(thread, flags)) {
			schedulerLocker.Unlock();
			sSems[slot].u.used.count += count;
			if (semToBeReleased >= B_OK) {
				// we need to still release the semaphore to always
				// leave in a consistent state
				release_sem_etc(semToBeReleased, 1, B_DO_NOT_RESCHEDULE);
			}
			return B_INTERRUPTED;
		}

		schedulerLocker.Unlock();

		if ((flags & (B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT)) == 0)
			timeout = B_INFINITE_TIMEOUT;

		// enqueue in the semaphore queue and get ready to wait
		queued_thread queueEntry(thread, count);
		sSems[slot].queue.Add(&queueEntry);
		queueEntry.queued = true;

		thread_prepare_to_block(thread, flags, THREAD_BLOCK_TYPE_SEMAPHORE,
			(void*)(addr_t)id);

		semLocker.Unlock();

		// release the other semaphore, if any
		if (semToBeReleased >= 0) {
			release_sem_etc(semToBeReleased, 1, B_DO_NOT_RESCHEDULE);
			semToBeReleased = -1;
		}

		status_t acquireStatus = timeout == B_INFINITE_TIMEOUT
			? thread_block() : thread_block_with_timeout(flags, timeout);

		semLocker.Lock();

		// If we're still queued, this means the acquiration failed, and we
		// need to remove our entry and (potentially) wake up other threads.
		if (queueEntry.queued)
			remove_thread_from_sem(&queueEntry, &sSems[slot]);

		if (acquireStatus >= B_OK) {
			sSems[slot].u.used.last_acquirer = thread_get_current_thread_id();
#if DEBUG_SEM_LAST_ACQUIRER
			sSems[slot].u.used.last_acquire_count = count;
#endif
		}

		semLocker.Unlock();

		TRACE(("switch_sem_etc(sem %ld): exit block name %s, "
			"thread %ld (%s)\n", id, sSems[slot].u.used.name, thread->id,
			thread->name));
		KTRACE("switch_sem_etc() done: 0x%lx", acquireStatus);
		return acquireStatus;
	} else {
		sSems[slot].u.used.net_count -= count;
		sSems[slot].u.used.last_acquirer = thread_get_current_thread_id();
#if DEBUG_SEM_LAST_ACQUIRER
		sSems[slot].u.used.last_acquire_count = count;
#endif
	}

	KTRACE("switch_sem_etc() done: 0x%lx", status);

	return B_OK;
}


/**
 * @brief Releases a semaphore once, potentially waking a waiting thread.
 *
 * @param id The sem_id to release.
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If the ID is invalid.
 * @retval B_NO_MORE_SEMS If the subsystem is inactive.
 */
status_t
release_sem(sem_id id)
{
	return release_sem_etc(id, 1, 0);
}


/**
 * @brief Releases a semaphore by @p count units, waking waiting threads as appropriate.
 *
 * Increments the semaphore count by @p count (or releases all blocked threads
 * if B_RELEASE_ALL is set) and unblocks queued threads whose requested counts
 * are now satisfiable. Optionally suppresses an immediate reschedule via
 * B_DO_NOT_RESCHEDULE.
 *
 * @param id    The sem_id to release.
 * @param count Number of units to release; must be > 0 unless B_RELEASE_ALL is set.
 * @param flags Combination of B_DO_NOT_RESCHEDULE, B_RELEASE_ALL,
 *              B_RELEASE_IF_WAITING_ONLY, B_CHECK_PERMISSION, etc.
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If @p id is invalid or < 0.
 * @retval B_BAD_VALUE   If @p count <= 0 and B_RELEASE_ALL is not set.
 * @retval B_NO_MORE_SEMS If the subsystem is inactive.
 * @retval B_NOT_ALLOWED If B_CHECK_PERMISSION is set and the semaphore is
 *                       kernel-owned.
 * @note When B_DO_NOT_RESCHEDULE is not set this function may reschedule;
 *       it must not be called with interrupts disabled in that case.
 */
status_t
release_sem_etc(sem_id id, int32 count, uint32 flags)
{
	int32 slot = id % sMaxSems;

	if (gKernelStartup)
		return B_OK;
	if (sSemsActive == false)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;
	if (count <= 0 && (flags & B_RELEASE_ALL) == 0)
		return B_BAD_VALUE;
#if KDEBUG
	if ((flags & B_DO_NOT_RESCHEDULE) == 0 && !are_interrupts_enabled()) {
		panic("release_sem_etc(): called with interrupts disabled and "
			"rescheduling allowed for sem_id %" B_PRId32, id);
	}
#endif

	InterruptsLocker _;
	SpinLocker semLocker(sSems[slot].lock);

	if (sSems[slot].id != id) {
		TRACE(("sem_release_etc: invalid sem_id %ld\n", id));
		return B_BAD_SEM_ID;
	}

	// ToDo: the B_CHECK_PERMISSION flag should be made private, as it
	//	doesn't have any use outside the kernel
	if ((flags & B_CHECK_PERMISSION) != 0
		&& sSems[slot].u.used.owner == team_get_kernel_team_id()) {
		dprintf("thread %" B_PRId32 " tried to release kernel semaphore.\n",
			thread_get_current_thread_id());
		return B_NOT_ALLOWED;
	}

	KTRACE("release_sem_etc(sem: %ld, count: %ld, flags: 0x%lx)", id, count,
		flags);

	sSems[slot].u.used.last_acquirer = -sSems[slot].u.used.last_acquirer;
#if DEBUG_SEM_LAST_ACQUIRER
	sSems[slot].u.used.last_releaser = thread_get_current_thread_id();
	sSems[slot].u.used.last_release_count = count;
#endif

	status_t unblockStatus = B_OK;
	if ((flags & B_RELEASE_ALL) != 0) {
		if (count < 0)
			unblockStatus = count;

		count = sSems[slot].u.used.net_count - sSems[slot].u.used.count;

		// is there anything to do for us at all?
		if (count == 0)
			return B_OK;

		// Don't release more than necessary -- there might be interrupted/
		// timed out threads in the queue.
		flags |= B_RELEASE_IF_WAITING_ONLY;
	}

	// Grab the scheduler lock, so thread_is_blocked() is reliable (due to
	// possible interruptions or timeouts, it wouldn't be otherwise).
	while (count > 0) {
		queued_thread* entry = sSems[slot].queue.Head();
		if (entry == NULL) {
			if ((flags & B_RELEASE_IF_WAITING_ONLY) == 0) {
				sSems[slot].u.used.count += count;
				sSems[slot].u.used.net_count += count;
			}
			break;
		}

		SpinLocker schedulerLock(entry->thread->scheduler_lock);
		if (thread_is_blocked(entry->thread)) {
			// The thread is still waiting. If its count is satisfied,
			// unblock it. Otherwise we can't unblock any other thread.
			if (entry->count > (sSems[slot].u.used.net_count + count)) {
				sSems[slot].u.used.count += count;
				sSems[slot].u.used.net_count += count;
				break;
			}

			thread_unblock_locked(entry->thread, unblockStatus);

			int delta = min_c(count, entry->count);
			sSems[slot].u.used.count += delta;
			sSems[slot].u.used.net_count += delta - entry->count;
			count -= delta;
		} else {
			// The thread is no longer waiting, but still queued, which
			// means acquiration failed and we can just remove it.
			sSems[slot].u.used.count += entry->count;
		}

		sSems[slot].queue.Remove(entry);
		entry->queued = false;
	}

	if (sSems[slot].u.used.count > 0)
		notify_sem_select_events(&sSems[slot], B_EVENT_ACQUIRE_SEMAPHORE);

	// If we've unblocked another thread reschedule, if we've not explicitly
	// been told not to.
	if ((flags & B_DO_NOT_RESCHEDULE) == 0) {
		semLocker.Unlock();

		SpinLocker _(thread_get_current_thread()->scheduler_lock);
		scheduler_reschedule_if_necessary_locked();
	}

	return B_OK;
}


/**
 * @brief Reads the current count of a semaphore without acquiring it.
 *
 * @param id     The sem_id to query.
 * @param _count Output pointer that receives the current count value.
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If @p id is invalid.
 * @retval B_BAD_VALUE   If @p _count is NULL.
 * @retval B_NO_MORE_SEMS If the subsystem is inactive.
 */
status_t
get_sem_count(sem_id id, int32 *_count)
{
	if (sSemsActive == false)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;
	if (_count == NULL)
		return B_BAD_VALUE;

	int slot = id % sMaxSems;

	InterruptsSpinLocker _(sSems[slot].lock);

	if (sSems[slot].id != id) {
		TRACE(("sem_get_count: invalid sem_id %ld\n", id));
		return B_BAD_SEM_ID;
	}

	*_count = sSems[slot].u.used.count;

	return B_OK;
}


/*!	Called by the get_sem_info() macro. */
/**
 * @brief Retrieves a snapshot of a semaphore's state into a sem_info structure.
 *
 * This is the function called by the get_sem_info() macro.
 *
 * @param id   The sem_id to query.
 * @param info Pointer to a sem_info buffer to fill.
 * @param size Must equal sizeof(sem_info).
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If @p id is invalid.
 * @retval B_BAD_VALUE   If @p info is NULL or @p size is wrong.
 * @retval B_NO_MORE_SEMS If the subsystem is inactive.
 */
status_t
_get_sem_info(sem_id id, struct sem_info *info, size_t size)
{
	if (!sSemsActive)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;
	if (info == NULL || size != sizeof(sem_info))
		return B_BAD_VALUE;

	int slot = id % sMaxSems;

	InterruptsSpinLocker _(sSems[slot].lock);

	if (sSems[slot].id != id) {
		TRACE(("get_sem_info: invalid sem_id %ld\n", id));
		return B_BAD_SEM_ID;
	} else
		fill_sem_info(&sSems[slot], info, size);

	return B_OK;
}


/*!	Called by the get_next_sem_info() macro. */
/**
 * @brief Iterates over the semaphores owned by a team, returning one per call.
 *
 * This is the function called by the get_next_sem_info() macro. Uses an integer
 * cookie (list offset) to track iteration position across calls.
 *
 * @param teamID   The team_id whose semaphores should be enumerated.
 * @param _cookie  In/out pointer to the iteration cookie (initialise to 0).
 * @param info     Pointer to a sem_info buffer to fill on success.
 * @param size     Must equal sizeof(sem_info).
 * @retval B_OK          On success; @p _cookie and @p info are updated.
 * @retval B_BAD_VALUE   If no more semaphores exist or arguments are invalid.
 * @retval B_BAD_TEAM_ID If @p teamID is < 0 or does not refer to a live team.
 * @retval B_NO_MORE_SEMS If the subsystem is inactive.
 */
status_t
_get_next_sem_info(team_id teamID, int32 *_cookie, struct sem_info *info,
	size_t size)
{
	if (!sSemsActive)
		return B_NO_MORE_SEMS;
	if (_cookie == NULL || info == NULL || size != sizeof(sem_info))
		return B_BAD_VALUE;
	if (teamID < 0)
		return B_BAD_TEAM_ID;

	Team* team = Team::Get(teamID);
	if (team == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> teamReference(team, true);

	InterruptsSpinLocker semListLocker(sSemsSpinlock);

	// TODO: find a way to iterate the list that is more reliable
	sem_entry* sem = (sem_entry*)list_get_first_item(&team->sem_list);
	int32 newIndex = *_cookie;
	int32 index = 0;
	bool found = false;

	while (!found) {
		// find the next entry to be returned
		while (sem != NULL && index < newIndex) {
			sem = (sem_entry*)list_get_next_item(&team->sem_list, sem);
			index++;
		}

		if (sem == NULL)
			return B_BAD_VALUE;

		SpinLocker _(sem->lock);

		if (sem->id != -1 && sem->u.used.owner == team->id) {
			// found one!
			fill_sem_info(sem, info, size);
			newIndex = index + 1;
			found = true;
		} else
			newIndex++;
	}

	if (!found)
		return B_BAD_VALUE;

	*_cookie = newIndex;
	return B_OK;
}


/**
 * @brief Transfers ownership of a semaphore to a different team.
 *
 * Removes the semaphore from its current team's list and adds it to
 * @p newTeamID's list, updating the owner field atomically under both
 * sSemsSpinlock and the semaphore's own lock.
 *
 * @param id        The sem_id of the semaphore to reassign.
 * @param newTeamID The team_id of the new owner.
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If @p id is invalid.
 * @retval B_BAD_TEAM_ID If @p newTeamID is < 0 or does not refer to a live team.
 * @retval B_NO_MORE_SEMS If the subsystem is inactive.
 * @note Acquires sSemsSpinlock and the semaphore's spinlock internally.
 */
status_t
set_sem_owner(sem_id id, team_id newTeamID)
{
	if (sSemsActive == false)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;
	if (newTeamID < 0)
		return B_BAD_TEAM_ID;

	int32 slot = id % sMaxSems;

	// get the new team
	Team* newTeam = Team::Get(newTeamID);
	if (newTeam == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> newTeamReference(newTeam, true);

	InterruptsSpinLocker semListLocker(sSemsSpinlock);
	SpinLocker semLocker(sSems[slot].lock);

	if (sSems[slot].id != id) {
		TRACE(("set_sem_owner: invalid sem_id %ld\n", id));
		return B_BAD_SEM_ID;
	}

	list_remove_link(&sSems[slot].u.used.team_link);
	list_add_item(&newTeam->sem_list, &sSems[slot].u.used.team_link);

	sSems[slot].u.used.owner = newTeam->id;
	return B_OK;
}


/*!	Returns the name of the semaphore. The name is not copied, so the caller
	must make sure that the semaphore remains alive as long as the name is used.
*/
/**
 * @brief Returns a direct (non-copied) pointer to a semaphore's name string.
 *
 * The returned pointer becomes invalid as soon as the semaphore is deleted.
 * The caller must ensure the semaphore outlives any use of the returned string.
 *
 * @param id The sem_id to look up.
 * @return Pointer to the internal name string, or NULL if the ID is invalid.
 * @note No lock is held across the return; the name pointer is inherently racy
 *       unless the caller holds the semaphore alive by other means.
 */
const char*
sem_get_name_unsafe(sem_id id)
{
	int slot = id % sMaxSems;

	if (sSemsActive == false || id < 0 || sSems[slot].id != id)
		return NULL;

	return sSems[slot].u.used.name;
}


//	#pragma mark - Syscalls


/**
 * @brief Syscall: creates a semaphore owned by the calling user-space team.
 *
 * Copies the name string from user space (if non-NULL) and delegates to
 * create_sem_etc() with the current team as owner.
 *
 * @param count    Initial semaphore count; must be >= 0.
 * @param userName User-space pointer to a NUL-terminated name, or NULL.
 * @return A valid sem_id on success, or a negative error code.
 * @retval B_BAD_ADDRESS  If @p userName is non-NULL but not a valid user address.
 * @retval B_BAD_VALUE    If @p count is negative.
 * @retval B_NO_MORE_SEMS If the table is full.
 */
sem_id
_user_create_sem(int32 count, const char *userName)
{
	char name[B_OS_NAME_LENGTH];

	if (userName == NULL)
		return create_sem_etc(count, NULL, team_get_current_team_id());

	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, B_OS_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return create_sem_etc(count, name, team_get_current_team_id());
}


/**
 * @brief Syscall: deletes a semaphore with permission checks.
 *
 * @param id The sem_id to delete.
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If the ID is invalid.
 * @retval B_NOT_ALLOWED If the semaphore is kernel-owned.
 */
status_t
_user_delete_sem(sem_id id)
{
	return delete_sem_internal(id, true);
}


/**
 * @brief Syscall: acquires a semaphore once from user space, interruptible.
 *
 * @param id The sem_id to acquire.
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If the ID is invalid.
 * @retval B_INTERRUPTED If interrupted by a signal.
 * @retval B_NOT_ALLOWED If the semaphore is kernel-owned.
 */
status_t
_user_acquire_sem(sem_id id)
{
	status_t error = switch_sem_etc(-1, id, 1,
		B_CAN_INTERRUPT | B_CHECK_PERMISSION, 0);

	return syscall_restart_handle_post(error);
}


/**
 * @brief Syscall: acquires a semaphore by @p count from user space, with timeout.
 *
 * Handles syscall-restart semantics for interruptible timed waits.
 *
 * @param id      The sem_id to acquire.
 * @param count   Number of units to acquire.
 * @param flags   Timeout/interrupt flags.
 * @param timeout Timeout value in microseconds.
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If the ID is invalid.
 * @retval B_TIMED_OUT   If the timeout expired.
 * @retval B_INTERRUPTED If interrupted by a signal.
 * @retval B_NOT_ALLOWED If the semaphore is kernel-owned.
 */
status_t
_user_acquire_sem_etc(sem_id id, int32 count, uint32 flags, bigtime_t timeout)
{
	syscall_restart_handle_timeout_pre(flags, timeout);

	status_t error = switch_sem_etc(-1, id, count,
		flags | B_CAN_INTERRUPT | B_CHECK_PERMISSION, timeout);

	return syscall_restart_handle_timeout_post(error, timeout);
}


/**
 * @brief Syscall: atomically releases one semaphore and acquires another from user space.
 *
 * @param releaseSem sem_id to release (pass -1 to skip).
 * @param id         sem_id to acquire.
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If @p id is invalid.
 * @retval B_INTERRUPTED If interrupted by a signal.
 * @retval B_NOT_ALLOWED If a semaphore is kernel-owned.
 */
status_t
_user_switch_sem(sem_id releaseSem, sem_id id)
{
	status_t error = switch_sem_etc(releaseSem, id, 1,
		B_CAN_INTERRUPT | B_CHECK_PERMISSION, 0);

	if (releaseSem < 0)
		return syscall_restart_handle_post(error);

	return error;
}


/**
 * @brief Syscall: atomically releases one semaphore and acquires another with timeout.
 *
 * @param releaseSem sem_id to release (pass -1 to skip).
 * @param id         sem_id to acquire.
 * @param count      Number of units to acquire.
 * @param flags      Timeout/interrupt flags.
 * @param timeout    Timeout value in microseconds.
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If @p id is invalid.
 * @retval B_TIMED_OUT   If the timeout expired.
 * @retval B_INTERRUPTED If interrupted by a signal.
 * @retval B_NOT_ALLOWED If a semaphore is kernel-owned.
 */
status_t
_user_switch_sem_etc(sem_id releaseSem, sem_id id, int32 count, uint32 flags,
	bigtime_t timeout)
{
	if (releaseSem < 0)
		syscall_restart_handle_timeout_pre(flags, timeout);

	status_t error = switch_sem_etc(releaseSem, id, count,
		flags | B_CAN_INTERRUPT | B_CHECK_PERMISSION, timeout);

	if (releaseSem < 0)
		return syscall_restart_handle_timeout_post(error, timeout);

	return error;
}


/**
 * @brief Syscall: releases a semaphore once from user space.
 *
 * @param id The sem_id to release.
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If the ID is invalid.
 * @retval B_NOT_ALLOWED If the semaphore is kernel-owned.
 */
status_t
_user_release_sem(sem_id id)
{
	return release_sem_etc(id, 1, B_CHECK_PERMISSION);
}


/**
 * @brief Syscall: releases a semaphore by @p count from user space, with flags.
 *
 * @param id    The sem_id to release.
 * @param count Number of units to release.
 * @param flags Release flags (e.g. B_RELEASE_ALL, B_DO_NOT_RESCHEDULE).
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If the ID is invalid.
 * @retval B_BAD_VALUE   If @p count <= 0 and B_RELEASE_ALL is not set.
 * @retval B_NOT_ALLOWED If the semaphore is kernel-owned.
 */
status_t
_user_release_sem_etc(sem_id id, int32 count, uint32 flags)
{
	return release_sem_etc(id, count, flags | B_CHECK_PERMISSION);
}


/**
 * @brief Syscall: reads a semaphore's current count into a user-space buffer.
 *
 * @param id        The sem_id to query.
 * @param userCount User-space pointer that receives the count value.
 * @retval B_OK          On success.
 * @retval B_BAD_ADDRESS If @p userCount is NULL or not a valid user address.
 * @retval B_BAD_SEM_ID  If the ID is invalid.
 */
status_t
_user_get_sem_count(sem_id id, int32 *userCount)
{
	status_t status;
	int32 count;

	if (userCount == NULL || !IS_USER_ADDRESS(userCount))
		return B_BAD_ADDRESS;

	status = get_sem_count(id, &count);
	if (status == B_OK && user_memcpy(userCount, &count, sizeof(int32)) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


/**
 * @brief Syscall: retrieves sem_info for a semaphore into a user-space buffer.
 *
 * @param id       The sem_id to query.
 * @param userInfo User-space pointer to a sem_info buffer to fill.
 * @param size     Must equal sizeof(sem_info).
 * @retval B_OK          On success.
 * @retval B_BAD_ADDRESS If @p userInfo is NULL or not a valid user address.
 * @retval B_BAD_SEM_ID  If the ID is invalid.
 */
status_t
_user_get_sem_info(sem_id id, struct sem_info *userInfo, size_t size)
{
	struct sem_info info;
	status_t status;

	if (userInfo == NULL || !IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	status = _get_sem_info(id, &info, size);
	if (status == B_OK && user_memcpy(userInfo, &info, size) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


/**
 * @brief Syscall: iterates over team semaphores, returning one sem_info per call.
 *
 * Copies the iteration cookie from user space, calls _get_next_sem_info(), and
 * writes back both the updated cookie and the sem_info on success.
 *
 * @param team       The team_id whose semaphores to enumerate.
 * @param userCookie User-space pointer to the int32 iteration cookie.
 * @param userInfo   User-space pointer to a sem_info buffer to fill.
 * @param size       Must equal sizeof(sem_info).
 * @retval B_OK          On success; @p userCookie and @p userInfo are updated.
 * @retval B_BAD_ADDRESS If any pointer argument is NULL or not a valid user address.
 * @retval B_BAD_VALUE   If no more semaphores exist.
 * @retval B_BAD_TEAM_ID If @p team is invalid.
 */
status_t
_user_get_next_sem_info(team_id team, int32 *userCookie, struct sem_info *userInfo,
	size_t size)
{
	struct sem_info info;
	int32 cookie;
	status_t status;

	if (userCookie == NULL || userInfo == NULL
		|| !IS_USER_ADDRESS(userCookie) || !IS_USER_ADDRESS(userInfo)
		|| user_memcpy(&cookie, userCookie, sizeof(int32)) < B_OK)
		return B_BAD_ADDRESS;

	status = _get_next_sem_info(team, &cookie, &info, size);

	if (status == B_OK) {
		if (user_memcpy(userInfo, &info, size) < B_OK
			|| user_memcpy(userCookie, &cookie, sizeof(int32)) < B_OK)
			return B_BAD_ADDRESS;
	}

	return status;
}


/**
 * @brief Syscall: transfers ownership of a semaphore to a different team.
 *
 * @param id   The sem_id to reassign.
 * @param team The team_id of the new owner.
 * @retval B_OK          On success.
 * @retval B_BAD_SEM_ID  If @p id is invalid.
 * @retval B_BAD_TEAM_ID If @p team is invalid.
 */
status_t
_user_set_sem_owner(sem_id id, team_id team)
{
	return set_sem_owner(id, team);
}
