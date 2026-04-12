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
 *   Copyright 2011, Oliver Tappe <zooey@hirschkaefer.de>
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BlockBufferPool.cpp
 * @brief Public facade for the fixed-size block buffer pool.
 *
 * BBlockBufferPool manages a pool of reusable fixed-size memory buffers used
 * during HPKG heap decompression. It delegates all internal work to
 * BlockBufferPoolImpl, which provides the actual caching and allocation logic.
 * Subclasses must supply the locking mechanism via BBufferPoolLockable.
 *
 * @see BBlockBufferPoolNoLock, BlockBufferPoolImpl, BBufferPool
 */


#include <package/hpkg/BlockBufferPool.h>

#include <new>

#include <package/hpkg/BlockBufferPoolImpl.h>


namespace BPackageKit {

namespace BHPKG {


/**
 * @brief Construct the pool with the given block size and cache limit.
 *
 * Allocates the internal BlockBufferPoolImpl. If allocation fails, Init()
 * will later return B_NO_MEMORY.
 *
 * @param blockSize       Size in bytes of each fixed-size buffer block.
 * @param maxCachedBlocks Maximum number of blocks to keep in the idle cache.
 */
BBlockBufferPool::BBlockBufferPool(size_t blockSize, uint32 maxCachedBlocks)
	:
	fImpl(new (std::nothrow) BlockBufferPoolImpl(blockSize, maxCachedBlocks,
		this))
{
}


/**
 * @brief Destroy the pool and release all cached buffers.
 */
BBlockBufferPool::~BBlockBufferPool()
{
	delete fImpl;
}


/**
 * @brief Initialize the pool so it is ready for use.
 *
 * Must be called after construction before GetBuffer() or PutBuffer().
 *
 * @return B_OK on success, B_NO_MEMORY if the implementation was not
 *         allocated during construction.
 */
status_t
BBlockBufferPool::Init()
{
	if (fImpl == NULL)
		return B_NO_MEMORY;

	return fImpl->Init();
}


/**
 * @brief Obtain a buffer of at least \a size bytes from the pool.
 *
 * If \a owner is non-NULL and already points to a cached buffer, that
 * buffer is returned directly without allocating a new one.  Otherwise a
 * buffer is taken from the idle list, stolen from the cache if the limit is
 * reached, or newly allocated.
 *
 * @param size       Minimum required buffer size in bytes.
 * @param owner      Optional pointer-to-pointer tracking buffer ownership.
 *                   Updated to point to the returned buffer when non-NULL.
 * @param _newBuffer Optional output flag set to true when a freshly
 *                   allocated buffer is returned, false when recycled.
 * @return Pointer to the obtained PoolBuffer, or NULL on failure.
 */
PoolBuffer*
BBlockBufferPool::GetBuffer(size_t size, PoolBuffer** owner,
	bool* _newBuffer)
{
	if (fImpl == NULL)
		return NULL;

	return fImpl->GetBuffer(size, owner, _newBuffer);
}


/**
 * @brief Return a buffer to the pool and keep it in the cache for reuse.
 *
 * The buffer remains associated with \a owner so it can be retrieved cheaply
 * on the next GetBuffer() call.  If the pool is over the cached-block limit
 * an excess buffer is freed to bring the count back within bounds.
 *
 * @param owner Pointer-to-pointer that identifies and holds the buffer;
 *              must not be NULL.
 */
void
BBlockBufferPool::PutBufferAndCache(PoolBuffer** owner)
{
	if (fImpl != NULL)
		fImpl->PutBufferAndCache(owner);
}


/**
 * @brief Return a buffer to the pool without caching it.
 *
 * The buffer is either placed on the idle list (if within the block limit)
 * or freed immediately.  The pointer at \a owner is set to NULL.
 *
 * @param owner Pointer-to-pointer that identifies and holds the buffer.
 */
void
BBlockBufferPool::PutBuffer(PoolBuffer** owner)
{
	if (fImpl != NULL)
		fImpl->PutBuffer(owner);
}


}	// namespace BHPKG

}	// namespace BPackageKit
