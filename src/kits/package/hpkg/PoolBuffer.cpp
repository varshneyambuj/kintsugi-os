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
 * @file PoolBuffer.cpp
 * @brief Fixed-size heap buffer managed by BBufferPool.
 *
 * PoolBuffer wraps a single malloc'd block of memory together with pool
 * ownership metadata. Instances are checked out from and returned to a
 * BBufferPool; the pool tracks whether each buffer is currently in use via
 * the fCached flag.
 *
 * @see BBufferPool, PoolBufferPutter
 */


#include <package/hpkg/PoolBuffer.h>

#include <stdlib.h>


namespace BPackageKit {

namespace BHPKG {

namespace BPrivate {


/**
 * @brief Constructs a PoolBuffer and allocates its backing memory.
 *
 * The buffer is initially marked as not cached (i.e., in active use).
 * If malloc() fails, fBuffer will be NULL; callers should check this.
 *
 * @param size Number of bytes to allocate for the buffer payload.
 */
PoolBuffer::PoolBuffer(size_t size)
	:
	fOwner(NULL),
	fBuffer(malloc(size)),
	fSize(size),
	fCached(false)
{
}


/**
 * @brief Destroys the PoolBuffer and releases the backing memory.
 */
PoolBuffer::~PoolBuffer()
{
	free(fBuffer);
}


}	// namespace BPrivate

}	// namespace BHPKG

}	// namespace BPackageKit
