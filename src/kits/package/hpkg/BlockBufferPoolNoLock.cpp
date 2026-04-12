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
 * @file BlockBufferPoolNoLock.cpp
 * @brief A BBlockBufferPool variant that performs no locking.
 *
 * BBlockBufferPoolNoLock satisfies the BBufferPoolLockable interface with
 * no-op Lock() and Unlock() operations, making it suitable for single-threaded
 * contexts where the overhead of synchronisation is unnecessary.
 *
 * @see BBlockBufferPool, BlockBufferPoolImpl
 */


#include <package/hpkg/BlockBufferPoolNoLock.h>


namespace BPackageKit {

namespace BHPKG {


/**
 * @brief Construct a no-lock buffer pool with the given parameters.
 *
 * @param blockSize       Nominal size of each buffer block in bytes.
 * @param maxCachedBlocks Maximum number of simultaneously allocated blocks.
 */
BBlockBufferPoolNoLock::BBlockBufferPoolNoLock(size_t blockSize,
	uint32 maxCachedBlocks)
	:
	BBlockBufferPool(blockSize, maxCachedBlocks)
{
}


/**
 * @brief Destroy the no-lock pool and release all resources.
 */
BBlockBufferPoolNoLock::~BBlockBufferPoolNoLock()
{
}


/**
 * @brief Acquire the pool lock (no-op in this implementation).
 *
 * @return true unconditionally, indicating that the lock was "acquired".
 */
bool
BBlockBufferPoolNoLock::Lock()
{
	return true;
}


/**
 * @brief Release the pool lock (no-op in this implementation).
 */
void
BBlockBufferPoolNoLock::Unlock()
{
}


}	// namespace BHPKG

}	// namespace BPackageKit
