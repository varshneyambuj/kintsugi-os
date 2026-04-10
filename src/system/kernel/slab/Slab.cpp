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
 * @file Slab.cpp
 * @brief Core slab allocator implementation — slab lifecycle and object recycling.
 *
 * Manages the allocation and freeing of slabs (contiguous memory regions
 * holding fixed-size objects). Implements the slow-path alloc/free that is
 * called when the per-CPU depot has no cached objects. Also handles slab
 * creation via MemoryManager and object constructor/destructor callbacks.
 *
 * @see MemoryManager.cpp, ObjectCache.cpp, ObjectDepot.cpp
 */


#include <slab/Slab.h>

#include <algorithm>
#include <new>
#include <stdlib.h>
#include <string.h>

#include <KernelExport.h>

#include <condition_variable.h>
#include <elf.h>
#include <kernel.h>
#include <low_resource_manager.h>
#include <smp.h>
#include <tracing.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>
#include <vm/vm.h>
#include <vm/VMAddressSpace.h>

#include "SmallObjectCache.h"
#include "HashedObjectCache.h"
#include "ObjectDepot.h"
#include "MemoryManager.h"
#include "slab_debug.h"
#include "slab_private.h"

#if DEBUG_HEAPS
#include "../debug/heaps.h"
#endif


#if !USE_DEBUG_HEAPS_FOR_ALL_OBJECT_CACHES


typedef DoublyLinkedList<ObjectCache> ObjectCacheList;

typedef DoublyLinkedList<ObjectCache,
	DoublyLinkedListMemberGetLink<ObjectCache, &ObjectCache::maintenance_link> >
		MaintenanceQueue;

static ObjectCacheList sObjectCaches;
static mutex sObjectCacheListLock = MUTEX_INITIALIZER("object cache list");

static mutex sMaintenanceLock
	= MUTEX_INITIALIZER("object cache resize requests");
static MaintenanceQueue sMaintenanceQueue;
static ConditionVariable sMaintenanceCondition;


#if SLAB_ALLOCATION_TRACKING_AVAILABLE

struct caller_info {
	addr_t		caller;
	size_t		count;
	size_t		size;
};

static const int32 kCallerInfoTableSize = 1024;
static caller_info sCallerInfoTable[kCallerInfoTableSize];
static int32 sCallerInfoCount = 0;

static caller_info* get_caller_info(addr_t caller);


RANGE_MARKER_FUNCTION_PROTOTYPES(slab_allocator)
RANGE_MARKER_FUNCTION_PROTOTYPES(SlabHashedObjectCache)
RANGE_MARKER_FUNCTION_PROTOTYPES(SlabMemoryManager)
RANGE_MARKER_FUNCTION_PROTOTYPES(SlabObjectCache)
RANGE_MARKER_FUNCTION_PROTOTYPES(SlabObjectDepot)
RANGE_MARKER_FUNCTION_PROTOTYPES(Slab)
RANGE_MARKER_FUNCTION_PROTOTYPES(SlabSmallObjectCache)


static const addr_t kSlabCodeAddressRanges[] = {
	RANGE_MARKER_FUNCTION_ADDRESS_RANGE(slab_allocator),
	RANGE_MARKER_FUNCTION_ADDRESS_RANGE(SlabHashedObjectCache),
	RANGE_MARKER_FUNCTION_ADDRESS_RANGE(SlabMemoryManager),
	RANGE_MARKER_FUNCTION_ADDRESS_RANGE(SlabObjectCache),
	RANGE_MARKER_FUNCTION_ADDRESS_RANGE(SlabObjectDepot),
	RANGE_MARKER_FUNCTION_ADDRESS_RANGE(Slab),
	RANGE_MARKER_FUNCTION_ADDRESS_RANGE(SlabSmallObjectCache)
};

static const uint32 kSlabCodeAddressRangeCount
	= B_COUNT_OF(kSlabCodeAddressRanges) / 2;

#endif	// SLAB_ALLOCATION_TRACKING_AVAILABLE


RANGE_MARKER_FUNCTION_BEGIN(Slab)


#if SLAB_OBJECT_CACHE_TRACING


namespace SlabObjectCacheTracing {

class ObjectCacheTraceEntry
	: public TRACE_ENTRY_SELECTOR(SLAB_OBJECT_CACHE_TRACING_STACK_TRACE) {
	public:
		ObjectCacheTraceEntry(ObjectCache* cache)
			:
			TraceEntryBase(SLAB_OBJECT_CACHE_TRACING_STACK_TRACE, 0, true),
			fCache(cache)
		{
		}

	protected:
		ObjectCache*	fCache;
};


class Create : public ObjectCacheTraceEntry {
	public:
		Create(const char* name, size_t objectSize, size_t alignment,
				size_t maxByteUsage, uint32 flags, void* cookie,
				ObjectCache* cache)
			:
			ObjectCacheTraceEntry(cache),
			fObjectSize(objectSize),
			fAlignment(alignment),
			fMaxByteUsage(maxByteUsage),
			fFlags(flags),
			fCookie(cookie)
		{
			fName = alloc_tracing_buffer_strcpy(name, 64, false);
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("object cache create: name: \"%s\", object size: "
				"%" B_PRIuSIZE ", alignment: %" B_PRIuSIZE ", max usage: "
				"%" B_PRIuSIZE ", flags: 0x%" B_PRIx32 ", cookie: %p -> cache: %p",
					fName, fObjectSize, fAlignment, fMaxByteUsage, fFlags,
					fCookie, fCache);
		}

	private:
		const char*	fName;
		size_t		fObjectSize;
		size_t		fAlignment;
		size_t		fMaxByteUsage;
		uint32		fFlags;
		void*		fCookie;
};


class Delete : public ObjectCacheTraceEntry {
	public:
		Delete(ObjectCache* cache)
			:
			ObjectCacheTraceEntry(cache)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("object cache delete: %p", fCache);
		}
};


class Alloc : public ObjectCacheTraceEntry {
	public:
		Alloc(ObjectCache* cache, uint32 flags, void* object)
			:
			ObjectCacheTraceEntry(cache),
			fFlags(flags),
			fObject(object)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("object cache alloc: cache: %p, flags: 0x%" B_PRIx32
				" -> object: %p", fCache, fFlags, fObject);
		}

	private:
		uint32		fFlags;
		void*		fObject;
};


class Free : public ObjectCacheTraceEntry {
	public:
		Free(ObjectCache* cache, void* object)
			:
			ObjectCacheTraceEntry(cache),
			fObject(object)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("object cache free: cache: %p, object: %p", fCache,
				fObject);
		}

	private:
		void*		fObject;
};


class Reserve : public ObjectCacheTraceEntry {
	public:
		Reserve(ObjectCache* cache, size_t count, uint32 flags)
			:
			ObjectCacheTraceEntry(cache),
			fCount(count),
			fFlags(flags)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("object cache reserve: cache: %p, count: %" B_PRIu32 ", "
				"flags: 0x%" B_PRIx32, fCache, fCount, fFlags);
		}

	private:
		uint32		fCount;
		uint32		fFlags;
};


}	// namespace SlabObjectCacheTracing

#	define T(x)	new(std::nothrow) SlabObjectCacheTracing::x

#else
#	define T(x)
#endif	// SLAB_OBJECT_CACHE_TRACING


// #pragma mark -


/**
 * @brief Prints a single slab's metadata to the kernel debugger.
 *
 * @param slab Pointer to the slab to display.
 * @note Intended for use from the kernel debugger only.
 */
static void
dump_slab(::slab* slab)
{
	kprintf("  %p  %p  %6" B_PRIuSIZE " %6" B_PRIuSIZE " %6" B_PRIuSIZE "  %p\n",
		slab, slab->pages, slab->size, slab->count, slab->offset, slab->free.head);
}


/**
 * @brief Kernel debugger command: list all registered object caches.
 *
 * Iterates sObjectCaches and prints a one-line summary for each cache
 * including name, object size, alignment, memory usage, and flags.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return Always returns 0.
 * @note Registered as the "slabs" debugger command. Safe to call only from
 *       the kernel debugger context.
 */
static int
dump_slabs(int argc, char* argv[])
{
	kprintf("%*s %22s %8s %8s %8s %6s %8s %8s %8s\n",
		B_PRINTF_POINTER_WIDTH + 2, "address", "name", "objsize", "align",
		"usage", "empty", "usedobj", "total", "flags");

	ObjectCacheList::Iterator it = sObjectCaches.GetIterator();

	while (it.HasNext()) {
		ObjectCache* cache = it.Next();

		kprintf("%p %22s %8lu %8" B_PRIuSIZE " %8lu %6lu %8lu %8lu %8" B_PRIx32
			"\n", cache, cache->name, cache->object_size, cache->alignment,
			cache->usage, cache->empty_count, cache->used_count,
			cache->total_objects, cache->flags);
	}

	return 0;
}


/**
 * @brief Kernel debugger command: dump detailed information about one object
 *        cache.
 *
 * Parses the cache address from argv[1] and prints all cache fields plus
 * per-slab listings for the empty, partial, and full slab lists, followed by
 * a dump of the associated object depot (if the cache uses one).
 *
 * @param argc Argument count; must be >= 2.
 * @param argv Argument vector; argv[1] must be a valid object cache address.
 * @return Always returns 0.
 * @note Registered as the "slab_cache" debugger command. Safe to call only
 *       from the kernel debugger context.
 */
static int
dump_cache_info(int argc, char* argv[])
{
	if (argc < 2) {
		kprintf("usage: slab_cache [address]\n");
		return 0;
	}

	ObjectCache* cache = (ObjectCache*)parse_expression(argv[1]);

	kprintf("name:              %s\n", cache->name);
	kprintf("lock:              %p\n", &cache->lock);
	kprintf("object_size:       %lu\n", cache->object_size);
	kprintf("alignment:         %" B_PRIuSIZE "\n", cache->alignment);
	kprintf("cache_color_cycle: %lu\n", cache->cache_color_cycle);
	kprintf("total_objects:     %lu\n", cache->total_objects);
	kprintf("used_count:        %lu\n", cache->used_count);
	kprintf("empty_count:       %lu\n", cache->empty_count);
	kprintf("pressure:          %lu\n", cache->pressure);
	kprintf("slab_size:         %lu\n", cache->slab_size);
	kprintf("usage:             %lu\n", cache->usage);
	kprintf("maximum:           %lu\n", cache->maximum);
	kprintf("flags:             0x%" B_PRIx32 "\n", cache->flags);
	kprintf("cookie:            %p\n", cache->cookie);
	kprintf("resize entry don't wait: %p\n", cache->resize_entry_dont_wait);
	kprintf("resize entry can wait:   %p\n", cache->resize_entry_can_wait);

	kprintf("  %-*s    %-*s      size   used offset  free\n",
		B_PRINTF_POINTER_WIDTH, "slab", B_PRINTF_POINTER_WIDTH, "chunk");

	SlabList::Iterator iterator = cache->empty.GetIterator();
	if (iterator.HasNext())
		kprintf("empty:\n");
	while (::slab* slab = iterator.Next())
		dump_slab(slab);

	iterator = cache->partial.GetIterator();
	if (iterator.HasNext())
		kprintf("partial:\n");
	while (::slab* slab = iterator.Next())
		dump_slab(slab);

	iterator = cache->full.GetIterator();
	if (iterator.HasNext())
		kprintf("full:\n");
	while (::slab* slab = iterator.Next())
		dump_slab(slab);

	if ((cache->flags & CACHE_NO_DEPOT) == 0) {
		kprintf("depot:\n");
		dump_object_depot(&cache->depot);
	}

	return 0;
}


/**
 * @brief Kernel debugger command: identify and display the object cache that
 *        owns a given allocation address.
 *
 * Resolves the object cache for the address given in argv[1] via
 * MemoryManager::DebugObjectCacheForAddress(), then identifies whether the
 * object's slab is on the empty, partial, or full list.
 *
 * @param argc Argument count; must be >= 2.
 * @param argv Argument vector; argv[1] must be any address within an object
 *             cache allocation.
 * @return 0 on success, 1 if the address could not be resolved to a cache.
 * @note Registered as the "slab_object" debugger command. Safe to call only
 *       from the kernel debugger context.
 */
static int
dump_object_info(int argc, char* argv[])
{
	if (argc < 2) {
		kprintf("usage: slab_object [address]\n");
		return 0;
	}

	void* object = (void*)parse_expression(argv[1]);
	ObjectCache* cache = MemoryManager::DebugObjectCacheForAddress(object);
	if (cache == NULL) {
		kprintf("%p does not seem to be in an object_cache\n", object);
		return 1;
	}

	kprintf("address %p\n", object);
	kprintf("\tslab_cache: %p (%s)\n", cache, cache->name);

	MutexTryLocker cacheLocker(cache->lock);
	if (cacheLocker.IsLocked()) {
		slab* slab = cache->ObjectSlab(object);
		const char* slabType = cache->empty.Contains(slab) ? "empty"
			: cache->partial.Contains(slab) ? "partial"
			: cache->full.Contains(slab) ? "full" : NULL;

		kprintf("\tobject is in %s slab: %p\n", slabType, slab);
	}

	return 0;
}


// #pragma mark - AllocationTrackingCallback


#if SLAB_ALLOCATION_TRACKING_AVAILABLE

AllocationTrackingCallback::~AllocationTrackingCallback()
{
}

#endif	// SLAB_ALLOCATION_TRACKING_AVAILABLE


// #pragma mark -


#if SLAB_ALLOCATION_TRACKING_AVAILABLE

namespace {

class AllocationCollectorCallback : public AllocationTrackingCallback {
public:
	AllocationCollectorCallback(bool resetInfos)
		:
		fResetInfos(resetInfos)
	{
	}

	virtual bool ProcessTrackingInfo(AllocationTrackingInfo* info,
		void* allocation, size_t allocationSize)
	{
		if (!info->IsInitialized())
			return true;

		addr_t caller = 0;
		AbstractTraceEntryWithStackTrace* traceEntry = info->TraceEntry();

		if (traceEntry != NULL && info->IsTraceEntryValid()) {
			caller = tracing_find_caller_in_stack_trace(
				traceEntry->StackTrace(), kSlabCodeAddressRanges,
				kSlabCodeAddressRangeCount);
		}

		caller_info* callerInfo = get_caller_info(caller);
		if (callerInfo == NULL) {
			kprintf("out of space for caller infos\n");
			return false;
		}

		callerInfo->count++;
		callerInfo->size += allocationSize;

		if (fResetInfos)
			info->Clear();

		return true;
	}

private:
	bool	fResetInfos;
};


class AllocationInfoPrinterCallback : public AllocationTrackingCallback {
public:
	AllocationInfoPrinterCallback(bool printStackTrace, addr_t addressFilter,
		team_id teamFilter, thread_id threadFilter)
		:
		fPrintStackTrace(printStackTrace),
		fAddressFilter(addressFilter),
		fTeamFilter(teamFilter),
		fThreadFilter(threadFilter)
	{
	}

	virtual bool ProcessTrackingInfo(AllocationTrackingInfo* info,
		void* allocation, size_t allocationSize)
	{
		if (!info->IsInitialized())
			return true;

		if (fAddressFilter != 0 && (addr_t)allocation != fAddressFilter)
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

		kprintf("allocation %p, size: %" B_PRIuSIZE, allocation,
			allocationSize);

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
	addr_t		fAddressFilter;
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
		void* allocation, size_t allocationSize)
	{
		if (!info->IsInitialized())
			return true;

		addr_t caller = 0;
		AbstractTraceEntryWithStackTrace* traceEntry = info->TraceEntry();
		if (traceEntry != NULL && !info->IsTraceEntryValid())
			traceEntry = NULL;

		if (traceEntry != NULL) {
			caller = tracing_find_caller_in_stack_trace(
				traceEntry->StackTrace(), kSlabCodeAddressRanges,
				kSlabCodeAddressRangeCount);
		}

		if (caller != fCaller)
			return true;

		kprintf("allocation %p, size: %" B_PRIuSIZE "\n", allocation,
			allocationSize);
		if (traceEntry != NULL)
			tracing_print_stack_trace(traceEntry->StackTrace());

		return true;
	}

private:
	addr_t	fCaller;
};

}	// unnamed namespace

/**
 * @brief Looks up or creates a caller_info entry for the given caller address.
 *
 * Searches sCallerInfoTable for an existing entry matching @p caller. If none
 * is found and a free slot is available, a new entry is initialised and
 * returned.
 *
 * @param caller The instruction address of the allocating caller.
 * @return Pointer to the matching caller_info entry, or NULL if the table is
 *         full.
 * @note Only available when SLAB_ALLOCATION_TRACKING_AVAILABLE is enabled.
 *       Not thread-safe; assumes single-threaded access from the debugger
 *       command context.
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
 * @brief qsort comparator that orders caller_info entries by descending
 *        allocation size.
 *
 * @param _a Pointer to the first caller_info.
 * @param _b Pointer to the second caller_info.
 * @return Positive if a's size is smaller than b's (sort descending).
 */
static int
caller_info_compare_size(const void* _a, const void* _b)
{
	const caller_info* a = (const caller_info*)_a;
	const caller_info* b = (const caller_info*)_b;
	return (int)(b->size - a->size);
}


/**
 * @brief qsort comparator that orders caller_info entries by descending
 *        allocation count.
 *
 * @param _a Pointer to the first caller_info.
 * @param _b Pointer to the second caller_info.
 * @return Positive if a's count is smaller than b's (sort descending).
 */
static int
caller_info_compare_count(const void* _a, const void* _b)
{
	const caller_info* a = (const caller_info*)_a;
	const caller_info* b = (const caller_info*)_b;
	return (int)(b->count - a->count);
}


#if SLAB_OBJECT_CACHE_ALLOCATION_TRACKING

/**
 * @brief Iterates every object slot in a slab and invokes the tracking
 *        callback for each one.
 *
 * @param cache    Pointer to the owning ObjectCache.
 * @param slab     Pointer to the slab whose objects are to be analysed.
 * @param callback Callback object whose ProcessTrackingInfo() is called for
 *                 each object.
 * @retval true  All objects were processed successfully.
 * @retval false ProcessTrackingInfo() returned false, requesting early exit.
 * @note Only compiled when SLAB_OBJECT_CACHE_ALLOCATION_TRACKING is enabled.
 */
static bool
analyze_allocation_callers(ObjectCache* cache, slab* slab,
	AllocationTrackingCallback& callback)
{
	for (uint32 i = 0; i < slab->size; i++) {
		if (!callback.ProcessTrackingInfo(&slab->tracking[i],
				cache->ObjectAtIndex(slab, i), cache->object_size)) {
			return false;
		}
	}

	return true;
}


/**
 * @brief Iterates every slab in a slab list and analyses allocation callers.
 *
 * @param cache     Pointer to the owning ObjectCache.
 * @param slabList  Reference to the slab list to iterate.
 * @param callback  Callback object invoked for each object in each slab.
 * @retval true  All slabs and objects were processed successfully.
 * @retval false Early exit was requested by the callback.
 * @note Only compiled when SLAB_OBJECT_CACHE_ALLOCATION_TRACKING is enabled.
 */
static bool
analyze_allocation_callers(ObjectCache* cache, const SlabList& slabList,
	AllocationTrackingCallback& callback)
{
	for (SlabList::ConstIterator it = slabList.GetIterator();
			slab* slab = it.Next();) {
		if (!analyze_allocation_callers(cache, slab, callback))
			return false;
	}

	return true;
}


/**
 * @brief Analyses allocation callers for all live objects in an ObjectCache.
 *
 * Iterates the cache's full and partial slab lists (which hold live objects)
 * and invokes the callback for each object.
 *
 * @param cache    Pointer to the ObjectCache to analyse.
 * @param callback Callback object invoked for each live object.
 * @retval true  All objects were processed.
 * @retval false Early exit was requested by the callback.
 * @note Only compiled when SLAB_OBJECT_CACHE_ALLOCATION_TRACKING is enabled.
 */
static bool
analyze_allocation_callers(ObjectCache* cache,
	AllocationTrackingCallback& callback)
{
	return analyze_allocation_callers(cache, cache->full, callback)
		&& analyze_allocation_callers(cache, cache->partial, callback);
}

#endif	// SLAB_OBJECT_CACHE_ALLOCATION_TRACKING


/**
 * @brief Kernel debugger command: print all live slab allocations, optionally
 *        filtered by cache, slab, address, team, or thread.
 *
 * Accepts multiple optional flags to narrow the output:
 *   --stacktrace  Print stack traces where available.
 *   -a <addr>     Show only the allocation at exactly <addr>.
 *   -o <cache>    Restrict to the given object cache address.
 *   -s <slab>     Restrict to the given slab address (or containing slab).
 *   --team <id>   Filter by team ID.
 *   --thread <id> Filter by thread ID.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Always returns 0.
 * @note Only meaningful when SLAB_ALLOCATION_TRACKING_AVAILABLE is enabled.
 *       Safe to call only from the kernel debugger context.
 */
static int
dump_allocation_infos(int argc, char **argv)
{
	ObjectCache* cache = NULL;
	slab* slab = NULL;
	addr_t addressFilter = 0;
	team_id teamFilter = -1;
	thread_id threadFilter = -1;
	bool printStackTraces = false;

	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--stacktrace") == 0)
			printStackTraces = true;
		else if (strcmp(argv[i], "-a") == 0) {
			uint64 address;
			if (++i >= argc
				|| !evaluate_debug_expression(argv[i], &address, true)) {
				print_debugger_command_usage(argv[0]);
				return 0;
			}

			addressFilter = address;
		} else if (strcmp(argv[i], "-o") == 0) {
			uint64 cacheAddress;
			if (++i >= argc
				|| !evaluate_debug_expression(argv[i], &cacheAddress, true)) {
				print_debugger_command_usage(argv[0]);
				return 0;
			}

			cache = (ObjectCache*)(addr_t)cacheAddress;
		} else if (strcasecmp(argv[i], "-s") == 0) {
			uint64 slabAddress;
			if (++i >= argc
				|| !evaluate_debug_expression(argv[i], &slabAddress, true)) {
				print_debugger_command_usage(argv[0]);
				return 0;
			}

			void* slabPages = (void*)slabAddress;
			if (strcmp(argv[i], "-s") == 0) {
				slab = (struct slab*)(addr_t)slabAddress;
				slabPages = slab->pages;
			}

			cache = MemoryManager::DebugObjectCacheForAddress(slabPages);
			if (cache == NULL) {
				kprintf("Couldn't find object cache for address %p.\n",
					slabPages);
				return 0;
			}

			if (slab == NULL) {
				slab = cache->ObjectSlab(slabPages);

				if (slab == NULL) {
					kprintf("Couldn't find slab for address %p.\n", slabPages);
					return 0;
				}
			}
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

	AllocationInfoPrinterCallback callback(printStackTraces, addressFilter,
		teamFilter, threadFilter);

	if (slab != NULL || cache != NULL) {
#if SLAB_OBJECT_CACHE_ALLOCATION_TRACKING
		if (slab != NULL) {
			if (!analyze_allocation_callers(cache, slab, callback))
				return 0;
		} else if (cache != NULL) {
			if (!analyze_allocation_callers(cache, callback))
				return 0;
		}
#else
		kprintf("Object cache allocation tracking not available. "
			"SLAB_OBJECT_CACHE_TRACING (%d) and "
			"SLAB_OBJECT_CACHE_TRACING_STACK_TRACE (%d) must be enabled.\n",
			SLAB_OBJECT_CACHE_TRACING, SLAB_OBJECT_CACHE_TRACING_STACK_TRACE);
		return 0;
#endif
	} else {
#if SLAB_OBJECT_CACHE_ALLOCATION_TRACKING

		for (ObjectCacheList::Iterator it = sObjectCaches.GetIterator();
				it.HasNext();) {
			if (!analyze_allocation_callers(it.Next(), callback))
				return 0;
		}
#endif

#if SLAB_MEMORY_MANAGER_ALLOCATION_TRACKING
		if (!MemoryManager::AnalyzeAllocationCallers(callback))
			return 0;
#endif
	}

	return 0;
}


/**
 * @brief Kernel debugger command: summarise live slab allocations grouped by
 *        call site.
 *
 * Collects allocation tracking info from all caches (or a single specified
 * cache), aggregates counts and byte totals per unique caller address, sorts
 * the result by size (default) or count ("-c"), and prints a ranked table.
 * With "-d <caller>" prints individual allocations for that call site instead.
 * "-r" resets tracking info after collection.
 *
 * @param argc Argument count.
 * @param argv Argument vector; see function body for supported flags.
 * @return Always returns 0.
 * @note Only meaningful when SLAB_ALLOCATION_TRACKING_AVAILABLE is enabled.
 *       Safe to call only from the kernel debugger context.
 */
static int
dump_allocations_per_caller(int argc, char **argv)
{
	bool sortBySize = true;
	bool resetAllocationInfos = false;
	bool printDetails = false;
	ObjectCache* cache = NULL;
	addr_t caller = 0;

	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-c") == 0) {
			sortBySize = false;
		} else if (strcmp(argv[i], "-d") == 0) {
			uint64 callerAddress;
			if (++i >= argc
				|| !evaluate_debug_expression(argv[i], &callerAddress, true)) {
				print_debugger_command_usage(argv[0]);
				return 0;
			}

			caller = callerAddress;
			printDetails = true;
		} else if (strcmp(argv[i], "-o") == 0) {
			uint64 cacheAddress;
			if (++i >= argc
				|| !evaluate_debug_expression(argv[i], &cacheAddress, true)) {
				print_debugger_command_usage(argv[0]);
				return 0;
			}

			cache = (ObjectCache*)(addr_t)cacheAddress;
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

	if (cache != NULL) {
#if SLAB_OBJECT_CACHE_ALLOCATION_TRACKING
		if (!analyze_allocation_callers(cache, callback))
			return 0;
#else
		kprintf("Object cache allocation tracking not available. "
			"SLAB_OBJECT_CACHE_TRACING (%d) and "
			"SLAB_OBJECT_CACHE_TRACING_STACK_TRACE (%d) must be enabled.\n",
			SLAB_OBJECT_CACHE_TRACING, SLAB_OBJECT_CACHE_TRACING_STACK_TRACE);
		return 0;
#endif
	} else {
#if SLAB_OBJECT_CACHE_ALLOCATION_TRACKING

		for (ObjectCacheList::Iterator it = sObjectCaches.GetIterator();
				it.HasNext();) {
			if (!analyze_allocation_callers(it.Next(), callback))
				return 0;
		}
#endif

#if SLAB_MEMORY_MANAGER_ALLOCATION_TRACKING
		if (!MemoryManager::AnalyzeAllocationCallers(callback))
			return 0;
#endif
	}

	if (printDetails)
		return 0;

	// sort the array
	qsort(sCallerInfoTable, sCallerInfoCount, sizeof(caller_info),
		sortBySize ? &caller_info_compare_size : &caller_info_compare_count);

	kprintf("%" B_PRId32 " different callers, sorted by %s...\n\n",
		sCallerInfoCount, sortBySize ? "size" : "count");

	size_t totalAllocationSize = 0;
	size_t totalAllocationCount = 0;

	kprintf("     count        size      caller\n");
	kprintf("----------------------------------\n");
	for (int32 i = 0; i < sCallerInfoCount; i++) {
		caller_info& info = sCallerInfoTable[i];
		kprintf("%10" B_PRIuSIZE "  %10" B_PRIuSIZE "  %p", info.count,
			info.size, (void*)info.caller);

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
		totalAllocationSize += info.size;
	}

	kprintf("\ntotal allocations: %" B_PRIuSIZE ", %" B_PRIuSIZE " bytes\n",
		totalAllocationCount, totalAllocationSize);

	return 0;
}

#endif	// SLAB_ALLOCATION_TRACKING_AVAILABLE


/**
 * @brief Records a tracing entry for a successful object allocation.
 *
 * When SLAB_OBJECT_CACHE_TRACING is enabled, emits a tracing event for the
 * allocation. When SLAB_OBJECT_CACHE_ALLOCATION_TRACKING is also enabled,
 * associates the tracing entry with the object's tracking slot under the
 * cache lock.
 *
 * @param cache  Pointer to the ObjectCache from which the object was allocated.
 * @param flags  Allocation flags used for the allocation.
 * @param object Pointer to the newly allocated object.
 * @note No-op unless SLAB_OBJECT_CACHE_TRACING is enabled at compile time.
 */
void
add_alloc_tracing_entry(ObjectCache* cache, uint32 flags, void* object)
{
#if SLAB_OBJECT_CACHE_TRACING
#if SLAB_OBJECT_CACHE_ALLOCATION_TRACKING
	MutexLocker _(cache->lock);
	cache->TrackingInfoFor(object)->Init(T(Alloc(cache, flags, object)));
#else
	T(Alloc(cache, flags, object));
#endif
#endif
}


// #pragma mark -


/**
 * @brief Wakes the MemoryManager maintenance thread to perform deferred work.
 *
 * Acquires sMaintenanceLock and broadcasts on sMaintenanceCondition so that
 * the object_cache_maintainer thread will wake and call
 * MemoryManager::PerformMaintenance() on its next iteration.
 *
 * @note Safe to call from any kernel context, including interrupt handlers
 *       (sMaintenanceLock is a plain mutex, not a spinlock).
 */
void
request_memory_manager_maintenance()
{
	MutexLocker locker(sMaintenanceLock);
	sMaintenanceCondition.NotifyAll();
}


// #pragma mark -


/**
 * @brief Destroys an object cache and releases all of its slabs.
 *
 * Drains the depot, panics if any full or partial slabs remain (indicating a
 * leak), returns all empty slabs via ReturnSlab(), and calls the cache's
 * virtual Delete() method.
 *
 * @param cache Pointer to the object_cache to destroy.
 * @note Must be called without cache->lock held. Caller must ensure no
 *       concurrent allocations or frees are in progress.
 */
static void
delete_object_cache_internal(object_cache* cache)
{
	if (!(cache->flags & CACHE_NO_DEPOT))
		object_depot_destroy(&cache->depot, 0);

	mutex_lock(&cache->lock);

	if (!cache->full.IsEmpty())
		panic("cache destroy: still has full slabs");

	if (!cache->partial.IsEmpty())
		panic("cache destroy: still has partial slabs");

	while (!cache->empty.IsEmpty())
		cache->ReturnSlab(cache->empty.RemoveHead(), 0);

	mutex_destroy(&cache->lock);
	cache->Delete();
}


/**
 * @brief Schedules the maintenance thread to grow the object cache to its
 *        minimum reserve.
 *
 * Sets cache->maintenance_resize and, if the cache is not already queued for
 * maintenance, adds it to sMaintenanceQueue and wakes the maintainer.
 *
 * @param cache Pointer to the ObjectCache whose reserve needs increasing.
 * @note Acquires sMaintenanceLock internally. Safe to call with cache->lock held.
 */
static void
increase_object_reserve(ObjectCache* cache)
{
	MutexLocker locker(sMaintenanceLock);

	cache->maintenance_resize = true;

	if (!cache->maintenance_pending) {
		cache->maintenance_pending = true;
		sMaintenanceQueue.Add(cache);
		sMaintenanceCondition.NotifyAll();
	}
}


/*!	Makes sure that \a objectCount objects can be allocated.
*/
/**
 * @brief Ensures that at least @p objectCount free objects are available in
 *        the cache, allocating new slabs as needed.
 *
 * If another thread is currently resizing the cache, this function waits on
 * that thread's resize condition (unless CACHE_DONT_WAIT_FOR_MEMORY is set).
 * After waiting or if the cache already has enough free objects, returns
 * immediately. Otherwise allocates slabs via ObjectCache::CreateSlab() until
 * the required number of free objects is available.
 *
 * @param cache       Pointer to the ObjectCache to resize.
 * @param objectCount Minimum number of free objects required.
 * @param flags       Allocation flags; CACHE_DONT_WAIT_FOR_MEMORY skips the
 *                    wait for concurrent resizers.
 * @retval B_OK        The cache now has at least @p objectCount free objects.
 * @retval B_WOULD_BLOCK The calling thread is already performing a resize for
 *                    this cache (re-entrancy guard).
 * @retval B_NO_MEMORY A new slab could not be allocated.
 * @note Must be called with cache->lock held.
 */
static status_t
object_cache_reserve_internal(ObjectCache* cache, size_t objectCount,
	uint32 flags)
{
	// If someone else is already adding slabs, we wait for that to be finished
	// first.
	thread_id thread = find_thread(NULL);
	while (true) {
		if (objectCount <= cache->total_objects - cache->used_count)
			return B_OK;

		ObjectCacheResizeEntry* resizeEntry = NULL;
		if (cache->resize_entry_dont_wait != NULL) {
			resizeEntry = cache->resize_entry_dont_wait;
			if (thread == resizeEntry->thread)
				return B_WOULD_BLOCK;
			// Note: We could still have reentered the function, i.e.
			// resize_entry_can_wait would be ours. That doesn't matter much,
			// though, since after the don't-wait thread has done its job
			// everyone will be happy.
		} else if (cache->resize_entry_can_wait != NULL) {
			resizeEntry = cache->resize_entry_can_wait;
			if (thread == resizeEntry->thread)
				return B_WOULD_BLOCK;

			if ((flags & CACHE_DONT_WAIT_FOR_MEMORY) != 0)
				break;
		} else
			break;

		resizeEntry->condition.Wait(&cache->lock);
	}

	// prepare the resize entry others can wait on
	ObjectCacheResizeEntry*& resizeEntry
		= (flags & CACHE_DONT_WAIT_FOR_MEMORY) != 0
			? cache->resize_entry_dont_wait : cache->resize_entry_can_wait;

	ObjectCacheResizeEntry myResizeEntry;
	resizeEntry = &myResizeEntry;
	resizeEntry->condition.Init(cache, "wait for slabs");
	resizeEntry->thread = thread;

	// add new slabs until there are as many free ones as requested
	while (objectCount > cache->total_objects - cache->used_count) {
		slab* newSlab = cache->CreateSlab(flags);
		if (newSlab == NULL) {
			resizeEntry->condition.NotifyAll();
			resizeEntry = NULL;
			return B_NO_MEMORY;
		}

		cache->usage += cache->slab_size;
		cache->total_objects += newSlab->size;

		cache->empty.Add(newSlab);
		cache->empty_count++;
	}

	resizeEntry->condition.NotifyAll();
	resizeEntry = NULL;

	return B_OK;
}


/**
 * @brief Low-memory callback that trims empty slabs from all object caches.
 *
 * Called by the low-resource manager when memory is scarce. Iterates every
 * registered object cache, optionally calls its reclaimer, drains its depot,
 * and returns empty slabs back to the MemoryManager according to the resource
 * pressure level.
 *
 * @param dummy     Unused cookie (registered as NULL).
 * @param resources Bitmask of low resource types (pages, memory, address space).
 * @param level     B_NO_LOW_RESOURCE, B_LOW_RESOURCE_NOTE,
 *                  B_LOW_RESOURCE_WARNING, or B_LOW_RESOURCE_CRITICAL.
 * @note Runs in the context of the low-resource manager thread. Must not
 *       acquire sObjectCacheListLock for the duration of slab return operations
 *       to avoid priority inversion.
 */
static void
object_cache_low_memory(void* dummy, uint32 resources, int32 level)
{
	if (level == B_NO_LOW_RESOURCE)
		return;

	MutexLocker cacheListLocker(sObjectCacheListLock);

	// Append the first cache to the end of the queue. We assume that it is
	// one of the caches that will never be deleted and thus we use it as a
	// marker.
	ObjectCache* firstCache = sObjectCaches.RemoveHead();
	sObjectCaches.Add(firstCache);
	cacheListLocker.Unlock();

	ObjectCache* cache;
	do {
		cacheListLocker.Lock();

		cache = sObjectCaches.RemoveHead();
		sObjectCaches.Add(cache);

		MutexLocker maintenanceLocker(sMaintenanceLock);
		if (cache->maintenance_pending || cache->maintenance_in_progress) {
			// We don't want to mess with caches in maintenance.
			continue;
		}

		cache->maintenance_pending = true;
		cache->maintenance_in_progress = true;

		maintenanceLocker.Unlock();
		cacheListLocker.Unlock();

		// We are calling the reclaimer without the object cache lock
		// to give the owner a chance to return objects to the slabs.

		if (cache->reclaimer)
			cache->reclaimer(cache->cookie, level);

		if ((cache->flags & CACHE_NO_DEPOT) == 0)
			object_depot_make_empty(&cache->depot, 0);

		MutexLocker cacheLocker(cache->lock);
		size_t minimumAllowed;

		switch (level) {
			case B_LOW_RESOURCE_NOTE:
				minimumAllowed = cache->pressure / 2 + 1;
				cache->pressure -= cache->pressure / 8;
				break;

			case B_LOW_RESOURCE_WARNING:
				cache->pressure /= 2;
				minimumAllowed = 0;
				break;

			default:
				cache->pressure = 0;
				minimumAllowed = 0;
				break;
		}

		while (cache->empty_count > minimumAllowed) {
			// make sure we respect the cache's minimum object reserve
			size_t objectsPerSlab = cache->empty.Head()->size;
			size_t freeObjects = cache->total_objects - cache->used_count;
			if (freeObjects < cache->min_object_reserve + objectsPerSlab)
				break;

			cache->ReturnSlab(cache->empty.RemoveHead(), 0);
			cache->empty_count--;
		}

		cacheLocker.Unlock();

		// Check whether in the meantime someone has really requested
		// maintenance for the cache.
		maintenanceLocker.Lock();

		if (cache->maintenance_delete) {
			delete_object_cache_internal(cache);
			continue;
		}

		cache->maintenance_in_progress = false;

		if (cache->maintenance_resize)
			sMaintenanceQueue.Add(cache);
		else
			cache->maintenance_pending = false;
	} while (cache != firstCache);
}


/**
 * @brief Kernel thread that services object cache maintenance requests.
 *
 * Runs as a dedicated kernel thread ("object cache resizer"). Waits on
 * sMaintenanceCondition for work to appear in sMaintenanceQueue. For each
 * queued cache it either deletes it (if maintenance_delete is set) or calls
 * object_cache_reserve_internal() to satisfy its minimum object reserve. Also
 * drives MemoryManager::PerformMaintenance() when needed.
 *
 * @param unused Unused thread argument.
 * @return B_OK (never actually reached; the thread loops forever).
 * @note Runs at B_URGENT_PRIORITY. Do not call directly; spawned by
 *       slab_init_post_thread().
 */
static status_t
object_cache_maintainer(void*)
{
	while (true) {
		MutexLocker locker(sMaintenanceLock);

		// wait for the next request
		while (sMaintenanceQueue.IsEmpty()) {
			// perform memory manager maintenance, if needed
			if (MemoryManager::MaintenanceNeeded()) {
				locker.Unlock();
				MemoryManager::PerformMaintenance();
				locker.Lock();
				continue;
			}

			sMaintenanceCondition.Wait(locker.Get());
		}

		ObjectCache* cache = sMaintenanceQueue.RemoveHead();

		while (true) {
			bool resizeRequested = cache->maintenance_resize;
			bool deleteRequested = cache->maintenance_delete;

			if (!resizeRequested && !deleteRequested) {
				cache->maintenance_pending = false;
				cache->maintenance_in_progress = false;
				break;
			}

			cache->maintenance_resize = false;
			cache->maintenance_in_progress = true;

			locker.Unlock();

			if (deleteRequested) {
				delete_object_cache_internal(cache);
				break;
			}

			// resize the cache, if necessary

			MutexLocker cacheLocker(cache->lock);

			if (resizeRequested) {
				status_t error = object_cache_reserve_internal(cache,
					cache->min_object_reserve, 0);
				if (error != B_OK) {
					dprintf("object cache resizer: Failed to resize object "
						"cache %p!\n", cache);
					break;
				}
			}

			locker.Lock();
		}
	}

	// never can get here
	return B_OK;
}


// #pragma mark - public API


/**
 * @brief Creates an object cache with default alignment and no magazine limits.
 *
 * Convenience wrapper around create_object_cache_etc() that supplies zeroes for
 * alignment, maximum byte usage, magazine capacity, and max magazine count.
 *
 * @param name        Human-readable name for the cache (used in debug output).
 * @param object_size Size in bytes of each object managed by the cache.
 * @param flags       Cache creation flags (e.g., CACHE_NO_DEPOT).
 * @return Pointer to the new object_cache, or NULL on failure.
 */
object_cache*
create_object_cache(const char* name, size_t object_size, uint32 flags)
{
	return create_object_cache_etc(name, object_size, 0, 0, 0, 0, flags,
		NULL, NULL, NULL, NULL);
}


/**
 * @brief Creates a fully configured object cache.
 *
 * Selects either SmallObjectCache (object <= 256 bytes) or HashedObjectCache
 * (object > 256 bytes), initialises it, and adds it to the global cache list.
 * When DEBUG_HEAPS and GUARDED_HEAP_CAN_REPLACE_OBJECT_CACHES are both set,
 * certain caches may be backed by the guarded heap instead.
 *
 * @param name               Human-readable name for the cache.
 * @param objectSize         Size in bytes of each managed object.
 * @param alignment          Required alignment of each object (0 = default).
 * @param maximum            Maximum total bytes the cache may consume (0 = unlimited).
 * @param magazineCapacity   Number of object slots per depot magazine (0 = default).
 * @param maxMagazineCount   Maximum full magazines in the depot (0 = default).
 * @param flags              Cache creation flags.
 * @param cookie             Opaque pointer forwarded to constructor/destructor/reclaimer.
 * @param constructor        Called after an object is allocated from a new slab.
 * @param destructor         Called before an object's slab memory is released.
 * @param reclaimer          Called by the low-memory handler to reclaim objects.
 * @return Pointer to the new object_cache, or NULL if objectSize is 0 or
 *         memory allocation fails.
 */
object_cache*
create_object_cache_etc(const char* name, size_t objectSize, size_t alignment,
	size_t maximum, size_t magazineCapacity, size_t maxMagazineCount,
	uint32 flags, void* cookie, object_cache_constructor constructor,
	object_cache_destructor destructor, object_cache_reclaimer reclaimer)
{
#if DEBUG_HEAPS && GUARDED_HEAP_CAN_REPLACE_OBJECT_CACHES
	if (guarded_heap_replaces_object_cache(name)) {
		ObjectCache* cache = (ObjectCache*)
			kernel_guarded_heap.memalign(0, sizeof(ObjectCache), flags);
		memset((void*)cache, 0, sizeof(ObjectCache));
		strcpy(cache->name, name);
		cache->object_size = objectSize;
		cache->alignment = alignment;
		cache->constructor = constructor;
		cache->destructor = destructor;
		cache->cookie = cookie;

		cache->flags = (uint32)-1;
		return cache;
	}
#endif

	ObjectCache* cache;

	if (objectSize == 0) {
		cache = NULL;
	} else if (objectSize <= 256) {
		cache = SmallObjectCache::Create(name, objectSize, alignment, maximum,
			magazineCapacity, maxMagazineCount, flags, cookie, constructor,
			destructor, reclaimer);
	} else {
		cache = HashedObjectCache::Create(name, objectSize, alignment, maximum,
			magazineCapacity, maxMagazineCount, flags, cookie, constructor,
			destructor, reclaimer);
	}

	if (cache != NULL) {
		MutexLocker _(sObjectCacheListLock);
		sObjectCaches.Add(cache);
	}

	T(Create(name, objectSize, alignment, maximum, flags, cookie, cache));
	return cache;
}


/**
 * @brief Deletes an object cache and releases all resources.
 *
 * Removes the cache from the global list, then either schedules deferred
 * deletion (if the maintenance thread is currently working on the cache) or
 * calls delete_object_cache_internal() immediately.
 *
 * @param cache Pointer to the object_cache to delete.
 * @note The caller must ensure no further alloc/free calls are made against
 *       @p cache after this function returns (or is scheduled for deferred
 *       deletion).
 */
void
delete_object_cache(object_cache* cache)
{
#if DEBUG_HEAPS && GUARDED_HEAP_CAN_REPLACE_OBJECT_CACHES
	if (cache->flags == (uint32)-1) {
		kernel_guarded_heap.free(cache, 0);
		return;
	}
#endif

	T(Delete(cache));

	{
		MutexLocker _(sObjectCacheListLock);
		sObjectCaches.Remove(cache);
	}

	MutexLocker cacheLocker(cache->lock);

	{
		MutexLocker maintenanceLocker(sMaintenanceLock);
		if (cache->maintenance_in_progress) {
			// The maintainer thread is working with the cache. Just mark it
			// to be deleted.
			cache->maintenance_delete = true;
			return;
		}

		// unschedule maintenance
		if (cache->maintenance_pending)
			sMaintenanceQueue.Remove(cache);
	}

	// at this point no-one should have a reference to the cache anymore
	cacheLocker.Unlock();

	delete_object_cache_internal(cache);
}


/**
 * @brief Sets the minimum number of free objects the cache must always keep
 *        reserved.
 *
 * Updates cache->min_object_reserve and schedules the maintenance thread to
 * grow the cache immediately if the current free count is below the new value.
 *
 * @param cache       Pointer to the target object_cache.
 * @param objectCount New minimum reserve object count.
 * @retval B_OK Always (or immediately if the guarded-heap path is active).
 * @note Acquires cache->lock internally.
 */
status_t
object_cache_set_minimum_reserve(object_cache* cache, size_t objectCount)
{
#if DEBUG_HEAPS && GUARDED_HEAP_CAN_REPLACE_OBJECT_CACHES
	if (cache->flags == (uint32)-1)
		return B_OK;
#endif

	MutexLocker _(cache->lock);

	if (cache->min_object_reserve == objectCount)
		return B_OK;

	cache->min_object_reserve = objectCount;

	increase_object_reserve(cache);

	return B_OK;
}


/**
 * @brief Allocates one object from the object cache (public API, fast + slow path).
 *
 * First attempts to obtain a cached object from the per-CPU depot
 * (object_depot_obtain()). If the depot is empty, acquires cache->lock and
 * allocates from a partial or empty slab, growing the cache if necessary.
 * Records a tracing entry on success.
 *
 * @param cache Pointer to the object_cache from which to allocate.
 * @param flags Allocation flags (e.g., CACHE_DONT_SLEEP, CACHE_DONT_WAIT_FOR_MEMORY).
 * @return Pointer to the allocated object (filled with 0xcc in debug builds),
 *         or NULL if allocation failed.
 * @note This is the primary public allocation entry point. Preemptible; may
 *       sleep unless CACHE_DONT_SLEEP is specified.
 */
void*
object_cache_alloc(object_cache* cache, uint32 flags)
{
#if DEBUG_HEAPS && GUARDED_HEAP_CAN_REPLACE_OBJECT_CACHES
	if (cache->flags == (uint32)-1) {
		void* object = kernel_guarded_heap.memalign(cache->alignment,
			cache->object_size, flags);
		if (cache->constructor != NULL)
			cache->constructor(cache->cookie, object);
		return object;
	}
#endif

	void* object = NULL;
	if ((cache->flags & CACHE_NO_DEPOT) == 0)
		object = object_depot_obtain(&cache->depot);

	if (object == NULL) {
		MutexLocker locker(cache->lock);
		slab* source = NULL;

		while (true) {
			source = cache->partial.Head();
			if (source != NULL)
				break;

			source = cache->empty.RemoveHead();
			if (source != NULL) {
				cache->empty_count--;
				cache->partial.Add(source);
				break;
			}

			if (object_cache_reserve_internal(cache, 1, flags) != B_OK) {
				T(Alloc(cache, flags, NULL));
				return NULL;
			}

			cache->pressure++;
		}

		ParanoiaChecker _2(source);

		slab_queue_link* link = source->free.Pop();
		source->count--;
		cache->used_count++;

		if (cache->total_objects - cache->used_count < cache->min_object_reserve)
			increase_object_reserve(cache);

		REMOVE_PARANOIA_CHECK(PARANOIA_SUSPICIOUS, source, &link->next,
			sizeof(void*));

		TRACE_CACHE(cache, "allocate %p (%p) from %p, %lu remaining.",
			link_to_object(link, cache->object_size), link, source, source->count);

		if (source->count == 0) {
			cache->partial.Remove(source);
			cache->full.Add(source);
		}

		object = link_to_object(link, cache->object_size);
		locker.Unlock();
	}

#if PARANOID_KERNEL_FREE
	if (cache->object_size >= (sizeof(void*) * 2)) {
		ASSERT_ALWAYS_PRINT(*(uint32*)object == 0xdeadbeef,
			"object %p @! slab_object %p", object, object);
	}
#endif

	add_alloc_tracing_entry(cache, flags, object);
	return fill_allocated_block(object, cache->object_size);
}


/**
 * @brief Frees an object back to its object cache (public API, fast + slow path).
 *
 * If the cache uses a depot, stores the object in the per-CPU magazine via
 * object_depot_store(). Otherwise acquires cache->lock and returns the object
 * directly to its slab via ReturnObjectToSlab(). Handles NULL gracefully.
 *
 * When PARANOID_KERNEL_FREE is enabled, checks for double-free by looking for
 * the 0xdeadbeef fill pattern and searching the depot before proceeding.
 *
 * @param cache  Pointer to the object_cache the object belongs to.
 * @param object Pointer to the object to free (may be NULL; no-op in that case).
 * @param flags  Flags forwarded to the depot store or slab return operations.
 * @note This is the primary public free entry point. Preemptible.
 */
void
object_cache_free(object_cache* cache, void* object, uint32 flags)
{
#if DEBUG_HEAPS && GUARDED_HEAP_CAN_REPLACE_OBJECT_CACHES
	if (cache->flags == (uint32)-1) {
		if (cache->destructor != NULL)
			cache->destructor(cache->cookie, object);
		kernel_guarded_heap.free(object, flags);
		return;
	}
#endif

	if (object == NULL)
		return;

	T(Free(cache, object));

#if PARANOID_KERNEL_FREE
	// TODO: allow forcing the check even if we don't find deadbeef
	if (*(uint32*)object == 0xdeadbeef) {
		if (!cache->AssertObjectNotFreed(object))
			return;

		if ((cache->flags & CACHE_NO_DEPOT) == 0) {
			if (object_depot_contains_object(&cache->depot, object)) {
				panic("object_cache: object %p is already freed", object);
				return;
			}
		}
	}

	fill_freed_block(object, cache->object_size);
#endif

#if SLAB_OBJECT_CACHE_ALLOCATION_TRACKING
	mutex_lock(&cache->lock);
	cache->TrackingInfoFor(object)->Clear();
	mutex_unlock(&cache->lock);
#endif

	if ((cache->flags & CACHE_NO_DEPOT) == 0) {
		object_depot_store(&cache->depot, object, flags);
		return;
	}

	MutexLocker _(cache->lock);
	cache->ReturnObjectToSlab(cache->ObjectSlab(object), object, flags);
}


/**
 * @brief Pre-populates an object cache so that at least @p objectCount objects
 *        can be allocated without triggering slab growth.
 *
 * Acquires cache->lock and delegates to object_cache_reserve_internal().
 *
 * @param cache       Pointer to the target object_cache.
 * @param objectCount Number of free objects to ensure are available.
 * @param flags       Allocation flags (e.g., CACHE_DONT_WAIT_FOR_MEMORY).
 * @retval B_OK        Reserve satisfied.
 * @retval B_NO_MEMORY Could not allocate the required slabs.
 * @note May sleep waiting for memory unless CACHE_DONT_WAIT_FOR_MEMORY is set.
 */
status_t
object_cache_reserve(object_cache* cache, size_t objectCount, uint32 flags)
{
#if DEBUG_HEAPS && GUARDED_HEAP_CAN_REPLACE_OBJECT_CACHES
	if (cache->flags == (uint32)-1)
		return B_OK;
#endif

	if (objectCount == 0)
		return B_OK;

	T(Reserve(cache, objectCount, flags));

	MutexLocker _(cache->lock);
	return object_cache_reserve_internal(cache, objectCount, flags);
}


/**
 * @brief Returns the total number of bytes currently allocated from the
 *        MemoryManager on behalf of this cache.
 *
 * @param cache             Pointer to the target object_cache.
 * @param _allocatedMemory  Out: set to cache->usage (bytes of slab memory
 *                          currently held by the cache).
 * @note Acquires cache->lock internally.
 */
void
object_cache_get_usage(object_cache* cache, size_t* _allocatedMemory)
{
	MutexLocker _(cache->lock);
	*_allocatedMemory = cache->usage;
}


/**
 * @brief Performs early slab allocator initialisation during kernel boot.
 *
 * Calls MemoryManager::Init() to set up the memory manager with the early
 * kernel_args memory map, and placement-constructs the global object cache list.
 *
 * @param args Pointer to the kernel_args structure provided by the bootloader.
 * @note Called very early in boot, before VM areas exist. Must not allocate
 *       memory through normal means. No locking is needed as only one CPU is
 *       active at this stage.
 */
void
slab_init(kernel_args* args)
{
	MemoryManager::Init(args);

	new (&sObjectCaches) ObjectCacheList();
}


/**
 * @brief Completes slab allocator initialisation after VM areas are available.
 *
 * Calls MemoryManager::InitPostArea() so that the memory manager can move its
 * bootstrap structures into proper VM areas. Also registers all slab-related
 * kernel debugger commands.
 *
 * @note Called during the area-init phase of kernel startup. No locking
 *       required; runs single-threaded.
 */
void
slab_init_post_area()
{
	MemoryManager::InitPostArea();

	add_debugger_command("slabs", dump_slabs, "list all object caches");
	add_debugger_command("slab_cache", dump_cache_info,
		"dump information about a specific object cache");
	add_debugger_command("slab_depot", dump_object_depot,
		"dump contents of an object depot");
	add_debugger_command("slab_magazine", dump_depot_magazine,
		"dump contents of a depot magazine");
	add_debugger_command("slab_object", dump_object_info,
		"dump information about an object in an object_cache");
#if SLAB_ALLOCATION_TRACKING_AVAILABLE
	add_debugger_command_etc("allocations_per_caller",
		&dump_allocations_per_caller,
		"Dump current slab allocations summed up per caller",
		"[ -c ] [ -d <caller> ] [ -o <object cache> ] [ -r ]\n"
		"The current allocations will by summed up by caller (their count and\n"
		"size) printed in decreasing order by size or, if \"-c\" is\n"
		"specified, by allocation count. If given <object cache> specifies\n"
		"the address of the object cache for which to print the allocations.\n"
		"If \"-d\" is given, each allocation for caller <caller> is printed\n"
		"including the respective stack trace.\n"
		"If \"-r\" is given, the allocation infos are reset after gathering\n"
		"the information, so the next command invocation will only show the\n"
		"allocations made after the reset.\n", 0);
	add_debugger_command_etc("allocation_infos",
		&dump_allocation_infos,
		"Dump current slab allocations",
		"[ --stacktrace ] [ -o <object cache> | -s <slab> | -S <address> ] "
		"[ -a <allocation> ] [ --team <team ID> ] [ --thread <thread ID> ]\n"
		"The current allocations filtered by optional values will be printed.\n"
		"If given, <object cache> specifies the address of the object cache\n"
		"or <slab> specifies the address of a slab, for which to print the\n"
		"allocations. Alternatively <address> specifies any address within\n"
		"a slab allocation range.\n"
		"The optional \"-a\" address filters for a specific allocation,\n"
		"with \"--team\" and \"--thread\" allocations by specific teams\n"
		"and/or threads can be filtered (these only work if a corresponding\n"
		"tracing entry is still available).\n"
		"If \"--stacktrace\" is given, then stack traces of the allocation\n"
		"callers are printed, where available\n", 0);
#endif	// SLAB_ALLOCATION_TRACKING_AVAILABLE
}


/**
 * @brief Registers the low-memory handler for slab trimming after semaphores
 *        are available.
 *
 * Registers object_cache_low_memory() with the low-resource manager so that
 * the slab allocator can return memory under page, memory, and address-space
 * pressure. Called during the semaphore-init phase of kernel startup.
 *
 * @note No locking required; runs single-threaded during kernel init.
 */
void
slab_init_post_sem()
{
	register_low_resource_handler(object_cache_low_memory, NULL,
		B_KERNEL_RESOURCE_PAGES | B_KERNEL_RESOURCE_MEMORY
			| B_KERNEL_RESOURCE_ADDRESS_SPACE, 5);
}


/**
 * @brief Spawns the object cache maintenance thread after threading is
 *        available.
 *
 * Placement-constructs the MaintenanceQueue, initialises the
 * sMaintenanceCondition, and spawns the "object cache resizer" kernel thread
 * at B_URGENT_PRIORITY to service cache resize and deletion requests.
 *
 * @note Panics if the maintenance thread cannot be spawned. Must be called
 *       exactly once, during the threading-init phase of kernel startup.
 */
void
slab_init_post_thread()
{
	new(&sMaintenanceQueue) MaintenanceQueue;
	sMaintenanceCondition.Init(&sMaintenanceQueue, "object cache maintainer");

	thread_id objectCacheResizer = spawn_kernel_thread(object_cache_maintainer,
		"object cache resizer", B_URGENT_PRIORITY, NULL);
	if (objectCacheResizer < 0) {
		panic("slab_init_post_thread(): failed to spawn object cache resizer "
			"thread\n");
		return;
	}

	resume_thread(objectCacheResizer);
}


RANGE_MARKER_FUNCTION_END(Slab)


#endif	// !USE_DEBUG_HEAPS_FOR_ALL_OBJECT_CACHES
