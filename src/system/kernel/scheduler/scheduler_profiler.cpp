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
 * @file scheduler_profiler.cpp
 * @brief Optional scheduler-internal function-level profiler.
 *
 * Compiled in only when SCHEDULER_PROFILING is defined. Each CPU has its own
 * call stack of FunctionEntry records; EnterFunction()/ExitFunction() bracket
 * hot scheduler routines to record inclusive and exclusive time plus call
 * counts. The aggregated data is inspected with the `scheduler_profiler`
 * kernel-debugger command.
 */

#include "scheduler_profiler.h"

#include <debug.h>
#include <util/AutoLock.h>

#include <algorithm>


#ifdef SCHEDULER_PROFILING


using namespace Scheduler;
using namespace Scheduler::Profiling;


static Profiler* sProfiler;

static int dump_profiler(int argc, char** argv);


/**
 * @brief Allocate per-function and per-CPU stack buffers.
 *
 * Failure to allocate any table leaves fStatus at B_NO_MEMORY; callers must
 * check GetStatus() before using the profiler.
 */
Profiler::Profiler()
	:
	kMaxFunctionEntries(1024),
	kMaxFunctionStackEntries(512),
	fFunctionData(new(std::nothrow) FunctionData[kMaxFunctionEntries]),
	fStatus(B_OK)
{
	B_INITIALIZE_SPINLOCK(&fFunctionLock);

	if (fFunctionData == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}
	memset(fFunctionData, 0, sizeof(FunctionData) * kMaxFunctionEntries);

	for (int32 i = 0; i < smp_get_num_cpus(); i++) {
		fFunctionStacks[i]
			= new(std::nothrow) FunctionEntry[kMaxFunctionStackEntries];
		if (fFunctionStacks[i] == NULL) {
			fStatus = B_NO_MEMORY;
			return;
		}
		memset(fFunctionStacks[i], 0,
			sizeof(FunctionEntry) * kMaxFunctionStackEntries);
	}
	memset(fFunctionStackPointers, 0, sizeof(int32) * smp_get_num_cpus());
}


/**
 * @brief Push a profiling frame onto the current CPU's function stack.
 *
 * Looks up (or inserts) a FunctionData record for @a functionName, bumps its
 * call counter, and records the entry timestamp. The profiler's own overhead
 * for this call is stored on the new frame so it can be subtracted later.
 *
 * @param cpu          Current CPU index.
 * @param functionName Stable string identifying the instrumented function.
 */
void
Profiler::EnterFunction(int32 cpu, const char* functionName)
{
	nanotime_t start = system_time_nsecs();

	FunctionData* function = _FindFunction(functionName);
	if (function == NULL)
		return;
	atomic_add((int32*)&function->fCalled, 1);

	FunctionEntry* stackEntry
		= &fFunctionStacks[cpu][fFunctionStackPointers[cpu]];
	fFunctionStackPointers[cpu]++;

	ASSERT(fFunctionStackPointers[cpu] < kMaxFunctionStackEntries);

	stackEntry->fFunction = function;
	stackEntry->fEntryTime = start;
	stackEntry->fOthersTime = 0;

	nanotime_t stop = system_time_nsecs();
	stackEntry->fProfilerTime = stop - start;
}


/**
 * @brief Pop the top profiling frame and charge it.
 *
 * Computes elapsed time, subtracts profiler overhead, and atomically adds
 * the result to the function's inclusive time. Exclusive time excludes the
 * inclusive time attributed to any nested calls. The parent frame is credited
 * with the nested-call time so its exclusive total stays accurate.
 *
 * @param cpu          Current CPU index.
 * @param functionName Unused (kept for symmetry with EnterFunction).
 */
void
Profiler::ExitFunction(int32 cpu, const char* functionName)
{
	nanotime_t start = system_time_nsecs();

	ASSERT(fFunctionStackPointers[cpu] > 0);
	fFunctionStackPointers[cpu]--;
	FunctionEntry* stackEntry
		= &fFunctionStacks[cpu][fFunctionStackPointers[cpu]];

	nanotime_t timeSpent = start - stackEntry->fEntryTime;
	timeSpent -= stackEntry->fProfilerTime;

	atomic_add64(&stackEntry->fFunction->fTimeInclusive, timeSpent);
	atomic_add64(&stackEntry->fFunction->fTimeExclusive,
		timeSpent - stackEntry->fOthersTime);

	nanotime_t profilerTime = stackEntry->fProfilerTime;
	if (fFunctionStackPointers[cpu] > 0) {
		stackEntry = &fFunctionStacks[cpu][fFunctionStackPointers[cpu] - 1];
		stackEntry->fOthersTime += timeSpent;
		stackEntry->fProfilerTime += profilerTime;

		nanotime_t stop = system_time_nsecs();
		stackEntry->fProfilerTime += stop - start;
	}
}


/**
 * @brief Print functions sorted by call count.
 * @param maxCount Upper bound on rows printed (0 means all).
 */
void
Profiler::DumpCalled(uint32 maxCount)
{
	uint32 count = _FunctionCount();

	qsort(fFunctionData, count, sizeof(FunctionData),
		&_CompareFunctions<uint32, &FunctionData::fCalled>);

	if (maxCount > 0)
		count = std::min(count, maxCount);
	_Dump(count);
}


/**
 * @brief Print functions sorted by inclusive time (self + descendants).
 * @param maxCount Upper bound on rows printed (0 means all).
 */
void
Profiler::DumpTimeInclusive(uint32 maxCount)
{
	uint32 count = _FunctionCount();

	qsort(fFunctionData, count, sizeof(FunctionData),
		&_CompareFunctions<nanotime_t, &FunctionData::fTimeInclusive>);

	if (maxCount > 0)
		count = std::min(count, maxCount);
	_Dump(count);
}


/**
 * @brief Print functions sorted by exclusive time (self only).
 * @param maxCount Upper bound on rows printed (0 means all).
 */
void
Profiler::DumpTimeExclusive(uint32 maxCount)
{
	uint32 count = _FunctionCount();

	qsort(fFunctionData, count, sizeof(FunctionData),
		&_CompareFunctions<nanotime_t, &FunctionData::fTimeExclusive>);

	if (maxCount > 0)
		count = std::min(count, maxCount);
	_Dump(count);
}


/**
 * @brief Print functions sorted by average inclusive time per call.
 * @param maxCount Upper bound on rows printed (0 means all).
 */
void
Profiler::DumpTimeInclusivePerCall(uint32 maxCount)
{
	uint32 count = _FunctionCount();

	qsort(fFunctionData, count, sizeof(FunctionData),
		&_CompareFunctionsPerCall<nanotime_t, &FunctionData::fTimeInclusive>);

	if (maxCount > 0)
		count = std::min(count, maxCount);
	_Dump(count);
}


/**
 * @brief Print functions sorted by average exclusive time per call.
 * @param maxCount Upper bound on rows printed (0 means all).
 */
void
Profiler::DumpTimeExclusivePerCall(uint32 maxCount)
{
	uint32 count = _FunctionCount();

	qsort(fFunctionData, count, sizeof(FunctionData),
		&_CompareFunctionsPerCall<nanotime_t, &FunctionData::fTimeExclusive>);

	if (maxCount > 0)
		count = std::min(count, maxCount);
	_Dump(count);
}


/**
 * @brief Return the singleton profiler instance.
 * @return Pointer to the global Profiler, or NULL before Initialize().
 */
/* static */ Profiler*
Profiler::Get()
{
	return sProfiler;
}


/**
 * @brief Allocate the singleton profiler and register its debugger command.
 *
 * Panics if the profiler cannot be allocated.
 */
/* static */ void
Profiler::Initialize()
{
	sProfiler = new(std::nothrow) Profiler;
	if (sProfiler == NULL || sProfiler->GetStatus() != B_OK)
		panic("Scheduler::Profiling::Profiler: could not initialize profiler");

	add_debugger_command_etc("scheduler_profiler", &dump_profiler,
		"Show data collected by scheduler profiler",
		"[ <field> [ <count> ] ]\n"
		"Shows data collected by scheduler profiler\n"
		"  <field>   - Field used to sort functions. Available: called,"
			" time-inclusive, time-inclusive-per-call, time-exclusive,"
			" time-exclusive-per-call.\n"
		"              (defaults to \"called\")\n"
		"  <count>   - Maximum number of showed functions.\n", 0);
}


/**
 * @brief Return the number of function slots currently in use.
 * @return Index of the first empty slot in the FunctionData table.
 */
uint32
Profiler::_FunctionCount() const
{
	uint32 count;
	for (count = 0; count < kMaxFunctionEntries; count++) {
		if (fFunctionData[count].fFunction == NULL)
			break;
	}
	return count;
}


/**
 * @brief Print the first @a count rows of the (already-sorted) FunctionData table.
 * @param count Number of rows to print.
 */
void
Profiler::_Dump(uint32 count)
{
	kprintf("Function calls (%" B_PRId32 " functions):\n", count);
	kprintf("    called time-inclusive per-call time-exclusive per-call "
		"function\n");
	for (uint32 i = 0; i < count; i++) {
		FunctionData* function = &fFunctionData[i];
		kprintf("%10" B_PRId32 " %14" B_PRId64 " %8" B_PRId64 " %14" B_PRId64
			" %8" B_PRId64 " %s\n", function->fCalled,
			function->fTimeInclusive,
			function->fTimeInclusive / function->fCalled,
			function->fTimeExclusive,
			function->fTimeExclusive / function->fCalled, function->fFunction);
	}
}


/**
 * @brief Look up or insert the FunctionData record for @a function.
 *
 * Two-phase lookup: first a lock-free scan, then a spin-locked scan that may
 * claim a fresh slot. Returns NULL if the table is full.
 *
 * @param function Stable string identifying the function.
 * @return Pointer to the matching FunctionData, or NULL on table overflow.
 */
Profiler::FunctionData*
Profiler::_FindFunction(const char* function)
{
	for (uint32 i = 0; i < kMaxFunctionEntries; i++) {
		if (fFunctionData[i].fFunction == NULL)
			break;
		if (!strcmp(fFunctionData[i].fFunction, function))
			return fFunctionData + i;
	}

	SpinLocker _(fFunctionLock);
	for (uint32 i = 0; i < kMaxFunctionEntries; i++) {
		if (fFunctionData[i].fFunction == NULL) {
			fFunctionData[i].fFunction = function;
			return fFunctionData + i;
		}
		if (!strcmp(fFunctionData[i].fFunction, function))
			return fFunctionData + i;
	}

	return NULL;
}


/**
 * @brief qsort comparator ordering FunctionData by the given member, descending.
 * @tparam Type    Numeric type of the sort key.
 * @tparam Member  Pointer-to-member selecting the sort key.
 * @return Negative if @a _a sorts before @a _b, positive if after, zero if equal.
 */
template<typename Type, Type Profiler::FunctionData::*Member>
/* static */ int
Profiler::_CompareFunctions(const void* _a, const void* _b)
{
	const FunctionData* a = static_cast<const FunctionData*>(_a);
	const FunctionData* b = static_cast<const FunctionData*>(_b);

	if (b->*Member > a->*Member)
		return 1;
	if (b->*Member < a->*Member)
		return -1;
	return 0;
}


/**
 * @brief qsort comparator ordering FunctionData by (member / fCalled), descending.
 * @tparam Type    Numeric type of the sort-key member.
 * @tparam Member  Pointer-to-member selecting the accumulator.
 * @return Negative if @a _a sorts before @a _b, positive if after, zero if equal.
 */
template<typename Type, Type Profiler::FunctionData::*Member>
/* static */ int
Profiler::_CompareFunctionsPerCall(const void* _a, const void* _b)
{
	const FunctionData* a = static_cast<const FunctionData*>(_a);
	const FunctionData* b = static_cast<const FunctionData*>(_b);

	Type valueA = a->*Member / a->fCalled;
	Type valueB = b->*Member / b->fCalled;

	if (valueB > valueA)
		return 1;
	if (valueB < valueA)
		return -1;
	return 0;
}


/**
 * @brief `scheduler_profiler` kernel debugger command handler.
 *
 * Dispatches to the appropriate Dump* routine based on the sort-field
 * argument and optional row-count argument.
 *
 * @param argc Argument count including the command name.
 * @param argv argv[1] is the sort field, argv[2] optional row limit.
 * @return Always 0.
 */
static int
dump_profiler(int argc, char** argv)
{
	if (argc < 2) {
		Profiler::Get()->DumpCalled(0);
		return 0;
	}

	int32 count = 0;
	if (argc >= 3)
		count = parse_expression(argv[2]);
	count = std::max(count, int32(0));

	if (!strcmp(argv[1], "called"))
		Profiler::Get()->DumpCalled(count);
	else if (!strcmp(argv[1], "time-inclusive"))
		Profiler::Get()->DumpTimeInclusive(count);
	else if (!strcmp(argv[1], "time-inclusive-per-call"))
		Profiler::Get()->DumpTimeInclusivePerCall(count);
	else if (!strcmp(argv[1], "time-exclusive"))
		Profiler::Get()->DumpTimeExclusive(count);
	else if (!strcmp(argv[1], "time-exclusive-per-call"))
		Profiler::Get()->DumpTimeExclusivePerCall(count);
	else
		print_debugger_command_usage(argv[0]);

	return 0;
}


#endif	// SCHEDULER_PROFILING

