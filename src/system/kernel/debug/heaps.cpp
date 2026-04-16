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
 *   Copyright 2026, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file heaps.cpp
 * @brief Front-end for the kernel's pluggable heap implementations.
 *
 * Selects, initialises and dispatches to one of the registered
 * kernel_heap_implementation back-ends (default, "guarded", "debug" or
 * "slab") based on the "kernel_malloc" safemode option. Exposes the C
 * library allocation surface (malloc/free/realloc/memalign/posix_memalign
 * and their *_etc variants) as thin wrappers that forward into the active
 * heap. When USE_DEBUG_HEAPS_FOR_ALL_OBJECT_CACHES is set, also provides a
 * minimal ObjectCache shim that funnels all slab-style allocations through
 * the same heap.
 *
 * @brief Locking / KDL caveats.
 *
 * The functions here themselves do no locking: callers reach these entry
 * points either via the normal kmalloc path (where the back-end heap does
 * its own locking) or from the kernel debugger. In KDL the heap may be mid
 * mutation and no locks are honoured, so dumping helpers and walkers
 * invoked from the debugger must tolerate a possibly inconsistent heap
 * state and must never try to acquire a semaphore or mutex.
 */

#include "kernel_debug_config.h"

#if DEBUG_HEAPS

#include <stdlib.h>
#include <ctype.h>

#include <heap.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <slab/Slab.h>
#include <safemode.h>

#include "heaps.h"


//#define TRACE_HEAPS
#ifdef TRACE_HEAPS
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


#define HEAP_SYMBOL_NAME(NAME) kernel_##NAME##_heap
#define HEAP_SYMBOL(NAME) HEAP_SYMBOL_NAME(NAME)
static kernel_heap_implementation* sActiveHeaps[2] = { &HEAP_SYMBOL(DEBUG_HEAPS_DEFAULT) };

#if GUARDED_HEAP_CAN_REPLACE_OBJECT_CACHES
struct CacheSelector {
	char name[32];
	bool globPrefix : 1;
	bool globSuffix : 1;
};
static CacheSelector* sGuardedHeapForObjectCaches = NULL;
static int32 sGuardedHeapForObjectCachesCount = 0;
#endif


//	#pragma mark -


/**
 * @brief Maps the initial virtual range for a heap and invokes its init().
 *
 * If the heap declares a non-zero initial_size, halves the request until it
 * fits within 1/8 of available physical memory (panicking under 1 MiB),
 * reserves that region via vm_allocate_early() with kernel R/W permissions
 * and then forwards (heapBase, heapSize) to the back-end's init() hook.
 * Assumes it runs early during boot with no other heap clients active.
 *
 * @param args Pointer to the kernel_args blob from the boot loader.
 * @param heap Heap implementation whose init() should be invoked.
 * @return B_OK on success or the error returned by heap->init().
 */
static status_t
init_heap(struct kernel_args* args, kernel_heap_implementation* heap)
{
	addr_t heapBase = 0;
	size_t heapSize = heap->initial_size;
	if (heapSize != 0) {
		// try to accomodate low memory systems
		while (heapSize > (vm_page_num_pages() * B_PAGE_SIZE) / 8)
			heapSize /= 2;
		if (heapSize < 1024 * 1024)
			panic("heap_init: go buy some RAM please.");

		// map in the new heap and initialize it
		heapBase = vm_allocate_early(args, heapSize, heapSize,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0);
		TRACE(("heap at 0x%lx\n", heapBase));
	}

	return heap->init(args, heapBase, heapSize);
}


#if GUARDED_HEAP_CAN_REPLACE_OBJECT_CACHES
/**
 * @brief Decides whether an object_cache should be routed to the guarded heap.
 *
 * Walks the selector list populated from the "guarded_heap_for_object_caches"
 * safemode option. Selectors may start and/or end with '*' for
 * prefix/suffix/substring matching against the cache name. When a match is
 * found a short dprintf() note is emitted for the boot log.
 *
 * @param name Object-cache name being evaluated.
 * @return true if a selector matches and the cache should use the guarded
 *         heap, false otherwise.
 */
bool
guarded_heap_replaces_object_cache(const char* name)
{
	if (sGuardedHeapForObjectCaches == NULL)
		return false;

	bool match = false;
	for (int32 i = 0; !match && i < sGuardedHeapForObjectCachesCount; i++) {
		CacheSelector& selector = sGuardedHeapForObjectCaches[i];
		if (selector.globPrefix && selector.globSuffix) {
			if (strstr(name, selector.name) != NULL)
				match = true;
		} else if (selector.globPrefix) {
			int32 nameLen = strlen(name), selectorLen = strlen(selector.name);
			if (strcmp(name + (nameLen - selectorLen), selector.name) == 0)
				match = true;
		} else if (selector.globSuffix) {
			if (strncmp(name, selector.name, strlen(selector.name)) == 0)
				match = true;
		} else {
			if (strcmp(name, selector.name) == 0)
				match = true;
		}
	}

	if (match)
		dprintf("using guarded heap for object_cache \"%s\"\n", name);
	return match;
}


/**
 * @brief Parses the "guarded_heap_for_object_caches" safemode option.
 *
 * Reads the option string, spins up the guarded heap as a secondary
 * back-end when it is not already the default, and splits the option into
 * comma-separated selectors. Each selector is copied into
 * sGuardedHeapForObjectCaches (allocated out of the guarded heap itself,
 * page-sized), tracking '*' wildcards at the beginning/end of each name.
 * Runs single-threaded during boot; no locking required.
 *
 * @param args Boot-time kernel arguments used to fetch the safemode value.
 * @return void.
 */
static void
init_object_cache_replacements(struct kernel_args* args)
{
	char buffer[1024];
	size_t bufferSize = sizeof(buffer);
	if (get_safemode_option_early(args, "guarded_heap_for_object_caches",
			buffer, &bufferSize) != B_OK)
		return;

	if (sActiveHeaps[0] != &kernel_guarded_heap) {
		// We need to initialize the guarded heap, too.
		sActiveHeaps[1] = &kernel_guarded_heap;
		init_heap(args, sActiveHeaps[1]);
	}

	sGuardedHeapForObjectCaches = (CacheSelector*)
		kernel_guarded_heap.memalign(0, B_PAGE_SIZE, 0);
	memset(sGuardedHeapForObjectCaches, 0, B_PAGE_SIZE);

	// Parse the option.
	const char* option = buffer;
	while (*option != '\0') {
		CacheSelector& selector
			= sGuardedHeapForObjectCaches[sGuardedHeapForObjectCachesCount++];

		size_t nameLength = 0;
		char quoteEnd = '\0';
		bool phraseEnd = false;
		bool seenGlob = false;
		while (!phraseEnd && nameLength < (sizeof(selector.name) - 1)) {
			if (quoteEnd == '\0' && isspace(*option)) {
				option++;
				continue;
			}

			switch (*option) {
				case '"':
				case '\'':
					if (quoteEnd == '\0')
						quoteEnd = *option;
					else if (quoteEnd == *option)
						quoteEnd = '\0';
					break;

				case '\0':
				case ',':
					phraseEnd = true;
					break;

				case '*':
					if (nameLength == 0)
						selector.globPrefix = true;
					else
						seenGlob = true;
					break;

				case '\\':
					option++;
					// fall through
				default:
					if (seenGlob) {
						dprintf("heap_init: error: unsupported glob pattern\n");
						seenGlob = false;
					}
					selector.name[nameLength++] = *option;
					break;
			}
			option++;
		}
		if (seenGlob)
			selector.globSuffix = true;
		if (!phraseEnd) {
			dprintf("heap_init: error: pattern overflow after '%s'\n", selector.name);
			continue;
		}
	}

	dprintf("guarded_heap_for_object_caches: loaded %" B_PRId32 " selectors\n",
		sGuardedHeapForObjectCachesCount);
}
#endif


/**
 * @brief Selects the primary heap back-end and performs first-stage init.
 *
 * Reads the "kernel_malloc" safemode option to pick between the default,
 * guarded, debug or slab heaps, panicking on unknown names or on builds
 * where the requested heap is unavailable. Logs the choice, calls
 * init_heap() for the selected back-end, and if guarded-heap object-cache
 * replacement is compiled in, invokes init_object_cache_replacements().
 *
 * @param args Boot kernel arguments.
 * @return B_OK on success or the error from init_heap() on failure.
 */
status_t
heap_init(struct kernel_args* args)
{
	char buffer[32];
	size_t bufferSize = sizeof(buffer);
	if (get_safemode_option_early(args, "kernel_malloc", buffer, &bufferSize) == B_OK) {
		if (strcmp(buffer, "guarded") == 0)
			sActiveHeaps[0] = &kernel_guarded_heap;
		else if (strcmp(buffer, "debug") == 0)
			sActiveHeaps[0] = &kernel_debug_heap;
#if !USE_DEBUG_HEAPS_FOR_ALL_OBJECT_CACHES
		else if (strcmp(buffer, "slab") == 0)
			sActiveHeaps[0] = &kernel_slab_heap;
#endif
		else
			panic("unknown or unavailable kernel heap '%s'!", buffer);
	}
	dprintf("kernel malloc: using %s\n", sActiveHeaps[0]->name);

	status_t status = init_heap(args, sActiveHeaps[0]);
	if (status != B_OK)
		return status;

#if GUARDED_HEAP_CAN_REPLACE_OBJECT_CACHES
	init_object_cache_replacements(args);
#endif

	return B_OK;
}


/**
 * @brief Second-phase heap init, run once the VM area subsystem is up.
 *
 * Iterates every registered heap back-end and invokes its init_post_area()
 * hook (if present), giving the heap a chance to create its own VM areas
 * and bookkeeping. Bails out on the first failing back-end.
 *
 * @return B_OK on success or the first non-B_OK status from a back-end.
 */
status_t
heap_init_post_area()
{
	for (size_t i = 0; i < B_COUNT_OF(sActiveHeaps); i++) {
		if (sActiveHeaps[i] == NULL || sActiveHeaps[i]->init_post_area == NULL)
			continue;

		status_t status = sActiveHeaps[i]->init_post_area();
		if (status != B_OK)
			return status;
	}
	return B_OK;
}


/**
 * @brief Third-phase heap init, run after semaphores become available.
 *
 * Calls init_post_sem() on each registered back-end so they can install
 * locks now that the semaphore allocator works. Stops on the first
 * failure. Do not call from KDL: the heap is expected to be consistent.
 *
 * @return B_OK on success or the first non-B_OK status from a back-end.
 */
status_t
heap_init_post_sem()
{
	for (size_t i = 0; i < B_COUNT_OF(sActiveHeaps); i++) {
		if (sActiveHeaps[i] == NULL || sActiveHeaps[i]->init_post_sem == NULL)
			continue;

		status_t status = sActiveHeaps[i]->init_post_sem();
		if (status != B_OK)
			return status;
	}
	return B_OK;
}


/**
 * @brief Final heap init, run after the threading subsystem is ready.
 *
 * Invokes init_post_thread() on each back-end so heaps can spawn their own
 * maintenance / reclamation threads.
 *
 * @return B_OK on success or the first non-B_OK status from a back-end.
 */
status_t
heap_init_post_thread()
{
	for (size_t i = 0; i < B_COUNT_OF(sActiveHeaps); i++) {
		if (sActiveHeaps[i] == NULL || sActiveHeaps[i]->init_post_thread == NULL)
			continue;

		status_t status = sActiveHeaps[i]->init_post_thread();
		if (status != B_OK)
			return status;
	}
	return B_OK;
}


/**
 * @brief Aligned allocation routed to the primary heap.
 *
 * @param alignment Required alignment in bytes (0 means default).
 * @param size Number of bytes to allocate.
 * @return Allocated pointer, or NULL on failure.
 */
void*
memalign(size_t alignment, size_t size)
{
	return sActiveHeaps[0]->memalign(alignment, size, 0);
}


/**
 * @brief Aligned allocation with caller-supplied flags.
 *
 * @param alignment Required alignment in bytes.
 * @param size Number of bytes to allocate.
 * @param flags Heap-specific flags (e.g. CANNOT_WAIT, DONT_WAIT_FOR_MEMORY).
 * @return Allocated pointer, or NULL on failure.
 */
void*
memalign_etc(size_t alignment, size_t size, uint32 flags)
{
	return sActiveHeaps[0]->memalign(alignment, size, flags);
}


/**
 * @brief Frees a previously allocated block, honouring the given flags.
 *
 * @param address Pointer returned by a previous heap allocation (NULL OK).
 * @param flags Heap-specific free flags.
 * @return void.
 */
void
free_etc(void* address, uint32 flags)
{
	return sActiveHeaps[0]->free(address, flags);
}


/**
 * @brief Standard free(3) routed to the primary heap.
 *
 * @param address Pointer to free (NULL is a no-op).
 * @return void.
 */
void
free(void *address)
{
	return sActiveHeaps[0]->free(address, 0);
}


/**
 * @brief Standard malloc(3) routed to the primary heap.
 *
 * @param size Number of bytes to allocate.
 * @return Allocated pointer, or NULL on failure.
 */
void*
malloc(size_t size)
{
	return sActiveHeaps[0]->memalign(0, size, 0);
}


/**
 * @brief Reallocation with caller-supplied flags.
 *
 * @param address Existing block (NULL behaves like malloc).
 * @param newSize Desired new size.
 * @param flags Heap-specific flags.
 * @return Pointer to the resized block or NULL on failure.
 */
void*
realloc_etc(void* address, size_t newSize, uint32 flags)
{
	return sActiveHeaps[0]->realloc(address, newSize, flags);
}


/**
 * @brief Standard realloc(3) routed to the primary heap.
 *
 * @param address Existing block (NULL behaves like malloc).
 * @param newSize Desired new size.
 * @return Pointer to the resized block or NULL on failure.
 */
void*
realloc(void *address, size_t newSize)
{
	return sActiveHeaps[0]->realloc(address, newSize, 0);
}


/**
 * @brief POSIX posix_memalign() bridging into the primary heap.
 *
 * Validates that alignment is a multiple of sizeof(void*) and that the
 * output pointer is non-NULL, then delegates to memalign().
 *
 * @param _pointer Out-parameter receiving the allocated pointer.
 * @param alignment Alignment in bytes (power of two, multiple of sizeof(void*)).
 * @param size Number of bytes to allocate.
 * @return 0 on success, B_BAD_VALUE on invalid arguments.
 */
extern "C" int
posix_memalign(void** _pointer, size_t alignment, size_t size)
{
	if ((alignment & (sizeof(void*) - 1)) != 0 || _pointer == NULL)
		return B_BAD_VALUE;

	*_pointer = sActiveHeaps[0]->memalign(alignment, size, 0);
	return 0;
}


#if USE_DEBUG_HEAPS_FOR_ALL_OBJECT_CACHES


// #pragma mark - Slab API


struct ObjectCache {
	size_t object_size;
	size_t alignment;

	void* cookie;
	object_cache_constructor constructor;
	object_cache_destructor destructor;
};


/**
 * @brief Minimal object_cache creator when slabs are disabled.
 *
 * Convenience wrapper that forwards to create_object_cache_etc() with all
 * optional parameters zeroed. Used by slab callers in DEBUG_HEAPS builds
 * that have no real slab allocator.
 *
 * @param name Debug name for the cache (ignored here).
 * @param object_size Size of a single object in bytes.
 * @param flags Slab flags (ignored here).
 * @return Newly allocated ObjectCache, or NULL on failure.
 */
object_cache*
create_object_cache(const char* name, size_t object_size, uint32 flags)
{
	return create_object_cache_etc(name, object_size, 0, 0, 0, 0, flags,
		NULL, NULL, NULL, NULL);
}


/**
 * @brief Full-form object_cache creator, heap-backed.
 *
 * Allocates a shim ObjectCache struct remembering object size, alignment,
 * constructor, destructor and cookie; actual allocations later go through
 * the primary heap rather than a real slab.
 *
 * @param unused Cache name (unused).
 * @param objectSize Size of each object.
 * @param alignment Required alignment of each object.
 * @param cookie Opaque value passed to constructor/destructor.
 * @param ctor Optional constructor invoked after allocation.
 * @param dtor Optional destructor invoked before free.
 * @return Newly allocated ObjectCache, or NULL on failure.
 */
object_cache*
create_object_cache_etc(const char*, size_t objectSize, size_t alignment, size_t, size_t,
	size_t, uint32, void* cookie, object_cache_constructor ctor, object_cache_destructor dtor,
	object_cache_reclaimer)
{
	ObjectCache* cache = new ObjectCache;
	if (cache == NULL)
		return NULL;

	cache->object_size = objectSize;
	cache->alignment = alignment;
	cache->cookie = cookie;
	cache->constructor = ctor;
	cache->destructor = dtor;
	return cache;
}


/**
 * @brief Destroys an object_cache shim.
 *
 * @param cache Cache previously returned by create_object_cache[_etc]().
 * @return void.
 */
void
delete_object_cache(object_cache* cache)
{
	delete cache;
}


/**
 * @brief No-op reserve hook for the shim implementation.
 *
 * @param cache Unused cache pointer.
 * @param objectCount Unused reservation request.
 * @return Always B_OK.
 */
status_t
object_cache_set_minimum_reserve(object_cache* cache, size_t objectCount)
{
	return B_OK;
}


/**
 * @brief Allocates and optionally constructs an object from the shim cache.
 *
 * Performs a heap memalign() for cache->object_size with cache->alignment
 * and, if the cache has a constructor, runs it on the fresh memory.
 *
 * @param cache Shim cache to allocate from.
 * @param flags Heap allocation flags.
 * @return Pointer to the allocated object, or NULL on failure.
 */
void*
object_cache_alloc(object_cache* cache, uint32 flags)
{
	void* object = sActiveHeaps[0]->memalign(cache->alignment, cache->object_size, flags);
	if (object == NULL)
		return NULL;

	if (cache->constructor != NULL)
		cache->constructor(cache->cookie, object);
	return object;
}


/**
 * @brief Runs the destructor (if any) and frees the object back to the heap.
 *
 * @param cache Shim cache the object came from.
 * @param object Object previously returned by object_cache_alloc().
 * @param flags Heap free flags.
 * @return void.
 */
void
object_cache_free(object_cache* cache, void* object, uint32 flags)
{
	if (cache->destructor != NULL)
		cache->destructor(cache->cookie, object);
	return sActiveHeaps[0]->free(object, flags);
}


/**
 * @brief No-op reservation in the shim object_cache implementation.
 *
 * @param cache Unused.
 * @param objectCount Unused.
 * @param flags Unused.
 * @return Always B_OK.
 */
status_t
object_cache_reserve(object_cache* cache, size_t objectCount, uint32 flags)
{
	return B_OK;
}


/**
 * @brief Reports memory used by the shim cache (always zero here).
 *
 * @param cache Unused cache pointer.
 * @param _allocatedMemory Out-parameter, set to zero.
 * @return void.
 */
void
object_cache_get_usage(object_cache* cache, size_t* _allocatedMemory)
{
	*_allocatedMemory = 0;
}


/**
 * @brief No-op maintenance request when there is no slab allocator to poke.
 *
 * @return void.
 */
void
request_memory_manager_maintenance()
{
}


/**
 * @brief No-op slab allocator init for DEBUG_HEAPS builds.
 *
 * @param args Unused boot arguments.
 * @return void.
 */
void
slab_init(kernel_args* args)
{
}


/**
 * @brief No-op slab post-area init for DEBUG_HEAPS builds.
 *
 * @return void.
 */
void
slab_init_post_area()
{
}


/**
 * @brief No-op slab post-sem init for DEBUG_HEAPS builds.
 *
 * @return void.
 */
void
slab_init_post_sem()
{
}


/**
 * @brief No-op slab post-thread init for DEBUG_HEAPS builds.
 *
 * @return void.
 */
void
slab_init_post_thread()
{
}


#endif	// USE_DEBUG_HEAPS_FOR_ALL_OBJECT_CACHES


#endif	// DEBUG_HEAPS
