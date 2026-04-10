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
 *   Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2008-2010, Axel Dörfler. All Rights Reserved.
 *   Copyright 2007, Hugo Santos. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file ObjectDepot.cpp
 * @brief Per-CPU magazine depot for the slab allocator.
 *
 * ObjectDepot maintains a set of per-CPU magazines (arrays of cached object
 * pointers) to allow lock-free alloc/free in the common case. When a magazine
 * is exhausted or full, it is exchanged with the depot's full/empty magazine
 * lists under a spinlock.
 *
 * @see ObjectCache.cpp, Slab.cpp
 */


#include "ObjectDepot.h"

#include <algorithm>

#include <interrupts.h>
#include <slab/Slab.h>
#include <smp.h>
#include <util/AutoLock.h>

#include "slab_debug.h"
#include "slab_private.h"
#include "slab_queue.h"


struct DepotMagazine : public slab_queue_link {
			uint16				current_round;
			uint16				round_count;
			void*				rounds[0];

public:
	inline	bool				IsEmpty() const;
	inline	bool				IsFull() const;

	inline	void*				Pop();
	inline	bool				Push(void* object);

#if PARANOID_KERNEL_FREE
			bool				ContainsObject(void* object) const;
#endif
};


#if PARANOID_KERNEL_FREE
struct depot_cpu_store {
	DepotMagazine*	obtain;
	DepotMagazine*	store;
};
#else
struct depot_cpu_store {
	DepotMagazine*	loaded;
	DepotMagazine*	previous;
};
#endif


RANGE_MARKER_FUNCTION_BEGIN(SlabObjectDepot)


/**
 * @brief Returns true if the magazine holds no objects.
 *
 * @return true if current_round == 0, false otherwise.
 * @note Inline; no locking — caller must hold appropriate CPU-local context.
 */
bool
DepotMagazine::IsEmpty() const
{
	return current_round == 0;
}


/**
 * @brief Returns true if the magazine is at full capacity.
 *
 * @return true if current_round == round_count, false otherwise.
 * @note Inline; no locking — caller must hold appropriate CPU-local context.
 */
bool
DepotMagazine::IsFull() const
{
	return current_round == round_count;
}


/**
 * @brief Pops and returns the top object from the magazine.
 *
 * @return Pointer to the most recently pushed object.
 * @note Caller must ensure the magazine is not empty before calling.
 *       No locking is performed; this is intended for CPU-local fast path use.
 */
void*
DepotMagazine::Pop()
{
	return rounds[--current_round];
}


/**
 * @brief Pushes an object onto the magazine.
 *
 * @param object Pointer to the object to store.
 * @retval true  Object was successfully stored.
 * @retval false Magazine is full; object was not stored.
 * @note No locking is performed; this is intended for CPU-local fast path use.
 */
bool
DepotMagazine::Push(void* object)
{
	if (IsFull())
		return false;

	rounds[current_round++] = object;
	return true;
}


#if PARANOID_KERNEL_FREE

/**
 * @brief Checks whether a given object pointer exists in this magazine.
 *
 * @param object Pointer to the object to search for.
 * @retval true  The object was found in the magazine's active rounds.
 * @retval false The object was not found.
 * @note Only compiled when PARANOID_KERNEL_FREE is enabled. Used to detect
 *       double-free errors before an object is reused.
 */
bool
DepotMagazine::ContainsObject(void* object) const
{
	for (uint16 i = 0; i < current_round; i++) {
		if (rounds[i] == object)
			return true;
	}

	return false;
}

#endif // PARANOID_KERNEL_FREE


// #pragma mark -


/**
 * @brief Allocates a new magazine with capacity determined by the depot.
 *
 * @param depot Pointer to the owning object_depot; provides magazine_capacity.
 * @param flags Allocation flags (e.g., CACHE_DONT_SLEEP).
 * @return Pointer to the newly allocated DepotMagazine, or NULL on failure.
 * @note Called outside of CPU-local context; may block depending on @p flags.
 */
static DepotMagazine*
alloc_magazine(object_depot* depot, uint32 flags)
{
	DepotMagazine* magazine = (DepotMagazine*)slab_internal_alloc(
		sizeof(DepotMagazine) + depot->magazine_capacity * sizeof(void*),
		flags);
	if (magazine) {
		magazine->next = NULL;
		magazine->current_round = 0;
		magazine->round_count = depot->magazine_capacity;
	}

	return magazine;
}


/**
 * @brief Frees a magazine back to the slab allocator.
 *
 * @param magazine Pointer to the magazine to free.
 * @param flags    Allocation flags used during the original allocation.
 * @note Does not return contained objects; call empty_magazine() first if
 *       the magazine still holds objects.
 */
static void
free_magazine(DepotMagazine* magazine, uint32 flags)
{
	slab_internal_free(magazine, flags);
}


/**
 * @brief Returns all objects in a magazine to the depot's return_object
 *        callback, then frees the magazine itself.
 *
 * @param depot    Pointer to the owning object_depot.
 * @param magazine Pointer to the magazine whose objects are to be returned.
 * @param flags    Allocation flags forwarded to return_object and free_magazine.
 * @note Must not be called with depot->inner_lock held, as return_object may
 *       re-enter the allocator.
 */
static void
empty_magazine(object_depot* depot, DepotMagazine* magazine, uint32 flags)
{
	for (uint16 i = 0; i < magazine->current_round; i++)
		depot->return_object(depot, depot->cookie, magazine->rounds[i], flags);
	free_magazine(magazine, flags);
}


/**
 * @brief Exchanges an empty magazine for a full one from the depot's full list.
 *
 * If the depot has at least one full magazine, the caller's empty magazine is
 * pushed onto the depot's empty list and replaced with a full magazine.
 *
 * @param depot    Pointer to the owning object_depot.
 * @param magazine In/out: pointer to the caller's (empty) magazine slot.
 *                 On success, updated to point to a full magazine from the depot.
 * @retval true  Exchange succeeded; @p magazine now points to a full magazine.
 * @retval false No full magazines available in the depot.
 * @note Acquires depot->inner_lock (spinlock) internally.
 */
static bool
exchange_with_full(object_depot* depot, DepotMagazine*& magazine)
{
	ASSERT(magazine == NULL || magazine->IsEmpty());

	SpinLocker _(depot->inner_lock);

	if (depot->full.head == NULL)
		return false;

	if (magazine != NULL) {
		depot->empty.Push(magazine);
		depot->empty_count++;
	}

	magazine = (DepotMagazine*)depot->full.Pop();
	depot->full_count--;
	return true;
}


/**
 * @brief Exchanges a full magazine for an empty one from the depot's empty list.
 *
 * If the depot has at least one empty magazine, the caller's full magazine is
 * pushed onto the depot's full list (if there is room) or returned via
 * @p freeMagazine, and replaced with an empty magazine.
 *
 * @param depot        Pointer to the owning object_depot.
 * @param magazine     In/out: pointer to the caller's (full) magazine slot.
 *                     On success, updated to point to an empty magazine.
 * @param freeMagazine Out: if the full magazine could not be stored (depot at
 *                     max_count), set to the displaced full magazine so the
 *                     caller can free it. Set to NULL otherwise.
 * @retval true  Exchange succeeded; @p magazine now points to an empty magazine.
 * @retval false No empty magazines available in the depot.
 * @note Acquires depot->inner_lock (spinlock) internally.
 *       When PARANOID_KERNEL_FREE is enabled, the magazine's rounds are
 *       reversed to enforce FIFO ordering for deferred object reuse.
 */
static bool
exchange_with_empty(object_depot* depot, DepotMagazine*& magazine,
	DepotMagazine*& freeMagazine)
{
	ASSERT(magazine == NULL || magazine->IsFull());

#if PARANOID_KERNEL_FREE
	if (magazine != NULL) {
		// Reverse the rounds in the magazine so we get FIFO, not LIFO.
		for (int i = 0; i < magazine->current_round / 2; i++)
			std::swap(magazine->rounds[i], magazine->rounds[magazine->current_round - i - 1]);
	}
#endif

	SpinLocker _(depot->inner_lock);

	if (depot->empty.head == NULL)
		return false;

	if (magazine != NULL) {
		if (depot->full_count < depot->max_count) {
			depot->full.Push(magazine);
			depot->full_count++;
			freeMagazine = NULL;
		} else
			freeMagazine = magazine;
	}

	magazine = (DepotMagazine*)depot->empty.Pop();
	depot->empty_count--;
	return true;
}


/**
 * @brief Pushes a magazine onto the depot's empty list.
 *
 * @param depot    Pointer to the owning object_depot.
 * @param magazine Pointer to the empty magazine to add.
 * @note Acquires depot->inner_lock (spinlock) internally.
 */
static void
push_empty_magazine(object_depot* depot, DepotMagazine* magazine)
{
	SpinLocker _(depot->inner_lock);

	depot->empty.Push(magazine);
	depot->empty_count++;
}


/**
 * @brief Returns the per-CPU depot store for the currently executing CPU.
 *
 * @param depot Pointer to the owning object_depot.
 * @return Pointer to the depot_cpu_store for the current CPU.
 * @note Must be called with interrupts disabled to ensure the CPU does not
 *       change between the call to smp_get_current_cpu() and use of the store.
 */
static inline depot_cpu_store*
object_depot_cpu(object_depot* depot)
{
	return &depot->stores[smp_get_current_cpu()];
}


// #pragma mark - public API


/**
 * @brief Initialises an object_depot structure.
 *
 * Allocates per-CPU magazine store arrays, initialises the full/empty magazine
 * lists, and sets up the reader-writer lock and inner spinlock.
 *
 * @param depot         Pointer to the object_depot to initialise.
 * @param capacity      Number of object slots per magazine.
 * @param maxCount      Maximum number of full magazines the depot may hold.
 * @param flags         Allocation flags for internal memory (e.g., CACHE_DONT_SLEEP).
 * @param cookie        Opaque pointer forwarded to @p return_object.
 * @param return_object Callback invoked to return an object to the underlying
 *                      object cache when a magazine is drained during teardown
 *                      or low-memory reclaim.
 * @retval B_OK        Initialisation succeeded.
 * @retval B_NO_MEMORY Failed to allocate the per-CPU store array.
 * @note Must be called once before any alloc/free operations on @p depot.
 *       Not re-entrant; external synchronisation required if called concurrently.
 */
status_t
object_depot_init(object_depot* depot, size_t capacity, size_t maxCount,
	uint32 flags, void* cookie, void (*return_object)(object_depot* depot,
		void* cookie, void* object, uint32 flags))
{
	depot->full.Init();
	depot->empty.Init();
	depot->full_count = depot->empty_count = 0;
	depot->max_count = maxCount;
	depot->magazine_capacity = capacity;

	rw_lock_init(&depot->outer_lock, "object depot");
	B_INITIALIZE_SPINLOCK(&depot->inner_lock);

	int cpuCount = smp_get_num_cpus();
	depot->stores = (depot_cpu_store*)slab_internal_alloc(
		sizeof(depot_cpu_store) * cpuCount, flags);
	if (depot->stores == NULL) {
		rw_lock_destroy(&depot->outer_lock);
		return B_NO_MEMORY;
	}

	for (int i = 0; i < cpuCount; i++) {
#if PARANOID_KERNEL_FREE
		depot->stores[i].obtain = NULL;
		depot->stores[i].store = NULL;
#else
		depot->stores[i].loaded = NULL;
		depot->stores[i].previous = NULL;
#endif
	}

	depot->cookie = cookie;
	depot->return_object = return_object;

	return B_OK;
}


/**
 * @brief Destroys an object_depot, returning all cached objects and freeing
 *        all internal resources.
 *
 * Calls object_depot_make_empty() to flush every magazine, frees the per-CPU
 * store array, and destroys the outer reader-writer lock.
 *
 * @param depot Pointer to the object_depot to destroy.
 * @param flags Allocation flags forwarded to internal free operations.
 * @note Must not be called while any thread is concurrently allocating from
 *       or freeing to the depot. After this call @p depot is invalid.
 */
void
object_depot_destroy(object_depot* depot, uint32 flags)
{
	object_depot_make_empty(depot, flags);

	slab_internal_free(depot->stores, flags);

	rw_lock_destroy(&depot->outer_lock);
}


/**
 * @brief Obtains a cached object from the per-CPU magazine (fast path alloc).
 *
 * Attempts to pop an object from the current CPU's loaded magazine. If that
 * magazine is empty, tries to swap with the previous magazine or exchange with
 * a full magazine from the depot. Returns NULL if no cached objects are
 * available (caller should fall through to the slab slow path).
 *
 * When PARANOID_KERNEL_FREE is enabled, always exchanges with the depot's full
 * list to enforce deferred reuse (FIFO ordering).
 *
 * @param depot Pointer to the owning object_depot.
 * @return Pointer to a cached object, or NULL if none are available.
 * @note Acquires depot->outer_lock (read) and disables interrupts internally.
 *       CPU-local; must be called in a preemptible kernel context.
 */
void*
object_depot_obtain(object_depot* depot)
{
	ReadLocker readLocker(depot->outer_lock);
	InterruptsLocker interruptsLocker;

	depot_cpu_store* store = object_depot_cpu(depot);

#if PARANOID_KERNEL_FREE
	// When paranoid free is enabled, we want to defer object reuse,
	// instead of reusing as rapidly as possible. Thus we always exchange
	// full and empty magazines with the depot.

	if (store->obtain == NULL || store->obtain->IsEmpty()) {
		if (!exchange_with_full(depot, store->obtain))
			return NULL;
	}

	return store->obtain->Pop();
#else
	// To better understand both the Alloc() and Free() logic refer to
	// Bonwick's ``Magazines and Vmem'' [in 2001 USENIX proceedings]

	// In a nutshell, we try to get an object from the loaded magazine
	// if it's not empty, or from the previous magazine if it's full
	// and finally from the Slab if the magazine depot has no full magazines.

	if (store->loaded == NULL)
		return NULL;

	while (true) {
		if (!store->loaded->IsEmpty())
			return store->loaded->Pop();

		if (store->previous != NULL
			&& (store->previous->IsFull()
				|| exchange_with_full(depot, store->previous))) {
			std::swap(store->previous, store->loaded);
		} else
			return NULL;
	}
#endif
}


/**
 * @brief Stores a freed object into the per-CPU magazine (fast path free).
 *
 * Attempts to push the object onto the current CPU's loaded magazine. If that
 * magazine is full, tries to swap with the previous magazine or exchange with
 * an empty magazine from the depot. If no empty magazine is available,
 * allocates a new one. As a last resort, returns the object directly via
 * depot->return_object().
 *
 * @param depot  Pointer to the owning object_depot.
 * @param object Pointer to the object being freed.
 * @param flags  Allocation flags forwarded to magazine allocation and
 *               return_object callbacks.
 * @note Acquires depot->outer_lock (read) and disables interrupts internally.
 *       CPU-local; must be called in a preemptible kernel context.
 */
void
object_depot_store(object_depot* depot, void* object, uint32 flags)
{
	ReadLocker readLocker(depot->outer_lock);
	InterruptsLocker interruptsLocker;

	depot_cpu_store* store = object_depot_cpu(depot);

	while (true) {
#if PARANOID_KERNEL_FREE
		if (store->store != NULL && store->store->Push(object))
			return;

		DepotMagazine* freeMagazine = NULL;
		if (exchange_with_empty(depot, store->store, freeMagazine)) {
#else
		// We try to add the object to the loaded magazine if we have one
		// and it's not full, or to the previous one if it is empty. If
		// the magazine depot doesn't provide us with a new empty magazine
		// we return the object directly to the slab.

		if (store->loaded != NULL && store->loaded->Push(object))
			return;

		DepotMagazine* freeMagazine = NULL;
		if ((store->previous != NULL && store->previous->IsEmpty())
			|| exchange_with_empty(depot, store->previous, freeMagazine)) {
			std::swap(store->loaded, store->previous);
#endif
			if (freeMagazine != NULL) {
				// Free the magazine that didn't have space in the list
				interruptsLocker.Unlock();
				readLocker.Unlock();

				empty_magazine(depot, freeMagazine, flags);

				readLocker.Lock();
				interruptsLocker.Lock();

				store = object_depot_cpu(depot);
			}
		} else {
			// allocate a new empty magazine
			interruptsLocker.Unlock();
			readLocker.Unlock();

			DepotMagazine* magazine = alloc_magazine(depot, flags);
			if (magazine == NULL) {
				depot->return_object(depot, depot->cookie, object, flags);
				return;
			}

			readLocker.Lock();
			interruptsLocker.Lock();

			push_empty_magazine(depot, magazine);
			store = object_depot_cpu(depot);
		}
	}
}


/**
 * @brief Flushes all cached objects from every magazine in the depot.
 *
 * Collects magazines from every CPU's store, plus the depot's full and empty
 * lists. Full/store magazines have their objects returned via
 * depot->return_object(). Empty magazines are simply freed. On return the depot
 * holds no cached objects and all magazine lists are empty.
 *
 * @param depot Pointer to the object_depot to drain.
 * @param flags Allocation flags forwarded to empty_magazine() and free_magazine().
 * @note Acquires depot->outer_lock (write) during collection of per-CPU stores
 *       and depot magazine lists, then releases it before freeing objects to
 *       avoid holding the write lock during potentially blocking callbacks.
 */
void
object_depot_make_empty(object_depot* depot, uint32 flags)
{
	WriteLocker writeLocker(depot->outer_lock);

	// collect the store magazines

	slab_queue storeMagazines;
	storeMagazines.Init();

	int cpuCount = smp_get_num_cpus();
	for (int i = 0; i < cpuCount; i++) {
		depot_cpu_store& store = depot->stores[i];

#if PARANOID_KERNEL_FREE
		if (store.obtain != NULL) {
			storeMagazines.Push(store.obtain);
			store.obtain = NULL;
		}

		if (store.store != NULL) {
			storeMagazines.Push(store.store);
			store.store = NULL;
		}
#else
		if (store.loaded != NULL) {
			storeMagazines.Push(store.loaded);
			store.loaded = NULL;
		}

		if (store.previous != NULL) {
			storeMagazines.Push(store.previous);
			store.previous = NULL;
		}
#endif
	}

	// detach the depot's full and empty magazines

	slab_queue fullMagazines = depot->full;
	depot->full.Init();

	slab_queue emptyMagazines = depot->empty;
	depot->empty.Init();

	writeLocker.Unlock();

	// free all magazines

	while (storeMagazines.head != NULL)
		empty_magazine(depot, (DepotMagazine*)storeMagazines.Pop(), flags);

	while (fullMagazines.head != NULL)
		empty_magazine(depot, (DepotMagazine*)fullMagazines.Pop(), flags);

	while (emptyMagazines.head != NULL)
		free_magazine((DepotMagazine*)emptyMagazines.Pop(), flags);
}


#if PARANOID_KERNEL_FREE

/**
 * @brief Checks whether a given object is currently cached in any magazine
 *        of the depot (paranoid double-free detection).
 *
 * Scans every CPU's obtain/store magazines and all full depot magazines to
 * determine whether @p object is present. Used by the paranoid free path to
 * detect attempts to free an already-freed object.
 *
 * @param depot  Pointer to the owning object_depot.
 * @param object Pointer to the object to search for.
 * @retval true  The object was found in a cached magazine (likely a double-free).
 * @retval false The object was not found in any cached magazine.
 * @note Only compiled when PARANOID_KERNEL_FREE is enabled. Acquires
 *       depot->outer_lock (write) for the duration of the scan.
 */
bool
object_depot_contains_object(object_depot* depot, void* object)
{
	WriteLocker writeLocker(depot->outer_lock);

	int cpuCount = smp_get_num_cpus();
	for (int i = 0; i < cpuCount; i++) {
		depot_cpu_store& store = depot->stores[i];

		if (store.obtain != NULL && !store.obtain->IsEmpty()) {
			if (store.obtain->ContainsObject(object))
				return true;
		}

		if (store.store != NULL && !store.store->IsEmpty()) {
			if (store.store->ContainsObject(object))
				return true;
		}
	}

	for (DepotMagazine* magazine = (DepotMagazine*)depot->full.head; magazine != NULL;
			magazine = (DepotMagazine*)magazine->next) {
		if (magazine->ContainsObject(object))
			return true;
	}

	return false;
}

#endif // PARANOID_KERNEL_FREE


// #pragma mark - private kernel API


/**
 * @brief Prints a summary of an object_depot to the kernel debugger.
 *
 * Outputs full/empty magazine counts, maximum count, per-magazine capacity,
 * and the loaded/previous (or obtain/store) magazine pointers for every CPU.
 *
 * @param depot Pointer to the object_depot to display.
 * @note Intended for use from the kernel debugger only; not safe to call from
 *       normal kernel code while the depot is in use.
 */
void
dump_object_depot(object_depot* depot)
{
	kprintf("  full:     %p, count %lu\n", depot->full.head, depot->full_count);
	kprintf("  empty:    %p, count %lu\n", depot->empty.head, depot->empty_count);
	kprintf("  max full: %lu\n", depot->max_count);
	kprintf("  capacity: %lu\n", depot->magazine_capacity);
	kprintf("  stores:\n");

	int cpuCount = smp_get_num_cpus();

#if PARANOID_KERNEL_FREE
	for (int i = 0; i < cpuCount; i++) {
		kprintf("  [%d] obtain:   %p\n", i, depot->stores[i].obtain);
		kprintf("      store:    %p\n", depot->stores[i].store);
	}
#else
	for (int i = 0; i < cpuCount; i++) {
		kprintf("  [%d] loaded:   %p\n", i, depot->stores[i].loaded);
		kprintf("      previous: %p\n", depot->stores[i].previous);
	}
#endif
}


/**
 * @brief Kernel debugger command: dump an object_depot by address.
 *
 * Parses one argument (the address of an object_depot) and calls the
 * single-argument dump_object_depot() overload to display its contents.
 *
 * @param argCount Number of command-line arguments (including command name).
 * @param args     Array of argument strings; args[1] must be a depot address.
 * @return Always returns 0.
 * @note Registered with add_debugger_command(); safe to call only from the
 *       kernel debugger context.
 */
int
dump_object_depot(int argCount, char** args)
{
	if (argCount != 2)
		kprintf("usage: %s [address]\n", args[0]);
	else
		dump_object_depot((object_depot*)parse_expression(args[1]));

	return 0;
}


/**
 * @brief Kernel debugger command: dump a single depot magazine by address.
 *
 * Parses one argument (the address of a DepotMagazine) and prints its
 * next pointer, current_round, round_count, and all stored object pointers.
 *
 * @param argCount Number of command-line arguments (including command name).
 * @param args     Array of argument strings; args[1] must be a magazine address.
 * @return Always returns 0.
 * @note Registered with add_debugger_command(); safe to call only from the
 *       kernel debugger context.
 */
int
dump_depot_magazine(int argCount, char** args)
{
	if (argCount != 2) {
		kprintf("usage: %s [address]\n", args[0]);
		return 0;
	}

	DepotMagazine* magazine = (DepotMagazine*)parse_expression(args[1]);

	kprintf("next:          %p\n", magazine->next);
	kprintf("current_round: %u\n", magazine->current_round);
	kprintf("round_count:   %u\n", magazine->round_count);

	for (uint16 i = 0; i < magazine->current_round; i++)
		kprintf("  [%i] %p\n", i, magazine->rounds[i]);

	return 0;
}


RANGE_MARKER_FUNCTION_END(SlabObjectDepot)
