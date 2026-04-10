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
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file scheduler_thread.cpp
 * @brief Per-thread scheduler data and quantum length calculations.
 *
 * Manages the scheduler-specific state stored on each Thread object, including
 * quantum length tables indexed by priority. Used by both the low-latency and
 * power-saving scheduler modes.
 *
 * @see scheduler_cpu.cpp, scheduler.cpp
 */

#include "scheduler_thread.h"


using namespace Scheduler;


static bigtime_t sQuantumLengths[THREAD_MAX_SET_PRIORITY + 1];

const int32 kMaximumQuantumLengthsCount	= 20;
static bigtime_t sMaximumQuantumLengths[kMaximumQuantumLengthsCount];


/**
 * @brief Initialize base per-thread scheduler fields to their default values.
 *
 * Resets timing counters, penalty fields, enqueue flags, and computes the
 * initial effective priority and base quantum from the current quantum table.
 * Called by both Init() overloads before any core-specific setup.
 */
void
ThreadData::_InitBase()
{
	fStolenTime = 0;
	fQuantumStart = 0;
	fLastInterruptTime = 0;

	fWentSleep = 0;
	fWentSleepActive = 0;

	fEnqueued = false;
	fReady = false;

	fPriorityPenalty = 0;
	fAdditionalPenalty = 0;

	fEffectivePriority = GetPriority();
	fBaseQuantum = sQuantumLengths[GetEffectivePriority()];

	fTimeUsed = 0;

	fMeasureAvailableActiveTime = 0;
	fLastMeasureAvailableTime = 0;
	fMeasureAvailableTime = 0;
}


/**
 * @brief Select the best core for this thread to run on.
 *
 * Delegates to the current scheduler mode's choose_core() callback.
 * Must only be called in a multi-core configuration (asserts !gSingleCore).
 *
 * @return Pointer to the chosen CoreEntry.
 */
inline CoreEntry*
ThreadData::_ChooseCore() const
{
	SCHEDULER_ENTER_FUNCTION();

	ASSERT(!gSingleCore);
	return gCurrentMode->choose_core(this);
}


/**
 * @brief Select the best CPU within @p core for this thread.
 *
 * Tries to reuse the thread's previous CPU if it belongs to @p core and its
 * current priority is lower than the thread's effective priority. Otherwise
 * picks the lowest-priority CPU from the core heap. Sets @p rescheduleNeeded
 * to true when the chosen CPU needs to be preempted.
 *
 * @param core              The core whose CPUs are considered.
 * @param rescheduleNeeded  Set to true if the chosen CPU must reschedule.
 * @return Pointer to the chosen CPUEntry.
 */
inline CPUEntry*
ThreadData::_ChooseCPU(CoreEntry* core, bool& rescheduleNeeded) const
{
	SCHEDULER_ENTER_FUNCTION();

	int32 threadPriority = GetEffectivePriority();

	CPUSet mask = GetCPUMask();
	const bool useMask = !mask.IsEmpty();
	ASSERT(!useMask || mask.Matches(core->CPUMask()));

	if (fThread->previous_cpu != NULL && !fThread->previous_cpu->disabled
			&& (!useMask || mask.GetBit(fThread->previous_cpu->cpu_num))) {
		CPUEntry* previousCPU
			= CPUEntry::GetCPU(fThread->previous_cpu->cpu_num);
		if (previousCPU->Core() == core) {
			CoreCPUHeapLocker _(core);
			if (CPUPriorityHeap::GetKey(previousCPU) < threadPriority) {
				previousCPU->UpdatePriority(threadPriority);
				rescheduleNeeded = true;
				return previousCPU;
			}
		}
	}

	CoreCPUHeapLocker _(core);
	int32 index = 0;
	CPUEntry* cpu;
	do {
		cpu = core->CPUHeap()->PeekRoot(index++);
	} while (useMask && cpu != NULL && !mask.GetBit(cpu->ID()));
	ASSERT(cpu != NULL);

	if (CPUPriorityHeap::GetKey(cpu) < threadPriority) {
		cpu->UpdatePriority(threadPriority);
		rescheduleNeeded = true;
	} else
		rescheduleNeeded = false;

	return cpu;
}


/**
 * @brief Construct a ThreadData object bound to @p thread.
 *
 * @param thread The kernel Thread this object tracks scheduler state for.
 */
ThreadData::ThreadData(Thread* thread)
	:
	fThread(thread)
{
}


/**
 * @brief Initialize ThreadData for a newly created thread, inheriting load
 *        hints from the currently running thread.
 *
 * Calls _InitBase(), leaves fCore as NULL, and copies the needed-load estimate
 * and penalty values from the current thread so the new thread starts with
 * reasonable defaults.
 */
void
ThreadData::Init()
{
	_InitBase();
	fCore = NULL;

	Thread* currentThread = thread_get_current_thread();
	ThreadData* currentThreadData = currentThread->scheduler_data;
	fNeededLoad = currentThreadData->fNeededLoad;

	if (!IsRealTime()) {
		fPriorityPenalty = std::min(currentThreadData->fPriorityPenalty,
				std::max(GetPriority() - _GetMinimalPriority(), int32(0)));
		fAdditionalPenalty = currentThreadData->fAdditionalPenalty;

		_ComputeEffectivePriority();
	}
}


/**
 * @brief Initialize ThreadData for a thread that is pinned to a specific core.
 *
 * Calls _InitBase(), assigns @p core as the home core, marks the thread as
 * ready, and clears the needed-load estimate.
 *
 * @param core The CoreEntry the thread will be pinned to.
 */
void
ThreadData::Init(CoreEntry* core)
{
	_InitBase();

	fCore = core;
	fReady = true;
	fNeededLoad = 0;
}


/**
 * @brief Print scheduler state for this thread to the kernel debugger.
 *
 * Outputs priority penalties, effective priority, time used, quantum,
 * stolen time, load, sleep timestamps, and cache-affinity status via kprintf.
 */
void
ThreadData::Dump() const
{
	kprintf("\tpriority_penalty:\t%" B_PRId32 "\n", fPriorityPenalty);

	int32 priority = GetPriority() - _GetPenalty();
	priority = std::max(priority, int32(1));
	kprintf("\tadditional_penalty:\t%" B_PRId32 " (%" B_PRId32 ")\n",
		fAdditionalPenalty % priority, fAdditionalPenalty);
	kprintf("\teffective_priority:\t%" B_PRId32 "\n", GetEffectivePriority());

	kprintf("\ttime_used:\t\t%" B_PRId64 " us (quantum: %" B_PRId64 " us)\n",
		fTimeUsed, ComputeQuantum());
	kprintf("\tstolen_time:\t\t%" B_PRId64 " us\n", fStolenTime);
	kprintf("\tquantum_start:\t\t%" B_PRId64 " us\n", fQuantumStart);
	kprintf("\tneeded_load:\t\t%" B_PRId32 "%%\n", fNeededLoad / 10);
	kprintf("\twent_sleep:\t\t%" B_PRId64 "\n", fWentSleep);
	kprintf("\twent_sleep_active:\t%" B_PRId64 "\n", fWentSleepActive);
	kprintf("\tcore:\t\t\t%" B_PRId32 "\n",
		fCore != NULL ? fCore->ID() : -1);
	if (fCore != NULL && HasCacheExpired())
		kprintf("\tcache affinity has expired\n");
}


/**
 * @brief Resolve the final core and CPU assignment for this thread.
 *
 * Validates any CPU-affinity mask against caller-supplied hints, then fills in
 * whichever of @p targetCore / @p targetCPU is missing. Updates the thread's
 * fCore and adjusts load accounting if the thread is migrating between cores.
 *
 * @param targetCore  In/out: preferred core, or NULL to let the scheduler pick.
 * @param targetCPU   In/out: preferred CPU, or NULL to let the scheduler pick.
 * @return true if the chosen CPU needs to reschedule immediately.
 */
bool
ThreadData::ChooseCoreAndCPU(CoreEntry*& targetCore, CPUEntry*& targetCPU)
{
	SCHEDULER_ENTER_FUNCTION();

	bool rescheduleNeeded = false;

	CPUSet mask = GetCPUMask();
	const bool useMask = !mask.IsEmpty();

	if (targetCore != NULL && (useMask && !targetCore->CPUMask().Matches(mask)))
		targetCore = NULL;
	if (targetCPU != NULL && (useMask && !mask.GetBit(targetCPU->ID())))
		targetCPU = NULL;

	if (targetCore == NULL && targetCPU != NULL)
		targetCore = targetCPU->Core();
	else if (targetCore != NULL && targetCPU == NULL)
		targetCPU = _ChooseCPU(targetCore, rescheduleNeeded);
	else if (targetCore == NULL && targetCPU == NULL) {
		targetCore = _ChooseCore();
		ASSERT(!useMask || mask.Matches(targetCore->CPUMask()));
		targetCPU = _ChooseCPU(targetCore, rescheduleNeeded);
	}

	ASSERT(targetCore != NULL);
	ASSERT(targetCPU != NULL);

	if (fCore != targetCore) {
		fLoadMeasurementEpoch = targetCore->LoadMeasurementEpoch() - 1;
		if (fReady) {
			if (fCore != NULL)
				fCore->RemoveLoad(fNeededLoad, true);
			targetCore->AddLoad(fNeededLoad, fLoadMeasurementEpoch, true);
		}
	}

	fCore = targetCore;
	return rescheduleNeeded;
}


/**
 * @brief Compute the effective quantum length for this thread.
 *
 * For real-time threads the base quantum is returned unchanged. For normal
 * threads the quantum is capped by sMaximumQuantumLengths, which enforces a
 * scheduling latency bound based on the number of threads sharing the core.
 *
 * @return Quantum length in microseconds.
 */
bigtime_t
ThreadData::ComputeQuantum() const
{
	SCHEDULER_ENTER_FUNCTION();

	if (IsRealTime())
		return fBaseQuantum;

	int32 threadCount = fCore->ThreadCount();
	if (fCore->CPUCount() > 0)
		threadCount /= fCore->CPUCount();

	bigtime_t quantum = fBaseQuantum;
	if (threadCount < kMaximumQuantumLengthsCount)
		quantum = std::min(sMaximumQuantumLengths[threadCount], quantum);
	return quantum;
}


/**
 * @brief Detach this thread from its current core assignment.
 *
 * If @p running is true, or the thread is not in the B_THREAD_READY state,
 * fReady is cleared. If fReady ends up false, fCore is set to NULL so the
 * thread will be reassigned on its next enqueue.
 *
 * @param running true if the thread is currently executing (not merely ready).
 */
void
ThreadData::UnassignCore(bool running)
{
	SCHEDULER_ENTER_FUNCTION();

	ASSERT(fCore != NULL);
	if (running || fThread->state == B_THREAD_READY)
		fReady = false;
	if (!fReady)
		fCore = NULL;
}


/**
 * @brief Rebuild the global quantum-length lookup tables from the current
 *        scheduler mode parameters.
 *
 * Fills sQuantumLengths[] for every valid thread priority using a piecewise
 * linear interpolation between the mode's base quantum and its two multiplier
 * levels. Also fills sMaximumQuantumLengths[] which caps the quantum by the
 * required maximum scheduling latency divided by the number of runnable
 * threads.
 *
 * Must be called whenever gCurrentMode changes.
 */
/* static */ void
ThreadData::ComputeQuantumLengths()
{
	SCHEDULER_ENTER_FUNCTION();

	for (int32 priority = 0; priority <= THREAD_MAX_SET_PRIORITY; priority++) {
		const bigtime_t kQuantum0 = gCurrentMode->base_quantum;
		if (priority >= B_URGENT_DISPLAY_PRIORITY) {
			sQuantumLengths[priority] = kQuantum0;
			continue;
		}

		const bigtime_t kQuantum1
			= kQuantum0 * gCurrentMode->quantum_multipliers[0];
		if (priority > B_NORMAL_PRIORITY) {
			sQuantumLengths[priority] = _ScaleQuantum(kQuantum1, kQuantum0,
				B_URGENT_DISPLAY_PRIORITY, B_NORMAL_PRIORITY, priority);
			continue;
		}

		const bigtime_t kQuantum2
			= kQuantum0 * gCurrentMode->quantum_multipliers[1];
		sQuantumLengths[priority] = _ScaleQuantum(kQuantum2, kQuantum1,
			B_NORMAL_PRIORITY, B_IDLE_PRIORITY, priority);
	}

	for (int32 threadCount = 0; threadCount < kMaximumQuantumLengthsCount;
		threadCount++) {

		bigtime_t quantum = gCurrentMode->maximum_latency;
		if (threadCount != 0)
			quantum /= threadCount;
		quantum = std::max(quantum, gCurrentMode->minimal_quantum);
		sMaximumQuantumLengths[threadCount] = quantum;
	}
}


/**
 * @brief Return the combined priority penalty applied to this thread.
 *
 * @return Total priority penalty (fPriorityPenalty).
 */
inline int32
ThreadData::_GetPenalty() const
{
	SCHEDULER_ENTER_FUNCTION();
	return fPriorityPenalty;
}


/**
 * @brief Recompute fNeededLoad based on recent CPU usage measurements.
 *
 * Uses compute_load() to derive an updated load estimate. If the load has
 * changed, propagates the delta to the owning CoreEntry via ChangeLoad().
 * Must not be called for idle threads.
 */
void
ThreadData::_ComputeNeededLoad()
{
	SCHEDULER_ENTER_FUNCTION();
	ASSERT(!IsIdle());

	int32 oldLoad = compute_load(fLastMeasureAvailableTime,
		fMeasureAvailableActiveTime, fNeededLoad, fMeasureAvailableTime);
	if (oldLoad < 0 || oldLoad == fNeededLoad)
		return;

	fCore->ChangeLoad(fNeededLoad - oldLoad);
}


/**
 * @brief Recompute fEffectivePriority and fBaseQuantum from current penalties.
 *
 * Idle threads are clamped to B_IDLE_PRIORITY. Real-time threads keep their
 * nominal priority. Normal threads subtract the combined penalty and apply the
 * additional modular penalty, then look up the corresponding base quantum in
 * sQuantumLengths[].
 */
void
ThreadData::_ComputeEffectivePriority() const
{
	SCHEDULER_ENTER_FUNCTION();

	if (IsIdle())
		fEffectivePriority = B_IDLE_PRIORITY;
	else if (IsRealTime())
		fEffectivePriority = GetPriority();
	else {
		fEffectivePriority = GetPriority();
		fEffectivePriority -= _GetPenalty();
		if (fEffectivePriority > 0)
			fEffectivePriority -= fAdditionalPenalty % fEffectivePriority;

		ASSERT(fEffectivePriority < B_FIRST_REAL_TIME_PRIORITY);
		ASSERT(fEffectivePriority >= B_LOWEST_ACTIVE_PRIORITY);
	}

	fBaseQuantum = sQuantumLengths[GetEffectivePriority()];
}


/**
 * @brief Linearly interpolate a quantum length within a priority band.
 *
 * Maps @p priority from the range [@p minPriority, @p maxPriority] to a
 * quantum in the range [@p minQuantum, @p maxQuantum]. Higher priority yields
 * a shorter quantum.
 *
 * @param maxQuantum  Quantum assigned at @p minPriority (longest).
 * @param minQuantum  Quantum assigned at @p maxPriority (shortest).
 * @param maxPriority Upper bound of the priority band.
 * @param minPriority Lower bound of the priority band.
 * @param priority    The priority value to scale.
 * @return Interpolated quantum length in microseconds.
 */
/* static */ bigtime_t
ThreadData::_ScaleQuantum(bigtime_t maxQuantum, bigtime_t minQuantum,
	int32 maxPriority, int32 minPriority, int32 priority)
{
	SCHEDULER_ENTER_FUNCTION();

	ASSERT(priority <= maxPriority);
	ASSERT(priority >= minPriority);

	bigtime_t result = (maxQuantum - minQuantum) * (priority - minPriority);
	result /= maxPriority - minPriority;
	return maxQuantum - result;
}


/**
 * @brief Virtual destructor for ThreadProcessing.
 *
 * Ensures correct destruction of subclasses used as thread post-processing
 * callbacks during core removal.
 */
ThreadProcessing::~ThreadProcessing()
{
}
