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
 *   Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file cpu.cpp
 * @brief CPU initialization and per-CPU data management.
 *
 * Handles early CPU initialization (cpu_init, cpu_init_post_vm, cpu_preboot_init),
 * topology discovery, and per-CPU topology information queries.
 */


#include <cpu.h>
#include <arch/cpu.h>
#include <arch/system_info.h>

#include <string.h>

#include <cpufreq.h>
#include <cpuidle.h>

#include <boot/kernel_args.h>
#include <kscheduler.h>
#include <thread_types.h>
#include <util/AutoLock.h>
#include <util/ThreadAutoLock.h>


/* global per-cpu structure */
cpu_ent gCPU[SMP_MAX_CPUS];
CPUSet gCPUEnabled;

uint32 gCPUCacheLevelCount;
static cpu_topology_node sCPUTopology;

static cpufreq_module_info* sCPUPerformanceModule;
static cpuidle_module_info* sCPUIdleModule;

static spinlock sSetCpuLock;


/**
 * @brief Perform early architecture-level CPU initialization.
 *
 * Delegates to arch_cpu_init() to carry out any platform-specific CPU setup
 * that must happen before the VM is available.
 *
 * @param args Pointer to the boot kernel_args structure.
 * @return B_OK on success, or an error code on failure.
 * @retval B_OK Architecture CPU initialization succeeded.
 */
status_t
cpu_init(kernel_args *args)
{
	return arch_cpu_init(args);
}


/**
 * @brief Perform per-CPU architecture initialization for a single processor.
 *
 * Called for each logical CPU after cpu_init(). Delegates to
 * arch_cpu_init_percpu() which sets up CPU-local structures such as the GDT,
 * TSS, and MSRs on x86.
 *
 * @param args     Pointer to the boot kernel_args structure.
 * @param curr_cpu Logical index of the CPU being initialized.
 * @return B_OK on success, or an error code on failure.
 * @retval B_OK Per-CPU architecture initialization succeeded.
 */
status_t
cpu_init_percpu(kernel_args *args, int curr_cpu)
{
	return arch_cpu_init_percpu(args, curr_cpu);
}


/**
 * @brief Perform CPU initialization steps that require the VM to be available.
 *
 * Called after the virtual memory subsystem is initialized. Delegates to
 * arch_cpu_init_post_vm() which may, for example, map per-CPU memory regions
 * or finalize cache-related configuration.
 *
 * @param args Pointer to the boot kernel_args structure.
 * @return B_OK on success, or an error code on failure.
 * @retval B_OK Post-VM CPU initialization succeeded.
 */
status_t
cpu_init_post_vm(kernel_args *args)
{
	return arch_cpu_init_post_vm(args);
}


/**
 * @brief Scan and load the highest-ranked cpufreq module available.
 *
 * Iterates over all modules under the CPUFREQ_MODULES_PREFIX namespace,
 * selects the one with the highest rank field, and stores a reference in
 * sCPUPerformanceModule. After a module is selected the scheduler policy is
 * updated to reflect the new frequency-scaling capability.
 */
static void
load_cpufreq_module()
{
	void* cookie = open_module_list(CPUFREQ_MODULES_PREFIX);

	while (true) {
		char name[B_FILE_NAME_LENGTH];
		size_t nameLength = sizeof(name);
		cpufreq_module_info* current = NULL;

		if (read_next_module_name(cookie, name, &nameLength) != B_OK)
			break;

		if (get_module(name, (module_info**)&current) == B_OK) {
			dprintf("found cpufreq module: %s\n", name);

			if (sCPUPerformanceModule != NULL) {
				if (sCPUPerformanceModule->rank < current->rank) {
					put_module(sCPUPerformanceModule->info.name);
					sCPUPerformanceModule = current;
				} else
					put_module(name);
			} else
				sCPUPerformanceModule = current;
		}
	}

	close_module_list(cookie);

	if (sCPUPerformanceModule == NULL)
		dprintf("no valid cpufreq module found\n");
	else
		scheduler_update_policy();
}


/**
 * @brief Scan and load the highest-ranked cpuidle module available.
 *
 * Iterates over all modules under the CPUIDLE_MODULES_PREFIX namespace and
 * selects the one with the highest rank field, storing a reference in
 * sCPUIdleModule. The selected module is used by cpu_idle() and cpu_wait()
 * to implement platform-optimized idle and pause behavior.
 */
static void
load_cpuidle_module()
{
	void* cookie = open_module_list(CPUIDLE_MODULES_PREFIX);

	while (true) {
		char name[B_FILE_NAME_LENGTH];
		size_t nameLength = sizeof(name);
		cpuidle_module_info* current = NULL;

		if (read_next_module_name(cookie, name, &nameLength) != B_OK)
			break;

		if (get_module(name, (module_info**)&current) == B_OK) {
			dprintf("found cpuidle module: %s\n", name);

			if (sCPUIdleModule != NULL) {
				if (sCPUIdleModule->rank < current->rank) {
					put_module(sCPUIdleModule->info.name);
					sCPUIdleModule = current;
				} else
					put_module(name);
			} else
				sCPUIdleModule = current;
		}
	}

	close_module_list(cookie);

	if (sCPUIdleModule == NULL)
		dprintf("no valid cpuidle module found\n");
}


/**
 * @brief Initialize CPU subsystems that depend on loadable modules.
 *
 * Called once the module infrastructure is fully operational. Runs the
 * architecture-level post-modules hook, then discovers and loads the best
 * available cpufreq and cpuidle modules.
 *
 * @param args Pointer to the boot kernel_args structure.
 * @return B_OK on success, or the first error code encountered.
 * @retval B_OK All post-module CPU initialization succeeded.
 */
status_t
cpu_init_post_modules(kernel_args *args)
{
	status_t result = arch_cpu_init_post_modules(args);
	if (result != B_OK)
		return result;

	load_cpufreq_module();
	load_cpuidle_module();
	return B_OK;
}


/**
 * @brief Perform the earliest per-CPU initialization before the VM is set up.
 *
 * Zeroes the gCPU entry for the given CPU, records its logical number, marks
 * it as enabled in gCPUEnabled, and initializes the IRQ list and its spinlock.
 * Finally delegates to arch_cpu_preboot_init_percpu() for any platform-specific
 * very-early setup (e.g., setting up the %gs base on x86 so that
 * get_current_cpu() works).
 *
 * @param args     Pointer to the boot kernel_args structure.
 * @param curr_cpu Logical index of the CPU being initialized.
 * @return B_OK on success, or an error code on failure.
 * @retval B_OK Pre-boot per-CPU initialization succeeded.
 */
status_t
cpu_preboot_init_percpu(kernel_args *args, int curr_cpu)
{
	// set the cpu number in the local cpu structure so that
	// we can use it for get_current_cpu
	memset(&gCPU[curr_cpu], 0, sizeof(gCPU[curr_cpu]));
	gCPU[curr_cpu].cpu_num = curr_cpu;
	gCPUEnabled.SetBitAtomic(curr_cpu);

	list_init(&gCPU[curr_cpu].irqs);
	B_INITIALIZE_SPINLOCK(&gCPU[curr_cpu].irqs_lock);

	return arch_cpu_preboot_init_percpu(args, curr_cpu);
}


/**
 * @brief Return the total time a CPU has spent executing non-idle threads.
 *
 * Reads the active_time field of the specified CPU's gCPU entry using a
 * seqlock to guarantee a consistent snapshot without taking a full spinlock.
 *
 * @param cpu Logical CPU index (0-based).
 * @return Accumulated active time in microseconds, or 0 if @p cpu is out of
 *         range.
 */
bigtime_t
cpu_get_active_time(int32 cpu)
{
	if (cpu < 0 || cpu > smp_get_num_cpus())
		return 0;

	bigtime_t activeTime;
	uint32 count;

	do {
		count = acquire_read_seqlock(&gCPU[cpu].active_time_lock);
		activeTime = gCPU[cpu].active_time;
	} while (!release_read_seqlock(&gCPU[cpu].active_time_lock, count));

	return activeTime;
}


/**
 * @brief Query the current operating frequency of a logical CPU.
 *
 * Delegates to arch_get_frequency() which reads the platform-specific
 * frequency counter or MSR for the requested CPU.
 *
 * @param cpu Logical CPU index (0-based).
 * @return Current CPU frequency in Hz, or 0 if @p cpu is out of range or the
 *         frequency cannot be determined.
 */
uint64
cpu_frequency(int32 cpu)
{
	if (cpu < 0 || cpu >= smp_get_num_cpus())
		return 0;
	uint64 frequency = 0;
	arch_get_frequency(&frequency, cpu);
	return frequency;
}


/**
 * @brief Synchronize or invalidate CPU caches for a given memory region.
 *
 * Currently only implements I-cache invalidation via arch_cpu_sync_icache()
 * when B_INVALIDATE_ICACHE is set in @p flags. D-cache handling is a known
 * TODO.
 *
 * @param address Base address of the memory region to operate on.
 * @param length  Size in bytes of the region.
 * @param flags   Combination of cache-operation flags (e.g., B_INVALIDATE_ICACHE).
 */
void
clear_caches(void *address, size_t length, uint32 flags)
{
	// TODO: data cache
	if ((B_INVALIDATE_ICACHE & flags) != 0) {
		arch_cpu_sync_icache(address, length);
	}
}


/**
 * @brief Allocate and link a new child node into the CPU topology tree.
 *
 * Creates a cpu_topology_node at the level one below @p node, attaches it at
 * slot @p id in @p node->children, and allocates the child's own children
 * array (unless the new node is at the SMT leaf level).
 *
 * @param node   Parent topology node to which the new child is added.
 * @param maxID  Array of per-level maximum IDs used to size children arrays.
 * @param id     Slot index within @p node->children for the new child.
 * @return B_OK on success, B_NO_MEMORY if a heap allocation fails.
 * @retval B_OK      Node created and linked successfully.
 * @retval B_NO_MEMORY Insufficient heap memory for the new node or its children array.
 */
static status_t
cpu_create_topology_node(cpu_topology_node* node, int32* maxID, int32 id)
{
	cpu_topology_level level = static_cast<cpu_topology_level>(node->level - 1);
	ASSERT(level >= 0);

	cpu_topology_node* newNode = new(std::nothrow) cpu_topology_node;
	if (newNode == NULL)
		return B_NO_MEMORY;
	node->children[id] = newNode;

	newNode->level = level;
	if (level != CPU_TOPOLOGY_SMT) {
		newNode->children_count = maxID[level - 1];
		newNode->children
			= new(std::nothrow) cpu_topology_node*[maxID[level - 1]];
		if (newNode->children == NULL)
			return B_NO_MEMORY;

		memset(newNode->children, 0,
			maxID[level - 1] * sizeof(cpu_topology_node*));
	} else {
		newNode->children_count = 0;
		newNode->children = NULL;
	}

	return B_OK;
}


/**
 * @brief Compact and re-sequence IDs in a CPU topology subtree.
 *
 * Removes NULL child slots left by cpu_build_topology_tree() after sparse
 * insertion, packs live children to the front of the children array, and
 * assigns monotonically increasing IDs at each non-SMT level using @p lastID
 * counters. Recurses depth-first through the tree.
 *
 * @param node   Root of the subtree to rebuild.
 * @param lastID Array of per-level ID counters; updated in place as IDs are
 *               assigned.
 */
static void
cpu_rebuild_topology_tree(cpu_topology_node* node, int32* lastID)
{
	if (node->children == NULL)
		return;

	int32 count = 0;
	for (int32 i = 0; i < node->children_count; i++) {
		if (node->children[i] == NULL)
			continue;

		if (count != i)
			node->children[count] = node->children[i];

		if (node->children[count]->level != CPU_TOPOLOGY_SMT)
			node->children[count]->id = lastID[node->children[count]->level]++;

		cpu_rebuild_topology_tree(node->children[count], lastID);
		count++;
	}
	node->children_count = count;
}


/**
 * @brief Build the system-wide CPU topology tree from per-CPU topology IDs.
 *
 * Scans all logical CPUs, determines the maximum ID at each topology level,
 * allocates the tree nodes top-down, and then compacts the tree with
 * cpu_rebuild_topology_tree(). The resulting tree is stored in sCPUTopology
 * and is accessible via get_cpu_topology().
 *
 * @return B_OK on success, B_NO_MEMORY if any node allocation fails.
 * @retval B_OK     Topology tree built successfully.
 * @retval B_NO_MEMORY Heap allocation failed during tree construction.
 */
status_t
cpu_build_topology_tree(void)
{
	sCPUTopology.level = CPU_TOPOLOGY_LEVELS;

	int32 maxID[CPU_TOPOLOGY_LEVELS];
	memset(&maxID, 0, sizeof(maxID));

	const int32 kCPUCount = smp_get_num_cpus();
	for (int32 i = 0; i < kCPUCount; i++) {
		for (int32 j = 0; j < CPU_TOPOLOGY_LEVELS; j++)
			maxID[j] = max_c(maxID[j], gCPU[i].topology_id[j]);
	}

	for (int32 j = 0; j < CPU_TOPOLOGY_LEVELS; j++)
		maxID[j]++;

	sCPUTopology.children_count = maxID[CPU_TOPOLOGY_LEVELS - 1];
	sCPUTopology.children
		= new(std::nothrow) cpu_topology_node*[maxID[CPU_TOPOLOGY_LEVELS - 1]];
	if (sCPUTopology.children == NULL)
		return B_NO_MEMORY;
	memset(sCPUTopology.children, 0,
		maxID[CPU_TOPOLOGY_LEVELS - 1] * sizeof(cpu_topology_node*));

	for (int32 i = 0; i < kCPUCount; i++) {
		cpu_topology_node* node = &sCPUTopology;
		for (int32 j = CPU_TOPOLOGY_LEVELS - 1; j >= 0; j--) {
			int32 id = gCPU[i].topology_id[j];
			if (node->children[id] == NULL) {
				status_t result = cpu_create_topology_node(node, maxID, id);
				if (result != B_OK)
					return result;
			}

			node = node->children[id];
		}

		ASSERT(node->level == CPU_TOPOLOGY_SMT);
		node->id = i;
	}

	int32 lastID[CPU_TOPOLOGY_LEVELS];
	memset(&lastID, 0, sizeof(lastID));
	cpu_rebuild_topology_tree(&sCPUTopology, lastID);

	return B_OK;
}


/**
 * @brief Return a read-only pointer to the root of the CPU topology tree.
 *
 * The tree is valid after cpu_build_topology_tree() has returned B_OK.
 *
 * @return Pointer to the root cpu_topology_node of sCPUTopology.
 */
const cpu_topology_node*
get_cpu_topology(void)
{
	return &sCPUTopology;
}


/**
 * @brief Notify cpufreq and cpuidle modules of a scheduler mode change.
 *
 * Propagates the new scheduler operating mode (e.g., low-latency vs.
 * power-saving) to whichever performance and idle modules are currently loaded,
 * allowing them to adjust frequency and C-state policies accordingly.
 *
 * @param mode New scheduler mode to apply.
 */
void
cpu_set_scheduler_mode(enum scheduler_mode mode)
{
	if (sCPUPerformanceModule != NULL)
		sCPUPerformanceModule->cpufreq_set_scheduler_mode(mode);
	if (sCPUIdleModule != NULL)
		sCPUIdleModule->cpuidle_set_scheduler_mode(mode);
}


/**
 * @brief Request an increase in CPU performance level.
 *
 * Forwards the request to the active cpufreq module. If no cpufreq module is
 * loaded the call is a no-op and B_NOT_SUPPORTED is returned.
 *
 * @param delta Magnitude of the desired performance increase (module-defined units).
 * @return B_OK on success, B_NOT_SUPPORTED if no cpufreq module is available,
 *         or another error code from the module.
 * @retval B_OK            Performance level increased successfully.
 * @retval B_NOT_SUPPORTED No cpufreq module is loaded.
 */
status_t
increase_cpu_performance(int delta)
{
	if (sCPUPerformanceModule != NULL)
		return sCPUPerformanceModule->cpufreq_increase_performance(delta);
	return B_NOT_SUPPORTED;
}


/**
 * @brief Request a decrease in CPU performance level.
 *
 * Forwards the request to the active cpufreq module. If no cpufreq module is
 * loaded the call is a no-op and B_NOT_SUPPORTED is returned.
 *
 * @param delta Magnitude of the desired performance decrease (module-defined units).
 * @return B_OK on success, B_NOT_SUPPORTED if no cpufreq module is available,
 *         or another error code from the module.
 * @retval B_OK            Performance level decreased successfully.
 * @retval B_NOT_SUPPORTED No cpufreq module is loaded.
 */
status_t
decrease_cpu_performance(int delta)
{
	if (sCPUPerformanceModule != NULL)
		return sCPUPerformanceModule->cpufreq_decrease_performance(delta);
	return B_NOT_SUPPORTED;
}


/**
 * @brief Halt the current CPU until the next interrupt.
 *
 * Called from the idle thread. Uses the cpuidle module's idle entry-point when
 * one is available so that the hardware can enter a deep sleep state; otherwise
 * falls back to arch_cpu_idle() (typically a plain HLT or WFI instruction).
 * Panics in debug builds if called with interrupts disabled, which would cause
 * the CPU to sleep forever.
 */
void
cpu_idle(void)
{
#if KDEBUG
	if (!are_interrupts_enabled())
		panic("cpu_idle() called with interrupts disabled.");
#endif

	if (sCPUIdleModule != NULL)
		sCPUIdleModule->cpuidle_idle();
	else
		arch_cpu_idle();
}


/**
 * @brief Spin-wait efficiently until a variable reaches a target value.
 *
 * Delegates to the cpuidle module's wait implementation when available, which
 * may use MWAIT or a similar instruction to reduce power consumption and bus
 * traffic during the spin. Falls back to arch_cpu_pause() (e.g., PAUSE on x86)
 * when no cpuidle module is loaded.
 *
 * @param variable Pointer to the value to poll.
 * @param test     Value to wait for.
 */
void
cpu_wait(int32* variable, int32 test)
{
	if (sCPUIdleModule != NULL)
		sCPUIdleModule->cpuidle_wait(variable, test);
	else
		arch_cpu_pause();
}


//	#pragma mark -


/**
 * @brief Syscall wrapper: synchronize or invalidate CPU caches for a user-space region.
 *
 * @param address User-space base address of the region.
 * @param length  Size in bytes of the region.
 * @param flags   Cache-operation flags (e.g., B_INVALIDATE_ICACHE).
 */
void
_user_clear_caches(void *address, size_t length, uint32 flags)
{
	clear_caches(address, length, flags);
}


/**
 * @brief Syscall: query whether a logical CPU is currently enabled.
 *
 * @param cpu Logical CPU index (0-based).
 * @return true if the CPU exists and is not disabled, false otherwise.
 */
bool
_user_cpu_enabled(int32 cpu)
{
	if (cpu < 0 || cpu >= smp_get_num_cpus())
		return false;

	return !gCPU[cpu].disabled;
}


/**
 * @brief Syscall: enable or disable a logical CPU at runtime.
 *
 * Requires root privileges. Prevents disabling the last remaining active CPU.
 * When disabling, yields the current thread until the target CPU is running
 * only its idle thread before marking it disabled in the scheduler.
 *
 * @param cpu     Logical CPU index (0-based) to enable or disable.
 * @param enabled Pass true to enable the CPU, false to disable it.
 * @return Status code indicating the outcome.
 * @retval B_OK             Operation completed successfully.
 * @retval B_PERMISSION_DENIED Caller is not root.
 * @retval B_BAD_VALUE      @p cpu is out of range.
 * @retval B_NOT_ALLOWED    Attempt to disable the last active CPU.
 */
status_t
_user_set_cpu_enabled(int32 cpu, bool enabled)
{
	int32 i, count;

	if (geteuid() != 0)
		return B_PERMISSION_DENIED;
	if (cpu < 0 || cpu >= smp_get_num_cpus())
		return B_BAD_VALUE;

	// We need to lock here to make sure that no one can disable
	// the last CPU

	InterruptsSpinLocker locker(sSetCpuLock);

	if (!enabled) {
		// check if this is the last CPU to be disabled
		for (i = 0, count = 0; i < smp_get_num_cpus(); i++) {
			if (!gCPU[i].disabled)
				count++;
		}

		if (count == 1)
			return B_NOT_ALLOWED;
	}

	bool oldState = gCPU[cpu].disabled;

	if (oldState != !enabled)
		scheduler_set_cpu_enabled(cpu, enabled);

	if (!enabled) {
		if (smp_get_current_cpu() == cpu) {
			locker.Unlock();
			thread_yield();
			locker.Lock();
		}

		// someone reenabled the CPU while we were rescheduling
		if (!gCPU[cpu].disabled)
			return B_OK;

		ASSERT(smp_get_current_cpu() != cpu);
		while (!thread_is_idle_thread(gCPU[cpu].running_thread)) {
			locker.Unlock();
			thread_yield();
			locker.Lock();

			if (!gCPU[cpu].disabled)
				return B_OK;
			ASSERT(smp_get_current_cpu() != cpu);
		}
	}

	return B_OK;
}
