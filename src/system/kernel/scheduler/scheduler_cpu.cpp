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
 *   Copyright 2013, Paweł Dziepak, pdziepak@quarnos.org.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file scheduler_cpu.cpp
 * @brief Per-CPU scheduler state and run-queue management.
 *
 * Maintains the per-CPU scheduler structures (CPUEntry, CoreEntry) including
 * run queues, load tracking, and CPU activation/deactivation. Controls which
 * threads are enqueued on which CPU for both normal and real-time scheduling.
 *
 * @see scheduler.cpp, scheduler_thread.cpp
 */


#include "scheduler_cpu.h"

#include <util/AutoLock.h>

#include <algorithm>

#include "scheduler_thread.h"


namespace Scheduler {


CPUEntry* gCPUEntries;

CoreEntry* gCoreEntries;
CoreLoadHeap gCoreLoadHeap;
CoreLoadHeap gCoreHighLoadHeap;
rw_spinlock gCoreHeapsLock = B_RW_SPINLOCK_INITIALIZER;
int32 gCoreCount;

PackageEntry* gPackageEntries;
IdlePackageList gIdlePackageList;
rw_spinlock gIdlePackageLock = B_RW_SPINLOCK_INITIALIZER;
int32 gPackageCount;


}	// namespace Scheduler

using namespace Scheduler;


class Scheduler::DebugDumper {
public:
	static	void		DumpCPURunQueue(CPUEntry* cpu);
	static	void		DumpCoreRunQueue(CoreEntry* core);
	static	void		DumpCoreLoadHeapEntry(CoreEntry* core);
	static	void		DumpIdleCoresInPackage(PackageEntry* package);

private:
	struct CoreThreadsData {
			CoreEntry*	fCore;
			int32		fLoad;
	};

	static	void		_AnalyzeCoreThreads(Thread* thread, void* data);
};


static CPUPriorityHeap sDebugCPUHeap;
static CoreLoadHeap sDebugCoreHeap;


/**
 * @brief Dump all threads in this run queue to the kernel debugger.
 *
 * Prints a header line followed by one row per thread showing the thread
 * pointer, id, nominal priority, effective-priority penalty, and name.
 * Prints a short message when the queue is empty.
 */
void
ThreadRunQueue::Dump() const
{
	ThreadRunQueue::ConstIterator iterator = GetConstIterator();
	if (!iterator.HasNext())
		kprintf("Run queue is empty.\n");
	else {
		kprintf("thread      id      priority penalty  name\n");
		while (iterator.HasNext()) {
			ThreadData* threadData = iterator.Next();
			Thread* thread = threadData->GetThread();

			kprintf("%p  %-7" B_PRId32 " %-8" B_PRId32 " %-8" B_PRId32 " %s\n",
				thread, thread->id, thread->priority,
				thread->priority - threadData->GetEffectivePriority(),
				thread->name);
		}
	}
}


/**
 * @brief Construct a CPUEntry with zeroed load counters and initialized locks.
 */
CPUEntry::CPUEntry()
	:
	fLoad(0),
	fMeasureActiveTime(0),
	fMeasureTime(0),
	fUpdateLoadEvent(false)
{
	B_INITIALIZE_RW_SPINLOCK(&fSchedulerModeLock);
	B_INITIALIZE_SPINLOCK(&fQueueLock);
}


/**
 * @brief Associate this CPUEntry with a logical CPU number and its parent core.
 *
 * @param id   Logical CPU index (matches gCPU[] array index).
 * @param core Pointer to the CoreEntry that owns this CPU.
 */
void
CPUEntry::Init(int32 id, CoreEntry* core)
{
	fCPUNumber = id;
	fCore = core;
}


/**
 * @brief Mark this CPU as active and register it with its parent CoreEntry.
 *
 * Resets the load counter to zero and calls CoreEntry::AddCPU() so the core
 * knows an additional logical CPU is available for scheduling.
 */
void
CPUEntry::Start()
{
	fLoad = 0;
	fCore->AddCPU(this);
}


/**
 * @brief Drain all IRQ assignments from this CPU before it goes offline.
 *
 * Iterates the CPU's IRQ list and reassigns each interrupt to any available
 * CPU (assign_io_interrupt_to_cpu with target -1) until the list is empty.
 */
void
CPUEntry::Stop()
{
	cpu_ent* entry = &gCPU[fCPUNumber];

	// get rid of irqs
	SpinLocker locker(entry->irqs_lock);
	irq_assignment* irq
		= (irq_assignment*)list_get_first_item(&entry->irqs);
	while (irq != NULL) {
		locker.Unlock();

		assign_io_interrupt_to_cpu(irq->irq, -1);

		locker.Lock();
		irq = (irq_assignment*)list_get_first_item(&entry->irqs);
	}
	locker.Unlock();
}


/**
 * @brief Insert @p thread at the front of this CPU's private run queue.
 *
 * @param thread   ThreadData to enqueue.
 * @param priority Scheduling priority key for the queue.
 */
void
CPUEntry::PushFront(ThreadData* thread, int32 priority)
{
	SCHEDULER_ENTER_FUNCTION();
	fRunQueue.PushFront(thread, priority);
}


/**
 * @brief Insert @p thread at the back of this CPU's private run queue.
 *
 * @param thread   ThreadData to enqueue.
 * @param priority Scheduling priority key for the queue.
 */
void
CPUEntry::PushBack(ThreadData* thread, int32 priority)
{
	SCHEDULER_ENTER_FUNCTION();
	fRunQueue.PushBack(thread, priority);
}


/**
 * @brief Remove @p thread from this CPU's private run queue.
 *
 * Asserts that the thread is currently marked as enqueued, clears the enqueued
 * flag via SetDequeued(), then removes it from the underlying priority queue.
 *
 * @param thread ThreadData to remove; must be enqueued on this CPU.
 */
void
CPUEntry::Remove(ThreadData* thread)
{
	SCHEDULER_ENTER_FUNCTION();
	ASSERT(thread->IsEnqueued());
	thread->SetDequeued();
	fRunQueue.Remove(thread);
}


/**
 * @brief Peek at the highest-priority thread in the core's shared run queue.
 *
 * @return Pointer to the highest-priority ThreadData, or NULL if empty.
 */
ThreadData*
CoreEntry::PeekThread() const
{
	SCHEDULER_ENTER_FUNCTION();
	return fRunQueue.PeekMaximum();
}


/**
 * @brief Peek at the highest-priority thread in this CPU's private run queue.
 *
 * @return Pointer to the highest-priority ThreadData, or NULL if empty.
 */
ThreadData*
CPUEntry::PeekThread() const
{
	SCHEDULER_ENTER_FUNCTION();
	return fRunQueue.PeekMaximum();
}


/**
 * @brief Peek at the idle thread pinned to this CPU.
 *
 * @return Pointer to the idle ThreadData at B_IDLE_PRIORITY, or NULL if none.
 */
ThreadData*
CPUEntry::PeekIdleThread() const
{
	SCHEDULER_ENTER_FUNCTION();
	return fRunQueue.GetHead(B_IDLE_PRIORITY);
}


/**
 * @brief Update the priority key of this CPU in the parent core's CPU heap.
 *
 * If the priority transitions from or to B_IDLE_PRIORITY the core's idle-CPU
 * tracking is updated via CPUWakesUp() or CPUGoesIdle(). No-ops when the
 * priority is unchanged. Must not be called for a disabled CPU.
 *
 * @param priority New effective priority to set on this CPU's heap key.
 */
void
CPUEntry::UpdatePriority(int32 priority)
{
	SCHEDULER_ENTER_FUNCTION();

	ASSERT(!gCPU[fCPUNumber].disabled);

	int32 oldPriority = CPUPriorityHeap::GetKey(this);
	if (oldPriority == priority)
		return;
	fCore->CPUHeap()->ModifyKey(this, priority);

	if (oldPriority == B_IDLE_PRIORITY)
		fCore->CPUWakesUp(this);
	else if (priority == B_IDLE_PRIORITY)
		fCore->CPUGoesIdle(this);
}


/**
 * @brief Sample current CPU load and optionally trigger IRQ rebalancing.
 *
 * Calls compute_load() to update fLoad from fMeasureTime/fMeasureActiveTime.
 * If the resulting load exceeds kVeryHighLoad the current scheduler mode's
 * rebalance_irqs() callback is invoked with false (non-forced).
 *
 * Must only be called on the current CPU and only when gTrackCPULoad is set.
 */
void
CPUEntry::ComputeLoad()
{
	SCHEDULER_ENTER_FUNCTION();

	ASSERT(gTrackCPULoad);
	ASSERT(!gCPU[fCPUNumber].disabled);
	ASSERT(fCPUNumber == smp_get_current_cpu());

	int oldLoad = compute_load(fMeasureTime, fMeasureActiveTime, fLoad,
			system_time());
	if (oldLoad < 0)
		return;

	if (fLoad > kVeryHighLoad)
		gCurrentMode->rebalance_irqs(false);
}


/**
 * @brief Select the next thread to run on this CPU.
 *
 * Compares the highest-priority thread from this CPU's private queue, the
 * core's shared queue, and the currently running @p oldThread. Returns
 * @p oldThread if it should continue running (still highest priority and
 * @p putAtBack is false). Otherwise dequeues and returns the winner from
 * whichever queue it came from.
 *
 * @param oldThread  The thread that is currently running, or NULL.
 * @param putAtBack  true if oldThread has used its full quantum and should be
 *                   placed at the back of the queue.
 * @return The ThreadData that should run next, or NULL if no runnable thread
 *         exists.
 */
ThreadData*
CPUEntry::ChooseNextThread(ThreadData* oldThread, bool putAtBack)
{
	SCHEDULER_ENTER_FUNCTION();

	int32 oldPriority = -1;
	if (oldThread != NULL)
		oldPriority = oldThread->GetEffectivePriority();

	CPURunQueueLocker cpuLocker(this);

	ThreadData* pinnedThread = fRunQueue.PeekMaximum();
	int32 pinnedPriority = -1;
	if (pinnedThread != NULL)
		pinnedPriority = pinnedThread->GetEffectivePriority();

	CoreRunQueueLocker coreLocker(fCore);

	ThreadData* sharedThread = fCore->PeekThread();
	if (sharedThread == NULL && pinnedThread == NULL && oldThread == NULL)
		return NULL;

	int32 sharedPriority = -1;
	if (sharedThread != NULL)
		sharedPriority = sharedThread->GetEffectivePriority();

	int32 rest = std::max(pinnedPriority, sharedPriority);
	if (oldPriority > rest || (!putAtBack && oldPriority == rest))
		return oldThread;

	if (sharedPriority > pinnedPriority) {
		fCore->Remove(sharedThread);
		return sharedThread;
	}

	coreLocker.Unlock();

	Remove(pinnedThread);
	return pinnedThread;
}


/**
 * @brief Account for the time the outgoing thread ran and set up the
 *        incoming thread's kernel/user time baseline.
 *
 * Updates the CPU's active-time accumulator and the core's active-time counter
 * from the difference between the old thread's kernel and user time fields and
 * the per-CPU last-measured values. Also propagates load tracking to the old
 * thread via UpdateActivity() and, when gTrackCPULoad is set, calls
 * ComputeLoad() and _RequestPerformanceLevel().
 *
 * @param oldThreadData  ThreadData of the thread that just stopped running.
 * @param nextThreadData ThreadData of the thread that is about to run.
 */
void
CPUEntry::TrackActivity(ThreadData* oldThreadData, ThreadData* nextThreadData)
{
	SCHEDULER_ENTER_FUNCTION();

	cpu_ent* cpuEntry = &gCPU[fCPUNumber];

	Thread* oldThread = oldThreadData->GetThread();
	if (!thread_is_idle_thread(oldThread)) {
		bigtime_t active
			= (oldThread->kernel_time - cpuEntry->last_kernel_time)
				+ (oldThread->user_time - cpuEntry->last_user_time);

		WriteSequentialLocker locker(cpuEntry->active_time_lock);
		cpuEntry->active_time += active;
		locker.Unlock();

		fMeasureActiveTime += active;
		fCore->IncreaseActiveTime(active);

		oldThreadData->UpdateActivity(active);
	}

	if (gTrackCPULoad) {
		if (!cpuEntry->disabled)
			ComputeLoad();
		_RequestPerformanceLevel(nextThreadData);
	}

	Thread* nextThread = nextThreadData->GetThread();
	if (!thread_is_idle_thread(nextThread)) {
		cpuEntry->last_kernel_time = nextThread->kernel_time;
		cpuEntry->last_user_time = nextThread->user_time;

		nextThreadData->SetLastInterruptTime(cpuEntry->interrupt_time);
	}
}


/**
 * @brief Arm (or re-arm) the per-CPU quantum timer for @p thread.
 *
 * Cancels any pending quantum timer when the thread was preempted or a load
 * event was pending. For non-idle threads programs a one-shot timer for the
 * thread's remaining quantum. For idle threads programs a periodic load-update
 * event timer instead.
 *
 * @param thread       ThreadData of the thread about to run.
 * @param wasPreempted true if the previous thread was preempted rather than
 *                     voluntarily yielding.
 */
void
CPUEntry::StartQuantumTimer(ThreadData* thread, bool wasPreempted)
{
	cpu_ent* cpu = &gCPU[ID()];

	if (!wasPreempted || fUpdateLoadEvent)
		cancel_timer(&cpu->quantum_timer);
	fUpdateLoadEvent = false;

	if (!thread->IsIdle()) {
		bigtime_t quantum = thread->GetQuantumLeft();
		add_timer(&cpu->quantum_timer, &CPUEntry::_RescheduleEvent, quantum,
			B_ONE_SHOT_RELATIVE_TIMER);
	} else if (gTrackCoreLoad) {
		add_timer(&cpu->quantum_timer, &CPUEntry::_UpdateLoadEvent,
			kLoadMeasureInterval * 2, B_ONE_SHOT_RELATIVE_TIMER);
		fUpdateLoadEvent = true;
	}
}


/**
 * @brief Adjust CPU performance level based on the current thread's load.
 *
 * Disabled CPUs are throttled to maximum. For enabled CPUs, computes the
 * maximum of the thread's own load and the core load, then calls
 * decrease_cpu_performance() or increase_cpu_performance() to drive the
 * hardware P-state toward kTargetLoad.
 *
 * @param threadData ThreadData of the thread that is about to run.
 */
void
CPUEntry::_RequestPerformanceLevel(ThreadData* threadData)
{
	SCHEDULER_ENTER_FUNCTION();

	if (gCPU[fCPUNumber].disabled) {
		decrease_cpu_performance(kCPUPerformanceScaleMax);
		return;
	}

	int32 load = std::max(threadData->GetLoad(), fCore->GetLoad());
	ASSERT_PRINT(load >= 0 && load <= kMaxLoad, "load is out of range %"
		B_PRId32 " (max of %" B_PRId32 " %" B_PRId32 ")", load,
		threadData->GetLoad(), fCore->GetLoad());

	if (load < kTargetLoad) {
		int32 delta = kTargetLoad - load;

		delta *= kTargetLoad;
		delta /= kCPUPerformanceScaleMax;

		decrease_cpu_performance(delta);
	} else {
		int32 delta = load - kTargetLoad;
		delta *= kMaxLoad - kTargetLoad;
		delta /= kCPUPerformanceScaleMax;

		increase_cpu_performance(delta);
	}
}


/**
 * @brief Timer callback that requests a reschedule on the current CPU.
 *
 * Sets invoke_scheduler and preempted flags on the current CPU's cpu_ent so
 * the scheduler runs at the next safe point.
 *
 * @return B_HANDLED_INTERRUPT always.
 */
/* static */ int32
CPUEntry::_RescheduleEvent(timer* /* unused */)
{
	get_cpu_struct()->invoke_scheduler = true;
	get_cpu_struct()->preempted = true;
	return B_HANDLED_INTERRUPT;
}


/**
 * @brief Timer callback that triggers a load measurement update.
 *
 * Called when an idle CPU's load-update timer fires. Calls ChangeLoad(0) on
 * the current core to refresh its load epoch and clears fUpdateLoadEvent on
 * the current CPU.
 *
 * @return B_HANDLED_INTERRUPT always.
 */
/* static */ int32
CPUEntry::_UpdateLoadEvent(timer* /* unused */)
{
	CoreEntry::GetCore(smp_get_current_cpu())->ChangeLoad(0);
	CPUEntry::GetCPU(smp_get_current_cpu())->fUpdateLoadEvent = false;
	return B_HANDLED_INTERRUPT;
}


/**
 * @brief Construct a CPUPriorityHeap sized for @p cpuCount entries.
 *
 * @param cpuCount Number of CPUs this heap must accommodate.
 */
CPUPriorityHeap::CPUPriorityHeap(int32 cpuCount)
	:
	Heap<CPUEntry, int32>(cpuCount)
{
}


/**
 * @brief Dump all entries in this CPU priority heap to the kernel debugger.
 *
 * Iterates by repeatedly removing the root, prints each CPU's id, priority
 * key, and load percentage, then restores the heap to its original state via
 * a temporary sDebugCPUHeap.
 */
void
CPUPriorityHeap::Dump()
{
	kprintf("cpu priority load\n");
	CPUEntry* entry = PeekRoot();
	while (entry) {
		int32 cpu = entry->ID();
		int32 key = GetKey(entry);
		kprintf("%3" B_PRId32 " %8" B_PRId32 " %3" B_PRId32 "%%\n", cpu, key,
			entry->GetLoad() / 10);

		RemoveRoot();
		sDebugCPUHeap.Insert(entry, key);

		entry = PeekRoot();
	}

	entry = sDebugCPUHeap.PeekRoot();
	while (entry) {
		int32 key = GetKey(entry);
		sDebugCPUHeap.RemoveRoot();
		Insert(entry, key);
		entry = sDebugCPUHeap.PeekRoot();
	}
}


/**
 * @brief Construct a CoreEntry with zeroed counters and initialized locks.
 */
CoreEntry::CoreEntry()
	:
	fCPUCount(0),
	fIdleCPUCount(0),
	fThreadCount(0),
	fActiveTime(0),
	fLoad(0),
	fCurrentLoad(0),
	fLoadMeasurementEpoch(0),
	fHighLoad(false),
	fLastLoadUpdate(0)
{
	B_INITIALIZE_SPINLOCK(&fCPULock);
	B_INITIALIZE_SPINLOCK(&fQueueLock);
	B_INITIALIZE_SEQLOCK(&fActiveTimeLock);
	B_INITIALIZE_RW_SPINLOCK(&fLoadLock);
}


/**
 * @brief Associate this CoreEntry with a core id and its parent package.
 *
 * @param id      Logical core index.
 * @param package Pointer to the PackageEntry that contains this core.
 */
void
CoreEntry::Init(int32 id, PackageEntry* package)
{
	fCoreID = id;
	fPackage = package;
}


/**
 * @brief Insert @p thread at the front of the core's shared run queue.
 *
 * Atomically increments fThreadCount after enqueueing.
 *
 * @param thread   ThreadData to enqueue.
 * @param priority Scheduling priority key for the queue.
 */
void
CoreEntry::PushFront(ThreadData* thread, int32 priority)
{
	SCHEDULER_ENTER_FUNCTION();

	fRunQueue.PushFront(thread, priority);
	atomic_add(&fThreadCount, 1);
}


/**
 * @brief Insert @p thread at the back of the core's shared run queue.
 *
 * Atomically increments fThreadCount after enqueueing.
 *
 * @param thread   ThreadData to enqueue.
 * @param priority Scheduling priority key for the queue.
 */
void
CoreEntry::PushBack(ThreadData* thread, int32 priority)
{
	SCHEDULER_ENTER_FUNCTION();

	fRunQueue.PushBack(thread, priority);
	atomic_add(&fThreadCount, 1);
}


/**
 * @brief Remove @p thread from the core's shared run queue.
 *
 * Asserts the thread is non-idle and currently enqueued. Clears the enqueued
 * flag and atomically decrements fThreadCount.
 *
 * @param thread ThreadData to remove; must be enqueued on this core.
 */
void
CoreEntry::Remove(ThreadData* thread)
{
	SCHEDULER_ENTER_FUNCTION();

	ASSERT(!thread->IsIdle());

	ASSERT(thread->IsEnqueued());
	thread->SetDequeued();

	fRunQueue.Remove(thread);
	atomic_add(&fThreadCount, -1);
}


/**
 * @brief Register a new logical CPU as active on this core.
 *
 * Increments the idle and total CPU counts. If this is the first CPU on the
 * core, reinitialises load tracking, inserts the core into gCoreLoadHeap, and
 * notifies the parent package. Also inserts @p cpu into the core's CPU heap at
 * B_IDLE_PRIORITY.
 *
 * @param cpu CPUEntry to add; must not already be active on this core.
 */
void
CoreEntry::AddCPU(CPUEntry* cpu)
{
	ASSERT(fCPUCount >= 0);
	ASSERT(fIdleCPUCount >= 0);

	fIdleCPUCount++;
	if (fCPUCount++ == 0) {
		// core has been reenabled
		fLoad = 0;
		fCurrentLoad = 0;
		fHighLoad = false;
		gCoreLoadHeap.Insert(this, 0);

		fPackage->AddIdleCore(this);
	}
	fCPUSet.SetBit(cpu->ID());

	fCPUHeap.Insert(cpu, B_IDLE_PRIORITY);
}


/**
 * @brief Unregister a logical CPU from this core, optionally disabling the
 *        core entirely.
 *
 * Decrements idle and total CPU counts and clears the CPU's bit in fCPUSet. If
 * this was the last CPU, unassigns all threads via thread_map(), removes the
 * core from whichever load heap it belongs to, notifies the package, and
 * drains the shared run queue by calling @p threadPostProcessing for each
 * remaining thread. Always removes @p cpu from the per-core CPU heap.
 *
 * @param cpu                  CPUEntry to remove; must be active on this core.
 * @param threadPostProcessing Callback invoked for each thread left in the
 *                             core's run queue when the last CPU goes away.
 */
void
CoreEntry::RemoveCPU(CPUEntry* cpu, ThreadProcessing& threadPostProcessing)
{
	ASSERT(fCPUCount > 0);
	ASSERT(fIdleCPUCount > 0);

	fIdleCPUCount--;
	fCPUSet.ClearBit(cpu->ID());
	if (--fCPUCount == 0) {
		// unassign threads
		thread_map(CoreEntry::_UnassignThread, this);

		// core has been disabled
		if (fHighLoad) {
			gCoreHighLoadHeap.ModifyKey(this, -1);
			ASSERT(gCoreHighLoadHeap.PeekMinimum() == this);
			gCoreHighLoadHeap.RemoveMinimum();
		} else {
			gCoreLoadHeap.ModifyKey(this, -1);
			ASSERT(gCoreLoadHeap.PeekMinimum() == this);
			gCoreLoadHeap.RemoveMinimum();
		}

		fPackage->RemoveIdleCore(this);

		// get rid of threads
		while (fRunQueue.PeekMaximum() != NULL) {
			ThreadData* threadData = fRunQueue.PeekMaximum();

			Remove(threadData);

			ASSERT(threadData->Core() == NULL);
			threadPostProcessing(threadData);
		}

		fThreadCount = 0;
	}

	fCPUHeap.ModifyKey(cpu, -1);
	ASSERT(fCPUHeap.PeekRoot() == cpu);
	fCPUHeap.RemoveRoot();

	ASSERT(cpu->GetLoad() >= 0 && cpu->GetLoad() <= kMaxLoad);
	ASSERT(fLoad >= 0);
}


/**
 * @brief Refresh this core's position in the global load heaps.
 *
 * Checks whether a load-measurement interval has elapsed. If so, commits the
 * current-interval load (fCurrentLoad) as the authoritative load (fLoad) and
 * advances fLoadMeasurementEpoch. Then moves the core between gCoreLoadHeap
 * and gCoreHighLoadHeap as necessary based on the kHighLoad / kMediumLoad
 * thresholds.
 *
 * @param forceUpdate When true, refreshes the heap key even if no full
 *                    measurement interval has elapsed.
 */
void
CoreEntry::_UpdateLoad(bool forceUpdate)
{
	SCHEDULER_ENTER_FUNCTION();

	if (fCPUCount <= 0)
		return;

	bigtime_t now = system_time();
	bool intervalEnded = now >= kLoadMeasureInterval + fLastLoadUpdate;
	bool intervalSkipped = now >= kLoadMeasureInterval * 2 + fLastLoadUpdate;

	if (!intervalEnded && !forceUpdate)
		return;

	WriteSpinLocker coreLocker(gCoreHeapsLock);

	int32 newKey;
	if (intervalEnded) {
		WriteSpinLocker locker(fLoadLock);

		newKey = intervalSkipped ? fCurrentLoad : GetLoad();

		ASSERT(fCurrentLoad >= 0);
		ASSERT(fLoad >= fCurrentLoad);

		fLoad = fCurrentLoad;
		fLoadMeasurementEpoch++;
		fLastLoadUpdate = now;
	} else
		newKey = GetLoad();

	int32 oldKey = CoreLoadHeap::GetKey(this);

	ASSERT(oldKey >= 0);
	ASSERT(newKey >= 0);

	if (oldKey == newKey)
		return;

	if (newKey > kHighLoad) {
		if (!fHighLoad) {
			gCoreLoadHeap.ModifyKey(this, -1);
			ASSERT(gCoreLoadHeap.PeekMinimum() == this);
			gCoreLoadHeap.RemoveMinimum();

			gCoreHighLoadHeap.Insert(this, newKey);

			fHighLoad = true;
		} else
			gCoreHighLoadHeap.ModifyKey(this, newKey);
	} else if (newKey < kMediumLoad) {
		if (fHighLoad) {
			gCoreHighLoadHeap.ModifyKey(this, -1);
			ASSERT(gCoreHighLoadHeap.PeekMinimum() == this);
			gCoreHighLoadHeap.RemoveMinimum();

			gCoreLoadHeap.Insert(this, newKey);

			fHighLoad = false;
		} else
			gCoreLoadHeap.ModifyKey(this, newKey);
	} else {
		if (fHighLoad)
			gCoreHighLoadHeap.ModifyKey(this, newKey);
		else
			gCoreLoadHeap.ModifyKey(this, newKey);
	}
}


/**
 * @brief thread_map() callback that clears the core assignment of unpinned
 *        threads belonging to a core that is going offline.
 *
 * @param thread Kernel Thread being examined.
 * @param data   Pointer to the CoreEntry that is being deactivated.
 */
/* static */ void
CoreEntry::_UnassignThread(Thread* thread, void* data)
{
	CoreEntry* core = static_cast<CoreEntry*>(data);
	ThreadData* threadData = thread->scheduler_data;

	if (threadData->Core() == core && thread->pinned_to_cpu == 0)
		threadData->UnassignCore();
}


/**
 * @brief Construct a CoreLoadHeap sized for @p coreCount entries.
 *
 * @param coreCount Number of cores this heap must accommodate.
 */
CoreLoadHeap::CoreLoadHeap(int32 coreCount)
	:
	MinMaxHeap<CoreEntry, int32>(coreCount)
{
}


/**
 * @brief Dump all entries in this core load heap to the kernel debugger.
 *
 * Iterates by repeatedly removing the minimum entry, delegates per-entry
 * formatting to DebugDumper::DumpCoreLoadHeapEntry(), then restores the heap
 * via a temporary sDebugCoreHeap.
 */
void
CoreLoadHeap::Dump()
{
	CoreEntry* entry = PeekMinimum();
	while (entry) {
		int32 key = GetKey(entry);

		DebugDumper::DumpCoreLoadHeapEntry(entry);

		RemoveMinimum();
		sDebugCoreHeap.Insert(entry, key);

		entry = PeekMinimum();
	}

	entry = sDebugCoreHeap.PeekMinimum();
	while (entry) {
		int32 key = GetKey(entry);
		sDebugCoreHeap.RemoveMinimum();
		Insert(entry, key);
		entry = sDebugCoreHeap.PeekMinimum();
	}
}


/**
 * @brief Construct a PackageEntry with zeroed core counts and an initialized
 *        lock.
 */
PackageEntry::PackageEntry()
	:
	fIdleCoreCount(0),
	fCoreCount(0)
{
	B_INITIALIZE_RW_SPINLOCK(&fCoreLock);
}


/**
 * @brief Associate this PackageEntry with a package id.
 *
 * @param id Logical package index.
 */
void
PackageEntry::Init(int32 id)
{
	fPackageID = id;
}


/**
 * @brief Register @p core as an idle core within this package.
 *
 * Increments both the total and idle core counts and appends @p core to the
 * fIdleCores list. If this is the first core in the package, adds the package
 * to gIdlePackageList.
 *
 * @param core CoreEntry to add; must belong to this package.
 */
void
PackageEntry::AddIdleCore(CoreEntry* core)
{
	fCoreCount++;
	fIdleCoreCount++;
	fIdleCores.Add(core);

	if (fCoreCount == 1)
		gIdlePackageList.Add(this);
}


/**
 * @brief Unregister @p core from the package's idle-core list.
 *
 * Removes @p core from fIdleCores and decrements both the idle and total core
 * counts. If the package has no more cores it is removed from
 * gIdlePackageList.
 *
 * @param core CoreEntry to remove; must currently be in this package's idle
 *             list.
 */
void
PackageEntry::RemoveIdleCore(CoreEntry* core)
{
	fIdleCores.Remove(core);
	fIdleCoreCount--;
	fCoreCount--;

	if (fCoreCount == 0)
		gIdlePackageList.Remove(this);
}


/**
 * @brief Dump the private run queue of @p cpu to the kernel debugger.
 *
 * Prints the queue only when it contains at least one non-idle thread.
 *
 * @param cpu CPUEntry whose run queue should be printed.
 */
/* static */ void
DebugDumper::DumpCPURunQueue(CPUEntry* cpu)
{
	ThreadRunQueue::ConstIterator iterator = cpu->fRunQueue.GetConstIterator();

	if (iterator.HasNext()
		&& !thread_is_idle_thread(iterator.Next()->GetThread())) {
		kprintf("\nCPU %" B_PRId32 " run queue:\n", cpu->ID());
		cpu->fRunQueue.Dump();
	}
}


/**
 * @brief Dump the shared run queue of @p core to the kernel debugger.
 *
 * @param core CoreEntry whose run queue should be printed.
 */
/* static */ void
DebugDumper::DumpCoreRunQueue(CoreEntry* core)
{
	core->fRunQueue.Dump();
}


/**
 * @brief Print a single core's load-heap entry line to the kernel debugger.
 *
 * Walks all threads via thread_map() to accumulate the sum of thread loads on
 * this core, then prints the core id, average load, current-interval load,
 * thread-aggregated load, thread count, and load-measurement epoch.
 *
 * @param entry CoreEntry to describe.
 */
/* static */ void
DebugDumper::DumpCoreLoadHeapEntry(CoreEntry* entry)
{
	CoreThreadsData threadsData;
	threadsData.fCore = entry;
	threadsData.fLoad = 0;
	thread_map(DebugDumper::_AnalyzeCoreThreads, &threadsData);

	kprintf("%4" B_PRId32 " %11" B_PRId32 "%% %11" B_PRId32 "%% %11" B_PRId32
		"%% %7" B_PRId32 " %5" B_PRIu32 "\n", entry->ID(), entry->fLoad / 10,
		entry->fCurrentLoad / 10, threadsData.fLoad, entry->ThreadCount(),
		entry->fLoadMeasurementEpoch);
}


/**
 * @brief Print the idle-cores list for @p package to the kernel debugger.
 *
 * Prints the package id followed by a comma-separated list of idle core ids,
 * or "-" if no idle cores are present.
 *
 * @param package PackageEntry whose idle cores should be listed.
 */
/* static */ void
DebugDumper::DumpIdleCoresInPackage(PackageEntry* package)
{
	kprintf("%-7" B_PRId32 " ", package->fPackageID);

	DoublyLinkedList<CoreEntry>::ReverseIterator iterator
		= package->fIdleCores.GetReverseIterator();
	if (iterator.HasNext()) {
		while (iterator.HasNext()) {
			CoreEntry* coreEntry = iterator.Next();
			kprintf("%" B_PRId32 "%s", coreEntry->ID(),
				iterator.HasNext() ? ", " : "");
		}
	} else
		kprintf("-");
	kprintf("\n");
}


/**
 * @brief thread_map() callback that accumulates per-core thread load totals.
 *
 * Adds the scheduler load of @p thread to the running total in
 * CoreThreadsData::fLoad when the thread's assigned core matches
 * CoreThreadsData::fCore.
 *
 * @param thread Kernel Thread being examined.
 * @param data   Pointer to a CoreThreadsData accumulator.
 */
/* static */ void
DebugDumper::_AnalyzeCoreThreads(Thread* thread, void* data)
{
	CoreThreadsData* threadsData = static_cast<CoreThreadsData*>(data);
	if (thread->scheduler_data->Core() == threadsData->fCore)
		threadsData->fLoad += thread->scheduler_data->GetLoad();
}


/**
 * @brief Kernel debugger command: list all core and CPU run queues.
 *
 * Iterates gCoreEntries[] and gCPUEntries[], printing each run queue in turn.
 *
 * @return 0 always.
 */
static int
dump_run_queue(int /* argc */, char** /* argv */)
{
	int32 cpuCount = smp_get_num_cpus();
	int32 coreCount = gCoreCount;

	for (int32 i = 0; i < coreCount; i++) {
		kprintf("%sCore %" B_PRId32 " run queue:\n", i > 0 ? "\n" : "", i);
		DebugDumper::DumpCoreRunQueue(&gCoreEntries[i]);
	}

	for (int32 i = 0; i < cpuCount; i++)
		DebugDumper::DumpCPURunQueue(&gCPUEntries[i]);

	return 0;
}


/**
 * @brief Kernel debugger command: dump the core load heaps and per-core CPU
 *        heaps.
 *
 * Prints the global gCoreLoadHeap and gCoreHighLoadHeap, then for each core
 * with more than one CPU prints its per-core CPUPriorityHeap.
 *
 * @return 0 always.
 */
static int
dump_cpu_heap(int /* argc */, char** /* argv */)
{
	kprintf("core average_load current_load threads_load threads epoch\n");
	gCoreLoadHeap.Dump();
	kprintf("\n");
	gCoreHighLoadHeap.Dump();

	for (int32 i = 0; i < gCoreCount; i++) {
		if (gCoreEntries[i].CPUCount() < 2)
			continue;

		kprintf("\nCore %" B_PRId32 " heap:\n", i);
		gCoreEntries[i].CPUHeap()->Dump();
	}

	return 0;
}


/**
 * @brief Kernel debugger command: list all packages that have idle cores.
 *
 * Walks gIdlePackageList in reverse order and for each package prints the set
 * of idle core ids via DebugDumper::DumpIdleCoresInPackage().
 *
 * @return 0 always.
 */
static int
dump_idle_cores(int /* argc */, char** /* argv */)
{
	kprintf("Idle packages:\n");
	IdlePackageList::ReverseIterator idleIterator
		= gIdlePackageList.GetReverseIterator();

	if (idleIterator.HasNext()) {
		kprintf("package cores\n");

		while (idleIterator.HasNext())
			DebugDumper::DumpIdleCoresInPackage(idleIterator.Next());
	} else
		kprintf("No idle packages.\n");

	return 0;
}


/**
 * @brief Register all scheduler-related kernel debugger commands.
 *
 * Initialises the debug-only scratch heaps (sDebugCPUHeap, sDebugCoreHeap)
 * and registers the "run_queue" command unconditionally. On multi-core
 * systems also registers "cpu_heap" and "idle_cores".
 */
void Scheduler::init_debug_commands()
{
	new(&sDebugCPUHeap) CPUPriorityHeap(smp_get_num_cpus());
	new(&sDebugCoreHeap) CoreLoadHeap(smp_get_num_cpus());

	add_debugger_command_etc("run_queue", &dump_run_queue,
		"List threads in run queue", "\nLists threads in run queue", 0);
	if (!gSingleCore) {
		add_debugger_command_etc("cpu_heap", &dump_cpu_heap,
			"List CPUs in CPU priority heap",
			"\nList CPUs in CPU priority heap", 0);
		add_debugger_command_etc("idle_cores", &dump_idle_cores,
			"List idle cores", "\nList idle cores", 0);
	}
}
