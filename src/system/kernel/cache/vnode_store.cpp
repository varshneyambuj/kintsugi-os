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
 *   Copyright 2004-2007, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file vnode_store.cpp
 * @brief VMCache backing store implementation for file-backed vnodes.
 *
 * Connects the VM page cache to the VFS layer. When a page fault occurs on
 * a file-backed mapping, vnode_store reads the page from the file's vnode.
 * Also handles writing dirty pages back to the vnode on memory pressure.
 *
 * @see file_cache.cpp, VMCache.cpp
 */

#include "vnode_store.h"

#include <stdlib.h>
#include <string.h>

#include <file_cache.h>
#include <slab/Slab.h>
#include <vfs.h>
#include <vm/vm.h>

#include "IORequest.h"


/**
 * @brief Initialises a VMVnodeCache instance and binds it to a vnode.
 *
 * Sets up the underlying VMCache with type CACHE_TYPE_VNODE, stores the
 * vnode pointer, and records the device/inode pair for later reference
 * acquisition via vfs_get_vnode().
 *
 * @param vnode           The vnode that backs this cache.
 * @param allocationFlags Slab allocation flags forwarded to VMCache::Init().
 * @return B_OK on success, or a negative error code if VMCache::Init() fails.
 */
status_t
VMVnodeCache::Init(struct vnode* vnode, uint32 allocationFlags)
{
	status_t error = VMCache::Init("VMVnodeCache", CACHE_TYPE_VNODE, allocationFlags);
	if (error != B_OK)
		return error;

	fVnode = vnode;
	fFileCacheRef = NULL;
	fVnodeDeleted = false;

	vfs_vnode_to_node_ref(fVnode, &fDevice, &fInode);

	return B_OK;
}


/**
 * @brief No-op memory commitment for vnode caches.
 *
 * Vnode-backed pages are populated on demand from the file system, so no
 * swap or physical memory reservation is required at commit time.
 *
 * @param size     Requested committed size (ignored).
 * @param priority Allocation priority (ignored).
 * @return Always returns B_OK.
 *
 * @note Mapped pages that cannot be stolen by the page daemon may require
 *       a real commitment in a future implementation.
 */
status_t
VMVnodeCache::Commit(off_t size, int priority)
{
	// We don't need to commit memory here.
	// TODO: We do need to commit memory when pages are mapped, though,
	// as mapped pages can't be stolen like CACHED ones can.
	// (When the system is low on memory, the page daemon will unmap
	// unused pages, and they can be decommitted then.)
	return B_OK;
}


/**
 * @brief Tests whether a given file offset falls within the cache's virtual range.
 *
 * The offset is considered valid when it is page-aligned-up within
 * [virtual_base, virtual_end).
 *
 * @param offset Byte offset within the file to test.
 * @return @c true if the offset is backed by this store, @c false otherwise.
 */
bool
VMVnodeCache::StoreHasPage(off_t offset)
{
	return ROUNDUP(offset, B_PAGE_SIZE) >= virtual_base
		&& offset < virtual_end;
}


/**
 * @brief Reads pages from the backing vnode into the supplied I/O vectors.
 *
 * Delegates to vfs_read_pages() and then zeroes any portion of the vectors
 * that was not filled by the file system (e.g., beyond the end of file), so
 * that callers always receive fully initialised pages.
 *
 * @param offset    Byte offset within the file at which to start reading.
 * @param vecs      Array of generic I/O vectors describing the destination buffers.
 * @param count     Number of entries in @p vecs.
 * @param flags     I/O flags (e.g., B_PHYSICAL_IO_REQUEST).
 * @param _numBytes In: total bytes requested. Out: bytes actually transferred.
 * @return B_OK on success, or a VFS/driver error code on failure.
 *
 * @note Unfilled bytes at the tail of each vector are zeroed with
 *       vm_memset_physical() for physical requests or memset() otherwise.
 */
status_t
VMVnodeCache::Read(off_t offset, const generic_io_vec* vecs, size_t count,
	uint32 flags, generic_size_t* _numBytes)
{
	generic_size_t bytesUntouched = *_numBytes;

	status_t status = vfs_read_pages(fVnode, NULL, offset, vecs, count,
		flags, _numBytes);

	generic_size_t bytesEnd = *_numBytes;

	if (offset + (off_t)bytesEnd > virtual_end)
		bytesEnd = virtual_end - offset;

	// If the request could be filled completely, or an error occured,
	// we're done here
	if (status != B_OK || bytesUntouched == bytesEnd)
		return status;

	bytesUntouched -= bytesEnd;

	// Clear out any leftovers that were not touched by the above read - we're
	// doing this here so that not every file system/device has to implement
	// this
	for (int32 i = count; i-- > 0 && bytesUntouched != 0;) {
		generic_size_t length = min_c(bytesUntouched, vecs[i].length);

		generic_addr_t address = vecs[i].base + vecs[i].length - length;
		if ((flags & B_PHYSICAL_IO_REQUEST) != 0)
			vm_memset_physical(address, 0, length);
		else
			memset((void*)(addr_t)address, 0, length);

		bytesUntouched -= length;
	}

	return B_OK;
}


/**
 * @brief Writes dirty pages from the supplied I/O vectors back to the vnode.
 *
 * Delegates directly to vfs_write_pages() with no additional processing.
 *
 * @param offset    Byte offset within the file at which to start writing.
 * @param vecs      Array of generic I/O vectors describing the source buffers.
 * @param count     Number of entries in @p vecs.
 * @param flags     I/O flags forwarded to the VFS layer.
 * @param _numBytes In: total bytes to write. Out: bytes actually written.
 * @return B_OK on success, or a VFS/driver error code on failure.
 */
status_t
VMVnodeCache::Write(off_t offset, const generic_io_vec* vecs, size_t count,
	uint32 flags, generic_size_t* _numBytes)
{
	return vfs_write_pages(fVnode, NULL, offset, vecs, count, flags, _numBytes);
}


/**
 * @brief Submits an asynchronous write of dirty pages to the vnode.
 *
 * Issues a non-blocking write via vfs_asynchronous_write_pages(). The
 * supplied @p callback is invoked when the operation completes.
 *
 * @param offset    Byte offset within the file at which to start writing.
 * @param vecs      Array of generic I/O vectors describing the source buffers.
 * @param count     Number of entries in @p vecs.
 * @param numBytes  Total number of bytes to write.
 * @param flags     I/O flags forwarded to the VFS layer.
 * @param callback  Completion callback invoked by the I/O subsystem.
 * @return B_OK if the request was successfully submitted, or an error code.
 */
status_t
VMVnodeCache::WriteAsync(off_t offset, const generic_io_vec* vecs, size_t count,
	generic_size_t numBytes, uint32 flags, AsyncIOCallback* callback)
{
	return vfs_asynchronous_write_pages(fVnode, NULL, offset, vecs, count,
		numBytes, flags, callback);
}


/**
 * @brief Handles a page fault for a vnode-backed mapping.
 *
 * Verifies that the faulting @p offset is within the store's valid range and
 * returns B_BAD_HANDLER to direct vm_soft_fault() to perform the actual page
 * read. Returns B_BAD_ADDRESS if the offset is out of range.
 *
 * @param aspace  The address space in which the fault occurred.
 * @param offset  The file offset that triggered the fault.
 * @retval B_BAD_HANDLER The offset is valid; vm_soft_fault() should handle it.
 * @retval B_BAD_ADDRESS The offset is outside the store's virtual range.
 */
status_t
VMVnodeCache::Fault(struct VMAddressSpace* aspace, off_t offset)
{
	if (!StoreHasPage(offset))
		return B_BAD_ADDRESS;

	// vm_soft_fault() reads the page in.
	return B_BAD_HANDLER;
}


/**
 * @brief Reports whether a page at the given offset is eligible for writeback.
 *
 * All pages in a vnode-backed cache can be written back to the file system,
 * so this always returns @c true.
 *
 * @param offset Byte offset of the page within the file (unused).
 * @return Always @c true.
 */
bool
VMVnodeCache::CanWritePage(off_t offset)
{
	// all pages can be written
	return true;
}


/**
 * @brief Acquires a store reference when the vnode may have been deleted.
 *
 * Performs a full vfs_get_vnode() to safely obtain a counted reference to the
 * backing vnode. If the vnode has been marked deleted between the fast-path
 * check and the vfs_get_vnode() call, the acquired reference is immediately
 * released and B_BUSY is returned.
 *
 * @return B_OK if a reference was acquired successfully.
 * @retval B_BUSY  The vnode has been deleted or is in the process of deletion.
 *
 * @note This is more expensive than AcquireStoreRef() because it goes through
 *       the vnode lookup path; use it only when the caller cannot guarantee
 *       the vnode is still alive.
 */
status_t
VMVnodeCache::AcquireUnreferencedStoreRef()
{
	// Quick check whether getting a vnode reference is still allowed. Only
	// after a successful vfs_get_vnode() the check is safe (since then we've
	// either got the reference to our vnode, or have been notified that it is
	// toast), but the check is cheap and saves quite a bit of work in case the
	// condition holds.
	if (fVnodeDeleted)
		return B_BUSY;

	struct vnode* vnode;
	status_t status = vfs_get_vnode(fDevice, fInode, false, &vnode);

	// If successful, update the store's vnode pointer, so that release_ref()
	// won't use a stale pointer.
	if (status == B_OK && fVnodeDeleted) {
		vfs_put_vnode(vnode);
		status = B_BUSY;
	}

	return status;
}


/**
 * @brief Increments the reference count of the backing vnode.
 *
 * Calls vfs_acquire_vnode() to prevent the vnode from being freed while the
 * store holds a reference. Paired with ReleaseStoreRef().
 */
void
VMVnodeCache::AcquireStoreRef()
{
	vfs_acquire_vnode(fVnode);
}


/**
 * @brief Decrements the reference count of the backing vnode.
 *
 * Calls vfs_put_vnode() to release the reference obtained by
 * AcquireStoreRef() or AcquireUnreferencedStoreRef(). The vnode may be
 * freed by the VFS layer once its reference count reaches zero.
 */
void
VMVnodeCache::ReleaseStoreRef()
{
	vfs_put_vnode(fVnode);
}


/**
 * @brief Prints cache state to the kernel debugger.
 *
 * Extends the base VMCache::Dump() output with the vnode pointer and its
 * device/inode identifiers, aiding post-mortem analysis.
 *
 * @param showPages If @c true, also dumps the individual cached pages.
 */
void
VMVnodeCache::Dump(bool showPages) const
{
	VMCache::Dump(showPages);

	kprintf("  vnode:        %p <%" B_PRIdDEV ", %" B_PRIdINO ">\n", fVnode,
		fDevice, fInode);
}


/**
 * @brief Returns this object to the vnode cache slab allocator.
 *
 * Called by the VMCache reference-counting machinery when the last reference
 * is dropped. Delegates to object_cache_delete() rather than the global
 * operator delete so that the slab statistics remain accurate.
 */
void
VMVnodeCache::DeleteObject()
{
	object_cache_delete(gVnodeCacheObjectCache, this);
}
