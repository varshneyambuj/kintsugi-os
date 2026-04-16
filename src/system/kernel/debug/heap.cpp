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
 *   Copyright 2008-2010, Michael Lotz, mmlr@mlotz.ch.
 *   Copyright 2002-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file heap.cpp
 * @brief KDEBUG debug heap: a diagnostic allocator used in place of the
 *        regular kernel slab/heap when DEBUG_HEAPS is enabled.
 *
 * Provides malloc/free/realloc/memalign replacements that add extensive
 * bookkeeping for catching heap corruption: optional redzone/dead-beef fill
 * patterns (PARANOID_KERNEL_MALLOC / PARANOID_KERNEL_FREE), per-allocation
 * leak-check records (KERNEL_HEAP_LEAK_CHECK) capturing caller/size/thread/
 * team, and full heap-structure validation walks (PARANOID_HEAP_VALIDATION).
 * Exposes KDL debugger commands ("heap", "allocations",
 * "allocations_per_caller") for post-mortem inspection, plus a dedicated
 * grow-heap and VIP heap so out-of-memory recovery does not re-enter the
 * primary heaps. Per-CPU heap arrays reduce contention under load.
 */

#include <arch/debug.h>
#include <debug.h>
#include <elf.h>
#include <heap.h>
#include <interrupts.h>
#include <kernel.h>
#include <lock.h>
#include <string.h>
#include <team.h>
#include <thread.h>
#include <tracing.h>
#include <util/AutoLock.h>
#include <vm/vm.h>
#include <vm/vm_page.h>

#include "heaps.h"


//#define TRACE_HEAP
#ifdef TRACE_HEAP
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


#if DEBUG_HEAPS

#define USE_DEBUG_HEAP_FOR_MALLOC 1
#if !USE_DEBUG_HEAP_FOR_MALLOC
#	undef KERNEL_HEAP_LEAK_CHECK
#endif

// allocate a dedicated 1MB area for dynamic growing
#define HEAP_DEDICATED_GROW_SIZE	1 * 1024 * 1024
// use areas for allocations bigger than 1MB
#define HEAP_AREA_USE_THRESHOLD		1 * 1024 * 1024


#if KERNEL_HEAP_LEAK_CHECK
/**
 * @brief Per-allocation footer recording who made the allocation.
 *
 * Placed at the tail of every allocation when KERNEL_HEAP_LEAK_CHECK is
 * enabled so leak walks can attribute blocks to their originating caller,
 * thread, and team.
 */
typedef struct heap_leak_check_info_s {
	addr_t		caller;
	size_t		size;
	thread_id	thread;
	team_id		team;
} heap_leak_check_info;

/**
 * @brief Aggregated allocation statistics keyed by caller return address.
 *
 * Populated by @c analyze_allocation_callers and consumed by the
 * "allocations_per_caller" KDL command.
 */
struct caller_info {
	addr_t		caller;
	uint32		count;
	uint32		size;
};

/** @brief Maximum number of distinct callers tracked by the leak walk. */
static const int32 kCallerInfoTableSize = 1024;
/** @brief Aggregation table populated during per-caller leak analysis. */
static caller_info sCallerInfoTable[kCallerInfoTableSize];
/** @brief Number of valid entries currently in @c sCallerInfoTable. */
static int32 sCallerInfoCount = 0;
#endif	// KERNEL_HEAP_LEAK_CHECK


/**
 * @brief Static configuration describing one size-class of the debug heap.
 *
 * The table @c sHeapClasses defines three such classes (small, medium,
 * large) which carve the initial heap memory at startup and determine
 * the bin layout inside each allocator.
 */
typedef struct heap_class_s {
	const char *name;
	uint32		initial_percentage;
	size_t		max_allocation_size;
	size_t		page_size;
	size_t		min_bin_size;
	size_t		bin_alignment;
	uint32		min_count_per_page;
	size_t		max_waste_per_page;
} heap_class;

typedef struct heap_page_s heap_page;

/**
 * @brief One backing memory region inside an allocator.
 *
 * Each area owns a contiguous @c page_count page range, a matching
 * @c page_table, and a free-page list. Areas are threaded into two lists
 * on the owning allocator: @c areas (size-ordered, excludes completely
 * full areas) and @c all_areas (base-ordered, used for address lookups).
 */
typedef struct heap_area_s {
	area_id			area;

	addr_t			base;
	size_t			size;

	uint32			page_count;
	uint32			free_page_count;

	heap_page *		free_pages;
	heap_page *		page_table;

	heap_area_s *	prev;
	heap_area_s *	next;
	heap_area_s *	all_next;
} heap_area;

#define MAX_BIN_COUNT	31	// depends on the size of the bin_index field

/**
 * @brief Per-page bookkeeping entry inside an area's page table.
 *
 * Small bin allocations set @c bin_index to the owning bin; large
 * (multi-page) allocations set @c bin_index to the sentinel
 * @c heap->bin_count and share a single @c allocation_id across the run.
 * The free list threads through the page's free slots.
 */
typedef struct heap_page_s {
	heap_area *		area;
	uint16			index;
	uint16			bin_index : 5;
	uint16			free_count : 10;
	uint16			in_use : 1;
	heap_page_s *	next;
	heap_page_s *	prev;
	union {
		uint16			empty_index;
		uint16			allocation_id; // used for bin == bin_count allocations
	};
	addr_t *		free_list;
} heap_page;

/**
 * @brief Bin managing a single fixed-size allocation class.
 *
 * Each bin owns a page list sorted so that the page with the fewest free
 * slots appears first, keeping cache hot pages preferred. Access is
 * serialised by the embedded per-bin mutex.
 */
typedef struct heap_bin_s {
	mutex		lock;
	uint32		element_size;
	uint16		max_free_count;
	heap_page *	page_list; // sorted so that the desired page is always first
} heap_bin;

/**
 * @brief Primary debug-heap allocator instance.
 *
 * One of these is created per heap class (and per CPU where configured).
 * The @c area_lock protects the @c areas / @c all_areas lists, the
 * @c page_lock protects free-page manipulation, and each @c heap_bin has
 * its own mutex. Bookkeeping counters track total / free / empty areas
 * so the grow thread can decide when to enlarge the heap.
 */
struct heap_allocator_s {
	rw_lock		area_lock;
	mutex		page_lock;

	const char *name;
	uint32		bin_count;
	uint32		page_size;

	uint32		total_pages;
	uint32		total_free_pages;
	uint32		empty_areas;

#if KERNEL_HEAP_LEAK_CHECK
	addr_t		(*get_caller)();
#endif

	heap_bin *	bins;
	heap_area *	areas; // sorted so that the desired area is always first
	heap_area *	all_areas; // all areas including full ones
};

typedef struct heap_allocator_s heap_allocator;

static const uint32 kAreaAllocationMagic = 'AAMG';
/**
 * @brief Header stamped at the base of every "huge" (per-area) allocation.
 *
 * Allocations larger than @c HEAP_AREA_USE_THRESHOLD bypass the heaps and
 * get their own @c area_id. The @c kAreaAllocationMagic sentinel lets
 * @c debug_heap_free / @c debug_heap_realloc recognise these blocks and
 * tear them down via @c delete_area.
 */
typedef struct area_allocation_info_s {
	area_id		area;
	void *		base;
	uint32		magic;
	size_t		size;
	size_t		allocation_size;
	size_t		allocation_alignment;
	void *		allocation_base;
} area_allocation_info;


#define VIP_HEAP_SIZE	1024 * 1024

// Heap class configuration
#define HEAP_CLASS_COUNT 3
/**
 * @brief Static heap-class table consumed at allocator creation time.
 *
 * Three classes span small, medium and large allocations with
 * progressively larger page and bin sizes to keep internal fragmentation
 * bounded across the full allocation spectrum.
 */
static const heap_class sHeapClasses[HEAP_CLASS_COUNT] = {
	{
		"small",					/* name */
		50,							/* initial percentage */
		B_PAGE_SIZE / 8,			/* max allocation size */
		B_PAGE_SIZE,				/* page size */
		8,							/* min bin size */
		sizeof(void*),				/* bin alignment */
		8,							/* min count per page */
		16							/* max waste per page */
	},
	{
		"medium",					/* name */
		30,							/* initial percentage */
		B_PAGE_SIZE * 2,			/* max allocation size */
		B_PAGE_SIZE * 8,			/* page size */
		B_PAGE_SIZE / 8,			/* min bin size */
		32,							/* bin alignment */
		4,							/* min count per page */
		64							/* max waste per page */
	},
	{
		"large",					/* name */
		20,							/* initial percentage */
		HEAP_AREA_USE_THRESHOLD,	/* max allocation size */
		B_PAGE_SIZE * 16,			/* page size */
		B_PAGE_SIZE * 2,			/* min bin size */
		128,						/* bin alignment */
		1,							/* min count per page */
		256							/* max waste per page */
	}
};


/** @brief Base of the bootstrap heap memory carved at early init. */
static addr_t sInitialBase;
/** @brief Size of the bootstrap heap memory carved at early init. */
static size_t sInitialSize;

/** @brief Number of active entries in @c sHeaps. */
static uint32 sHeapCount;
/** @brief All heap allocator instances (HEAP_CLASS_COUNT per CPU). */
static heap_allocator *sHeaps[HEAP_CLASS_COUNT * SMP_MAX_CPUS];
/** @brief Monotonic counter of grow requests posted per heap. */
static uint32 *sLastGrowRequest[HEAP_CLASS_COUNT * SMP_MAX_CPUS];
/** @brief Value of @c sLastGrowRequest the grow thread last observed. */
static uint32 *sLastHandledGrowRequest[HEAP_CLASS_COUNT * SMP_MAX_CPUS];

/** @brief Dedicated heap for HEAP_PRIORITY_VIP (interrupt-context) allocs. */
static heap_allocator *sVIPHeap;
/** @brief Private heap used exclusively by the grow thread. */
static heap_allocator *sGrowHeap = NULL;
/** @brief Thread ID of the heap grower. */
static thread_id sHeapGrowThread = -1;
/** @brief Semaphore used to request heap growth. */
static sem_id sHeapGrowSem = -1;
/** @brief Semaphore released once growth has completed. */
static sem_id sHeapGrownNotify = -1;
/** @brief Flag asking for a new grow heap area on the next grow pass. */
static bool sAddGrowHeap = false;


// #pragma mark - Tracing

#if KERNEL_HEAP_TRACING
namespace KernelHeapTracing {

/**
 * @brief Trace entry recording a successful heap allocation.
 */
class Allocate : public AbstractTraceEntry {
	public:
		/**
		 * @brief Constructs the trace entry and marks it initialised so
		 *        the tracing subsystem publishes it.
		 *
		 * @param address Address returned by the allocator.
		 * @param size    Requested size in bytes.
		 */
		Allocate(addr_t address, size_t size)
			:	fAddress(address),
				fSize(size)
		{
			Initialized();
		}

		/**
		 * @brief Renders a human-readable description of the entry.
		 *
		 * @param out Sink used by the tracing subsystem.
		 */
		virtual void AddDump(TraceOutput &out)
		{
			out.Print("heap allocate: 0x%08lx (%lu bytes)", fAddress, fSize);
		}

	private:
		addr_t	fAddress;
		size_t	fSize;
};


/**
 * @brief Trace entry recording a heap reallocation.
 */
class Reallocate : public AbstractTraceEntry {
	public:
		/**
		 * @brief Constructs the trace entry and marks it initialised.
		 *
		 * @param oldAddress Address of the source allocation.
		 * @param newAddress Address of the resulting allocation.
		 * @param newSize    Requested new size.
		 */
		Reallocate(addr_t oldAddress, addr_t newAddress, size_t newSize)
			:	fOldAddress(oldAddress),
				fNewAddress(newAddress),
				fNewSize(newSize)
		{
			Initialized();
		};

		/**
		 * @brief Renders a human-readable description of the entry.
		 *
		 * @param out Sink used by the tracing subsystem.
		 */
		virtual void AddDump(TraceOutput &out)
		{
			out.Print("heap reallocate: 0x%08lx -> 0x%08lx (%lu bytes)",
				fOldAddress, fNewAddress, fNewSize);
		}

	private:
		addr_t	fOldAddress;
		addr_t	fNewAddress;
		size_t	fNewSize;
};


/**
 * @brief Trace entry recording a heap free.
 */
class Free : public AbstractTraceEntry {
	public:
		/**
		 * @brief Constructs the trace entry and marks it initialised.
		 *
		 * @param address Address whose allocation was released.
		 */
		Free(addr_t address)
			:	fAddress(address)
		{
			Initialized();
		};

		/**
		 * @brief Renders a human-readable description of the entry.
		 *
		 * @param out Sink used by the tracing subsystem.
		 */
		virtual void AddDump(TraceOutput &out)
		{
			out.Print("heap free: 0x%08lx", fAddress);
		}

	private:
		addr_t	fAddress;
};


} // namespace KernelHeapTracing

#	define T(x)	if (!gKernelStartup) new(std::nothrow) KernelHeapTracing::x;
#else
#	define T(x)	;
#endif


// #pragma mark - Debug functions


#if KERNEL_HEAP_LEAK_CHECK
/**
 * @brief Walks the current call stack to find the first return address
 *        outside the allocator code, used to attribute allocations.
 *
 * Scans up to five return addresses and returns the first one whose value
 * is below @c get_caller itself, which heuristically skips frames that
 * belong to the heap machinery.
 *
 * @return Caller return address, or 0 if none could be determined.
 */
static addr_t
get_caller()
{
	// Find the first return address outside of the allocator code. Note, that
	// this makes certain assumptions about how the code for the functions
	// ends up in the kernel object.
	addr_t returnAddresses[5];
	int32 depth = arch_debug_get_stack_trace(returnAddresses, 5, 0, 1,
		STACK_TRACE_KERNEL);
	for (int32 i = 0; i < depth; i++) {
		if (returnAddresses[i] < (addr_t)&get_caller)
			return returnAddresses[i];
	}

	return 0;
}
#endif


/**
 * @brief Prints a single heap page's bookkeeping to the debugger console.
 *
 * Walks the page's free list, counting entries, and dumps bin_index,
 * free_count, empty_index and the free-list head. Called from KDL.
 *
 * @param page Heap page to describe.
 */
static void
dump_page(heap_page *page)
{
	uint32 count = 0;
	for (addr_t *temp = page->free_list; temp != NULL; temp = (addr_t *)*temp)
		count++;

	kprintf("\t\tpage %p: bin_index: %u; free_count: %u; empty_index: %u; "
		"free_list %p (%" B_PRIu32 " entr%s)\n", page, page->bin_index,
		page->free_count, page->empty_index, page->free_list, count,
		count == 1 ? "y" : "ies");
}


/**
 * @brief Prints a single heap bin and each of its pages.
 *
 * @param bin Bin whose metadata and page list should be dumped.
 */
static void
dump_bin(heap_bin *bin)
{
	uint32 count = 0;
	for (heap_page *page = bin->page_list; page != NULL; page = page->next)
		count++;

	kprintf("\telement_size: %" B_PRIu32 "; max_free_count: %u; page_list %p "
		"(%" B_PRIu32 " pages);\n", bin->element_size, bin->max_free_count,
		bin->page_list, count);

	for (heap_page *page = bin->page_list; page != NULL; page = page->next)
		dump_page(page);
}


/**
 * @brief Prints all bins in the given allocator.
 *
 * @param heap Allocator whose bin list should be dumped.
 */
static void
dump_bin_list(heap_allocator *heap)
{
	for (uint32 i = 0; i < heap->bin_count; i++)
		dump_bin(&heap->bins[i]);
	kprintf("\n");
}


/**
 * @brief Prints each area belonging to an allocator (base/size/free pages).
 *
 * @param heap Allocator whose @c all_areas list should be walked.
 */
static void
dump_allocator_areas(heap_allocator *heap)
{
	heap_area *area = heap->all_areas;
	while (area) {
		kprintf("\tarea %p: area: %" B_PRId32 "; base: %p; size: %zu; page_count: "
			"%" B_PRIu32 "; free_pages: %p (%" B_PRIu32 " entr%s)\n", area,
			area->area, (void *)area->base, area->size, area->page_count,
			area->free_pages, area->free_page_count,
			area->free_page_count == 1 ? "y" : "ies");
		area = area->all_next;
	}

	kprintf("\n");
}


/**
 * @brief Prints a one-line summary of an allocator plus optional detail.
 *
 * @param heap  Allocator to describe.
 * @param areas If true, also dump the per-area list.
 * @param bins  If true, also dump the per-bin list.
 */
static void
dump_allocator(heap_allocator *heap, bool areas, bool bins)
{
	kprintf("allocator %p: name: %s; page_size: %" B_PRIu32 "; bin_count: "
		"%" B_PRIu32 "; pages: %" B_PRIu32 "; free_pages: %" B_PRIu32 "; "
		"empty_areas: %" B_PRIu32 "\n", heap, heap->name, heap->page_size,
		heap->bin_count, heap->total_pages, heap->total_free_pages,
		heap->empty_areas);

	if (areas)
		dump_allocator_areas(heap);
	if (bins)
		dump_bin_list(heap);
}


/**
 * @brief KDL command handler that dumps info about one or all kernel heaps.
 *
 * Invoked from the kernel debugger with interrupts disabled. Accepts
 * "grow" to print only the dedicated grow heap, "stats" to limit output,
 * or a raw heap address expression.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 always (command result code).
 */
static int
dump_heap_list(int argc, char **argv)
{
#if USE_DEBUG_HEAP_FOR_MALLOC
	if (argc == 2 && strcmp(argv[1], "grow") == 0) {
		// only dump dedicated grow heap info
		kprintf("dedicated grow heap:\n");
		dump_allocator(sGrowHeap, true, true);
		return 0;
	}
#endif

	bool stats = false;
	int i = 1;

	if (strcmp(argv[1], "stats") == 0) {
		stats = true;
		i++;
	}

	uint64 heapAddress = 0;
	if (i < argc && !evaluate_debug_expression(argv[i], &heapAddress, true)) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	if (heapAddress == 0) {
#if USE_DEBUG_HEAP_FOR_MALLOC
		// dump default kernel heaps
		for (uint32 i = 0; i < sHeapCount; i++)
			dump_allocator(sHeaps[i], !stats, !stats);
#else
		print_debugger_command_usage(argv[0]);
#endif
	} else {
		// dump specified heap
		dump_allocator((heap_allocator*)(addr_t)heapAddress, !stats, !stats);
	}

	return 0;
}


#if !KERNEL_HEAP_LEAK_CHECK

/**
 * @brief KDL command handler that dumps live allocations without leak
 *        tracking compiled in.
 *
 * Runs with interrupts disabled from the debugger context. Iterates every
 * page of every area of every (or the specified) heap, distinguishing
 * small-bin allocations (by consulting the per-page free list) from
 * multi-page large allocations (which share an @c allocation_id). With
 * "stats" only the totals are printed.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success or after printing usage.
 */
static int
dump_allocations(int argc, char **argv)
{
	uint64 heapAddress = 0;
	bool statsOnly = false;
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "stats") == 0)
			statsOnly = true;
		else if (!evaluate_debug_expression(argv[i], &heapAddress, true)) {
			print_debugger_command_usage(argv[0]);
			return 0;
		}
	}

	size_t totalSize = 0;
	uint32 totalCount = 0;
#if USE_DEBUG_HEAP_FOR_MALLOC
	for (uint32 heapIndex = 0; heapIndex < sHeapCount; heapIndex++) {
		heap_allocator *heap = sHeaps[heapIndex];
		if (heapAddress != 0)
			heap = (heap_allocator *)(addr_t)heapAddress;
#else
	while (true) {
		heap_allocator *heap = (heap_allocator *)(addr_t)heapAddress;
		if (heap == NULL) {
			print_debugger_command_usage(argv[0]);
			return 0;
		}
#endif
#if 0
	}
#endif

		// go through all the pages in all the areas
		heap_area *area = heap->all_areas;
		while (area) {
			for (uint32 i = 0; i < area->page_count; i++) {
				heap_page *page = &area->page_table[i];
				if (!page->in_use)
					continue;

				addr_t base = area->base + i * heap->page_size;
				if (page->bin_index < heap->bin_count) {
					// page is used by a small allocation bin
					uint32 elementCount = page->empty_index;
					size_t elementSize
						= heap->bins[page->bin_index].element_size;
					for (uint32 j = 0; j < elementCount;
							j++, base += elementSize) {
						// walk the free list to see if this element is in use
						bool elementInUse = true;
						for (addr_t *temp = page->free_list; temp != NULL;
								temp = (addr_t *)*temp) {
							if ((addr_t)temp == base) {
								elementInUse = false;
								break;
							}
						}

						if (!elementInUse)
							continue;

						if (!statsOnly) {
							kprintf("address: 0x%p; size: %lu bytes\n",
								(void *)base, elementSize);
						}

						totalSize += elementSize;
						totalCount++;
					}
				} else {
					// page is used by a big allocation, find the page count
					uint32 pageCount = 1;
					while (i + pageCount < area->page_count
						&& area->page_table[i + pageCount].in_use
						&& area->page_table[i + pageCount].bin_index
							== heap->bin_count
						&& area->page_table[i + pageCount].allocation_id
							== page->allocation_id)
						pageCount++;

					size_t size = pageCount * heap->page_size;

					if (!statsOnly) {
						kprintf("address: %p; size: %lu bytes\n", (void *)base,
							size);
					}

					totalSize += size;
					totalCount++;

					// skip the allocated pages
					i += pageCount - 1;
				}
			}

			area = area->all_next;
		}

		if (heapAddress != 0)
			break;
	}

	kprintf("total allocations: %" B_PRIu32 "; total bytes: %zu\n", totalCount, totalSize);
	return 0;
}

#else // !KERNEL_HEAP_LEAK_CHECK

/**
 * @brief KDL command handler that dumps live allocations with leak-check
 *        metadata filtering.
 *
 * Runs with interrupts disabled in the debugger. Supports filtering by
 * team, thread, caller or address using the @c heap_leak_check_info record
 * stored at the tail of each allocation. With "stats" only counts/totals
 * are printed.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success or after printing usage.
 */
static int
dump_allocations(int argc, char **argv)
{
	team_id team = -1;
	thread_id thread = -1;
	addr_t caller = 0;
	addr_t address = 0;
	bool statsOnly = false;

	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "team") == 0)
			team = parse_expression(argv[++i]);
		else if (strcmp(argv[i], "thread") == 0)
			thread = parse_expression(argv[++i]);
		else if (strcmp(argv[i], "caller") == 0)
			caller = parse_expression(argv[++i]);
		else if (strcmp(argv[i], "address") == 0)
			address = parse_expression(argv[++i]);
		else if (strcmp(argv[i], "stats") == 0)
			statsOnly = true;
		else {
			print_debugger_command_usage(argv[0]);
			return 0;
		}
	}

	size_t totalSize = 0;
	uint32 totalCount = 0;
	for (uint32 heapIndex = 0; heapIndex < sHeapCount; heapIndex++) {
		heap_allocator *heap = sHeaps[heapIndex];

		// go through all the pages in all the areas
		heap_area *area = heap->all_areas;
		while (area) {
			heap_leak_check_info *info = NULL;
			for (uint32 i = 0; i < area->page_count; i++) {
				heap_page *page = &area->page_table[i];
				if (!page->in_use)
					continue;

				addr_t base = area->base + i * heap->page_size;
				if (page->bin_index < heap->bin_count) {
					// page is used by a small allocation bin
					uint32 elementCount = page->empty_index;
					size_t elementSize
						= heap->bins[page->bin_index].element_size;
					for (uint32 j = 0; j < elementCount;
							j++, base += elementSize) {
						// walk the free list to see if this element is in use
						bool elementInUse = true;
						for (addr_t *temp = page->free_list; temp != NULL;
								temp = (addr_t *)*temp) {
							if ((addr_t)temp == base) {
								elementInUse = false;
								break;
							}
						}

						if (!elementInUse)
							continue;

						info = (heap_leak_check_info *)(base + elementSize
							- sizeof(heap_leak_check_info));

						if ((team == -1 || info->team == team)
							&& (thread == -1 || info->thread == thread)
							&& (caller == 0 || info->caller == caller)
							&& (address == 0 || base == address)) {
							// interesting...
							if (!statsOnly) {
								kprintf("team: % 6" B_PRId32 "; thread: % 6" B_PRId32 "; "
									"address: 0x%08lx; size: %lu bytes; "
									"caller: %#lx\n", info->team, info->thread,
									base, info->size, info->caller);
							}

							totalSize += info->size;
							totalCount++;
						}
					}
				} else {
					// page is used by a big allocation, find the page count
					uint32 pageCount = 1;
					while (i + pageCount < area->page_count
						&& area->page_table[i + pageCount].in_use
						&& area->page_table[i + pageCount].bin_index
							== heap->bin_count
						&& area->page_table[i + pageCount].allocation_id
							== page->allocation_id)
						pageCount++;

					info = (heap_leak_check_info *)(base + pageCount
						* heap->page_size - sizeof(heap_leak_check_info));

					if ((team == -1 || info->team == team)
						&& (thread == -1 || info->thread == thread)
						&& (caller == 0 || info->caller == caller)
						&& (address == 0 || base == address)) {
						// interesting...
						if (!statsOnly) {
							kprintf("team: % 6" B_PRId32 "; thread: % 6" B_PRId32 ";"
								" address: 0x%08lx; size: %lu bytes;"
								" caller: %#lx\n", info->team, info->thread,
								base, info->size, info->caller);
						}

						totalSize += info->size;
						totalCount++;
					}

					// skip the allocated pages
					i += pageCount - 1;
				}
			}

			area = area->all_next;
		}
	}

	kprintf("total allocations: %" B_PRIu32 "; total bytes: %" B_PRIuSIZE "\n",
		totalCount, totalSize);
	return 0;
}


/**
 * @brief Returns (and lazily creates) the caller_info entry for @p caller.
 *
 * The entries are stored in a small fixed-size table populated during the
 * per-caller summary run. Returns NULL when the table is exhausted.
 *
 * @param caller Caller return address to look up.
 * @return Pointer to an existing or new caller_info, or NULL if the table
 *         is full.
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
	info->size = 0;

	return info;
}


/**
 * @brief qsort comparator: orders caller_info entries by descending size.
 *
 * @param _a First caller_info pointer.
 * @param _b Second caller_info pointer.
 * @return Negative, zero, or positive per qsort convention.
 */
static int
caller_info_compare_size(const void* _a, const void* _b)
{
	const caller_info* a = (const caller_info*)_a;
	const caller_info* b = (const caller_info*)_b;
	return (int)(b->size - a->size);
}


/**
 * @brief qsort comparator: orders caller_info entries by descending count.
 *
 * @param _a First caller_info pointer.
 * @param _b Second caller_info pointer.
 * @return Negative, zero, or positive per qsort convention.
 */
static int
caller_info_compare_count(const void* _a, const void* _b)
{
	const caller_info* a = (const caller_info*)_a;
	const caller_info* b = (const caller_info*)_b;
	return (int)(b->count - a->count);
}


/**
 * @brief Walks every live allocation in @p heap and accumulates per-caller
 *        counts/sizes into @c sCallerInfoTable.
 *
 * Used to build the summary displayed by the "allocations_per_caller" KDL
 * command. The walk handles both bin-sized and multi-page large
 * allocations, reading the @c heap_leak_check_info record placed at the
 * tail of each allocation.
 *
 * @param heap Allocator to traverse.
 * @return True on success, false if the caller info table overflowed.
 */
static bool
analyze_allocation_callers(heap_allocator *heap)
{
	// go through all the pages in all the areas
	heap_area *area = heap->all_areas;
	while (area) {
		heap_leak_check_info *info = NULL;
		for (uint32 i = 0; i < area->page_count; i++) {
			heap_page *page = &area->page_table[i];
			if (!page->in_use)
				continue;

			addr_t base = area->base + i * heap->page_size;
			if (page->bin_index < heap->bin_count) {
				// page is used by a small allocation bin
				uint32 elementCount = page->empty_index;
				size_t elementSize = heap->bins[page->bin_index].element_size;
				for (uint32 j = 0; j < elementCount; j++, base += elementSize) {
					// walk the free list to see if this element is in use
					bool elementInUse = true;
					for (addr_t *temp = page->free_list; temp != NULL;
						temp = (addr_t *)*temp) {
						if ((addr_t)temp == base) {
							elementInUse = false;
							break;
						}
					}

					if (!elementInUse)
						continue;

					info = (heap_leak_check_info *)(base + elementSize
						- sizeof(heap_leak_check_info));

					caller_info *callerInfo = get_caller_info(info->caller);
					if (callerInfo == NULL) {
						kprintf("out of space for caller infos\n");
						return false;
					}

					callerInfo->count++;
					callerInfo->size += info->size;
				}
			} else {
				// page is used by a big allocation, find the page count
				uint32 pageCount = 1;
				while (i + pageCount < area->page_count
					&& area->page_table[i + pageCount].in_use
					&& area->page_table[i + pageCount].bin_index
						== heap->bin_count
					&& area->page_table[i + pageCount].allocation_id
						== page->allocation_id) {
					pageCount++;
				}

				info = (heap_leak_check_info *)(base + pageCount
					* heap->page_size - sizeof(heap_leak_check_info));

				caller_info *callerInfo = get_caller_info(info->caller);
				if (callerInfo == NULL) {
					kprintf("out of space for caller infos\n");
					return false;
				}

				callerInfo->count++;
				callerInfo->size += info->size;

				// skip the allocated pages
				i += pageCount - 1;
			}
		}

		area = area->all_next;
	}

	return true;
}


/**
 * @brief KDL command handler summarising live allocations grouped by the
 *        captured caller address.
 *
 * Runs with interrupts disabled from the debugger. Accepts @c -c to sort
 * by allocation count instead of total size, and @c -h to restrict the
 * analysis to a specific heap address. For each caller, prints count,
 * size, and the resolved ELF symbol when available.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success or after printing usage.
 */
static int
dump_allocations_per_caller(int argc, char **argv)
{
	bool sortBySize = true;
	heap_allocator *heap = NULL;

	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-c") == 0) {
			sortBySize = false;
		} else if (strcmp(argv[i], "-h") == 0) {
			uint64 heapAddress;
			if (++i >= argc
				|| !evaluate_debug_expression(argv[i], &heapAddress, true)) {
				print_debugger_command_usage(argv[0]);
				return 0;
			}

			heap = (heap_allocator*)(addr_t)heapAddress;
		} else {
			print_debugger_command_usage(argv[0]);
			return 0;
		}
	}

	sCallerInfoCount = 0;

	if (heap != NULL) {
		if (!analyze_allocation_callers(heap))
			return 0;
	} else {
		for (uint32 heapIndex = 0; heapIndex < sHeapCount; heapIndex++) {
			if (!analyze_allocation_callers(sHeaps[heapIndex]))
				return 0;
		}
	}

	// sort the array
	qsort(sCallerInfoTable, sCallerInfoCount, sizeof(caller_info),
		sortBySize ? &caller_info_compare_size : &caller_info_compare_count);

	kprintf("%" B_PRId32 " different callers, sorted by %s...\n\n",
		sCallerInfoCount, sortBySize ? "size" : "count");

	kprintf("     count        size      caller\n");
	kprintf("----------------------------------\n");
	for (int32 i = 0; i < sCallerInfoCount; i++) {
		caller_info& info = sCallerInfoTable[i];
		kprintf("%10" B_PRId32 "  %10" B_PRId32 "  %#08lx", info.count, info.size, info.caller);

		const char *symbol;
		const char *imageName;
		bool exactMatch;
		addr_t baseAddress;

		if (elf_debug_lookup_symbol_address(info.caller, &baseAddress, &symbol,
				&imageName, &exactMatch) == B_OK) {
			kprintf("  %s + 0x%lx (%s)%s\n", symbol,
				info.caller - baseAddress, imageName,
				exactMatch ? "" : " (nearest)");
		} else
			kprintf("\n");
	}

	return 0;
}

#endif // KERNEL_HEAP_LEAK_CHECK


#if PARANOID_HEAP_VALIDATION
/**
 * @brief Exhaustively validates every internal data structure of a heap,
 *        panicking on the first corruption it detects.
 *
 * Acquires all locks (area read lock, every bin lock, and the page lock)
 * then walks: the per-area free-page list (checking pointer bounds,
 * indices, prev-link integrity, and the @c free_page_count counter); the
 * @c areas and @c all_areas ordering invariants; and each bin's page list
 * including per-page free lists (which must lie on element boundaries).
 * Corruptions caught include orphaned pages, double-free chains,
 * mis-linked pages, and free-count mismatches.
 *
 * @param heap Allocator to validate.
 */
static void
heap_validate_heap(heap_allocator *heap)
{
	ReadLocker areaReadLocker(heap->area_lock);
	for (uint32 i = 0; i < heap->bin_count; i++)
		mutex_lock(&heap->bins[i].lock);
	MutexLocker pageLocker(heap->page_lock);

	uint32 totalPageCount = 0;
	uint32 totalFreePageCount = 0;
	heap_area *area = heap->all_areas;
	while (area != NULL) {
		// validate the free pages list
		uint32 freePageCount = 0;
		heap_page *lastPage = NULL;
		heap_page *page = area->free_pages;
		while (page) {
			if ((addr_t)page < (addr_t)&area->page_table[0]
				|| (addr_t)page >= (addr_t)&area->page_table[area->page_count])
				panic("free page is not part of the page table\n");

			if (page->index >= area->page_count)
				panic("free page has invalid index\n");

			if ((addr_t)&area->page_table[page->index] != (addr_t)page)
				panic("free page index does not lead to target page\n");

			if (page->prev != lastPage)
				panic("free page entry has invalid prev link\n");

			if (page->in_use)
				panic("free page marked as in use\n");

			lastPage = page;
			page = page->next;
			freePageCount++;
		}

		totalPageCount += freePageCount;
		totalFreePageCount += freePageCount;
		if (area->free_page_count != freePageCount)
			panic("free page count doesn't match free page list\n");

		// validate the page table
		uint32 usedPageCount = 0;
		for (uint32 i = 0; i < area->page_count; i++) {
			if (area->page_table[i].in_use)
				usedPageCount++;
		}

		totalPageCount += usedPageCount;
		if (freePageCount + usedPageCount != area->page_count) {
			panic("free pages and used pages do not add up (%lu + %lu != %lu)\n",
				freePageCount, usedPageCount, area->page_count);
		}

		area = area->all_next;
	}

	// validate the areas
	area = heap->areas;
	heap_area *lastArea = NULL;
	uint32 lastFreeCount = 0;
	while (area != NULL) {
		if (area->free_page_count < lastFreeCount)
			panic("size ordering of area list broken\n");

		if (area->prev != lastArea)
			panic("area list entry has invalid prev link\n");

		lastArea = area;
		lastFreeCount = area->free_page_count;
		area = area->next;
	}

	lastArea = NULL;
	area = heap->all_areas;
	while (area != NULL) {
		if (lastArea != NULL && lastArea->base < area->base)
			panic("base ordering of all_areas list broken\n");

		lastArea = area;
		area = area->all_next;
	}

	// validate the bins
	for (uint32 i = 0; i < heap->bin_count; i++) {
		heap_bin *bin = &heap->bins[i];
		heap_page *lastPage = NULL;
		heap_page *page = bin->page_list;
		lastFreeCount = 0;
		while (page) {
			area = heap->all_areas;
			while (area) {
				if (area == page->area)
					break;
				area = area->all_next;
			}

			if (area == NULL) {
				panic("page area not present in area list\n");
				page = page->next;
				continue;
			}

			if ((addr_t)page < (addr_t)&area->page_table[0]
				|| (addr_t)page >= (addr_t)&area->page_table[area->page_count])
				panic("used page is not part of the page table\n");

			if (page->index >= area->page_count)
				panic("used page has invalid index\n");

			if ((addr_t)&area->page_table[page->index] != (addr_t)page)
				panic("used page index does not lead to target page\n");

			if (page->prev != lastPage) {
				panic("used page entry has invalid prev link (%p vs %p bin "
					"%lu)\n", page->prev, lastPage, i);
			}

			if (!page->in_use)
				panic("used page marked as not in use\n");

			if (page->bin_index != i) {
				panic("used page with bin index %u in page list of bin %lu\n",
					page->bin_index, i);
			}

			if (page->free_count < lastFreeCount)
				panic("ordering of bin page list broken\n");

			// validate the free list
			uint32 freeSlotsCount = 0;
			addr_t *element = page->free_list;
			addr_t pageBase = area->base + page->index * heap->page_size;
			while (element) {
				if ((addr_t)element < pageBase
					|| (addr_t)element >= pageBase + heap->page_size)
					panic("free list entry out of page range\n");

				if (((addr_t)element - pageBase) % bin->element_size != 0)
					panic("free list entry not on a element boundary\n");

				element = (addr_t *)*element;
				freeSlotsCount++;
			}

			uint32 slotCount = bin->max_free_count;
			if (page->empty_index > slotCount) {
				panic("empty index beyond slot count (%u with %lu slots)\n",
					page->empty_index, slotCount);
			}

			freeSlotsCount += (slotCount - page->empty_index);
			if (freeSlotsCount > slotCount)
				panic("more free slots than fit into the page\n");

			lastPage = page;
			lastFreeCount = page->free_count;
			page = page->next;
		}
	}

	pageLocker.Unlock();
	for (uint32 i = 0; i < heap->bin_count; i++)
		mutex_unlock(&heap->bins[i].lock);
	areaReadLocker.Unlock();
}
#endif // PARANOID_HEAP_VALIDATION


// #pragma mark - Heap functions


/**
 * @brief Adds a memory region to a heap allocator as a new heap area.
 *
 * Carves the area header and page table out of the front of @p base, then
 * inserts the area into both the size-ordered @c areas list (empty, at the
 * tail) and the base-ordered @c all_areas list. Adjusts the heap's total
 * page and free-page counters. Takes the area write lock and page lock.
 *
 * @param heap   Allocator receiving the new area.
 * @param areaID Backing @c area_id, or -1 for the initial bootstrap area.
 * @param base   Base address of the raw memory region.
 * @param size   Size of the region in bytes.
 */
void
heap_add_area(heap_allocator *heap, area_id areaID, addr_t base, size_t size)
{
	heap_area *area = (heap_area *)base;
	area->area = areaID;

	base += sizeof(heap_area);
	size -= sizeof(heap_area);

	uint32 pageCount = size / heap->page_size;
	size_t pageTableSize = pageCount * sizeof(heap_page);
	area->page_table = (heap_page *)base;
	base += pageTableSize;
	size -= pageTableSize;

	// the rest is now actually usable memory (rounded to the next page)
	area->base = ROUNDUP(base, B_PAGE_SIZE);
	area->size = size & ~(B_PAGE_SIZE - 1);

	// now we know the real page count
	pageCount = area->size / heap->page_size;
	area->page_count = pageCount;

	// zero out the page table and fill in page indexes
	memset((void *)area->page_table, 0, pageTableSize);
	for (uint32 i = 0; i < pageCount; i++) {
		area->page_table[i].area = area;
		area->page_table[i].index = i;
	}

	// add all pages up into the free pages list
	for (uint32 i = 1; i < pageCount; i++) {
		area->page_table[i - 1].next = &area->page_table[i];
		area->page_table[i].prev = &area->page_table[i - 1];
	}
	area->free_pages = &area->page_table[0];
	area->free_page_count = pageCount;
	area->page_table[0].prev = NULL;
	area->next = NULL;

	WriteLocker areaWriteLocker(heap->area_lock);
	MutexLocker pageLocker(heap->page_lock);
	if (heap->areas == NULL) {
		// it's the only (empty) area in that heap
		area->prev = NULL;
		heap->areas = area;
	} else {
		// link in this area as the last one as it is completely empty
		heap_area *lastArea = heap->areas;
		while (lastArea->next != NULL)
			lastArea = lastArea->next;

		lastArea->next = area;
		area->prev = lastArea;
	}

	// insert this area in the all_areas list so it stays ordered by base
	if (heap->all_areas == NULL || heap->all_areas->base < area->base) {
		area->all_next = heap->all_areas;
		heap->all_areas = area;
	} else {
		heap_area *insert = heap->all_areas;
		while (insert->all_next && insert->all_next->base > area->base)
			insert = insert->all_next;

		area->all_next = insert->all_next;
		insert->all_next = area;
	}

	heap->total_pages += area->page_count;
	heap->total_free_pages += area->free_page_count;

	if (areaID >= 0) {
		// this later on deletable area is yet empty - the empty count will be
		// decremented as soon as this area is used for the first time
		heap->empty_areas++;
	}

	pageLocker.Unlock();
	areaWriteLocker.Unlock();

	dprintf("heap_add_area: area %" B_PRId32 " added to %s heap %p - usable "
		"range %p - %p\n", area->area, heap->name, heap, (void *)area->base,
		(void *)(area->base + area->size));
}


/**
 * @brief Detaches an entirely-empty area from an allocator.
 *
 * Assumes both the area write lock and the page lock are held by the
 * caller. Panics if the area still has in-use pages or is the sole
 * non-full area left. Updates the @c areas and @c all_areas lists and
 * decrements heap-wide page counters; the backing @c area_id is deleted
 * by the caller after the locks are dropped.
 *
 * @param heap Allocator owning the area.
 * @param area Area to unlink.
 * @return B_OK on success, B_ERROR if preconditions are violated.
 */
static status_t
heap_remove_area(heap_allocator *heap, heap_area *area)
{
	if (area->free_page_count != area->page_count) {
		panic("tried removing heap area that has still pages in use");
		return B_ERROR;
	}

	if (area->prev == NULL && area->next == NULL) {
		panic("tried removing the last non-full heap area");
		return B_ERROR;
	}

	if (heap->areas == area)
		heap->areas = area->next;
	if (area->prev != NULL)
		area->prev->next = area->next;
	if (area->next != NULL)
		area->next->prev = area->prev;

	if (heap->all_areas == area)
		heap->all_areas = area->all_next;
	else {
		heap_area *previous = heap->all_areas;
		while (previous) {
			if (previous->all_next == area) {
				previous->all_next = area->all_next;
				break;
			}

			previous = previous->all_next;
		}

		if (previous == NULL)
			panic("removing heap area that is not in all list");
	}

	heap->total_pages -= area->page_count;
	heap->total_free_pages -= area->free_page_count;

	dprintf("heap_remove_area: area %" B_PRId32 " with range %p - %p removed "
		"from %s heap %p\n", area->area, (void *)area->base,
		(void *)(area->base + area->size), heap->name, heap);

	return B_OK;
}


/**
 * @brief Constructs a new heap_allocator either at the head of @p base or
 *        on the existing kernel heap.
 *
 * Computes the bin layout from @p heapClass (min_bin_size, min_count_per_
 * page, bin_alignment, max_waste_per_page), initialises per-bin mutexes,
 * the area rw-lock and page lock, then calls @c heap_add_area to install
 * the remaining memory as the allocator's first area.
 *
 * @param name          Name used in debug output.
 * @param base          Base of the backing memory region.
 * @param size          Size of the backing memory region.
 * @param heapClass     Configuration describing bin shapes.
 * @param allocateOnHeap If true, allocate the header via @c malloc rather
 *                      than embedding it in @p base.
 * @return The newly initialised allocator.
 */
static heap_allocator *
heap_create_allocator(const char *name, addr_t base, size_t size,
	const heap_class *heapClass, bool allocateOnHeap)
{
	heap_allocator *heap;
	if (allocateOnHeap) {
		// allocate seperately on the heap
		heap = (heap_allocator *)malloc(sizeof(heap_allocator)
			+ sizeof(heap_bin) * MAX_BIN_COUNT);
	} else {
		// use up the first part of the area
		heap = (heap_allocator *)base;
		base += sizeof(heap_allocator);
		size -= sizeof(heap_allocator);
	}

	heap->name = name;
	heap->page_size = heapClass->page_size;
	heap->total_pages = heap->total_free_pages = heap->empty_areas = 0;
	heap->areas = heap->all_areas = NULL;
	heap->bins = (heap_bin *)((addr_t)heap + sizeof(heap_allocator));

#if KERNEL_HEAP_LEAK_CHECK
	heap->get_caller = &get_caller;
#endif

	heap->bin_count = 0;
	size_t binSize = 0, lastSize = 0;
	uint32 count = heap->page_size / heapClass->min_bin_size;
	for (; count >= heapClass->min_count_per_page; count--, lastSize = binSize) {
		if (heap->bin_count >= MAX_BIN_COUNT)
			panic("heap configuration invalid - max bin count reached\n");

		binSize = (heap->page_size / count) & ~(heapClass->bin_alignment - 1);
		if (binSize == lastSize)
			continue;
		if (heap->page_size - count * binSize > heapClass->max_waste_per_page)
			continue;

		heap_bin *bin = &heap->bins[heap->bin_count];
		mutex_init(&bin->lock, "heap bin lock");
		bin->element_size = binSize;
		bin->max_free_count = heap->page_size / binSize;
		bin->page_list = NULL;
		heap->bin_count++;
	};

	if (!allocateOnHeap) {
		base += heap->bin_count * sizeof(heap_bin);
		size -= heap->bin_count * sizeof(heap_bin);
	}

	rw_lock_init(&heap->area_lock, "heap area rw lock");
	mutex_init(&heap->page_lock, "heap page lock");

	heap_add_area(heap, -1, base, size);
	return heap;
}


/**
 * @brief Updates bookkeeping after @p pageCount pages are returned to the
 *        area's free list.
 *
 * Assumes the heap's page lock is held by the caller. Re-links the area
 * into the size-ordered @c areas list so that areas with more free pages
 * appear first, and updates the heap's empty-area count when an area
 * becomes completely free.
 *
 * @param heap      Owning allocator.
 * @param area      Area whose pages were freed.
 * @param pageCount Number of pages just returned.
 */
static inline void
heap_free_pages_added(heap_allocator *heap, heap_area *area, uint32 pageCount)
{
	area->free_page_count += pageCount;
	heap->total_free_pages += pageCount;

	if (area->free_page_count == pageCount) {
		// we need to add ourselfs to the area list of the heap
		area->prev = NULL;
		area->next = heap->areas;
		if (area->next)
			area->next->prev = area;
		heap->areas = area;
	} else {
		// we might need to move back in the area list
		if (area->next && area->next->free_page_count < area->free_page_count) {
			// move ourselfs so the list stays ordered
			heap_area *insert = area->next;
			while (insert->next
				&& insert->next->free_page_count < area->free_page_count)
				insert = insert->next;

			if (area->prev)
				area->prev->next = area->next;
			if (area->next)
				area->next->prev = area->prev;
			if (heap->areas == area)
				heap->areas = area->next;

			area->prev = insert;
			area->next = insert->next;
			if (area->next)
				area->next->prev = area;
			insert->next = area;
		}
	}

	if (area->free_page_count == area->page_count && area->area >= 0)
		heap->empty_areas++;
}


/**
 * @brief Updates bookkeeping after @p pageCount pages are taken from an
 *        area's free list.
 *
 * Assumes the heap's page lock is held. Removes the area from @c areas
 * if it is now full, or re-sorts it forward so areas with more free pages
 * stay at the head. Also decrements the empty-area count if applicable.
 *
 * @param heap      Owning allocator.
 * @param area      Area whose pages were consumed.
 * @param pageCount Number of pages just taken.
 */
static inline void
heap_free_pages_removed(heap_allocator *heap, heap_area *area, uint32 pageCount)
{
	if (area->free_page_count == area->page_count && area->area >= 0) {
		// this area was completely empty
		heap->empty_areas--;
	}

	area->free_page_count -= pageCount;
	heap->total_free_pages -= pageCount;

	if (area->free_page_count == 0) {
		// the area is now full so we remove it from the area list
		if (area->prev)
			area->prev->next = area->next;
		if (area->next)
			area->next->prev = area->prev;
		if (heap->areas == area)
			heap->areas = area->next;
		area->next = area->prev = NULL;
	} else {
		// we might need to move forward in the area list
		if (area->prev && area->prev->free_page_count > area->free_page_count) {
			// move ourselfs so the list stays ordered
			heap_area *insert = area->prev;
			while (insert->prev
				&& insert->prev->free_page_count > area->free_page_count)
				insert = insert->prev;

			if (area->prev)
				area->prev->next = area->next;
			if (area->next)
				area->next->prev = area->prev;

			area->prev = insert->prev;
			area->next = insert;
			if (area->prev)
				area->prev->next = area;
			if (heap->areas == insert)
				heap->areas = area;
			insert->prev = area;
		}
	}
}


/**
 * @brief Inserts @p page at the head of a doubly-linked page list.
 *
 * Assumes the appropriate bin or area lock is held by the caller.
 *
 * @param page Page to insert.
 * @param list Address of the list head pointer.
 */
static inline void
heap_link_page(heap_page *page, heap_page **list)
{
	page->prev = NULL;
	page->next = *list;
	if (page->next)
		page->next->prev = page;
	*list = page;
}


/**
 * @brief Removes @p page from a doubly-linked page list, fixing up the
 *        head pointer if needed.
 *
 * Assumes the appropriate bin or area lock is held by the caller.
 *
 * @param page Page to unlink.
 * @param list Address of the list head pointer, or NULL if only neighbour
 *             fix-up is needed.
 */
static inline void
heap_unlink_page(heap_page *page, heap_page **list)
{
	if (page->prev)
		page->prev->next = page->next;
	if (page->next)
		page->next->prev = page->prev;
	if (list && *list == page) {
		*list = page->next;
		if (page->next)
			page->next->prev = NULL;
	}
}


/**
 * @brief Finds and reserves @p pageCount contiguous free pages across the
 *        allocator's areas honouring @p alignment.
 *
 * Acquires the heap page lock. Iterates the size-ordered @c areas list,
 * stepping by @p alignment when it exceeds the page size, and greedily
 * searches for a run of free pages. Marks each selected page in-use,
 * tags @c bin_index as the "large allocation" sentinel and stamps the
 * starting index into @c allocation_id for later free/realloc walks.
 *
 * @param heap      Allocator to allocate from.
 * @param pageCount Number of contiguous pages required.
 * @param alignment Required alignment in bytes (may exceed the page size).
 * @return First page of the run, or NULL if none of the areas can satisfy.
 */
static heap_page *
heap_allocate_contiguous_pages(heap_allocator *heap, uint32 pageCount,
	size_t alignment)
{
	MutexLocker pageLocker(heap->page_lock);
	heap_area *area = heap->areas;
	while (area) {
		if (area->free_page_count < pageCount) {
			area = area->next;
			continue;
		}

		uint32 step = 1;
		uint32 firstValid = 0;
		const uint32 lastValid = area->page_count - pageCount + 1;

		if (alignment > heap->page_size) {
			firstValid = (ROUNDUP(area->base, alignment) - area->base)
				/ heap->page_size;
			step = alignment / heap->page_size;
		}

		int32 first = -1;
		for (uint32 i = firstValid; i < lastValid; i += step) {
			if (area->page_table[i].in_use)
				continue;

			first = i;

			for (uint32 j = 1; j < pageCount; j++) {
				if (area->page_table[i + j].in_use) {
					first = -1;
					i += j / step * step;
					break;
				}
			}

			if (first >= 0)
				break;
		}

		if (first < 0) {
			area = area->next;
			continue;
		}

		for (uint32 i = first; i < first + pageCount; i++) {
			heap_page *page = &area->page_table[i];
			page->in_use = 1;
			page->bin_index = heap->bin_count;

			heap_unlink_page(page, &area->free_pages);

			page->next = page->prev = NULL;
			page->free_list = NULL;
			page->allocation_id = (uint16)first;
		}

		heap_free_pages_removed(heap, area, pageCount);
		return &area->page_table[first];
	}

	return NULL;
}


#if KERNEL_HEAP_LEAK_CHECK
/**
 * @brief Stamps a @c heap_leak_check_info footer at the tail of an
 *        allocation so future walks can attribute it.
 *
 * The record captures the requested size, current thread/team, and the
 * caller return address reported by the allocator's @c get_caller hook.
 *
 * @param heap      Allocator performing the allocation (for its caller
 *                  hook).
 * @param address   Base of the allocation.
 * @param allocated Total bytes reserved (bin element size or page run).
 * @param size      Requested user-visible size including leak-check fudge.
 */
static void
heap_add_leak_check_info(heap_allocator *heap, addr_t address, size_t allocated,
	size_t size)
{
	heap_leak_check_info *info = (heap_leak_check_info *)(address + allocated
		- sizeof(heap_leak_check_info));
	info->size = size - sizeof(heap_leak_check_info);
	info->thread = (gKernelStartup ? 0 : thread_get_current_thread_id());
	info->team = (gKernelStartup ? 0 : team_get_current_team_id());
	info->caller = heap->get_caller();
}
#endif


/**
 * @brief Serves an allocation by reserving a page run from the allocator
 *        rather than from a bin.
 *
 * Rounds @p size up to a whole number of pages, requests contiguous pages
 * via @c heap_allocate_contiguous_pages, and stamps leak-check metadata
 * into the tail when compiled in.
 *
 * @param heap      Allocator to allocate from.
 * @param size      Requested size in bytes.
 * @param alignment Requested alignment (forwarded to the page allocator).
 * @return Pointer to the allocated memory, or NULL on failure.
 */
static void *
heap_raw_alloc(heap_allocator *heap, size_t size, size_t alignment)
{
	TRACE(("heap %p: allocate %lu bytes from raw pages with alignment %lu\n",
		heap, size, alignment));

	uint32 pageCount = (size + heap->page_size - 1) / heap->page_size;
	heap_page *firstPage = heap_allocate_contiguous_pages(heap, pageCount,
		alignment);
	if (firstPage == NULL) {
		TRACE(("heap %p: found no contiguous pages to allocate %ld bytes\n",
			heap, size));
		return NULL;
	}

	addr_t address = firstPage->area->base + firstPage->index * heap->page_size;
#if KERNEL_HEAP_LEAK_CHECK
	heap_add_leak_check_info(heap, address, pageCount * heap->page_size, size);
#endif
	return (void *)address;
}


/**
 * @brief Allocates one element from a specific bin of @p heap.
 *
 * Acquires the bin lock (and, on cold paths that need a new page, the
 * page lock). If the bin has no partially-used page, pulls one off the
 * area free list and initialises it. Reuses a previously-freed slot via
 * the page's @c free_list when present; otherwise bumps @c empty_index.
 * Updates the bin page list ordering when the page becomes full.
 *
 * @param heap     Allocator to allocate from.
 * @param binIndex Index into @c heap->bins.
 * @param size     Requested user size (used for leak-check metadata).
 * @return Pointer to the allocated element, or NULL if no pages are free.
 */
static void *
heap_allocate_from_bin(heap_allocator *heap, uint32 binIndex, size_t size)
{
	heap_bin *bin = &heap->bins[binIndex];
	TRACE(("heap %p: allocate %lu bytes from bin %lu with element_size %lu\n",
		heap, size, binIndex, bin->element_size));

	MutexLocker binLocker(bin->lock);
	heap_page *page = bin->page_list;
	if (page == NULL) {
		MutexLocker pageLocker(heap->page_lock);
		heap_area *area = heap->areas;
		if (area == NULL) {
			TRACE(("heap %p: no free pages to allocate %lu bytes\n", heap,
				size));
			return NULL;
		}

		// by design there are only areas in the list that still have
		// free pages available
		page = area->free_pages;
		area->free_pages = page->next;
		if (page->next)
			page->next->prev = NULL;

		heap_free_pages_removed(heap, area, 1);

		if (page->in_use)
			panic("got an in use page %p from the free pages list\n", page);
		page->in_use = 1;

		pageLocker.Unlock();

		page->bin_index = binIndex;
		page->free_count = bin->max_free_count;
		page->empty_index = 0;
		page->free_list = NULL;
		page->next = page->prev = NULL;
		bin->page_list = page;
	}

	// we have a page where we have a free slot
	void *address = NULL;
	if (page->free_list) {
		// there's a previously freed entry we can use
		address = page->free_list;
		page->free_list = (addr_t *)*page->free_list;
	} else {
		// the page hasn't been fully allocated so use the next empty_index
		address = (void *)(page->area->base + page->index * heap->page_size
			+ page->empty_index * bin->element_size);
		page->empty_index++;
	}

	page->free_count--;
	if (page->free_count == 0) {
		// the page is now full so we remove it from the page_list
		bin->page_list = page->next;
		if (page->next)
			page->next->prev = NULL;
		page->next = page->prev = NULL;
	}

#if KERNEL_HEAP_LEAK_CHECK
	binLocker.Unlock();
	heap_add_leak_check_info(heap, (addr_t)address, bin->element_size, size);
#endif
	return address;
}


/**
 * @brief Tests whether @p number is zero or a power of two.
 *
 * Used to sanity-check alignment arguments passed to memalign.
 *
 * @param number Candidate alignment value.
 * @return True if @p number is zero or a single-bit integer.
 */
static bool
is_valid_alignment(size_t number)
{
	// this cryptic line accepts zero and all powers of two
	return ((~number + 1) | ((number << 1) - 1)) == ~0UL;
}


/**
 * @brief Returns true when @p heap is close enough to full that the grow
 *        thread should be notified.
 *
 * The threshold is 20% of the configured grow size of free memory.
 *
 * @param heap Allocator to inspect.
 * @return True if the heap should be grown soon.
 */
inline bool
heap_should_grow(heap_allocator *heap)
{
	// suggest growing if there is less than 20% of a grow size available
	return heap->total_free_pages * heap->page_size < kernel_debug_heap.grow_size / 5;
}


/**
 * @brief Core aligned allocation entry point for a single heap instance.
 *
 * Picks the smallest suitable bin whose element size satisfies @p size and
 * whose alignment matches when @p alignment is non-zero; otherwise falls
 * back to @c heap_raw_alloc. When PARANOID_KERNEL_MALLOC is enabled the
 * returned memory is filled with 0xcc, and when PARANOID_KERNEL_FREE is
 * enabled the second word is cleared of its stale 0xdeadbeef poison so
 * a subsequent free does not misdiagnose double-free. Records the
 * allocation with the tracing macro @c T(Allocate...).
 *
 * @param heap      Allocator to serve from.
 * @param alignment Required alignment (must be a power of two, or zero).
 * @param size      Requested size in bytes.
 * @return Pointer to the allocation, or NULL on failure.
 */
static void *
heap_memalign(heap_allocator *heap, size_t alignment, size_t size)
{
	TRACE(("memalign(alignment = %lu, size = %lu)\n", alignment, size));

#if DEBUG
	if (!is_valid_alignment(alignment))
		panic("memalign() with an alignment which is not a power of 2\n");
#endif

#if KERNEL_HEAP_LEAK_CHECK
	size += sizeof(heap_leak_check_info);
#endif

	void *address = NULL;
	if (alignment < B_PAGE_SIZE) {
		if (alignment != 0) {
			// TODO: The alignment is done by ensuring that the element size
			// of the target bin is aligned with the requested alignment. This
			// has the problem that it wastes space because a better (smaller)
			// bin could possibly be selected. We should pick the best bin and
			// check if there is an aligned block in the free list or if a new
			// (page aligned) page has to be allocated anyway.
			size = ROUNDUP(size, alignment);
			for (uint32 i = 0; i < heap->bin_count; i++) {
				if (size <= heap->bins[i].element_size
					&& is_valid_alignment(heap->bins[i].element_size)) {
					address = heap_allocate_from_bin(heap, i, size);
					break;
				}
			}
		} else {
			for (uint32 i = 0; i < heap->bin_count; i++) {
				if (size <= heap->bins[i].element_size) {
					address = heap_allocate_from_bin(heap, i, size);
					break;
				}
			}
		}
	}

	if (address == NULL)
		address = heap_raw_alloc(heap, size, alignment);

#if KERNEL_HEAP_LEAK_CHECK
	size -= sizeof(heap_leak_check_info);
#endif

	TRACE(("memalign(): asked to allocate %lu bytes, returning pointer %p\n",
		size, address));

	T(Allocate((addr_t)address, size));
	if (address == NULL)
		return address;

#if PARANOID_KERNEL_MALLOC
	memset(address, 0xcc, size);
#endif

#if PARANOID_KERNEL_FREE
	// make sure 0xdeadbeef is cleared if we do not overwrite the memory
	// and the user does not clear it
	if (((uint32 *)address)[1] == 0xdeadbeef)
		((uint32 *)address)[1] = 0xcccccccc;
#endif

	return address;
}


/**
 * @brief Releases an allocation back to its owning heap.
 *
 * Walks the base-ordered @c all_areas list to locate the containing area
 * (returning @c B_ENTRY_NOT_FOUND if the address is not ours). For small
 * allocations: when PARANOID_KERNEL_FREE is on, detects double-free by
 * checking the 0xdeadbeef sentinel and by scanning the page's free list;
 * otherwise pushes the address onto the free list and overwrites the rest
 * with 0xdeadbeef. Rejects misaligned pointers. Updates the bin page list
 * and, if the page becomes empty, returns it to the area free-page list.
 * For large allocations, walks the consecutive pages sharing the same
 * @c allocation_id and releases them all at once. When more than one
 * empty area exists, deletes the redundant ones to return memory.
 *
 * @param heap    Allocator to attempt the free on.
 * @param address Pointer returned by a previous allocation, or NULL.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if the address is not in this
 *         heap, or B_ERROR on detected corruption.
 */
static status_t
heap_free(heap_allocator *heap, void *address)
{
	if (address == NULL)
		return B_OK;

	ReadLocker areaReadLocker(heap->area_lock);
	heap_area *area = heap->all_areas;
	while (area) {
		// since the all_areas list is ordered by base with the biggest
		// base at the top, we need only find the first area with a base
		// smaller than our address to become our only candidate for freeing
		if (area->base <= (addr_t)address) {
			if ((addr_t)address >= area->base + area->size) {
				// none of the other areas can contain the address as the list
				// is ordered
				return B_ENTRY_NOT_FOUND;
			}

			// this area contains the allocation, we're done searching
			break;
		}

		area = area->all_next;
	}

	if (area == NULL) {
		// this address does not belong to us
		return B_ENTRY_NOT_FOUND;
	}

	TRACE(("free(): asked to free pointer %p\n", address));

	heap_page *page = &area->page_table[((addr_t)address - area->base)
		/ heap->page_size];

	TRACE(("free(): page %p: bin_index %d, free_count %d\n", page,
		page->bin_index, page->free_count));

	if (page->bin_index > heap->bin_count) {
		panic("free(): page %p: invalid bin_index %d\n", page, page->bin_index);
		return B_ERROR;
	}

	if (page->bin_index < heap->bin_count) {
		// small allocation
		heap_bin *bin = &heap->bins[page->bin_index];

#if PARANOID_KERNEL_FREE
		if (((uint32 *)address)[1] == 0xdeadbeef) {
			// This block looks like it was freed already, walk the free list
			// on this page to make sure this address doesn't exist.
			MutexLocker binLocker(bin->lock);
			for (addr_t *temp = page->free_list; temp != NULL;
					temp = (addr_t *)*temp) {
				if (temp == address) {
					panic("free(): address %p already exists in page free "
						"list\n", address);
					return B_ERROR;
				}
			}
		}

		// the first 4 bytes are overwritten with the next free list pointer
		// later
		uint32 *dead = (uint32 *)address;
		for (uint32 i = 1; i < bin->element_size / sizeof(uint32); i++)
			dead[i] = 0xdeadbeef;
#endif

		MutexLocker binLocker(bin->lock);
		if (((addr_t)address - area->base - page->index
			* heap->page_size) % bin->element_size != 0) {
			panic("free(): passed invalid pointer %p supposed to be in bin for "
				"element size %" B_PRIu32 "\n", address, bin->element_size);
			return B_ERROR;
		}

		// add the address to the page free list
		*(addr_t *)address = (addr_t)page->free_list;
		page->free_list = (addr_t *)address;
		page->free_count++;

		if (page->free_count == bin->max_free_count) {
			// we are now empty, remove the page from the bin list
			MutexLocker pageLocker(heap->page_lock);
			heap_unlink_page(page, &bin->page_list);
			page->in_use = 0;
			heap_link_page(page, &area->free_pages);
			heap_free_pages_added(heap, area, 1);
		} else if (page->free_count == 1) {
			// we need to add ourselfs to the page list of the bin
			heap_link_page(page, &bin->page_list);
		} else {
			// we might need to move back in the free pages list
			if (page->next && page->next->free_count < page->free_count) {
				// move ourselfs so the list stays ordered
				heap_page *insert = page->next;
				while (insert->next
					&& insert->next->free_count < page->free_count)
					insert = insert->next;

				heap_unlink_page(page, &bin->page_list);

				page->prev = insert;
				page->next = insert->next;
				if (page->next)
					page->next->prev = page;
				insert->next = page;
			}
		}
	} else {
		// large allocation, just return the pages to the page free list
		uint32 allocationID = page->allocation_id;
		uint32 maxPages = area->page_count - page->index;
		uint32 pageCount = 0;

		MutexLocker pageLocker(heap->page_lock);
		for (uint32 i = 0; i < maxPages; i++) {
			// loop until we find the end of this allocation
			if (!page[i].in_use || page[i].bin_index != heap->bin_count
				|| page[i].allocation_id != allocationID)
				break;

			// this page still belongs to the same allocation
			page[i].in_use = 0;
			page[i].allocation_id = 0;

			// return it to the free list
			heap_link_page(&page[i], &area->free_pages);
			pageCount++;
		}

		heap_free_pages_added(heap, area, pageCount);
	}

	T(Free((addr_t)address));
	areaReadLocker.Unlock();

	if (heap->empty_areas > 1) {
		WriteLocker areaWriteLocker(heap->area_lock);
		MutexLocker pageLocker(heap->page_lock);

		area_id areasToDelete[heap->empty_areas - 1];
		int32 areasToDeleteIndex = 0;

		area = heap->areas;
		while (area != NULL && heap->empty_areas > 1) {
			heap_area *next = area->next;
			if (area->area >= 0
				&& area->free_page_count == area->page_count
				&& heap_remove_area(heap, area) == B_OK) {
				areasToDelete[areasToDeleteIndex++] = area->area;
				heap->empty_areas--;
			}

			area = next;
		}

		pageLocker.Unlock();
		areaWriteLocker.Unlock();

		for (int32 i = 0; i < areasToDeleteIndex; i++)
			delete_area(areasToDelete[i]);
	}

	return B_OK;
}


#if KERNEL_HEAP_LEAK_CHECK
/**
 * @brief Overrides the caller-capture hook used by an allocator.
 *
 * Allows subsystems such as the slab layer to install a custom unwind
 * routine so that leak records attribute allocations to their real
 * user instead of the inner heap machinery.
 *
 * @param heap      Allocator whose hook should be replaced.
 * @param getCaller New caller-resolution function.
 */
extern "C" void
heap_set_get_caller(heap_allocator* heap, addr_t (*getCaller)())
{
	heap->get_caller = getCaller;
}
#endif


/**
 * @brief Resizes an allocation belonging to @p heap.
 *
 * Locates the source area via the base-ordered @c all_areas list. Works
 * out the current min/max size (bin element size boundary for small
 * allocations, consecutive same-@c allocation_id page count for large
 * ones). If @p newSize fits in the existing slot the pointer is reused
 * and leak-check metadata updated in place; otherwise a new block is
 * allocated with @c malloc_etc, the payload is copied up to the smaller
 * of the two sizes, and the original is freed.
 *
 * @param heap       Allocator that owns the block.
 * @param address    Current allocation.
 * @param newAddress [out] Resulting pointer (may equal @p address).
 * @param newSize    Requested new size.
 * @param flags      Allocation flags forwarded to @c malloc_etc.
 * @return B_OK on success or when the allocator could not fulfil (in
 *         which case @c *newAddress is NULL), or B_ENTRY_NOT_FOUND if the
 *         address does not belong to @p heap.
 */
static status_t
heap_realloc(heap_allocator *heap, void *address, void **newAddress,
	size_t newSize, uint32 flags)
{
	ReadLocker areaReadLocker(heap->area_lock);
	heap_area *area = heap->all_areas;
	while (area) {
		// since the all_areas list is ordered by base with the biggest
		// base at the top, we need only find the first area with a base
		// smaller than our address to become our only candidate for
		// reallocating
		if (area->base <= (addr_t)address) {
			if ((addr_t)address >= area->base + area->size) {
				// none of the other areas can contain the address as the list
				// is ordered
				return B_ENTRY_NOT_FOUND;
			}

			// this area contains the allocation, we're done searching
			break;
		}

		area = area->all_next;
	}

	if (area == NULL) {
		// this address does not belong to us
		return B_ENTRY_NOT_FOUND;
	}

	TRACE(("realloc(address = %p, newSize = %lu)\n", address, newSize));

	heap_page *page = &area->page_table[((addr_t)address - area->base)
		/ heap->page_size];
	if (page->bin_index > heap->bin_count) {
		panic("realloc(): page %p: invalid bin_index %d\n", page,
			page->bin_index);
		return B_ERROR;
	}

	// find out the size of the old allocation first
	size_t minSize = 0;
	size_t maxSize = 0;
	if (page->bin_index < heap->bin_count) {
		// this was a small allocation
		heap_bin *bin = &heap->bins[page->bin_index];
		maxSize = bin->element_size;
		if (page->bin_index > 0)
			minSize = heap->bins[page->bin_index - 1].element_size + 1;
	} else {
		// this was a large allocation
		uint32 allocationID = page->allocation_id;
		uint32 maxPages = area->page_count - page->index;
		maxSize = heap->page_size;

		MutexLocker pageLocker(heap->page_lock);
		for (uint32 i = 1; i < maxPages; i++) {
			if (!page[i].in_use || page[i].bin_index != heap->bin_count
				|| page[i].allocation_id != allocationID)
				break;

			minSize += heap->page_size;
			maxSize += heap->page_size;
		}
	}

	areaReadLocker.Unlock();

#if KERNEL_HEAP_LEAK_CHECK
	newSize += sizeof(heap_leak_check_info);
#endif

	// does the new allocation simply fit in the old allocation?
	if (newSize > minSize && newSize <= maxSize) {
#if KERNEL_HEAP_LEAK_CHECK
		// update the size info (the info is at the end so stays where it is)
		heap_leak_check_info *info = (heap_leak_check_info *)((addr_t)address
			+ maxSize - sizeof(heap_leak_check_info));
		info->size = newSize - sizeof(heap_leak_check_info);
		newSize -= sizeof(heap_leak_check_info);
#endif

		T(Reallocate((addr_t)address, (addr_t)address, newSize));
		*newAddress = address;
		return B_OK;
	}

#if KERNEL_HEAP_LEAK_CHECK
	// new leak check info will be created with the malloc below
	newSize -= sizeof(heap_leak_check_info);
#endif

	// if not, allocate a new chunk of memory
	*newAddress = malloc_etc(newSize, flags);
	T(Reallocate((addr_t)address, (addr_t)*newAddress, newSize));
	if (*newAddress == NULL) {
		// we tried but it didn't work out, but still the operation is done
		return B_OK;
	}

	// copy the old data and free the old allocation
	memcpy(*newAddress, address, min_c(maxSize, newSize));
	heap_free(heap, address);
	return B_OK;
}


/**
 * @brief Selects the heap index that should serve an allocation of a
 *        given size on a given CPU.
 *
 * Chooses the smallest heap class whose @c max_allocation_size covers
 * @p size, then rotates across per-CPU duplicates to spread contention.
 *
 * @param size Requested allocation size (leak-check overhead added when
 *             compiled in).
 * @param cpu  Origin CPU number.
 * @return Index into @c sHeaps.
 */
inline uint32
heap_index_for(size_t size, int32 cpu)
{
#if KERNEL_HEAP_LEAK_CHECK
	// take the extra info size into account
	size += sizeof(heap_leak_check_info_s);
#endif

	uint32 index = 0;
	for (; index < HEAP_CLASS_COUNT - 1; index++) {
		if (size <= sHeapClasses[index].max_allocation_size)
			break;
	}

	return (index + cpu * HEAP_CLASS_COUNT) % sHeapCount;
}


/**
 * @brief Attempts an allocation without triggering synchronous heap
 *        growth.
 *
 * Used when the caller cannot block (e.g. the grow thread itself, or
 * allocations flagged @c HEAP_DONT_WAIT_FOR_MEMORY). Prefers the
 * dedicated grow heap when running on the grow thread, nudging the heap
 * grower asynchronously when its reserves look thin; otherwise walks the
 * per-CPU public heaps in rotation. Returns NULL rather than waiting for
 * more memory.
 *
 * @param alignment Required alignment.
 * @param size      Requested size in bytes.
 * @return Pointer to the allocation, or NULL if no heap could satisfy.
 */
static void *
memalign_nogrow(size_t alignment, size_t size)
{
	// use dedicated memory in the grow thread by default
	if (thread_get_current_thread_id() == sHeapGrowThread) {
		void *result = heap_memalign(sGrowHeap, alignment, size);
		if (!sAddGrowHeap && heap_should_grow(sGrowHeap)) {
			// hopefully the heap grower will manage to create a new heap
			// before running out of private memory...
			dprintf("heap: requesting new grow heap\n");
			sAddGrowHeap = true;
			release_sem_etc(sHeapGrowSem, 1, B_DO_NOT_RESCHEDULE);
		}

		if (result != NULL)
			return result;
	}

	// try public memory, there might be something available
	void *result = NULL;
	int32 cpuCount = MIN(smp_get_num_cpus(),
		(int32)sHeapCount / HEAP_CLASS_COUNT);
	int32 cpuNumber = smp_get_current_cpu();
	for (int32 i = 0; i < cpuCount; i++) {
		uint32 heapIndex = heap_index_for(size, cpuNumber++ % cpuCount);
		heap_allocator *heap = sHeaps[heapIndex];
		result = heap_memalign(heap, alignment, size);
		if (result != NULL)
			return result;
	}

	// no memory available
	if (thread_get_current_thread_id() == sHeapGrowThread)
		panic("heap: all heaps have run out of memory while growing\n");
	else
		dprintf("heap: all heaps have run out of memory\n");

	return NULL;
}


/**
 * @brief Creates a fresh backing kernel area and attaches it to @p heap.
 *
 * Invoked from the grow thread to enlarge a heap that is running low. On
 * PARANOID_HEAP_VALIDATION builds the newly enlarged heap is then
 * re-validated.
 *
 * @param heap Allocator to extend.
 * @param name Debug name for the new area.
 * @param size Size of the new area in bytes.
 * @return B_OK on success; propagated @c create_area error otherwise.
 */
static status_t
heap_create_new_heap_area(heap_allocator *heap, const char *name, size_t size)
{
	void *address = NULL;
	area_id heapArea = create_area(name, &address,
		B_ANY_KERNEL_BLOCK_ADDRESS, size, B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (heapArea < B_OK) {
		TRACE(("heap: couldn't allocate heap area \"%s\"\n", name));
		return heapArea;
	}

	heap_add_area(heap, heapArea, (addr_t)address, size);
#if PARANOID_HEAP_VALIDATION
	heap_validate_heap(heap);
#endif
	return B_OK;
}


/**
 * @brief Worker thread body that grows the public and grow heaps on
 *        demand.
 *
 * Blocks on @c sHeapGrowSem, then extends the dedicated grow heap first
 * when requested, followed by any public heap that has accumulated an
 * un-handled grow request or that @c heap_should_grow now deems low.
 * Signals @c sHeapGrownNotify to release waiters retrying failed
 * allocations.
 *
 * @return Never returns.
 */
static int32
heap_grow_thread(void *)
{
	while (true) {
		// wait for a request to grow the heap list
		if (acquire_sem(sHeapGrowSem) < B_OK)
			continue;

		if (sAddGrowHeap) {
			// the grow heap is going to run full soon, try to allocate a new
			// one to make some room.
			TRACE(("heap_grower: grow heaps will run out of memory soon\n"));
			if (heap_create_new_heap_area(sGrowHeap, "additional grow heap",
					HEAP_DEDICATED_GROW_SIZE) != B_OK)
				dprintf("heap_grower: failed to create new grow heap area\n");
		}

		for (uint32 i = 0; i < sHeapCount; i++) {
			heap_allocator *heap = sHeaps[i];
			if (sLastGrowRequest[i] > sLastHandledGrowRequest[i]
				|| heap_should_grow(heap)) {
				// grow this heap if it is nearly full or if a grow was
				// explicitly requested for this heap (happens when a large
				// allocation cannot be fulfilled due to lack of contiguous
				// pages)
				if (heap_create_new_heap_area(heap, "additional heap",
						kernel_debug_heap.grow_size) != B_OK)
					dprintf("heap_grower: failed to create new heap area\n");
				sLastHandledGrowRequest[i] = sLastGrowRequest[i];
			}
		}

		// notify anyone waiting for this request
		release_sem_etc(sHeapGrownNotify, -1, B_RELEASE_ALL);
	}

	return 0;
}


//	#pragma mark -


/**
 * @brief First-phase initialiser for the debug heap.
 *
 * Carves the initial kernel heap memory into one allocator per heap class
 * using each class's @c initial_percentage, and registers KDL commands
 * ("heap", "allocations", "allocations_per_caller") so debugging support
 * is available as early as possible.
 *
 * @param base  Base of the initial kernel heap region.
 * @param size  Size of that region.
 * @return B_OK.
 */
static status_t
debug_heap_init(struct kernel_args*, addr_t base, size_t size)
{
	sInitialBase = base;
	sInitialSize = size;

	for (uint32 i = 0; i < HEAP_CLASS_COUNT; i++) {
		size_t partSize = size * sHeapClasses[i].initial_percentage / 100;
		sHeaps[i] = heap_create_allocator(sHeapClasses[i].name, base, partSize,
			&sHeapClasses[i], false);
		sLastGrowRequest[i] = sLastHandledGrowRequest[i] = 0;
		base += partSize;
		sHeapCount++;
	}

	// set up some debug commands
	add_debugger_command_etc("heap", &dump_heap_list,
		"Dump infos about the kernel heap(s)",
		"[(\"grow\" | \"stats\" | <heap>)]\n"
		"Dump infos about the kernel heap(s). If \"grow\" is specified, only\n"
		"infos about the dedicated grow heap are printed. If \"stats\" is\n"
		"given as the argument, currently only the heap count is printed.\n"
		"If <heap> is given, it is interpreted as the address of the heap to\n"
		"print infos about.\n", 0);
#if !KERNEL_HEAP_LEAK_CHECK
	add_debugger_command_etc("allocations", &dump_allocations,
		"Dump current heap allocations",
		"[\"stats\"] [<heap>]\n"
		"If no parameters are given, all current alloactions are dumped.\n"
		"If the optional argument \"stats\" is specified, only the allocation\n"
		"counts and no individual allocations are printed\n"
		"If a specific heap address is given, only allocations of this\n"
		"allocator are dumped\n", 0);
#else // !KERNEL_HEAP_LEAK_CHECK
	add_debugger_command_etc("allocations", &dump_allocations,
		"Dump current heap allocations",
		"[(\"team\" | \"thread\") <id>] [\"caller\" <address>] [\"address\" <address>] [\"stats\"]\n"
		"If no parameters are given, all current alloactions are dumped.\n"
		"If \"team\", \"thread\", \"caller\", and/or \"address\" is specified as the first\n"
		"argument, only allocations matching the team ID, thread ID, caller\n"
		"address or allocated address given in the second argument are printed.\n"
		"If the optional argument \"stats\" is specified, only the allocation\n"
		"counts and no individual allocations are printed.\n", 0);
	add_debugger_command_etc("allocations_per_caller",
		&dump_allocations_per_caller,
		"Dump current heap allocations summed up per caller",
		"[ \"-c\" ] [ -h <heap> ]\n"
		"The current allocations will by summed up by caller (their count and\n"
		"size) printed in decreasing order by size or, if \"-c\" is\n"
		"specified, by allocation count. If given <heap> specifies the\n"
		"address of the heap for which to print the allocations.\n", 0);
#endif // KERNEL_HEAP_LEAK_CHECK
	return B_OK;
}


/**
 * @brief Area-subsystem init phase: promotes the bootstrap heap and
 *        creates the dedicated grow and VIP heaps.
 *
 * Wraps the pre-existing kernel heap memory in a proper @c area_id,
 * allocates a private 1 MiB "grow" area used by the grow thread to avoid
 * re-entering the public heaps, and a 1 MiB "VIP I/O" heap for
 * allocations tagged @c HEAP_PRIORITY_VIP.
 *
 * @return B_OK on success; B_ERROR or propagated @c create_area error
 *         otherwise.
 */
static status_t
debug_heap_init_post_area()
{
	create_area("kernel heap", (void**)&sInitialBase, B_EXACT_ADDRESS,
		sInitialSize, B_ALREADY_WIRED, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);

	void *address = NULL;
	area_id growHeapArea = create_area("dedicated grow heap", &address,
		B_ANY_KERNEL_BLOCK_ADDRESS, HEAP_DEDICATED_GROW_SIZE, B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (growHeapArea < 0) {
		panic("heap_init_post_area(): couldn't allocate dedicate grow heap "
			"area");
		return growHeapArea;
	}

	sGrowHeap = heap_create_allocator("grow", (addr_t)address,
		HEAP_DEDICATED_GROW_SIZE, &sHeapClasses[0], false);
	if (sGrowHeap == NULL) {
		panic("heap_init_post_area(): failed to create dedicated grow heap\n");
		return B_ERROR;
	}

	// create the VIP heap
	static const heap_class heapClass = {
		"VIP I/O",					/* name */
		100,						/* initial percentage */
		B_PAGE_SIZE / 8,			/* max allocation size */
		B_PAGE_SIZE,				/* page size */
		8,							/* min bin size */
		sizeof(void*),				/* bin alignment */
		8,							/* min count per page */
		16							/* max waste per page */
	};

	area_id vipHeapArea = create_area("VIP heap", &address,
		B_ANY_KERNEL_ADDRESS, VIP_HEAP_SIZE, B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (vipHeapArea < 0) {
		panic("heap_init_post_area(): couldn't allocate VIP heap area");
		return B_ERROR;
	}

	sVIPHeap = heap_create_allocator("VIP heap", (addr_t)address,
		VIP_HEAP_SIZE, &heapClass, false);
	if (sVIPHeap == NULL) {
		panic("heap_init_post_area(): failed to create VIP heap\n");
		return B_ERROR;
	}

	dprintf("heap_init_post_area(): created VIP heap: %p\n", sVIPHeap);

	return B_OK;
}


/**
 * @brief Semaphore-subsystem init phase: creates the semaphores used to
 *        wake and synchronise the grow thread.
 *
 * @return B_OK on success; B_ERROR on @c create_sem failure.
 */
static status_t
debug_heap_init_post_sem()
{
	sHeapGrowSem = create_sem(0, "heap_grow_sem");
	if (sHeapGrowSem < 0) {
		panic("heap_init_post_sem(): failed to create heap grow sem\n");
		return B_ERROR;
	}

	sHeapGrownNotify = create_sem(0, "heap_grown_notify");
	if (sHeapGrownNotify < 0) {
		panic("heap_init_post_sem(): failed to create heap grown notify sem\n");
		return B_ERROR;
	}

	return B_OK;
}


/**
 * @brief Thread-subsystem init phase: spawns the grow thread and the
 *        per-CPU heap duplicates.
 *
 * When there is enough physical memory, allocates additional heap
 * instances (one set of HEAP_CLASS_COUNT allocators per extra CPU) so
 * allocation load can be spread across CPUs. Also registers per-heap
 * variants of the "heap" and "heap_allocations" KDL commands.
 *
 * @return B_OK on success; propagated thread-creation error otherwise.
 */
static status_t
debug_heap_init_post_thread()
{
	sHeapGrowThread = spawn_kernel_thread(heap_grow_thread, "heap grower",
		B_URGENT_PRIORITY, NULL);
	if (sHeapGrowThread < 0) {
		panic("heap_init_post_thread(): cannot create heap grow thread\n");
		return sHeapGrowThread;
	}

	// create per-cpu heaps if there's enough memory
	int32 heapCount = MIN(smp_get_num_cpus(),
		(int32)vm_page_num_pages() / 60 / 1024);
	for (int32 i = 1; i < heapCount; i++) {
		addr_t base = 0;
		size_t size = kernel_debug_heap.grow_size * HEAP_CLASS_COUNT;
		area_id perCPUHeapArea = create_area("per cpu initial heap",
			(void **)&base, B_ANY_KERNEL_ADDRESS, size, B_FULL_LOCK,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (perCPUHeapArea < 0)
			break;

		for (uint32 j = 0; j < HEAP_CLASS_COUNT; j++) {
			int32 heapIndex = i * HEAP_CLASS_COUNT + j;
			size_t partSize = size * sHeapClasses[j].initial_percentage / 100;
			sHeaps[heapIndex] = heap_create_allocator(sHeapClasses[j].name,
				base, partSize, &sHeapClasses[j], false);
			sLastGrowRequest[heapIndex] = 0;
			sLastHandledGrowRequest[heapIndex] = 0;
			base += partSize;
			sHeapCount++;
		}
	}

	resume_thread(sHeapGrowThread);

	// set up some debug commands
	add_debugger_command_etc("heap", &dump_heap_list,
		"Dump infos about a specific heap",
		"[\"stats\"] <heap>\n"
		"Dump infos about the specified kernel heap. If \"stats\" is given\n"
		"as the argument, currently only the heap count is printed.\n", 0);
#if !KERNEL_HEAP_LEAK_CHECK
	add_debugger_command_etc("heap_allocations", &dump_allocations,
		"Dump current heap allocations",
		"[\"stats\"] <heap>\n"
		"If the optional argument \"stats\" is specified, only the allocation\n"
		"counts and no individual allocations are printed.\n", 0);
#endif	// KERNEL_HEAP_LEAK_CHECK

	return B_OK;
}


//	#pragma mark - Public API



/**
 * @brief Top-level aligned allocation entry point of the debug heap.
 *
 * Replaces the normal kernel heap's memalign under DEBUG_HEAPS. Panics
 * when called with interrupts disabled outside the kernel startup window.
 * Huge allocations (> HEAP_AREA_USE_THRESHOLD) bypass the heaps entirely
 * and get their own @c area_id framed by an @c area_allocation_info
 * header so @c debug_heap_free can recognise them. Regular allocations
 * round-robin through the per-CPU heaps, and when none can satisfy, an
 * urgent grow request is sent and the caller waits on
 * @c sHeapGrownNotify before retrying.
 *
 * @param alignment Required alignment (power of two or zero).
 * @param size      Requested size in bytes.
 * @return Pointer to allocated memory; never returns NULL (panics if all
 *         heaps are exhausted even after growth).
 */
static void *
debug_heap_memalign(size_t alignment, size_t size)
{
	if (!gKernelStartup && !are_interrupts_enabled()) {
		panic("memalign(): called with interrupts disabled\n");
		return NULL;
	}

	if (!gKernelStartup && size > HEAP_AREA_USE_THRESHOLD) {
		// don't even attempt such a huge allocation - use areas instead
		size_t areaSize = ROUNDUP(size + sizeof(area_allocation_info)
			+ alignment, B_PAGE_SIZE);
		if (areaSize < size) {
			// the size overflowed
			return NULL;
		}

		void *address = NULL;
		area_id allocationArea = create_area("memalign area", &address,
			B_ANY_KERNEL_BLOCK_ADDRESS, areaSize, B_FULL_LOCK,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (allocationArea < B_OK) {
			dprintf("heap: failed to create area for huge allocation\n");
			return NULL;
		}

		area_allocation_info *info = (area_allocation_info *)address;
		info->magic = kAreaAllocationMagic;
		info->area = allocationArea;
		info->base = address;
		info->size = areaSize;
		info->allocation_size = size;
		info->allocation_alignment = alignment;

		address = (void *)((addr_t)address + sizeof(area_allocation_info));
		if (alignment != 0) {
			address = (void *)ROUNDUP((addr_t)address, alignment);
			ASSERT((addr_t)address % alignment == 0);
			ASSERT((addr_t)address + size - 1 < (addr_t)info + areaSize - 1);
		}

		TRACE(("heap: allocated area %ld for huge allocation of %lu bytes\n",
			allocationArea, size));

		info->allocation_base = address;

#if PARANOID_KERNEL_MALLOC
		memset(address, 0xcc, size);
#endif
		return address;
	}

	void *result = NULL;
	bool shouldGrow = false;
	int32 cpuCount = MIN(smp_get_num_cpus(),
		(int32)sHeapCount / HEAP_CLASS_COUNT);
	int32 cpuNumber = smp_get_current_cpu();
	for (int32 i = 0; i < cpuCount; i++) {
		uint32 heapIndex = heap_index_for(size, cpuNumber++ % cpuCount);
		heap_allocator *heap = sHeaps[heapIndex];
		result = heap_memalign(heap, alignment, size);
		if (result != NULL) {
			shouldGrow = heap_should_grow(heap);
			break;
		}

#if PARANOID_HEAP_VALIDATION
		heap_validate_heap(heap);
#endif
	}

	if (result == NULL) {
		// request an urgent grow and wait - we don't do it ourselfs here to
		// serialize growing through the grow thread, as otherwise multiple
		// threads hitting this situation (likely when memory ran out) would
		// all add areas
		uint32 heapIndex = heap_index_for(size, smp_get_current_cpu());
		sLastGrowRequest[heapIndex]++;
		switch_sem(sHeapGrowSem, sHeapGrownNotify);

		// and then try again
		result = heap_memalign(sHeaps[heapIndex], alignment, size);
	} else if (shouldGrow) {
		// should grow sometime soon, notify the grower
		release_sem_etc(sHeapGrowSem, 1, B_DO_NOT_RESCHEDULE);
	}

	if (result == NULL)
		panic("heap: kernel heap has run out of memory\n");
	return result;
}


/**
 * @brief Flag-aware allocation dispatcher wired into
 *        @c kernel_debug_heap::memalign_etc.
 *
 * Routes @c HEAP_PRIORITY_VIP requests to the VIP heap, non-blocking
 * requests (@c HEAP_DONT_WAIT_FOR_MEMORY / @c HEAP_DONT_LOCK_KERNEL_SPACE)
 * to @c memalign_nogrow, and everything else to the normal path.
 *
 * @param alignment Required alignment.
 * @param size      Requested size in bytes.
 * @param flags     Allocation flag bitmask.
 * @return Pointer to the allocated memory (may be NULL for non-blocking
 *         paths).
 */
static void *
debug_heap_memalign_etc(size_t alignment, size_t size, uint32 flags)
{
	if ((flags & HEAP_PRIORITY_VIP) != 0)
		return heap_memalign(sVIPHeap, alignment, size);

	if ((flags & (HEAP_DONT_WAIT_FOR_MEMORY | HEAP_DONT_LOCK_KERNEL_SPACE))
			!= 0) {
		return memalign_nogrow(alignment, size);
	}

	return debug_heap_memalign(alignment, size);
}


/**
 * @brief Top-level free entry point of the debug heap.
 *
 * Replaces the normal kernel heap's free under DEBUG_HEAPS. Panics if
 * called with interrupts disabled outside kernel startup. Tries each
 * public per-CPU heap (starting from the current CPU's offset), then the
 * dedicated grow heap and the VIP heap, and finally handles huge-area
 * allocations identified by their @c kAreaAllocationMagic sentinel. If
 * no owner can be found the caller passed a bogus pointer and the
 * function panics.
 *
 * @param address Pointer returned by a previous allocation.
 */
static void
debug_heap_free(void *address)
{
	if (!gKernelStartup && !are_interrupts_enabled()) {
		panic("free(): called with interrupts disabled\n");
		return;
	}

	int32 offset = smp_get_current_cpu() * HEAP_CLASS_COUNT;
	for (uint32 i = 0; i < sHeapCount; i++) {
		heap_allocator *heap = sHeaps[(i + offset) % sHeapCount];
		if (heap_free(heap, address) == B_OK) {
#if PARANOID_HEAP_VALIDATION
			heap_validate_heap(heap);
#endif
			return;
		}
	}

	// maybe it was allocated from the dedicated grow heap
	if (heap_free(sGrowHeap, address) == B_OK)
		return;

	// or maybe it was allocated from the VIP heap
	if (heap_free(sVIPHeap, address) == B_OK)
		return;

	// or maybe it was a huge allocation using an area
	area_info areaInfo;
	area_id area = area_for(address);
	if (area >= B_OK && get_area_info(area, &areaInfo) == B_OK) {
		area_allocation_info *info = (area_allocation_info *)areaInfo.address;

		// just make extra sure it was allocated by us
		if (info->magic == kAreaAllocationMagic && info->area == area
			&& info->size == areaInfo.size && info->base == areaInfo.address
			&& info->allocation_size < areaInfo.size) {
			delete_area(area);
			TRACE(("free(): freed huge allocation by deleting area %ld\n",
				area));
			return;
		}
	}

	panic("free(): free failed for address %p\n", address);
}


/**
 * @brief Flag-aware free dispatcher wired into
 *        @c kernel_debug_heap::free_etc.
 *
 * Routes frees tagged @c HEAP_PRIORITY_VIP directly to the VIP heap, and
 * otherwise delegates to @c debug_heap_free.
 *
 * @param address Pointer to free.
 * @param flags   Allocation-flag bitmask used at the matching allocation.
 */
static void
debug_heap_free_etc(void *address, uint32 flags)
{
	if ((flags & HEAP_PRIORITY_VIP) != 0)
		heap_free(sVIPHeap, address);
	else
		debug_heap_free(address);
}


/**
 * @brief Top-level realloc entry point of the debug heap.
 *
 * Replaces the normal kernel heap's realloc under DEBUG_HEAPS. Panics if
 * called with interrupts disabled outside kernel startup. Handles the
 * alloc/free degenerate cases, then asks each public per-CPU heap and
 * the grow heap in turn. Huge-area allocations are resized in place when
 * possible, otherwise a new block is allocated, the payload copied, and
 * the old area destroyed.
 *
 * @param address Existing allocation, or NULL to allocate fresh.
 * @param newSize New requested size; 0 frees.
 * @param flags   Allocation-flag bitmask.
 * @return Pointer to the (possibly moved) allocation, or NULL on failure
 *         / after a free.
 */
static void *
debug_heap_realloc(void *address, size_t newSize, uint32 flags)
{
	if (!gKernelStartup && !are_interrupts_enabled()) {
		panic("realloc(): called with interrupts disabled\n");
		return NULL;
	}

	if (address == NULL)
		return debug_heap_memalign_etc(0, newSize, flags);

	if (newSize == 0) {
		debug_heap_free_etc(address, flags);
		return NULL;
	}

	void *newAddress = NULL;
	int32 offset = smp_get_current_cpu() * HEAP_CLASS_COUNT;
	for (uint32 i = 0; i < sHeapCount; i++) {
		heap_allocator *heap = sHeaps[(i + offset) % sHeapCount];
		if (heap_realloc(heap, address, &newAddress, newSize, flags) == B_OK) {
#if PARANOID_HEAP_VALIDATION
			heap_validate_heap(heap);
#endif
			return newAddress;
		}
	}

	// maybe it was allocated from the dedicated grow heap
	if (heap_realloc(sGrowHeap, address, &newAddress, newSize, flags) == B_OK)
		return newAddress;

	// or maybe it was a huge allocation using an area
	area_info areaInfo;
	area_id area = area_for(address);
	if (area >= B_OK && get_area_info(area, &areaInfo) == B_OK) {
		area_allocation_info *info = (area_allocation_info *)areaInfo.address;

		// just make extra sure it was allocated by us
		if (info->magic == kAreaAllocationMagic && info->area == area
			&& info->size == areaInfo.size && info->base == areaInfo.address
			&& info->allocation_size < areaInfo.size) {
			size_t available = info->size - ((addr_t)info->allocation_base
				- (addr_t)info->base);

			if (available >= newSize) {
				// there is enough room available for the newSize
				TRACE(("realloc(): new size %ld fits in old area %ld with %ld "
					"available\n", newSize, area, available));
				info->allocation_size = newSize;
				return address;
			}

			// have to allocate/copy/free - TODO maybe resize the area instead?
			newAddress = malloc(newSize);
			if (newAddress == NULL) {
				dprintf("realloc(): failed to allocate new block of %ld bytes\n",
					newSize);
				return NULL;
			}

			memcpy(newAddress, address, min_c(newSize, info->allocation_size));
			delete_area(area);
			TRACE(("realloc(): allocated new block %p for size %ld and deleted "
				"old area %ld\n", newAddress, newSize, area));
			return newAddress;
		}
	}

	panic("realloc(): failed to realloc address %p to size %lu\n", address,
		newSize);
	return NULL;
}


/**
 * @brief Vtable wiring this debug heap into the kernel's heap dispatcher.
 *
 * Selected by the boot code when DEBUG_HEAPS is enabled so that every
 * kernel @c malloc / @c free / @c realloc / @c memalign call goes through
 * the allocator defined in this file instead of the normal slab/heap.
 */
kernel_heap_implementation kernel_debug_heap = {
	"debug_heap",
	// allocate 16MB initial heap for the kernel
	16 * 1024 * 1024,
	// grow by another 4MB each time the heap runs out of memory
	4 * 1024 * 1024,

	debug_heap_init,
	debug_heap_init_post_area,
	debug_heap_init_post_sem,
	debug_heap_init_post_thread,

	debug_heap_memalign_etc,
	debug_heap_realloc,
	debug_heap_free_etc,
};


#endif	// DEBUG_HEAPS
