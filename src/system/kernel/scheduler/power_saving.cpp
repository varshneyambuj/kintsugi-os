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
 * @file power_saving.cpp
 * @brief Power-saving scheduler mode: pack work and IRQs onto few cores.
 *
 * Implements the scheduler_mode_operations vtable gSchedulerPowerSavingMode.
 * The strategy favours keeping as many cores idle as possible by funnelling
 * small tasks (and their IRQs) onto a single "small-task core" and only
 * spreading work when the chosen core becomes overloaded.
 *
 * @see scheduler_modes.h, low_latency.cpp
 */


#include <util/atomic.h>
#include <util/AutoLock.h>

#include "scheduler_common.h"
#include "scheduler_cpu.h"
#include "scheduler_modes.h"
#include "scheduler_profiler.h"
#include "scheduler_thread.h"


using namespace Scheduler;


const bigtime_t kCacheExpire = 100000;

static CoreEntry* sSmallTaskCore;


/**
 * @brief Reset mode state when the scheduler switches into power saving.
 *
 * Clears the cached small-task core so the next placement decision picks
 * a fresh victim based on current load.
 */
static void
switch_to_mode()
{
	sSmallTaskCore = NULL;
}


/**
 * @brief React to a CPU being enabled or disabled at runtime.
 *
 * Clearing sSmallTaskCore on disable forces re-election next time, since the
 * previously chosen core may no longer be usable.
 *
 * @param cpu     CPU index whose state changed.
 * @param enabled true if the CPU is being enabled, false if disabled.
 */
static void
set_cpu_enabled(int32 cpu, bool enabled)
{
	if (!enabled)
		sSmallTaskCore = NULL;
}


/**
 * @brief Return true if the thread slept long enough to lose cache affinity.
 *
 * Used by the scheduler to decide whether to rebalance a waking thread off
 * its previous core.
 *
 * @param threadData Scheduler bookkeeping for the thread being woken.
 * @return true if the thread has been asleep longer than kCacheExpire.
 */
static bool
has_cache_expired(const ThreadData* threadData)
{
	SCHEDULER_ENTER_FUNCTION();
	if (threadData->WentSleep() == 0)
		return false;
	return system_time() - threadData->WentSleep() > kCacheExpire;
}


/**
 * @brief Elect (or return) the single core used to host small tasks.
 *
 * Picks the busiest non-idle core from gCoreLoadHeap and installs it as
 * sSmallTaskCore if no other thread raced ahead. Subsequent calls return
 * whichever core first won the race.
 *
 * @return Small-task core, or NULL only if no eligible cores exist.
 */
static CoreEntry*
choose_small_task_core()
{
	SCHEDULER_ENTER_FUNCTION();

	ReadSpinLocker coreLocker(gCoreHeapsLock);
	CoreEntry* core = gCoreLoadHeap.PeekMaximum();
	if (core == NULL)
		return sSmallTaskCore;

	CoreEntry* smallTaskCore
		= atomic_pointer_test_and_set(&sSmallTaskCore, core, (CoreEntry*)NULL);
	if (smallTaskCore == NULL)
		return core;
	return smallTaskCore;
}


/**
 * @brief Pick an idle core, preferring packages that already have activity.
 *
 * Returning an idle core on a partially awake package keeps fully idle
 * packages in deep sleep for longer.
 *
 * @return Idle core to wake, or NULL if none is available.
 */
static CoreEntry*
choose_idle_core()
{
	SCHEDULER_ENTER_FUNCTION();

	PackageEntry* package = PackageEntry::GetLeastIdlePackage();

	if (package == NULL)
		package = gIdlePackageList.Last();

	if (package != NULL)
		return package->GetIdleCore();
	return NULL;
}


/**
 * @brief Choose the destination core for a newly runnable thread.
 *
 * First tries to pile onto the small-task core, falling back to the least
 * loaded awake core, then to waking an idle core, then to the least loaded
 * highly loaded core. Honours the thread's CPU affinity mask.
 *
 * @param threadData Scheduler bookkeeping for the thread being placed.
 * @return Destination core (never NULL).
 */
static CoreEntry*
choose_core(const ThreadData* threadData)
{
	SCHEDULER_ENTER_FUNCTION();

	CoreEntry* core = NULL;

	CPUSet mask = threadData->GetCPUMask();
	const bool useMask = !mask.IsEmpty();

	// try to pack all threads on one core
	core = choose_small_task_core();
	if (core != NULL && (useMask && !core->CPUMask().Matches(mask)))
		core = NULL;

	if (core == NULL || core->GetLoad() + threadData->GetLoad() >= kHighLoad) {
		ReadSpinLocker coreLocker(gCoreHeapsLock);

		// run immediately on already woken core
		int32 index = 0;
		do {
			core = gCoreLoadHeap.PeekMinimum(index++);
		} while (useMask && core != NULL && !core->CPUMask().Matches(mask));
		if (core == NULL) {
			coreLocker.Unlock();

			core = choose_idle_core();
			if (useMask && !core->CPUMask().Matches(mask))
				core = NULL;

			if (core == NULL) {
				coreLocker.Lock();
				index = 0;
				do {
					core = gCoreHighLoadHeap.PeekMinimum(index++);
				} while (useMask && core != NULL && !core->CPUMask().Matches(mask));
			}
		}
	}

	ASSERT(core != NULL);
	return core;
}


/**
 * @brief Decide whether to migrate a running thread to a different core.
 *
 * When the current core is hot and the thread is the small-task core's
 * main contributor, consider stealing back the small-task core or moving
 * to the least loaded alternative. When the current core is cool enough,
 * try to consolidate the thread onto the small-task core instead.
 *
 * @param threadData Scheduler bookkeeping for the thread being rebalanced.
 * @return Chosen destination core (may be the current core).
 */
static CoreEntry*
rebalance(const ThreadData* threadData)
{
	SCHEDULER_ENTER_FUNCTION();

	ASSERT(!gSingleCore);

	CPUSet mask = threadData->GetCPUMask();
	const bool useMask = !mask.IsEmpty();

	CoreEntry* core = threadData->Core();

	int32 coreLoad = core->GetLoad();
	int32 threadLoad = threadData->GetLoad() / core->CPUCount();
	if (coreLoad > kHighLoad) {
		if (sSmallTaskCore == core) {
			sSmallTaskCore = NULL;
			CoreEntry* smallTaskCore = choose_small_task_core();

			if (threadLoad > coreLoad / 3 || smallTaskCore == NULL
					|| (useMask && !smallTaskCore->CPUMask().Matches(mask))) {
				return core;
			}
			return coreLoad > kVeryHighLoad ? smallTaskCore : core;
		}

		if (threadLoad >= coreLoad / 2)
			return core;

		ReadSpinLocker coreLocker(gCoreHeapsLock);
		CoreEntry* other;
		int32 index = 0;
		do {
			other = gCoreLoadHeap.PeekMaximum(index++);
		} while (useMask && other != NULL && !other->CPUMask().Matches(mask));
		if (other == NULL) {
			index = 0;
			do {
				other = gCoreHighLoadHeap.PeekMinimum(index++);
			} while (useMask && other != NULL && !other->CPUMask().Matches(mask));
		}
		coreLocker.Unlock();
		ASSERT(other != NULL);

		int32 coreNewLoad = coreLoad - threadLoad;
		int32 otherNewLoad = other->GetLoad() + threadLoad;
		return coreNewLoad - otherNewLoad >= kLoadDifference / 2 ? other : core;
	}

	if (coreLoad >= kMediumLoad)
		return core;

	CoreEntry* smallTaskCore = choose_small_task_core();
	if (smallTaskCore == NULL || (useMask && !smallTaskCore->CPUMask().Matches(mask)))
		return core;
	return smallTaskCore->GetLoad() + threadLoad < kHighLoad
		? smallTaskCore : core;
}


/**
 * @brief Move all IRQs off this CPU onto the small-task core.
 *
 * Walks the current CPU's IRQ list and reassigns each to the least loaded
 * CPU of sSmallTaskCore. Intended to be called from the idle path to let
 * non-small-task cores enter deeper sleep states.
 */
static inline void
pack_irqs()
{
	SCHEDULER_ENTER_FUNCTION();

	CoreEntry* smallTaskCore = atomic_pointer_get(&sSmallTaskCore);
	if (smallTaskCore == NULL)
		return;

	cpu_ent* cpu = get_cpu_struct();
	if (smallTaskCore == CoreEntry::GetCore(cpu->cpu_num))
		return;

	SpinLocker locker(cpu->irqs_lock);
	while (list_get_first_item(&cpu->irqs) != NULL) {
		irq_assignment* irq = (irq_assignment*)list_get_first_item(&cpu->irqs);
		locker.Unlock();

		int32 newCPU = smallTaskCore->CPUHeap()->PeekRoot()->ID();

		if (newCPU != cpu->cpu_num)
			assign_io_interrupt_to_cpu(irq->irq, newCPU);

		locker.Lock();
	}
}


/**
 * @brief Rebalance this CPU's IRQs according to the power-saving policy.
 *
 * When going idle and there is a small-task core elected, pack_irqs() moves
 * IRQs there. Otherwise — only when no small-task core exists — pick the
 * heaviest IRQ on this CPU and consider migrating it to a noticeably less
 * loaded core.
 *
 * @param idle true when called from the idle path.
 */
static void
rebalance_irqs(bool idle)
{
	SCHEDULER_ENTER_FUNCTION();

	if (idle && sSmallTaskCore != NULL) {
		pack_irqs();
		return;
	}

	if (idle || sSmallTaskCore != NULL)
		return;

	cpu_ent* cpu = get_cpu_struct();
	SpinLocker locker(cpu->irqs_lock);

	irq_assignment* chosen = NULL;
	irq_assignment* irq = (irq_assignment*)list_get_first_item(&cpu->irqs);

	while (irq != NULL) {
		if (chosen == NULL || chosen->load < irq->load)
			chosen = irq;
		irq = (irq_assignment*)list_get_next_item(&cpu->irqs, irq);
	}

	locker.Unlock();

	if (chosen == NULL || chosen->load < kLowLoad)
		return;

	ReadSpinLocker coreLocker(gCoreHeapsLock);
	CoreEntry* other = gCoreLoadHeap.PeekMinimum();
	coreLocker.Unlock();
	if (other == NULL)
		return;
	int32 newCPU = other->CPUHeap()->PeekRoot()->ID();

	CoreEntry* core = CoreEntry::GetCore(smp_get_current_cpu());
	if (other == core)
		return;
	if (other->GetLoad() + kLoadDifference >= core->GetLoad())
		return;

	assign_io_interrupt_to_cpu(chosen->irq, newCPU);
}


scheduler_mode_operations gSchedulerPowerSavingMode = {
	"power saving",

	2000,
	500,
	{ 3, 10 },

	20000,

	switch_to_mode,
	set_cpu_enabled,
	has_cache_expired,
	choose_core,
	rebalance,
	rebalance_irqs,
};

