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
 *   Copyright 2010-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file vm_page.cpp
 * @brief Physical page allocator and page daemon (page scanner/reclaimer).
 *
 * Manages the global pool of physical pages through page queues (free, clear,
 * modified, active, inactive). Implements vm_page_allocate_page,
 * vm_page_free, the page writer (writes modified pages to their backing store),
 * and the page daemon which trims working sets under memory pressure.
 *
 * @see vm.cpp, VMCache.cpp, VMPageQueue.cpp
 */


#include <string.h>
#include <stdlib.h>

#include <algorithm>

#include <KernelExport.h>
#include <OS.h>

#include <AutoDeleter.h>

#include <arch/cpu.h>
#include <arch/vm_translation_map.h>
#include <block_cache.h>
#include <boot/kernel_args.h>
#include <condition_variable.h>
#include <elf.h>
#include <heap.h>
#include <kernel.h>
#include <low_resource_manager.h>
#include <thread.h>
#include <tracing.h>
#include <util/AutoLock.h>
#include <vfs.h>
#include <vm/vm.h>
#include <vm/vm_priv.h>
#include <vm/vm_page.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>
#include <vm/VMCache.h>

#include "IORequest.h"
#include "PageCacheLocker.h"
#include "VMAnonymousCache.h"
#include "VMPageQueue.h"


//#define TRACE_VM_PAGE
#ifdef TRACE_VM_PAGE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

//#define TRACE_VM_DAEMONS
#ifdef TRACE_VM_DAEMONS
#define TRACE_DAEMON(x...) dprintf(x)
#else
#define TRACE_DAEMON(x...) do {} while (false)
#endif

//#define TRACK_PAGE_USAGE_STATS	1

#define PAGE_ASSERT(page, condition)	\
	ASSERT_PRINT((condition), "page: %p", (page))

#define SCRUB_SIZE 32
	// this many pages will be cleared at once in the page scrubber thread

#define MAX_PAGE_WRITER_IO_PRIORITY				B_URGENT_DISPLAY_PRIORITY
	// maximum I/O priority of the page writer
#define MAX_PAGE_WRITER_IO_PRIORITY_THRESHOLD	10000
	// the maximum I/O priority shall be reached when this many pages need to
	// be written


// The page reserve an allocation of the certain priority must not touch.
static const size_t kPageReserveForPriority[] = {
	VM_PAGE_RESERVE_USER,		// user
	VM_PAGE_RESERVE_SYSTEM,		// system
	0							// VIP
};

// Minimum number of free pages the page daemon will try to achieve.
static uint32 sFreePagesTarget;
static uint32 sFreeOrCachedPagesTarget;
static uint32 sInactivePagesTarget;

// Wait interval between page daemon runs.
static const bigtime_t kIdleScanWaitInterval = 1000000LL;	// 1 sec
static const bigtime_t kBusyScanWaitInterval = 500000LL;	// 0.5 sec

// Number of idle runs after which we want to have processed the full active
// queue.
static const uint32 kIdleRunsForFullQueue = 20;

// Maximum limit for the vm_page::usage_count.
static const int32 kPageUsageMax = 64;
// vm_page::usage_count buff an accessed page receives in a scan.
static const int32 kPageUsageAdvance = 3;
// vm_page::usage_count debuff an unaccessed page receives in a scan.
static const int32 kPageUsageDecline = 1;

int32 gMappedPagesCount;

static VMPageQueue sPageQueues[PAGE_STATE_FIRST_UNQUEUED];

static VMPageQueue& sFreePageQueue = sPageQueues[PAGE_STATE_FREE];
static VMPageQueue& sClearPageQueue = sPageQueues[PAGE_STATE_CLEAR];
static VMPageQueue& sModifiedPageQueue = sPageQueues[PAGE_STATE_MODIFIED];
static VMPageQueue& sInactivePageQueue = sPageQueues[PAGE_STATE_INACTIVE];
static VMPageQueue& sActivePageQueue = sPageQueues[PAGE_STATE_ACTIVE];
static VMPageQueue& sCachedPageQueue = sPageQueues[PAGE_STATE_CACHED];

static vm_page *sPages;
static page_num_t sPhysicalPageOffset;
static page_num_t sNumPages;
static page_num_t sNonExistingPages;
	// pages in the sPages array that aren't backed by physical memory
static uint64 sIgnoredPages;
	// pages of physical memory ignored by the boot loader (and thus not
	// available here)
static int32 sUnreservedFreePages;
static int32 sUnsatisfiedPageReservations;
static int32 sModifiedTemporaryPages;

static ConditionVariable sFreePageCondition;
static mutex sPageDeficitLock = MUTEX_INITIALIZER("page deficit");

// This lock must be used whenever the free or clear page queues are changed.
// If you need to work on both queues at the same time, you need to hold a write
// lock, otherwise, a read lock suffices (each queue still has a spinlock to
// guard against concurrent changes).
static rw_lock sFreePageQueuesLock
	= RW_LOCK_INITIALIZER("free/clear page queues");

#ifdef TRACK_PAGE_USAGE_STATS
static page_num_t sPageUsageArrays[512];
static page_num_t* sPageUsage = sPageUsageArrays;
static page_num_t sPageUsagePageCount;
static page_num_t* sNextPageUsage = sPageUsageArrays + 256;
static page_num_t sNextPageUsagePageCount;
#endif


#if VM_PAGE_ALLOCATION_TRACKING_AVAILABLE

struct caller_info {
	addr_t		caller;
	size_t		count;
};

static const int32 kCallerInfoTableSize = 1024;
static caller_info sCallerInfoTable[kCallerInfoTableSize];
static int32 sCallerInfoCount = 0;

static caller_info* get_caller_info(addr_t caller);


RANGE_MARKER_FUNCTION_PROTOTYPES(vm_page)

static const addr_t kVMPageCodeAddressRange[] = {
	RANGE_MARKER_FUNCTION_ADDRESS_RANGE(vm_page)
};

#endif


RANGE_MARKER_FUNCTION_BEGIN(vm_page)


struct page_stats {
	int32	totalFreePages;
	int32	unsatisfiedReservations;
	int32	cachedPages;
};


struct PageReservationWaiter
		: public DoublyLinkedListLinkImpl<PageReservationWaiter> {
	Thread*	thread;
	uint32	dontTouch;		// reserve not to touch
	uint32	requested;		// total pages requested
	uint32	reserved;		// pages reserved so far

	bool operator<(const PageReservationWaiter& other) const
	{
		// Implies an order by descending VM priority (ascending dontTouch)
		// and (secondarily) descending thread priority.
		if (dontTouch != other.dontTouch)
			return dontTouch < other.dontTouch;
		return thread->priority > other.thread->priority;
	}
};

typedef DoublyLinkedList<PageReservationWaiter> PageReservationWaiterList;
static PageReservationWaiterList sPageReservationWaiters;


struct DaemonCondition {
	void Init(const char* name)
	{
		mutex_init(&fLock, "daemon condition");
		fCondition.Init(this, name);
		fActivated = false;
	}

	bool Lock()
	{
		return mutex_lock(&fLock) == B_OK;
	}

	void Unlock()
	{
		mutex_unlock(&fLock);
	}

	bool Wait(bigtime_t timeout, bool clearActivated)
	{
		MutexLocker locker(fLock);
		if (clearActivated)
			fActivated = false;
		else if (fActivated)
			return true;

		ConditionVariableEntry entry;
		fCondition.Add(&entry);

		locker.Unlock();

		return entry.Wait(B_RELATIVE_TIMEOUT, timeout) == B_OK;
	}

	void WakeUp()
	{
		if (fActivated)
			return;

		MutexLocker locker(fLock);
		fActivated = true;
		fCondition.NotifyOne();
	}

	void ClearActivated()
	{
		MutexLocker locker(fLock);
		fActivated = false;
	}

private:
	mutex				fLock;
	ConditionVariable	fCondition;
	bool				fActivated;
};


static DaemonCondition sPageWriterCondition;
static DaemonCondition sPageDaemonCondition;


#if PAGE_ALLOCATION_TRACING

namespace PageAllocationTracing {

class ReservePages : public AbstractTraceEntry {
public:
	ReservePages(uint32 count)
		:
		fCount(count)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("page reserve:   %" B_PRIu32, fCount);
	}

private:
	uint32		fCount;
};


class UnreservePages : public AbstractTraceEntry {
public:
	UnreservePages(uint32 count)
		:
		fCount(count)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("page unreserve: %" B_PRId32, fCount);
	}

private:
	uint32		fCount;
};


class AllocatePage
	: public TRACE_ENTRY_SELECTOR(PAGE_ALLOCATION_TRACING_STACK_TRACE) {
public:
	AllocatePage(page_num_t pageNumber)
		:
		TraceEntryBase(PAGE_ALLOCATION_TRACING_STACK_TRACE, 0, true),
		fPageNumber(pageNumber)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("page alloc: %#" B_PRIxPHYSADDR, fPageNumber);
	}

private:
	page_num_t	fPageNumber;
};


class AllocatePageRun
	: public TRACE_ENTRY_SELECTOR(PAGE_ALLOCATION_TRACING_STACK_TRACE) {
public:
	AllocatePageRun(page_num_t startPage, uint32 length)
		:
		TraceEntryBase(PAGE_ALLOCATION_TRACING_STACK_TRACE, 0, true),
		fStartPage(startPage),
		fLength(length)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("page alloc run: start %#" B_PRIxPHYSADDR " length: %"
			B_PRIu32, fStartPage, fLength);
	}

private:
	page_num_t	fStartPage;
	uint32		fLength;
};


class FreePage
	: public TRACE_ENTRY_SELECTOR(PAGE_ALLOCATION_TRACING_STACK_TRACE) {
public:
	FreePage(page_num_t pageNumber)
		:
		TraceEntryBase(PAGE_ALLOCATION_TRACING_STACK_TRACE, 0, true),
		fPageNumber(pageNumber)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("page free: %#" B_PRIxPHYSADDR, fPageNumber);
	}

private:
	page_num_t	fPageNumber;
};


class ScrubbingPages : public AbstractTraceEntry {
public:
	ScrubbingPages(uint32 count)
		:
		fCount(count)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("page scrubbing: %" B_PRId32, fCount);
	}

private:
	uint32		fCount;
};


class ScrubbedPages : public AbstractTraceEntry {
public:
	ScrubbedPages(uint32 count)
		:
		fCount(count)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("page scrubbed:  %" B_PRId32, fCount);
	}

private:
	uint32		fCount;
};


class StolenPage : public AbstractTraceEntry {
public:
	StolenPage()
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("page stolen");
	}
};

}	// namespace PageAllocationTracing

#	define TA(x)	new(std::nothrow) PageAllocationTracing::x

#else
#	define TA(x)
#endif	// PAGE_ALLOCATION_TRACING


#if PAGE_DAEMON_TRACING

namespace PageDaemonTracing {

class ActivatePage : public AbstractTraceEntry {
	public:
		ActivatePage(vm_page* page)
			:
			fCache(page->cache),
			fPage(page)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("page activated:   %p, cache: %p", fPage, fCache);
		}

	private:
		VMCache*	fCache;
		vm_page*	fPage;
};


class DeactivatePage : public AbstractTraceEntry {
	public:
		DeactivatePage(vm_page* page)
			:
			fCache(page->cache),
			fPage(page)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("page deactivated: %p, cache: %p", fPage, fCache);
		}

	private:
		VMCache*	fCache;
		vm_page*	fPage;
};


class FreedPageSwap : public AbstractTraceEntry {
	public:
		FreedPageSwap(vm_page* page)
			:
			fCache(page->cache),
			fPage(page)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("page swap freed:  %p, cache: %p", fPage, fCache);
		}

	private:
		VMCache*	fCache;
		vm_page*	fPage;
};

}	// namespace PageDaemonTracing

#	define TD(x)	new(std::nothrow) PageDaemonTracing::x

#else
#	define TD(x)
#endif	// PAGE_DAEMON_TRACING


#if PAGE_WRITER_TRACING

namespace PageWriterTracing {

class WritePage : public AbstractTraceEntry {
	public:
		WritePage(vm_page* page)
			:
			fCache(page->Cache()),
			fPage(page)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("page write: %p, cache: %p", fPage, fCache);
		}

	private:
		VMCache*	fCache;
		vm_page*	fPage;
};

}	// namespace PageWriterTracing

#	define TPW(x)	new(std::nothrow) PageWriterTracing::x

#else
#	define TPW(x)
#endif	// PAGE_WRITER_TRACING


#if PAGE_STATE_TRACING

namespace PageStateTracing {

class SetPageState : public AbstractTraceEntry {
	public:
		SetPageState(vm_page* page, uint8 newState)
			:
			fPage(page),
			fOldState(page->State()),
			fNewState(newState),
			fBusy(page->busy),
			fWired(page->WiredCount() > 0),
			fMapped(!page->mappings.IsEmpty()),
			fAccessed(page->accessed),
			fModified(page->modified)
		{
#if PAGE_STATE_TRACING_STACK_TRACE
			fStackTrace = capture_tracing_stack_trace(
				PAGE_STATE_TRACING_STACK_TRACE, 0, true);
				// Don't capture userland stack trace to avoid potential
				// deadlocks.
#endif
			Initialized();
		}

#if PAGE_STATE_TRACING_STACK_TRACE
		virtual void DumpStackTrace(TraceOutput& out)
		{
			out.PrintStackTrace(fStackTrace);
		}
#endif

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("page set state: %p (%c%c%c%c%c): %s -> %s", fPage,
				fBusy ? 'b' : '-',
				fWired ? 'w' : '-',
				fMapped ? 'm' : '-',
				fAccessed ? 'a' : '-',
				fModified ? 'm' : '-',
				page_state_to_string(fOldState),
				page_state_to_string(fNewState));
		}

	private:
		vm_page*	fPage;
#if PAGE_STATE_TRACING_STACK_TRACE
		tracing_stack_trace* fStackTrace;
#endif
		uint8		fOldState;
		uint8		fNewState;
		bool		fBusy : 1;
		bool		fWired : 1;
		bool		fMapped : 1;
		bool		fAccessed : 1;
		bool		fModified : 1;
};

}	// namespace PageStateTracing

#	define TPS(x)	new(std::nothrow) PageStateTracing::x

#else
#	define TPS(x)
#endif	// PAGE_STATE_TRACING


#if VM_PAGE_ALLOCATION_TRACKING_AVAILABLE

namespace BKernel {

class AllocationTrackingCallback {
public:
	virtual						~AllocationTrackingCallback();

	virtual	bool				ProcessTrackingInfo(
									AllocationTrackingInfo* info,
									page_num_t pageNumber) = 0;
};

}

using BKernel::AllocationTrackingCallback;


class AllocationCollectorCallback : public AllocationTrackingCallback {
public:
	AllocationCollectorCallback(bool resetInfos)
		:
		fResetInfos(resetInfos)
	{
	}

	virtual bool ProcessTrackingInfo(AllocationTrackingInfo* info,
		page_num_t pageNumber)
	{
		if (!info->IsInitialized())
			return true;

		addr_t caller = 0;
		AbstractTraceEntryWithStackTrace* traceEntry = info->TraceEntry();

		if (traceEntry != NULL && info->IsTraceEntryValid()) {
			caller = tracing_find_caller_in_stack_trace(
				traceEntry->StackTrace(), kVMPageCodeAddressRange, 1);
		}

		caller_info* callerInfo = get_caller_info(caller);
		if (callerInfo == NULL) {
			kprintf("out of space for caller infos\n");
			return false;
		}

		callerInfo->count++;

		if (fResetInfos)
			info->Clear();

		return true;
	}

private:
	bool	fResetInfos;
};


class AllocationInfoPrinterCallback : public AllocationTrackingCallback {
public:
	AllocationInfoPrinterCallback(bool printStackTrace, page_num_t pageFilter,
		team_id teamFilter, thread_id threadFilter)
		:
		fPrintStackTrace(printStackTrace),
		fPageFilter(pageFilter),
		fTeamFilter(teamFilter),
		fThreadFilter(threadFilter)
	{
	}

	virtual bool ProcessTrackingInfo(AllocationTrackingInfo* info,
		page_num_t pageNumber)
	{
		if (!info->IsInitialized())
			return true;

		if (fPageFilter != 0 && pageNumber != fPageFilter)
			return true;

		AbstractTraceEntryWithStackTrace* traceEntry = info->TraceEntry();
		if (traceEntry != NULL && !info->IsTraceEntryValid())
			traceEntry = NULL;

		if (traceEntry != NULL) {
			if (fTeamFilter != -1 && traceEntry->TeamID() != fTeamFilter)
				return true;
			if (fThreadFilter != -1 && traceEntry->ThreadID() != fThreadFilter)
				return true;
		} else {
			// we need the info if we have filters set
			if (fTeamFilter != -1 || fThreadFilter != -1)
				return true;
		}

		kprintf("page number %#" B_PRIxPHYSADDR, pageNumber);

		if (traceEntry != NULL) {
			kprintf(", team: %" B_PRId32 ", thread %" B_PRId32
				", time %" B_PRId64 "\n", traceEntry->TeamID(),
				traceEntry->ThreadID(), traceEntry->Time());

			if (fPrintStackTrace)
				tracing_print_stack_trace(traceEntry->StackTrace());
		} else
			kprintf("\n");

		return true;
	}

private:
	bool		fPrintStackTrace;
	page_num_t	fPageFilter;
	team_id		fTeamFilter;
	thread_id	fThreadFilter;
};


class AllocationDetailPrinterCallback : public AllocationTrackingCallback {
public:
	AllocationDetailPrinterCallback(addr_t caller)
		:
		fCaller(caller)
	{
	}

	virtual bool ProcessTrackingInfo(AllocationTrackingInfo* info,
		page_num_t pageNumber)
	{
		if (!info->IsInitialized())
			return true;

		addr_t caller = 0;
		AbstractTraceEntryWithStackTrace* traceEntry = info->TraceEntry();
		if (traceEntry != NULL && !info->IsTraceEntryValid())
			traceEntry = NULL;

		if (traceEntry != NULL) {
			caller = tracing_find_caller_in_stack_trace(
				traceEntry->StackTrace(), kVMPageCodeAddressRange, 1);
		}

		if (caller != fCaller)
			return true;

		kprintf("page %#" B_PRIxPHYSADDR "\n", pageNumber);
		if (traceEntry != NULL)
			tracing_print_stack_trace(traceEntry->StackTrace());

		return true;
	}

private:
	addr_t	fCaller;
};

#endif	// VM_PAGE_ALLOCATION_TRACKING_AVAILABLE


/**
 * @brief Print a single page's address, state flags, usage count, wired count, and area mappings to the kernel debugger.
 *
 * @param page Pointer to the vm_page to display.
 * @note Intended for use from the kernel debugger only; not safe for normal kernel context.
 */
static void
list_page(vm_page* page)
{
	kprintf("0x%08" B_PRIxADDR " ",
		(addr_t)(page->physical_page_number * B_PAGE_SIZE));
	switch (page->State()) {
		case PAGE_STATE_ACTIVE:   kprintf("A"); break;
		case PAGE_STATE_INACTIVE: kprintf("I"); break;
		case PAGE_STATE_MODIFIED: kprintf("M"); break;
		case PAGE_STATE_CACHED:   kprintf("C"); break;
		case PAGE_STATE_FREE:     kprintf("F"); break;
		case PAGE_STATE_CLEAR:    kprintf("L"); break;
		case PAGE_STATE_WIRED:    kprintf("W"); break;
		case PAGE_STATE_UNUSED:   kprintf("-"); break;
	}
	kprintf(" ");
	if (page->busy)         kprintf("B"); else kprintf("-");
	if (page->busy_io)      kprintf("I"); else kprintf("-");
	if (page->accessed)     kprintf("A"); else kprintf("-");
	if (page->modified)     kprintf("M"); else kprintf("-");
	kprintf("-");

	kprintf(" usage:%3u", page->usage_count);
	kprintf(" wired:%5u", page->WiredCount());

	bool first = true;
	vm_page_mappings::Iterator iterator = page->mappings.GetIterator();
	vm_page_mapping* mapping;
	while ((mapping = iterator.Next()) != NULL) {
		if (first) {
			kprintf(": ");
			first = false;
		} else
			kprintf(", ");

		kprintf("%" B_PRId32 " (%s)", mapping->area->id, mapping->area->name);
		mapping = mapping->page_link.next;
	}
}


/**
 * @brief Kernel debugger command: dump all non-unused pages from the global page table.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @retval 0 Always.
 * @note Must only be called from the kernel debugger.
 */
static int
dump_page_list(int argc, char **argv)
{
	kprintf("page table:\n");
	for (page_num_t i = 0; i < sNumPages; i++) {
		if (sPages[i].State() != PAGE_STATE_UNUSED) {
			list_page(&sPages[i]);
			kprintf("\n");
		}
	}
	kprintf("end of page table\n");

	return 0;
}


/**
 * @brief Kernel debugger command: locate which page queue contains a given vm_page pointer.
 *
 * @param argc Argument count; must be at least 2.
 * @param argv argv[1] must be a hex address (0x...) of a vm_page struct.
 * @retval 0 Always.
 * @note Must only be called from the kernel debugger.
 */
static int
find_page(int argc, char **argv)
{
	struct vm_page *page;
	addr_t address;
	int32 index = 1;
	int i;

	struct {
		const char*	name;
		VMPageQueue*	queue;
	} pageQueueInfos[] = {
		{ "free",		&sFreePageQueue },
		{ "clear",		&sClearPageQueue },
		{ "modified",	&sModifiedPageQueue },
		{ "active",		&sActivePageQueue },
		{ "inactive",	&sInactivePageQueue },
		{ "cached",		&sCachedPageQueue },
		{ NULL, NULL }
	};

	if (argc < 2
		|| strlen(argv[index]) <= 2
		|| argv[index][0] != '0'
		|| argv[index][1] != 'x') {
		kprintf("usage: find_page <address>\n");
		return 0;
	}

	address = strtoul(argv[index], NULL, 0);
	page = (vm_page*)address;

	for (i = 0; pageQueueInfos[i].name; i++) {
		VMPageQueue::Iterator it = pageQueueInfos[i].queue->GetIterator();
		while (vm_page* p = it.Next()) {
			if (p == page) {
				kprintf("found page %p in queue %p (%s)\n", page,
					pageQueueInfos[i].queue, pageQueueInfos[i].name);
				return 0;
			}
		}
	}

	kprintf("page %p isn't in any queue\n", page);

	return 0;
}


/**
 * @brief Convert a numeric page state constant to a human-readable string.
 *
 * @param state One of the PAGE_STATE_* constants.
 * @return A static C string naming the state, or "unknown" if unrecognized.
 */
const char *
page_state_to_string(int state)
{
	switch(state) {
		case PAGE_STATE_ACTIVE:
			return "active";
		case PAGE_STATE_INACTIVE:
			return "inactive";
		case PAGE_STATE_MODIFIED:
			return "modified";
		case PAGE_STATE_CACHED:
			return "cached";
		case PAGE_STATE_FREE:
			return "free";
		case PAGE_STATE_CLEAR:
			return "clear";
		case PAGE_STATE_WIRED:
			return "wired";
		case PAGE_STATE_UNUSED:
			return "unused";
		default:
			return "unknown";
	}
}


/**
 * @brief Kernel debugger command: dump detailed information about a single physical page.
 *
 * Accepts flags: -p (address is physical), -v (address is virtual in current
 * thread's address space), -m (also search all address spaces for reverse mappings).
 *
 * @param argc Argument count.
 * @param argv argv[1..] flags and the address of the page.
 * @retval 0 Always.
 * @note Must only be called from the kernel debugger.
 */
static int
dump_page_long(int argc, char **argv)
{
	bool addressIsPointer = true;
	bool physical = false;
	bool searchMappings = false;
	int32 index = 1;

	while (index < argc) {
		if (argv[index][0] != '-')
			break;

		if (!strcmp(argv[index], "-p")) {
			addressIsPointer = false;
			physical = true;
		} else if (!strcmp(argv[index], "-v")) {
			addressIsPointer = false;
		} else if (!strcmp(argv[index], "-m")) {
			searchMappings = true;
		} else {
			print_debugger_command_usage(argv[0]);
			return 0;
		}

		index++;
	}

	if (index + 1 != argc) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	uint64 value;
	if (!evaluate_debug_expression(argv[index], &value, false))
		return 0;

	uint64 pageAddress = value;
	struct vm_page* page;

	if (addressIsPointer) {
		page = (struct vm_page *)(addr_t)pageAddress;
	} else {
		if (!physical) {
			VMAddressSpace *addressSpace = VMAddressSpace::Kernel();

			if (debug_get_debugged_thread()->team->address_space != NULL)
				addressSpace = debug_get_debugged_thread()->team->address_space;

			uint32 flags = 0;
			phys_addr_t physicalAddress;
			if (addressSpace->TranslationMap()->QueryInterrupt(pageAddress,
					&physicalAddress, &flags) != B_OK
				|| (flags & PAGE_PRESENT) == 0) {
				kprintf("Virtual address not mapped to a physical page in this "
					"address space.\n");
				return 0;
			}
			pageAddress = physicalAddress;
		}

		page = vm_lookup_page(pageAddress / B_PAGE_SIZE);
	}

	if (page == NULL) {
		kprintf("Page not found.\n");
		return 0;
	}

	kprintf("PAGE: %p\n", page);

	const off_t pageOffset = (addr_t)page - (addr_t)sPages;
	const off_t pageIndex = pageOffset / (off_t)sizeof(vm_page);
	if (pageIndex < 0) {
		kprintf("\taddress is before start of page array!"
			" (offset %" B_PRIdOFF ")\n", pageOffset);
	} else if ((page_num_t)pageIndex >= sNumPages) {
		kprintf("\taddress is after end of page array!"
			" (offset %" B_PRIdOFF ")\n", pageOffset);
	} else if ((pageIndex * (off_t)sizeof(vm_page)) != pageOffset) {
		kprintf("\taddress isn't a multiple of page structure size!"
			" (offset %" B_PRIdOFF ", expected align %" B_PRIuSIZE ")\n",
			pageOffset, sizeof(vm_page));
	}

	kprintf("queue_next,prev: %p, %p\n", page->queue_link.next,
		page->queue_link.previous);
	kprintf("physical_number: %#" B_PRIxPHYSADDR "\n", page->physical_page_number);
	kprintf("cache:           %p\n", page->Cache());
	kprintf("cache_offset:    %" B_PRIuPHYSADDR "\n", page->cache_offset);
	kprintf("cache_next:      %p\n", page->cache_next);
	kprintf("state:           %s\n", page_state_to_string(page->State()));
	kprintf("wired_count:     %d\n", page->WiredCount());
	kprintf("usage_count:     %d\n", page->usage_count);
	kprintf("busy:            %d\n", page->busy);
	kprintf("busy_io:         %d\n", page->busy_io);
	kprintf("accessed:        %d\n", page->accessed);
	kprintf("modified:        %d\n", page->modified);
#if DEBUG_PAGE_QUEUE
	kprintf("queue:           %p\n", page->queue);
#endif
#if DEBUG_PAGE_ACCESS
	kprintf("accessor:        %" B_PRId32 "\n", page->accessing_thread);
#endif

	if (pageIndex < 0 || (page_num_t)pageIndex >= sNumPages) {
		// Don't try to read the mappings.
		return 0;
	}

	kprintf("area mappings:\n");
	vm_page_mappings::Iterator iterator = page->mappings.GetIterator();
	vm_page_mapping *mapping;
	while ((mapping = iterator.Next()) != NULL) {
		kprintf("  %p (%" B_PRId32 ")\n", mapping->area, mapping->area->id);
		mapping = mapping->page_link.next;
	}

	if (searchMappings) {
		struct Callback : VMTranslationMap::ReverseMappingInfoCallback {
			VMAddressSpace*	fAddressSpace;

			virtual bool HandleVirtualAddress(addr_t virtualAddress)
			{
				phys_addr_t physicalAddress;
				uint32 flags = 0;
				if (fAddressSpace->TranslationMap()->QueryInterrupt(virtualAddress,
						&physicalAddress, &flags) != B_OK) {
					kprintf(" aspace %" B_PRId32 ": %#"	B_PRIxADDR " (querying failed)\n",
						fAddressSpace->ID(), virtualAddress);
					return false;
				}
				VMArea* area = fAddressSpace->LookupArea(virtualAddress);
				kprintf("  aspace %" B_PRId32 ", area %" B_PRId32 ": %#"
					B_PRIxADDR " (%c%c%s%s)\n", fAddressSpace->ID(),
					area != NULL ? area->id : -1, virtualAddress,
					(flags & B_KERNEL_READ_AREA) != 0 ? 'r' : '-',
					(flags & B_KERNEL_WRITE_AREA) != 0 ? 'w' : '-',
					(flags & PAGE_MODIFIED) != 0 ? " modified" : "",
					(flags & PAGE_ACCESSED) != 0 ? " accessed" : "");
				return false;
			}
		} callback;

		kprintf("all mappings:\n");
		VMAddressSpace* addressSpace = VMAddressSpace::DebugFirst();
		while (addressSpace != NULL) {
			callback.fAddressSpace = addressSpace;
			addressSpace->TranslationMap()->DebugGetReverseMappingInfo(
				page->physical_page_number * B_PAGE_SIZE, callback);
			addressSpace = VMAddressSpace::DebugNext(addressSpace);
		}
	}

	set_debug_variable("_cache", (addr_t)page->Cache());
#if DEBUG_PAGE_ACCESS
	set_debug_variable("_accessor", page->accessing_thread);
#endif

	return 0;
}


/**
 * @brief Kernel debugger command: dump summary and optionally full listing of a named page queue.
 *
 * @param argc Argument count; must be at least 2.
 * @param argv argv[1] is the queue name or hex address; argv[2] (optional) "list" to enumerate entries.
 * @retval 0 Always.
 * @note Must only be called from the kernel debugger.
 */
static int
dump_page_queue(int argc, char **argv)
{
	struct VMPageQueue *queue;

	if (argc < 2) {
		kprintf("usage: page_queue <address/name> [list]\n");
		return 0;
	}

	if (strlen(argv[1]) >= 2 && argv[1][0] == '0' && argv[1][1] == 'x')
		queue = (VMPageQueue*)strtoul(argv[1], NULL, 16);
	else if (!strcmp(argv[1], "free"))
		queue = &sFreePageQueue;
	else if (!strcmp(argv[1], "clear"))
		queue = &sClearPageQueue;
	else if (!strcmp(argv[1], "modified"))
		queue = &sModifiedPageQueue;
	else if (!strcmp(argv[1], "active"))
		queue = &sActivePageQueue;
	else if (!strcmp(argv[1], "inactive"))
		queue = &sInactivePageQueue;
	else if (!strcmp(argv[1], "cached"))
		queue = &sCachedPageQueue;
	else {
		kprintf("page_queue: unknown queue \"%s\".\n", argv[1]);
		return 0;
	}

	kprintf("queue = %p, queue->head = %p, queue->tail = %p, queue->count = %"
		B_PRIuPHYSADDR "\n", queue, queue->Head(), queue->Tail(),
		queue->Count());

	if (argc == 3) {
		struct vm_page *page = queue->Head();

		kprintf("page        cache       type       state  wired  usage\n");
		for (page_num_t i = 0; page; i++, page = queue->Next(page)) {
			kprintf("%p  %p  %-7s %8s  %5d  %5d\n", page, page->Cache(),
				vm_cache_type_to_string(page->Cache()->type),
				page_state_to_string(page->State()),
				page->WiredCount(), page->usage_count);
		}
	}
	return 0;
}


/**
 * @brief Kernel debugger command: print detailed statistics for all page queues and counters.
 *
 * Reports per-state page counts, busy counts, longest free/cached runs,
 * unsatisfied reservation waiters, and queue lengths.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @retval 0 Always.
 * @note Must only be called from the kernel debugger.
 */
static int
dump_page_stats(int argc, char **argv)
{
	page_num_t swappableModified = 0;
	page_num_t swappableModifiedInactive = 0;

	size_t counter[8];
	size_t busyCounter[8];
	memset(counter, 0, sizeof(counter));
	memset(busyCounter, 0, sizeof(busyCounter));

	struct page_run {
		page_num_t	start;
		page_num_t	end;

		page_num_t Length() const	{ return end - start; }
	};

	page_run currentFreeRun = { 0, 0 };
	page_run currentCachedRun = { 0, 0 };
	page_run longestFreeRun = { 0, 0 };
	page_run longestCachedRun = { 0, 0 };

	for (page_num_t i = 0; i < sNumPages; i++) {
		if (sPages[i].State() > 7) {
			panic("page %" B_PRIuPHYSADDR " at %p has invalid state!\n", i,
				&sPages[i]);
		}

		uint32 pageState = sPages[i].State();

		counter[pageState]++;
		if (sPages[i].busy)
			busyCounter[pageState]++;

		if (pageState == PAGE_STATE_MODIFIED
			&& sPages[i].Cache() != NULL
			&& sPages[i].Cache()->temporary && sPages[i].WiredCount() == 0) {
			swappableModified++;
			if (sPages[i].usage_count == 0)
				swappableModifiedInactive++;
		}

		// track free and cached pages runs
		if (pageState == PAGE_STATE_FREE || pageState == PAGE_STATE_CLEAR) {
			currentFreeRun.end = i + 1;
			currentCachedRun.end = i + 1;
		} else {
			if (currentFreeRun.Length() > longestFreeRun.Length())
				longestFreeRun = currentFreeRun;
			currentFreeRun.start = currentFreeRun.end = i + 1;

			if (pageState == PAGE_STATE_CACHED) {
				currentCachedRun.end = i + 1;
			} else {
				if (currentCachedRun.Length() > longestCachedRun.Length())
					longestCachedRun = currentCachedRun;
				currentCachedRun.start = currentCachedRun.end = i + 1;
			}
		}
	}

	kprintf("page stats:\n");
	kprintf("total: %" B_PRIuPHYSADDR "\n", sNumPages);

	kprintf("active: %" B_PRIuSIZE " (busy: %" B_PRIuSIZE ")\n",
		counter[PAGE_STATE_ACTIVE], busyCounter[PAGE_STATE_ACTIVE]);
	kprintf("inactive: %" B_PRIuSIZE " (busy: %" B_PRIuSIZE ")\n",
		counter[PAGE_STATE_INACTIVE], busyCounter[PAGE_STATE_INACTIVE]);
	kprintf("cached: %" B_PRIuSIZE " (busy: %" B_PRIuSIZE ")\n",
		counter[PAGE_STATE_CACHED], busyCounter[PAGE_STATE_CACHED]);
	kprintf("unused: %" B_PRIuSIZE " (busy: %" B_PRIuSIZE ")\n",
		counter[PAGE_STATE_UNUSED], busyCounter[PAGE_STATE_UNUSED]);
	kprintf("wired: %" B_PRIuSIZE " (busy: %" B_PRIuSIZE ")\n",
		counter[PAGE_STATE_WIRED], busyCounter[PAGE_STATE_WIRED]);
	kprintf("modified: %" B_PRIuSIZE " (busy: %" B_PRIuSIZE ")\n",
		counter[PAGE_STATE_MODIFIED], busyCounter[PAGE_STATE_MODIFIED]);
	kprintf("free: %" B_PRIuSIZE "\n", counter[PAGE_STATE_FREE]);
	kprintf("clear: %" B_PRIuSIZE "\n", counter[PAGE_STATE_CLEAR]);

	kprintf("unreserved free pages: %" B_PRId32 "\n", sUnreservedFreePages);
	kprintf("unsatisfied page reservations: %" B_PRId32 "\n",
		sUnsatisfiedPageReservations);
	kprintf("mapped pages: %" B_PRId32 "\n", gMappedPagesCount);
	kprintf("longest free pages run: %" B_PRIuPHYSADDR " pages (at %"
		B_PRIuPHYSADDR ")\n", longestFreeRun.Length(),
		sPages[longestFreeRun.start].physical_page_number);
	kprintf("longest free/cached pages run: %" B_PRIuPHYSADDR " pages (at %"
		B_PRIuPHYSADDR ")\n", longestCachedRun.Length(),
		sPages[longestCachedRun.start].physical_page_number);

	kprintf("waiting threads:\n");
	for (PageReservationWaiterList::Iterator it
			= sPageReservationWaiters.GetIterator();
		PageReservationWaiter* waiter = it.Next();) {
		kprintf("  %6" B_PRId32 ": requested: %6" B_PRIu32 ", reserved: %6" B_PRIu32
			", don't touch: %6" B_PRIu32 "\n", waiter->thread->id,
			waiter->requested, waiter->reserved, waiter->dontTouch);
	}

	kprintf("\nfree queue: %p, count = %" B_PRIuPHYSADDR "\n", &sFreePageQueue,
		sFreePageQueue.Count());
	kprintf("clear queue: %p, count = %" B_PRIuPHYSADDR "\n", &sClearPageQueue,
		sClearPageQueue.Count());
	kprintf("modified queue: %p, count = %" B_PRIuPHYSADDR " (%" B_PRId32
		" temporary, %" B_PRIuPHYSADDR " swappable, " "inactive: %"
		B_PRIuPHYSADDR ")\n", &sModifiedPageQueue, sModifiedPageQueue.Count(),
		sModifiedTemporaryPages, swappableModified, swappableModifiedInactive);
	kprintf("active queue: %p, count = %" B_PRIuPHYSADDR "\n",
		&sActivePageQueue, sActivePageQueue.Count());
	kprintf("inactive queue: %p, count = %" B_PRIuPHYSADDR "\n",
		&sInactivePageQueue, sInactivePageQueue.Count());
	kprintf("cached queue: %p, count = %" B_PRIuPHYSADDR "\n",
		&sCachedPageQueue, sCachedPageQueue.Count());
	return 0;
}


#if VM_PAGE_ALLOCATION_TRACKING_AVAILABLE

/**
 * @brief Look up or create a caller_info entry for the given caller address.
 *
 * Searches the global sCallerInfoTable for an existing entry matching
 * \a caller. If none is found and there is room, a new zero-count entry is
 * inserted and returned.
 *
 * @param caller The return address identifying the allocation call site.
 * @return Pointer to the matching caller_info, or NULL if the table is full.
 * @note Must only be called from the kernel debugger.
 */
static caller_info*
get_caller_info(addr_t caller)
{
	// find the caller info
	for (int32 i = 0; i < sCallerInfoCount; i++) {
		if (caller == sCallerInfoTable[i].caller)
			return &sCallerInfoTable[i];
	}

	// not found, add a new entry, if there are free slots
	if (sCallerInfoCount >= kCallerInfoTableSize)
		return NULL;

	caller_info* info = &sCallerInfoTable[sCallerInfoCount++];
	info->caller = caller;
	info->count = 0;

	return info;
}


/**
 * @brief qsort comparator: sort caller_info entries by descending allocation count.
 *
 * @param _a Pointer to first caller_info.
 * @param _b Pointer to second caller_info.
 * @return Positive if a->count < b->count, negative if a->count > b->count, zero if equal.
 */
static int
caller_info_compare_count(const void* _a, const void* _b)
{
	const caller_info* a = (const caller_info*)_a;
	const caller_info* b = (const caller_info*)_b;
	return (int)(b->count - a->count);
}


/**
 * @brief Kernel debugger command: show live page allocations grouped and sorted by call site.
 *
 * Iterates all page tracking infos, groups them by caller address, sorts by
 * count (descending), and prints. With -d <caller> prints individual pages for
 * that caller. With -r resets all tracking infos after collection.
 *
 * @param argc Argument count.
 * @param argv argv[1..] optional flags: -d <caller_addr>, -r.
 * @retval 0 Always.
 * @note Must only be called from the kernel debugger; only available when
 *       VM_PAGE_ALLOCATION_TRACKING_AVAILABLE is defined.
 */
static int
dump_page_allocations_per_caller(int argc, char** argv)
{
	bool resetAllocationInfos = false;
	bool printDetails = false;
	addr_t caller = 0;

	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0) {
			uint64 callerAddress;
			if (++i >= argc
				|| !evaluate_debug_expression(argv[i], &callerAddress, true)) {
				print_debugger_command_usage(argv[0]);
				return 0;
			}

			caller = callerAddress;
			printDetails = true;
		} else if (strcmp(argv[i], "-r") == 0) {
			resetAllocationInfos = true;
		} else {
			print_debugger_command_usage(argv[0]);
			return 0;
		}
	}

	sCallerInfoCount = 0;

	AllocationCollectorCallback collectorCallback(resetAllocationInfos);
	AllocationDetailPrinterCallback detailsCallback(caller);
	AllocationTrackingCallback& callback = printDetails
		? (AllocationTrackingCallback&)detailsCallback
		: (AllocationTrackingCallback&)collectorCallback;

	for (page_num_t i = 0; i < sNumPages; i++)
		callback.ProcessTrackingInfo(&sPages[i].allocation_tracking_info, i);

	if (printDetails)
		return 0;

	// sort the array
	qsort(sCallerInfoTable, sCallerInfoCount, sizeof(caller_info),
		&caller_info_compare_count);

	kprintf("%" B_PRId32 " different callers\n\n", sCallerInfoCount);

	size_t totalAllocationCount = 0;

	kprintf("     count      caller\n");
	kprintf("----------------------------------\n");
	for (int32 i = 0; i < sCallerInfoCount; i++) {
		caller_info& info = sCallerInfoTable[i];
		kprintf("%10" B_PRIuSIZE "  %p", info.count, (void*)info.caller);

		const char* symbol;
		const char* imageName;
		bool exactMatch;
		addr_t baseAddress;

		if (elf_debug_lookup_symbol_address(info.caller, &baseAddress, &symbol,
				&imageName, &exactMatch) == B_OK) {
			kprintf("  %s + %#" B_PRIxADDR " (%s)%s\n", symbol,
				info.caller - baseAddress, imageName,
				exactMatch ? "" : " (nearest)");
		} else
			kprintf("\n");

		totalAllocationCount += info.count;
	}

	kprintf("\ntotal page allocations: %" B_PRIuSIZE "\n",
		totalAllocationCount);

	return 0;
}


/**
 * @brief Kernel debugger command: print raw per-page allocation tracking info with optional filters.
 *
 * Supports filtering by page number (-p), team (--team), thread (--thread),
 * and optionally printing stack traces (--stacktrace).
 *
 * @param argc Argument count.
 * @param argv argv[1..] optional flags: --stacktrace, -p <page>, --team <id>, --thread <id>.
 * @retval 0 Always.
 * @note Must only be called from the kernel debugger; only available when
 *       VM_PAGE_ALLOCATION_TRACKING_AVAILABLE is defined.
 */
static int
dump_page_allocation_infos(int argc, char** argv)
{
	page_num_t pageFilter = 0;
	team_id teamFilter = -1;
	thread_id threadFilter = -1;
	bool printStackTraces = false;

	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--stacktrace") == 0)
			printStackTraces = true;
		else if (strcmp(argv[i], "-p") == 0) {
			uint64 pageNumber;
			if (++i >= argc
				|| !evaluate_debug_expression(argv[i], &pageNumber, true)) {
				print_debugger_command_usage(argv[0]);
				return 0;
			}

			pageFilter = pageNumber;
		} else if (strcmp(argv[i], "--team") == 0) {
			uint64 team;
			if (++i >= argc
				|| !evaluate_debug_expression(argv[i], &team, true)) {
				print_debugger_command_usage(argv[0]);
				return 0;
			}

			teamFilter = team;
		} else if (strcmp(argv[i], "--thread") == 0) {
			uint64 thread;
			if (++i >= argc
				|| !evaluate_debug_expression(argv[i], &thread, true)) {
				print_debugger_command_usage(argv[0]);
				return 0;
			}

			threadFilter = thread;
		} else {
			print_debugger_command_usage(argv[0]);
			return 0;
		}
	}

	AllocationInfoPrinterCallback callback(printStackTraces, pageFilter,
		teamFilter, threadFilter);

	for (page_num_t i = 0; i < sNumPages; i++)
		callback.ProcessTrackingInfo(&sPages[i].allocation_tracking_info, i);

	return 0;
}

#endif	// VM_PAGE_ALLOCATION_TRACKING_AVAILABLE


#ifdef TRACK_PAGE_USAGE_STATS

/**
 * @brief Record the usage count of a single page into the next-cycle usage histogram.
 *
 * @param page Pointer to the page whose usage_count is to be sampled.
 * @note Only counts unwired pages. Must be called with appropriate exclusion
 *       to avoid races on sNextPageUsage.
 */
static void
track_page_usage(vm_page* page)
{
	if (page->WiredCount() == 0) {
		sNextPageUsage[(int32)page->usage_count + 128]++;
		sNextPageUsagePageCount++;
	}
}


/**
 * @brief Swap the current and next page-usage histograms and reset the next histogram to zero.
 *
 * Called once per page-daemon scan cycle to rotate usage statistics.
 * @note Not thread-safe; must be called from the page daemon thread only.
 */
static void
update_page_usage_stats()
{
	std::swap(sPageUsage, sNextPageUsage);
	sPageUsagePageCount = sNextPageUsagePageCount;

	memset(sNextPageUsage, 0, sizeof(page_num_t) * 256);
	sNextPageUsagePageCount = 0;

	// compute average
	if (sPageUsagePageCount > 0) {
		int64 sum = 0;
		for (int32 i = 0; i < 256; i++)
			sum += (int64)sPageUsage[i] * (i - 128);

		TRACE_DAEMON("average page usage: %f (%lu pages)\n",
			(float)sum / sPageUsagePageCount, sPageUsagePageCount);
	}
}


/**
 * @brief Kernel debugger command: print the distribution of page usage counts from the last scan.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @retval 0 Always.
 * @note Must only be called from the kernel debugger; only available when
 *       TRACK_PAGE_USAGE_STATS is defined.
 */
static int
dump_page_usage_stats(int argc, char** argv)
{
	kprintf("distribution of page usage counts (%lu pages):",
		sPageUsagePageCount);

	int64 sum = 0;
	for (int32 i = 0; i < 256; i++) {
		if (i % 8 == 0)
			kprintf("\n%4ld:", i - 128);

		int64 count = sPageUsage[i];
		sum += count * (i - 128);

		kprintf("  %9llu", count);
	}

	kprintf("\n\n");

	kprintf("average usage count: %f\n",
		sPageUsagePageCount > 0 ? (float)sum / sPageUsagePageCount : 0);

	return 0;
}

#endif	// TRACK_PAGE_USAGE_STATS


// #pragma mark - vm_page


/**
 * @brief Initialize the page's state field without emitting a tracing record.
 *
 * @param newState The initial PAGE_STATE_* value to assign.
 * @note Use only during early boot before tracing is active.
 */
inline void
vm_page::InitState(uint8 newState)
{
	state = newState;
}


/**
 * @brief Update the page's state field and emit a PAGE_STATE_TRACING record if enabled.
 *
 * @param newState The new PAGE_STATE_* value to assign.
 * @note The page's cache must be locked before calling this if the page is cached.
 */
inline void
vm_page::SetState(uint8 newState)
{
	TPS(SetPageState(this, newState));

	state = newState;
}


// #pragma mark -


/**
 * @brief Snapshot the current free-page counters into a page_stats structure.
 *
 * Reads sUnreservedFreePages, sCachedPageQueue.Count(), and
 * sUnsatisfiedPageReservations atomically enough for heuristic use.
 *
 * @param _pageStats Output structure to fill.
 * @note No locking is performed; values may be stale by the time the caller
 *       inspects them.
 */
static void
get_page_stats(page_stats& _pageStats)
{
	_pageStats.totalFreePages = sUnreservedFreePages;
	_pageStats.cachedPages = sCachedPageQueue.Count();
	_pageStats.unsatisfiedReservations = sUnsatisfiedPageReservations;
	// TODO: We don't get an actual snapshot here!
}


/**
 * @brief Determine whether the page daemon should perform active (full) paging.
 *
 * Returns true when the combined free and cached pages fall below the target
 * plus any unsatisfied reservations.
 *
 * @param pageStats A recently sampled page_stats snapshot.
 * @return true if active reclaim is needed, false if memory is sufficient.
 */
static bool
do_active_paging(const page_stats& pageStats)
{
	return pageStats.totalFreePages + pageStats.cachedPages
		< pageStats.unsatisfiedReservations
			+ (int32)sFreeOrCachedPagesTarget;
}


/*!	Reserves as many pages as possible from \c sUnreservedFreePages up to
	\a count. Doesn't touch the last \a dontTouch pages of
	\c sUnreservedFreePages, though.
	\return The number of actually reserved pages.
*/
/**
 * @brief Atomically reserve up to \a count pages from sUnreservedFreePages,
 *        leaving at least \a dontTouch pages untouched.
 *
 * Uses a compare-and-swap retry loop so no mutex is needed.
 *
 * @param count Maximum number of pages to reserve.
 * @param dontTouch Minimum number of free pages that must remain untouched.
 * @return The number of pages actually reserved (may be less than \a count).
 * @note May be called from any context including interrupt handlers.
 */
static uint32
reserve_some_pages(uint32 count, uint32 dontTouch)
{
	while (true) {
		int32 freePages = atomic_get(&sUnreservedFreePages);
		if (freePages <= (int32)dontTouch)
			return 0;

		int32 toReserve = std::min(count, freePages - dontTouch);
		if (atomic_test_and_set(&sUnreservedFreePages,
					freePages - toReserve, freePages)
				== freePages) {
			return toReserve;
		}

		// the count changed in the meantime -- retry
	}
}


/**
 * @brief Wake the highest-priority page-reservation waiter that can now be satisfied.
 *
 * Walks sPageReservationWaiters from head (highest priority) and attempts to
 * reserve pages for each waiter in turn.  Stops as soon as a waiter cannot be
 * fully satisfied.
 *
 * @note sPageDeficitLock must be held by the caller.
 */
static void
wake_up_page_reservation_waiters()
{
	ASSERT_LOCKED_MUTEX(&sPageDeficitLock);

	// TODO: If this is a low priority thread, we might want to disable
	// interrupts or otherwise ensure that we aren't unscheduled. Otherwise
	// high priority threads wait be kept waiting while a medium priority thread
	// prevents us from running.

	while (PageReservationWaiter* waiter = sPageReservationWaiters.Head()) {
		int32 reserved = reserve_some_pages(waiter->requested - waiter->reserved,
			waiter->dontTouch);
		if (reserved == 0)
			return;

		sUnsatisfiedPageReservations -= reserved;
		waiter->reserved += reserved;

		if (waiter->reserved != waiter->requested)
			return;

		sPageReservationWaiters.Remove(waiter);
		thread_unblock(waiter->thread, B_OK);
	}
}


/**
 * @brief Return \a count previously reserved pages to the unreserved pool and
 *        wake any waiting reservers.
 *
 * @param count Number of pages to return.
 * @note May be called from any context; sPageDeficitLock is acquired internally
 *       only when there are unsatisfied reservations.
 */
static inline void
unreserve_pages(uint32 count)
{
	atomic_add(&sUnreservedFreePages, count);
	if (atomic_get(&sUnsatisfiedPageReservations) != 0) {
		MutexLocker pageDeficitLocker(sPageDeficitLock);
		wake_up_page_reservation_waiters();
	}
}


/**
 * @brief Return a page to the free or clear queue, removing it from its current queue.
 *
 * Moves the page from whatever queue it currently belongs to into either the
 * clear queue (if \a clear is true) or the free queue, and notifies waiters on
 * the free-page condition variable.
 *
 * @param page  The page to free; must not be mapped and must not have a cache.
 * @param clear If true, place the page on the clear queue; otherwise the free queue.
 * @note The page's cache (if any) must have been removed before this call.
 *       Caller must hold DEBUG_PAGE_ACCESS on the page.
 *       sFreePageQueuesLock is acquired internally (read lock).
 */
static void
free_page(vm_page* page, bool clear)
{
	DEBUG_PAGE_ACCESS_CHECK(page);

	PAGE_ASSERT(page, !page->IsMapped());

	VMPageQueue* fromQueue;

	switch (page->State()) {
		case PAGE_STATE_ACTIVE:
			fromQueue = &sActivePageQueue;
			break;
		case PAGE_STATE_INACTIVE:
			fromQueue = &sInactivePageQueue;
			break;
		case PAGE_STATE_MODIFIED:
			fromQueue = &sModifiedPageQueue;
			break;
		case PAGE_STATE_CACHED:
			fromQueue = &sCachedPageQueue;
			break;
		case PAGE_STATE_FREE:
		case PAGE_STATE_CLEAR:
			panic("free_page(): page %p already free", page);
			return;
		case PAGE_STATE_WIRED:
		case PAGE_STATE_UNUSED:
			fromQueue = NULL;
			break;
		default:
			panic("free_page(): page %p in invalid state %d",
				page, page->State());
			return;
	}

	if (page->CacheRef() != NULL)
		panic("to be freed page %p has cache", page);
	if (page->IsMapped())
		panic("to be freed page %p has mappings", page);

	if (fromQueue != NULL)
		fromQueue->RemoveUnlocked(page);

	TA(FreePage(page->physical_page_number));

#if VM_PAGE_ALLOCATION_TRACKING_AVAILABLE
	page->allocation_tracking_info.Clear();
#endif

	ReadLocker locker(sFreePageQueuesLock);

	DEBUG_PAGE_ACCESS_END(page);

	if (clear) {
		page->SetState(PAGE_STATE_CLEAR);
		sClearPageQueue.PrependUnlocked(page);
	} else {
		page->SetState(PAGE_STATE_FREE);
		sFreePageQueue.PrependUnlocked(page);
		sFreePageCondition.NotifyAll();
	}

	locker.Unlock();
}


/*!	The caller must make sure that no-one else tries to change the page's state
	while the function is called. If the page has a cache, this can be done by
	locking the cache.
*/
/**
 * @brief Move a page between page queues according to the requested new state.
 *
 * Removes the page from its current queue and appends it to the queue
 * corresponding to \a pageState.  Also maintains sModifiedTemporaryPages for
 * temporary-cache pages that transition to/from PAGE_STATE_MODIFIED.
 *
 * @param page      The page to move; must not be in a free or clear state.
 * @param pageState Target PAGE_STATE_* value; must not be FREE or CLEAR.
 * @note If the page has a cache, the cache must be locked by the caller.
 *       Must not be called with sFreePageQueuesLock held.
 */
static void
set_page_state(vm_page *page, int pageState)
{
	DEBUG_PAGE_ACCESS_CHECK(page);

	if (pageState == page->State())
		return;

	VMPageQueue* fromQueue;

	switch (page->State()) {
		case PAGE_STATE_ACTIVE:
			fromQueue = &sActivePageQueue;
			break;
		case PAGE_STATE_INACTIVE:
			fromQueue = &sInactivePageQueue;
			break;
		case PAGE_STATE_MODIFIED:
			fromQueue = &sModifiedPageQueue;
			break;
		case PAGE_STATE_CACHED:
			fromQueue = &sCachedPageQueue;
			break;
		case PAGE_STATE_FREE:
		case PAGE_STATE_CLEAR:
			panic("set_page_state(): page %p is free/clear", page);
			return;
		case PAGE_STATE_WIRED:
		case PAGE_STATE_UNUSED:
			fromQueue = NULL;
			break;
		default:
			panic("set_page_state(): page %p in invalid state %d",
				page, page->State());
			return;
	}

	VMPageQueue* toQueue;

	switch (pageState) {
		case PAGE_STATE_ACTIVE:
			toQueue = &sActivePageQueue;
			break;
		case PAGE_STATE_INACTIVE:
			toQueue = &sInactivePageQueue;
			break;
		case PAGE_STATE_MODIFIED:
			toQueue = &sModifiedPageQueue;
			break;
		case PAGE_STATE_CACHED:
			PAGE_ASSERT(page, !page->IsMapped());
			PAGE_ASSERT(page, !page->modified);
			toQueue = &sCachedPageQueue;
			break;
		case PAGE_STATE_FREE:
		case PAGE_STATE_CLEAR:
			panic("set_page_state(): target state is free/clear");
			return;
		case PAGE_STATE_WIRED:
		case PAGE_STATE_UNUSED:
			toQueue = NULL;
			break;
		default:
			panic("set_page_state(): invalid target state %d", pageState);
			return;
	}

	VMCache* cache = page->Cache();
	if (cache != NULL && cache->temporary) {
		if (pageState == PAGE_STATE_MODIFIED)
			atomic_add(&sModifiedTemporaryPages, 1);
		else if (page->State() == PAGE_STATE_MODIFIED)
			atomic_add(&sModifiedTemporaryPages, -1);
	}

	// move the page
	if (toQueue == fromQueue) {
		// Note: Theoretically we are required to lock when changing the page
		// state, even if we don't change the queue. We actually don't have to
		// do this, though, since only for the active queue there are different
		// page states and active pages have a cache that must be locked at
		// this point. So we rely on the fact that everyone must lock the cache
		// before trying to change/interpret the page state.
		PAGE_ASSERT(page, cache != NULL);
		cache->AssertLocked();
		page->SetState(pageState);
	} else {
		if (fromQueue != NULL)
			fromQueue->RemoveUnlocked(page);

		page->SetState(pageState);

		if (toQueue != NULL)
			toQueue->AppendUnlocked(page);
	}
}


/*! Moves a previously modified page into a now appropriate queue.
	The page queues must not be locked.
*/
/**
 * @brief Place a page that was previously modified into the most appropriate queue.
 *
 * Chooses active (if mapped), modified (if dirty), or cached (if clean and
 * unmapped) and calls set_page_state() accordingly.
 *
 * @param page The page to requeue; must not be in the free or clear state.
 * @note Page queues must not be locked. The page's cache must be locked.
 */
static void
move_page_to_appropriate_queue(vm_page *page)
{
	DEBUG_PAGE_ACCESS_CHECK(page);

	// Note, this logic must be in sync with what the page daemon does.
	int32 state;
	if (page->IsMapped())
		state = PAGE_STATE_ACTIVE;
	else if (page->modified)
		state = PAGE_STATE_MODIFIED;
	else
		state = PAGE_STATE_CACHED;

// TODO: If free + cached pages are low, we might directly want to free the
// page.
	set_page_state(page, state);
}


/**
 * @brief Zero-fill the physical memory backing a page.
 *
 * @param page The page to clear; its physical_page_number must be valid.
 * @note May be called from a normal kernel thread context. Does not lock any
 *       page queue.
 */
static void
clear_page(struct vm_page *page)
{
	vm_memset_physical(page->physical_page_number << PAGE_SHIFT, 0,
		B_PAGE_SIZE);
}


/**
 * @brief Mark a range of physical pages as in-use (wired or unused) during early boot.
 *
 * Removes pages from the free/clear queues and sets their state to
 * PAGE_STATE_WIRED or PAGE_STATE_UNUSED.  Intended only for early boot
 * when the page reservation policy is not yet enforced.
 *
 * @param startPage  Physical page number of the first page to mark.
 * @param length     Number of pages to mark.
 * @param wired      If true, pages are set to PAGE_STATE_WIRED; otherwise PAGE_STATE_UNUSED.
 * @retval B_OK      Always (range clamping is performed silently).
 * @note Must only be called during kernel startup (gKernelStartup == true).
 *       Acquires sFreePageQueuesLock (write) internally.
 */
static status_t
mark_page_range_in_use(page_num_t startPage, page_num_t length, bool wired)
{
	TRACE(("mark_page_range_in_use: start %#" B_PRIxPHYSADDR ", len %#"
		B_PRIxPHYSADDR "\n", startPage, length));

	if (sPhysicalPageOffset > startPage) {
		dprintf("mark_page_range_in_use(%#" B_PRIxPHYSADDR ", %#" B_PRIxPHYSADDR
			"): start page is before free list\n", startPage, length);
		if (sPhysicalPageOffset - startPage >= length)
			return B_OK;
		length -= sPhysicalPageOffset - startPage;
		startPage = sPhysicalPageOffset;
	}

	startPage -= sPhysicalPageOffset;

	if (startPage + length > sNumPages) {
		dprintf("mark_page_range_in_use(%#" B_PRIxPHYSADDR ", %#" B_PRIxPHYSADDR
			"): range would extend past free list\n", startPage, length);
		if (startPage >= sNumPages)
			return B_OK;
		length = sNumPages - startPage;
	}

	WriteLocker locker(sFreePageQueuesLock);

	for (page_num_t i = 0; i < length; i++) {
		vm_page *page = &sPages[startPage + i];
		switch (page->State()) {
			case PAGE_STATE_FREE:
			case PAGE_STATE_CLEAR:
			{
				// This violates the page reservation policy, since we remove pages
				// from the free/clear queues without having reserved them before.
				// This should happen in the early boot process only, though.
				ASSERT(gKernelStartup);

				DEBUG_PAGE_ACCESS_START(page);
				VMPageQueue& queue = page->State() == PAGE_STATE_FREE
					? sFreePageQueue : sClearPageQueue;
				queue.Remove(page);
				page->SetState(wired ? PAGE_STATE_WIRED : PAGE_STATE_UNUSED);
				page->busy = false;
				atomic_add(&sUnreservedFreePages, -1);
				DEBUG_PAGE_ACCESS_END(page);
				break;
			}
			case PAGE_STATE_WIRED:
			case PAGE_STATE_UNUSED:
				break;
			case PAGE_STATE_ACTIVE:
			case PAGE_STATE_INACTIVE:
			case PAGE_STATE_MODIFIED:
			case PAGE_STATE_CACHED:
			default:
				// uh
				panic("mark_page_range_in_use: page %#" B_PRIxPHYSADDR
					" in non-free state %d!\n", startPage + i, page->State());
				break;
		}
	}

	return B_OK;
}


/*!
	This is a background thread that wakes up when its condition is notified
	and moves some pages from the free queue over to the clear queue.
	Given enough time, it will clear out all pages from the free queue - we
	could probably slow it down after having reached a certain threshold.
*/
/**
 * @brief Kernel thread: continuously move pages from the free queue to the clear queue.
 *
 * Wakes on sFreePageCondition, takes up to SCRUB_SIZE pages from sFreePageQueue,
 * zero-fills them, and places them onto sClearPageQueue.  Sleeps at least
 * 100 ms between runs.
 *
 * @param unused Ignored thread argument.
 * @return Never returns (infinite loop); declared B_OK for signature compatibility.
 * @note Runs as a dedicated kernel thread at B_LOWEST_ACTIVE_PRIORITY.
 */
static int32
page_scrubber(void *unused)
{
	(void)(unused);

	TRACE(("page_scrubber starting...\n"));

	ConditionVariableEntry entry;
	for (;;) {
		while (sFreePageQueue.Count() == 0
				|| atomic_get(&sUnreservedFreePages)
					< (int32)sFreePagesTarget) {
			sFreePageCondition.Add(&entry);
			entry.Wait();
		}

		// Since we temporarily remove pages from the free pages reserve,
		// we must make sure we don't cause a violation of the page
		// reservation warranty. The following is usually stricter than
		// necessary, because we don't have information on how many of the
		// reserved pages have already been allocated.
		int32 reserved = reserve_some_pages(SCRUB_SIZE,
			kPageReserveForPriority[VM_PRIORITY_USER]);
		if (reserved == 0)
			continue;

		// get some pages from the free queue, mostly sorted
		ReadLocker locker(sFreePageQueuesLock);

		vm_page *page[SCRUB_SIZE];
		int32 scrubCount = 0;
		for (int32 i = 0; i < reserved; i++) {
			page[i] = sFreePageQueue.RemoveHeadUnlocked();
			if (page[i] == NULL)
				break;

			DEBUG_PAGE_ACCESS_START(page[i]);

			page[i]->SetState(PAGE_STATE_ACTIVE);
			page[i]->busy = true;
			scrubCount++;
		}

		locker.Unlock();

		if (scrubCount == 0) {
			unreserve_pages(reserved);
			continue;
		}

		TA(ScrubbingPages(scrubCount));

		// clear them
		for (int32 i = 0; i < scrubCount; i++)
			clear_page(page[i]);

		locker.Lock();

		// and put them into the clear queue
		// process the array reversed when prepending to preserve sequential order
		for (int32 i = scrubCount - 1; i >= 0; i--) {
			page[i]->SetState(PAGE_STATE_CLEAR);
			page[i]->busy = false;
			DEBUG_PAGE_ACCESS_END(page[i]);
			sClearPageQueue.PrependUnlocked(page[i]);
		}

		locker.Unlock();

		unreserve_pages(reserved);

		TA(ScrubbedPages(scrubCount));

		// wait at least 100ms between runs
		snooze(100 * 1000);
	}

	return 0;
}


/**
 * @brief Initialize a dummy marker page for use as a cursor in page queue scans.
 *
 * Sets up a vm_page with no cache, state UNUSED, busy=true, so it can be
 * inserted into a queue as a placeholder without being mistaken for a real page.
 *
 * @param marker Reference to the vm_page to initialize as a marker.
 */
static void
init_page_marker(vm_page &marker)
{
	marker.SetCacheRef(NULL);
	marker.InitState(PAGE_STATE_UNUSED);
	marker.busy = true;
#if DEBUG_PAGE_QUEUE
	marker.queue = NULL;
#endif
#if DEBUG_PAGE_ACCESS
	marker.accessing_thread = thread_get_current_thread_id();
#endif
}


/**
 * @brief Remove a marker page from whatever queue it currently occupies and reset its state.
 *
 * @param marker Reference to the marker vm_page previously inserted by init_page_marker().
 * @note The marker's queue spinlock is acquired internally via RemoveUnlocked().
 */
static void
remove_page_marker(struct vm_page &marker)
{
	DEBUG_PAGE_ACCESS_CHECK(&marker);

	if (marker.State() < PAGE_STATE_FIRST_UNQUEUED)
		sPageQueues[marker.State()].RemoveUnlocked(&marker);

	marker.SetState(PAGE_STATE_UNUSED);
}


/**
 * @brief Return the next non-busy page from the modified queue, rotating it to the tail.
 *
 * Peeks at the head of sModifiedPageQueue, requeues it to the tail (so the
 * next call advances), decrements \a maxPagesToSee, and returns the page if
 * it is not busy.
 *
 * @param maxPagesToSee In/out counter of how many pages may still be examined;
 *                      decremented on each iteration.
 * @return Pointer to the next non-busy modified page, or NULL when the queue
 *         is exhausted or \a maxPagesToSee reaches zero.
 * @note Acquires sModifiedPageQueue's spinlock internally.
 */
static vm_page*
next_modified_page(page_num_t& maxPagesToSee)
{
	InterruptsSpinLocker locker(sModifiedPageQueue.GetLock());

	while (maxPagesToSee > 0) {
		vm_page* page = sModifiedPageQueue.Head();
		if (page == NULL)
			return NULL;

		sModifiedPageQueue.Requeue(page, true);

		maxPagesToSee--;

		if (!page->busy)
			return page;
	}

	return NULL;
}


// #pragma mark -


class PageWriteTransfer;
class PageWriteWrapper;


class PageWriterRun {
public:
	status_t Init(uint32 maxPages);

	void PrepareNextRun();
	void AddPage(vm_page* page);
	uint32 Go();

	void PageWritten(PageWriteTransfer* transfer, status_t status,
		bool partialTransfer, size_t bytesTransferred);

private:
	uint32				fMaxPages;
	uint32				fWrapperCount;
	uint32				fTransferCount;
	int32				fPendingTransfers;
	PageWriteWrapper*	fWrappers;
	PageWriteTransfer*	fTransfers;
	ConditionVariable	fAllFinishedCondition;
};


class PageWriteTransfer : public AsyncIOCallback {
public:
	void SetTo(PageWriterRun* run, vm_page* page, int32 maxPages);
	bool AddPage(vm_page* page);

	status_t Schedule(uint32 flags);

	void SetStatus(status_t status, size_t transferred);

	status_t Status() const	{ return fStatus; }
	struct VMCache* Cache() const { return fCache; }
	uint32 PageCount() const { return fPageCount; }

	virtual void IOFinished(status_t status, bool partialTransfer,
		generic_size_t bytesTransferred);

private:
	PageWriterRun*		fRun;
	struct VMCache*		fCache;
	off_t				fOffset;
	uint32				fPageCount;
	int32				fMaxPages;
	status_t			fStatus;
	uint32				fVecCount;
	generic_io_vec		fVecs[32]; // TODO: make dynamic/configurable
};


class PageWriteWrapper {
public:
	PageWriteWrapper();
	~PageWriteWrapper();
	void SetTo(vm_page* page);
	bool Done(status_t result);

private:
	vm_page*			fPage;
	struct VMCache*		fCache;
	bool				fIsActive;
};


PageWriteWrapper::PageWriteWrapper()
	:
	fIsActive(false)
{
}


PageWriteWrapper::~PageWriteWrapper()
{
	if (fIsActive)
		panic("page write wrapper going out of scope but isn't completed");
}


/*!	The page's cache must be locked.
*/
/**
 * @brief Prepare a page for writeback: mark it busy and clear the dirty (modified) mapping flag.
 *
 * Associates this wrapper with \a page, sets page->busy and page->busy_io,
 * and clears the PAGE_MODIFIED mapping flag so subsequent writes to the page
 * can be detected.
 *
 * @param page The modified page to prepare; must not already be busy.
 * @note The page's cache must be locked by the caller.
 */
void
PageWriteWrapper::SetTo(vm_page* page)
{
	DEBUG_PAGE_ACCESS_CHECK(page);

	if (page->busy)
		panic("setting page write wrapper to busy page");

	if (fIsActive)
		panic("re-setting page write wrapper that isn't completed");

	fPage = page;
	fCache = page->Cache();
	fIsActive = true;

	fPage->busy = true;
	fPage->busy_io = true;

	// We have a modified page -- however, while we're writing it back,
	// the page might still be mapped. In order not to lose any changes to the
	// page, we mark it clean before actually writing it back; if
	// writing the page fails for some reason, we'll just keep it in the
	// modified page list, but that should happen only rarely.

	// If the page is changed after we cleared the dirty flag, but before we
	// had the chance to write it back, then we'll write it again later -- that
	// will probably not happen that often, though.

	vm_clear_map_flags(fPage, PAGE_MODIFIED);
}


/*!	The page's cache must be locked.
	The page queues must not be locked.
	\return \c true if the page was written successfully respectively could be
		handled somehow, \c false otherwise.
*/
/**
 * @brief Finalize a page writeback: clear busy flags and move the page to an appropriate queue.
 *
 * On success the page is moved to the active or inactive queue via
 * move_page_to_appropriate_queue().  On failure the page is marked modified
 * and moved to a non-modified queue to avoid infinite retry loops.
 * If busy_io was cleared externally the cache's FreeRemovedPage() is called.
 *
 * @param result B_OK if the I/O succeeded, an error code otherwise.
 * @return true if the page was handled successfully, false if writeback failed.
 * @note The page's cache must be locked by the caller.
 *       Page queues must not be locked.
 */
bool
PageWriteWrapper::Done(status_t result)
{
	if (!fIsActive)
		panic("completing page write wrapper that is not active");

	DEBUG_PAGE_ACCESS_START(fPage);

	fPage->busy = false;
		// Set unbusy and notify later by hand.

	bool success = true;

	if (!fPage->busy_io) {
		// The busy_io flag was cleared. That means the cache tried to remove
		// the page while we were trying to write it. Let the cache handle the rest.
		fCache->FreeRemovedPage(fPage);
	} else if (result == B_OK) {
		// put it into the active/inactive queue
		move_page_to_appropriate_queue(fPage);
		fPage->busy_io = false;
		DEBUG_PAGE_ACCESS_END(fPage);

		fCache->NotifyPageEvents(fPage, PAGE_EVENT_NOT_BUSY);
	} else {
		// Writing the page failed -- mark the page modified and move it to
		// an appropriate queue other than the modified queue, so we don't
		// keep trying to write it over and over again. We keep
		// non-temporary pages in the modified queue, though, so they don't
		// get lost in the inactive queue.
		dprintf("PageWriteWrapper: Failed to write page %p: %s\n", fPage,
			strerror(result));

		fPage->modified = true;
		if (!fCache->temporary)
			set_page_state(fPage, PAGE_STATE_MODIFIED);
		else if (fPage->IsMapped())
			set_page_state(fPage, PAGE_STATE_ACTIVE);
		else
			set_page_state(fPage, PAGE_STATE_INACTIVE);

		fPage->busy_io = false;
		DEBUG_PAGE_ACCESS_END(fPage);

		fCache->NotifyPageEvents(fPage, PAGE_EVENT_NOT_BUSY);
		success = false;
	}

	fIsActive = false;

	return success;
}


/*!	The page's cache must be locked.
*/
/**
 * @brief Initialize this transfer to write the given page from a specific cache.
 *
 * @param run       The owning PageWriterRun, or NULL for a synchronous transfer.
 * @param page      The first page to include in the transfer.
 * @param maxPages  Maximum number of pages this transfer may aggregate (-1 for unlimited).
 * @note The page's cache must be locked by the caller.
 */
void
PageWriteTransfer::SetTo(PageWriterRun* run, vm_page* page, int32 maxPages)
{
	fRun = run;
	fCache = page->Cache();
	fOffset = page->cache_offset;
	fPageCount = 1;
	fMaxPages = maxPages;
	fStatus = B_OK;

	fVecs[0].base = (phys_addr_t)page->physical_page_number << PAGE_SHIFT;
	fVecs[0].length = B_PAGE_SIZE;
	fVecCount = 1;
}


/*!	The page's cache must be locked.
*/
/**
 * @brief Try to append or prepend \a page to this transfer's I/O vector list.
 *
 * Succeeds only if \a page belongs to the same cache, is contiguous in both
 * physical memory and cache offset with the existing run, and the page limit
 * has not been reached.
 *
 * @param page The candidate page to add.
 * @return true if the page was added, false if a new transfer must be started.
 * @note The page's cache must be locked by the caller.
 */
bool
PageWriteTransfer::AddPage(vm_page* page)
{
	if (page->Cache() != fCache
		|| (fMaxPages >= 0 && fPageCount >= (uint32)fMaxPages))
		return false;

	phys_addr_t nextBase = fVecs[fVecCount - 1].base
		+ fVecs[fVecCount - 1].length;

	if ((phys_addr_t)page->physical_page_number << PAGE_SHIFT == nextBase
		&& (off_t)page->cache_offset == fOffset + fPageCount) {
		// append to last iovec
		fVecs[fVecCount - 1].length += B_PAGE_SIZE;
		fPageCount++;
		return true;
	}

	nextBase = fVecs[0].base - B_PAGE_SIZE;
	if ((phys_addr_t)page->physical_page_number << PAGE_SHIFT == nextBase
		&& (off_t)page->cache_offset == fOffset - 1) {
		// prepend to first iovec and adjust offset
		fVecs[0].base = nextBase;
		fVecs[0].length += B_PAGE_SIZE;
		fOffset = page->cache_offset;
		fPageCount++;
		return true;
	}

	if (((off_t)page->cache_offset == fOffset + fPageCount
			|| (off_t)page->cache_offset == fOffset - 1)
		&& fVecCount < sizeof(fVecs) / sizeof(fVecs[0])) {
		// not physically contiguous or not in the right order
		uint32 vectorIndex;
		if ((off_t)page->cache_offset < fOffset) {
			// we are pre-pending another vector, move the other vecs
			for (uint32 i = fVecCount; i > 0; i--)
				fVecs[i] = fVecs[i - 1];

			fOffset = page->cache_offset;
			vectorIndex = 0;
		} else
			vectorIndex = fVecCount;

		fVecs[vectorIndex].base
			= (phys_addr_t)page->physical_page_number << PAGE_SHIFT;
		fVecs[vectorIndex].length = B_PAGE_SIZE;

		fVecCount++;
		fPageCount++;
		return true;
	}

	return false;
}


/**
 * @brief Submit this transfer's I/O vectors to the cache for writing.
 *
 * For asynchronous runs delegates to VMCache::WriteAsync(); for synchronous
 * use calls VMCache::Write() and updates the status immediately.
 *
 * @param flags Additional I/O flags (e.g. B_VIP_IO_REQUEST).
 * @retval B_OK          I/O was scheduled (async) or completed successfully (sync).
 * @retval B_ERROR       Fewer bytes than required were transferred (sync).
 * @retval other errors  Forwarded from the cache write implementation.
 */
status_t
PageWriteTransfer::Schedule(uint32 flags)
{
	off_t writeOffset = (off_t)fOffset << PAGE_SHIFT;
	generic_size_t writeLength = (phys_size_t)fPageCount << PAGE_SHIFT;

	if (fRun != NULL) {
		return fCache->WriteAsync(writeOffset, fVecs, fVecCount, writeLength,
			flags | B_PHYSICAL_IO_REQUEST, this);
	}

	status_t status = fCache->Write(writeOffset, fVecs, fVecCount,
		flags | B_PHYSICAL_IO_REQUEST, &writeLength);

	SetStatus(status, writeLength);
	return fStatus;
}


/**
 * @brief Update the transfer's status, treating a short write as B_ERROR.
 *
 * A transfer is considered successful only if at least the last page has been
 * partially written (i.e. transferred > (pageCount - 1) * PAGE_SIZE).
 *
 * @param status     Raw I/O status from the cache.
 * @param transferred Number of bytes reported as transferred.
 */
void
PageWriteTransfer::SetStatus(status_t status, size_t transferred)
{
	// only succeed if all pages up to the last one have been written fully
	// and the last page has at least been written partially
	if (status == B_OK && transferred <= (fPageCount - 1) * B_PAGE_SIZE)
		status = B_ERROR;

	fStatus = status;
}


/**
 * @brief AsyncIOCallback implementation: called by the I/O subsystem when this transfer completes.
 *
 * Updates the transfer status and notifies the owning PageWriterRun.
 *
 * @param status           Final I/O status.
 * @param partialTransfer  true if not all bytes were transferred.
 * @param bytesTransferred Actual number of bytes transferred.
 */
void
PageWriteTransfer::IOFinished(status_t status, bool partialTransfer,
	generic_size_t bytesTransferred)
{
	SetStatus(status, bytesTransferred);
	fRun->PageWritten(this, fStatus, partialTransfer, bytesTransferred);
}


/**
 * @brief Allocate per-run wrapper and transfer arrays for up to \a maxPages pages.
 *
 * @param maxPages Maximum number of pages that can be batched in a single run.
 * @retval B_OK        Arrays allocated successfully.
 * @retval B_NO_MEMORY Heap allocation failed.
 */
status_t
PageWriterRun::Init(uint32 maxPages)
{
	fMaxPages = maxPages;
	fWrapperCount = 0;
	fTransferCount = 0;
	fPendingTransfers = 0;

	fWrappers = new(std::nothrow) PageWriteWrapper[maxPages];
	fTransfers = new(std::nothrow) PageWriteTransfer[maxPages];
	if (fWrappers == NULL || fTransfers == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


/**
 * @brief Reset wrapper and transfer counters so this run object can be reused.
 *
 * Does not free the underlying arrays; call Init() for that.
 */
void
PageWriterRun::PrepareNextRun()
{
	fWrapperCount = 0;
	fTransferCount = 0;
	fPendingTransfers = 0;
}


/*!	The page's cache must be locked.
*/
/**
 * @brief Add a modified page to this run's wrapper and transfer lists.
 *
 * Appends the page to the next available PageWriteWrapper and either extends
 * the current PageWriteTransfer or starts a new one.
 *
 * @param page Modified page to schedule for write; must be locked (cache held).
 * @note The page's cache must be locked by the caller.
 */
void
PageWriterRun::AddPage(vm_page* page)
{
	fWrappers[fWrapperCount++].SetTo(page);

	if (fTransferCount == 0 || !fTransfers[fTransferCount - 1].AddPage(page)) {
		fTransfers[fTransferCount++].SetTo(this, page,
			page->Cache()->MaxPagesPerAsyncWrite());
	}
}


/*!	Writes all pages previously added.
	\return The number of pages that could not be written or otherwise handled.
*/
/**
 * @brief Dispatch all pending transfers asynchronously and wait for completion.
 *
 * Schedules every transfer with B_VIP_IO_REQUEST, then blocks until all
 * async callbacks have fired.  Calls Done() on each wrapper to move pages to
 * their post-write queues, and releases cache store/ref counts.
 *
 * @return Number of pages that could not be written or otherwise handled.
 * @note Blocks the calling thread until all I/O completes.
 */
uint32
PageWriterRun::Go()
{
	atomic_set(&fPendingTransfers, fTransferCount);

	fAllFinishedCondition.Init(this, "page writer wait for I/O");
	ConditionVariableEntry waitEntry;
	fAllFinishedCondition.Add(&waitEntry);

	// schedule writes
	for (uint32 i = 0; i < fTransferCount; i++)
		fTransfers[i].Schedule(B_VIP_IO_REQUEST);

	// wait until all pages have been written
	waitEntry.Wait();

	// mark pages depending on whether they could be written or not

	uint32 failedPages = 0;
	uint32 wrapperIndex = 0;
	for (uint32 i = 0; i < fTransferCount; i++) {
		PageWriteTransfer& transfer = fTransfers[i];
		transfer.Cache()->Lock();

		for (uint32 j = 0; j < transfer.PageCount(); j++) {
			if (!fWrappers[wrapperIndex++].Done(transfer.Status()))
				failedPages++;
		}

		transfer.Cache()->Unlock();
	}

	ASSERT(wrapperIndex == fWrapperCount);

	for (uint32 i = 0; i < fTransferCount; i++) {
		PageWriteTransfer& transfer = fTransfers[i];
		struct VMCache* cache = transfer.Cache();

		// We've acquired a references for each page
		for (uint32 j = 0; j < transfer.PageCount(); j++) {
			// We release the cache references after all pages were made
			// unbusy again - otherwise releasing a vnode could deadlock.
			cache->ReleaseStoreRef();
			cache->ReleaseRef();
		}
	}

	return failedPages;
}


/**
 * @brief Called by a PageWriteTransfer when its async I/O completes.
 *
 * Decrements the pending-transfer counter and signals fAllFinishedCondition
 * when the last transfer has finished.
 *
 * @param transfer        The transfer that finished.
 * @param status          Final I/O status of the transfer.
 * @param partialTransfer Whether the transfer was only partially completed.
 * @param bytesTransferred Actual number of bytes transferred.
 */
void
PageWriterRun::PageWritten(PageWriteTransfer* transfer, status_t status,
	bool partialTransfer, size_t bytesTransferred)
{
	if (atomic_add(&fPendingTransfers, -1) == 1)
		fAllFinishedCondition.NotifyAll();
}


/*!	The page writer continuously takes some pages from the modified
	queue, writes them back, and moves them back to the active queue.
	It runs in its own thread, and is only there to keep the number
	of modified pages low, so that more pages can be reused with
	fewer costs.
*/
/**
 * @brief Kernel thread: drain the modified page queue by writing pages to their backing stores.
 *
 * Sleeps on sPageWriterCondition (or at most 3 seconds), collects up to 256
 * modified pages per run, adjusts I/O priority based on memory pressure, and
 * calls PageWriterRun::Go() to flush them.  Skips wired pages and, unless
 * actively paging, also skips temporary-cache pages.
 *
 * @param unused Ignored thread argument.
 * @retval B_OK Never; returns only on fatal init failure.
 * @note Runs as a dedicated kernel thread at B_NORMAL_PRIORITY + 1.
 *       Must not be called from any other context.
 */
status_t
page_writer(void* /*unused*/)
{
	const uint32 kNumPages = 256;
#ifdef TRACE_VM_PAGE
	uint32 writtenPages = 0;
	bigtime_t lastWrittenTime = 0;
	bigtime_t pageCollectionTime = 0;
	bigtime_t pageWritingTime = 0;
#endif

	PageWriterRun run;
	if (run.Init(kNumPages) != B_OK) {
		panic("page writer: Failed to init PageWriterRun!");
		return B_ERROR;
	}

	page_num_t pagesSinceLastSuccessfulWrite = 0;

	while (true) {
// TODO: Maybe wait shorter when memory is low!
		if (sModifiedPageQueue.Count() < kNumPages) {
			sPageWriterCondition.Wait(3000000, true);
				// all 3 seconds when no one triggers us
		}

		page_num_t modifiedPages = sModifiedPageQueue.Count();
		if (modifiedPages == 0)
			continue;

		if (modifiedPages <= pagesSinceLastSuccessfulWrite) {
			// We ran through the whole queue without being able to write a
			// single page. Take a break.
			snooze(500000);
			pagesSinceLastSuccessfulWrite = 0;
		}

#if ENABLE_SWAP_SUPPORT
		page_stats pageStats;
		get_page_stats(pageStats);
		const bool activePaging = do_active_paging(pageStats);
#endif

		// depending on how urgent it becomes to get pages to disk, we adjust
		// our I/O priority
		uint32 lowPagesState = low_resource_state(B_KERNEL_RESOURCE_PAGES);
		int32 ioPriority = B_IDLE_PRIORITY;
		if (lowPagesState >= B_LOW_RESOURCE_CRITICAL
			|| modifiedPages > MAX_PAGE_WRITER_IO_PRIORITY_THRESHOLD) {
			ioPriority = MAX_PAGE_WRITER_IO_PRIORITY;
		} else {
			ioPriority = (uint64)MAX_PAGE_WRITER_IO_PRIORITY * modifiedPages
				/ MAX_PAGE_WRITER_IO_PRIORITY_THRESHOLD;
		}

		thread_set_io_priority(ioPriority);

		uint32 numPages = 0;
		run.PrepareNextRun();

		// TODO: make this laptop friendly, too (ie. only start doing
		// something if someone else did something or there is really
		// enough to do).

		// collect pages to be written
#ifdef TRACE_VM_PAGE
		pageCollectionTime -= system_time();
#endif

		page_num_t maxPagesToSee = modifiedPages;

		while (numPages < kNumPages && maxPagesToSee > 0) {
			vm_page *page = next_modified_page(maxPagesToSee);
			if (page == NULL)
				break;

			PageCacheLocker cacheLocker(page, false);
			if (!cacheLocker.IsLocked())
				continue;

			VMCache *cache = page->Cache();

			// If the page is busy or its state has changed while we were
			// locking the cache, just ignore it.
			if (page->busy || page->State() != PAGE_STATE_MODIFIED)
				continue;

			DEBUG_PAGE_ACCESS_START(page);

			// Don't write back wired (locked) pages.
			if (page->WiredCount() > 0) {
				set_page_state(page, PAGE_STATE_ACTIVE);
				DEBUG_PAGE_ACCESS_END(page);
				continue;
			}

			// Write back temporary pages only when we're actively paging.
			if (cache->temporary
#if ENABLE_SWAP_SUPPORT
				&& (!activePaging
					|| !cache->CanWritePage(
							(off_t)page->cache_offset << PAGE_SHIFT))
#endif
				) {
				// We can't/don't want to do anything with this page, so move it
				// to one of the other queues.
				if (page->mappings.IsEmpty())
					set_page_state(page, PAGE_STATE_INACTIVE);
				else
					set_page_state(page, PAGE_STATE_ACTIVE);

				DEBUG_PAGE_ACCESS_END(page);
				continue;
			}

			// We need our own reference to the store, as it might currently be
			// destroyed.
			if (cache->AcquireUnreferencedStoreRef() != B_OK) {
				DEBUG_PAGE_ACCESS_END(page);
				cacheLocker.Unlock();
				thread_yield();
				continue;
			}

			run.AddPage(page);
				// TODO: We're possibly adding pages of different caches and
				// thus maybe of different underlying file systems here. This
				// is a potential problem for loop file systems/devices, since
				// we could mark a page busy that would need to be accessed
				// when writing back another page, thus causing a deadlock.

			DEBUG_PAGE_ACCESS_END(page);

			//dprintf("write page %p, cache %p (%ld)\n", page, page->cache, page->cache->ref_count);
			TPW(WritePage(page));

			cache->AcquireRefLocked();
			numPages++;

			// Write adjacent pages at the same time, if they're also modified.
			if (cache->temporary)
				continue;
			while (page->cache_next != NULL && numPages < kNumPages) {
				page = page->cache_next;
				if (page->busy || page->State() != PAGE_STATE_MODIFIED)
					break;
				if (page->WiredCount() > 0)
					break;

				DEBUG_PAGE_ACCESS_START(page);
				sModifiedPageQueue.RequeueUnlocked(page, true);
				run.AddPage(page);
				DEBUG_PAGE_ACCESS_END(page);

				cache->AcquireStoreRef();
				cache->AcquireRefLocked();
				numPages++;
				if (maxPagesToSee > 0)
					maxPagesToSee--;
			}
		}

#ifdef TRACE_VM_PAGE
		pageCollectionTime += system_time();
#endif
		if (numPages == 0)
			continue;

		// write pages to disk and do all the cleanup
#ifdef TRACE_VM_PAGE
		pageWritingTime -= system_time();
#endif
		uint32 failedPages = run.Go();
#ifdef TRACE_VM_PAGE
		pageWritingTime += system_time();

		// debug output only...
		writtenPages += numPages;
		if (writtenPages >= 1024) {
			bigtime_t now = system_time();
			TRACE(("page writer: wrote 1024 pages (total: %" B_PRIu64 " ms, "
				"collect: %" B_PRIu64 " ms, write: %" B_PRIu64 " ms)\n",
				(now - lastWrittenTime) / 1000,
				pageCollectionTime / 1000, pageWritingTime / 1000));
			lastWrittenTime = now;

			writtenPages -= 1024;
			pageCollectionTime = 0;
			pageWritingTime = 0;
		}
#endif

		if (failedPages == numPages)
			pagesSinceLastSuccessfulWrite += modifiedPages - maxPagesToSee;
		else
			pagesSinceLastSuccessfulWrite = 0;
	}

	return B_OK;
}


// #pragma mark -


#if ENABLE_SWAP_SUPPORT
/**
 * @brief Free swap space associated with an active page if possible.
 *
 * Attempts to free the swap slot backing \a page in its temporary cache.
 * If successful, marks the page modified so its data is not lost on reclaim.
 *
 * @param page An active page belonging to a temporary cache.
 * @return true if swap space was freed, false otherwise.
 * @note The page's cache must be locked. The page must be in PAGE_STATE_ACTIVE.
 */
static bool
free_page_swap_space(vm_page *page)
{
	DEBUG_PAGE_ACCESS_CHECK(page);
	PAGE_ASSERT(page, page->State() == PAGE_STATE_ACTIVE);

	VMCache* cache = page->Cache();
	if (cache->temporary && cache->StoreHasPage(page->cache_offset << PAGE_SHIFT)) {
		if (swap_free_page_swap_space(page)) {
			// We need to mark the page modified, since otherwise it could be
			// stolen and we'd lose its data.
			page->modified = true;
			TD(FreedPageSwap(page));
			return true;
		}
	}
	return false;
}
#endif


/**
 * @brief Find the next non-busy cached page, advancing the marker cursor.
 *
 * The \a marker must have been initialised with init_page_marker() before the
 * first call.  On each call the marker is advanced past the last returned page.
 *
 * @param marker A cursor marker page previously set up by init_page_marker().
 * @return Pointer to the next candidate cached page, or NULL if none remain.
 * @note Acquires sCachedPageQueue's spinlock internally (InterruptsSpinLocker).
 */
static vm_page *
find_cached_page_candidate(struct vm_page &marker)
{
	DEBUG_PAGE_ACCESS_CHECK(&marker);

	InterruptsSpinLocker locker(sCachedPageQueue.GetLock());
	vm_page *page;

	if (marker.State() == PAGE_STATE_UNUSED) {
		// Get the first free pages of the (in)active queue
		page = sCachedPageQueue.Head();
	} else {
		// Get the next page of the current queue
		if (marker.State() != PAGE_STATE_CACHED) {
			panic("invalid marker %p state", &marker);
			return NULL;
		}

		page = sCachedPageQueue.Next(&marker);
		sCachedPageQueue.Remove(&marker);
		marker.SetState(PAGE_STATE_UNUSED);
	}

	while (page != NULL) {
		if (!page->busy) {
			// we found a candidate, insert marker
			marker.SetState(PAGE_STATE_CACHED);
			sCachedPageQueue.InsertAfter(page, &marker);
			return page;
		}

		page = sCachedPageQueue.Next(page);
	}

	return NULL;
}


/**
 * @brief Steal a single cached page by locking its cache and removing it.
 *
 * Acquires the page's cache lock, verifies the page is still a clean cached
 * candidate, removes it from the cache and from sCachedPageQueue.
 *
 * @param page      A candidate page returned by find_cached_page_candidate().
 * @param dontWait  If true, return false immediately rather than blocking on
 *                  the cache lock.
 * @return true if the page was successfully stolen (caller owns it), false otherwise.
 * @note On success the caller must dispose of the page (e.g. add to free queue).
 */
static bool
free_cached_page(vm_page *page, bool dontWait)
{
	// try to lock the page's cache
	if (vm_cache_acquire_locked_page_cache(page, dontWait) == NULL)
		return false;
	VMCache* cache = page->Cache();

	AutoLocker<VMCache> cacheLocker(cache, true);
	MethodDeleter<VMCache, void, &VMCache::ReleaseRefLocked> _2(cache);

	// check again if that page is still a candidate
	if (page->busy || page->State() != PAGE_STATE_CACHED)
		return false;

	DEBUG_PAGE_ACCESS_START(page);

	PAGE_ASSERT(page, !page->IsMapped());
	PAGE_ASSERT(page, !page->modified);

	// we can now steal this page

	cache->RemovePage(page);
		// Now the page doesn't have cache anymore, so no one else (e.g.
		// vm_page_allocate_page_run() can pick it up), since they would be
		// required to lock the cache first, which would fail.

	sCachedPageQueue.RemoveUnlocked(page);
	return true;
}


/**
 * @brief Free up to \a pagesToFree cached pages, returning them to the free queue.
 *
 * Iterates sCachedPageQueue using a marker cursor, stealing and freeing up to
 * \a pagesToFree clean cached pages.  Notifies sFreePageCondition on completion.
 *
 * @param pagesToFree Maximum number of pages to free.
 * @param dontWait    Passed through to free_cached_page(); if true, skips pages
 *                    whose cache cannot be locked immediately.
 * @return The number of pages actually freed.
 * @note Page faults are forbidden during this function (forbid_page_faults()).
 */
static uint32
free_cached_pages(uint32 pagesToFree, bool dontWait)
{
	vm_page marker;
	init_page_marker(marker);
	forbid_page_faults();

	uint32 pagesFreed = 0;

	while (pagesFreed < pagesToFree) {
		vm_page *page = find_cached_page_candidate(marker);
		if (page == NULL)
			break;

		if (free_cached_page(page, dontWait)) {
			ReadLocker locker(sFreePageQueuesLock);
			page->SetState(PAGE_STATE_FREE);
			DEBUG_PAGE_ACCESS_END(page);
			sFreePageQueue.PrependUnlocked(page);
			locker.Unlock();

			TA(StolenPage());

			pagesFreed++;
		}
	}

	remove_page_marker(marker);
	permit_page_faults();

	sFreePageCondition.NotifyAll();

	return pagesFreed;
}


/**
 * @brief Perform an idle (low-pressure) scan of the active page queue to age pages.
 *
 * Scans at most Count/kIdleRunsForFullQueue active pages per call, clearing
 * accessed flags and advancing or decrementing usage counts.  Pages whose
 * usage count drops to zero are moved to the inactive queue.  Also frees
 * swap space from accessed temporary pages when swap support is enabled.
 *
 * @param pageStats Current page statistics snapshot (may be updated internally).
 * @note Called from the page daemon thread only.
 */
static void
idle_scan_active_pages(page_stats& pageStats)
{
	VMPageQueue& queue = sActivePageQueue;

	// We want to scan the whole queue in roughly kIdleRunsForFullQueue runs.
	uint32 maxToScan = queue.Count() / kIdleRunsForFullQueue + 1;

	while (maxToScan > 0) {
		maxToScan--;

		// Get the next page. Note that we don't bother to lock here. We go with
		// the assumption that on all architectures reading/writing pointers is
		// atomic. Beyond that it doesn't really matter. We have to unlock the
		// queue anyway to lock the page's cache, and we'll recheck afterwards.
		vm_page* page = queue.Head();
		if (page == NULL)
			break;

		// lock the page's cache
		VMCache* cache = vm_cache_acquire_locked_page_cache(page, true);
		if (cache == NULL)
			continue;

		if (page->State() != PAGE_STATE_ACTIVE) {
			// page is no longer in the cache or in this queue
			cache->ReleaseRefAndUnlock();
			continue;
		}

		if (page->busy) {
			// page is busy -- requeue at the end
			vm_page_requeue(page, true);
			cache->ReleaseRefAndUnlock();
			continue;
		}

		DEBUG_PAGE_ACCESS_START(page);

		// Get the page active/modified flags and update the page's usage count.
		// We completely unmap inactive temporary pages. This saves us to
		// iterate through the inactive list as well, since we'll be notified
		// via page fault whenever such an inactive page is used again.
		// We don't remove the mappings of non-temporary pages, since we
		// wouldn't notice when those would become unused and could thus be
		// moved to the cached list.
		int32 usageCount;
		if (page->WiredCount() > 0 || page->usage_count > 0 || !cache->temporary)
			usageCount = vm_clear_page_mapping_accessed_flags(page);
		else
			usageCount = vm_remove_all_page_mappings_if_unaccessed(page);

		if (usageCount > 0) {
			usageCount += page->usage_count + kPageUsageAdvance;
			if (usageCount > kPageUsageMax)
				usageCount = kPageUsageMax;

#if ENABLE_SWAP_SUPPORT
			free_page_swap_space(page);
#endif
		} else {
			usageCount += page->usage_count - (int32)kPageUsageDecline;
			if (usageCount < 0) {
				usageCount = 0;
				set_page_state(page, PAGE_STATE_INACTIVE);
			}
		}

		page->usage_count = usageCount;

		DEBUG_PAGE_ACCESS_END(page);

		cache->ReleaseRefAndUnlock();
	}
}


/**
 * @brief Perform a full scan of the inactive page queue to reclaim pages under memory pressure.
 *
 * Examines every inactive page and moves unmodified ones to the cached queue,
 * modified ones (up to \a maxToFlush derived from \a despairLevel) to the modified
 * queue, and re-queues others.  Wakes the page writer if pages were sent to the
 * modified queue.
 *
 * @param pageStats   Current page statistics snapshot.
 * @param despairLevel Urgency level (higher = more aggressive flushing).
 * @note Called from the page daemon thread only.
 *       Acquires sInactivePageQueue's spinlock internally.
 */
static void
full_scan_inactive_pages(page_stats& pageStats, int32 despairLevel)
{
	int32 pagesToFree = pageStats.unsatisfiedReservations
		+ sFreeOrCachedPagesTarget
		- (pageStats.totalFreePages + pageStats.cachedPages);
	if (pagesToFree <= 0)
		return;

	bigtime_t time = system_time();
	uint32 pagesScanned = 0;
	uint32 pagesToCached = 0;
	uint32 pagesToModified = 0;
	uint32 pagesToActive = 0;

	// Determine how many pages at maximum to send to the modified queue. Since
	// it is relatively expensive to page out pages, we do that on a grander
	// scale only when things get desperate.
	uint32 maxToFlush = despairLevel <= 1 ? 32 : 10000;

	vm_page marker;
	init_page_marker(marker);

	VMPageQueue& queue = sInactivePageQueue;
	InterruptsSpinLocker queueLocker(queue.GetLock());
	uint32 maxToScan = queue.Count();

	vm_page* nextPage = queue.Head();

	while (pagesToFree > 0 && maxToScan > 0) {
		maxToScan--;

		// get the next page
		vm_page* page = nextPage;
		if (page == NULL)
			break;
		nextPage = queue.Next(page);

		if (page->busy)
			continue;

		// mark the position
		queue.InsertAfter(page, &marker);
		queueLocker.Unlock();

		// lock the page's cache
		VMCache* cache = vm_cache_acquire_locked_page_cache(page, true);
		if (cache == NULL || page->busy
				|| page->State() != PAGE_STATE_INACTIVE) {
			if (cache != NULL)
				cache->ReleaseRefAndUnlock();
			queueLocker.Lock();
			nextPage = queue.Next(&marker);
			queue.Remove(&marker);
			continue;
		}

		pagesScanned++;

		DEBUG_PAGE_ACCESS_START(page);

		// Get the accessed count, clear the accessed/modified flags and
		// unmap the page, if it hasn't been accessed.
		int32 usageCount;
		if (page->WiredCount() > 0)
			usageCount = vm_clear_page_mapping_accessed_flags(page);
		else
			usageCount = vm_remove_all_page_mappings_if_unaccessed(page);

		// update usage count
		if (usageCount > 0) {
			usageCount += page->usage_count + kPageUsageAdvance;
			if (usageCount > kPageUsageMax)
				usageCount = kPageUsageMax;
		} else {
			usageCount += page->usage_count - (int32)kPageUsageDecline;
			if (usageCount < 0)
				usageCount = 0;
		}

		page->usage_count = usageCount;

		// Move to fitting queue or requeue:
		// * Active mapped pages go to the active queue.
		// * Inactive mapped (i.e. wired) pages are requeued.
		// * The remaining pages are cachable. Thus, if unmodified they go to
		//   the cached queue, otherwise to the modified queue (up to a limit).
		//   Note that until in the idle scanning we don't exempt pages of
		//   temporary caches. Apparently we really need memory, so we better
		//   page out memory as well.
		bool isMapped = page->IsMapped();
		if (usageCount > 0) {
			if (isMapped) {
				set_page_state(page, PAGE_STATE_ACTIVE);
				pagesToActive++;
			} else
				vm_page_requeue(page, true);
		} else if (isMapped) {
			vm_page_requeue(page, true);
		} else if (!page->modified) {
			set_page_state(page, PAGE_STATE_CACHED);
			pagesToFree--;
			pagesToCached++;
		} else if (maxToFlush > 0) {
			set_page_state(page, PAGE_STATE_MODIFIED);
			maxToFlush--;
			pagesToModified++;
		} else
			vm_page_requeue(page, true);

		DEBUG_PAGE_ACCESS_END(page);

		cache->ReleaseRefAndUnlock();

		// remove the marker
		queueLocker.Lock();
		nextPage = queue.Next(&marker);
		queue.Remove(&marker);
	}

	queueLocker.Unlock();

	time = system_time() - time;
	TRACE_DAEMON("  -> inactive scan (%7" B_PRId64 " us): scanned: %7" B_PRIu32
		", moved: %" B_PRIu32 " -> cached, %" B_PRIu32 " -> modified, %"
		B_PRIu32 " -> active\n", time, pagesScanned, pagesToCached,
		pagesToModified, pagesToActive);

	// wake up the page writer, if we tossed it some pages
	if (pagesToModified > 0)
		sPageWriterCondition.WakeUp();
}


/**
 * @brief Perform a full scan of the active page queue to deactivate pages under memory pressure.
 *
 * Determines how many pages must be moved to the inactive queue based on the
 * current deficit, then walks the active queue clearing accessed flags and
 * demoting pages whose usage count drops to zero.
 *
 * @param pageStats   Current page statistics snapshot.
 * @param despairLevel Urgency level (currently unused but reserved for future use).
 * @note Called from the page daemon thread only.
 *       Acquires sActivePageQueue's spinlock internally.
 */
static void
full_scan_active_pages(page_stats& pageStats, int32 despairLevel)
{
	vm_page marker;
	init_page_marker(marker);

	VMPageQueue& queue = sActivePageQueue;
	InterruptsSpinLocker queueLocker(queue.GetLock());
	uint32 maxToScan = queue.Count();

	int32 pagesToDeactivate = pageStats.unsatisfiedReservations
		+ sFreeOrCachedPagesTarget
		- (pageStats.totalFreePages + pageStats.cachedPages)
		+ std::max((int32)sInactivePagesTarget - (int32)maxToScan, (int32)0);
	if (pagesToDeactivate <= 0)
		return;

	bigtime_t time = system_time();
	uint32 pagesAccessed = 0;
	uint32 pagesToInactive = 0;
	uint32 pagesScanned = 0;

	vm_page* nextPage = queue.Head();

	while (pagesToDeactivate > 0 && maxToScan > 0) {
		maxToScan--;

		// get the next page
		vm_page* page = nextPage;
		if (page == NULL)
			break;
		nextPage = queue.Next(page);

		if (page->busy)
			continue;

		// mark the position
		queue.InsertAfter(page, &marker);
		queueLocker.Unlock();

		// lock the page's cache
		VMCache* cache = vm_cache_acquire_locked_page_cache(page, true);
		if (cache == NULL || page->busy || page->State() != PAGE_STATE_ACTIVE) {
			if (cache != NULL)
				cache->ReleaseRefAndUnlock();
			queueLocker.Lock();
			nextPage = queue.Next(&marker);
			queue.Remove(&marker);
			continue;
		}

		pagesScanned++;

		DEBUG_PAGE_ACCESS_START(page);

		// Get the page active/modified flags and update the page's usage count.
		int32 usageCount = vm_clear_page_mapping_accessed_flags(page);

		if (usageCount > 0) {
			usageCount += page->usage_count + kPageUsageAdvance;
			if (usageCount > kPageUsageMax)
				usageCount = kPageUsageMax;
			pagesAccessed++;

#if ENABLE_SWAP_SUPPORT
			free_page_swap_space(page);
#endif
		} else {
			usageCount += page->usage_count - (int32)kPageUsageDecline;
			if (usageCount <= 0) {
				usageCount = 0;
				set_page_state(page, PAGE_STATE_INACTIVE);
				pagesToInactive++;
			}
		}

		page->usage_count = usageCount;

		DEBUG_PAGE_ACCESS_END(page);

		cache->ReleaseRefAndUnlock();

		// remove the marker
		queueLocker.Lock();
		nextPage = queue.Next(&marker);
		queue.Remove(&marker);
	}

	time = system_time() - time;
	TRACE_DAEMON("  ->   active scan (%7" B_PRId64 " us): scanned: %7" B_PRIu32
		", moved: %" B_PRIu32 " -> inactive, encountered %" B_PRIu32 " accessed"
		" ones\n", time, pagesScanned, pagesToInactive, pagesAccessed);
}


/**
 * @brief Page daemon idle-mode scan: top up the free pool and age the active queue.
 *
 * If the truly-free count falls below sFreePagesTarget, reclaims cached pages
 * to compensate.  Then runs idle_scan_active_pages() to age active pages.
 *
 * @param pageStats Current page statistics; may be refreshed internally.
 * @note Called from the page daemon thread only.
 */
static void
page_daemon_idle_scan(page_stats& pageStats)
{
	TRACE_DAEMON("page daemon: idle run\n");

	if (pageStats.totalFreePages < (int32)sFreePagesTarget) {
		// We want more actually free pages, so free some from the cached
		// ones.
		uint32 freed = free_cached_pages(
			sFreePagesTarget - pageStats.totalFreePages, false);
		if (freed > 0)
			unreserve_pages(freed);
		get_page_stats(pageStats);
	}

	// Walk the active list and move pages to the inactive queue.
	get_page_stats(pageStats);
	idle_scan_active_pages(pageStats);
}


/**
 * @brief Page daemon full-mode scan: aggressively reclaim inactive, cached, and active pages.
 *
 * Runs full_scan_inactive_pages(), then frees cached pages to satisfy
 * reservations, then runs full_scan_active_pages().  Wakes reservation waiters
 * via unreserve_pages() after reclaiming cached pages.
 *
 * @param pageStats   Current page statistics; refreshed between sub-scans.
 * @param despairLevel Urgency level controlling how aggressively modified pages
 *                    are flushed.
 * @note Called from the page daemon thread only.
 */
static void
page_daemon_full_scan(page_stats& pageStats, int32 despairLevel)
{
	TRACE_DAEMON("page daemon: full run: free: %" B_PRIu32 ", cached: %"
		B_PRIu32 ", to free: %" B_PRIu32 "\n", pageStats.totalFreePages,
		pageStats.cachedPages, pageStats.unsatisfiedReservations
			+ sFreeOrCachedPagesTarget
			- (pageStats.totalFreePages + pageStats.cachedPages));

	// Walk the inactive list and transfer pages to the cached and modified
	// queues.
	full_scan_inactive_pages(pageStats, despairLevel);

	// Free cached pages. Also wake up reservation waiters.
	get_page_stats(pageStats);
	int32 pagesToFree = pageStats.unsatisfiedReservations + sFreePagesTarget
		- (pageStats.totalFreePages);
	if (pagesToFree > 0) {
		uint32 freed = free_cached_pages(pagesToFree, true);
		if (freed > 0)
			unreserve_pages(freed);
	}

	// Walk the active list and move pages to the inactive queue.
	get_page_stats(pageStats);
	full_scan_active_pages(pageStats, despairLevel);
}


/**
 * @brief Kernel thread: main page reclaim daemon loop.
 *
 * Evaluates current memory pressure on each wake-up.  When memory is
 * sufficient performs an idle scan; when under pressure performs increasingly
 * aggressive full scans with a rising despairLevel.  Waits on
 * sPageDaemonCondition between iterations.
 *
 * @param unused Ignored thread argument.
 * @retval B_OK Never; the function loops indefinitely.
 * @note Runs as a dedicated kernel thread at B_NORMAL_PRIORITY.
 *       Must not be called directly from any other context.
 */
static status_t
page_daemon(void* /*unused*/)
{
	int32 despairLevel = 0;

	while (true) {
		sPageDaemonCondition.ClearActivated();

		// evaluate the free pages situation
		page_stats pageStats;
		get_page_stats(pageStats);

		if (!do_active_paging(pageStats)) {
			// Things look good -- just maintain statistics and keep the pool
			// of actually free pages full enough.
			despairLevel = 0;
			page_daemon_idle_scan(pageStats);
			sPageDaemonCondition.Wait(kIdleScanWaitInterval, false);
		} else {
			// Not enough free pages. We need to do some real work.
			despairLevel = std::max(despairLevel + 1, (int32)3);
			page_daemon_full_scan(pageStats, despairLevel);

			// Don't wait after the first full scan, but rather immediately
			// check whether we were successful in freeing enough pages and
			// re-run with increased despair level. The first scan is
			// conservative with respect to moving inactive modified pages to
			// the modified list to avoid thrashing. The second scan, however,
			// will not hold back.
			if (despairLevel > 1)
				snooze(kBusyScanWaitInterval);
		}
	}

	return B_OK;
}


/*!	Returns how many pages could *not* be reserved.
*/
/**
 * @brief Reserve \a missing pages at the given \a priority, waiting if necessary.
 *
 * Attempts to atomically decrement sUnreservedFreePages.  If insufficient pages
 * are available, tries freeing cached pages.  If still insufficient and
 * \a dontWait is false, blocks the calling thread on sPageReservationWaiters
 * until the reservation is satisfied.  Higher-priority waiters can steal
 * reservations from lower-priority ones.
 *
 * @param missing   Number of pages that still need to be reserved.
 * @param priority  VM_PRIORITY_* constant controlling the dontTouch floor.
 * @param dontWait  If true, return immediately with the unsatisfied count
 *                  rather than blocking.
 * @return Number of pages that could NOT be reserved (0 on full success).
 * @note Must not be called with any VMCache lock held (may block).
 *       Acquires sPageDeficitLock internally when blocking.
 */
static uint32
reserve_pages(uint32 missing, int priority, bool dontWait)
{
	const uint32 requested = missing;
	const int32 dontTouch = kPageReserveForPriority[priority];

	while (true) {
		missing -= reserve_some_pages(missing, dontTouch);
		if (missing == 0)
			return 0;

		if (sUnsatisfiedPageReservations == 0) {
			missing -= free_cached_pages(missing, dontWait);
			if (missing == 0)
				return 0;
		}

		if (dontWait)
			return missing;

		// we need to wait for pages to become available

		MutexLocker pageDeficitLocker(sPageDeficitLock);
		if (atomic_get(&sUnreservedFreePages) > dontTouch) {
			// the situation changed
			pageDeficitLocker.Unlock();
			continue;
		}

		const bool notifyDaemon = (sUnsatisfiedPageReservations == 0);
		sUnsatisfiedPageReservations += missing;

		PageReservationWaiter waiter;
		waiter.thread = thread_get_current_thread();
		waiter.dontTouch = dontTouch;
		waiter.requested = requested;
		waiter.reserved = requested - missing;

		// insert ordered (i.e. after all waiters with higher or equal priority)
		PageReservationWaiter* otherWaiter = NULL;
		for (PageReservationWaiterList::Iterator it
					= sPageReservationWaiters.GetIterator();
				(otherWaiter = it.Next()) != NULL;) {
			if (waiter < *otherWaiter)
				break;
		}

		if (otherWaiter != NULL && sPageReservationWaiters.Head() == otherWaiter) {
			// We're higher-priority than the head waiter; steal its reservation.
			if (otherWaiter->reserved >= missing) {
				otherWaiter->reserved -= missing;
				return 0;
			}

			missing -= otherWaiter->reserved;
			waiter.reserved += otherWaiter->reserved;
			otherWaiter->reserved = 0;
		} else if (!sPageReservationWaiters.IsEmpty() && waiter.reserved != 0) {
			// We're lower-priority than the head waiter, but we have a reservation.
			// We must have raced with other threads somehow. Unreserve and try again.
			sUnsatisfiedPageReservations -= missing;
			missing += waiter.reserved;
			atomic_add(&sUnreservedFreePages, waiter.reserved);
			wake_up_page_reservation_waiters();
			continue;
		}

		sPageReservationWaiters.InsertBefore(otherWaiter, &waiter);

		thread_prepare_to_block(waiter.thread, 0, THREAD_BLOCK_TYPE_OTHER,
			"waiting for pages");

		if (notifyDaemon)
			sPageDaemonCondition.WakeUp();

		pageDeficitLocker.Unlock();

		low_resource(B_KERNEL_RESOURCE_PAGES, missing, B_RELATIVE_TIMEOUT, 0);
		thread_block();

		ASSERT(waiter.requested == waiter.reserved);
		return 0;
	}
}


//	#pragma mark - private kernel API


/*!	Writes a range of modified pages of a cache to disk.
	You need to hold the VMCache lock when calling this function.
	Note that the cache lock is released in this function.
	\param cache The cache.
	\param firstPage Offset (in page size units) of the first page in the range.
	\param endPage End offset (in page size units) of the page range. The page
		at this offset is not included.
*/
/**
 * @brief Write all modified pages of a VMCache in the range [firstPage, endPage) to disk.
 *
 * Iterates the cache's page tree, batches contiguous modified pages into
 * PageWriteTransfer objects, and schedules synchronous writes.  The cache lock
 * is temporarily released around each write.
 *
 * @param cache      The cache whose pages to write; must be locked on entry.
 * @param firstPage  First page offset (in page units) of the range to write.
 * @param endPage    Exclusive end page offset of the range to write.
 * @retval B_OK Always (individual page errors are handled internally via
 *              PageWriteWrapper::Done()).
 * @note The VMCache lock is released and re-acquired around each write.
 *       Must not be called from interrupt context.
 */
status_t
vm_page_write_modified_page_range(struct VMCache* cache, uint32 firstPage,
	uint32 endPage)
{
	static const int32 kMaxPages = 256;
	int32 maxPages = cache->MaxPagesPerWrite();
	if (maxPages < 0 || maxPages > kMaxPages)
		maxPages = kMaxPages;

	const uint32 allocationFlags = HEAP_DONT_WAIT_FOR_MEMORY
		| HEAP_DONT_LOCK_KERNEL_SPACE;

	PageWriteWrapper stackWrappersPool[2];
	PageWriteWrapper* stackWrappers[1];
	PageWriteWrapper* wrapperPool
		= new(malloc_flags(allocationFlags)) PageWriteWrapper[maxPages + 1];
	PageWriteWrapper** wrappers
		= new(malloc_flags(allocationFlags)) PageWriteWrapper*[maxPages];
	if (wrapperPool == NULL || wrappers == NULL) {
		// don't fail, just limit our capabilities
		delete[] wrapperPool;
		delete[] wrappers;
		wrapperPool = stackWrappersPool;
		wrappers = stackWrappers;
		maxPages = 1;
	}

	int32 nextWrapper = 0;
	int32 usedWrappers = 0;

	PageWriteTransfer transfer;
	bool transferEmpty = true;

	VMCachePagesTree::Iterator it
		= cache->pages.GetIterator(firstPage, true, true);

	while (true) {
		vm_page* page = it.Next();
		if (page == NULL || page->cache_offset >= endPage) {
			if (transferEmpty)
				break;

			page = NULL;
		}

		if (page != NULL) {
			if (page->busy
				|| (page->State() != PAGE_STATE_MODIFIED
					&& !vm_test_map_modification(page))) {
				page = NULL;
			}
		}

		PageWriteWrapper* wrapper = NULL;
		if (page != NULL) {
			wrapper = &wrapperPool[nextWrapper++];
			if (nextWrapper > maxPages)
				nextWrapper = 0;

			DEBUG_PAGE_ACCESS_START(page);

			wrapper->SetTo(page);

			if (transferEmpty || transfer.AddPage(page)) {
				if (transferEmpty) {
					transfer.SetTo(NULL, page, maxPages);
					transferEmpty = false;
				}

				DEBUG_PAGE_ACCESS_END(page);

				wrappers[usedWrappers++] = wrapper;
				continue;
			}

			DEBUG_PAGE_ACCESS_END(page);
		}

		if (transferEmpty)
			continue;

		cache->Unlock();
		status_t status = transfer.Schedule(0);
		cache->Lock();

		for (int32 i = 0; i < usedWrappers; i++)
			wrappers[i]->Done(status);

		usedWrappers = 0;

		if (page != NULL) {
			transfer.SetTo(NULL, page, maxPages);
			wrappers[usedWrappers++] = wrapper;
		} else
			transferEmpty = true;
	}

	if (wrapperPool != stackWrappersPool) {
		delete[] wrapperPool;
		delete[] wrappers;
	}

	return B_OK;
}


/*!	You need to hold the VMCache lock when calling this function.
	Note that the cache lock is released in this function.
*/
/**
 * @brief Write all modified pages of a VMCache to disk.
 *
 * Convenience wrapper around vm_page_write_modified_page_range() that
 * covers the entire cache extent [0, virtual_end / PAGE_SIZE).
 *
 * @param cache The cache to write; must be locked by the caller.
 * @retval B_OK Always.
 * @note The VMCache lock is released and re-acquired internally.
 */
status_t
vm_page_write_modified_pages(VMCache *cache)
{
	return vm_page_write_modified_page_range(cache, 0,
		(cache->virtual_end + B_PAGE_SIZE - 1) >> PAGE_SHIFT);
}


/*!	Schedules the page writer to write back the specified \a page.
	Note, however, that it might not do this immediately, and it can well
	take several seconds until the page is actually written out.
*/
/**
 * @brief Ask the page writer to schedule a writeback of a single modified page.
 *
 * Requeues the page to the tail of sModifiedPageQueue and wakes the page
 * writer condition variable.  The actual write may be delayed by several
 * seconds.
 *
 * @param page A page in PAGE_STATE_MODIFIED.
 * @note The page's cache must be locked.
 */
void
vm_page_schedule_write_page(vm_page *page)
{
	PAGE_ASSERT(page, page->State() == PAGE_STATE_MODIFIED);

	vm_page_requeue(page, false);

	sPageWriterCondition.WakeUp();
}


/*!	Cache must be locked.
*/
/**
 * @brief Schedule writeback of all modified pages in a cache offset range.
 *
 * Iterates the cache's page tree from \a firstPage to \a endPage, requeuing
 * each non-busy modified page to the tail of sModifiedPageQueue, then wakes
 * the page writer.
 *
 * @param cache      The cache to scan; must be locked by the caller.
 * @param firstPage  First page offset (in page units) of the range.
 * @param endPage    Exclusive end page offset of the range.
 * @note The cache lock must be held throughout.
 */
void
vm_page_schedule_write_page_range(struct VMCache *cache, uint32 firstPage,
	uint32 endPage)
{
	uint32 modified = 0;
	for (VMCachePagesTree::Iterator it
				= cache->pages.GetIterator(firstPage, true, true);
			vm_page *page = it.Next();) {
		if (page->cache_offset >= endPage)
			break;

		if (!page->busy && page->State() == PAGE_STATE_MODIFIED) {
			DEBUG_PAGE_ACCESS_START(page);
			vm_page_requeue(page, false);
			modified++;
			DEBUG_PAGE_ACCESS_END(page);
		}
	}

	if (modified > 0)
		sPageWriterCondition.WakeUp();
}


/**
 * @brief Compute the total number of pages covered by physical memory ranges and set sNumPages.
 *
 * Derives sPhysicalPageOffset, sNumPages, sNonExistingPages (holes between
 * ranges), and sIgnoredPages from kernel_args. Respects LIMIT_AVAILABLE_MEMORY
 * if defined.
 *
 * @param args Pointer to the kernel boot arguments structure.
 * @note Must be called before vm_page_init(); no locks are held or needed.
 */
void
vm_page_init_num_pages(kernel_args *args)
{
	// calculate the size of memory by looking at the physical_memory_range array
	sPhysicalPageOffset = args->physical_memory_range[0].start / B_PAGE_SIZE;
	page_num_t physicalPagesEnd = sPhysicalPageOffset
		+ args->physical_memory_range[0].size / B_PAGE_SIZE;

	sNonExistingPages = 0;
	sIgnoredPages = args->ignored_physical_memory / B_PAGE_SIZE;

	for (uint32 i = 1; i < args->num_physical_memory_ranges; i++) {
		page_num_t start = args->physical_memory_range[i].start / B_PAGE_SIZE;
		if (start > physicalPagesEnd)
			sNonExistingPages += start - physicalPagesEnd;
		physicalPagesEnd = start
			+ args->physical_memory_range[i].size / B_PAGE_SIZE;

#ifdef LIMIT_AVAILABLE_MEMORY
		page_num_t available
			= physicalPagesEnd - sPhysicalPageOffset - sNonExistingPages;
		if (available > LIMIT_AVAILABLE_MEMORY * (1024 * 1024 / B_PAGE_SIZE)) {
			physicalPagesEnd = sPhysicalPageOffset + sNonExistingPages
				+ LIMIT_AVAILABLE_MEMORY * (1024 * 1024 / B_PAGE_SIZE);
			break;
		}
#endif
	}

	TRACE(("first phys page = %#" B_PRIxPHYSADDR ", end %#" B_PRIxPHYSADDR "\n",
		sPhysicalPageOffset, physicalPagesEnd));

	sNumPages = physicalPagesEnd - sPhysicalPageOffset;
}


/**
 * @brief First-phase physical page allocator initialization: build the page table and free queues.
 *
 * Allocates the sPages array via vm_allocate_early(), initialises all page
 * structures, places every page on sFreePageQueue, marks non-RAM and
 * pre-allocated ranges, and computes sFreePagesTarget /
 * sFreeOrCachedPagesTarget / sInactivePagesTarget.
 *
 * @param args Pointer to the kernel boot arguments structure.
 * @retval B_OK Always.
 * @note Must be called after vm_page_init_num_pages().
 *       Runs during early kernel boot; no threads exist yet.
 */
status_t
vm_page_init(kernel_args *args)
{
	TRACE(("vm_page_init: entry\n"));

	// init page queues
	sModifiedPageQueue.Init("modified pages queue");
	sInactivePageQueue.Init("inactive pages queue");
	sActivePageQueue.Init("active pages queue");
	sCachedPageQueue.Init("cached pages queue");
	sFreePageQueue.Init("free pages queue");
	sClearPageQueue.Init("clear pages queue");

	new (&sPageReservationWaiters) PageReservationWaiterList;

	// map in the new free page table
	sPages = (vm_page *)vm_allocate_early(args, sNumPages * sizeof(vm_page),
		~0L, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0);

	TRACE(("vm_init: putting free_page_table @ %p, # ents %" B_PRIuPHYSADDR
		" (size %#" B_PRIxPHYSADDR ")\n", sPages, sNumPages,
		(phys_addr_t)(sNumPages * sizeof(vm_page))));

	// initialize the free page table
	for (uint32 i = 0; i < sNumPages; i++) {
		sPages[i].Init(sPhysicalPageOffset + i);
		sFreePageQueue.Append(&sPages[i]);

#if VM_PAGE_ALLOCATION_TRACKING_AVAILABLE
		sPages[i].allocation_tracking_info.Clear();
#endif
	}

	sUnreservedFreePages = sNumPages;

	TRACE(("initialized table\n"));

	// mark the ranges between usable physical memory unused
	phys_addr_t previousEnd = 0;
	for (uint32 i = 0; i < args->num_physical_memory_ranges; i++) {
		phys_addr_t base = args->physical_memory_range[i].start;
		phys_size_t size = args->physical_memory_range[i].size;
		if (base > previousEnd) {
			mark_page_range_in_use(previousEnd / B_PAGE_SIZE,
				(base - previousEnd) / B_PAGE_SIZE, false);
		}
		previousEnd = base + size;
	}

	// mark the allocated physical page ranges wired
	for (uint32 i = 0; i < args->num_physical_allocated_ranges; i++) {
		mark_page_range_in_use(
			args->physical_allocated_range[i].start / B_PAGE_SIZE,
			args->physical_allocated_range[i].size / B_PAGE_SIZE, true);
	}

	// prevent future allocations from the kernel args ranges
	args->num_physical_allocated_ranges = 0;

	// report initially available memory
	vm_unreserve_memory(vm_page_num_free_pages() * B_PAGE_SIZE);

	// The target of actually free pages. This must be at least the system
	// reserve, but should be a few more pages, so we don't have to extract
	// a cached page with each allocation.
	sFreePagesTarget = VM_PAGE_RESERVE_USER
		+ std::max((page_num_t)32, (sNumPages - sNonExistingPages) / 1024);

	// The target of free + cached and inactive pages. On low-memory machines
	// keep things tight. free + cached is the pool of immediately allocatable
	// pages. We want a few inactive pages, so when we're actually paging, we
	// have a reasonably large set of pages to work with.
	if (sUnreservedFreePages < (16 * 1024)) {
		sFreeOrCachedPagesTarget = sFreePagesTarget + 128;
		sInactivePagesTarget = sFreePagesTarget / 3;
	} else {
		sFreeOrCachedPagesTarget = 2 * sFreePagesTarget;
		sInactivePagesTarget = sFreePagesTarget / 2;
	}

	TRACE(("vm_page_init: exit\n"));

	return B_OK;
}


/**
 * @brief Second-phase page allocator initialization: create the page-structures area and register debugger commands.
 *
 * Wraps the sPages array in a named kernel area so it appears in the area
 * table, and registers all vm_page-related kernel debugger commands.
 *
 * @param args Pointer to the kernel boot arguments (unused beyond signature).
 * @retval B_OK Always.
 * @note Must be called after the VM area infrastructure is available
 *       (i.e. after vm_init_post_area()).
 */
status_t
vm_page_init_post_area(kernel_args *args)
{
	void *dummy;

	dummy = sPages;
	create_area("page structures", &dummy, B_EXACT_ADDRESS,
		PAGE_ALIGN(sNumPages * sizeof(vm_page)), B_ALREADY_WIRED,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);

	add_debugger_command("list_pages", &dump_page_list,
		"List physical pages");
	add_debugger_command("page_stats", &dump_page_stats,
		"Dump statistics about page usage");
	add_debugger_command_etc("page", &dump_page_long,
		"Dump page info",
		"[ \"-p\" | \"-v\" ] [ \"-m\" ] <address>\n"
		"Prints information for the physical page. If neither \"-p\" nor\n"
		"\"-v\" are given, the provided address is interpreted as address of\n"
		"the vm_page data structure for the page in question. If \"-p\" is\n"
		"given, the address is the physical address of the page. If \"-v\" is\n"
		"given, the address is interpreted as virtual address in the current\n"
		"thread's address space and for the page it is mapped to (if any)\n"
		"information are printed. If \"-m\" is specified, the command will\n"
		"search all known address spaces for mappings to that page and print\n"
		"them.\n", 0);
	add_debugger_command("page_queue", &dump_page_queue, "Dump page queue");
	add_debugger_command("find_page", &find_page,
		"Find out which queue a page is actually in");

#ifdef TRACK_PAGE_USAGE_STATS
	add_debugger_command_etc("page_usage", &dump_page_usage_stats,
		"Dumps statistics about page usage counts",
		"\n"
		"Dumps statistics about page usage counts.\n",
		B_KDEBUG_DONT_PARSE_ARGUMENTS);
#endif

#if VM_PAGE_ALLOCATION_TRACKING_AVAILABLE
	add_debugger_command_etc("page_allocations_per_caller",
		&dump_page_allocations_per_caller,
		"Dump current page allocations summed up per caller",
		"[ -d <caller> ] [ -r ]\n"
		"The current allocations will by summed up by caller (their count)\n"
		"printed in decreasing order by count.\n"
		"If \"-d\" is given, each allocation for caller <caller> is printed\n"
		"including the respective stack trace.\n"
		"If \"-r\" is given, the allocation infos are reset after gathering\n"
		"the information, so the next command invocation will only show the\n"
		"allocations made after the reset.\n", 0);
	add_debugger_command_etc("page_allocation_infos",
		&dump_page_allocation_infos,
		"Dump current page allocations",
		"[ --stacktrace ] [ -p <page number> ] [ --team <team ID> ] "
		"[ --thread <thread ID> ]\n"
		"The current allocations filtered by optional values will be printed.\n"
		"The optional \"-p\" page number filters for a specific page,\n"
		"with \"--team\" and \"--thread\" allocations by specific teams\n"
		"and/or threads can be filtered (these only work if a corresponding\n"
		"tracing entry is still available).\n"
		"If \"--stacktrace\" is given, then stack traces of the allocation\n"
		"callers are printed, where available\n", 0);
#endif

	return B_OK;
}


/**
 * @brief Third-phase page allocator initialization: start background kernel threads.
 *
 * Initialises sFreePageCondition and spawns three kernel threads:
 *   - page_scrubber (B_LOWEST_ACTIVE_PRIORITY): zero-fills free pages.
 *   - page_writer (B_NORMAL_PRIORITY + 1): writes modified pages to backing store.
 *   - page_daemon (B_NORMAL_PRIORITY): reclaims pages under memory pressure.
 *
 * @param args Pointer to the kernel boot arguments (unused beyond signature).
 * @retval B_OK Always.
 * @note Must be called after the thread infrastructure is available.
 */
status_t
vm_page_init_post_thread(kernel_args *args)
{
	new (&sFreePageCondition) ConditionVariable;

	// create a kernel thread to clear out pages

	thread_id thread = spawn_kernel_thread(&page_scrubber, "page scrubber",
		B_LOWEST_ACTIVE_PRIORITY, NULL);
	resume_thread(thread);

	// start page writer

	sPageWriterCondition.Init("page writer");

	thread = spawn_kernel_thread(&page_writer, "page writer",
		B_NORMAL_PRIORITY + 1, NULL);
	resume_thread(thread);

	// start page daemon

	sPageDaemonCondition.Init("page daemon");

	thread = spawn_kernel_thread(&page_daemon, "page daemon",
		B_NORMAL_PRIORITY, NULL);
	resume_thread(thread);

	return B_OK;
}


/**
 * @brief Mark a single physical page as in-use (delegates to vm_mark_page_range_inuse()).
 *
 * @param page Physical page number to mark.
 * @retval B_OK Always (range clamping is performed silently).
 */
status_t
vm_mark_page_inuse(page_num_t page)
{
	return vm_mark_page_range_inuse(page, 1);
}


/**
 * @brief Mark a range of physical pages as in-use (non-wired).
 *
 * @param startPage Physical page number of the first page to mark.
 * @param length    Number of pages to mark.
 * @retval B_OK Always.
 */
status_t
vm_mark_page_range_inuse(page_num_t startPage, page_num_t length)
{
	return mark_page_range_in_use(startPage, length, false);
}


/*!	Unreserve pages previously reserved with vm_page_reserve_pages().
*/
/**
 * @brief Release a previously acquired page reservation.
 *
 * Returns \a reservation->count pages to the unreserved pool and resets
 * the reservation count to zero.  Wakes any blocked reservation waiters.
 *
 * @param reservation The reservation to release; its count is set to 0 on return.
 * @note Safe to call even if reservation->count is already 0.
 */
void
vm_page_unreserve_pages(vm_page_reservation* reservation)
{
	uint32 count = reservation->count;
	reservation->count = 0;

	if (count == 0)
		return;

	TA(UnreservePages(count));

	unreserve_pages(count);
}


/*!	With this call, you can reserve a number of free pages in the system.
	They will only be handed out to someone who has actually reserved them.
	This call returns as soon as the number of requested pages has been
	reached.
	The caller must not hold any cache lock or the function might deadlock.
*/
/**
 * @brief Reserve exactly \a count free pages, blocking until all are available.
 *
 * Fills \a reservation->count on return.  If count is 0 the call is a no-op.
 *
 * @param reservation Output reservation structure; count is set to \a count on success.
 * @param count       Number of pages to reserve.
 * @param priority    VM_PRIORITY_* constant (USER, SYSTEM, or VIP) that controls
 *                    which floor of reserved pages must not be touched.
 * @note Must not be called with any VMCache lock held (may block indefinitely).
 */
void
vm_page_reserve_pages(vm_page_reservation* reservation, uint32 count,
	int priority)
{
	reservation->count = count;

	if (count == 0)
		return;

	TA(ReservePages(count));

	reserve_pages(count, priority, false);
}


/**
 * @brief Try to reserve exactly \a count free pages without blocking.
 *
 * Attempts to reserve the pages but returns false immediately if they are not
 * all available.  On failure, any partially reserved pages are returned to the
 * pool.
 *
 * @param reservation Output reservation structure; count is set to \a count on success.
 * @param count       Number of pages to reserve.
 * @param priority    VM_PRIORITY_* constant controlling the dontTouch floor.
 * @return true if all \a count pages were reserved, false otherwise.
 * @note Does not block; safe to call with VMCache locks held.
 */
bool
vm_page_try_reserve_pages(vm_page_reservation* reservation, uint32 count,
	int priority)
{
	if (count == 0) {
		reservation->count = count;
		return true;
	}

	uint32 remaining = reserve_pages(count, priority, true);
	if (remaining == 0) {
		TA(ReservePages(count));
		reservation->count = count;
		return true;
	}

	unreserve_pages(count - remaining);

	return false;
}


/**
 * @brief Allocate a single physical page from a previously established reservation.
 *
 * Removes a page from the free or clear queue (preferring clear if
 * VM_PAGE_ALLOC_CLEAR is set), initialises it with the requested state and
 * flags, and optionally zero-fills it.  Decrements the reservation count by 1.
 *
 * @param reservation A valid reservation with count > 0; decremented on return.
 * @param flags       Allocation flags:
 *                    - Low bits: target PAGE_STATE_* (must not be FREE or CLEAR).
 *                    - VM_PAGE_ALLOC_BUSY: mark the page busy immediately.
 *                    - VM_PAGE_ALLOC_CLEAR: prefer a pre-cleared page; zero-fill
 *                      if a free page had to be used instead.
 * @return Pointer to the allocated vm_page on success.
 * @note Caller must hold a valid reservation. May panic if no page is available
 *       despite the reservation (indicates a reservation accounting bug).
 *       Acquires sFreePageQueuesLock (read, upgrading to write on contention).
 */
vm_page *
vm_page_allocate_page(vm_page_reservation* reservation, uint32 flags)
{
	uint32 pageState = flags & VM_PAGE_ALLOC_STATE;
	ASSERT(pageState != PAGE_STATE_FREE);
	ASSERT(pageState != PAGE_STATE_CLEAR);

	ASSERT(reservation->count > 0);
	reservation->count--;

	VMPageQueue* queue;
	VMPageQueue* otherQueue;

	if ((flags & VM_PAGE_ALLOC_CLEAR) != 0) {
		queue = &sClearPageQueue;
		otherQueue = &sFreePageQueue;
	} else {
		queue = &sFreePageQueue;
		otherQueue = &sClearPageQueue;
	}

	ReadLocker locker(sFreePageQueuesLock);

	vm_page* page = queue->RemoveHeadUnlocked();
	if (page == NULL) {
		// if the primary queue was empty, grab the page from the
		// secondary queue
		page = otherQueue->RemoveHeadUnlocked();

		if (page == NULL) {
			// Unlikely, but possible: the page we have reserved has moved
			// between the queues after we checked the first queue. Grab the
			// write locker to make sure this doesn't happen again.
			locker.Unlock();
			WriteLocker writeLocker(sFreePageQueuesLock);

			page = queue->RemoveHead();
			if (page == NULL)
				otherQueue->RemoveHead();

			if (page == NULL) {
				panic("Had reserved page, but there is none!");
				return NULL;
			}

			// downgrade to read lock
			locker.Lock();
		}
	}

	if (page->CacheRef() != NULL)
		panic("supposed to be free page %p has cache @! page %p; cache _cache", page, page);

	DEBUG_PAGE_ACCESS_START(page);

	int oldPageState = page->State();
	page->SetState(pageState);
	page->busy = (flags & VM_PAGE_ALLOC_BUSY) != 0;
	page->busy_io = false;
	page->accessed = false;
	page->modified = false;
	page->usage_count = 0;

	locker.Unlock();

	if (pageState < PAGE_STATE_FIRST_UNQUEUED)
		sPageQueues[pageState].AppendUnlocked(page);

	// clear the page, if we had to take it from the free queue and a clear
	// page was requested
	if ((flags & VM_PAGE_ALLOC_CLEAR) != 0 && oldPageState != PAGE_STATE_CLEAR)
		clear_page(page);

#if VM_PAGE_ALLOCATION_TRACKING_AVAILABLE
	page->allocation_tracking_info.Init(
		TA(AllocatePage(page->physical_page_number)));
#else
	TA(AllocatePage(page->physical_page_number));
#endif

	return page;
}


/**
 * @brief Return a list of partially-allocated pages to the free/clear queues after a failed run allocation.
 *
 * Pages in \a freePages are returned to sFreePageQueue; pages in \a clearPages
 * to sClearPageQueue.  Both lists are consumed.  Notifies sFreePageCondition.
 *
 * @param freePages  List of pages to return to the free queue (sorted ascending).
 * @param clearPages List of pages to return to the clear queue (sorted ascending).
 * @note sFreePageQueuesLock must NOT be held by the caller; the function uses
 *       PrependUnlocked() which acquires the per-queue spinlock internally.
 */
static void
allocate_page_run_cleanup(VMPageQueue::PageList& freePages,
	VMPageQueue::PageList& clearPages)
{
	// Page lists are sorted, so remove tails before prepending to the respective queue.

	while (vm_page* page = freePages.RemoveTail()) {
		page->busy = false;
		page->SetState(PAGE_STATE_FREE);
		DEBUG_PAGE_ACCESS_END(page);
		sFreePageQueue.PrependUnlocked(page);
	}

	while (vm_page* page = clearPages.RemoveTail()) {
		page->busy = false;
		page->SetState(PAGE_STATE_CLEAR);
		DEBUG_PAGE_ACCESS_END(page);
		sClearPageQueue.PrependUnlocked(page);
	}

	sFreePageCondition.NotifyAll();
}


/*!	Tries to allocate the a contiguous run of \a length pages starting at
	index \a start.

	The caller must have write-locked the free/clear page queues. The function
	will unlock regardless of whether it succeeds or fails.

	If the function fails, it cleans up after itself, i.e. it will free all
	pages it manages to allocate.

	\param start The start index (into \c sPages) of the run.
	\param length The number of pages to allocate.
	\param flags Page allocation flags. Encodes the state the function shall
		set the allocated pages to, whether the pages shall be marked busy
		(VM_PAGE_ALLOC_BUSY), and whether the pages shall be cleared
		(VM_PAGE_ALLOC_CLEAR).
	\param freeClearQueueLocker Locked WriteLocker for the free/clear page
		queues in locked state. Will be unlocked by the function.
	\return The index of the first page that could not be allocated. \a length
		is returned when the function was successful.
*/
/**
 * @brief Attempt to allocate a physically contiguous run of \a length pages starting at sPages[\a start].
 *
 * Pulls free and clear pages out of their queues under the write lock, then
 * releases the lock and steals any remaining cached pages individually.  On
 * failure at any point, all acquired pages are returned via
 * allocate_page_run_cleanup() and the function reports the failing index.
 *
 * @param start                Physical page index (into sPages[]) of the run start.
 * @param length               Number of contiguous pages to allocate.
 * @param flags                Allocation flags (state, VM_PAGE_ALLOC_BUSY, VM_PAGE_ALLOC_CLEAR).
 * @param freeClearQueueLocker A write-locked WriteLocker for sFreePageQueuesLock;
 *                             will be unlocked by this function regardless of outcome.
 * @return \a length on full success; the index of the first page that could not
 *         be allocated otherwise (0 .. length-1).
 * @note The caller must hold sFreePageQueuesLock (write) on entry.
 *       On return, sFreePageQueuesLock is always unlocked.
 */
static page_num_t
allocate_page_run(page_num_t start, page_num_t length, uint32 flags,
	WriteLocker& freeClearQueueLocker)
{
	uint32 pageState = flags & VM_PAGE_ALLOC_STATE;
	ASSERT(pageState != PAGE_STATE_FREE);
	ASSERT(pageState != PAGE_STATE_CLEAR);
	ASSERT(start + length <= sNumPages);

	// Pull the free/clear pages out of their respective queues. Cached pages
	// are allocated later.
	page_num_t cachedPages = 0;
	VMPageQueue::PageList freePages;
	VMPageQueue::PageList clearPages;
	page_num_t i = 0;
	for (; i < length; i++) {
		bool pageAllocated = true;
		bool noPage = false;
		vm_page& page = sPages[start + i];
		switch (page.State()) {
			case PAGE_STATE_CLEAR:
				DEBUG_PAGE_ACCESS_START(&page);
				sClearPageQueue.Remove(&page);
				clearPages.Add(&page);
				break;
			case PAGE_STATE_FREE:
				DEBUG_PAGE_ACCESS_START(&page);
				sFreePageQueue.Remove(&page);
				freePages.Add(&page);
				break;
			case PAGE_STATE_CACHED:
				// We allocate cached pages later.
				cachedPages++;
				pageAllocated = false;
				break;

			default:
				// Probably a page was cached when our caller checked. Now it's
				// gone and we have to abort.
				noPage = true;
				break;
		}

		if (noPage)
			break;

		if (pageAllocated) {
			page.SetState(flags & VM_PAGE_ALLOC_STATE);
			page.busy = (flags & VM_PAGE_ALLOC_BUSY) != 0;
			page.busy_io = false;
			page.accessed = false;
			page.modified = false;
			page.usage_count = 0;
		}
	}

	if (i < length) {
		// failed to allocate a page -- free all that we've got
		allocate_page_run_cleanup(freePages, clearPages);
		return i;
	}

	freeClearQueueLocker.Unlock();

	if (cachedPages > 0) {
		// allocate the pages that weren't free but cached
		page_num_t freedCachedPages = 0;
		page_num_t nextIndex = start;
		vm_page* freePage = freePages.Head();
		vm_page* clearPage = clearPages.Head();
		while (cachedPages > 0) {
			// skip, if we've already got the page
			if (freePage != NULL && size_t(freePage - sPages) == nextIndex) {
				freePage = freePages.GetNext(freePage);
				nextIndex++;
				continue;
			}
			if (clearPage != NULL && size_t(clearPage - sPages) == nextIndex) {
				clearPage = clearPages.GetNext(clearPage);
				nextIndex++;
				continue;
			}

			// free the page, if it is still cached
			vm_page& page = sPages[nextIndex];
			if (!free_cached_page(&page, false)) {
				// TODO: if the page turns out to have been freed already,
				// there would be no need to fail
				break;
			}

			page.SetState(flags & VM_PAGE_ALLOC_STATE);
			page.busy = (flags & VM_PAGE_ALLOC_BUSY) != 0;
			page.busy_io = false;
			page.accessed = false;
			page.modified = false;
			page.usage_count = 0;

			freePages.InsertBefore(freePage, &page);
			freedCachedPages++;
			cachedPages--;
			nextIndex++;
		}

		// If we have freed cached pages, we need to balance things.
		if (freedCachedPages > 0)
			unreserve_pages(freedCachedPages);

		if ((nextIndex - start) < length) {
			// failed to allocate all cached pages -- free all that we've got
			freeClearQueueLocker.Lock();
			allocate_page_run_cleanup(freePages, clearPages);
			freeClearQueueLocker.Unlock();

			return nextIndex - start;
		}
	}

	// clear pages, if requested
	if ((flags & VM_PAGE_ALLOC_CLEAR) != 0) {
		for (VMPageQueue::PageList::Iterator it = freePages.GetIterator();
				vm_page* page = it.Next();) {
			clear_page(page);
		}
	}

	// add pages to target queue
	if (pageState < PAGE_STATE_FIRST_UNQUEUED) {
		freePages.TakeFrom(&clearPages);
		sPageQueues[pageState].AppendUnlocked(freePages, length);
	}

#if VM_PAGE_ALLOCATION_TRACKING_AVAILABLE
	AbstractTraceEntryWithStackTrace* traceEntry
		= TA(AllocatePageRun(start, length));

	for (page_num_t i = start; i < start + length; i++)
		sPages[i].allocation_tracking_info.Init(traceEntry);
#else
	TA(AllocatePageRun(start, length));
#endif

	return length;
}


/*! Allocate a physically contiguous range of pages.

	\param flags Page allocation flags. Encodes the state the function shall
		set the allocated pages to, whether the pages shall be marked busy
		(VM_PAGE_ALLOC_BUSY), and whether the pages shall be cleared
		(VM_PAGE_ALLOC_CLEAR).
	\param length The number of contiguous pages to allocate.
	\param restrictions Restrictions to the physical addresses of the page run
		to allocate, including \c low_address, the first acceptable physical
		address where the page run may start, \c high_address, the last
		acceptable physical address where the page run may end (i.e. it must
		hold \code runStartAddress + length <= high_address \endcode),
		\c alignment, the alignment of the page run start address, and
		\c boundary, multiples of which the page run must not cross.
		Values set to \c 0 are ignored.
	\param priority The page reservation priority (as passed to
		vm_page_reserve_pages()).
	\return The first page of the allocated page run on success; \c NULL
		when the allocation failed.
*/
/**
 * @brief Allocate a physically contiguous run of \a length pages satisfying address constraints.
 *
 * Internally reserves \a length pages at \a priority, then scans the physical
 * page array for a contiguous run meeting the low/high address, alignment, and
 * boundary restrictions.  First attempts free/clear pages only; if that fails,
 * retries including cached pages.
 *
 * @param flags        Allocation flags (state, VM_PAGE_ALLOC_BUSY, VM_PAGE_ALLOC_CLEAR).
 * @param length       Number of contiguous pages required.
 * @param restrictions Physical address constraints (low_address, high_address,
 *                     alignment, boundary); zero fields are ignored.
 * @param priority     VM_PRIORITY_* for the internal page reservation.
 * @return Pointer to the first vm_page of the allocated run, or NULL on failure.
 * @note Blocks until \a length pages are reserved.  Acquires
 *       sFreePageQueuesLock (write) internally.
 *       Must not be called with any VMCache lock held.
 */
vm_page*
vm_page_allocate_page_run(uint32 flags, page_num_t length,
	const physical_address_restrictions* restrictions, int priority)
{
	// compute start and end page index
	page_num_t requestedStart
		= std::max(restrictions->low_address / B_PAGE_SIZE, sPhysicalPageOffset)
			- sPhysicalPageOffset;
	page_num_t start = requestedStart;
	page_num_t end;
	if (restrictions->high_address > 0) {
		end = std::max(restrictions->high_address / B_PAGE_SIZE,
				sPhysicalPageOffset)
			- sPhysicalPageOffset;
		end = std::min(end, sNumPages);
	} else
		end = sNumPages;

	// compute alignment mask
	page_num_t alignmentMask
		= std::max(restrictions->alignment / B_PAGE_SIZE, (phys_addr_t)1) - 1;
	ASSERT(((alignmentMask + 1) & alignmentMask) == 0);
		// alignment must be a power of 2

	// compute the boundary mask
	uint32 boundaryMask = 0;
	if (restrictions->boundary != 0) {
		page_num_t boundary = restrictions->boundary / B_PAGE_SIZE;
		// boundary must be a power of two and not less than alignment and
		// length
		ASSERT(((boundary - 1) & boundary) == 0);
		ASSERT(boundary >= alignmentMask + 1);
		ASSERT(boundary >= length);

		boundaryMask = -boundary;
	}

	vm_page_reservation reservation;
	vm_page_reserve_pages(&reservation, length, priority);

	WriteLocker freeClearQueueLocker(sFreePageQueuesLock);

	// First we try to get a run with free pages only. If that fails, we also
	// consider cached pages. If there are only few free pages and many cached
	// ones, the odds are that we won't find enough contiguous ones, so we skip
	// the first iteration in this case.
	int32 freePages = sUnreservedFreePages;
	bool useCached = (freePages > 0) && ((page_num_t)freePages > (length * 2));

	for (;;) {
		if (alignmentMask != 0 || boundaryMask != 0) {
			page_num_t offsetStart = start + sPhysicalPageOffset;

			// enforce alignment
			if ((offsetStart & alignmentMask) != 0)
				offsetStart = (offsetStart + alignmentMask) & ~alignmentMask;

			// enforce boundary
			if (boundaryMask != 0 && ((offsetStart ^ (offsetStart
					+ length - 1)) & boundaryMask) != 0) {
				offsetStart = (offsetStart + length - 1) & boundaryMask;
			}

			start = offsetStart - sPhysicalPageOffset;
		}

		if (start + length > end) {
			if (!useCached) {
				// The first iteration with free pages only was unsuccessful.
				// Try again also considering cached pages.
				useCached = true;
				start = requestedStart;
				continue;
			}

			dprintf("vm_page_allocate_page_run(): Failed to allocate run of "
				"length %" B_PRIuPHYSADDR " (%" B_PRIuPHYSADDR " %"
				B_PRIuPHYSADDR ") in second iteration (align: %" B_PRIuPHYSADDR
				" boundary: %" B_PRIuPHYSADDR ")!\n", length, requestedStart,
				end, restrictions->alignment, restrictions->boundary);

			freeClearQueueLocker.Unlock();
			vm_page_unreserve_pages(&reservation);
			return NULL;
		}

		bool foundRun = true;
		page_num_t i;
		for (i = 0; i < length; i++) {
			uint32 pageState = sPages[start + i].State();
			if (pageState != PAGE_STATE_FREE
				&& pageState != PAGE_STATE_CLEAR
				&& (pageState != PAGE_STATE_CACHED || !useCached)) {
				foundRun = false;
				break;
			}
		}

		if (foundRun) {
			i = allocate_page_run(start, length, flags, freeClearQueueLocker);
			if (i == length) {
				reservation.count = 0;
				return &sPages[start];
			}

			// apparently a cached page couldn't be allocated -- skip it and
			// continue
			freeClearQueueLocker.Lock();
		}

		start += i + 1;
	}
}


/**
 * @brief Return the vm_page at the given raw index into the sPages array.
 *
 * @param index Zero-based index into sPages[].
 * @return Pointer to the corresponding vm_page.
 * @note No bounds checking is performed.
 */
vm_page *
vm_page_at_index(int32 index)
{
	return &sPages[index];
}


/**
 * @brief Look up the vm_page descriptor for a given physical page number.
 *
 * Translates \a pageNumber to an index into sPages[], accounting for
 * sPhysicalPageOffset.
 *
 * @param pageNumber Physical page number (physical address >> PAGE_SHIFT).
 * @return Pointer to the vm_page, or NULL if the page number is outside the
 *         managed range.
 */
vm_page *
vm_lookup_page(page_num_t pageNumber)
{
	if (pageNumber < sPhysicalPageOffset)
		return NULL;

	pageNumber -= sPhysicalPageOffset;
	if (pageNumber >= sNumPages)
		return NULL;

	return &sPages[pageNumber];
}


/**
 * @brief Test whether a vm_page pointer refers to a dummy/sentinel page outside the managed array.
 *
 * @param page The page pointer to test.
 * @return true if \a page is outside sPages[0..sNumPages), false if it is a real managed page.
 */
bool
vm_page_is_dummy(struct vm_page *page)
{
	return page < sPages || page >= sPages + sNumPages;
}


/*!	Free the page that belonged to a certain cache.
	You can use vm_page_set_state() manually if you prefer, but only
	if the page does not equal PAGE_STATE_MODIFIED.

	\param cache The cache the page was previously owned by or NULL. The page
		must have been removed from its cache before calling this method in
		either case.
	\param page The page to free.
	\param reservation If not NULL, the page count of the reservation will be
		incremented, thus allowing to allocate another page for the freed one at
		a later time.
*/
/**
 * @brief Free a page that has been removed from its cache, optionally recycling the slot into a reservation.
 *
 * Removes the page from any queue it is in and places it on the free queue.
 * If \a reservation is non-NULL the reservation count is incremented by 1
 * (so the caller can immediately re-use the slot); otherwise the page is
 * returned to the unreserved pool.
 *
 * @param cache       The cache the page previously belonged to, or NULL.
 *                    The page must already have been removed from the cache.
 * @param page        The page to free; must not be in FREE or CLEAR state.
 * @param reservation Optional reservation to recycle the freed page into.
 * @note The page must have been removed from its cache and must not be mapped.
 *       Caller must hold DEBUG_PAGE_ACCESS on the page.
 */
void
vm_page_free_etc(VMCache* cache, vm_page* page,
	vm_page_reservation* reservation)
{
	PAGE_ASSERT(page, page->State() != PAGE_STATE_FREE
		&& page->State() != PAGE_STATE_CLEAR);

	if (page->State() == PAGE_STATE_MODIFIED && (cache != NULL && cache->temporary))
		atomic_add(&sModifiedTemporaryPages, -1);

	free_page(page, false);
	if (reservation == NULL)
		unreserve_pages(1);
	else
		reservation->count++;
}


/**
 * @brief Move a page to the specified queue state (public wrapper around set_page_state()).
 *
 * @param page      The page to transition; must not be in FREE or CLEAR state.
 * @param pageState Target PAGE_STATE_* value; must not be FREE or CLEAR.
 * @note The page's cache must be locked if the page has one.
 *       Page queues must not be locked.
 */
void
vm_page_set_state(vm_page *page, int pageState)
{
	PAGE_ASSERT(page, page->State() != PAGE_STATE_FREE
		&& page->State() != PAGE_STATE_CLEAR);

	set_page_state(page, pageState);
}


/*!	Moves a page to either the tail of the head of its current queue,
	depending on \a tail.
	The page must have a cache and the cache must be locked!
*/
/**
 * @brief Move a page to the head or tail of its current queue without changing its state.
 *
 * @param page The page to requeue; must have a cache, and that cache must be locked.
 * @param tail If true, move to the tail; if false, move to the head.
 * @note The page's cache must be locked.
 *       Panics if called on a page in the FREE or CLEAR state.
 */
void
vm_page_requeue(struct vm_page *page, bool tail)
{
	PAGE_ASSERT(page, page->Cache() != NULL);
	page->Cache()->AssertLocked();
	DEBUG_PAGE_ACCESS_CHECK(page);

	VMPageQueue *queue = NULL;

	switch (page->State()) {
		case PAGE_STATE_ACTIVE:
			queue = &sActivePageQueue;
			break;
		case PAGE_STATE_INACTIVE:
			queue = &sInactivePageQueue;
			break;
		case PAGE_STATE_MODIFIED:
			queue = &sModifiedPageQueue;
			break;
		case PAGE_STATE_CACHED:
			queue = &sCachedPageQueue;
			break;
		case PAGE_STATE_FREE:
		case PAGE_STATE_CLEAR:
			panic("vm_page_requeue() called for free/clear page %p", page);
			return;
		case PAGE_STATE_WIRED:
		case PAGE_STATE_UNUSED:
			return;
		default:
			panic("vm_page_touch: vm_page %p in invalid state %d\n",
				page, page->State());
			break;
	}

	queue->RequeueUnlocked(page, tail);
}


/**
 * @brief Return the total number of physical pages managed by the allocator, excluding non-existing holes.
 *
 * @return sNumPages - sNonExistingPages.
 */
page_num_t
vm_page_num_pages(void)
{
	return sNumPages - sNonExistingPages;
}


/**
 * @brief Return the number of immediately allocatable pages (free + cached).
 *
 * Sums sUnreservedFreePages and the cached queue count, clamped to zero.
 *
 * @return Number of available free and cached pages, or 0 if the count is negative.
 */
page_num_t
vm_page_num_free_pages(void)
{
	int32 count = sUnreservedFreePages + sCachedPageQueue.Count();
	return count > 0 ? count : 0;
}


/**
 * @brief Return the number of truly unreserved free pages (excludes cached pages).
 *
 * @return sUnreservedFreePages clamped to zero.
 */
page_num_t
vm_page_num_unused_pages(void)
{
	int32 count = sUnreservedFreePages;
	return count > 0 ? count : 0;
}


/**
 * @brief Fill a system_info structure with current page and memory statistics.
 *
 * Populates info->max_pages, info->used_pages, info->cached_pages,
 * info->block_cache_pages, info->page_faults, and info->ignored_pages.
 * Block-cache pages are classified as cached rather than used.
 *
 * @param info Pointer to the system_info structure to fill.
 * @note No locking is performed; values are best-effort snapshots and may be
 *       slightly inconsistent if page state changes during the call.
 */
void
vm_page_get_stats(system_info *info)
{
	// Note: there's no locking protecting any of the queues or counters here,
	// so we run the risk of getting bogus values when evaluating them
	// throughout this function. As these stats are for informational purposes
	// only, it is not really worth introducing such locking. Therefore we just
	// ensure that we don't under- or overflow any of the values.

	// The pages used for the block cache buffers. Those should not be counted
	// as used but as cached pages.
	// TODO: We should subtract the blocks that are in use ATM, since those
	// can't really be freed in a low memory situation.
	page_num_t blockCachePages = block_cache_used_memory() / B_PAGE_SIZE;
	info->block_cache_pages = blockCachePages;

	// Non-temporary modified pages are special as they represent pages that
	// can be written back, so they could be freed if necessary, for us
	// basically making them into cached pages with a higher overhead. The
	// modified queue count is therefore split into temporary and non-temporary
	// counts that are then added to the corresponding number.
	page_num_t modifiedNonTemporaryPages
		= (sModifiedPageQueue.Count() - sModifiedTemporaryPages);

	info->max_pages = vm_page_num_pages();
	info->cached_pages = sCachedPageQueue.Count() + modifiedNonTemporaryPages
		+ blockCachePages;

	// max_pages is composed of:
	//	active + inactive + unused + wired + modified + cached + free + clear
	// So taking out the cached (including modified non-temporary), free and
	// clear ones leaves us with all used pages.
	uint32 subtractPages = info->cached_pages + sFreePageQueue.Count()
		+ sClearPageQueue.Count();
	info->used_pages = subtractPages > info->max_pages
		? 0 : info->max_pages - subtractPages;

	if (info->used_pages + info->cached_pages > info->max_pages) {
		// Something was shuffled around while we were summing up the counts.
		// Make the values sane, preferring the worse case of more used pages.
		info->cached_pages = info->max_pages - info->used_pages;
	}

	info->page_faults = vm_num_page_faults();
	info->ignored_pages = sIgnoredPages;

	// TODO: We don't consider pages used for page directories/tables yet.
}


/*!	Returns the greatest address within the last page of accessible physical
	memory.
	The value is inclusive, i.e. in case of a 32 bit phys_addr_t 0xffffffff
	means the that the last page ends at exactly 4 GB.
*/
/**
 * @brief Return the highest inclusive physical address belonging to the last managed page.
 *
 * For a 32-bit phys_addr_t, a return value of 0xffffffff means the last page
 * ends exactly at 4 GB.
 *
 * @return (sPhysicalPageOffset + sNumPages) * B_PAGE_SIZE - 1.
 */
phys_addr_t
vm_page_max_address()
{
	return ((phys_addr_t)sPhysicalPageOffset + sNumPages) * B_PAGE_SIZE - 1;
}


RANGE_MARKER_FUNCTION_END(vm_page)
