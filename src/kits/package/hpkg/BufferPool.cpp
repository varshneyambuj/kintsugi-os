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
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BufferPool.cpp
 * @brief Abstract base classes for HPKG buffer pool management.
 *
 * Defines the virtual destructors for BBufferPool and BBufferPoolLockable,
 * anchoring their vtables and ensuring correct polymorphic destruction.
 * Concrete buffer pool implementations derive from these interfaces to
 * provide memory management and thread-safety strategies.
 *
 * @see BBlockBufferPool, BBlockBufferPoolNoLock
 */


#include <package/hpkg/BufferPool.h>


namespace BPackageKit {

namespace BHPKG {


/**
 * @brief Virtual destructor for BBufferPool.
 *
 * Ensures that derived pool implementations are properly destroyed when
 * deleted through a base-class pointer.
 */
BBufferPool::~BBufferPool()
{
}


/**
 * @brief Virtual destructor for BBufferPoolLockable.
 *
 * Ensures that derived lockable implementations are properly destroyed when
 * deleted through a base-class pointer.
 */
BBufferPoolLockable::~BBufferPoolLockable()
{
}


}	// namespace BHPKG

}	// namespace BPackageKit
