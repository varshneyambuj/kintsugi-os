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
 *   Copyright 2013-2014, Paweł Dziepak, pdziepak@quarnos.org.
 *   Copyright 2009, Rene Gollent, rene@gollent.com.
 *   Copyright 2008-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2002, Angelo Mottola, a.mottola@libero.it.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file scheduler.cpp
 * @brief Core thread scheduler — run-queue management and CPU dispatch.
 *
 * Implements scheduler_reschedule(), scheduler_enqueue_in_run_queue(),
 * scheduler_thread_enqueued(), and the scheduler initialization. Delegates
 * to the active scheduler mode (low-latency or power-saving) for the actual
 * thread-selection policy.
 *
 * @see scheduler_cpu.cpp, scheduler_thread.cpp, low_latency.cpp
 */

#include <OS.h>

#include <AutoDeleter.h>
#include <cpu.h>
#include <debug.h>
#include <interrupts.h>
#include <kernel.h>
#include <kscheduler.h>
#include <listeners.h>
#include <load_tracking.h>
#include <scheduler_defs.h>
#include <smp.h>
#include <timer.h>
#include <util/Random.h>

#include "scheduler_common.h"
#include "scheduler_cpu.h"
#include "scheduler_locking.h"
#include "scheduler_modes.h"
#include "scheduler_profiler.h"
#include "scheduler_thread.h"
#include "scheduler_tracing.h"


namespace Scheduler {


class ThreadEnqueuer : public ThreadProcessing {
public:
	void		operator()(ThreadData* thread);
};

scheduler_mode gCurrentModeID;
scheduler_mode_operations* gCurrentMode;

bool gSingleCore;
bool gTrackCoreLoad;
bool gTrackCPULoad;

}	// namespace Scheduler

using namespace Scheduler;


static bool sSchedulerEnabled;

SchedulerListenerList gSchedulerListeners;
spinlock gSchedulerListenersLock = B_SPINLOCK_INITIALIZER;

static scheduler_mode_operations* sSchedulerModes[] = {
	&gSchedulerLowLatencyMode,
	&gSchedulerPowerSavingMode,
};

// Since CPU IDs used internally by the kernel bear no relation to the actual
// CPU topology the following arrays are used to efficiently get the core
// and the package that CPU in question belongs to.
static int32* sCPUToCore;
static int32* sCPUToPackage;


static void enqueue(Thread* thread, bool newOne);


/**
 * @brief ThreadProcessing functor that re-enqueues a thread into the run queue.
 *
 * Used when iterating threads that need to be migrated or reinserted, e.g.
 * when a CPU is being disabled.  Calls the file-local enqueue() helper with
 * @c newOne = false so that penalty state is preserved.
 *
 * @param thread ThreadData for the thread to re-enqueue.
 */
void
ThreadEnqueuer::operator()(ThreadData* thread)
{
	enqueue(thread->GetThread(), false);
}


/**
 * @brief Dumps the scheduler-private data for a thread to the kernel debugger.
 *
 * Forwards to ThreadData::Dump() which prints priority, core/CPU assignment,
 * quantum state, and penalty information.
 *
 * @param thread Thread whose scheduler data should be printed.
 */
void
scheduler_dump_thread_data(Thread* thread)
{
	thread->scheduler_data->Dump();
}


/**
 * @brief Internal helper that places a thread onto the most appropriate CPU run queue.
 *
 * Selects a target CPU/core for the thread based on pinning, single-core
 * mode, cache affinity, and load balancing, then calls ThreadData::Enqueue().
 * If the selected CPU has lower priority than the incoming thread, or if the
 * run queue was empty, an ICI (inter-CPU interrupt) is sent to trigger an
 * immediate reschedule on the target CPU.
 *
 * @param thread  Thread to enqueue.
 * @param newOne  @c true if this is the first time the thread enters the queue
 *                (allows cache-affinity check bypass); @c false for a re-queue.
 * @note Must be called with interrupts disabled and the scheduler mode lock held.
 */
static void
enqueue(Thread* thread, bool newOne)
{
	SCHEDULER_ENTER_FUNCTION();

	ThreadData* threadData = thread->scheduler_data;

	int32 threadPriority = threadData->GetEffectivePriority();
	T(EnqueueThread(thread, threadPriority));

	CPUEntry* targetCPU = NULL;
	CoreEntry* targetCore = NULL;
	if (thread->pinned_to_cpu > 0) {
		ASSERT(thread->previous_cpu != NULL);
		ASSERT(threadData->Core() != NULL);
		targetCPU = &gCPUEntries[thread->previous_cpu->cpu_num];
	} else if (gSingleCore) {
		targetCore = &gCoreEntries[0];
	} else if (threadData->Core() != NULL
		&& (!newOne || !threadData->HasCacheExpired())) {
		targetCore = threadData->Rebalance();
	}

	const bool rescheduleNeeded = threadData->ChooseCoreAndCPU(targetCore, targetCPU);

	TRACE("enqueueing thread %" B_PRId32 " with priority %" B_PRId32 " on CPU %" B_PRId32 " (core %" B_PRId32 ")\n",
		thread->id, threadPriority, targetCPU->ID(), targetCore->ID());

	bool wasRunQueueEmpty = false;
	threadData->Enqueue(wasRunQueueEmpty);

	// notify listeners
	NotifySchedulerListeners(&SchedulerListener::ThreadEnqueuedInRunQueue,
		thread);

	int32 heapPriority = CPUPriorityHeap::GetKey(targetCPU);
	if (threadPriority > heapPriority
		|| (threadPriority == heapPriority && rescheduleNeeded)
		|| wasRunQueueEmpty) {

		if (targetCPU->ID() == smp_get_current_cpu()) {
			gCPU[targetCPU->ID()].invoke_scheduler = true;
		} else if (atomic_get_and_set(&gCPU[targetCPU->ID()].invoke_scheduler, true) != true) {
			smp_send_ici(targetCPU->ID(), SMP_MSG_RESCHEDULE, 0, 0, 0,
				NULL, SMP_MSG_FLAG_ASYNC);
		}
	}
}


/**
 * @brief Adds a thread to the run queue so it can be scheduled on a CPU.
 *
 * Cancels any active scheduling penalty if appropriate, then delegates to the
 * internal enqueue() helper.  This is the primary entry point called from
 * thread wake-up paths (e.g. semaphore release, condition variable signal).
 *
 * @param thread Thread to enqueue; must not already be in the run queue.
 * @note Interrupts must be disabled on entry (asserted at runtime).
 * @note The scheduler mode lock is acquired internally.
 */
void
scheduler_enqueue_in_run_queue(Thread *thread)
{
	ASSERT(!are_interrupts_enabled());
	SCHEDULER_ENTER_FUNCTION();

	SchedulerModeLocker _;

	TRACE("enqueueing new thread %" B_PRId32 " with static priority %" B_PRId32 "\n", thread->id,
		thread->priority);

	ThreadData* threadData = thread->scheduler_data;

	if (threadData->ShouldCancelPenalty())
		threadData->CancelPenalty();

	enqueue(thread, true);
}


/**
 * @brief Changes the scheduling priority of a thread.
 *
 * Acquires the thread's scheduler lock and the mode lock, updates
 * @c thread->priority, cancels any active penalty, and if the thread is
 * already in the run queue removes and re-inserts it at the new priority.
 * If the thread is currently running its CPU priority heap entry is updated
 * in place.
 *
 * @param thread   Thread whose priority should be changed.
 * @param priority New static priority value.
 * @return The thread's previous priority.
 * @note Must be called with interrupts enabled; acquires locks internally.
 */
int32
scheduler_set_thread_priority(Thread *thread, int32 priority)
{
	ASSERT(are_interrupts_enabled());

	InterruptsSpinLocker _(thread->scheduler_lock);
	SchedulerModeLocker modeLocker;

	SCHEDULER_ENTER_FUNCTION();

	ThreadData* threadData = thread->scheduler_data;
	int32 oldPriority = thread->priority;

	TRACE("changing thread %" B_PRId32 " priority to %" B_PRId32 " (old: %" B_PRId32 ", effective: %" B_PRId32 ")\n",
		thread->id, priority, oldPriority, threadData->GetEffectivePriority());

	thread->priority = priority;
	threadData->CancelPenalty();

	if (priority == oldPriority)
		return oldPriority;

	if (thread->state != B_THREAD_READY) {
		if (thread->state == B_THREAD_RUNNING) {
			ASSERT(threadData->Core() != NULL);

			ASSERT(thread->cpu != NULL);
			CPUEntry* cpu = &gCPUEntries[thread->cpu->cpu_num];

			CoreCPUHeapLocker _(threadData->Core());
			cpu->UpdatePriority(priority);
		}

		return oldPriority;
	}

	// The thread is in the run queue. We need to remove it and re-insert it at
	// a new position.

	T(RemoveThread(thread));

	// notify listeners
	NotifySchedulerListeners(&SchedulerListener::ThreadRemovedFromRunQueue,
		thread);

	if (threadData->Dequeue())
		enqueue(thread, true);

	return oldPriority;
}


/**
 * @brief ICI handler stub for reschedule inter-CPU interrupts.
 *
 * Called on the target CPU when another CPU sends an @c SMP_MSG_RESCHEDULE
 * ICI.  The actual reschedule is driven by the @c invoke_scheduler flag which
 * the sender already set; this function intentionally performs no additional
 * work to avoid clearing that flag prematurely.
 */
void
scheduler_reschedule_ici()
{
	// This function is called as a result of an incoming ICI.
	// Since invoke_scheduler will have been set by whatever sent the ICI, we
	// shouldn't set it here (as the scheduler may have already cleared it.)
}


/**
 * @brief Stops CPU-time user timers for a thread that is being descheduled.
 *
 * Acquires the team and thread time locks before delegating to
 * user_timer_stop_cpu_timers() so that timer accounting remains consistent
 * across a context switch.
 *
 * @param fromThread Thread being switched away from.
 * @param toThread   Thread being switched to.
 * @note Called from switch_thread() with interrupts disabled.
 */
static inline void
stop_cpu_timers(Thread* fromThread, Thread* toThread)
{
	SpinLocker teamLocker(&fromThread->team->time_lock);
	SpinLocker threadLocker(&fromThread->time_lock);

	if (fromThread->HasActiveCPUTimeUserTimers()
		|| fromThread->team->HasActiveCPUTimeUserTimers()) {
		user_timer_stop_cpu_timers(fromThread, toThread);
	}
}


/**
 * @brief Resumes CPU-time user timers for a thread that has just been scheduled.
 *
 * Acquires the team and thread time locks before delegating to
 * user_timer_continue_cpu_timers().
 *
 * @param thread Thread that has just been scheduled onto a CPU.
 * @param cpu    The CPU structure for the processor running @p thread.
 * @note Called from thread_resumes() with interrupts disabled.
 */
static inline void
continue_cpu_timers(Thread* thread, cpu_ent* cpu)
{
	SpinLocker teamLocker(&thread->team->time_lock);
	SpinLocker threadLocker(&thread->time_lock);

	if (thread->HasActiveCPUTimeUserTimers()
		|| thread->team->HasActiveCPUTimeUserTimers()) {
		user_timer_continue_cpu_timers(thread, cpu->previous_thread);
	}
}


/**
 * @brief Performs post-switch bookkeeping for a thread that has just resumed.
 *
 * Releases the scheduler lock of the thread that was previously running on
 * this CPU, resumes CPU-time user timers, and notifies the user debugger that
 * this thread has been scheduled.
 *
 * @param thread Thread that has just resumed execution.
 * @note Called with interrupts disabled immediately after the context switch.
 */
static void
thread_resumes(Thread* thread)
{
	cpu_ent* cpu = thread->cpu;

	release_spinlock(&cpu->previous_thread->scheduler_lock);

	// continue CPU time based user timers
	continue_cpu_timers(thread, cpu);

	// notify the user debugger code
	if ((thread->flags & THREAD_FLAGS_DEBUGGER_INSTALLED) != 0)
		user_debug_thread_scheduled(thread);
}


/**
 * @brief Entry point called when a brand-new thread begins executing for the first time.
 *
 * Performs the resume bookkeeping (releasing the previous thread's scheduler
 * lock, restarting CPU timers) and records the thread's start time for CPU
 * accounting.
 *
 * @param thread The newly started thread (the current thread on entry).
 * @note Must be called with interrupts disabled.
 */
void
scheduler_new_thread_entry(Thread* thread)
{
	thread_resumes(thread);

	SpinLocker locker(thread->time_lock);
	thread->last_time = system_time();
}


/**
 * @brief Performs the low-level CPU context switch between two threads.
 *
 * Notifies the user debugger that @p fromThread is being unscheduled, stops
 * its CPU-time timers, updates the CPU and thread structures to reflect the
 * new running thread, then calls arch_thread_context_switch() to do the
 * actual register-level switch.  When arch_thread_context_switch() returns
 * (i.e. @p fromThread is rescheduled) thread_resumes() is called for
 * @p fromThread.
 *
 * @param fromThread Currently running thread; will be suspended.
 * @param toThread   Thread to switch to; must differ from @p fromThread.
 * @note Called with interrupts disabled and @p toThread's scheduler lock held.
 */
static inline void
switch_thread(Thread* fromThread, Thread* toThread)
{
	// notify the user debugger code
	if ((fromThread->flags & THREAD_FLAGS_DEBUGGER_INSTALLED) != 0)
		user_debug_thread_unscheduled(fromThread);

	// stop CPU time based user timers
	stop_cpu_timers(fromThread, toThread);

	// update CPU and Thread structures and perform the context switch
	cpu_ent* cpu = fromThread->cpu;
	toThread->previous_cpu = toThread->cpu = cpu;
	fromThread->cpu = NULL;
	cpu->running_thread = toThread;
	cpu->previous_thread = fromThread;

	arch_thread_set_current_thread(toThread);
	arch_thread_context_switch(fromThread, toThread);

	// The use of fromThread below looks weird, but is correct. fromThread had
	// been unscheduled earlier, but is back now. For a thread scheduled the
	// first time the same is done in thread.cpp:common_thread_entry().
	thread_resumes(fromThread);
}


/**
 * @brief Core scheduling algorithm — selects the next thread and switches to it.
 *
 * Clears the @c invoke_scheduler flag, transitions the currently running
 * thread to @p nextState, selects the highest-priority runnable thread for
 * this CPU (or the idle thread if the CPU is disabled), enqueues the old
 * thread back into the run queue if appropriate, and calls switch_thread() to
 * perform the actual context switch.
 *
 * @param nextState Desired state for the currently running thread after it is
 *                  descheduled (e.g. @c B_THREAD_READY, @c B_THREAD_WAITING,
 *                  @c THREAD_STATE_FREE_ON_RESCHED).
 * @note Must be called with interrupts disabled.
 * @note The scheduler mode lock is acquired and released internally.
 */
static void
reschedule(int32 nextState)
{
	ASSERT(!are_interrupts_enabled());
	SCHEDULER_ENTER_FUNCTION();

	int32 thisCPU = smp_get_current_cpu();
	atomic_set(&gCPU[thisCPU].invoke_scheduler, false);

	CPUEntry* cpu = CPUEntry::GetCPU(thisCPU);
	CoreEntry* core = CoreEntry::GetCore(thisCPU);

	Thread* oldThread = thread_get_current_thread();
	ThreadData* oldThreadData = oldThread->scheduler_data;

	CPUSet oldThreadMask;
	bool useOldThreadMask, fetchedOldThreadMask = false;

	oldThreadData->StopCPUTime();

	SchedulerModeLocker modeLocker;

	TRACE("reschedule(): cpu %" B_PRId32 ", current thread = %" B_PRId32 "\n", thisCPU,
		oldThread->id);

	oldThread->state = nextState;

	// return time spent in interrupts
	oldThreadData->SetStolenInterruptTime(gCPU[thisCPU].interrupt_time);

	bool enqueueOldThread = false;
	bool putOldThreadAtBack = false;
	switch (nextState) {
		case B_THREAD_RUNNING:
		case B_THREAD_READY:
			enqueueOldThread = true;

			oldThreadMask = oldThreadData->GetCPUMask();
			useOldThreadMask = !oldThreadMask.IsEmpty();
			fetchedOldThreadMask = true;

			if (!oldThreadData->IsIdle() && (!useOldThreadMask || oldThreadMask.GetBit(thisCPU))) {
				oldThreadData->Continues();
				if (oldThreadData->HasQuantumEnded(oldThread->cpu->preempted,
						oldThread->has_yielded)) {
					TRACE("enqueueing thread %ld into run queue priority ="
						" %ld\n", oldThread->id,
						oldThreadData->GetEffectivePriority());
					putOldThreadAtBack = true;
				} else {
					TRACE("putting thread %ld back in run queue priority ="
						" %ld\n", oldThread->id,
						oldThreadData->GetEffectivePriority());
					putOldThreadAtBack = false;
				}
			}

			break;
		case THREAD_STATE_FREE_ON_RESCHED:
			oldThreadData->Dies();
			break;
		default:
			oldThreadData->GoesAway();
			TRACE("not enqueueing thread %ld into run queue next_state = %ld\n",
				oldThread->id, nextState);
			break;
	}

	oldThread->has_yielded = false;

	// select thread with the biggest priority and enqueue back the old thread
	ThreadData* nextThreadData;
	if (gCPU[thisCPU].disabled) {
		if (!oldThreadData->IsIdle()) {
			if (oldThread->pinned_to_cpu == 0) {
				putOldThreadAtBack = true;
				oldThreadData->UnassignCore(true);
			} else {
				putOldThreadAtBack = false;
			}

			CPURunQueueLocker cpuLocker(cpu);
			nextThreadData = cpu->PeekIdleThread();
			cpu->Remove(nextThreadData);
		} else
			nextThreadData = oldThreadData;
	} else {
		if (!fetchedOldThreadMask) {
			oldThreadMask = oldThreadData->GetCPUMask();
			useOldThreadMask = !oldThreadMask.IsEmpty();
			fetchedOldThreadMask = true;
		}
		bool oldThreadShouldMigrate = useOldThreadMask && !oldThreadMask.GetBit(thisCPU);
		if (oldThreadShouldMigrate)
			enqueueOldThread = false;

		nextThreadData
			= cpu->ChooseNextThread(enqueueOldThread ? oldThreadData : NULL,
				putOldThreadAtBack);

		if (oldThreadShouldMigrate) {
			enqueue(oldThread, true);
			// replace with the idle thread, if no other thread could be found
			if (oldThreadData == nextThreadData)
				nextThreadData = cpu->PeekIdleThread();
		}

		// update CPU heap
		CoreCPUHeapLocker cpuLocker(core);
		cpu->UpdatePriority(nextThreadData->GetEffectivePriority());
	}

	Thread* nextThread = nextThreadData->GetThread();
	ASSERT(!gCPU[thisCPU].disabled || nextThreadData->IsIdle());

	if (nextThread != oldThread) {
		if (enqueueOldThread) {
			if (putOldThreadAtBack)
				enqueue(oldThread, false);
			else
				oldThreadData->PutBack();
		}

		acquire_spinlock(&nextThread->scheduler_lock);
	}

	TRACE("reschedule(): cpu %" B_PRId32 ", next thread = %" B_PRId32 "\n", thisCPU,
		nextThread->id);

	T(ScheduleThread(nextThread, oldThread));

	// notify listeners
	NotifySchedulerListeners(&SchedulerListener::ThreadScheduled,
		oldThread, nextThread);

	ASSERT(nextThreadData->Core() == core);
	nextThread->state = B_THREAD_RUNNING;
	nextThreadData->StartCPUTime();

	// track CPU activity
	cpu->TrackActivity(oldThreadData, nextThreadData);

	if (nextThread != oldThread || oldThread->cpu->preempted) {
		cpu->StartQuantumTimer(nextThreadData, oldThread->cpu->preempted);

		oldThread->cpu->preempted = false;
		if (!nextThreadData->IsIdle())
			nextThreadData->Continues();
		else
			gCurrentMode->rebalance_irqs(true);
		nextThreadData->StartQuantum();

		modeLocker.Unlock();

		SCHEDULER_EXIT_FUNCTION();

		if (nextThread != oldThread)
			switch_thread(oldThread, nextThread);
	}
}


/**
 * @brief Public entry point to trigger a reschedule on the current CPU.
 *
 * Guards against calls made before the scheduler is fully enabled (e.g.
 * during early boot) and then delegates to the internal reschedule() function.
 *
 * @param nextState Desired state for the current thread after it yields the CPU.
 *                  Typical values are @c B_THREAD_READY (voluntary yield or
 *                  preemption) and @c B_THREAD_WAITING (blocking on a lock or
 *                  semaphore).
 * @note Interrupts must be disabled on entry (asserted at runtime).
 * @note The thread spinlock must be held by the caller.
 */
void
scheduler_reschedule(int32 nextState)
{
	ASSERT(!are_interrupts_enabled());
	SCHEDULER_ENTER_FUNCTION();

	if (!sSchedulerEnabled) {
		Thread* thread = thread_get_current_thread();
		if (thread != NULL && nextState != B_THREAD_READY)
			panic("scheduler_reschedule_no_op() called in non-ready thread");
		return;
	}

	reschedule(nextState);
}


/**
 * @brief Allocates and attaches scheduler-private data to a newly created thread.
 *
 * Creates a @c ThreadData object (using placement-new into the nothrow pool)
 * and assigns it to @c thread->scheduler_data.
 *
 * @param thread     Newly created thread that needs scheduler data.
 * @param idleThread @c true if this is an idle thread (currently unused here).
 * @retval B_OK        Scheduler data allocated successfully.
 * @retval B_NO_MEMORY Allocation failed.
 */
status_t
scheduler_on_thread_create(Thread* thread, bool idleThread)
{
	thread->scheduler_data = new(std::nothrow) ThreadData(thread);
	if (thread->scheduler_data == NULL)
		return B_NO_MEMORY;
	return B_OK;
}


/**
 * @brief Initializes the scheduler data for a thread before it runs for the first time.
 *
 * For idle threads, pins the thread to its assigned CPU and calls
 * ThreadData::Init() with the matching CoreEntry.  For regular threads calls
 * the parameterless ThreadData::Init().
 *
 * @param thread Thread whose scheduler data should be initialized.
 * @note Must be called after scheduler_on_thread_create() has succeeded.
 */
void
scheduler_on_thread_init(Thread* thread)
{
	ASSERT(thread->scheduler_data != NULL);

	if (thread_is_idle_thread(thread)) {
		static int32 sIdleThreadsID;
		int32 cpuID = atomic_add(&sIdleThreadsID, 1);

		thread->previous_cpu = &gCPU[cpuID];
		thread->pinned_to_cpu = 1;

		thread->scheduler_data->Init(CoreEntry::GetCore(cpuID));
	} else
		thread->scheduler_data->Init();
}


/**
 * @brief Releases scheduler-private resources when a thread is destroyed.
 *
 * Deletes the @c ThreadData object previously allocated by
 * scheduler_on_thread_create().
 *
 * @param thread Thread that is being destroyed.
 */
void
scheduler_on_thread_destroy(Thread* thread)
{
	delete thread->scheduler_data;
}


/**
 * @brief Bootstraps the scheduler by performing the first reschedule.
 *
 * Must be called from within the initial idle thread with interrupts disabled.
 * Acquires the idle thread's scheduler lock and calls reschedule() with
 * @c B_THREAD_READY so that the first real thread can be selected and started.
 *
 * @note Interrupts are disabled on entry and remain disabled on return.
 * @note This function does not return in the traditional sense — after the
 *       context switch the idle thread will only run again when no other
 *       thread is runnable.
 */
void
scheduler_start()
{
	InterruptsSpinLocker _(thread_get_current_thread()->scheduler_lock);
	SCHEDULER_ENTER_FUNCTION();

	reschedule(B_THREAD_READY);
}


/**
 * @brief Switches the global scheduler operation mode.
 *
 * Validates @p mode, logs the transition, acquires the big scheduler lock,
 * updates @c gCurrentMode and @c gCurrentModeID, notifies the new mode via
 * @c switch_to_mode(), and recomputes quantum lengths for all threads.
 *
 * @param mode  New mode: @c SCHEDULER_MODE_LOW_LATENCY or
 *              @c SCHEDULER_MODE_POWER_SAVING.
 * @retval B_OK        Mode switched successfully.
 * @retval B_BAD_VALUE @p mode is not a recognised scheduler mode.
 */
status_t
scheduler_set_operation_mode(scheduler_mode mode)
{
	if (mode != SCHEDULER_MODE_LOW_LATENCY
		&& mode != SCHEDULER_MODE_POWER_SAVING) {
		return B_BAD_VALUE;
	}

	dprintf("scheduler: switching to %s mode\n", sSchedulerModes[mode]->name);

	InterruptsBigSchedulerLocker _;

	gCurrentModeID = mode;
	gCurrentMode = sSchedulerModes[mode];
	gCurrentMode->switch_to_mode();

	ThreadData::ComputeQuantumLengths();

	return B_OK;
}


/**
 * @brief Enables or disables a CPU for scheduling purposes.
 *
 * Acquires the big scheduler lock, calls the current mode's set_cpu_enabled()
 * hook, starts or stops the CPUEntry, and updates the global CPU-enabled
 * bitmap.  If disabling, migrates any threads assigned to the CPU via a
 * ThreadEnqueuer and sends an ICI to force an immediate reschedule on the
 * target CPU.
 *
 * @param cpuID   Logical CPU ID to enable or disable.
 * @param enabled @c true to enable, @c false to disable.
 * @note Must be called with interrupts disabled (asserted in KDEBUG builds).
 */
void
scheduler_set_cpu_enabled(int32 cpuID, bool enabled)
{
#if KDEBUG
	if (are_interrupts_enabled())
		panic("scheduler_set_cpu_enabled: called with interrupts enabled");
#endif

	dprintf("scheduler: %s CPU %" B_PRId32 "\n",
		enabled ? "enabling" : "disabling", cpuID);

	InterruptsBigSchedulerLocker _;

	gCurrentMode->set_cpu_enabled(cpuID, enabled);

	CPUEntry* cpu = &gCPUEntries[cpuID];
	CoreEntry* core = cpu->Core();

	ASSERT(core->CPUCount() >= 0);
	if (enabled)
		cpu->Start();
	else {
		cpu->UpdatePriority(B_IDLE_PRIORITY);

		ThreadEnqueuer enqueuer;
		core->RemoveCPU(cpu, enqueuer);
	}

	gCPU[cpuID].disabled = !enabled;
	if (enabled)
		gCPUEnabled.SetBitAtomic(cpuID);
	else
		gCPUEnabled.ClearBitAtomic(cpuID);

	if (!enabled) {
		cpu->Stop();

		// don't wait until the thread quantum ends
		if (smp_get_current_cpu() != cpuID) {
			smp_send_ici(cpuID, SMP_MSG_RESCHEDULE, 0, 0, 0, NULL,
				SMP_MSG_FLAG_ASYNC);
		}
	}
}


/**
 * @brief Recursively populates the CPU-to-core and CPU-to-package mapping arrays.
 *
 * Walks the CPU topology tree depth-first.  At SMT leaf nodes the current
 * @p coreID and @p packageID are written into @c sCPUToCore and
 * @c sCPUToPackage respectively.
 *
 * @param node      Current topology node being processed.
 * @param packageID Physical package ID inherited from the parent PACKAGE node.
 * @param coreID    Physical core ID inherited from the parent CORE node.
 */
static void
traverse_topology_tree(const cpu_topology_node* node, int packageID, int coreID)
{
	switch (node->level) {
		case CPU_TOPOLOGY_SMT:
			sCPUToCore[node->id] = coreID;
			sCPUToPackage[node->id] = packageID;
			return;

		case CPU_TOPOLOGY_CORE:
			coreID = node->id;
			break;

		case CPU_TOPOLOGY_PACKAGE:
			packageID = node->id;
			break;

		default:
			break;
	}

	for (int32 i = 0; i < node->children_count; i++)
		traverse_topology_tree(node->children[i], packageID, coreID);
}


/**
 * @brief Allocates and fills the CPU-to-core and CPU-to-package mapping tables.
 *
 * Determines the logical CPU, core, and package counts from the SMP layer,
 * allocates the @c sCPUToCore and @c sCPUToPackage arrays, and populates them
 * by walking the topology tree with traverse_topology_tree().
 *
 * @param cpuCount     Output: total number of logical CPUs.
 * @param coreCount    Output: total number of physical cores.
 * @param packageCount Output: total number of physical packages.
 * @retval B_OK        Mappings built successfully.
 * @retval B_NO_MEMORY Allocation of one of the mapping arrays failed.
 */
static status_t
build_topology_mappings(int32& cpuCount, int32& coreCount, int32& packageCount)
{
	cpuCount = smp_get_num_cpus();

	sCPUToCore = new(std::nothrow) int32[cpuCount];
	if (sCPUToCore == NULL)
		return B_NO_MEMORY;
	ArrayDeleter<int32> cpuToCoreDeleter(sCPUToCore);

	sCPUToPackage = new(std::nothrow) int32[cpuCount];
	if (sCPUToPackage == NULL)
		return B_NO_MEMORY;
	ArrayDeleter<int32> cpuToPackageDeleter(sCPUToPackage);

	coreCount = 0;
	for (int32 i = 0; i < cpuCount; i++) {
		if (gCPU[i].topology_id[CPU_TOPOLOGY_SMT] == 0)
			coreCount++;
	}

	packageCount = 0;
	for (int32 i = 0; i < cpuCount; i++) {
		if (gCPU[i].topology_id[CPU_TOPOLOGY_SMT] == 0
			&& gCPU[i].topology_id[CPU_TOPOLOGY_CORE] == 0) {
			packageCount++;
		}
	}

	const cpu_topology_node* root = get_cpu_topology();
	traverse_topology_tree(root, 0, 0);

	cpuToCoreDeleter.Detach();
	cpuToPackageDeleter.Detach();
	return B_OK;
}


/**
 * @brief Internal scheduler initialization — builds topology maps and allocates entries.
 *
 * Calls build_topology_mappings() to determine the CPU/core/package counts,
 * configures single-core and load-tracking flags, and allocates the
 * @c CPUEntry, @c CoreEntry, and @c PackageEntry arrays.  Also initializes
 * the core load heaps and the idle-package list, then links CPUs to their
 * respective cores and packages.
 *
 * @retval B_OK        All structures allocated and initialized successfully.
 * @retval B_NO_MEMORY One of the allocation steps failed.
 */
static status_t
init()
{
	// create logical processor to core and package mappings
	int32 cpuCount, coreCount, packageCount;
	status_t result = build_topology_mappings(cpuCount, coreCount,
		packageCount);
	if (result != B_OK)
		return result;

	// disable parts of the scheduler logic that are not needed
	gSingleCore = coreCount == 1;
	scheduler_update_policy();

	gCoreCount = coreCount;
	gPackageCount = packageCount;

	gCPUEntries = new(std::nothrow) CPUEntry[cpuCount];
	if (gCPUEntries == NULL)
		return B_NO_MEMORY;
	ArrayDeleter<CPUEntry> cpuEntriesDeleter(gCPUEntries);

	gCoreEntries = new(std::nothrow) CoreEntry[coreCount];
	if (gCoreEntries == NULL)
		return B_NO_MEMORY;
	ArrayDeleter<CoreEntry> coreEntriesDeleter(gCoreEntries);

	gPackageEntries = new(std::nothrow) PackageEntry[packageCount];
	if (gPackageEntries == NULL)
		return B_NO_MEMORY;
	ArrayDeleter<PackageEntry> packageEntriesDeleter(gPackageEntries);

	new(&gCoreLoadHeap) CoreLoadHeap(coreCount);
	new(&gCoreHighLoadHeap) CoreLoadHeap(coreCount);

	new(&gIdlePackageList) IdlePackageList;

	for (int32 i = 0; i < cpuCount; i++) {
		CoreEntry* core = &gCoreEntries[sCPUToCore[i]];
		PackageEntry* package = &gPackageEntries[sCPUToPackage[i]];

		package->Init(sCPUToPackage[i]);
		core->Init(sCPUToCore[i], package);
		gCPUEntries[i].Init(i, core);

		core->AddCPU(&gCPUEntries[i]);
	}

	packageEntriesDeleter.Detach();
	coreEntriesDeleter.Detach();
	cpuEntriesDeleter.Detach();

	return B_OK;
}


/**
 * @brief Initializes the scheduler subsystem during kernel boot.
 *
 * Prints the CPU and cache-level topology, optionally initializes the
 * profiler, calls init() to build all scheduler data structures, sets the
 * initial operation mode to @c SCHEDULER_MODE_LOW_LATENCY, registers debugger
 * commands, and (when tracing is enabled) adds the scheduler trace command.
 *
 * @note Panics if init() fails — the kernel cannot continue without a working
 *       scheduler.
 */
void
scheduler_init()
{
	int32 cpuCount = smp_get_num_cpus();
	dprintf("scheduler_init: found %" B_PRId32 " logical cpu%s and %" B_PRId32
		" cache level%s\n", cpuCount, cpuCount != 1 ? "s" : "",
		gCPUCacheLevelCount, gCPUCacheLevelCount != 1 ? "s" : "");

#ifdef SCHEDULER_PROFILING
	Profiling::Profiler::Initialize();
#endif

	status_t result = init();
	if (result != B_OK)
		panic("scheduler_init: failed to initialize scheduler\n");

	scheduler_set_operation_mode(SCHEDULER_MODE_LOW_LATENCY);

	init_debug_commands();

#if SCHEDULER_TRACING
	add_debugger_command_etc("scheduler", &cmd_scheduler,
		"Analyze scheduler tracing information",
		"<thread>\n"
		"Analyzes scheduler tracing information for a given thread.\n"
		"  <thread>  - ID of the thread.\n", 0);
#endif
}


/**
 * @brief Enables the scheduler so that scheduler_reschedule() becomes operational.
 *
 * Sets the @c sSchedulerEnabled flag.  Until this is called,
 * scheduler_reschedule() is a no-op (panics if the current thread is not in
 * the ready state).
 */
void
scheduler_enable_scheduling()
{
	sSchedulerEnabled = true;
}


/**
 * @brief Recomputes load-tracking policy flags based on current hardware capabilities.
 *
 * Queries the CPU performance scaling interface to determine whether per-CPU
 * load tracking is possible, and derives the core-load tracking flag from the
 * single-core flag.  Prints a summary of the resulting policy to the kernel
 * log.
 */
void
scheduler_update_policy()
{
	gTrackCPULoad = increase_cpu_performance(0) == B_OK;
	gTrackCoreLoad = !gSingleCore || gTrackCPULoad;
	dprintf("scheduler switches: single core: %s, cpu load tracking: %s,"
		" core load tracking: %s\n", gSingleCore ? "true" : "false",
		gTrackCPULoad ? "true" : "false",
		gTrackCoreLoad ? "true" : "false");
}


// #pragma mark - SchedulerListener


/**
 * @brief Virtual destructor for the SchedulerListener base class.
 *
 * Defined out-of-line to anchor the vtable in this translation unit.
 */
SchedulerListener::~SchedulerListener()
{
}


// #pragma mark - kernel private


/**
 * @brief Registers a scheduler event listener.
 *
 * Appends @p listener to the global @c gSchedulerListeners list under the
 * @c gSchedulerListenersLock spinlock.
 *
 * @param listener Listener to register; must remain valid until removed.
 * @note The thread lock must be held by the caller.
 */
void
scheduler_add_listener(struct SchedulerListener* listener)
{
	InterruptsSpinLocker _(gSchedulerListenersLock);
	gSchedulerListeners.Add(listener);
}


/**
 * @brief Unregisters a previously added scheduler event listener.
 *
 * Removes @p listener from the global @c gSchedulerListeners list under the
 * @c gSchedulerListenersLock spinlock.
 *
 * @param listener Listener to remove; must have been previously added with
 *                 scheduler_add_listener().
 * @note The thread lock must be held by the caller.
 */
void
scheduler_remove_listener(struct SchedulerListener* listener)
{
	InterruptsSpinLocker _(gSchedulerListenersLock);
	gSchedulerListeners.Remove(listener);
}


// #pragma mark - Syscalls


/**
 * @brief Syscall: estimates the maximum scheduling latency for a thread.
 *
 * Computes an upper-bound estimate of how long @p id may wait before being
 * scheduled again, based on the number of threads competing on its core and
 * the current mode's quantum parameters.  Returns 0 if the thread does not
 * exist.
 *
 * @param id Thread ID to query, or a negative value for the calling thread.
 * @return Estimated maximum scheduling latency in microseconds, clamped to
 *         [@c minimal_quantum, @c maximum_latency].
 */
bigtime_t
_user_estimate_max_scheduling_latency(thread_id id)
{
	syscall_64_bit_return_value();

	// get the thread
	Thread* thread;
	if (id < 0) {
		thread = thread_get_current_thread();
		thread->AcquireReference();
	} else {
		thread = Thread::Get(id);
		if (thread == NULL)
			return 0;
	}
	BReference<Thread> threadReference(thread, true);

#ifdef SCHEDULER_PROFILING
	InterruptsLocker _;
#endif

	ThreadData* threadData = thread->scheduler_data;
	CoreEntry* core = threadData->Core();
	if (core == NULL)
		core = &gCoreEntries[get_random<int32>() % gCoreCount];

	int32 threadCount = core->ThreadCount();
	if (core->CPUCount() > 0)
		threadCount /= core->CPUCount();

	if (threadData->GetEffectivePriority() > 0) {
		threadCount -= threadCount * THREAD_MAX_SET_PRIORITY
				/ threadData->GetEffectivePriority();
	}

	return std::min(std::max(threadCount * gCurrentMode->base_quantum,
			gCurrentMode->minimal_quantum),
		gCurrentMode->maximum_latency);
}


/**
 * @brief Syscall: sets the global scheduler operation mode.
 *
 * Validates and applies the requested mode via scheduler_set_operation_mode()
 * and then notifies the CPU layer via cpu_set_scheduler_mode().
 *
 * @param mode Integer representation of the desired @c scheduler_mode.
 * @retval B_OK        Mode set successfully.
 * @retval B_BAD_VALUE @p mode does not correspond to a valid scheduler mode.
 */
status_t
_user_set_scheduler_mode(int32 mode)
{
	scheduler_mode schedulerMode = static_cast<scheduler_mode>(mode);
	status_t error = scheduler_set_operation_mode(schedulerMode);
	if (error == B_OK)
		cpu_set_scheduler_mode(schedulerMode);
	return error;
}


/**
 * @brief Syscall: returns the currently active scheduler operation mode.
 *
 * @return Current @c scheduler_mode as an @c int32 (either
 *         @c SCHEDULER_MODE_LOW_LATENCY or @c SCHEDULER_MODE_POWER_SAVING).
 */
int32
_user_get_scheduler_mode()
{
	return gCurrentModeID;
}
