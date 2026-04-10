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
 *   Copyright 2013, Paweł Dziepak, pdziepak@quarnos.org.
 *   Copyright 2008-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file smp.cpp
 * @brief Symmetric Multi-Processing (SMP) support — IPI dispatch and CPU startup.
 *
 * Implements inter-processor interrupt (IPI) messaging, CPU bring-up for AP
 * (application processor) cores, and the SMP broadcast/unicast send mechanisms.
 * Also handles TLB shootdown coordination and the SMP message loop.
 *
 * @see cpu.cpp, arch/x86/arch_smp.cpp
 */


#include <smp.h>

#include <stdlib.h>
#include <string.h>

#include <arch/atomic.h>
#include <arch/cpu.h>
#include <arch/debug.h>
#include <arch/int.h>
#include <arch/smp.h>
#include <boot/kernel_args.h>
#include <cpu.h>
#include <generic_syscall.h>
#include <interrupts.h>
#include <spinlock_contention.h>
#include <thread.h>
#include <util/atomic.h>

#include "kernel_debug_config.h"


//#define TRACE_SMP
#ifdef TRACE_SMP
#	define TRACE(...) dprintf_no_syslog(__VA_ARGS__)
#else
#	define TRACE(...) (void)0
#endif


#undef try_acquire_spinlock
#undef acquire_spinlock
#undef release_spinlock

#undef try_acquire_read_spinlock
#undef acquire_read_spinlock
#undef release_read_spinlock
#undef try_acquire_write_spinlock
#undef acquire_write_spinlock
#undef release_write_spinlock

#undef try_acquire_write_seqlock
#undef acquire_write_seqlock
#undef release_write_seqlock
#undef acquire_read_seqlock
#undef release_read_seqlock


#define MSG_ALLOCATE_PER_CPU		(4)

// These macros define the number of unsuccessful iterations in
// acquire_spinlock() and acquire_spinlock_nocheck() after which the functions
// panic(), assuming a deadlock.
#define SPINLOCK_DEADLOCK_COUNT				100000000
#define SPINLOCK_DEADLOCK_COUNT_NO_CHECK	2000000000


struct smp_msg {
	struct smp_msg	*next;
	int32			message;
	addr_t			data;
	addr_t			data2;
	addr_t			data3;
	void			*data_ptr;
	uint32			flags;
	int32			ref_count;
	int32			done;
	CPUSet			proc_bitmap;
};

enum mailbox_source {
	MAILBOX_LOCAL,
	MAILBOX_BCAST,
};

static int32 sBootCPUSpin = 0;

static int32 sEarlyCPUCallCount;
static CPUSet sEarlyCPUCallSet;
static void (*sEarlyCPUCallFunction)(void*, int);
void* sEarlyCPUCallCookie;

static struct smp_msg* sFreeMessages = NULL;
static int32 sFreeMessageCount = 0;
static rw_spinlock sFreeMessageSpinlock = B_RW_SPINLOCK_INITIALIZER;

static struct smp_msg* sCPUMessages[SMP_MAX_CPUS] = { NULL, };

static struct smp_msg* sBroadcastMessages = NULL;
static rw_spinlock sBroadcastMessageSpinlock = B_RW_SPINLOCK_INITIALIZER;
static int32 sBroadcastMessageCounter;

static bool sICIEnabled = false;
static int32 sNumCPUs = 1;

static int32 process_pending_ici(int32 currentCPU);


#if DEBUG_SPINLOCKS
#define NUM_LAST_CALLERS	32

static struct {
	void		*caller;
	spinlock	*lock;
} sLastCaller[NUM_LAST_CALLERS];

static int32 sLastIndex = 0;
	// Is incremented atomically. Must be % NUM_LAST_CALLERS before being used
	// as index into sLastCaller. Note, that it has to be casted to uint32
	// before applying the modulo operation, since otherwise after overflowing
	// that would yield negative indices.


/**
 * @brief Record the most recent caller that acquired a spinlock for debugging.
 *
 * @param caller The return address of the caller (from arch_debug_get_caller()).
 * @param lock   The spinlock being acquired.
 * @note Only compiled in when DEBUG_SPINLOCKS is enabled.
 */
static void
push_lock_caller(void* caller, spinlock* lock)
{
	int32 index = (uint32)atomic_add(&sLastIndex, 1) % NUM_LAST_CALLERS;

	sLastCaller[index].caller = caller;
	sLastCaller[index].lock = lock;
}


/**
 * @brief Search the recent-caller ring-buffer for the last acquirer of a spinlock.
 *
 * @param lock The spinlock whose last acquirer is sought.
 * @return Pointer to the caller's code location, or NULL if not found.
 * @note Only compiled in when DEBUG_SPINLOCKS is enabled.
 */
static void*
find_lock_caller(spinlock* lock)
{
	int32 lastIndex = (uint32)atomic_get(&sLastIndex) % NUM_LAST_CALLERS;

	for (int32 i = 0; i < NUM_LAST_CALLERS; i++) {
		int32 index = (NUM_LAST_CALLERS + lastIndex - 1 - i) % NUM_LAST_CALLERS;
		if (sLastCaller[index].lock == lock)
			return sLastCaller[index].caller;
	}

	return NULL;
}


/**
 * @brief Kernel debugger command: dump state and contention info for a spinlock.
 *
 * @param argc Argument count; must be 2 (command name + address expression).
 * @param argv Argument vector; argv[1] is an address expression for the spinlock.
 * @retval 0 Always.
 * @note Only compiled in when DEBUG_SPINLOCKS is enabled. Must be called from
 *       the kernel debugger (KDL) context only.
 */
int
dump_spinlock(int argc, char** argv)
{
	if (argc != 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	uint64 address;
	if (!evaluate_debug_expression(argv[1], &address, false))
		return 0;

	spinlock* lock = (spinlock*)(addr_t)address;
	kprintf("spinlock %p:\n", lock);
	bool locked = B_SPINLOCK_IS_LOCKED(lock);
	if (locked) {
		kprintf("  locked from %p\n", find_lock_caller(lock));
	} else
		kprintf("  not locked\n");

#if B_DEBUG_SPINLOCK_CONTENTION
	kprintf("  failed try_acquire():		%d\n", lock->failed_try_acquire);
	kprintf("  total wait time:		%" B_PRIdBIGTIME "\n", lock->total_wait);
	kprintf("  total held time:		%" B_PRIdBIGTIME "\n", lock->total_held);
	kprintf("  last acquired at:		%" B_PRIdBIGTIME "\n", lock->last_acquired);
#endif

	return 0;
}


#endif	// DEBUG_SPINLOCKS


#if B_DEBUG_SPINLOCK_CONTENTION


/**
 * @brief Update the wait-time contention counter after a spinlock is acquired.
 *
 * @param lock  The spinlock that was just acquired.
 * @param start Timestamp (from system_time()) recorded before the spin loop began.
 * @note Only compiled in when B_DEBUG_SPINLOCK_CONTENTION is enabled.
 */
static inline void
update_lock_contention(spinlock* lock, bigtime_t start)
{
	const bigtime_t now = system_time();
	lock->last_acquired = now;
	lock->total_wait += (now - start);
}


/**
 * @brief Update the held-time contention counter when a spinlock is released.
 *
 * @param lock The spinlock that is about to be released.
 * @note Only compiled in when B_DEBUG_SPINLOCK_CONTENTION is enabled.
 *       Will panic if the lock was held longer than DEBUG_SPINLOCK_LATENCIES
 *       (when that compile-time threshold is defined).
 */
static inline void
update_lock_held(spinlock* lock)
{
	const bigtime_t held = (system_time() - lock->last_acquired);
	lock->total_held += held;

//#define DEBUG_SPINLOCK_LATENCIES 2000
#if DEBUG_SPINLOCK_LATENCIES
	if (held > DEBUG_SPINLOCK_LATENCIES) {
		panic("spinlock %p was held for %" B_PRIdBIGTIME " usecs (%d allowed)\n",
			lock, held, DEBUG_SPINLOCK_LATENCIES);
	}
#endif // DEBUG_SPINLOCK_LATENCIES
}


#endif // B_DEBUG_SPINLOCK_CONTENTION


/**
 * @brief Kernel debugger command: dump all pending ICI messages across every CPU mailbox.
 *
 * @param argc Argument count (unused; no arguments required).
 * @param argv Argument vector (unused).
 * @retval 0 Always.
 * @note Must be called from the kernel debugger (KDL) context only.
 */
int
dump_ici_messages(int argc, char** argv)
{
	// count broadcast messages
	int32 count = 0;
	int32 doneCount = 0;
	int32 unreferencedCount = 0;
	smp_msg* message = sBroadcastMessages;
	while (message != NULL) {
		count++;
		if (message->done == 1)
			doneCount++;
		if (message->ref_count <= 0)
			unreferencedCount++;
		message = message->next;
	}

	kprintf("ICI broadcast messages: %" B_PRId32 ", first: %p\n", count,
		sBroadcastMessages);
	kprintf("  done:         %" B_PRId32 "\n", doneCount);
	kprintf("  unreferenced: %" B_PRId32 "\n", unreferencedCount);

	// count per-CPU messages
	for (int32 i = 0; i < sNumCPUs; i++) {
		count = 0;
		message = sCPUMessages[i];
		while (message != NULL) {
			count++;
			message = message->next;
		}

		kprintf("CPU %" B_PRId32 " messages: %" B_PRId32 ", first: %p\n", i,
			count, sCPUMessages[i]);
	}

	return 0;
}


/**
 * @brief Kernel debugger command: dump all fields of a single ICI message structure.
 *
 * @param argc Argument count; must be 2 (command name + address expression).
 * @param argv Argument vector; argv[1] is an address expression for the smp_msg.
 * @retval 0 Always.
 * @note Must be called from the kernel debugger (KDL) context only.
 */
int
dump_ici_message(int argc, char** argv)
{
	if (argc != 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	uint64 address;
	if (!evaluate_debug_expression(argv[1], &address, false))
		return 0;

	smp_msg* message = (smp_msg*)(addr_t)address;
	kprintf("ICI message %p:\n", message);
	kprintf("  next:        %p\n", message->next);
	kprintf("  message:     %" B_PRId32 "\n", message->message);
	kprintf("  data:        0x%lx\n", message->data);
	kprintf("  data2:       0x%lx\n", message->data2);
	kprintf("  data3:       0x%lx\n", message->data3);
	kprintf("  data_ptr:    %p\n", message->data_ptr);
	kprintf("  flags:       %" B_PRIx32 "\n", message->flags);
	kprintf("  ref_count:   %" B_PRIx32 "\n", message->ref_count);
	kprintf("  done:        %s\n", message->done == 1 ? "true" : "false");

	kprintf("  proc_bitmap: ");
	for (int32 i = 0; i < sNumCPUs; i++) {
		if (message->proc_bitmap.GetBit(i))
			kprintf("%s%" B_PRId32, i != 0 ? ", " : "", i);
	}
	kprintf("\n");

	return 0;
}


/**
 * @brief Drain all pending ICI messages for the calling CPU until the mailbox is empty.
 *
 * @param currentCPU Index of the CPU whose mailbox is being drained.
 * @note Must be called with interrupts disabled. Calls process_pending_ici()
 *       in a loop until it returns B_ENTRY_NOT_FOUND.
 */
static inline void
process_all_pending_ici(int32 currentCPU)
{
	while (process_pending_ici(currentCPU) != B_ENTRY_NOT_FOUND)
		;
}


/**
 * @brief Attempt to acquire a spinlock without blocking, returning immediately on contention.
 *
 * @param lock The spinlock to attempt to acquire.
 * @retval true  The spinlock was successfully acquired.
 * @retval false The spinlock was already held by another CPU.
 * @note Interrupts must be disabled before calling this function.
 *       Safe to call from interrupt context.
 */
bool
try_acquire_spinlock(spinlock* lock)
{
#if DEBUG_SPINLOCKS
	if (are_interrupts_enabled()) {
		panic("try_acquire_spinlock: attempt to acquire lock %p with "
			"interrupts enabled", lock);
	}
#endif

	if (atomic_get_and_set(&lock->lock, 1) != 0) {
#if B_DEBUG_SPINLOCK_CONTENTION
		atomic_add(&lock->failed_try_acquire, 1);
#endif
		return false;
	}

#if B_DEBUG_SPINLOCK_CONTENTION
	update_lock_contention(lock, system_time());
#endif

#if DEBUG_SPINLOCKS
	push_lock_caller(arch_debug_get_caller(), lock);
#endif

	return true;
}


/**
 * @brief Acquire a spinlock, spinning until it becomes available.
 *
 * @param lock The spinlock to acquire.
 * @note Interrupts must be disabled before calling this function.
 *       On SMP systems the function processes pending ICIs while spinning
 *       to avoid deadlocks. Will panic() if the lock cannot be obtained
 *       within SPINLOCK_DEADLOCK_COUNT iterations, indicating a deadlock.
 */
void
acquire_spinlock(spinlock* lock)
{
#if DEBUG_SPINLOCKS
	if (are_interrupts_enabled()) {
		panic("acquire_spinlock: attempt to acquire lock %p with interrupts "
			"enabled", lock);
	}
#endif

	if (sNumCPUs > 1) {
#if B_DEBUG_SPINLOCK_CONTENTION
		const bigtime_t start = system_time();
#endif
		int currentCPU = smp_get_current_cpu();
		while (1) {
			uint32 count = 0;
			while (lock->lock != 0) {
				if (++count == SPINLOCK_DEADLOCK_COUNT) {
#if DEBUG_SPINLOCKS
					panic("acquire_spinlock(): Failed to acquire spinlock %p "
						"for a long time (last caller: %p, value: %" B_PRIx32
						")", lock, find_lock_caller(lock), lock->lock);
#else
					panic("acquire_spinlock(): Failed to acquire spinlock %p "
						"for a long time (value: %" B_PRIx32 ")", lock,
						lock->lock);
#endif
					count = 0;
				}

				process_all_pending_ici(currentCPU);
				cpu_wait(&lock->lock, 0);
			}
			if (atomic_get_and_set(&lock->lock, 1) == 0)
				break;
		}

#if B_DEBUG_SPINLOCK_CONTENTION
		update_lock_contention(lock, start);
#endif

#if DEBUG_SPINLOCKS
		push_lock_caller(arch_debug_get_caller(), lock);
#endif
	} else {
#if B_DEBUG_SPINLOCK_CONTENTION
		lock->last_acquired = system_time();
#endif
#if DEBUG_SPINLOCKS
		int32 oldValue = atomic_get_and_set(&lock->lock, 1);
		if (oldValue != 0) {
			panic("acquire_spinlock: attempt to acquire lock %p twice on "
				"non-SMP system (last caller: %p, value %" B_PRIx32 ")", lock,
				find_lock_caller(lock), oldValue);
		}

		push_lock_caller(arch_debug_get_caller(), lock);
#endif
	}
}


/**
 * @brief Release a previously acquired spinlock.
 *
 * @param lock The spinlock to release.
 * @note Interrupts must be disabled before calling this function.
 *       On debug builds, panics if the lock was not held or if interrupts
 *       are enabled at call time.
 */
void
release_spinlock(spinlock *lock)
{
#if B_DEBUG_SPINLOCK_CONTENTION
	update_lock_held(lock);
#endif

	if (sNumCPUs > 1) {
		if (are_interrupts_enabled()) {
			panic("release_spinlock: attempt to release lock %p with "
				"interrupts enabled\n", lock);
		}

#if DEBUG_SPINLOCKS
		if (atomic_get_and_set(&lock->lock, 0) != 1)
			panic("release_spinlock: lock %p was already released\n", lock);
#else
		atomic_set(&lock->lock, 0);
#endif
	} else {
#if DEBUG_SPINLOCKS
		if (are_interrupts_enabled()) {
			panic("release_spinlock: attempt to release lock %p with "
				"interrupts enabled\n", lock);
		}
		if (atomic_get_and_set(&lock->lock, 0) != 1)
			panic("release_spinlock: lock %p was already released\n", lock);
#endif
	}
}


/**
 * @brief Attempt a non-blocking write-side acquisition of a reader-writer spinlock.
 *
 * @param lock The rw_spinlock to attempt to acquire for writing.
 * @retval true  Write lock successfully acquired (no readers or writers present).
 * @retval false Lock was already held; caller must retry.
 * @note Interrupts must be disabled. On single-CPU debug builds, panics if the
 *       lock is already held.
 */
bool
try_acquire_write_spinlock(rw_spinlock* lock)
{
#if DEBUG_SPINLOCKS
	if (are_interrupts_enabled()) {
		panic("try_acquire_write_spinlock: attempt to acquire lock %p with "
			"interrupts enabled", lock);
	}

	if (sNumCPUs < 2 && lock->lock != 0) {
		panic("try_acquire_write_spinlock(): attempt to acquire lock %p twice "
			"on non-SMP system", lock);
	}
#endif

	return atomic_test_and_set(&lock->lock, 1u << 31, 0) == 0;
}


/**
 * @brief Acquire the write side of a reader-writer spinlock, spinning until available.
 *
 * @param lock The rw_spinlock to acquire for exclusive writing.
 * @note Interrupts must be disabled. Processes pending ICIs while spinning
 *       to avoid deadlock. Panics after SPINLOCK_DEADLOCK_COUNT iterations.
 */
void
acquire_write_spinlock(rw_spinlock* lock)
{
	uint32 count = 0;
	int32 currentCPU = smp_get_current_cpu();
	while (true) {
		if (try_acquire_write_spinlock(lock))
			break;

		while (lock->lock != 0) {
			if (++count == SPINLOCK_DEADLOCK_COUNT) {
				panic("acquire_write_spinlock(): Failed to acquire spinlock %p "
					"for a long time!", lock);
				count = 0;
			}

			process_all_pending_ici(currentCPU);
			cpu_wait(&lock->lock, 0);
		}
	}
}


/**
 * @brief Acquire the write side of a reader-writer spinlock without processing ICIs.
 *
 * @param lock The rw_spinlock to acquire for exclusive writing.
 * @note Interrupts must be disabled. Does NOT drain the ICI mailbox while
 *       spinning — use only in contexts where ICI processing would be unsafe
 *       or circular. Panics after SPINLOCK_DEADLOCK_COUNT_NO_CHECK iterations.
 */
static void
acquire_write_spinlock_nocheck(rw_spinlock* lock)
{
	uint32 count = 0;
	while (true) {
		if (try_acquire_write_spinlock(lock))
			break;

		while (lock->lock != 0) {
			if (++count == SPINLOCK_DEADLOCK_COUNT_NO_CHECK) {
				panic("acquire_write_spinlock(): Failed to acquire spinlock %p "
					"for a long time!", lock);
				count = 0;
			}

			cpu_wait(&lock->lock, 0);
		}
	}
}


/*! Equivalent to acquire_write_spinlock(), save for currentCPU parameter
 * (in order to avoid accessing the current thread structure.) */
/**
 * @brief Acquire the write side of a reader-writer spinlock using an explicit CPU index.
 *
 * @param currentCPU Index of the calling CPU (avoids accessing the thread structure).
 * @param lock       The rw_spinlock to acquire for exclusive writing.
 * @note Interrupts must be disabled. Processes pending ICIs while spinning.
 *       Equivalent to acquire_write_spinlock() but safe to call before the
 *       per-CPU thread pointer is fully initialised.
 */
static void
acquire_write_spinlock_cpu(int32 currentCPU, rw_spinlock* lock)
{
	uint32 count = 0;
	while (true) {
		if (try_acquire_write_spinlock(lock))
			break;

		while (lock->lock != 0) {
			if (++count == SPINLOCK_DEADLOCK_COUNT) {
				panic("acquire_write_spinlock(): Failed to acquire spinlock %p "
					"for a long time!", lock);
				count = 0;
			}

			process_all_pending_ici(currentCPU);
			cpu_wait(&lock->lock, 0);
		}
	}
}


/**
 * @brief Release the write side of a reader-writer spinlock.
 *
 * @param lock The rw_spinlock to release from exclusive write ownership.
 * @note On debug builds, panics if the write-owner bit was not set, indicating
 *       a double-release or mismatched acquire/release.
 */
void
release_write_spinlock(rw_spinlock* lock)
{
#if DEBUG_SPINLOCKS
	uint32 previous = atomic_get_and_set(&lock->lock, 0);
	if ((previous & 1u << 31) == 0) {
		panic("release_write_spinlock: lock %p was already released (value: "
			"%#" B_PRIx32 ")\n", lock, previous);
	}
#else
	atomic_set(&lock->lock, 0);
#endif
}


/**
 * @brief Attempt a non-blocking read-side acquisition of a reader-writer spinlock.
 *
 * @param lock The rw_spinlock to attempt to acquire for reading.
 * @retval true  Read lock successfully acquired (no writer present).
 * @retval false A writer holds or is waiting on the lock; caller must retry.
 * @note Interrupts must be disabled. Increments the reader count atomically;
 *       if a writer bit is observed the caller must undo the increment and retry.
 */
bool
try_acquire_read_spinlock(rw_spinlock* lock)
{
#if DEBUG_SPINLOCKS
	if (are_interrupts_enabled()) {
		panic("try_acquire_read_spinlock: attempt to acquire lock %p with "
			"interrupts enabled", lock);
	}

	if (sNumCPUs < 2 && lock->lock != 0) {
		panic("try_acquire_read_spinlock(): attempt to acquire lock %p twice "
			"on non-SMP system", lock);
	}
#endif

	uint32 previous = atomic_add(&lock->lock, 1);
	return (previous & (1u << 31)) == 0;
}


/**
 * @brief Acquire the read side of a reader-writer spinlock, spinning until no writer holds it.
 *
 * @param lock The rw_spinlock to acquire for shared reading.
 * @note Interrupts must be disabled. Processes pending ICIs while spinning.
 *       Panics after SPINLOCK_DEADLOCK_COUNT iterations.
 */
void
acquire_read_spinlock(rw_spinlock* lock)
{
	uint32 count = 0;
	int currentCPU = smp_get_current_cpu();
	while (1) {
		if (try_acquire_read_spinlock(lock))
			break;

		while ((lock->lock & (1u << 31)) != 0) {
			if (++count == SPINLOCK_DEADLOCK_COUNT) {
				panic("acquire_read_spinlock(): Failed to acquire spinlock %p "
					"for a long time!", lock);
				count = 0;
			}

			process_all_pending_ici(currentCPU);
			cpu_wait(&lock->lock, 0);
		}
	}
}


/**
 * @brief Acquire the read side of a reader-writer spinlock without processing ICIs.
 *
 * @param lock The rw_spinlock to acquire for shared reading.
 * @note Interrupts must be disabled. Does NOT drain the ICI mailbox while
 *       spinning — use only in contexts where ICI processing would be unsafe.
 *       Panics after SPINLOCK_DEADLOCK_COUNT_NO_CHECK iterations.
 */
static void
acquire_read_spinlock_nocheck(rw_spinlock* lock)
{
	uint32 count = 0;
	while (1) {
		if (try_acquire_read_spinlock(lock))
			break;

		while ((lock->lock & (1u << 31)) != 0) {
			if (++count == SPINLOCK_DEADLOCK_COUNT_NO_CHECK) {
				panic("acquire_read_spinlock(): Failed to acquire spinlock %p "
					"for a long time!", lock);
				count = 0;
			}

			cpu_wait(&lock->lock, 0);
		}
	}
}


/**
 * @brief Release the read side of a reader-writer spinlock.
 *
 * @param lock The rw_spinlock to release from shared read ownership.
 * @note On debug builds, panics if the write-owner bit is set at release time,
 *       indicating a mismatched acquire/release pair.
 */
void
release_read_spinlock(rw_spinlock* lock)
{
#if DEBUG_SPINLOCKS
	uint32 previous = atomic_add(&lock->lock, -1);
	if ((previous & 1u << 31) != 0) {
		panic("release_read_spinlock: lock %p was already released (value:"
			" %#" B_PRIx32 ")\n", lock, previous);
	}
#else
	atomic_add(&lock->lock, -1);
#endif

}


/**
 * @brief Attempt to acquire a seqlock for writing without blocking.
 *
 * @param lock The seqlock to attempt to acquire for writing.
 * @retval true  Write lock acquired; sequence counter has been incremented.
 * @retval false The underlying spinlock was already held.
 * @note Interrupts must be disabled. Increments the sequence counter on success
 *       so that concurrent readers can detect the in-progress update.
 */
bool
try_acquire_write_seqlock(seqlock* lock)
{
	bool succeed = try_acquire_spinlock(&lock->lock);
	if (succeed)
		atomic_add((int32*)&lock->count, 1);
	return succeed;
}


/**
 * @brief Acquire a seqlock for writing, spinning until the spinlock is available.
 *
 * @param lock The seqlock to acquire for writing.
 * @note Interrupts must be disabled. Increments the sequence counter so that
 *       readers can detect an ongoing write and retry their read section.
 */
void
acquire_write_seqlock(seqlock* lock)
{
	acquire_spinlock(&lock->lock);
	atomic_add((int32*)&lock->count, 1);
}


/**
 * @brief Release a seqlock previously acquired for writing.
 *
 * @param lock The seqlock to release from write ownership.
 * @note Increments the sequence counter a second time (making it even) so that
 *       readers know the write is complete, then releases the underlying spinlock.
 */
void
release_write_seqlock(seqlock* lock)
{
	atomic_add((int32*)&lock->count, 1);
	release_spinlock(&lock->lock);
}


/**
 * @brief Sample the sequence counter to begin a read-side critical section.
 *
 * @param lock The seqlock to read from.
 * @return The current sequence counter value; must be passed to release_read_seqlock().
 * @note Does not acquire any lock. Callers must re-read and call
 *       release_read_seqlock() to verify that no concurrent write occurred.
 */
uint32
acquire_read_seqlock(seqlock* lock)
{
	return atomic_get((int32*)&lock->count);
}


/**
 * @brief Validate a seqlock read section and determine whether it must be retried.
 *
 * @param lock  The seqlock that was read.
 * @param count The sequence value returned by the matching acquire_read_seqlock() call.
 * @retval true  The read section is valid; no concurrent write occurred.
 * @retval false A concurrent write was detected; the caller must retry.
 * @note Issues a memory read barrier before comparing counters. If the saved
 *       count was odd (write in progress at sample time) or has since changed,
 *       returns false and calls cpu_pause() as a back-off hint.
 */
bool
release_read_seqlock(seqlock* lock, uint32 count)
{
	memory_read_barrier();

	uint32 current = *(volatile int32*)&lock->count;

	if (count % 2 == 1 || current != count) {
		cpu_pause();
		return false;
	}

	return true;
}


/*!	Finds a free message and gets it.
	NOTE: has side effect of disabling interrupts
	return value is the former interrupt state
*/
/**
 * @brief Dequeue a free smp_msg from the pool, disabling interrupts as a side-effect.
 *
 * @param msg Output pointer set to the allocated message on success.
 * @return The previous interrupt state (cpu_status) that must be restored by
 *         the caller after the message has been dispatched or returned.
 * @note Blocks (with interrupts enabled) until a free message is available,
 *       then disables interrupts and takes the write spinlock to claim it.
 *       The caller is responsible for calling restore_interrupts() with the
 *       returned state once the ICI transaction is complete.
 */
static cpu_status
find_free_message(struct smp_msg** msg)
{
	TRACE("find_free_message: entry\n");

	cpu_status state;

retry:
	while (sFreeMessageCount <= 0) {
		ASSERT(are_interrupts_enabled());
		cpu_pause();
	}

	state = disable_interrupts();
	acquire_write_spinlock(&sFreeMessageSpinlock);

	if (sFreeMessageCount <= 0) {
		// someone grabbed one while we were getting the lock,
		// go back to waiting for it
		release_write_spinlock(&sFreeMessageSpinlock);
		restore_interrupts(state);
		goto retry;
	}

	*msg = sFreeMessages;
	sFreeMessages = (*msg)->next;
	sFreeMessageCount--;

	release_write_spinlock(&sFreeMessageSpinlock);

	TRACE("find_free_message: returning msg %p\n", *msg);

	return state;
}


/*!	Similar to find_free_message(), but expects the interrupts to be disabled
	already.
*/
/**
 * @brief Dequeue a free smp_msg from the pool when interrupts are already disabled.
 *
 * @param currentCPU  Index of the calling CPU (used to process ICIs while waiting).
 * @param _message    Output pointer set to the allocated message.
 * @note Interrupts must already be disabled by the caller. Processes pending
 *       ICIs while spinning to avoid deadlock when all messages are in flight.
 */
static void
find_free_message_interrupts_disabled(int32 currentCPU,
	struct smp_msg** _message)
{
	TRACE("find_free_message_interrupts_disabled: entry\n");

	acquire_write_spinlock_cpu(currentCPU, &sFreeMessageSpinlock);
	while (sFreeMessageCount <= 0) {
		release_write_spinlock(&sFreeMessageSpinlock);
		process_all_pending_ici(currentCPU);
		cpu_pause();
		acquire_write_spinlock_cpu(currentCPU, &sFreeMessageSpinlock);
	}

	*_message = sFreeMessages;
	sFreeMessages = (*_message)->next;
	sFreeMessageCount--;

	release_write_spinlock(&sFreeMessageSpinlock);

	TRACE("find_free_message_interrupts_disabled: returning msg %p\n",
		*_message);
}


/**
 * @brief Return a processed smp_msg to the free-message pool.
 *
 * @param msg The message to return to the pool.
 * @note Uses acquire_write_spinlock_nocheck() so that it is safe to call
 *       from within ICI processing context where ICI re-entrancy must be avoided.
 */
static void
return_free_message(struct smp_msg* msg)
{
	TRACE("return_free_message: returning msg %p\n", msg);

	acquire_write_spinlock_nocheck(&sFreeMessageSpinlock);
	msg->next = sFreeMessages;
	sFreeMessages = msg;
	sFreeMessageCount++;
	release_write_spinlock(&sFreeMessageSpinlock);
}


/**
 * @brief Atomically prepend an smp_msg to a lock-free singly-linked list head.
 *
 * @param listHead Reference to the list head pointer (modified atomically).
 * @param msg      The message to prepend.
 * @note Uses a compare-and-swap retry loop; safe for concurrent producers.
 *       The consumer side (check_for_message) must also use atomic operations.
 */
static void
prepend_message(struct smp_msg*& listHead, struct smp_msg* msg)
{
	while (true) {
		struct smp_msg* next = atomic_pointer_get(&listHead);
		msg->next = next;
		if (atomic_pointer_test_and_set(&listHead, msg, next) == next)
			break;
		cpu_pause();
	}
}


/**
 * @brief Dequeue the next pending ICI message from the local or broadcast mailbox.
 *
 * @param currentCPU     Index of the calling CPU.
 * @param sourceMailbox  Output parameter set to MAILBOX_LOCAL or MAILBOX_BCAST
 *                       to indicate where the message was found.
 * @return Pointer to the dequeued smp_msg, or NULL if both mailboxes are empty.
 * @note Must be called with interrupts disabled. Checks the per-CPU mailbox
 *       first; falls back to the broadcast mailbox using the ICI counter to
 *       avoid unnecessary spinlock traffic.
 */
static struct smp_msg*
check_for_message(int currentCPU, mailbox_source& sourceMailbox)
{
	if (!sICIEnabled)
		return NULL;

	struct smp_msg* msg = atomic_pointer_get(&sCPUMessages[currentCPU]);
	if (msg != NULL) {
		// since only this CPU ever dequeues, we can just use atomics
		while (true) {
			if (atomic_pointer_test_and_set(&sCPUMessages[currentCPU], msg->next, msg) == msg)
				break;

			cpu_pause();
			msg = atomic_pointer_get(&sCPUMessages[currentCPU]);
			ASSERT(msg != NULL);
		}

		TRACE(" cpu %d: found msg %p in cpu mailbox\n", currentCPU, msg);
		sourceMailbox = MAILBOX_LOCAL;
	} else if (atomic_get(&get_cpu_struct()->ici_counter)
		!= atomic_get(&sBroadcastMessageCounter)) {

		// try getting one from the broadcast mailbox
		acquire_read_spinlock_nocheck(&sBroadcastMessageSpinlock);

		msg = sBroadcastMessages;
		while (msg != NULL) {
			if (!msg->proc_bitmap.GetBitAtomic(currentCPU)) {
				// we have handled this one already
				msg = msg->next;
				continue;
			}

			// mark it so we wont try to process this one again
			msg->proc_bitmap.ClearBitAtomic(currentCPU);
			atomic_add(&gCPU[currentCPU].ici_counter, 1);

			sourceMailbox = MAILBOX_BCAST;
			break;
		}
		release_read_spinlock(&sBroadcastMessageSpinlock);

		if (msg != NULL) {
			TRACE(" cpu %d: found msg %p in broadcast mailbox\n", currentCPU,
				msg);
		}
	}

	return msg;
}


/**
 * @brief Decrement the reference count of a processed ICI message and clean it up when zero.
 *
 * @param currentCPU    Index of the CPU that just finished processing the message.
 * @param msg           The message whose reference count is to be decremented.
 * @param sourceMailbox Whether the message came from the per-CPU or broadcast mailbox.
 * @note Must be called after the message action has been executed. The last CPU
 *       to decrement the count removes the message from the broadcast list (if
 *       applicable), frees any SMP_MSG_FLAG_FREE_ARG data pointer, and either
 *       signals the sender (SYNC) or returns the message to the free pool (ASYNC).
 */
static void
finish_message_processing(int currentCPU, struct smp_msg* msg,
	mailbox_source sourceMailbox)
{
	if (atomic_add(&msg->ref_count, -1) != 1)
		return;

	// we were the last one to decrement the ref_count
	// it's our job to remove it from the list & possibly clean it up

	// clean up the message
	if (sourceMailbox == MAILBOX_BCAST)
		acquire_write_spinlock_nocheck(&sBroadcastMessageSpinlock);

	TRACE("cleaning up message %p\n", msg);

	if (sourceMailbox != MAILBOX_BCAST) {
		// local mailbox -- the message has already been removed in
		// check_for_message()
	} else if (msg == sBroadcastMessages) {
		sBroadcastMessages = msg->next;
	} else {
		// we need to walk to find the message in the list.
		// we can't use any data found when previously walking through
		// the list, since the list may have changed. But, we are guaranteed
		// to at least have msg in it.
		struct smp_msg* last = NULL;
		struct smp_msg* msg1;

		msg1 = sBroadcastMessages;
		while (msg1 != NULL && msg1 != msg) {
			last = msg1;
			msg1 = msg1->next;
		}

		// by definition, last must be something
		if (msg1 == msg && last != NULL)
			last->next = msg->next;
		else
			panic("last == NULL or msg != msg1");
	}

	if (sourceMailbox == MAILBOX_BCAST)
		release_write_spinlock(&sBroadcastMessageSpinlock);

	if ((msg->flags & SMP_MSG_FLAG_FREE_ARG) != 0 && msg->data_ptr != NULL)
		free(msg->data_ptr);

	if ((msg->flags & SMP_MSG_FLAG_SYNC) != 0) {
		atomic_set(&msg->done, 1);
		// the caller cpu should now free the message
	} else {
		// in the !SYNC case, we get to free the message
		return_free_message(msg);
	}
}


/**
 * @brief Dispatch a single ICI message to the appropriate handler for the calling CPU.
 *
 * @param msg        The ICI message to dispatch.
 * @param currentCPU Index of the CPU processing the message.
 * @param haltCPU    Output flag; set to true if the message is SMP_MSG_CPU_HALT,
 *                   instructing the caller to enter the kernel debugger loop.
 * @note Must be called with interrupts disabled. Unknown message types produce
 *       a dprintf warning but do not panic.
 */
static void
invoke_smp_msg(struct smp_msg* msg, int currentCPU, bool* haltCPU)
{
	switch (msg->message) {
		case SMP_MSG_INVALIDATE_PAGE_RANGE:
			arch_cpu_invalidate_tlb_range(msg->data, msg->data2, msg->data3);
			break;
		case SMP_MSG_INVALIDATE_PAGE_LIST:
			arch_cpu_invalidate_tlb_list(msg->data, (addr_t*)msg->data2, (int)msg->data3);
			break;
		case SMP_MSG_USER_INVALIDATE_PAGES:
			arch_cpu_user_tlb_invalidate(msg->data);
			break;
		case SMP_MSG_GLOBAL_INVALIDATE_PAGES:
			arch_cpu_global_tlb_invalidate();
			break;
		case SMP_MSG_CPU_HALT:
			*haltCPU = true;
			break;
		case SMP_MSG_CALL_FUNCTION:
		{
			smp_call_func func = (smp_call_func)msg->data_ptr;
			func(msg->data, currentCPU, msg->data2, msg->data3);
			break;
		}
		case SMP_MSG_RESCHEDULE:
			scheduler_reschedule_ici();
			break;

		default:
			dprintf("smp_intercpu_interrupt_handler: got unknown message %" B_PRId32 "\n",
				msg->message);
			break;
	}
}


/**
 * @brief Dequeue and process one pending ICI message for the calling CPU.
 *
 * @param currentCPU Index of the CPU processing messages.
 * @retval B_OK            A message was found, dispatched, and cleaned up.
 * @retval B_ENTRY_NOT_FOUND Both the per-CPU and broadcast mailboxes were empty.
 * @note Must be called with interrupts disabled. After dispatching, calls
 *       finish_message_processing() to handle reference counting and cleanup.
 *       If the message is SMP_MSG_CPU_HALT the CPU enters the kernel debugger.
 */
static status_t
process_pending_ici(int32 currentCPU)
{
	mailbox_source sourceMailbox;
	struct smp_msg* msg = check_for_message(currentCPU, sourceMailbox);
	if (msg == NULL)
		return B_ENTRY_NOT_FOUND;

	TRACE("  cpu %" B_PRId32 " message = %" B_PRId32 "\n", currentCPU, msg->message);

	bool haltCPU = false;
	invoke_smp_msg(msg, currentCPU, &haltCPU);

	// finish dealing with this message, possibly removing it from the list
	finish_message_processing(currentCPU, msg, sourceMailbox);

	// special case for the halt message
	if (haltCPU)
		debug_trap_cpu_in_kdl(currentCPU, false);

	return B_OK;
}


#if B_DEBUG_SPINLOCK_CONTENTION


/**
 * @brief Generic syscall handler to retrieve spinlock contention statistics.
 *
 * @param subsystem   Subsystem name string (unused; must match SPINLOCK_CONTENTION).
 * @param function    Function code; only GET_SPINLOCK_CONTENTION_INFO is supported.
 * @param buffer      User-space buffer to receive a spinlock_contention_info struct.
 * @param bufferSize  Size in bytes of the user-space buffer.
 * @retval B_OK          Info copied to user buffer successfully.
 * @retval B_BAD_VALUE   Unknown function code or buffer too small.
 * @retval B_BAD_ADDRESS Buffer pointer is not a valid user address.
 * @note Only compiled in when B_DEBUG_SPINLOCK_CONTENTION is enabled.
 */
static status_t
spinlock_contention_syscall(const char* subsystem, uint32 function,
	void* buffer, size_t bufferSize)
{
	if (function != GET_SPINLOCK_CONTENTION_INFO)
		return B_BAD_VALUE;

	if (bufferSize < sizeof(spinlock_contention_info))
		return B_BAD_VALUE;

	// TODO: This isn't very useful at the moment...

	spinlock_contention_info info;
	info.thread_creation_spinlock = gThreadCreationLock.total_wait;

	if (!IS_USER_ADDRESS(buffer)
		|| user_memcpy(buffer, &info, sizeof(info)) != B_OK) {
		return B_BAD_ADDRESS;
	}

	return B_OK;
}


#endif	// B_DEBUG_SPINLOCK_CONTENTION


/**
 * @brief Execute the pending early-boot CPU call on a single AP and mark it complete.
 *
 * @param cpu Index of the application processor executing the call.
 * @note Called from the AP spin loop in smp_trap_non_boot_cpus() before ICI is
 *       enabled. Clears the CPU's bit in sEarlyCPUCallSet and increments the
 *       completion counter so the BSP can detect when all APs have finished.
 */
static void
process_early_cpu_call(int32 cpu)
{
	sEarlyCPUCallFunction(sEarlyCPUCallCookie, cpu);
	sEarlyCPUCallSet.ClearBitAtomic(cpu);
	atomic_add(&sEarlyCPUCallCount, 1);
}


/**
 * @brief Broadcast a function call to all CPUs during early boot before ICI is available.
 *
 * @param function Callback to invoke on every CPU; signature is void(void*, int).
 * @param cookie   Opaque pointer forwarded to each callback invocation.
 * @note Interrupts must be disabled. Sets up the global early-call state and
 *       spins until all APs have invoked the function via process_early_cpu_call(),
 *       then invokes the function on the BSP (CPU 0) itself.
 */
static void
call_all_cpus_early(void (*function)(void*, int), void* cookie)
{
	ASSERT(!are_interrupts_enabled());

	if (sNumCPUs > 1) {
		sEarlyCPUCallFunction = function;
		sEarlyCPUCallCookie = cookie;

		atomic_set(&sEarlyCPUCallCount, 1);
		sEarlyCPUCallSet.SetAll();
		sEarlyCPUCallSet.ClearBit(0);

		// wait for all CPUs to finish
		while (sEarlyCPUCallCount < sNumCPUs)
			cpu_wait(&sEarlyCPUCallCount, sNumCPUs);
	}

	function(cookie, 0);
}


//	#pragma mark -


/**
 * @brief Interrupt handler for incoming inter-CPU interrupts on the local CPU.
 *
 * @param cpu Index of the CPU that received the IPI hardware interrupt.
 * @retval B_HANDLED_INTERRUPT Always; signals the interrupt was consumed.
 * @note Called at interrupt level with interrupts disabled. Drains all pending
 *       ICI messages from both the per-CPU and broadcast mailboxes before returning.
 */
int
smp_intercpu_interrupt_handler(int32 cpu)
{
	TRACE("smp_intercpu_interrupt_handler: entry on cpu %" B_PRId32 "\n", cpu);

	process_all_pending_ici(cpu);

	TRACE("smp_intercpu_interrupt_handler: done on cpu %" B_PRId32 "\n", cpu);

	return B_HANDLED_INTERRUPT;
}


/**
 * @brief Send an inter-CPU interrupt carrying a typed message to a single target CPU.
 *
 * @param targetCPU   Index of the destination CPU (must differ from the caller's CPU).
 * @param message     ICI message type (e.g. SMP_MSG_INVALIDATE_PAGE_RANGE).
 * @param data        First inline data word passed with the message.
 * @param data2       Second inline data word passed with the message.
 * @param data3       Third inline data word passed with the message.
 * @param dataPointer Optional heap pointer forwarded with the message.
 * @param flags       SMP_MSG_FLAG_SYNC to block until the target has processed the
 *                    message, or SMP_MSG_FLAG_ASYNC for fire-and-forget.
 * @note May be called with interrupts enabled; disables them internally while
 *       queuing the message and restores them afterwards. Panics if targetCPU
 *       equals the current CPU. When SMP_MSG_FLAG_SYNC is set the caller spins
 *       (processing its own pending ICIs) until the target sets msg->done.
 */
void
smp_send_ici(int32 targetCPU, int32 message, addr_t data, addr_t data2,
	addr_t data3, void* dataPointer, uint32 flags)
{
	if (!sICIEnabled)
		return;

	TRACE("smp_send_ici: target 0x%" B_PRIx32 ", mess 0x%" B_PRIx32 ", data 0x%lx, data2 0x%lx, "
		"data3 0x%lx, ptr %p, flags 0x%" B_PRIx32 "\n", targetCPU, message, data, data2,
		data3, dataPointer, flags);

	// find_free_message leaves interrupts disabled
	struct smp_msg *msg;
	cpu_status state = find_free_message(&msg);

	int currentCPU = smp_get_current_cpu();
	if (targetCPU == currentCPU) {
		// nope, can't do that
		ASSERT(false);
		return_free_message(msg);
		restore_interrupts(state);
		return;
	}

	// set up the message
	msg->message = message;
	msg->data = data;
	msg->data2 = data2;
	msg->data3 = data3;
	msg->data_ptr = dataPointer;
	msg->ref_count = 1;
	msg->flags = flags;
	msg->done = 0;

	// stick it in the appropriate cpu's mailbox
	prepend_message(sCPUMessages[targetCPU], msg);

	arch_smp_send_ici(targetCPU);

	if ((flags & SMP_MSG_FLAG_SYNC) != 0) {
		// wait for the other cpu to finish processing it
		// the interrupt handler will ref count it to <0
		// if the message is sync after it has removed it from the mailbox
		while (msg->done == 0) {
			process_all_pending_ici(currentCPU);
			cpu_wait(&msg->done, 1);
		}
		// for SYNC messages, it's our responsibility to put it
		// back into the free list
		return_free_message(msg);
	}

	restore_interrupts(state);
}


/**
 * @brief Broadcast an ICI message to all CPUs, including the sender.
 *
 * @param message     ICI message type.
 * @param data        First inline data word.
 * @param data2       Second inline data word.
 * @param data3       Third inline data word.
 * @param dataPointer Optional heap pointer forwarded with the message.
 * @param flags       SMP_MSG_FLAG_SYNC to wait for all recipients to finish,
 *                    or SMP_MSG_FLAG_ASYNC for fire-and-forget.
 * @note When ICI is not yet enabled (early boot), invokes the message handler
 *       directly on CPU 0 in lieu of a true broadcast. On SMP systems the
 *       message is inserted into the broadcast mailbox and all CPUs are
 *       interrupted via arch_smp_send_broadcast_ici(). With SYNC the caller
 *       spins until all recipients have processed the message.
 */
void
smp_broadcast_ici(int32 message, addr_t data, addr_t data2, addr_t data3,
	void *dataPointer, uint32 flags)
{
	if (!sICIEnabled) {
		smp_msg dummy;
		dummy.message = message;
		dummy.data = data;
		dummy.data2 = data2;
		dummy.data3 = data3;
		dummy.data_ptr = dataPointer;

		cpu_status state = disable_interrupts();
		invoke_smp_msg(&dummy, 0, NULL);
		restore_interrupts(state);
		return;
	}

	TRACE("smp_broadcast_ici: cpu %" B_PRId32 " mess 0x%" B_PRIx32 ", data 0x%lx, data2 "
		"0x%lx, data3 0x%lx, ptr %p, flags 0x%" B_PRIx32 "\n", smp_get_current_cpu(),
		message, data, data2, data3, dataPointer, flags);

	// find_free_message leaves interrupts disabled
	struct smp_msg *msg;
	cpu_status state = find_free_message(&msg);

	int32 currentCPU = smp_get_current_cpu();

	msg->message = message;
	msg->data = data;
	msg->data2 = data2;
	msg->data3 = data3;
	msg->data_ptr = dataPointer;
	msg->ref_count = sNumCPUs;
	msg->flags = flags;
	msg->proc_bitmap.SetAll();
	msg->done = 0;

	TRACE("smp_broadcast_ici%d: inserting msg %p into broadcast "
		"mbox\n", currentCPU, msg);

	// stick it in the appropriate cpu's mailbox
	acquire_read_spinlock_nocheck(&sBroadcastMessageSpinlock);
	prepend_message(sBroadcastMessages, msg);
	release_read_spinlock(&sBroadcastMessageSpinlock);

	atomic_add(&sBroadcastMessageCounter, 1);

	arch_smp_send_broadcast_ici();

	TRACE("smp_broadcast_ici: sent interrupt\n");

	if ((flags & SMP_MSG_FLAG_SYNC) != 0) {
		// wait for the other cpus to finish processing it
		// the interrupt handler will ref count it to <0
		// if the message is sync after it has removed it from the mailbox
		TRACE("smp_broadcast_ici: waiting for ack\n");

		while (msg->done == 0) {
			process_all_pending_ici(currentCPU);
			cpu_wait(&msg->done, 1);
		}

		TRACE("smp_broadcast_ici: returning message to free list\n");

		// for SYNC messages, it's our responsibility to put it
		// back into the free list
		return_free_message(msg);
	} else {
		// make sure this CPU has processed the message at least
		process_all_pending_ici(currentCPU);
	}

	restore_interrupts(state);
}


/**
 * @brief Send an ICI to an arbitrary subset of CPUs specified by a bitmask.
 *
 * @param cpuMask     Bitmask of destination CPUs; the calling CPU may be included.
 * @param message     ICI message type.
 * @param data        First inline data word.
 * @param data2       Second inline data word.
 * @param data3       Third inline data word.
 * @param dataPointer Optional heap pointer forwarded with the message.
 * @param flags       SMP_MSG_FLAG_SYNC to wait for all recipients, or ASYNC.
 * @note Requires ICI to be enabled (panics otherwise via ASSERT). The calling
 *       thread must be pinned to its CPU. Chooses the cheapest delivery path:
 *       unicast ICI for a single remote target, or broadcast mailbox for larger
 *       sets. If the calling CPU is in the mask its portion is processed inline.
 */
void
smp_multicast_ici(const CPUSet& cpuMask, int32 message, addr_t data,
	addr_t data2, addr_t data3, void *dataPointer, uint32 flags)
{
	if (!sICIEnabled) {
		// how did you decide what CPUs to interrupt?
		ASSERT(false);
		return;
	}

	ASSERT(thread_get_current_thread()->pinned_to_cpu);
	int32 currentCPU = smp_get_current_cpu();
	bool self = cpuMask.GetBit(currentCPU);

	int32 targetCPUs = 0, firstNonCurrentCPU = -1;
	for (int32 i = 0; i < sNumCPUs; i++) {
		if (cpuMask.GetBit(i)) {
			targetCPUs++;
			if (firstNonCurrentCPU < 0 && i != currentCPU)
				firstNonCurrentCPU = i;
		}
	}

	if (targetCPUs == 0) {
		panic("smp_multicast_ici(): 0 CPU mask");
		return;
	}

	// find_free_message leaves interrupts disabled
	struct smp_msg *msg;
	cpu_status state = find_free_message(&msg);

	msg->message = message;
	msg->data = data;
	msg->data2 = data2;
	msg->data3 = data3;
	msg->data_ptr = dataPointer;
	msg->ref_count = targetCPUs;
	msg->flags = flags;
	msg->done = 0;
	msg->proc_bitmap = cpuMask;

	if ((!self && targetCPUs == 1) || (self && targetCPUs == 2)) {
		// stick it in the appropriate cpu's mailbox
		prepend_message(sCPUMessages[firstNonCurrentCPU], msg);

		arch_smp_send_ici(firstNonCurrentCPU);

		if (self) {
			// invoke for ourselves
			invoke_smp_msg(msg, currentCPU, NULL);
			finish_message_processing(currentCPU, msg, MAILBOX_LOCAL);
		}
	} else {
		bool broadcast = (!self && targetCPUs == sNumCPUs - 1)
			|| (self && targetCPUs == sNumCPUs);

		// stick it in the broadcast mailbox
		acquire_read_spinlock_nocheck(&sBroadcastMessageSpinlock);
		prepend_message(sBroadcastMessages, msg);
		release_read_spinlock(&sBroadcastMessageSpinlock);

		atomic_add(&sBroadcastMessageCounter, 1);
		for (int32 i = 0; i < sNumCPUs; i++) {
			if (!cpuMask.GetBit(i))
				atomic_add(&gCPU[i].ici_counter, 1);
		}

		if (broadcast) {
			arch_smp_send_broadcast_ici();
		} else {
			CPUSet sendMask = cpuMask;
			sendMask.ClearBit(currentCPU);
			arch_smp_send_multicast_ici(sendMask);
		}
	}

	if ((flags & SMP_MSG_FLAG_SYNC) != 0) {
		// wait for the other cpus to finish processing it
		// the interrupt handler will ref count it to <0
		// if the message is sync after it has removed it from the mailbox
		while (msg->done == 0) {
			process_all_pending_ici(currentCPU);
			cpu_wait(&msg->done, 1);
		}

		// for SYNC messages, it's our responsibility to put it
		// back into the free list
		return_free_message(msg);
	} else if (self) {
		// if broadcast, make sure this CPU has processed the message at least
		process_all_pending_ici(currentCPU);
	}

	restore_interrupts(state);
}


/**
 * @brief Send an ICI to an arbitrary CPU subset when interrupts are already disabled.
 *
 * @param currentCPU  Index of the calling CPU.
 * @param cpuMask     Bitmask of destination CPUs.
 * @param message     ICI message type.
 * @param data        First inline data word.
 * @param data2       Second inline data word.
 * @param data3       Third inline data word.
 * @param dataPointer Optional heap pointer forwarded with the message.
 * @param flags       SMP_MSG_FLAG_SYNC to wait for all recipients, or ASYNC.
 * @note Interrupts must already be disabled by the caller. Uses
 *       find_free_message_interrupts_disabled() to obtain a message slot.
 *       Always uses the broadcast mailbox regardless of target count.
 */
void
smp_multicast_ici_interrupts_disabled(int32 currentCPU, const CPUSet& cpuMask,
	int32 message, addr_t data, addr_t data2, addr_t data3, void *dataPointer, uint32 flags)
{
	if (!sICIEnabled)
		return;

	TRACE("smp_multicast_ici_interrupts_disabled: cpu %" B_PRId32 " mess 0x%" B_PRIx32 ", "
		"data 0x%lx, data2 0x%lx, data3 0x%lx, ptr %p, flags 0x%" B_PRIx32 "\n",
		currentCPU, message, data, data2, data3, dataPointer, flags);

	int32 targetCPUs = 0;
	for (int32 i = 0; i < sNumCPUs; i++) {
		if (cpuMask.GetBit(i))
			targetCPUs++;
	}

	if (targetCPUs == 0) {
		panic("smp_multicast_ici_interrupts_disabled(): 0 CPU mask");
		return;
	}

	struct smp_msg *msg;
	find_free_message_interrupts_disabled(currentCPU, &msg);

	msg->message = message;
	msg->data = data;
	msg->data2 = data2;
	msg->data3 = data3;
	msg->data_ptr = dataPointer;
	msg->ref_count = targetCPUs;
	msg->flags = flags;
	msg->done = 0;
	msg->proc_bitmap = cpuMask;

	bool self = cpuMask.GetBit(currentCPU);
	bool broadcast = (!self && targetCPUs == sNumCPUs - 1)
		|| (self && targetCPUs == sNumCPUs);

	TRACE("smp_multicast_ici_interrupts_disabled %" B_PRId32 ": inserting msg %p "
		"into broadcast mbox\n", currentCPU, msg);

	// stick it in the appropriate cpu's mailbox
	acquire_write_spinlock_nocheck(&sBroadcastMessageSpinlock);
	msg->next = sBroadcastMessages;
	sBroadcastMessages = msg;
	release_write_spinlock(&sBroadcastMessageSpinlock);

	atomic_add(&sBroadcastMessageCounter, 1);
	for (int32 i = 0; i < sNumCPUs; i++) {
		if (!cpuMask.GetBit(i))
			atomic_add(&gCPU[i].ici_counter, 1);
	}

	if (broadcast) {
		arch_smp_send_broadcast_ici();
	} else {
		CPUSet sendMask = cpuMask;
		sendMask.ClearBit(currentCPU);
		arch_smp_send_multicast_ici(sendMask);
	}

	TRACE("smp_multicast_ici_interrupts_disabled %" B_PRId32 ": sent interrupt\n",
		currentCPU);

	if ((flags & SMP_MSG_FLAG_SYNC) != 0) {
		// wait for the other cpus to finish processing it
		// the interrupt handler will ref count it to <0
		// if the message is sync after it has removed it from the mailbox
		TRACE("smp_multicast_ici_interrupts_disabled %" B_PRId32 ": waiting for "
			"ack\n", currentCPU);

		while (msg->done == 0) {
			process_all_pending_ici(currentCPU);
			cpu_wait(&msg->done, 1);
		}

		TRACE("smp_multicast_ici_interrupts_disabled %" B_PRId32 ": returning "
			"message to free list\n", currentCPU);

		// for SYNC messages, it's our responsibility to put it
		// back into the free list
		return_free_message(msg);
	} else if (self) {
		// make sure this CPU has processed the message at least
		process_all_pending_ici(currentCPU);
	}

	TRACE("smp_multicast_ici_interrupts_disabled: done\n");
}


/*!	Spin on non-boot CPUs until smp_wake_up_non_boot_cpus() has been called.

	\param cpu The index of the calling CPU.
	\param rendezVous A rendez-vous variable to make sure that the boot CPU
		does not return before all other CPUs have started waiting.
	\return \c true on the boot CPU, \c false otherwise.
*/
/**
 * @brief Hold application processors at the startup barrier until the BSP is ready.
 *
 * @param cpu         Index of the calling CPU (0 = BSP, >0 = AP).
 * @param rendezVous  Shared counter used to synchronise all CPUs at the barrier;
 *                    must be initialised to 0 before the first call.
 * @retval true  Caller is the boot CPU (cpu == 0); returns after all APs have arrived.
 * @retval false Caller is an AP; spins here until smp_wake_up_non_boot_cpus() is called,
 *               servicing early CPU calls in the interim.
 * @note Interrupts must be disabled. APs busy-wait on sBootCPUSpin and process
 *       any pending early-boot CPU calls via process_early_cpu_call() while blocked.
 */
bool
smp_trap_non_boot_cpus(int32 cpu, uint32* rendezVous)
{
	ASSERT(!are_interrupts_enabled());

	if (cpu == 0) {
		smp_cpu_rendezvous(rendezVous);
		return true;
	}

	smp_cpu_rendezvous(rendezVous);

	while (sBootCPUSpin == 0) {
		if (sEarlyCPUCallSet.GetBitAtomic(cpu))
			process_early_cpu_call(cpu);

		cpu_pause();
	}

	return false;
}


/**
 * @brief Release all application processors from the startup barrier and enable ICI.
 *
 * @note Called by the BSP after early kernel initialisation is complete. Sets
 *       sICIEnabled so that ICI messages will be dispatched from this point
 *       forward, then atomically sets sBootCPUSpin to unblock all waiting APs.
 */
void
smp_wake_up_non_boot_cpus()
{
	// ICIs were previously being ignored
	if (sNumCPUs > 1)
		sICIEnabled = true;

	// resume non boot CPUs
	atomic_set(&sBootCPUSpin, 1);
}


/*!	Spin until all CPUs have reached the rendez-vous point.

	The rendez-vous variable \c *var must have been initialized to 0 before the
	function is called. The variable will be non-null when the function returns.

	Note that when the function returns on one CPU, it only means that all CPU
	have already entered the function. It does not mean that the variable can
	already be reset. Only when all CPUs have returned (which would have to be
	ensured via another rendez-vous) the variable can be reset.
*/
/**
 * @brief Spin-wait until every online CPU has incremented the shared rendez-vous counter.
 *
 * @param var Pointer to a counter initialised to 0; atomically incremented by each
 *            participating CPU. The function returns once the counter equals sNumCPUs.
 * @note Must be called with interrupts disabled. Does not guarantee that all CPUs
 *       have returned from the function — only that all have entered it. A second
 *       rendez-vous is needed to safely reset @p var.
 */
void
smp_cpu_rendezvous(uint32* var)
{
	atomic_add((int32*)var, 1);

	while (*var < (uint32)sNumCPUs)
		cpu_wait((int32*)var, sNumCPUs);
}


/**
 * @brief Initialize SMP infrastructure and allocate the ICI message pool.
 *
 * @param args Pointer to the kernel_args structure populated by the boot loader;
 *             args->num_cpus determines whether SMP message allocation occurs.
 * @retval B_OK     Initialisation succeeded.
 * @retval B_ERROR  Area creation for the ICI message pool failed (panics first).
 * @note Registers kernel debugger commands for spinlock and ICI diagnostics.
 *       Called once by the BSP during early kernel startup before VM is active.
 */
status_t
smp_init(kernel_args* args)
{
	TRACE("smp_init: entry\n");

#if DEBUG_SPINLOCKS
	add_debugger_command_etc("spinlock", &dump_spinlock,
		"Dump info on a spinlock",
		"\n"
		"Dumps info on a spinlock.\n", 0);
#endif
	add_debugger_command_etc("ici", &dump_ici_messages,
		"Dump info on pending ICI messages",
		"\n"
		"Dumps info on pending ICI messages.\n", 0);
	add_debugger_command_etc("ici_message", &dump_ici_message,
		"Dump info on an ICI message",
		"\n"
		"Dumps info on an ICI message.\n", 0);

	if (args->num_cpus > 1) {
		sNumCPUs = args->num_cpus;

		struct smp_msg* messages;
		size_t size = ROUNDUP(sNumCPUs * MSG_ALLOCATE_PER_CPU * sizeof(smp_msg), B_PAGE_SIZE);
		area_id area = create_area("smp ici msgs", (void**)&messages, B_ANY_KERNEL_ADDRESS,
			size, B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (area < 0) {
			panic("error creating smp msgs");
			return area;
		}
		memset((void*)messages, 0, size);

		for (size_t i = 0; i < (size / sizeof(smp_msg)); i++) {
			struct smp_msg* msg = &messages[i];
			msg->next = sFreeMessages;
			sFreeMessages = msg;
			sFreeMessageCount++;
		}
	}
	TRACE("smp_init: calling arch_smp_init\n");

	return arch_smp_init(args);
}


/**
 * @brief Per-CPU SMP initialisation called on each processor during kernel startup.
 *
 * @param args Pointer to the kernel_args structure from the boot loader.
 * @param cpu  Index of the CPU being initialised.
 * @retval B_OK     Architecture-level per-CPU SMP init succeeded.
 * @retval <other>  Error code propagated from arch_smp_per_cpu_init().
 * @note Delegates entirely to the architecture-specific implementation.
 *       Called once per CPU (both BSP and APs) after smp_init() has completed.
 */
status_t
smp_per_cpu_init(kernel_args* args, int32 cpu)
{
	return arch_smp_per_cpu_init(args, cpu);
}


/**
 * @brief Register the spinlock-contention generic syscall after the syscall layer is ready.
 *
 * @retval B_OK      Registration succeeded, or B_DEBUG_SPINLOCK_CONTENTION is disabled.
 * @retval <other>   Error code from register_generic_syscall().
 * @note Only registers the syscall when B_DEBUG_SPINLOCK_CONTENTION is defined.
 *       Called during the generic-syscall initialisation phase of kernel startup.
 */
status_t
smp_init_post_generic_syscalls(void)
{
#if B_DEBUG_SPINLOCK_CONTENTION
	return register_generic_syscall(SPINLOCK_CONTENTION,
		&spinlock_contention_syscall, 0, 0);
#else
	return B_OK;
#endif
}


/**
 * @brief Set the global CPU count used by all SMP primitives.
 *
 * @param numCPUs The new CPU count; must be between 1 and SMP_MAX_CPUS inclusive.
 * @note Not thread-safe; must be called only during early boot before any other
 *       CPU has started executing kernel code or after all CPUs are quiesced.
 */
void
smp_set_num_cpus(int32 numCPUs)
{
	sNumCPUs = numCPUs;
}


/**
 * @brief Return the number of CPUs currently known to the SMP layer.
 *
 * @return The current CPU count (at least 1, even on uniprocessor systems).
 */
int32
smp_get_num_cpus()
{
	return sNumCPUs;
}


/**
 * @brief Return the logical index of the CPU executing the calling thread.
 *
 * @return Zero-based index of the current CPU.
 * @note Reads the cpu_num field of the current thread's CPU structure.
 *       Must not be called before the per-CPU thread pointer is initialised.
 */
int32
smp_get_current_cpu(void)
{
	return thread_get_current_thread()->cpu->cpu_num;
}


/**
 * @brief Invoke a function on a single target CPU, either inline or via ICI.
 *
 * @param targetCPU Index of the CPU on which @p func should execute.
 * @param func      Callback with signature void(void*, int).
 * @param cookie    Opaque pointer forwarded to @p func.
 * @param sync      If true, block until @p func completes on the target CPU.
 * @note Pins the calling thread to its CPU for the duration of the call to
 *       prevent migration. If the target is the current CPU, the function is
 *       invoked directly with interrupts disabled. Panics if ICI is not yet
 *       enabled and the target differs from the current CPU.
 */
static void
call_single_cpu(uint32 targetCPU, void (*func)(void*, int), void* cookie, bool sync)
{
	thread_pin_to_current_cpu(thread_get_current_thread());

	if (targetCPU == (uint32)smp_get_current_cpu()) {
		cpu_status state = disable_interrupts();
		func(cookie, smp_get_current_cpu());
		restore_interrupts(state);
		thread_unpin_from_current_cpu(thread_get_current_thread());
		return;
	}

	if (!sICIEnabled) {
		// Early mechanism not available
		panic("call_single_cpu is not yet available");
		thread_unpin_from_current_cpu(thread_get_current_thread());
		return;
	}

	smp_send_ici(targetCPU, SMP_MSG_CALL_FUNCTION, (addr_t)cookie,
		0, 0, (void*)func, sync ? SMP_MSG_FLAG_SYNC : SMP_MSG_FLAG_ASYNC);

	thread_unpin_from_current_cpu(thread_get_current_thread());
}


/**
 * @brief Asynchronously invoke a function on a single target CPU.
 *
 * @param targetCPU Index of the CPU on which @p func should execute.
 * @param func      Callback with signature void(void*, int).
 * @param cookie    Opaque pointer forwarded to @p func.
 * @note Returns immediately without waiting for @p func to complete.
 *       Delegates to the internal call_single_cpu() with sync=false.
 */
void
call_single_cpu(uint32 targetCPU, void (*func)(void*, int), void* cookie)
{
	return call_single_cpu(targetCPU, func, cookie, false);
}


/**
 * @brief Synchronously invoke a function on a single target CPU and wait for completion.
 *
 * @param targetCPU Index of the CPU on which @p func should execute.
 * @param func      Callback with signature void(void*, int).
 * @param cookie    Opaque pointer forwarded to @p func.
 * @note Blocks the caller until @p func has finished executing on the target CPU.
 *       Delegates to the internal call_single_cpu() with sync=true.
 */
void
call_single_cpu_sync(uint32 targetCPU, void (*func)(void*, int), void* cookie)
{
	return call_single_cpu(targetCPU, func, cookie, true);
}


// #pragma mark - public exported functions


/**
 * @brief Invoke a function on all CPUs, using the early or ICI mechanism as appropriate.
 *
 * @param function Callback with signature void(void*, int).
 * @param cookie   Opaque pointer forwarded to each callback invocation.
 * @param sync     If true, block until all CPUs have completed the callback.
 * @note On a single-CPU system the callback is invoked directly with interrupts
 *       disabled. On SMP systems before ICI is available, call_all_cpus_early()
 *       is used; afterwards smp_broadcast_ici() is used.
 */
static void
call_all_cpus(void (*function)(void*, int), void* cookie, bool sync)
{
	if (sNumCPUs == 1) {
		cpu_status state = disable_interrupts();
		function(cookie, 0);
		restore_interrupts(state);
		return;
	}

	// if inter-CPU communication is not yet enabled, use the early mechanism
	if (!sICIEnabled) {
		call_all_cpus_early(function, cookie);
		return;
	}

	smp_broadcast_ici(SMP_MSG_CALL_FUNCTION, (addr_t)cookie,
		0, 0, (void*)function, sync ? SMP_MSG_FLAG_SYNC : SMP_MSG_FLAG_ASYNC);
}


/**
 * @brief Asynchronously invoke a function on every online CPU.
 *
 * @param func   Callback with signature void(void*, int).
 * @param cookie Opaque pointer forwarded to each callback invocation.
 * @note Returns before the callback has necessarily completed on all CPUs.
 *       Delegates to the internal call_all_cpus() with sync=false.
 */
void
call_all_cpus(void (*func)(void*, int), void* cookie)
{
	call_all_cpus(func, cookie, false);
}


/**
 * @brief Synchronously invoke a function on every online CPU and wait for all to finish.
 *
 * @param func   Callback with signature void(void*, int).
 * @param cookie Opaque pointer forwarded to each callback invocation.
 * @note Blocks until all CPUs have executed @p func.
 *       Delegates to the internal call_all_cpus() with sync=true.
 */
void
call_all_cpus_sync(void (*func)(void*, int), void* cookie)
{
	call_all_cpus(func, cookie, true);
}


// Ensure the symbols for memory_barriers are still included
// in the kernel for binary compatibility. Calls are forwarded
// to the more efficent per-processor atomic implementations.
#undef memory_read_barrier
#undef memory_write_barrier


/**
 * @brief Issue a memory read barrier to prevent load reordering across this point.
 *
 * @note This out-of-line version is provided for binary-compatibility with code
 *       that calls memory_read_barrier() as a function. New code should use the
 *       inline memory_read_barrier_inline() instead.
 */
void
memory_read_barrier()
{
	memory_read_barrier_inline();
}


/**
 * @brief Issue a memory write barrier to prevent store reordering across this point.
 *
 * @note This out-of-line version is provided for binary-compatibility with code
 *       that calls memory_write_barrier() as a function. New code should use the
 *       inline memory_write_barrier_inline() instead.
 */
void
memory_write_barrier()
{
	memory_write_barrier_inline();
}
