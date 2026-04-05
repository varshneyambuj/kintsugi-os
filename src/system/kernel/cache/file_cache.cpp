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
 *   Copyright 2004-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file file_cache.cpp
 * @brief VFS file cache — page-cache-backed buffered file I/O.
 *
 * Implements the kernel file cache used for buffered reads and writes.
 * File data is stored in VM pages managed by a VMCache per vnode. On a
 * cache miss, pages are faulted in via the vnode store. Dirty pages are
 * written back by the page writer or on explicit flush.
 *
 * @see vnode_store.cpp, file_map.cpp, block_cache.cpp
 */

#include "vnode_store.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <AutoDeleter.h>
#include <KernelExport.h>
#include <fs_cache.h>

#include <condition_variable.h>
#include <file_cache.h>
#include <generic_syscall.h>
#include <low_resource_manager.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <util/kernel_cpp.h>
#include <vfs.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/VMCache.h>

#include "IORequest.h"


//#define TRACE_FILE_CACHE
#ifdef TRACE_FILE_CACHE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

// maximum number of iovecs per request
#define MAX_IO_VECS			32	// 128 kB

#define BYPASS_IO_SIZE		65536
#define LAST_ACCESSES		3

struct file_cache_ref {
	VMCache			*cache;
	struct vnode	*vnode;
	off_t			last_access[LAST_ACCESSES];
		// TODO: it would probably be enough to only store the least
		//	significant 31 bits, and make this uint32 (one bit for
		//	write vs. read)
	int32			last_access_index;
	uint16			disabled_count;

	inline void SetLastAccess(int32 index, off_t access, bool isWrite)
	{
		// we remember writes as negative offsets
		last_access[index] = isWrite ? -access : access;
	}

	inline off_t LastAccess(int32 index, bool isWrite) const
	{
		return isWrite ? -last_access[index] : last_access[index];
	}

	inline uint32 LastAccessPageOffset(int32 index, bool isWrite)
	{
		return LastAccess(index, isWrite) >> PAGE_SHIFT;
	}
};

class PrecacheIO : public AsyncIOCallback {
public:
								PrecacheIO(file_cache_ref* ref, off_t offset,
									generic_size_t size);
								~PrecacheIO();

			status_t			Prepare(vm_page_reservation* reservation);
			void				ReadAsync();

	virtual	void				IOFinished(status_t status,
									bool partialTransfer,
									generic_size_t bytesTransferred);

private:
			file_cache_ref*		fRef;
			VMCache*			fCache;
			vm_page**			fPages;
			size_t				fPageCount;
			ConditionVariable*	fBusyConditions;
			generic_io_vec*		fVecs;
			off_t				fOffset;
			uint32				fVecCount;
			generic_size_t		fSize;
};

typedef status_t (*cache_func)(file_cache_ref* ref, void* cookie, off_t offset,
	int32 pageOffset, addr_t buffer, size_t bufferSize, bool useBuffer,
	vm_page_reservation* reservation, size_t reservePages);

static void add_to_iovec(generic_io_vec* vecs, uint32 &index, uint32 max,
	generic_addr_t address, generic_size_t size);


static struct cache_module_info* sCacheModule;


static const uint32 kZeroVecCount = 32;
static const size_t kZeroVecSize = kZeroVecCount * B_PAGE_SIZE;
static phys_addr_t sZeroPage;
static generic_io_vec sZeroVecs[kZeroVecCount];


//	#pragma mark -


/**
 * @brief Construct a PrecacheIO object for an asynchronous read-ahead operation.
 *
 * @param ref    The file cache reference whose VMCache will receive the pages.
 * @param offset Byte offset in the file at which the read-ahead begins (page-aligned).
 * @param size   Number of bytes to prefetch.
 *
 * @note Acquires both a reference and a store-ref on the underlying VMCache so
 *       the cache cannot be destroyed before IOFinished() is called.
 */
PrecacheIO::PrecacheIO(file_cache_ref* ref, off_t offset, generic_size_t size)
	:
	fRef(ref),
	fCache(ref->cache),
	fPages(NULL),
	fVecs(NULL),
	fOffset(offset),
	fVecCount(0),
	fSize(size)
{
	fPageCount = (size + B_PAGE_SIZE - 1) / B_PAGE_SIZE;
	fCache->AcquireRefLocked();
	fCache->AcquireStoreRef();
}


/**
 * @brief Destroy the PrecacheIO object and release VMCache references.
 *
 * @note Frees the page and I/O-vector arrays and drops the store-ref and
 *       reference that were acquired in the constructor.
 */
PrecacheIO::~PrecacheIO()
{
	delete[] fPages;
	delete[] fVecs;
	fCache->ReleaseStoreRef();
	fCache->ReleaseRef();
}


/**
 * @brief Allocate physical pages and build the I/O-vector list for the read-ahead.
 *
 * @param reservation Pre-reserved page pool from which pages are drawn.
 * @retval B_OK       Pages were allocated and inserted into the cache successfully.
 * @retval B_BAD_VALUE fPageCount is zero.
 * @retval B_NO_MEMORY Heap allocation for the page or vec arrays failed.
 *
 * @note All allocated pages are marked busy (busy_io = true) and inserted into
 *       the VMCache before the function returns. The caller must subsequently
 *       call ReadAsync() or free the pages manually on error.
 */
status_t
PrecacheIO::Prepare(vm_page_reservation* reservation)
{
	if (fPageCount == 0)
		return B_BAD_VALUE;

	fPages = new(std::nothrow) vm_page*[fPageCount];
	if (fPages == NULL)
		return B_NO_MEMORY;

	fVecs = new(std::nothrow) generic_io_vec[fPageCount];
	if (fVecs == NULL)
		return B_NO_MEMORY;

	// allocate pages for the cache and mark them busy
	uint32 i = 0;
	for (generic_size_t pos = 0; pos < fSize; pos += B_PAGE_SIZE) {
		vm_page* page = vm_page_allocate_page(reservation,
			PAGE_STATE_CACHED | VM_PAGE_ALLOC_BUSY);
		page->busy_io = true;

		fCache->InsertPage(page, fOffset + pos);
		DEBUG_PAGE_ACCESS_END(page);

		add_to_iovec(fVecs, fVecCount, fPageCount,
			page->physical_page_number * B_PAGE_SIZE, B_PAGE_SIZE);
		fPages[i++] = page;
	}

	return B_OK;
}


/**
 * @brief Issue the asynchronous read request to the VFS layer.
 *
 * @note This function transfers ownership of the PrecacheIO object to the I/O
 *       subsystem; the object will be deleted automatically inside IOFinished()
 *       once the transfer completes. The caller must not access the object after
 *       this call returns.
 */
void
PrecacheIO::ReadAsync()
{
	// This object is going to be deleted after the I/O request has been
	// fulfilled
	vfs_asynchronous_read_pages(fRef->vnode, NULL, fOffset, fVecs, fVecCount,
		fSize, B_PHYSICAL_IO_REQUEST, this);
}


/**
 * @brief Async I/O completion callback — mark transferred pages accessible and
 *        free pages that failed.
 *
 * @param status           Overall I/O status returned by the lower layer.
 * @param partialTransfer  True when fewer bytes than requested were transferred.
 * @param bytesTransferred Number of bytes actually read from the device.
 *
 * @note Acquires the VMCache lock internally. Pages for which busy_io was
 *       cleared externally (e.g. by cache shrinking) are released via
 *       FreeRemovedPage(). This function deletes @c this before returning.
 */
void
PrecacheIO::IOFinished(status_t status, bool partialTransfer,
	generic_size_t bytesTransferred)
{
	fCache->Lock();

	// Make successfully loaded pages accessible again (partially
	// transferred pages are considered failed)
	phys_size_t pagesTransferred
		= (bytesTransferred + B_PAGE_SIZE - 1) / B_PAGE_SIZE;

	if ((fOffset + (off_t)bytesTransferred) > fCache->virtual_end)
		bytesTransferred = fCache->virtual_end - fOffset;

	for (uint32 i = 0; i < pagesTransferred; i++) {
		DEBUG_PAGE_ACCESS_START(fPages[i]);

		if (i == pagesTransferred - 1
			&& (bytesTransferred % B_PAGE_SIZE) != 0) {
			// clear partial page
			size_t bytesTouched = bytesTransferred % B_PAGE_SIZE;
			vm_memset_physical(
				((phys_addr_t)fPages[i]->physical_page_number << PAGE_SHIFT)
					+ bytesTouched,
				0, B_PAGE_SIZE - bytesTouched);
		}

		if (!fPages[i]->busy_io) {
			// The busy_io flag was cleared. Let the cache handle the rest.
			fCache->FreeRemovedPage(fPages[i]);
			continue;
		}

		fPages[i]->busy_io = false;
		fCache->MarkPageUnbusy(fPages[i]);
		DEBUG_PAGE_ACCESS_END(fPages[i]);
	}

	// Free pages after failed I/O
	for (uint32 i = pagesTransferred; i < fPageCount; i++) {
		DEBUG_PAGE_ACCESS_START(fPages[i]);
		if (!fPages[i]->busy_io) {
			fCache->FreeRemovedPage(fPages[i]);
			continue;
		}

		fCache->NotifyPageEvents(fPages[i], PAGE_EVENT_NOT_BUSY);
		fCache->RemovePage(fPages[i]);
		vm_page_free(fCache, fPages[i]);
	}

	fCache->Unlock();
	delete this;
}


//	#pragma mark -


/**
 * @brief Append a physical address range to a generic_io_vec array, merging
 *        adjacent ranges where possible.
 *
 * @param vecs    Array of I/O vectors to update.
 * @param index   Current number of valid entries; incremented on a new entry.
 * @param max     Capacity of @p vecs; triggers a kernel panic if exceeded.
 * @param address Physical base address of the range to add.
 * @param size    Length of the range in bytes.
 *
 * @note If the new range is contiguous with the last existing vector the
 *       last vector's length is extended in place rather than creating a new
 *       entry.  Panics if @p index would overflow @p max.
 */
static void
add_to_iovec(generic_io_vec* vecs, uint32 &index, uint32 max,
	generic_addr_t address, generic_size_t size)
{
	if (index > 0 && vecs[index - 1].base + vecs[index - 1].length == address) {
		// the iovec can be combined with the previous one
		vecs[index - 1].length += size;
		return;
	}

	if (index == max)
		panic("no more space for iovecs!");

	// we need to start a new iovec
	vecs[index].base = address;
	vecs[index].length = size;
	index++;
}


/**
 * @brief Return whether the last recorded access pattern for a cache ref is
 *        sequential.
 *
 * @param ref The file cache reference to inspect.
 * @return true if the current slot in the access ring buffer is non-zero
 *         (indicating a previous sequential access was recorded).
 */
static inline bool
access_is_sequential(file_cache_ref* ref)
{
	return ref->last_access[ref->last_access_index] != 0;
}


/**
 * @brief Record a file access in the sequential-access ring buffer.
 *
 * @param ref     The file cache reference whose ring buffer is updated.
 * @param offset  Starting byte offset of the access.
 * @param bytes   Number of bytes accessed.
 * @param isWrite True for a write access; the offset is stored as a negative
 *                value to distinguish it from reads.
 *
 * @note If the access does not continue exactly from the previous end offset
 *       the previous slot is zeroed to break the sequential-access heuristic.
 */
static inline void
push_access(file_cache_ref* ref, off_t offset, generic_size_t bytes,
	bool isWrite)
{
	TRACE(("%p: push %lld, %ld, %s\n", ref, offset, bytes,
		isWrite ? "write" : "read"));

	int32 index = ref->last_access_index;
	int32 previous = index - 1;
	if (previous < 0)
		previous = LAST_ACCESSES - 1;

	if (offset != ref->LastAccess(previous, isWrite))
		ref->last_access[previous] = 0;

	ref->SetLastAccess(index, offset + bytes, isWrite);

	if (++index >= LAST_ACCESSES)
		index = 0;
	ref->last_access_index = index;
}


/**
 * @brief Reserve VM pages for an upcoming I/O operation, evicting cache pages
 *        under memory pressure if the access pattern is sequential.
 *
 * @param ref          The file cache reference whose cache may be trimmed.
 * @param reservation  Output reservation structure filled by this function.
 * @param reservePages Number of pages to reserve.
 * @param isWrite      True if the reservation is for a write operation; affects
 *                     which eviction strategy is used under low memory.
 *
 * @note Under low memory this function may release cached (unmodified) pages
 *       from the vnode cache, or wait for modified pages to be written back,
 *       before delegating to vm_page_reserve_pages().
 */
static void
reserve_pages(file_cache_ref* ref, vm_page_reservation* reservation,
	size_t reservePages, bool isWrite)
{
	if (low_resource_state(B_KERNEL_RESOURCE_PAGES) != B_NO_LOW_RESOURCE) {
		VMCache* cache = ref->cache;
		cache->Lock();

		if (cache->consumers.IsEmpty() && cache->areas.IsEmpty()
			&& access_is_sequential(ref)) {
			// we are not mapped, and we're accessed sequentially

			if (isWrite) {
				// Just write some pages back, and actually wait until they
				// have been written back in order to relieve the page pressure
				// a bit.
				int32 index = ref->last_access_index;
				int32 previous = index - 1;
				if (previous < 0)
					previous = LAST_ACCESSES - 1;

				vm_page_write_modified_page_range(cache,
					ref->LastAccessPageOffset(previous, true),
					ref->LastAccessPageOffset(index, true));
			} else {
				// free some pages from our cache
				// TODO: start with oldest
				uint32 left = reservePages;
				vm_page* page;
				for (VMCachePagesTree::Iterator it = cache->pages.GetIterator();
						(page = it.Next()) != NULL && left > 0;) {
					if (page->State() == PAGE_STATE_CACHED && !page->busy) {
						DEBUG_PAGE_ACCESS_START(page);
						ASSERT(!page->IsMapped());
						ASSERT(!page->modified);
						cache->RemovePage(page);
						vm_page_free(cache, page);
						left--;
					}
				}
			}
		}
		cache->Unlock();
	}

	vm_page_reserve_pages(reservation, reservePages, VM_PRIORITY_USER);
}


/**
 * @brief Read pages from the vnode into physical memory and zero any bytes
 *        beyond the end of the file that were not filled by the read.
 *
 * @param ref       The file cache reference identifying the vnode and cache.
 * @param cookie    VFS cookie forwarded to vfs_read_pages().
 * @param offset    File offset at which to start reading.
 * @param vecs      Array of physical I/O vectors describing the target pages.
 * @param count     Number of valid entries in @p vecs.
 * @param flags     Flags forwarded to vfs_read_pages() (e.g. B_PHYSICAL_IO_REQUEST).
 * @param _numBytes In: maximum bytes to read. Out: bytes actually transferred.
 * @retval B_OK     Read completed (possibly with a short transfer that was zeroed).
 *
 * @note Any tail bytes in the last physical page that were not covered by the
 *       vnode data are explicitly zeroed so callers never see stale memory.
 */
static inline status_t
read_pages_and_clear_partial(file_cache_ref* ref, void* cookie, off_t offset,
	const generic_io_vec* vecs, size_t count, uint32 flags,
	generic_size_t* _numBytes)
{
	generic_size_t bytesUntouched = *_numBytes;

	status_t status = vfs_read_pages(ref->vnode, cookie, offset, vecs, count,
		flags, _numBytes);

	generic_size_t bytesEnd = *_numBytes;

	if (offset + (off_t)bytesEnd > ref->cache->virtual_end)
		bytesEnd = ref->cache->virtual_end - offset;

	if (status == B_OK && bytesEnd < bytesUntouched) {
		// Clear out any leftovers that were not touched by the above read.
		// We're doing this here so that not every file system/device has to
		// implement this.
		bytesUntouched -= bytesEnd;

		for (int32 i = count; i-- > 0 && bytesUntouched != 0; ) {
			generic_size_t length = min_c(bytesUntouched, vecs[i].length);
			vm_memset_physical(vecs[i].base + vecs[i].length - length, 0,
				length);

			bytesUntouched -= length;
		}
	}

	return status;
}


/*!	Reads the requested amount of data into the cache, and allocates
	pages needed to fulfill that request. This function is called by cache_io().
	It can only handle a certain amount of bytes, and the caller must make
	sure that it matches that criterion.
	The cache_ref lock must be held when calling this function; during
	operation it will unlock the cache, though.
*/
/**
 * @brief Fault pages into the VMCache by reading from the backing vnode store
 *        and optionally copy the data to a caller-supplied buffer.
 *
 * @param ref          The file cache reference (cache lock must be held on entry).
 * @param cookie       VFS cookie forwarded to the read helper.
 * @param offset       Page-aligned file offset at which to start the read.
 * @param pageOffset   Byte offset within the first page (0 to B_PAGE_SIZE-1).
 * @param buffer       Kernel or user virtual address to copy read data into.
 * @param bufferSize   Number of bytes the caller wants copied into @p buffer.
 * @param useBuffer    When false, pages are populated but no copy is performed.
 * @param reservation  Page reservation used to allocate new cache pages.
 * @param reservePages Number of pages to re-reserve after the read for the next
 *                     iteration.
 * @retval B_OK        Pages were read, inserted into the cache, and (if
 *                     @p useBuffer is true) copied to @p buffer.
 *
 * @note The cache lock is dropped while I/O is in flight and re-acquired before
 *       returning.  On read failure all pages allocated by this call are
 *       removed from the cache and freed.
 */
static status_t
read_into_cache(file_cache_ref* ref, void* cookie, off_t offset,
	int32 pageOffset, addr_t buffer, size_t bufferSize, bool useBuffer,
	vm_page_reservation* reservation, size_t reservePages)
{
	TRACE(("read_into_cache(offset = %lld, pageOffset = %ld, buffer = %#lx, "
		"bufferSize = %lu\n", offset, pageOffset, buffer, bufferSize));

	VMCache* cache = ref->cache;

	// TODO: We're using way too much stack! Rather allocate a sufficiently
	// large chunk on the heap.
	generic_io_vec vecs[MAX_IO_VECS];
	uint32 vecCount = 0;

	generic_size_t numBytes = PAGE_ALIGN(pageOffset + bufferSize);
	vm_page* pages[MAX_IO_VECS];
	int32 pageIndex = 0;

	// allocate pages for the cache and mark them busy
	for (generic_size_t pos = 0; pos < numBytes; pos += B_PAGE_SIZE) {
		vm_page* page = pages[pageIndex++] = vm_page_allocate_page(
			reservation, PAGE_STATE_CACHED | VM_PAGE_ALLOC_BUSY);
		page->busy_io = true;

		cache->InsertPage(page, offset + pos);
		DEBUG_PAGE_ACCESS_END(page);

		add_to_iovec(vecs, vecCount, MAX_IO_VECS,
			page->physical_page_number * B_PAGE_SIZE, B_PAGE_SIZE);
			// TODO: check if the array is large enough (currently panics)!
	}

	push_access(ref, offset, bufferSize, false);
	cache->Unlock();
	vm_page_unreserve_pages(reservation);

	// read file into reserved pages
	status_t status = read_pages_and_clear_partial(ref, cookie, offset, vecs,
		vecCount, B_PHYSICAL_IO_REQUEST, &numBytes);
	if (status != B_OK) {
		// reading failed, free allocated pages

		dprintf("file_cache: read pages failed: %s\n", strerror(status));

		cache->Lock();

		for (int32 i = 0; i < pageIndex; i++) {
			DEBUG_PAGE_ACCESS_START(pages[i]);
			if (!pages[i]->busy_io) {
				cache->FreeRemovedPage(pages[i]);
				continue;
			}

			cache->NotifyPageEvents(pages[i], PAGE_EVENT_NOT_BUSY);
			cache->RemovePage(pages[i]);
			vm_page_free(cache, pages[i]);
		}

		return status;
	}

	// copy the pages if needed and unmap them again

	for (int32 i = 0; i < pageIndex; i++) {
		if (useBuffer && bufferSize != 0) {
			size_t bytes = min_c(bufferSize, (size_t)B_PAGE_SIZE - pageOffset);

			vm_memcpy_from_physical((void*)buffer,
				pages[i]->physical_page_number * B_PAGE_SIZE + pageOffset,
				bytes, IS_USER_ADDRESS(buffer));

			buffer += bytes;
			bufferSize -= bytes;
			pageOffset = 0;
		}
	}

	reserve_pages(ref, reservation, reservePages, false);
	cache->Lock();

	// make the pages accessible in the cache
	for (int32 i = pageIndex; i-- > 0;) {
		DEBUG_PAGE_ACCESS_START(pages[i]);
		if (!pages[i]->busy_io) {
			cache->FreeRemovedPage(pages[i]);
			continue;
		}

		pages[i]->busy_io = false;
		cache->MarkPageUnbusy(pages[i]);
		DEBUG_PAGE_ACCESS_END(pages[i]);
	}

	return B_OK;
}


/**
 * @brief Read data directly from the vnode into a caller-supplied buffer,
 *        bypassing the page cache entirely.
 *
 * @param ref          The file cache reference identifying the vnode.
 * @param cookie       VFS cookie forwarded to vfs_read_pages().
 * @param offset       Page-aligned file offset to read from.
 * @param pageOffset   Intra-page byte offset added to @p offset for the actual
 *                     read position.
 * @param buffer       Destination address for the data.
 * @param bufferSize   Number of bytes to read.
 * @param useBuffer    When false the function returns immediately with B_OK
 *                     without issuing any I/O.
 * @param reservation  Page reservation to restore after the bypass read.
 * @param reservePages Number of pages to re-reserve via reserve_pages().
 * @retval B_OK        Data was read successfully (or @p useBuffer was false).
 *
 * @note The VMCache lock is dropped for the duration of the I/O and
 *       re-acquired before returning.
 */
static status_t
read_from_file(file_cache_ref* ref, void* cookie, off_t offset,
	int32 pageOffset, addr_t buffer, size_t bufferSize, bool useBuffer,
	vm_page_reservation* reservation, size_t reservePages)
{
	TRACE(("read_from_file(offset = %lld, pageOffset = %ld, buffer = %#lx, "
		"bufferSize = %lu\n", offset, pageOffset, buffer, bufferSize));

	if (!useBuffer)
		return B_OK;

	generic_io_vec vec;
	vec.base = buffer;
	vec.length = bufferSize;

	push_access(ref, offset, bufferSize, false);
	ref->cache->Unlock();
	vm_page_unreserve_pages(reservation);

	generic_size_t toRead = bufferSize;
	status_t status = vfs_read_pages(ref->vnode, cookie, offset + pageOffset,
		&vec, 1, 0, &toRead);

	if (status == B_OK)
		reserve_pages(ref, reservation, reservePages, false);

	ref->cache->Lock();

	return status;
}


/*!	Like read_into_cache() but writes data into the cache.
	To preserve data consistency, it might also read pages into the cache,
	though, if only a partial page gets written.
	The same restrictions apply.
*/
/**
 * @brief Allocate cache pages, fill them from @p buffer, and mark them dirty
 *        for writeback (or write through immediately if write-through is active).
 *
 * @param ref          The file cache reference (cache lock must be held on entry).
 * @param cookie       VFS cookie used for partial-page read-modify-write cycles.
 * @param offset       Page-aligned file offset at which to begin writing.
 * @param pageOffset   Byte offset within the first page to start the write.
 * @param buffer       Source address (kernel or user space) for write data.
 * @param bufferSize   Number of bytes to write from @p buffer.
 * @param useBuffer    When false, the cache pages are zeroed instead of copied
 *                     from @p buffer.
 * @param reservation  Page reservation used to allocate new cache pages.
 * @param reservePages Number of pages to re-reserve after the write.
 * @retval B_OK        Pages were allocated, populated, and inserted successfully.
 *
 * @note For partial first or last pages a read-modify-write is performed via
 *       vfs_read_pages() to preserve unwritten bytes.  The cache lock is dropped
 *       while I/O is in flight.
 */
static status_t
write_to_cache(file_cache_ref* ref, void* cookie, off_t offset,
	int32 pageOffset, addr_t buffer, size_t bufferSize, bool useBuffer,
	vm_page_reservation* reservation, size_t reservePages)
{
	// TODO: We're using way too much stack! Rather allocate a sufficiently
	// large chunk on the heap.
	generic_io_vec vecs[MAX_IO_VECS];
	uint32 vecCount = 0;
	generic_size_t numBytes = PAGE_ALIGN(pageOffset + bufferSize);
	vm_page* pages[MAX_IO_VECS];
	int32 pageIndex = 0;
	status_t status = B_OK;

	// ToDo: this should be settable somewhere
	bool writeThrough = false;

	// allocate pages for the cache and mark them busy
	for (generic_size_t pos = 0; pos < numBytes; pos += B_PAGE_SIZE) {
		// TODO: if space is becoming tight, and this cache is already grown
		//	big - shouldn't we better steal the pages directly in that case?
		//	(a working set like approach for the file cache)
		// TODO: the pages we allocate here should have been reserved upfront
		//	in cache_io()
		vm_page* page = pages[pageIndex++] = vm_page_allocate_page(
			reservation,
			(writeThrough ? PAGE_STATE_CACHED : PAGE_STATE_MODIFIED)
				| VM_PAGE_ALLOC_BUSY);
		page->busy_io = true;

		page->modified = !writeThrough;

		ref->cache->InsertPage(page, offset + pos);
		DEBUG_PAGE_ACCESS_END(page);

		add_to_iovec(vecs, vecCount, MAX_IO_VECS,
			page->physical_page_number * B_PAGE_SIZE, B_PAGE_SIZE);
	}

	push_access(ref, offset, bufferSize, true);
	ref->cache->Unlock();
	vm_page_unreserve_pages(reservation);

	// copy contents (and read in partially written pages first)

	if (pageOffset != 0) {
		// This is only a partial write, so we have to read the rest of the page
		// from the file to have consistent data in the cache
		generic_io_vec readVec = { vecs[0].base, B_PAGE_SIZE };
		generic_size_t bytesRead = B_PAGE_SIZE;

		status = vfs_read_pages(ref->vnode, cookie, offset, &readVec, 1,
			B_PHYSICAL_IO_REQUEST, &bytesRead);
		// ToDo: handle errors for real!
		if (status < B_OK)
			panic("1. vfs_read_pages() failed: %s!\n", strerror(status));
	}

	size_t lastPageOffset = (pageOffset + bufferSize) % B_PAGE_SIZE;
	if (lastPageOffset != 0) {
		// get the last page in the I/O vectors
		generic_addr_t last = vecs[vecCount - 1].base
			+ vecs[vecCount - 1].length - B_PAGE_SIZE;

		if ((off_t)(offset + pageOffset + bufferSize) == ref->cache->virtual_end) {
			// the space in the page after this write action needs to be cleaned
			vm_memset_physical(last + lastPageOffset, 0,
				B_PAGE_SIZE - lastPageOffset);
		} else {
			// the end of this write does not happen on a page boundary, so we
			// need to fetch the last page before we can update it
			generic_io_vec readVec = { last, B_PAGE_SIZE };
			generic_size_t bytesRead = B_PAGE_SIZE;

			status = vfs_read_pages(ref->vnode, cookie,
				PAGE_ALIGN(offset + pageOffset + bufferSize) - B_PAGE_SIZE,
				&readVec, 1, B_PHYSICAL_IO_REQUEST, &bytesRead);
			// ToDo: handle errors for real!
			if (status < B_OK)
				panic("vfs_read_pages() failed: %s!\n", strerror(status));

			if (bytesRead < B_PAGE_SIZE) {
				// the space beyond the file size needs to be cleaned
				vm_memset_physical(last + bytesRead, 0,
					B_PAGE_SIZE - bytesRead);
			}
		}
	}

	for (uint32 i = 0; i < vecCount; i++) {
		generic_addr_t base = vecs[i].base;
		generic_size_t bytes = min_c((generic_size_t)bufferSize,
			generic_size_t(vecs[i].length - pageOffset));

		if (useBuffer) {
			// copy data from user buffer
			vm_memcpy_to_physical(base + pageOffset, (void*)buffer, bytes,
				IS_USER_ADDRESS(buffer));
		} else {
			// clear buffer instead
			vm_memset_physical(base + pageOffset, 0, bytes);
		}

		bufferSize -= bytes;
		if (bufferSize == 0)
			break;

		buffer += bytes;
		pageOffset = 0;
	}

	if (writeThrough) {
		// write cached pages back to the file if we were asked to do that
		status_t status = vfs_write_pages(ref->vnode, cookie, offset, vecs,
			vecCount, B_PHYSICAL_IO_REQUEST, &numBytes);
		if (status < B_OK) {
			// ToDo: remove allocated pages, ...?
			panic("file_cache: remove allocated pages! write pages failed: %s\n",
				strerror(status));
		}
	}

	if (status == B_OK)
		reserve_pages(ref, reservation, reservePages, true);

	ref->cache->Lock();

	// make the pages accessible in the cache
	for (int32 i = pageIndex; i-- > 0;) {
		DEBUG_PAGE_ACCESS_START(pages[i]);
		if (!pages[i]->busy_io) {
			ref->cache->FreeRemovedPage(pages[i]);
			continue;
		}

		pages[i]->busy_io = false;
		ref->cache->MarkPageUnbusy(pages[i]);
		DEBUG_PAGE_ACCESS_END(pages[i]);
	}

	return status;
}


/**
 * @brief Write a range of zeroes directly to a vnode, bypassing the page cache.
 *
 * @param vnode   The vnode to write to.
 * @param cookie  VFS cookie forwarded to vfs_write_pages().
 * @param offset  File offset at which to start writing zeroes.
 * @param _size   In: number of zero bytes to write. Out: bytes actually written.
 * @retval B_OK   All bytes were zeroed successfully.
 *
 * @note Uses the pre-allocated sZeroPage / sZeroVecs to avoid dynamic
 *       allocation in the I/O path.
 */
static status_t
write_zeros_to_file(struct vnode* vnode, void* cookie, off_t offset,
	size_t* _size)
{
	size_t size = *_size;
	status_t status = B_OK;
	while (size > 0) {
		generic_size_t length = min_c(size, kZeroVecSize);
		generic_io_vec* vecs = sZeroVecs;
		generic_io_vec vec;
		size_t count = kZeroVecCount;
		if (length != kZeroVecSize) {
			if (length > B_PAGE_SIZE) {
				length = ROUNDDOWN(length, B_PAGE_SIZE);
				count = length / B_PAGE_SIZE;
			} else {
				vec.base = sZeroPage;
				vec.length = length;
				vecs = &vec;
				count = 1;
			}
		}

		status = vfs_write_pages(vnode, cookie, offset,
			vecs, count, B_PHYSICAL_IO_REQUEST, &length);
		if (status != B_OK || length == 0)
			break;

		offset += length;
		size -= length;
	}

	*_size = *_size - size;
	return status;
}


/**
 * @brief Write data directly to the vnode, bypassing the page cache.
 *
 * @param ref          The file cache reference identifying the vnode.
 * @param cookie       VFS cookie forwarded to vfs_write_pages() or
 *                     write_zeros_to_file().
 * @param offset       Page-aligned base file offset.
 * @param pageOffset   Intra-page byte offset added to @p offset for the write.
 * @param buffer       Source address for write data.
 * @param bufferSize   Number of bytes to write.
 * @param useBuffer    When false, zeroes are written instead of buffer data.
 * @param reservation  Page reservation to restore after the write.
 * @param reservePages Number of pages to re-reserve via reserve_pages().
 * @retval B_OK        Data was written successfully.
 *
 * @note The VMCache lock is dropped for the duration of the I/O and
 *       re-acquired before returning.
 */
static status_t
write_to_file(file_cache_ref* ref, void* cookie, off_t offset, int32 pageOffset,
	addr_t buffer, size_t bufferSize, bool useBuffer,
	vm_page_reservation* reservation, size_t reservePages)
{
	push_access(ref, offset, bufferSize, true);
	ref->cache->Unlock();
	vm_page_unreserve_pages(reservation);

	status_t status = B_OK;

	if (!useBuffer) {
		status = write_zeros_to_file(ref->vnode, cookie, offset + pageOffset,
			&bufferSize);
	} else {
		generic_io_vec vec;
		vec.base = buffer;
		vec.length = bufferSize;
		generic_size_t toWrite = bufferSize;
		status = vfs_write_pages(ref->vnode, cookie, offset + pageOffset,
			&vec, 1, 0, &toWrite);
	}

	if (status == B_OK)
		reserve_pages(ref, reservation, reservePages, true);

	ref->cache->Lock();

	return status;
}


/**
 * @brief Flush any pending gap in the current I/O sweep by calling the
 *        appropriate cache read or write function.
 *
 * @param ref              The file cache reference.
 * @param cookie           VFS cookie forwarded to @p function.
 * @param function         The cache_func to invoke (read_into_cache,
 *                         write_to_cache, read_from_file, or write_to_file).
 * @param offset           Current page-aligned file offset (end of gap).
 * @param buffer           Current user/kernel buffer pointer (end of gap).
 * @param useBuffer        Whether the buffer contains valid data.
 * @param pageOffset       Intra-page offset for the current position; reset to
 *                         0 on success.
 * @param bytesLeft        Bytes remaining in the overall request.
 * @param reservePages     Updated with the number of pages reserved for the
 *                         next chunk.
 * @param lastOffset       In/out: start of the pending gap; updated on success.
 * @param lastBuffer       In/out: buffer pointer at start of gap; updated on
 *                         success.
 * @param lastPageOffset   In/out: page offset at start of gap; reset on success.
 * @param lastLeft         In/out: bytes left at start of gap; updated on success.
 * @param lastReservedPages In/out: reservation count from previous iteration.
 * @param reservation      Active page reservation passed to @p function.
 * @retval B_OK            The gap was satisfied and the "last*" state variables
 *                         advanced.
 *
 * @note Returns B_OK immediately (no-op) when @p lastBuffer == @p buffer,
 *       meaning no gap has accumulated since the last flush.
 */
static inline status_t
satisfy_cache_io(file_cache_ref* ref, void* cookie, cache_func function,
	off_t offset, addr_t buffer, bool useBuffer, int32 &pageOffset,
	size_t bytesLeft, size_t &reservePages, off_t &lastOffset,
	addr_t &lastBuffer, int32 &lastPageOffset, size_t &lastLeft,
	size_t &lastReservedPages, vm_page_reservation* reservation)
{
	if (lastBuffer == buffer)
		return B_OK;

	size_t requestSize = buffer - lastBuffer;
	reservePages = min_c(MAX_IO_VECS, (lastLeft - requestSize
		+ lastPageOffset + B_PAGE_SIZE - 1) >> PAGE_SHIFT);

	status_t status = function(ref, cookie, lastOffset, lastPageOffset,
		lastBuffer, requestSize, useBuffer, reservation, reservePages);
	if (status == B_OK) {
		lastReservedPages = reservePages;
		lastBuffer = buffer;
		lastLeft = bytesLeft;
		lastOffset = offset;
		lastPageOffset = 0;
		pageOffset = 0;
	}
	return status;
}


/**
 * @brief Core I/O dispatch loop: walk the page cache, serve hits in-place, and
 *        batch cache misses into calls to the selected read or write function.
 *
 * @param _cacheRef Opaque pointer to a file_cache_ref; must not be NULL.
 * @param cookie    VFS cookie forwarded to all subordinate I/O helpers.
 * @param offset    Byte offset in the file at which the I/O begins.
 * @param buffer    Kernel or user virtual address of the data buffer.
 * @param _size     In: requested byte count. Out: bytes actually transferred.
 * @param doWrite   True for write, false for read.
 * @retval B_OK     I/O completed (possibly short due to end-of-file).
 *
 * @note Holds the VMCache lock for most of its execution; drops it briefly
 *       around every subordinate read/write call and while waiting for busy
 *       pages.  Adapts between cached (read_into_cache / write_to_cache) and
 *       bypass (read_from_file / write_to_file) modes every 32 pages based on
 *       available memory.
 */
static status_t
do_cache_io(void* _cacheRef, void* cookie, off_t offset, addr_t buffer,
	size_t* _size, bool doWrite)
{
	if (_cacheRef == NULL)
		panic("cache_io() called with NULL ref!\n");

	file_cache_ref* ref = (file_cache_ref*)_cacheRef;
	VMCache* cache = ref->cache;

	const bool useBuffer = buffer != 0;
	const off_t startOffset = offset;
	const size_t size = *_size;

	TRACE(("cache_io(ref = %p, offset = %lld, buffer = %p, size = %lu, %s)\n",
		ref, offset, (void*)buffer, size, doWrite ? "write" : "read"));

	int32 pageOffset = offset & (B_PAGE_SIZE - 1);
	offset -= pageOffset;

	// "offset" and "lastOffset" are always aligned to B_PAGE_SIZE,
	// the "last*" variables always point to the end of the last
	// satisfied request part

	const uint32 kMaxChunkSize = MAX_IO_VECS * B_PAGE_SIZE;

	size_t lastReservedPages = min_c(MAX_IO_VECS, (pageOffset + size
		+ B_PAGE_SIZE - 1) >> PAGE_SHIFT);
	vm_page_reservation reservation;
	reserve_pages(ref, &reservation, lastReservedPages, doWrite);
	CObjectDeleter<vm_page_reservation, void, vm_page_unreserve_pages>
		pagesUnreserver(&reservation);

	AutoLocker<VMCache> locker(cache);

	size_t bytesLeft = size, lastLeft = size;
	int32 lastPageOffset = pageOffset;
	addr_t lastBuffer = buffer;
	off_t lastOffset = offset;
	size_t reservePages = 0;
	size_t pagesProcessed = 0;
	cache_func function = NULL;

	while (bytesLeft > 0) {
		// Periodically reevaluate the low memory situation and select the
		// read/write hook accordingly
		if (pagesProcessed % 32 == 0) {
			if (size >= BYPASS_IO_SIZE
				&& low_resource_state(B_KERNEL_RESOURCE_PAGES)
					!= B_NO_LOW_RESOURCE) {
				// In low memory situations we bypass the cache beyond a
				// certain I/O size.
				function = doWrite ? write_to_file : read_from_file;
			} else
				function = doWrite ? write_to_cache : read_into_cache;
		}

		// check if this page is already in memory
		vm_page* page = cache->LookupPage(offset);
		if (page != NULL) {
			// The page may be busy - since we need to unlock the cache sometime
			// in the near future, we need to satisfy the request of the pages
			// we didn't get yet (to make sure no one else interferes in the
			// meantime).
			status_t status = satisfy_cache_io(ref, cookie, function, offset,
				buffer, useBuffer, pageOffset, bytesLeft, reservePages,
				lastOffset, lastBuffer, lastPageOffset, lastLeft,
				lastReservedPages, &reservation);
			if (status != B_OK)
				return status;

			// Since satisfy_cache_io() unlocks the cache, we need to look up
			// the page again.
			page = cache->LookupPage(offset);
			if (page != NULL && page->busy) {
				cache->WaitForPageEvents(page, PAGE_EVENT_NOT_BUSY, true);
				continue;
			}
		}

		size_t bytesInPage = min_c(size_t(B_PAGE_SIZE - pageOffset), bytesLeft);

		TRACE(("lookup page from offset %lld: %p, size = %lu, pageOffset "
			"= %lu\n", offset, page, bytesLeft, pageOffset));

		if (page != NULL) {
			if (doWrite || useBuffer) {
				// Since the following user_mem{cpy,set}() might cause a page
				// fault, which in turn might cause pages to be reserved, we
				// need to unlock the cache temporarily to avoid a potential
				// deadlock. To make sure that our page doesn't go away, we mark
				// it busy for the time.
				page->busy = true;
				locker.Unlock();

				// copy the contents of the page already in memory
				phys_addr_t pageAddress
					= (phys_addr_t)page->physical_page_number * B_PAGE_SIZE
						+ pageOffset;
				bool userBuffer = IS_USER_ADDRESS(buffer);
				if (doWrite) {
					if (useBuffer) {
						vm_memcpy_to_physical(pageAddress, (void*)buffer,
							bytesInPage, userBuffer);
					} else {
						vm_memset_physical(pageAddress, 0, bytesInPage);
					}
				} else if (useBuffer) {
					vm_memcpy_from_physical((void*)buffer, pageAddress,
						bytesInPage, userBuffer);
				}

				locker.Lock();

				if (doWrite) {
					DEBUG_PAGE_ACCESS_START(page);

					page->modified = true;

					if (page->State() != PAGE_STATE_MODIFIED)
						vm_page_set_state(page, PAGE_STATE_MODIFIED);

					DEBUG_PAGE_ACCESS_END(page);
				}

				cache->MarkPageUnbusy(page);
			}

			// If it is cached only, requeue the page, so the respective queue
			// roughly remains LRU first sorted.
			if (page->State() == PAGE_STATE_CACHED
					|| page->State() == PAGE_STATE_MODIFIED) {
				DEBUG_PAGE_ACCESS_START(page);
				vm_page_requeue(page, true);
				DEBUG_PAGE_ACCESS_END(page);
			}

			if (bytesLeft <= bytesInPage) {
				// we've read the last page, so we're done!
				locker.Unlock();
				return B_OK;
			}

			// prepare a potential gap request
			lastBuffer = buffer + bytesInPage;
			lastLeft = bytesLeft - bytesInPage;
			lastOffset = offset + B_PAGE_SIZE;
			lastPageOffset = 0;
		}

		if ((lastOffset + (off_t)lastLeft) > cache->virtual_end) {
			// Someone else must've shrunk the cache.
			if (lastOffset > startOffset)
				*_size = lastOffset - startOffset;
			else
				*_size = 0;
			return B_OK;
		}

		if (bytesLeft <= bytesInPage)
			break;

		buffer += bytesInPage;
		bytesLeft -= bytesInPage;
		pageOffset = 0;
		offset += B_PAGE_SIZE;
		pagesProcessed++;

		if (buffer - lastBuffer + lastPageOffset >= kMaxChunkSize) {
			status_t status = satisfy_cache_io(ref, cookie, function, offset,
				buffer, useBuffer, pageOffset, bytesLeft, reservePages,
				lastOffset, lastBuffer, lastPageOffset, lastLeft,
				lastReservedPages, &reservation);
			if (status != B_OK)
				return status;
		}
	}

	// fill the last remaining bytes of the request (either write or read)

	return function(ref, cookie, lastOffset, lastPageOffset, lastBuffer,
		lastLeft, useBuffer, &reservation, 0);
}


/**
 * @brief Public wrapper around do_cache_io() that handles B_BUSY retries
 *        caused by page-fault-wait restrictions.
 *
 * @param ref     Opaque file_cache_ref pointer.
 * @param cookie  VFS cookie forwarded to do_cache_io().
 * @param offset  Starting byte offset of the I/O.
 * @param buffer  User or kernel data buffer.
 * @param _size   In: requested size. Out: bytes transferred.
 * @param doWrite True for write, false for read.
 * @retval B_OK   I/O completed successfully.
 *
 * @note Temporarily decrements page_fault_waits_allowed on the current thread
 *       so that page faults do not deadlock on cache pages. On B_BUSY the
 *       unfinished portion of the buffer is zeroed and the I/O is retried.
 */
static status_t
cache_io(void* ref, void* cookie, off_t offset, addr_t buffer,
	size_t* _size, bool doWrite)
{
	size_t originalSize = *_size;

	thread_get_current_thread()->page_fault_waits_allowed--;
	status_t status = do_cache_io(ref, cookie, offset, buffer, _size, doWrite);
	thread_get_current_thread()->page_fault_waits_allowed++;

	if (status == B_BUSY) {
		// This likely means that fault handler would've needed to wait for a page,
		// but we can't allow that here because it could be one of our pages that
		// it would've waited on, which would cause a deadlock.
		// Call memset so that all pages are faulted in, and retry.
		off_t retryOffset = offset;
		addr_t retryBuffer = buffer;
		size_t retrySize = originalSize;
		if (*_size != originalSize) {
			retryOffset += *_size;
			retryBuffer += *_size;
			retrySize -= *_size;
		}
		if (IS_USER_ADDRESS(buffer)) {
			status = user_memset((void*)retryBuffer, 0, retrySize);
		} else {
			memset((void*)retryBuffer, 0, retrySize);
			status = B_OK;
		}
		if (status == B_OK) {
			thread_get_current_thread()->page_fault_waits_allowed--;
			status = do_cache_io(ref, cookie, retryOffset, retryBuffer, &retrySize, doWrite);
			*_size += retrySize;
			thread_get_current_thread()->page_fault_waits_allowed++;
		}
	}

	return status;
}


/**
 * @brief Generic syscall handler for the file cache subsystem.
 *
 * @param subsystem  Subsystem name string (unused).
 * @param function   One of CACHE_CLEAR or CACHE_SET_MODULE.
 * @param buffer     User-space pointer; for CACHE_SET_MODULE it holds the
 *                   NUL-terminated module name to activate.
 * @param bufferSize Size of @p buffer in bytes.
 * @retval B_OK         Operation completed successfully.
 * @retval B_BAD_ADDRESS @p buffer is not a valid user address.
 * @retval B_BAD_VALUE  The module name does not start with CACHE_MODULES_NAME.
 * @retval B_BAD_HANDLER Unknown @p function code.
 *
 * @note CACHE_SET_MODULE unloads the previous cache module (if any) with a
 *       100 ms grace period before loading the new one.
 */
static status_t
file_cache_control(const char* subsystem, uint32 function, void* buffer,
	size_t bufferSize)
{
	switch (function) {
		case CACHE_CLEAR:
			// ToDo: clear the cache
			dprintf("cache_control: clear cache!\n");
			return B_OK;

		case CACHE_SET_MODULE:
		{
			cache_module_info* module = sCacheModule;

			// unset previous module

			if (sCacheModule != NULL) {
				sCacheModule = NULL;
				snooze(100000);	// 0.1 secs
				put_module(module->info.name);
			}

			// get new module, if any

			if (buffer == NULL)
				return B_OK;

			char name[B_FILE_NAME_LENGTH];
			if (!IS_USER_ADDRESS(buffer)
				|| user_strlcpy(name, (char*)buffer,
						B_FILE_NAME_LENGTH) < B_OK)
				return B_BAD_ADDRESS;

			if (strncmp(name, CACHE_MODULES_NAME, strlen(CACHE_MODULES_NAME)))
				return B_BAD_VALUE;

			dprintf("cache_control: set module %s!\n", name);

			status_t status = get_module(name, (module_info**)&module);
			if (status == B_OK)
				sCacheModule = module;

			return status;
		}
	}

	return B_BAD_HANDLER;
}


//	#pragma mark - private kernel API


/**
 * @brief Prefetch file data into the page cache for a vnode that is already
 *        known to the caller.
 *
 * @param vnode  The vnode whose cache should be populated.
 * @param offset Byte offset at which prefetching begins (rounded down to page).
 * @param size   Number of bytes to prefetch (rounded up to page).
 *
 * @note Silently returns without doing anything if the vnode has no associated
 *       file cache, if there are insufficient free pages (fewer than twice the
 *       requested page count), or if the cache already contains more than 2/3
 *       of the file's pages.  I/O is issued asynchronously via PrecacheIO.
 */
extern "C" void
cache_prefetch_vnode(struct vnode* vnode, off_t offset, size_t size)
{
	if (size == 0)
		return;

	VMCache* cache;
	if (vfs_get_vnode_cache(vnode, &cache, false) != B_OK)
		return;
	if (cache->type != CACHE_TYPE_VNODE) {
		cache->ReleaseRef();
		return;
	}

	file_cache_ref* ref = ((VMVnodeCache*)cache)->FileCacheRef();
	off_t fileSize = cache->virtual_end;

	if ((off_t)(offset + size) > fileSize)
		size = fileSize - offset;

	// "offset" and "size" are always aligned to B_PAGE_SIZE,
	offset = ROUNDDOWN(offset, B_PAGE_SIZE);
	size = ROUNDUP(size, B_PAGE_SIZE);

	const size_t pagesCount = size / B_PAGE_SIZE;

	// Don't do anything if we don't have the resources left, or the cache
	// already contains more than 2/3 of its pages
	if (offset >= fileSize || vm_page_num_unused_pages() < 2 * pagesCount
		|| (3 * cache->page_count) > (2 * fileSize / B_PAGE_SIZE)) {
		cache->ReleaseRef();
		return;
	}

	size_t bytesToRead = 0;
	off_t lastOffset = offset;

	vm_page_reservation reservation;
	vm_page_reserve_pages(&reservation, pagesCount, VM_PRIORITY_USER);

	cache->Lock();

	while (true) {
		// check if this page is already in memory
		if (size > 0) {
			vm_page* page = cache->LookupPage(offset);

			offset += B_PAGE_SIZE;
			size -= B_PAGE_SIZE;

			if (page == NULL) {
				bytesToRead += B_PAGE_SIZE;
				continue;
			}
		}
		if (bytesToRead != 0) {
			// read the part before the current page (or the end of the request)
			PrecacheIO* io = new(std::nothrow) PrecacheIO(ref, lastOffset,
				bytesToRead);
			if (io == NULL || io->Prepare(&reservation) != B_OK) {
				cache->Unlock();
				delete io;
				cache->Lock();
				break;
			}

			// we must not have the cache locked during I/O
			cache->Unlock();
			io->ReadAsync();
			cache->Lock();

			bytesToRead = 0;
		}

		if (size == 0) {
			// we have reached the end of the request
			break;
		}

		lastOffset = offset;
	}

	cache->ReleaseRefAndUnlock();
	vm_page_unreserve_pages(&reservation);
}


/**
 * @brief Prefetch file data into the page cache by mount and vnode ID.
 *
 * @param mountID  Mount identifier used to look up the vnode.
 * @param vnodeID  Vnode identifier within the mount.
 * @param offset   Byte offset at which prefetching begins.
 * @param size     Number of bytes to prefetch.
 *
 * @note Acquires a temporary reference to the vnode via vfs_get_vnode() and
 *       delegates to cache_prefetch_vnode().
 */
extern "C" void
cache_prefetch(dev_t mountID, ino_t vnodeID, off_t offset, size_t size)
{
	// ToDo: schedule prefetch

	TRACE(("cache_prefetch(vnode %ld:%lld)\n", mountID, vnodeID));

	// get the vnode for the object, this also grabs a ref to it
	struct vnode* vnode;
	if (vfs_get_vnode(mountID, vnodeID, true, &vnode) != B_OK)
		return;

	cache_prefetch_vnode(vnode, offset, size);
	vfs_put_vnode(vnode);
}


/**
 * @brief Notify the active cache module that a vnode has been opened.
 *
 * @param vnode    The vnode that was opened.
 * @param cache    The VMCache associated with the vnode, or NULL.
 * @param mountID  Mount identifier of the vnode.
 * @param parentID Inode number of the parent directory.
 * @param vnodeID  Inode number of the opened vnode.
 * @param name     Name of the file as it appears in its parent directory.
 *
 * @note No-op when no cache module is loaded or the module does not implement
 *       node_opened.  The file size passed to the module is -1 when the vnode
 *       has no associated file cache.
 */
extern "C" void
cache_node_opened(struct vnode* vnode, VMCache* cache,
	dev_t mountID, ino_t parentID, ino_t vnodeID, const char* name)
{
	if (sCacheModule == NULL || sCacheModule->node_opened == NULL)
		return;

	off_t size = -1;
	if (cache != NULL && cache->type == CACHE_TYPE_VNODE) {
		file_cache_ref* ref = ((VMVnodeCache*)cache)->FileCacheRef();
		if (ref != NULL)
			size = cache->virtual_end;
	}

	sCacheModule->node_opened(vnode, mountID, parentID, vnodeID, name,
		size);
}


/**
 * @brief Notify the active cache module that a vnode has been closed.
 *
 * @param vnode   The vnode that was closed.
 * @param cache   The VMCache associated with the vnode, or NULL.
 * @param mountID Mount identifier of the vnode.
 * @param vnodeID Inode number of the closed vnode.
 *
 * @note No-op when no cache module is loaded or the module does not implement
 *       node_closed.
 */
extern "C" void
cache_node_closed(struct vnode* vnode, VMCache* cache,
	dev_t mountID, ino_t vnodeID)
{
	if (sCacheModule == NULL || sCacheModule->node_closed == NULL)
		return;

	int32 accessType = 0;
	if (cache != NULL && cache->type == CACHE_TYPE_VNODE) {
		// ToDo: set accessType
	}

	sCacheModule->node_closed(vnode, mountID, vnodeID, accessType);
}


/**
 * @brief Notify the active cache module that a new process has been launched.
 *
 * @param argCount Number of arguments in @p args.
 * @param args     NULL-terminated argv array of the launched process.
 *
 * @note No-op when no cache module is loaded or the module does not implement
 *       node_launched.
 */
extern "C" void
cache_node_launched(size_t argCount, char*  const* args)
{
	if (sCacheModule == NULL || sCacheModule->node_launched == NULL)
		return;

	sCacheModule->node_launched(argCount, args);
}


/**
 * @brief Load the optional launch-speedup cache module after the boot device
 *        is available.
 *
 * @retval B_OK Always returns B_OK; module load failure is non-fatal.
 *
 * @note Called once during the post-boot-device initialisation phase.
 *       If the "file_cache/launch_speedup/v1" module is present it is opened
 *       and stored in sCacheModule.
 */
extern "C" status_t
file_cache_init_post_boot_device(void)
{
	// ToDo: get cache module out of driver settings

	if (get_module("file_cache/launch_speedup/v1",
			(module_info**)&sCacheModule) == B_OK) {
		dprintf("** opened launch speedup: %" B_PRId64 "\n", system_time());
	}
	return B_OK;
}


/**
 * @brief Initialise the file cache subsystem at kernel boot time.
 *
 * @retval B_OK Initialisation succeeded.
 *
 * @note Allocates and wires a single zeroed page (sZeroPage) used for writing
 *       zeroes without dynamic allocation in the I/O path.  Also populates the
 *       sZeroVecs array and registers the CACHE_SYSCALLS generic syscall handler.
 *       Must be called before any file cache or vnode store operations.
 */
extern "C" status_t
file_cache_init(void)
{
	// allocate a clean page we can use for writing zeroes
	vm_page_reservation reservation;
	vm_page_reserve_pages(&reservation, 1, VM_PRIORITY_SYSTEM);
	vm_page* page = vm_page_allocate_page(&reservation,
		PAGE_STATE_WIRED | VM_PAGE_ALLOC_CLEAR);
	vm_page_unreserve_pages(&reservation);

	sZeroPage = (phys_addr_t)page->physical_page_number * B_PAGE_SIZE;

	for (uint32 i = 0; i < kZeroVecCount; i++) {
		sZeroVecs[i].base = sZeroPage;
		sZeroVecs[i].length = B_PAGE_SIZE;
	}

	register_generic_syscall(CACHE_SYSCALLS, file_cache_control, 1, 0);
	return B_OK;
}


//	#pragma mark - public FS API


/**
 * @brief Create a file cache for the given vnode, associating it with a new or
 *        existing VMCache.
 *
 * @param mountID  Mount identifier of the file system.
 * @param vnodeID  Inode number within that mount.
 * @param size     Initial logical file size in bytes; stored in
 *                 VMCache::virtual_end.
 * @return Opaque file_cache_ref pointer on success, NULL on failure.
 *
 * @note Does not grab a vnode reference; the vnode must remain valid for the
 *       lifetime of the returned cache ref.  The new ref is registered with
 *       the VMVnodeCache via SetFileCacheRef().
 */
extern "C" void*
file_cache_create(dev_t mountID, ino_t vnodeID, off_t size)
{
	TRACE(("file_cache_create(mountID = %ld, vnodeID = %lld, size = %lld)\n",
		mountID, vnodeID, size));

	file_cache_ref* ref = new file_cache_ref;
	if (ref == NULL)
		return NULL;

	memset(ref->last_access, 0, sizeof(ref->last_access));
	ref->last_access_index = 0;
	ref->disabled_count = 0;

	// TODO: delay VMCache creation until data is
	//	requested/written for the first time? Listing lots of
	//	files in Tracker (and elsewhere) could be slowed down.
	//	Since the file_cache_ref itself doesn't have a lock,
	//	we would need to "rent" one during construction, possibly
	//	the vnode lock, maybe a dedicated one.
	//	As there shouldn't be too much contention, we could also
	//	use atomic_test_and_set(), and free the resources again
	//	when that fails...

	// Get the vnode for the object
	// (note, this does not grab a reference to the node)
	if (vfs_lookup_vnode(mountID, vnodeID, &ref->vnode) != B_OK)
		goto err1;

	// Gets (usually creates) the cache for the node
	if (vfs_get_vnode_cache(ref->vnode, &ref->cache, true) != B_OK)
		goto err1;

	ref->cache->virtual_end = size;
	((VMVnodeCache*)ref->cache)->SetFileCacheRef(ref);
	return ref;

err1:
	delete ref;
	return NULL;
}


/**
 * @brief Destroy a file cache and release the associated VMCache reference.
 *
 * @param _cacheRef Opaque pointer returned by file_cache_create(); silently
 *                  ignored if NULL.
 *
 * @note After this call the cache ref and all pages it owns may be freed by
 *       the VM at any time.  The caller must ensure no concurrent I/O is in
 *       flight on this ref.
 */
extern "C" void
file_cache_delete(void* _cacheRef)
{
	file_cache_ref* ref = (file_cache_ref*)_cacheRef;

	if (ref == NULL)
		return;

	TRACE(("file_cache_delete(ref = %p)\n", ref));

	ref->cache->ReleaseRef();
	delete ref;
}


/**
 * @brief Decrement the disable counter, re-enabling caching when it reaches zero.
 *
 * @param _cacheRef Opaque file cache reference previously disabled with
 *                  file_cache_disable().
 *
 * @note Panics if called more times than file_cache_disable() to catch
 *       unbalanced enable/disable pairs.  Acquires the VMCache lock internally.
 */
extern "C" void
file_cache_enable(void* _cacheRef)
{
	file_cache_ref* ref = (file_cache_ref*)_cacheRef;

	AutoLocker<VMCache> _(ref->cache);

	if (ref->disabled_count == 0) {
		panic("Unbalanced file_cache_enable()!");
		return;
	}

	ref->disabled_count--;
}


/**
 * @brief Disable the page cache for this file, flushing and removing all
 *        cached pages on the first call.
 *
 * @param _cacheRef Opaque file cache reference.
 * @retval B_OK     Caching was disabled (or was already disabled).
 *
 * @note Subsequent calls increment a counter so that nested disable/enable
 *       pairs are supported.  On the first disable, FlushAndRemoveAllPages()
 *       is called which may block while dirty pages are written back.
 *       The VMCache lock is held for the duration.
 */
extern "C" status_t
file_cache_disable(void* _cacheRef)
{
	// TODO: This function only removes all pages from the cache and prevents
	// that the file cache functions add any new ones until re-enabled. The
	// VM (on page fault) can still add pages, if the file is mmap()ed. We
	// should mark the cache to prevent shared mappings of the file and fix
	// the page fault code to deal correctly with private mappings (i.e. only
	// insert pages in consumer caches).

	file_cache_ref* ref = (file_cache_ref*)_cacheRef;

	AutoLocker<VMCache> _(ref->cache);

	// If already disabled, there's nothing for us to do.
	if (ref->disabled_count > 0) {
		ref->disabled_count++;
		return B_OK;
	}

	// The file cache is not yet disabled. We need to evict all cached pages.
	status_t error = ref->cache->FlushAndRemoveAllPages();
	if (error != B_OK)
		return error;

	ref->disabled_count++;
	return B_OK;
}


/**
 * @brief Query whether the file cache is currently active for this ref.
 *
 * @param _cacheRef Opaque file cache reference.
 * @return true if caching is enabled (disabled_count == 0), false otherwise.
 *
 * @note Acquires the VMCache lock to read disabled_count atomically.
 */
extern "C" bool
file_cache_is_enabled(void* _cacheRef)
{
	file_cache_ref* ref = (file_cache_ref*)_cacheRef;
	AutoLocker<VMCache> _(ref->cache);

	return ref->disabled_count == 0;
}


/**
 * @brief Resize the VMCache backing a file cache to reflect a new file size.
 *
 * @param _cacheRef Opaque file cache reference; silently succeeds if NULL.
 * @param newSize   New logical file size in bytes.
 * @retval B_OK     The cache was resized successfully.
 *
 * @note Delegates to VMCache::Resize() with VM_PRIORITY_USER.  Pages beyond
 *       @p newSize are evicted by the cache implementation.  Acquires the
 *       VMCache lock internally.
 */
extern "C" status_t
file_cache_set_size(void* _cacheRef, off_t newSize)
{
	file_cache_ref* ref = (file_cache_ref*)_cacheRef;

	TRACE(("file_cache_set_size(ref = %p, size = %lld)\n", ref, newSize));

	if (ref == NULL)
		return B_OK;

	VMCache* cache = ref->cache;
	AutoLocker<VMCache> _(cache);

	status_t status = cache->Resize(newSize, VM_PRIORITY_USER);
		// Note, the priority doesn't really matter, since this cache doesn't
		// reserve any memory.
	return status;
}


/**
 * @brief Flush all dirty pages of a file cache to the backing store.
 *
 * @param _cacheRef Opaque file cache reference.
 * @retval B_OK        All modified pages were written back successfully.
 * @retval B_BAD_VALUE @p _cacheRef is NULL.
 *
 * @note Delegates to VMCache::WriteModified(), which may block while dirty
 *       pages are written by the page writer.
 */
extern "C" status_t
file_cache_sync(void* _cacheRef)
{
	file_cache_ref* ref = (file_cache_ref*)_cacheRef;
	if (ref == NULL)
		return B_BAD_VALUE;

	return ref->cache->WriteModified();
}


/**
 * @brief Read data from a file through the page cache into a caller-supplied
 *        buffer.
 *
 * @param _cacheRef Opaque file cache reference.
 * @param cookie    VFS cookie forwarded to the I/O path.
 * @param offset    Byte offset in the file to read from; must be >= 0.
 * @param buffer    Destination buffer (kernel or user space).
 * @param _size     In: maximum bytes to read. Out: bytes actually read.
 * @retval B_OK        Read completed (possibly short at end-of-file).
 * @retval B_BAD_VALUE @p offset is negative.
 *
 * @note When caching is disabled (disabled_count > 0) data is read directly
 *       from the vnode via vfs_read_pages(), bypassing the VMCache entirely.
 *       Bounds-checks the request against virtual_end and truncates @p _size
 *       accordingly.
 */
extern "C" status_t
file_cache_read(void* _cacheRef, void* cookie, off_t offset, void* buffer,
	size_t* _size)
{
	file_cache_ref* ref = (file_cache_ref*)_cacheRef;

	TRACE(("file_cache_read(ref = %p, offset = %lld, buffer = %p, size = %lu)\n",
		ref, offset, buffer, *_size));

	// Bounds checking. We do this here so it applies to uncached I/O.
	if (offset < 0)
		return B_BAD_VALUE;
	const off_t fileSize = ref->cache->virtual_end;
	if (offset >= fileSize || *_size == 0) {
		*_size = 0;
		return B_OK;
	}
	if ((off_t)(offset + *_size) > fileSize)
		*_size = fileSize - offset;

	if (ref->disabled_count > 0) {
		// Caching is disabled -- read directly from the file.
		generic_io_vec vec;
		vec.base = (addr_t)buffer;
		generic_size_t size = vec.length = *_size;
		status_t error = vfs_read_pages(ref->vnode, cookie, offset, &vec, 1, 0,
			&size);
		*_size = size;
		return error;
	}

	return cache_io(ref, cookie, offset, (addr_t)buffer, _size, false);
}


/**
 * @brief Write data from a caller-supplied buffer into a file through the page
 *        cache.
 *
 * @param _cacheRef Opaque file cache reference.
 * @param cookie    VFS cookie forwarded to the I/O path.
 * @param offset    Byte offset in the file at which to begin writing.
 * @param buffer    Source buffer (kernel or user space), or NULL to write zeroes.
 * @param _size     In: bytes to write. Out: bytes actually written.
 * @retval B_OK     Write completed successfully.
 *
 * @note No explicit bounds checking is performed here; the calling file system
 *       is expected to have validated and adjusted @p offset and @p _size before
 *       the call.  When caching is disabled data is written directly to the
 *       vnode; a NULL @p buffer triggers write_zeros_to_file().
 */
extern "C" status_t
file_cache_write(void* _cacheRef, void* cookie, off_t offset,
	const void* buffer, size_t* _size)
{
	file_cache_ref* ref = (file_cache_ref*)_cacheRef;

	// We don't do bounds checking here, as we are relying on the
	// file system which called us to already have done that and made
	// adjustments as necessary, unlike in read().

	if (ref->disabled_count > 0) {
		// Caching is disabled -- write directly to the file.
		if (buffer != NULL) {
			generic_io_vec vec;
			vec.base = (addr_t)buffer;
			generic_size_t size = vec.length = *_size;

			status_t error = vfs_write_pages(ref->vnode, cookie, offset, &vec,
				1, 0, &size);
			*_size = size;
			return error;
		}
		return write_zeros_to_file(ref->vnode, cookie, offset, _size);
	}

	status_t status = cache_io(ref, cookie, offset,
		(addr_t)const_cast<void*>(buffer), _size, true);

	TRACE(("file_cache_write(ref = %p, offset = %lld, buffer = %p, size = %lu)"
		" = %ld\n", ref, offset, buffer, *_size, status));

	return status;
}
