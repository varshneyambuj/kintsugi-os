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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2011, Oliver Tappe <zooey@hirschkaefer.de>
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BlockBufferPoolImpl.cpp
 * @brief Internal implementation of the fixed-size block buffer pool.
 *
 * BlockBufferPoolImpl maintains two intrusive lists of PoolBuffer objects:
 * a cache of buffers that are still associated with an owner, and an idle
 * list of unowned buffers available for immediate reuse.  Locking is
 * delegated to the BBufferPoolLockable supplied at construction time,
 * allowing the same logic to be used with or without mutual exclusion.
 *
 * @see BBlockBufferPool, BBlockBufferPoolNoLock, PoolBuffer
 */


#include <package/hpkg/BlockBufferPoolImpl.h>

#include <algorithm>
#include <new>

#include <AutoLocker.h>

#include <package/hpkg/PoolBuffer.h>


namespace BPackageKit {

namespace BHPKG {

namespace BPrivate {


// #pragma mark - BlockBufferPoolImpl


/**
 * @brief Construct the pool implementation.
 *
 * @param blockSize       Nominal size of each buffer block.  Requests smaller
 *                        than or equal to this value are served from the cache.
 * @param maxCachedBlocks Upper bound on the total number of simultaneously
 *                        allocated blocks before older ones are evicted.
 * @param lockable        The locking object used to protect shared state;
 *                        must remain valid for the lifetime of this object.
 */
BlockBufferPoolImpl::BlockBufferPoolImpl(size_t blockSize,
	uint32 maxCachedBlocks, BBufferPoolLockable* lockable)
	:
	fBlockSize(blockSize),
	fMaxCachedBlocks(maxCachedBlocks),
	fAllocatedBlocks(0),
	fLockable(lockable)
{
}


/**
 * @brief Destroy the pool and free all outstanding buffers.
 *
 * Iterates and deletes every buffer remaining in both the cached and idle
 * lists.
 */
BlockBufferPoolImpl::~BlockBufferPoolImpl()
{
	// delete all cached blocks
	while (PoolBuffer* block = fCachedBuffers.RemoveHead())
		delete block;

	while (PoolBuffer* block = fUnusedBuffers.RemoveHead())
		delete block;
}


/**
 * @brief Perform any deferred initialisation of the pool.
 *
 * @return B_OK unconditionally; provided for API symmetry with BBlockBufferPool.
 */
status_t
BlockBufferPoolImpl::Init()
{
	return B_OK;
}


/**
 * @brief Obtain a buffer large enough to hold \a size bytes.
 *
 * Oversized requests (larger than the nominal block size) are always
 * individually allocated.  For normal-sized requests the pool tries, in
 * order: the owner's previously cached buffer, the idle list, a stolen
 * cached buffer (when at the limit), and finally a fresh allocation.
 *
 * @param size       Minimum required capacity in bytes.
 * @param owner      Optional pointer-to-pointer tracking buffer ownership.
 * @param _newBuffer Optional output flag; set to true for a freshly allocated
 *                   buffer, false when a cached buffer is returned.
 * @return Pointer to the obtained PoolBuffer, or NULL on allocation failure.
 */
PoolBuffer*
BlockBufferPoolImpl::GetBuffer(size_t size, PoolBuffer** owner, bool* _newBuffer)
{
	// for sizes greater than the block size, we always allocate a new buffer
	if (size > fBlockSize)
		return _AllocateBuffer(size, owner, _newBuffer);

	AutoLocker<BBufferPoolLockable> locker(fLockable);

	// if an owner is given and the buffer is still cached, return it
	if (owner != NULL && *owner != NULL) {
		PoolBuffer* buffer = *owner;
		fCachedBuffers.Remove(buffer);

		if (_newBuffer != NULL)
			*_newBuffer = false;
		return buffer;
	}

	// we need a new buffer -- try unused ones first
	PoolBuffer* buffer = fUnusedBuffers.RemoveHead();
	if (buffer != NULL) {
		buffer->SetOwner(owner);

		if (owner != NULL)
			*owner = buffer;
		if (_newBuffer != NULL)
			*_newBuffer = true;
		return buffer;
	}

	// if we have already hit the max block limit, steal a cached block
	if (fAllocatedBlocks >= fMaxCachedBlocks) {
		buffer = fCachedBuffers.RemoveHead();
		if (buffer != NULL) {
			buffer->SetCached(false);
			*buffer->Owner() = NULL;
			buffer->SetOwner(owner);

			if (owner != NULL)
				*owner = buffer;
			if (_newBuffer != NULL)
				*_newBuffer = true;
			return buffer;
		}
	}

	// allocate a new buffer
	locker.Unlock();
	return _AllocateBuffer(size, owner, _newBuffer);
}


/**
 * @brief Return a buffer to the pool and mark it as cached.
 *
 * The buffer is queued in the cached list so that the same owner can
 * reclaim it cheaply.  If the pool exceeds the maximum block count an
 * excess buffer is freed immediately.  Buffers whose size does not match
 * the nominal block size are deleted outright.
 *
 * @param owner Pointer-to-pointer identifying the buffer to return;
 *              must not be NULL.
 */
void
BlockBufferPoolImpl::PutBufferAndCache(PoolBuffer** owner)
{
	PoolBuffer* buffer = *owner;

	// always delete buffers with non-standard size
	if (buffer->Size() != fBlockSize) {
		*owner = NULL;
		delete buffer;
		return;
	}

	AutoLocker<BBufferPoolLockable> locker(fLockable);

	// queue the cached buffer
	buffer->SetOwner(owner);
	fCachedBuffers.Add(buffer);
	buffer->SetCached(true);

	if (fAllocatedBlocks > fMaxCachedBlocks) {
		// We have exceeded the limit -- we need to free a buffer.
		PoolBuffer* otherBuffer = fUnusedBuffers.RemoveHead();
		if (otherBuffer == NULL) {
			otherBuffer = fCachedBuffers.RemoveHead();
			*otherBuffer->Owner() = NULL;
			otherBuffer->SetCached(false);
		}

		delete otherBuffer;
	}
}


/**
 * @brief Return a buffer to the pool without caching it.
 *
 * If the buffer is currently in the cached list it is removed first.  A
 * standard-sized buffer within the block limit is placed on the idle list;
 * otherwise it is deleted.  The pointer at \a owner is set to NULL.
 *
 * @param owner Pointer-to-pointer identifying the buffer to return.
 */
void
BlockBufferPoolImpl::PutBuffer(PoolBuffer** owner)
{
	AutoLocker<BBufferPoolLockable> locker(fLockable);

	PoolBuffer* buffer = *owner;

	if (buffer == NULL)
		return;

	if (buffer->IsCached()) {
		fCachedBuffers.Remove(buffer);
		buffer->SetCached(false);
	}

	buffer->SetOwner(NULL);
	*owner = NULL;

	if (buffer->Size() == fBlockSize && fAllocatedBlocks < fMaxCachedBlocks)
		fUnusedBuffers.Add(buffer);
	else
		delete buffer;
}


/**
 * @brief Allocate a new PoolBuffer of at least \a size bytes.
 *
 * The buffer is sized to the maximum of \a size and the nominal block size.
 * The allocated-block count is incremented under the lock before returning.
 *
 * @param size       Minimum capacity of the new buffer.
 * @param owner      Optional pointer-to-pointer to associate with the buffer.
 * @param _newBuffer Optional output flag; always set to true on success.
 * @return Pointer to the newly allocated PoolBuffer, or NULL on failure.
 */
PoolBuffer*
BlockBufferPoolImpl::_AllocateBuffer(size_t size, PoolBuffer** owner,
	bool* _newBuffer)
{
	PoolBuffer* buffer = new(std::nothrow) PoolBuffer(
		std::max(size, fBlockSize));
	if (buffer == NULL || buffer->Buffer() == NULL) {
		delete buffer;
		return NULL;
	}

	buffer->SetOwner(owner);

	if (_newBuffer != NULL)
		*_newBuffer = true;

	AutoLocker<BBufferPoolLockable> locker(fLockable);
	fAllocatedBlocks++;

	if (owner != NULL)
		*owner = buffer;

	return buffer;
}


}	// namespace BPrivate

}	// namespace BHPKG

}	// namespace BPackageKit
