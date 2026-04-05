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
 *   Copyright 2008-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file VMCache.cpp
 * @brief Virtual memory cache — manages the page cache for VM areas.
 *
 * VMCache is the central object connecting VMArea objects to their backing
 * pages. Each area maps into a cache (which may be a file cache, anonymous
 * cache, or device cache). The cache owns the physical pages and handles
 * page faults, page writing/reading, and cache consumer/source chains.
 *
 * @see VMAnonymousCache.cpp, VMArea.cpp, vm.cpp
 */


#include <vm/VMCache.h>

#include <stddef.h>
#include <stdlib.h>

#include <algorithm>

#include <arch/cpu.h>
#include <condition_variable.h>
#include <heap.h>
#include <interrupts.h>
#include <kernel.h>
#include <slab/Slab.h>
#include <smp.h>
#include <thread.h>
#include <tracing.h>
#include <util/AutoLock.h>
#include <vfs.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_priv.h>
#include <vm/vm_types.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>

// needed for the factory only
#include "VMAnonymousCache.h"
#include "VMAnonymousNoSwapCache.h"
#include "VMDeviceCache.h"
#include "VMNullCache.h"
#include "../cache/vnode_store.h"


//#define TRACE_VM_CACHE
#ifdef TRACE_VM_CACHE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


#if DEBUG_CACHE_LIST
VMCache* gDebugCacheList;
#endif
static rw_lock sCacheListLock = RW_LOCK_INITIALIZER("global VMCache list");
	// The lock is also needed when the debug feature is disabled.

ObjectCache* gCacheRefObjectCache;
#if ENABLE_SWAP_SUPPORT
ObjectCache* gAnonymousCacheObjectCache;
#endif
ObjectCache* gAnonymousNoSwapCacheObjectCache;
ObjectCache* gVnodeCacheObjectCache;
ObjectCache* gDeviceCacheObjectCache;
ObjectCache* gNullCacheObjectCache;


struct VMCache::PageEventWaiter {
	Thread*				thread;
	PageEventWaiter*	next;
	vm_page*			page;
	uint32				events;
};


#if VM_CACHE_TRACING

namespace VMCacheTracing {

class VMCacheTraceEntry : public AbstractTraceEntry {
	public:
		VMCacheTraceEntry(VMCache* cache)
			:
			fCache(cache)
		{
#if VM_CACHE_TRACING_STACK_TRACE
			fStackTrace = capture_tracing_stack_trace(
				VM_CACHE_TRACING_STACK_TRACE, 0, true);
				// Don't capture userland stack trace to avoid potential
				// deadlocks.
#endif
		}

#if VM_CACHE_TRACING_STACK_TRACE
		virtual void DumpStackTrace(TraceOutput& out)
		{
			out.PrintStackTrace(fStackTrace);
		}
#endif

		VMCache* Cache() const
		{
			return fCache;
		}

	protected:
		VMCache*	fCache;
#if VM_CACHE_TRACING_STACK_TRACE
		tracing_stack_trace* fStackTrace;
#endif
};


class Create : public VMCacheTraceEntry {
	public:
		Create(VMCache* cache)
			:
			VMCacheTraceEntry(cache)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache create: -> cache: %p", fCache);
		}
};


class Delete : public VMCacheTraceEntry {
	public:
		Delete(VMCache* cache)
			:
			VMCacheTraceEntry(cache)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache delete: cache: %p", fCache);
		}
};


class SetMinimalCommitment : public VMCacheTraceEntry {
	public:
		SetMinimalCommitment(VMCache* cache, off_t commitment)
			:
			VMCacheTraceEntry(cache),
			fOldCommitment(cache->Commitment()),
			fCommitment(commitment)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache set min commitment: cache: %p, "
				"commitment: %" B_PRIdOFF " -> %" B_PRIdOFF, fCache,
				fOldCommitment, fCommitment);
		}

	private:
		off_t	fOldCommitment;
		off_t	fCommitment;
};


class Resize : public VMCacheTraceEntry {
	public:
		Resize(VMCache* cache, off_t size)
			:
			VMCacheTraceEntry(cache),
			fOldSize(cache->virtual_end),
			fSize(size)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache resize: cache: %p, size: %" B_PRIdOFF " -> %"
				B_PRIdOFF, fCache, fOldSize, fSize);
		}

	private:
		off_t	fOldSize;
		off_t	fSize;
};


class Rebase : public VMCacheTraceEntry {
	public:
		Rebase(VMCache* cache, off_t base)
			:
			VMCacheTraceEntry(cache),
			fOldBase(cache->virtual_base),
			fBase(base)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache rebase: cache: %p, base: %lld -> %lld", fCache,
				fOldBase, fBase);
		}

	private:
		off_t	fOldBase;
		off_t	fBase;
};


class AddConsumer : public VMCacheTraceEntry {
	public:
		AddConsumer(VMCache* cache, VMCache* consumer)
			:
			VMCacheTraceEntry(cache),
			fConsumer(consumer)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache add consumer: cache: %p, consumer: %p", fCache,
				fConsumer);
		}

		VMCache* Consumer() const
		{
			return fConsumer;
		}

	private:
		VMCache*	fConsumer;
};


class RemoveConsumer : public VMCacheTraceEntry {
	public:
		RemoveConsumer(VMCache* cache, VMCache* consumer)
			:
			VMCacheTraceEntry(cache),
			fConsumer(consumer)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache remove consumer: cache: %p, consumer: %p",
				fCache, fConsumer);
		}

	private:
		VMCache*	fConsumer;
};


class Merge : public VMCacheTraceEntry {
	public:
		Merge(VMCache* cache, VMCache* consumer)
			:
			VMCacheTraceEntry(cache),
			fConsumer(consumer)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache merge with consumer: cache: %p, consumer: %p",
				fCache, fConsumer);
		}

	private:
		VMCache*	fConsumer;
};


class InsertArea : public VMCacheTraceEntry {
	public:
		InsertArea(VMCache* cache, VMArea* area)
			:
			VMCacheTraceEntry(cache),
			fArea(area)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache insert area: cache: %p, area: %p", fCache,
				fArea);
		}

		VMArea*	Area() const
		{
			return fArea;
		}

	private:
		VMArea*	fArea;
};


class RemoveArea : public VMCacheTraceEntry {
	public:
		RemoveArea(VMCache* cache, VMArea* area)
			:
			VMCacheTraceEntry(cache),
			fArea(area)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache remove area: cache: %p, area: %p", fCache,
				fArea);
		}

	private:
		VMArea*	fArea;
};

}	// namespace VMCacheTracing

#	define T(x) new(std::nothrow) VMCacheTracing::x;

#	if VM_CACHE_TRACING >= 2

namespace VMCacheTracing {

class InsertPage : public VMCacheTraceEntry {
	public:
		InsertPage(VMCache* cache, vm_page* page, off_t offset)
			:
			VMCacheTraceEntry(cache),
			fPage(page),
			fOffset(offset)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache insert page: cache: %p, page: %p, offset: %"
				B_PRIdOFF, fCache, fPage, fOffset);
		}

	private:
		vm_page*	fPage;
		off_t		fOffset;
};


class RemovePage : public VMCacheTraceEntry {
	public:
		RemovePage(VMCache* cache, vm_page* page)
			:
			VMCacheTraceEntry(cache),
			fPage(page)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache remove page: cache: %p, page: %p", fCache,
				fPage);
		}

	private:
		vm_page*	fPage;
};

}	// namespace VMCacheTracing

#		define T2(x) new(std::nothrow) VMCacheTracing::x;
#	else
#		define T2(x) ;
#	endif
#else
#	define T(x) ;
#	define T2(x) ;
#endif


//	#pragma mark - debugger commands


#if VM_CACHE_TRACING


/**
 * @brief Walk the trace log backwards to find the cache that owns an area.
 *
 * @param baseIterator Iterator positioned at the trace entry to start from.
 * @param area         Pointer to the VMArea whose owning cache is sought.
 * @return Pointer to the VMCache that last inserted @a area, or NULL if not found.
 */
static void*
cache_stack_find_area_cache(const TraceEntryIterator& baseIterator, void* area)
{
	using namespace VMCacheTracing;

	// find the previous "insert area" entry for the given area
	TraceEntryIterator iterator = baseIterator;
	TraceEntry* entry = iterator.Current();
	while (entry != NULL) {
		if (InsertArea* insertAreaEntry = dynamic_cast<InsertArea*>(entry)) {
			if (insertAreaEntry->Area() == area)
				return insertAreaEntry->Cache();
		}

		entry = iterator.Previous();
	}

	return NULL;
}


/**
 * @brief Walk the trace log backwards to find the source cache for a consumer.
 *
 * @param baseIterator Iterator positioned at the trace entry to start from.
 * @param cache        Pointer to the consumer VMCache whose source is sought.
 * @return Pointer to the source VMCache, or NULL if this cache was the root.
 */
static void*
cache_stack_find_consumer(const TraceEntryIterator& baseIterator, void* cache)
{
	using namespace VMCacheTracing;

	// find the previous "add consumer" or "create" entry for the given cache
	TraceEntryIterator iterator = baseIterator;
	TraceEntry* entry = iterator.Current();
	while (entry != NULL) {
		if (Create* createEntry = dynamic_cast<Create*>(entry)) {
			if (createEntry->Cache() == cache)
				return NULL;
		} else if (AddConsumer* addEntry = dynamic_cast<AddConsumer*>(entry)) {
			if (addEntry->Consumer() == cache)
				return addEntry->Cache();
		}

		entry = iterator.Previous();
	}

	return NULL;
}


/**
 * @brief Kernel debugger command that prints the ancestor cache chain.
 *
 * Usage: cache_stack [area] <address> <tracing_entry_index>
 *
 * @param argc Number of command arguments.
 * @param argv Command argument strings.
 * @return 0 in all cases (debugger command convention).
 */
static int
command_cache_stack(int argc, char** argv)
{
	if (argc < 3 || argc > 4) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	bool isArea = false;

	int argi = 1;
	if (argc == 4) {
		if (strcmp(argv[argi], "area") != 0) {
			print_debugger_command_usage(argv[0]);
			return 0;
		}

		argi++;
		isArea = true;
	}

	uint64 addressValue;
	uint64 debugEntryIndex;
	if (!evaluate_debug_expression(argv[argi++], &addressValue, false)
		|| !evaluate_debug_expression(argv[argi++], &debugEntryIndex, false)) {
		return 0;
	}

	TraceEntryIterator baseIterator;
	if (baseIterator.MoveTo((int32)debugEntryIndex) == NULL) {
		kprintf("Invalid tracing entry index %" B_PRIu64 "\n", debugEntryIndex);
		return 0;
	}

	void* address = (void*)(addr_t)addressValue;

	kprintf("cache stack for %s %p at %" B_PRIu64 ":\n",
		isArea ? "area" : "cache", address, debugEntryIndex);
	if (isArea) {
		address = cache_stack_find_area_cache(baseIterator, address);
		if (address == NULL) {
			kprintf("  cache not found\n");
			return 0;
		}
	}

	while (address != NULL) {
		kprintf("  %p\n", address);
		address = cache_stack_find_consumer(baseIterator, address);
	}

	return 0;
}


#endif	// VM_CACHE_TRACING


//	#pragma mark -


/**
 * @brief Initializes per-subsystem object caches used to allocate VM cache structures.
 *
 * Creates slab object caches for VMCacheRef, VMAnonymousCache,
 * VMAnonymousNoSwapCache, VMVnodeCache, VMDeviceCache, and VMNullCache.
 * Must be called once during early kernel initialization.
 *
 * @param args Kernel boot arguments (unused by this function).
 * @retval B_OK        All object caches were created successfully.
 * @retval B_NO_MEMORY At least one object cache allocation failed; kernel panics.
 */
status_t
vm_cache_init(kernel_args* args)
{
	// Create object caches for the structures we allocate here.
	gCacheRefObjectCache = create_object_cache("cache refs", sizeof(VMCacheRef),
		0);
#if ENABLE_SWAP_SUPPORT
	gAnonymousCacheObjectCache = create_object_cache("anon caches",
		sizeof(VMAnonymousCache), 0);
#endif
	gAnonymousNoSwapCacheObjectCache = create_object_cache(
		"anon no-swap caches", sizeof(VMAnonymousNoSwapCache), 0);
	gVnodeCacheObjectCache = create_object_cache("vnode caches",
		sizeof(VMVnodeCache), 0);
	gDeviceCacheObjectCache = create_object_cache("device caches",
		sizeof(VMDeviceCache), 0);
	gNullCacheObjectCache = create_object_cache("null caches",
		sizeof(VMNullCache), 0);

	if (gCacheRefObjectCache == NULL
#if ENABLE_SWAP_SUPPORT
		|| gAnonymousCacheObjectCache == NULL
#endif
		|| gAnonymousNoSwapCacheObjectCache == NULL
		|| gVnodeCacheObjectCache == NULL
		|| gDeviceCacheObjectCache == NULL
		|| gNullCacheObjectCache == NULL) {
		panic("vm_cache_init(): Failed to create object caches!");
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Registers debugger commands that depend on the heap being available.
 *
 * Called after the heap is fully initialised. When VM_CACHE_TRACING is
 * enabled this registers the "cache_stack" debugger command.
 */
void
vm_cache_init_post_heap()
{
#if VM_CACHE_TRACING
	add_debugger_command_etc("cache_stack", &command_cache_stack,
		"List the ancestors (sources) of a VMCache at the time given by "
			"tracing entry index",
		"[ \"area\" ] <address> <tracing entry index>\n"
		"All ancestors (sources) of a given VMCache at the time given by the\n"
		"tracing entry index are listed. If \"area\" is given the supplied\n"
		"address is an area instead of a cache address. The listing will\n"
		"start with the area's cache at that point.\n",
		0);
#endif	// VM_CACHE_TRACING
}


/**
 * @brief Acquires a locked reference to the VMCache that owns a physical page.
 *
 * Looks up the cache via the page's CacheRef, locks it, verifies the page
 * still belongs to that cache, and returns the cache with its reference count
 * incremented. The caller is responsible for calling ReleaseRef() and
 * Unlock() when done.
 *
 * @param page     The physical page whose owning cache is requested.
 * @param dontWait If true, the function returns NULL instead of blocking when
 *                 the cache lock is contended.
 * @return Pointer to the locked, ref-counted VMCache, or NULL if the page has
 *         no cache or (when @a dontWait is true) the lock could not be acquired.
 * @note Acquires and releases sCacheListLock internally.
 */
VMCache*
vm_cache_acquire_locked_page_cache(vm_page* page, bool dontWait)
{
	rw_lock_read_lock(&sCacheListLock);

	while (true) {
		VMCacheRef* cacheRef = page->CacheRef();
		if (cacheRef == NULL) {
			rw_lock_read_unlock(&sCacheListLock);
			return NULL;
		}

		VMCache* cache = cacheRef->cache;
		if (dontWait) {
			if (!cache->TryLock()) {
				rw_lock_read_unlock(&sCacheListLock);
				return NULL;
			}
		} else {
			if (!cache->SwitchFromReadLock(&sCacheListLock)) {
				// cache has been deleted
				rw_lock_read_lock(&sCacheListLock);
				continue;
			}
			rw_lock_read_lock(&sCacheListLock);
		}

		if (cache == page->Cache()) {
			rw_lock_read_unlock(&sCacheListLock);
			cache->AcquireRefLocked();
			return cache;
		}

		// the cache changed in the meantime
		cache->Unlock();
	}
}


// #pragma mark - VMCache


/**
 * @brief Constructs a VMCacheRef that back-references the owning VMCache.
 *
 * @param cache The VMCache that this ref object represents.
 */
VMCacheRef::VMCacheRef(VMCache* cache)
	:
	cache(cache)
{
}


// #pragma mark - VMCache


/**
 * @brief Returns true if this cache is eligible for merging with its sole consumer.
 *
 * A cache is mergeable when it has no areas, is temporary, and has exactly
 * one consumer.
 *
 * @return true if the cache can be merged with its only consumer.
 * @note The cache lock must be held by the caller.
 */
bool
VMCache::_IsMergeable() const
{
	return areas.IsEmpty() && temporary
		&& !consumers.IsEmpty() && consumers.Head() == consumers.Tail();
}


/**
 * @brief Default constructor — initialises the cache ref pointer to NULL.
 *
 * @note Call Init() before using this object.
 */
VMCache::VMCache()
	:
	fCacheRef(NULL)
{
}


/**
 * @brief Destructor — asserts invariants and frees the VMCacheRef object.
 *
 * Panics in debug builds if the reference count or page count is non-zero
 * at destruction time.
 */
VMCache::~VMCache()
{
	ASSERT(fRefCount == 0 && page_count == 0);

	object_cache_delete(gCacheRefObjectCache, fCacheRef);
}


/**
 * @brief Initializes the VMCache fields and allocates its VMCacheRef.
 *
 * Must be called exactly once after construction. Sets up the mutex, zeroes
 * all counters, and inserts the cache into the global debug cache list when
 * DEBUG_CACHE_LIST is enabled.
 *
 * @param name            Human-readable name used to label the mutex.
 * @param cacheType       One of the CACHE_TYPE_* constants identifying the
 *                        backing-store type.
 * @param allocationFlags Heap allocation flags (e.g. HEAP_DONT_WAIT_FOR_MEMORY).
 * @retval B_OK        Initialisation succeeded.
 * @retval B_NO_MEMORY Failed to allocate the VMCacheRef object.
 */
status_t
VMCache::Init(const char* name, uint32 cacheType, uint32 allocationFlags)
{
	mutex_init(&fLock, name);

	fRefCount = 1;
	source = NULL;
	virtual_base = 0;
	virtual_end = 0;
	temporary = 0;
	page_count = 0;
	fWiredPagesCount = 0;
	fFaultCount = 0;
	fCopiedPagesCount = 0;
	type = cacheType;
	fPageEventWaiters = NULL;

#if DEBUG_CACHE_LIST
	debug_previous = NULL;
	debug_next = NULL;
		// initialize in case the following fails
#endif

	fCacheRef = new(gCacheRefObjectCache, allocationFlags) VMCacheRef(this);
	if (fCacheRef == NULL)
		return B_NO_MEMORY;

#if DEBUG_CACHE_LIST
	rw_lock_write_lock(&sCacheListLock);

	if (gDebugCacheList != NULL)
		gDebugCacheList->debug_previous = this;
	debug_next = gDebugCacheList;
	gDebugCacheList = this;

	rw_lock_write_unlock(&sCacheListLock);
#endif

	return B_OK;
}


/**
 * @brief Destroys the cache, freeing all pages and detaching from its source.
 *
 * Panics if the cache still has areas, consumers, or removed-but-still-busy
 * pages. Frees every page in the page tree, removes the reference to the
 * source cache, removes the cache from the global debug list, destroys the
 * mutex, and finally calls DeleteObject() on the subclass.
 *
 * @note The cache must be locked and its reference count must have reached zero
 *       before Delete() is invoked.  This is normally called automatically by
 *       Unlock() when the last reference is released.
 */
void
VMCache::Delete()
{
	if (!areas.IsEmpty())
		panic("cache %p to be deleted still has areas", this);
	if (!consumers.IsEmpty())
		panic("cache %p to be deleted still has consumers", this);
	if (!fRemovedBusyPages.IsEmpty())
		panic("cache %p to be deleted still has removed busy pages", this);

	T(Delete(this));

	// free all of the pages in the cache
	vm_page_reservation reservation = {};
	while (vm_page* page = pages.Root()) {
		if (!page->mappings.IsEmpty() || page->WiredCount() != 0) {
			panic("remove page %p from cache %p: page still has mappings!\n"
				"@!page %p; cache %p", page, this, page, this);
		}

		// remove it
		pages.Remove(page);
		page->SetCacheRef(NULL);
		page_count--;

		TRACE(("vm_cache_release_ref: freeing page 0x%lx\n",
			page->physical_page_number));
		DEBUG_PAGE_ACCESS_START(page);
		vm_page_free_etc(this, page, &reservation);
	}
	vm_page_unreserve_pages(&reservation);

	// remove the ref to the source
	if (source)
		source->_RemoveConsumer(this);

	// We lock and unlock the sCacheListLock, even if the DEBUG_CACHE_LIST is
	// not enabled. This synchronization point is needed for
	// vm_cache_acquire_locked_page_cache().
	rw_lock_write_lock(&sCacheListLock);

#if DEBUG_CACHE_LIST
	if (debug_previous)
		debug_previous->debug_next = debug_next;
	if (debug_next)
		debug_next->debug_previous = debug_previous;
	if (this == gDebugCacheList)
		gDebugCacheList = debug_next;
#endif

	mutex_destroy(&fLock);

	rw_lock_write_unlock(&sCacheListLock);

	DeleteObject();
}


/**
 * @brief Unlocks the cache and, if eligible, merges it with its sole consumer.
 *
 * When the reference count is 1 and the cache is mergeable (_IsMergeable()),
 * the method tries to acquire the consumer's lock and calls
 * _MergeWithOnlyConsumer(). If the reference count drops to zero after any
 * merge attempt, Delete() is called instead of releasing the mutex.
 *
 * @param consumerLocked Pass true if the sole consumer's lock is already held
 *                       by the caller, to avoid a redundant lock acquisition.
 * @note The cache must be locked when this is called.
 */
void
VMCache::Unlock(bool consumerLocked)
{
	while (fRefCount == 1 && _IsMergeable()) {
		VMCache* consumer = consumers.Head();
		if (consumerLocked) {
			_MergeWithOnlyConsumer();
		} else if (consumer->TryLock()) {
			_MergeWithOnlyConsumer();
			consumer->Unlock();
		} else {
			// Someone else has locked the consumer ATM. Unlock this cache and
			// wait for the consumer lock. Increment the cache's ref count
			// temporarily, so that no one else will try what we are doing or
			// delete the cache.
			fRefCount++;
			bool consumerLockedTemp = consumer->SwitchLock(&fLock);
			Lock();
			fRefCount--;

			if (consumerLockedTemp) {
				if (fRefCount == 1 && _IsMergeable()
						&& consumer == consumers.Head()) {
					// nothing has changed in the meantime -- merge
					_MergeWithOnlyConsumer();
				}

				consumer->Unlock();
			}
		}
	}

	if (fRefCount == 0) {
		// delete this cache
		Delete();
	} else
		mutex_unlock(&fLock);
}


/**
 * @brief Looks up a page by its byte offset within the cache.
 *
 * @param offset Byte offset into the cache; must be page-aligned.
 * @return Pointer to the vm_page, or NULL if no page exists at @a offset.
 * @note The cache lock must be held by the caller.
 */
vm_page*
VMCache::LookupPage(off_t offset)
{
	AssertLocked();

	vm_page* page = pages.Lookup((page_num_t)(offset >> PAGE_SHIFT));

#if KDEBUG
	if (page != NULL && page->Cache() != this)
		panic("page %p not in cache %p\n", page, this);
#endif

	return page;
}


/**
 * @brief Inserts a physical page into the cache at the given byte offset.
 *
 * Sets the page's cache_offset, increments page_count, links the page to
 * fCacheRef, and inserts it into the splay tree. Increments the wired-pages
 * counter if the page is wired.
 *
 * @param page   The physical page to insert; must not already belong to a cache.
 * @param offset Byte offset within the cache where the page should reside.
 * @note The cache lock must be held. @a offset must be within
 *       [virtual_base, virtual_end).
 */
void
VMCache::InsertPage(vm_page* page, off_t offset)
{
	TRACE(("VMCache::InsertPage(): cache %p, page %p, offset %" B_PRIdOFF "\n",
		this, page, offset));
	T2(InsertPage(this, page, offset));

	AssertLocked();
	ASSERT(offset >= virtual_base && offset < virtual_end);

	if (page->CacheRef() != NULL) {
		panic("insert page %p into cache %p: page cache is set to %p\n",
			page, this, page->Cache());
	}

	page->cache_offset = (page_num_t)(offset >> PAGE_SHIFT);
	page_count++;
	page->SetCacheRef(fCacheRef);

#if KDEBUG
	vm_page* otherPage = pages.Lookup(page->cache_offset);
	if (otherPage != NULL) {
		panic("VMCache::InsertPage(): there's already page %p with cache "
			"offset %" B_PRIuPHYSADDR " in cache %p; inserting page %p",
			otherPage, page->cache_offset, this, page);
	}
#endif	// KDEBUG

	pages.Insert(page);

	if (page->WiredCount() > 0)
		IncrementWiredPagesCount();
}


/*!	Frees a page that was removed by _FreePageRange(), but which was busy
	and couldn't be freed then.
*/
/**
 * @brief Frees a page that was deferred because it was busy during range removal.
 *
 * Notifies any threads waiting on PAGE_EVENT_NOT_BUSY, removes the page from
 * fRemovedBusyPages, and releases it back to the free pool.
 *
 * @param page The page previously placed on fRemovedBusyPages by _FreePageRange().
 * @note The cache lock must be held.
 */
void
VMCache::FreeRemovedPage(vm_page* page)
{
	AssertLocked();

	NotifyPageEvents(page, PAGE_EVENT_NOT_BUSY);
	fRemovedBusyPages.Remove(page);
	vm_page_free(this, page);
}


/*!	Removes the vm_page from this cache. Of course, the page must
	really be in this cache or evil things will happen.
	The cache lock must be held.
*/
/**
 * @brief Removes a physical page from this cache's page tree.
 *
 * Decrements page_count, clears the page's CacheRef, and removes it from
 * the splay tree. Decrements the wired-pages counter if the page is wired.
 *
 * @param page The page to remove; must belong to this cache.
 * @note The cache lock must be held.
 */
void
VMCache::RemovePage(vm_page* page)
{
	TRACE(("VMCache::RemovePage(): cache %p, page %p\n", this, page));
	AssertLocked();

	if (page->Cache() != this) {
		panic("remove page %p from cache %p: page cache is set to %p\n", page,
			this, page->Cache());
	}

	T2(RemovePage(this, page));

	pages.Remove(page);
	page_count--;
	page->SetCacheRef(NULL);

	if (page->WiredCount() > 0)
		DecrementWiredPagesCount();
}


/*!	Moves the given page from its current cache inserts it into this cache
	at the given offset.
	Both caches must be locked.
*/
/**
 * @brief Moves a page from its current cache into this cache at a new offset.
 *
 * Removes the page from its old cache, updates cache_offset, and inserts it
 * here. Adjusts wired-page counts on both caches if necessary.
 *
 * @param page   The page to move.
 * @param offset New byte offset within this cache for the page.
 * @note Both this cache and the page's current cache must be locked.
 *       @a offset must be within [virtual_base, virtual_end).
 */
void
VMCache::MovePage(vm_page* page, off_t offset)
{
	VMCache* oldCache = page->Cache();

	AssertLocked();
	oldCache->AssertLocked();
	ASSERT(offset >= virtual_base && offset < virtual_end);

	// remove from old cache
	oldCache->pages.Remove(page);
	oldCache->page_count--;
	T2(RemovePage(oldCache, page));

	// change the offset
	page->cache_offset = offset >> PAGE_SHIFT;

	// insert here
	pages.Insert(page);
	page_count++;
	page->SetCacheRef(fCacheRef);

	if (page->WiredCount() > 0) {
		IncrementWiredPagesCount();
		oldCache->DecrementWiredPagesCount();
	}

	T2(InsertPage(this, page, page->cache_offset << PAGE_SHIFT));
}

/*!	Moves the given page from its current cache inserts it into this cache.
	Both caches must be locked.
*/
/**
 * @brief Moves a page from its current cache into this cache, keeping its offset.
 *
 * Convenience overload that preserves the page's existing cache_offset.
 *
 * @param page The page to move; its cache_offset is retained.
 * @note Both this cache and the page's current cache must be locked.
 */
void
VMCache::MovePage(vm_page* page)
{
	MovePage(page, page->cache_offset << PAGE_SHIFT);
}


/*!	Moves all pages from the given cache to this one.
	Both caches must be locked. This cache must be empty.
*/
/**
 * @brief Atomically transfers the entire page tree from another cache into this one.
 *
 * Swaps the internal page trees, moves the page and wired-page counts, and
 * re-links the VMCacheRef objects under sCacheListLock so that existing
 * page-to-cache pointers remain valid.
 *
 * @param fromCache The source cache; must be locked. Must not equal this cache.
 * @note This cache must be empty (page_count == 0) before the call. Both
 *       caches must be locked.
 */
void
VMCache::MoveAllPages(VMCache* fromCache)
{
	AssertLocked();
	fromCache->AssertLocked();
	ASSERT(page_count == 0);

	std::swap(fromCache->pages, pages);
	page_count = fromCache->page_count;
	fromCache->page_count = 0;
	fWiredPagesCount = fromCache->fWiredPagesCount;
	fromCache->fWiredPagesCount = 0;

	// swap the VMCacheRefs
	rw_lock_write_lock(&sCacheListLock);
	std::swap(fCacheRef, fromCache->fCacheRef);
	fCacheRef->cache = this;
	fromCache->fCacheRef->cache = fromCache;
	rw_lock_write_unlock(&sCacheListLock);

#if VM_CACHE_TRACING >= 2
	for (VMCachePagesTree::Iterator it = pages.GetIterator();
			vm_page* page = it.Next();) {
		T2(RemovePage(fromCache, page));
		T2(InsertPage(this, page, page->cache_offset << PAGE_SHIFT));
	}
#endif
}


/*!	Waits until one or more events happened for a given page which belongs to
	this cache.
	The cache must be locked. It will be unlocked by the method. \a relock
	specifies whether the method shall re-lock the cache before returning.
	\param page The page for which to wait.
	\param events The mask of events the caller is interested in.
	\param relock If \c true, the cache will be locked when returning,
		otherwise it won't be locked.
*/
/**
 * @brief Blocks the calling thread until one or more page events occur.
 *
 * Registers the current thread as a waiter for the specified events on
 * @a page, then releases the cache lock and blocks. The cache is optionally
 * re-locked before returning.
 *
 * @param page   The page to wait on; must belong to this cache.
 * @param events Bitmask of PAGE_EVENT_* flags the caller is interested in.
 * @param relock If true, the cache lock is re-acquired before returning.
 * @note The cache must be locked when this function is called. The lock is
 *       always released internally; if @a relock is false the caller must not
 *       assume the lock is held on return.
 */
void
VMCache::WaitForPageEvents(vm_page* page, uint32 events, bool relock)
{
	PageEventWaiter waiter;
	waiter.thread = thread_get_current_thread();
	waiter.next = fPageEventWaiters;
	waiter.page = page;
	waiter.events = events;

	fPageEventWaiters = &waiter;

	thread_prepare_to_block(waiter.thread, 0, THREAD_BLOCK_TYPE_OTHER_OBJECT, page);

	Unlock();
	thread_block();

	if (relock)
		Lock();
}


/*!	Makes this cache the source of the \a consumer cache,
	and adds the \a consumer to its list.
	This also grabs a reference to the source cache.
	Assumes you have the cache and the consumer's lock held.
*/
/**
 * @brief Registers @a consumer as a consumer of this cache (copy-on-write source link).
 *
 * Sets consumer->source to this cache, appends the consumer to the consumers
 * list, and increments both the reference count and the store reference.
 *
 * @param consumer The cache that will read pages from this one on fault.
 * @note Both this cache and @a consumer must be locked. @a consumer must not
 *       already have a source.
 */
void
VMCache::AddConsumer(VMCache* consumer)
{
	TRACE(("add consumer vm cache %p to cache %p\n", consumer, this));
	T(AddConsumer(this, consumer));

	AssertLocked();
	consumer->AssertLocked();
	ASSERT(consumer->source == NULL);

	consumer->source = this;
	consumers.Add(consumer);

	AcquireRefLocked();
	AcquireStoreRef();
}


/*!	Adds the \a area to this cache.
	Assumes you have the locked the cache.
*/
/**
 * @brief Inserts a VMArea into this cache's area list.
 *
 * Appends @a area to the areas list and acquires a store reference.
 *
 * @param area The area to associate with this cache.
 * @note The cache lock must be held.
 */
void
VMCache::InsertAreaLocked(VMArea* area)
{
	TRACE(("VMCache::InsertAreaLocked(cache %p, area %p)\n", this, area));
	T(InsertArea(this, area));
	AssertLocked();

	areas.Insert(area, false);

	AcquireStoreRef();
}


/**
 * @brief Removes a VMArea from this cache's area list.
 *
 * Releases the store reference first (to preserve correct locking order with
 * the VFS), then acquires the cache lock and removes @a area from the list.
 *
 * @param area The area to remove; must currently belong to this cache.
 */
void
VMCache::RemoveArea(VMArea* area)
{
	TRACE(("VMCache::RemoveArea(cache %p, area %p)\n", this, area));

	T(RemoveArea(this, area));

	// We release the store reference first, since otherwise we would reverse
	// the locking order or even deadlock ourselves (... -> free_vnode() -> ...
	// -> bfs_remove_vnode() -> ... -> file_cache_set_size() -> mutex_lock()).
	// Also cf. _RemoveConsumer().
	ReleaseStoreRef();

	AutoLocker<VMCache> locker(this);

	areas.Remove(area);
}


/*!	Takes the areas from \a fromCache to this cache. This cache must not
	have areas yet. Both caches must be locked.
*/
/**
 * @brief Transfers all VMArea objects from @a fromCache into this cache.
 *
 * Updates each area's cache pointer, adjusts reference counts on both caches,
 * and emits tracing events. This cache must have no existing areas.
 *
 * @param fromCache The source cache whose areas will be moved here.
 * @note Both caches must be locked. This cache must be empty of areas.
 */
void
VMCache::TakeAreasFrom(VMCache* fromCache)
{
	AssertLocked();
	fromCache->AssertLocked();
	ASSERT(areas.IsEmpty());

	areas.TakeFrom(&fromCache->areas);

	for (VMArea* area = areas.First(); area != NULL; area = areas.GetNext(area)) {
		area->cache = this;
		AcquireRefLocked();
		fromCache->ReleaseRefLocked();

		T(RemoveArea(fromCache, area));
		T(InsertArea(this, area));
	}
}


/**
 * @brief Counts the number of areas mapped with write access, optionally ignoring one.
 *
 * @param ignoreArea An area to exclude from the count, or NULL to count all.
 * @return Number of areas that have B_WRITE_AREA or B_KERNEL_WRITE_AREA set.
 */
uint32
VMCache::CountWritableAreas(VMArea* ignoreArea) const
{
	uint32 count = 0;

	for (VMArea* area = areas.First(); area != NULL; area = areas.GetNext(area)) {
		if (area != ignoreArea
			&& (area->protection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) != 0) {
			count++;
		}
	}

	return count;
}


/**
 * @brief Writes all dirty pages in this cache back to the backing store.
 *
 * No-ops immediately for temporary caches. Otherwise acquires the cache lock,
 * calls vm_page_write_modified_pages(), and releases the lock.
 *
 * @retval B_OK All modified pages were written successfully.
 * @return Any error code propagated from vm_page_write_modified_pages().
 */
status_t
VMCache::WriteModified()
{
	TRACE(("VMCache::WriteModified(cache = %p)\n", this));

	if (temporary)
		return B_OK;

	Lock();
	status_t status = vm_page_write_modified_pages(this);
	Unlock();

	return status;
}


/*!	Commits the memory to the store if the \a commitment is larger than
	what's committed already.
	Assumes you have the cache's lock held.
*/
/**
 * @brief Ensures that at least @a commitment bytes are committed in the backing store.
 *
 * If the current Commitment() is already at least @a commitment, this is a
 * no-op. Otherwise Commit() is called to satisfy the new requirement.
 *
 * @param commitment Minimum number of bytes that must be committed.
 * @param priority   Allocation priority passed to Commit().
 * @retval B_OK Commitment is satisfied.
 * @return Any error code returned by Commit().
 * @note The cache lock must be held.
 */
status_t
VMCache::SetMinimalCommitment(off_t commitment, int priority)
{
	TRACE(("VMCache::SetMinimalCommitment(cache %p, commitment %" B_PRIdOFF
		")\n", this, commitment));
	T(SetMinimalCommitment(this, commitment));

	status_t status = B_OK;

	// If we don't have enough committed space to cover through to the new end
	// of the area...
	if (Commitment() < commitment) {
#if KDEBUG
		const off_t size = PAGE_ALIGN(virtual_end - virtual_base);
		ASSERT_PRINT(commitment <= size, "cache %p, commitment %" B_PRIdOFF ", size %" B_PRIdOFF,
			this, commitment, size);
#endif

		// try to commit more memory
		status = Commit(commitment, priority);
	}

	return status;
}


/**
 * @brief Frees pages in a range of the page tree, handling busy pages.
 *
 * Iterates from the iterator position forward, unmapping and freeing each
 * page. Busy non-IO pages cause the method to wait and return true, signalling
 * the caller to restart. Busy IO pages are marked so the IO subsystem will
 * free them later.
 *
 * @param it        Iterator pointing to the first candidate page.
 * @param toPage    If non-NULL, upper bound (exclusive) page number to free.
 * @param freedPages If non-NULL, incremented for each page freed.
 * @return true if iteration was interrupted by a busy page (caller should retry),
 *         false when the range was fully processed.
 * @note The cache lock must be held; it may be temporarily released while
 *       waiting for a busy page.
 */
bool
VMCache::_FreePageRange(VMCachePagesTree::Iterator it,
	page_num_t* toPage = NULL, page_num_t* freedPages = NULL)
{
	for (vm_page* page = it.Next();
		page != NULL && (toPage == NULL || page->cache_offset < *toPage);
		page = it.Next()) {

		if (page->busy) {
			if (!page->busy_io) {
				// wait for page to become unbusy
				WaitForPageEvents(page, PAGE_EVENT_NOT_BUSY, true);
				return true;
			}

			// We cannot wait for the page to become available
			// as we might cause a deadlock that way
			page->busy_io = false;
				// this will notify the reader/writer to free the page
		}

		ASSERT(page->WiredCount() == 0);
			// TODO: Find a real solution! If the page is wired
			// temporarily (e.g. by lock_memory()), we actually must not
			// unmap it!

		// remove the page and put it into the free queue
		DEBUG_PAGE_ACCESS_START(page);
		vm_remove_all_page_mappings(page);

		RemovePage(page);
			// Note: When iterating through a IteratableSplayTree
			// removing the current node is safe.

		if (page->busy) {
			fRemovedBusyPages.Add(page);
			DEBUG_PAGE_ACCESS_END(page);
		} else {
			vm_page_free(this, page);
		}

		if (freedPages != NULL)
			(*freedPages)++;
	}

	return false;
}


/*!	This function updates the size field of the cache.
	If needed, it will free up all pages that don't belong to the cache anymore.
	The cache lock must be held when you call it.
	Since removed pages don't belong to the cache any longer, they are not
	written back before they will be removed.

	Note, this function may temporarily release the cache lock in case it
	has to wait for busy pages.
*/
/**
 * @brief Resizes the cache's virtual address range to @a newSize bytes.
 *
 * Removes any pages that fall outside the new range, optionally zeroes the
 * partial page at the new end, and calls Commit() to adjust the backing store.
 * virtual_end is updated to @a newSize on success.
 *
 * @param newSize  New size of the cache in bytes (new virtual_end value).
 * @param priority Allocation priority for Commit(), or a negative value to
 *                 skip the Commit() call entirely.
 * @retval B_OK Resize succeeded.
 * @return Any error code from Commit().
 * @note The cache lock must be held. The lock may be temporarily released
 *       while waiting for busy pages.
 */
status_t
VMCache::Resize(off_t newSize, int priority)
{
	TRACE(("VMCache::Resize(cache %p, newSize %" B_PRIdOFF ") old size %"
		B_PRIdOFF "\n", this, newSize, this->virtual_end));
	T(Resize(this, newSize));

	AssertLocked();

	page_num_t oldPageCount = (page_num_t)((virtual_end + B_PAGE_SIZE - 1)
		>> PAGE_SHIFT);
	page_num_t newPageCount = (page_num_t)((newSize + B_PAGE_SIZE - 1)
		>> PAGE_SHIFT);

	if (newPageCount < oldPageCount) {
		// Remove all pages in the cache outside of the new virtual size.
		while (_FreePageRange(pages.GetIterator(newPageCount, true, true)))
			;
	}
	if (newSize < virtual_end && newPageCount > 0) {
		// We may have a partial page at the end of the cache that must be cleared.
		uint32 partialBytes = newSize % B_PAGE_SIZE;
		if (partialBytes != 0) {
			vm_page* page = LookupPage(newSize - partialBytes);
			if (page != NULL) {
				vm_memset_physical(page->physical_page_number * B_PAGE_SIZE
					+ partialBytes, 0, B_PAGE_SIZE - partialBytes);
			}
		}
	}

	if (priority >= 0) {
		status_t status = Commit(PAGE_ALIGN(newSize - virtual_base), priority);
		if (status != B_OK)
			return status;
	}

	virtual_end = newSize;
	return B_OK;
}

/*!	This function updates the virtual_base field of the cache.
	If needed, it will free up all pages that don't belong to the cache anymore.
	The cache lock must be held when you call it.
	Since removed pages don't belong to the cache any longer, they are not
	written back before they will be removed.

	Note, this function may temporarily release the cache lock in case it
	has to wait for busy pages.
*/
/**
 * @brief Moves the cache's virtual base address forward to @a newBase.
 *
 * Removes any pages below @a newBase, calls Commit() to adjust the backing
 * store, and updates virtual_base.
 *
 * @param newBase  New base address in bytes; must be >= current virtual_base.
 * @param priority Allocation priority for Commit(), or negative to skip it.
 * @retval B_OK Rebase succeeded.
 * @return Any error code from Commit().
 * @note The cache lock must be held. The lock may be temporarily released
 *       while waiting for busy pages.
 */
status_t
VMCache::Rebase(off_t newBase, int priority)
{
	TRACE(("VMCache::Rebase(cache %p, newBase %lld) old base %lld\n",
		this, newBase, this->virtual_base));
	T(Rebase(this, newBase));

	AssertLocked();

	page_num_t basePage = (page_num_t)(newBase >> PAGE_SHIFT);

	if (newBase > virtual_base) {
		// Remove all pages in the cache outside of the new virtual base.
		while (_FreePageRange(pages.GetIterator(), &basePage))
			;
	}

	if (priority >= 0) {
		status_t status = Commit(PAGE_ALIGN(virtual_end - newBase), priority);
		if (status != B_OK)
			return status;
	}

	virtual_base = newBase;
	return B_OK;
}


/*!	Moves pages in the given range from the source cache into this cache. Both
	caches must be locked.
*/
/**
 * @brief Adopts pages from @a source in the specified byte range, remapping offsets.
 *
 * Pages in [@a offset, @a offset + @a size) are moved from @a source into
 * this cache. Each page's new offset is computed as
 * (old_offset + newOffset - offset).
 *
 * @param source    Cache from which to adopt pages.
 * @param offset    Starting byte offset in @a source.
 * @param size      Number of bytes to adopt.
 * @param newOffset Corresponding starting offset in this cache.
 * @retval B_OK Always returns B_OK.
 * @note Both caches must be locked.
 */
status_t
VMCache::Adopt(VMCache* source, off_t offset, off_t size, off_t newOffset)
{
	page_num_t startPage = offset >> PAGE_SHIFT;
	page_num_t endPage = (offset + size + B_PAGE_SIZE - 1) >> PAGE_SHIFT;
	off_t offsetChange = newOffset - offset;

	VMCachePagesTree::Iterator it = source->pages.GetIterator(startPage, true,
		true);
	for (vm_page* page = it.Next();
				page != NULL && page->cache_offset < endPage;
				page = it.Next()) {
		MovePage(page, (page->cache_offset << PAGE_SHIFT) + offsetChange);
	}

	return B_OK;
}


/*! Discards pages in the given range. */
/**
 * @brief Discards (frees without writing back) all pages in a byte range.
 *
 * @param offset Starting byte offset within the cache.
 * @param size   Number of bytes in the range to discard.
 * @return Number of bytes actually discarded (multiple of B_PAGE_SIZE).
 * @note The cache must be locked by the caller.
 */
ssize_t
VMCache::Discard(off_t offset, off_t size)
{
	page_num_t discarded = 0;
	page_num_t startPage = offset >> PAGE_SHIFT;
	page_num_t endPage = (offset + size + B_PAGE_SIZE - 1) >> PAGE_SHIFT;
	while (_FreePageRange(pages.GetIterator(startPage, true, true), &endPage, &discarded))
		;

	return (discarded * B_PAGE_SIZE);
}


/*!	You have to call this function with the VMCache lock held. */
/**
 * @brief Writes back all modified pages and then removes every page from the cache.
 *
 * Loops until page_count reaches zero: first writing modified pages via
 * vm_page_write_modified_pages(), then iterating the tree and freeing each
 * non-busy, non-mapped page. Returns B_BUSY if a mapped page is encountered.
 *
 * @retval B_OK    All pages were successfully flushed and removed.
 * @retval B_BUSY  A mapped page was found; caller should retry later.
 * @return Any error from vm_page_write_modified_pages().
 * @note The cache lock must be held throughout; it may be released temporarily
 *       while waiting for busy pages.
 */
status_t
VMCache::FlushAndRemoveAllPages()
{
	ASSERT_LOCKED_MUTEX(&fLock);

	while (page_count > 0) {
		// write back modified pages
		status_t status = vm_page_write_modified_pages(this);
		if (status != B_OK)
			return status;

		// remove pages
		for (VMCachePagesTree::Iterator it = pages.GetIterator();
				vm_page* page = it.Next();) {
			if (page->busy) {
				// wait for page to become unbusy
				WaitForPageEvents(page, PAGE_EVENT_NOT_BUSY, true);

				// restart from the start of the list
				it = pages.GetIterator();
				continue;
			}

			// skip modified pages -- they will be written back in the next
			// iteration
			if (page->State() == PAGE_STATE_MODIFIED)
				continue;

			// We can't remove mapped pages.
			if (page->IsMapped())
				return B_BUSY;

			DEBUG_PAGE_ACCESS_START(page);
			RemovePage(page);
			vm_page_free(this, page);
				// Note: When iterating through a IteratableSplayTree
				// removing the current node is safe.
		}
	}

	return B_OK;
}


/**
 * @brief Returns the number of bytes currently committed in the backing store.
 *
 * The base implementation always returns 0; subclasses with real backing
 * stores override this.
 *
 * @return Committed size in bytes.
 */
off_t
VMCache::Commitment() const
{
	return 0;
}


/**
 * @brief Returns whether this cache type supports overcommit.
 *
 * The base implementation returns false; anonymous caches may override this.
 *
 * @return true if the cache allows overcommitting physical memory.
 */
bool
VMCache::CanOvercommit()
{
	return false;
}


/**
 * @brief Commits @a size bytes of backing store for this cache.
 *
 * The base implementation panics — concrete subclasses must override this if
 * they support committing memory.
 *
 * @param size     Number of bytes to commit.
 * @param priority Allocation priority hint.
 * @retval B_NOT_SUPPORTED Always (base class is not supposed to be called).
 */
status_t
VMCache::Commit(off_t size, int priority)
{
	ASSERT_UNREACHABLE();
	return B_NOT_SUPPORTED;
}


/**
 * @brief Transfers committed memory accounting from another cache to this one.
 *
 * The base implementation panics — concrete subclasses must override this
 * when they track commitment separately.
 *
 * @param from       The cache from which to transfer commitment.
 * @param commitment Number of bytes of commitment to transfer.
 */
void
VMCache::TakeCommitmentFrom(VMCache* from, off_t commitment)
{
	ASSERT_UNREACHABLE();
}


/*!	Returns whether the cache's underlying backing store could deliver the
	page at the given offset.

	Basically it returns whether a Read() at \a offset would at least read a
	partial page (assuming that no unexpected errors occur or the situation
	changes in the meantime).
*/
/**
 * @brief Returns whether the backing store has data for the page at @a offset.
 *
 * Used during fault handling to determine whether a Read() would be useful
 * before allocating a zero page. The base implementation always returns false
 * because VMCache has no backing store.
 *
 * @param offset Byte offset within the cache (page-aligned).
 * @return true if a Read() at @a offset would supply at least a partial page.
 */
bool
VMCache::StoreHasPage(off_t offset)
{
	// In accordance with Fault() the default implementation doesn't have a
	// backing store and doesn't allow faults.
	return false;
}


/**
 * @brief Reads pages from the backing store into caller-supplied I/O vectors.
 *
 * The base implementation always returns B_ERROR; file-backed and device
 * caches override this to perform the actual I/O.
 *
 * @param offset    Byte offset within the cache to start reading from.
 * @param vecs      Array of generic_io_vec descriptors for the destination buffers.
 * @param count     Number of entries in @a vecs.
 * @param flags     I/O flags (e.g. B_PHYSICAL_IO_REQUEST).
 * @param _numBytes In/out: requested byte count on entry, actual bytes read on exit.
 * @retval B_ERROR  Base class — no backing store.
 */
status_t
VMCache::Read(off_t offset, const generic_io_vec *vecs, size_t count,
	uint32 flags, generic_size_t *_numBytes)
{
	return B_ERROR;
}


/**
 * @brief Writes pages from caller-supplied I/O vectors to the backing store.
 *
 * The base implementation always returns B_ERROR; file-backed and device
 * caches override this to perform the actual I/O.
 *
 * @param offset    Byte offset within the cache to start writing to.
 * @param vecs      Array of generic_io_vec descriptors for the source buffers.
 * @param count     Number of entries in @a vecs.
 * @param flags     I/O flags (e.g. B_PHYSICAL_IO_REQUEST).
 * @param _numBytes In/out: requested byte count on entry, actual bytes written on exit.
 * @retval B_ERROR  Base class — no backing store.
 */
status_t
VMCache::Write(off_t offset, const generic_io_vec *vecs, size_t count,
	uint32 flags, generic_size_t *_numBytes)
{
	return B_ERROR;
}


/**
 * @brief Initiates an asynchronous write, falling back to synchronous Write().
 *
 * Subclasses that support true async I/O should override this. The default
 * implementation calls Write() synchronously and then invokes the callback.
 *
 * @param offset    Byte offset within the cache to write to.
 * @param vecs      Source I/O vectors.
 * @param count     Number of vectors.
 * @param numBytes  Total byte count to write.
 * @param flags     I/O flags.
 * @param callback  Called with the result when I/O completes; may be NULL.
 * @return Status code from the underlying Write() call.
 */
status_t
VMCache::WriteAsync(off_t offset, const generic_io_vec* vecs, size_t count,
	generic_size_t numBytes, uint32 flags, AsyncIOCallback* callback)
{
	// Not supported, fall back to the synchronous hook.
	generic_size_t transferred = numBytes;
	status_t error = Write(offset, vecs, count, flags, &transferred);

	if (callback != NULL)
		callback->IOFinished(error, transferred != numBytes, transferred);

	return error;
}


/*!	\brief Returns whether the cache can write the page at the given offset.

	The cache must be locked when this function is invoked.

	@param offset The page offset.
	@return \c true, if the page can be written, \c false otherwise.
*/
/**
 * @brief Returns whether a dirty page at @a offset can be written to the backing store.
 *
 * The base implementation returns false; writable caches override this.
 *
 * @param offset Byte offset of the page within the cache.
 * @return true if the page at @a offset may be written back.
 * @note The cache lock must be held.
 */
bool
VMCache::CanWritePage(off_t offset)
{
	return false;
}


/**
 * @brief Handles a page fault for a missing page at @a offset.
 *
 * Called by the VM fault handler when a page is not present in this cache.
 * The base implementation returns B_BAD_ADDRESS because VMCache has no
 * backing store; subclasses (file, anonymous) override this to supply pages.
 *
 * @param aspace  The address space in which the fault occurred.
 * @param offset  Byte offset within the cache of the missing page.
 * @retval B_BAD_ADDRESS Base class — fault cannot be satisfied.
 */
status_t
VMCache::Fault(struct VMAddressSpace *aspace, off_t offset)
{
	return B_BAD_ADDRESS;
}


/**
 * @brief Merges pages from @a source into this cache (COW collapse optimisation).
 *
 * Iterates all pages in @a source that fall within this cache's virtual range.
 * For each page that does not already exist in this cache, the page is moved
 * up from @a source into this cache. Pages already present in this cache take
 * precedence and the source's copy is left untouched.
 *
 * @param source The source (lower-level) cache to merge pages from.
 * @note Both this cache and @a source must be locked.
 */
void
VMCache::Merge(VMCache* source)
{
	const page_num_t firstOffset = ROUNDDOWN(virtual_base, B_PAGE_SIZE) >> PAGE_SHIFT,
		endOffset = (page_num_t)((virtual_end + B_PAGE_SIZE - 1) >> PAGE_SHIFT);

	VMCachePagesTree::Iterator it = source->pages.GetIterator();
	while (vm_page* page = it.Next()) {
		if (page->cache_offset < firstOffset || page->cache_offset >= endOffset)
			continue;

		// Note: Removing the current node while iterating through a
		// IteratableSplayTree is safe.
		vm_page* consumerPage = LookupPage(
			(off_t)page->cache_offset << PAGE_SHIFT);
		if (consumerPage == NULL) {
			// the page is not yet in the consumer cache - move it upwards
			MovePage(page);
		}
	}
}


/**
 * @brief Acquires a store reference without holding the cache lock.
 *
 * Used during cache construction before the cache is reachable by other
 * threads. The base implementation always returns B_ERROR; subclasses with
 * reference-counted backing stores must override this.
 *
 * @retval B_ERROR Base class — no backing store reference semantics.
 */
status_t
VMCache::AcquireUnreferencedStoreRef()
{
	return B_ERROR;
}


/**
 * @brief Acquires a reference to the backing store.
 *
 * The base implementation is a no-op; file-backed caches (e.g. VMVnodeCache)
 * override this to increment the vnode reference.
 */
void
VMCache::AcquireStoreRef()
{
}


/**
 * @brief Releases a previously acquired reference to the backing store.
 *
 * The base implementation is a no-op; file-backed caches override this to
 * decrement the vnode reference.
 */
void
VMCache::ReleaseStoreRef()
{
}


/*!	Kernel debugger version of StoreHasPage().
	Does not do any locking.
*/
/**
 * @brief Kernel-debugger-safe version of StoreHasPage() — no locking.
 *
 * Calls StoreHasPage() directly. Safe to call from the kernel debugger
 * provided the subclass implementation does not acquire locks.
 *
 * @param offset Byte offset within the cache to query.
 * @return true if the backing store has data at @a offset.
 */
bool
VMCache::DebugStoreHasPage(off_t offset)
{
	// default that works for all subclasses that don't lock anyway
	return StoreHasPage(offset);
}


/*!	Kernel debugger version of LookupPage().
	Does not do any locking.
*/
/**
 * @brief Kernel-debugger-safe page lookup — no locking.
 *
 * Performs a raw splay-tree lookup without acquiring the cache lock. Safe to
 * call from the kernel debugger where locking is not possible.
 *
 * @param offset Byte offset within the cache (page-aligned).
 * @return Pointer to the vm_page, or NULL if not present.
 */
vm_page*
VMCache::DebugLookupPage(off_t offset)
{
	return pages.Lookup((page_num_t)(offset >> PAGE_SHIFT));
}


/**
 * @brief Prints a detailed description of the cache to the kernel debugger output.
 *
 * Prints reference count, source pointer, type, virtual range, temporariness,
 * lock state, all associated areas, all consumers, and optionally every page
 * in the page tree with its state.
 *
 * @param showPages If true, individual page entries are printed; otherwise
 *                  only the total page count is shown.
 */
void
VMCache::Dump(bool showPages) const
{
	kprintf("CACHE %p:\n", this);
	kprintf("  ref_count:    %" B_PRId32 "\n", RefCount());
	kprintf("  source:       %p\n", source);
	kprintf("  type:         %s\n", vm_cache_type_to_string(type));
	kprintf("  virtual_base: 0x%" B_PRIx64 "\n", virtual_base);
	kprintf("  virtual_end:  0x%" B_PRIx64 "\n", virtual_end);
	kprintf("  temporary:    %" B_PRIu32 "\n", uint32(temporary));
	kprintf("  lock:         %p\n", &fLock);
#if KDEBUG
	kprintf("  lock.holder:  %" B_PRId32 "\n", fLock.holder);
#endif
	kprintf("  areas:\n");

	for (VMArea* area = areas.First(); area != NULL; area = areas.GetNext(area)) {
		kprintf("    area 0x%" B_PRIx32 ", %s\n", area->id, area->name);
		kprintf("\tbase_addr:  0x%lx, size: 0x%lx\n", area->Base(),
			area->Size());
		kprintf("\tprotection: 0x%" B_PRIx32 "\n", area->protection);
		kprintf("\towner:      0x%" B_PRIx32 "\n", area->address_space->ID());
	}

	kprintf("  consumers:\n");
	for (ConsumerList::ConstIterator it = consumers.GetIterator();
		 	VMCache* consumer = it.Next();) {
		kprintf("\t%p\n", consumer);
	}

	kprintf("  pages:\n");
	if (showPages) {
		for (VMCachePagesTree::ConstIterator it = pages.GetIterator();
				vm_page* page = it.Next();) {
			if (!vm_page_is_dummy(page)) {
				kprintf("\t%p ppn %#" B_PRIxPHYSADDR " offset %#" B_PRIxPHYSADDR
					" state %u (%s) wired_count %u\n", page,
					page->physical_page_number, page->cache_offset,
					page->State(), page_state_to_string(page->State()),
					page->WiredCount());
			} else {
				kprintf("\t%p DUMMY PAGE state %u (%s)\n",
					page, page->State(), page_state_to_string(page->State()));
			}
		}
	} else
		kprintf("\t%" B_PRIu32 " in cache\n", page_count);
}


/*!	Wakes up threads waiting for page events.
	\param page The page for which events occurred.
	\param events The mask of events that occurred.
*/
/**
 * @brief Wakes all threads waiting for the specified page events.
 *
 * Walks the fPageEventWaiters list, unblocks any waiter whose page and event
 * mask intersect with @a page and @a events, and removes it from the list.
 *
 * @param page   The page for which events occurred.
 * @param events Bitmask of PAGE_EVENT_* flags that have occurred.
 * @note The cache lock must be held.
 */
void
VMCache::_NotifyPageEvents(vm_page* page, uint32 events)
{
	PageEventWaiter** it = &fPageEventWaiters;
	while (PageEventWaiter* waiter = *it) {
		if (waiter->page == page && (waiter->events & events) != 0) {
			// remove from list and unblock
			*it = waiter->next;
			thread_unblock(waiter->thread, B_OK);
		} else
			it = &waiter->next;
	}
}


/*!	Merges the given cache with its only consumer.
	The caller must hold both the cache's and the consumer's lock. The method
	does release neither lock.
*/
/**
 * @brief Merges this cache into its sole consumer and detaches from the source chain.
 *
 * Removes the consumer from the consumers list, calls consumer->Merge(this)
 * to pull up pages, re-wires the source link so the consumer points directly
 * to this cache's source, and releases this cache's reference count.
 *
 * @note Both this cache's lock and the consumer's lock must be held. Neither
 *       lock is released by this method.
 */
void
VMCache::_MergeWithOnlyConsumer()
{
	VMCache* consumer = consumers.RemoveHead();

	TRACE(("merge vm cache %p (ref == %" B_PRId32 ") with vm cache %p\n",
		this, this->fRefCount, consumer));

	T(Merge(this, consumer));

	// merge the cache
	consumer->Merge(this);

	// The remaining consumer has got a new source.
	if (source != NULL) {
		VMCache* newSource = source;

		newSource->Lock();

		newSource->consumers.Remove(this);
		newSource->consumers.Add(consumer);
		consumer->source = newSource;
		source = NULL;

		newSource->Unlock();
	} else
		consumer->source = NULL;

	// Release the reference the cache's consumer owned. The consumer takes
	// over the cache's ref to its source (if any) instead.
	ReleaseRefLocked();
}


/*!	Removes the \a consumer from this cache.
	It will also release the reference to the cache owned by the consumer.
	Assumes you have the consumer's cache lock held. This cache must not be
	locked.
*/
/**
 * @brief Removes @a consumer from this cache's consumer list and releases its reference.
 *
 * Locks this cache, removes @a consumer from the consumers list, clears
 * consumer->source, then releases the store reference and the ref count
 * contributed by @a consumer.
 *
 * @param consumer The consumer cache to remove; its lock must be held by
 *                 the caller. This cache must NOT be locked when called.
 */
void
VMCache::_RemoveConsumer(VMCache* consumer)
{
	TRACE(("remove consumer vm cache %p from cache %p\n", consumer, this));
	T(RemoveConsumer(this, consumer));

	consumer->AssertLocked();

	// Remove the consumer from the cache, but keep its reference until the end.
	Lock();
	consumers.Remove(consumer);
	consumer->source = NULL;
	Unlock();

	// Release the store ref without holding the cache lock, as calling into
	// the VFS while holding the cache lock would reverse the usual locking order.
	ReleaseStoreRef();

	// Now release the consumer's reference.
	ReleaseRef();
}


// #pragma mark - VMCacheFactory
	// TODO: Move to own source file!


/**
 * @brief Factory method that creates an anonymous (swap-backed or no-swap) cache.
 *
 * Selects VMAnonymousCache when swap support is enabled and @a swappable is
 * true; otherwise creates a VMAnonymousNoSwapCache.
 *
 * @param[out] _cache               Receives the newly created cache on success.
 * @param      canOvercommit        Whether the cache may overcommit physical memory.
 * @param      numPrecommittedPages Number of pages to pre-commit at creation time.
 * @param      numGuardPages        Number of guard pages to reserve.
 * @param      swappable            If true (and swap support is compiled in), use
 *                                  a swap-backed anonymous cache.
 * @param      priority             Allocation priority (e.g. VM_PRIORITY_USER).
 * @retval B_OK        Cache created successfully.
 * @retval B_NO_MEMORY Allocation of the cache object failed.
 * @return Any error code propagated from the cache's Init() method.
 */
/*static*/ status_t
VMCacheFactory::CreateAnonymousCache(VMCache*& _cache, bool canOvercommit,
	int32 numPrecommittedPages, int32 numGuardPages, bool swappable,
	int priority)
{
	uint32 allocationFlags = HEAP_DONT_WAIT_FOR_MEMORY
		| HEAP_DONT_LOCK_KERNEL_SPACE;
	if (priority >= VM_PRIORITY_VIP)
		allocationFlags |= HEAP_PRIORITY_VIP;

#if ENABLE_SWAP_SUPPORT
	if (swappable) {
		VMAnonymousCache* cache
			= new(gAnonymousCacheObjectCache, allocationFlags) VMAnonymousCache;
		if (cache == NULL)
			return B_NO_MEMORY;

		status_t error = cache->Init(canOvercommit, numPrecommittedPages,
			numGuardPages, allocationFlags);
		if (error != B_OK) {
			cache->Delete();
			return error;
		}

		T(Create(cache));

		_cache = cache;
		return B_OK;
	}
#endif

	VMAnonymousNoSwapCache* cache
		= new(gAnonymousNoSwapCacheObjectCache, allocationFlags)
			VMAnonymousNoSwapCache;
	if (cache == NULL)
		return B_NO_MEMORY;

	status_t error = cache->Init(canOvercommit, numPrecommittedPages,
		numGuardPages, allocationFlags);
	if (error != B_OK) {
		cache->Delete();
		return error;
	}

	T(Create(cache));

	_cache = cache;
	return B_OK;
}


/**
 * @brief Factory method that creates a vnode-backed (file) cache.
 *
 * Allocates a VMVnodeCache and initialises it with the given vnode.
 *
 * @param[out] _cache  Receives the newly created cache on success.
 * @param      vnode   The vnode that provides the backing store for this cache.
 * @retval B_OK        Cache created successfully.
 * @retval B_NO_MEMORY Allocation failed.
 * @return Any error propagated from VMVnodeCache::Init().
 */
/*static*/ status_t
VMCacheFactory::CreateVnodeCache(VMCache*& _cache, struct vnode* vnode)
{
	const uint32 allocationFlags = HEAP_DONT_WAIT_FOR_MEMORY
		| HEAP_DONT_LOCK_KERNEL_SPACE;
		// Note: Vnode cache creation is never VIP.

	VMVnodeCache* cache
		= new(gVnodeCacheObjectCache, allocationFlags) VMVnodeCache;
	if (cache == NULL)
		return B_NO_MEMORY;

	status_t error = cache->Init(vnode, allocationFlags);
	if (error != B_OK) {
		cache->Delete();
		return error;
	}

	T(Create(cache));

	_cache = cache;
	return B_OK;
}


/**
 * @brief Factory method that creates a device-mapped cache.
 *
 * Allocates a VMDeviceCache backed by a physical device at @a baseAddress.
 *
 * @param[out] _cache       Receives the newly created cache on success.
 * @param      baseAddress  Physical base address of the device memory region.
 * @retval B_OK        Cache created successfully.
 * @retval B_NO_MEMORY Allocation failed.
 * @return Any error propagated from VMDeviceCache::Init().
 */
/*static*/ status_t
VMCacheFactory::CreateDeviceCache(VMCache*& _cache, addr_t baseAddress)
{
	const uint32 allocationFlags = HEAP_DONT_WAIT_FOR_MEMORY
		| HEAP_DONT_LOCK_KERNEL_SPACE;
		// Note: Device cache creation is never VIP.

	VMDeviceCache* cache
		= new(gDeviceCacheObjectCache, allocationFlags) VMDeviceCache;
	if (cache == NULL)
		return B_NO_MEMORY;

	status_t error = cache->Init(baseAddress, allocationFlags);
	if (error != B_OK) {
		cache->Delete();
		return error;
	}

	T(Create(cache));

	_cache = cache;
	return B_OK;
}


/**
 * @brief Factory method that creates a null (no backing store) cache.
 *
 * Allocates a VMNullCache, which satisfies faults with zero pages and has no
 * persistent backing store.
 *
 * @param      priority  Allocation priority (e.g. VM_PRIORITY_USER).
 * @param[out] _cache    Receives the newly created cache on success.
 * @retval B_OK        Cache created successfully.
 * @retval B_NO_MEMORY Allocation failed.
 * @return Any error propagated from VMNullCache::Init().
 */
/*static*/ status_t
VMCacheFactory::CreateNullCache(int priority, VMCache*& _cache)
{
	uint32 allocationFlags = HEAP_DONT_WAIT_FOR_MEMORY
		| HEAP_DONT_LOCK_KERNEL_SPACE;
	if (priority >= VM_PRIORITY_VIP)
		allocationFlags |= HEAP_PRIORITY_VIP;

	VMNullCache* cache
		= new(gNullCacheObjectCache, allocationFlags) VMNullCache;
	if (cache == NULL)
		return B_NO_MEMORY;

	status_t error = cache->Init(allocationFlags);
	if (error != B_OK) {
		cache->Delete();
		return error;
	}

	T(Create(cache));

	_cache = cache;
	return B_OK;
}
