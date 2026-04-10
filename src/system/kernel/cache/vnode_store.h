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
 *   Copyright 2008-2016, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2004-2007, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file vnode_store.h
 *  @brief VMCache subclass that backs a vnode's file_cache with VM pages. */

#ifndef VNODE_STORE_H
#define VNODE_STORE_H


#include <vm/VMCache.h>


struct file_cache_ref;


/** @brief VM cache that stores the contents of a file vnode in pageable memory.
 *
 * One VMVnodeCache is attached per cached vnode. It hosts the page cache that
 * the file_cache layer reads from and writes to, and forwards uncached I/O
 * down to the underlying file system through the file_cache_ref. */
class VMVnodeCache final : public VMCache {
public:
	/** @brief Initialises the cache for the given vnode.
	 *  @param vnode           The vnode whose data this cache will hold.
	 *  @param allocationFlags VM allocation flags forwarded to the base class.
	 *  @return B_OK on success, or an error code from VMCache::Init(). */
			status_t			Init(struct vnode* vnode,
									uint32 allocationFlags);

	/** @brief Commits cache memory up to the given size.
	 *  @param size     Number of bytes that must be committed.
	 *  @param priority Memory pressure priority for the commit attempt.
	 *  @return B_OK on success, or an error code on commit failure. */
	virtual	status_t			Commit(off_t size, int priority);
	/** @brief Returns true if the underlying store holds the page at @p offset. */
	virtual	bool				StoreHasPage(off_t offset);

	/** @brief Reads file data from the backing vnode into the given vectors.
	 *  @param offset    Byte offset in the file.
	 *  @param vecs      I/O vector array to fill.
	 *  @param count     Number of vectors in @p vecs.
	 *  @param flags     I/O flags forwarded to the FS layer.
	 *  @param _numBytes On entry the maximum bytes to read; on return the bytes actually read.
	 *  @return B_OK on success, or an error code from the FS read path. */
	virtual	status_t			Read(off_t offset, const generic_io_vec* vecs,
									size_t count, uint32 flags,
									generic_size_t* _numBytes);
	/** @brief Synchronously writes data through to the backing vnode.
	 *  @see Read */
	virtual	status_t			Write(off_t offset, const generic_io_vec* vecs,
									size_t count, uint32 flags,
									generic_size_t* _numBytes);
	/** @brief Asynchronously writes data through to the backing vnode.
	 *  @param offset   Byte offset in the file.
	 *  @param vecs     I/O vector array describing the data.
	 *  @param count    Number of vectors in @p vecs.
	 *  @param numBytes Total bytes to write across all vectors.
	 *  @param flags    I/O flags forwarded to the FS layer.
	 *  @param callback Callback invoked when the asynchronous write completes.
	 *  @return B_OK if the write was queued, or an error code on failure. */
	virtual	status_t			WriteAsync(off_t offset,
									const generic_io_vec* vecs, size_t count,
									generic_size_t numBytes, uint32 flags,
									AsyncIOCallback* callback);
	/** @brief Returns true if the page at @p offset may be written back to disk. */
	virtual	bool				CanWritePage(off_t offset);

	/** @brief Handles a page fault on @p offset within the given address space.
	 *  @return B_OK if the fault was satisfied, or an error code otherwise. */
	virtual	status_t			Fault(struct VMAddressSpace* aspace,
									off_t offset);

	/** @brief Acquires a reference on the backing store without taking the cache lock.
	 *  @return B_OK if a reference was acquired, or an error code if the vnode is gone. */
	virtual	status_t			AcquireUnreferencedStoreRef();
	/** @brief Acquires a reference on the backing vnode. */
	virtual	void				AcquireStoreRef();
	/** @brief Releases a previously acquired reference on the backing vnode. */
	virtual	void				ReleaseStoreRef();

	/** @brief Dumps cache state to the kernel debugger output.
	 *  @param showPages If true, also lists the pages currently held by the cache. */
	virtual	void				Dump(bool showPages) const;

	/** @brief Stores the back-pointer to the owning file_cache_ref. */
			void				SetFileCacheRef(file_cache_ref* ref)
									{ fFileCacheRef = ref; }
	/** @brief Returns the file_cache_ref this cache is attached to. */
			file_cache_ref*		FileCacheRef() const
									{ return fFileCacheRef; }

	/** @brief Marks the underlying vnode as deleted; cache contents become orphaned. */
			void				VnodeDeleted()	{ fVnodeDeleted = true; }

	/** @brief Returns the device id of the backing file system. */
			dev_t				DeviceId() const
									{ return fDevice; }
	/** @brief Returns the inode number of the backing vnode. */
			ino_t				InodeId() const
									{ return fInode; }

protected:
	/** @brief Releases per-instance resources before the object is freed. */
	virtual	void				DeleteObject();

private:
			struct vnode*		fVnode;          /**< Backing vnode (may become invalid after VnodeDeleted()). */
			file_cache_ref*		fFileCacheRef;   /**< Owning file_cache_ref, used by the file_cache layer. */
			ino_t				fInode;          /**< Cached inode number for debug/identification. */
			dev_t				fDevice;         /**< Cached device id for debug/identification. */
	volatile bool				fVnodeDeleted;   /**< Set when the vnode has been removed from the FS. */
};


#endif	/* VNODE_STORE_H */
