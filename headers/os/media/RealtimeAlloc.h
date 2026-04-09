/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from Haiku, Inc. covered by:
 * Copyright 2009, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file RealtimeAlloc.h
 *  @brief Provides a real-time-safe memory allocator for use in media processing threads.
 */

#ifndef _REALTIME_ALLOC_H
#define _REALTIME_ALLOC_H


#include <SupportDefs.h>


#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque handle for a real-time memory pool. */
typedef struct rtm_pool rtm_pool;

#ifdef __cplusplus
/** @brief Creates a new real-time memory pool of the given size.
 *  @param _pool On return, a pointer to the newly created pool handle.
 *  @param totalSize Total size of the pool in bytes.
 *  @param name Optional human-readable name for debugging.
 *  @return B_OK on success, or an error code.
 */
status_t rtm_create_pool(rtm_pool** _pool, size_t totalSize,
	const char* name = NULL);
#else
/** @brief Creates a new real-time memory pool of the given size.
 *  @param _pool On return, a pointer to the newly created pool handle.
 *  @param totalSize Total size of the pool in bytes.
 *  @param name Optional human-readable name for debugging.
 *  @return B_OK on success, or an error code.
 */
status_t rtm_create_pool(rtm_pool** _pool, size_t totalSize, const char* name);
#endif

/** @brief Destroys a real-time memory pool and frees all its resources.
 *  @param pool The pool to destroy.
 *  @return B_OK on success, or an error code.
 */
status_t rtm_delete_pool(rtm_pool* pool);

/** @brief Allocates a block of memory from a real-time pool.
 *  @param pool The pool to allocate from, or NULL for the default pool.
 *  @param size Number of bytes to allocate.
 *  @return Pointer to the allocated block, or NULL on failure.
 */
void* rtm_alloc(rtm_pool* pool, size_t size);

/** @brief Frees a block previously allocated with rtm_alloc().
 *  @param data Pointer to the block to free.
 *  @return B_OK on success, or an error code.
 */
status_t rtm_free(void* data);

/** @brief Resizes a previously allocated real-time block.
 *  @param data In/out: pointer to the block; updated to the new address.
 *  @param new_size New size in bytes.
 *  @return B_OK on success, or an error code.
 */
status_t rtm_realloc(void** data, size_t new_size);

/** @brief Returns the usable size of a previously allocated block.
 *  @param data Pointer to the allocated block.
 *  @return Size in bytes, or an error code.
 */
status_t rtm_size_for(void* data);

/** @brief Returns the physical (page-aligned) size of a previously allocated block.
 *  @param data Pointer to the allocated block.
 *  @return Physical size in bytes, or an error code.
 */
status_t rtm_phys_size_for(void* data);

/** @brief Returns the number of bytes currently available in the pool.
 *  @param pool The pool to query.
 *  @return Available bytes.
 */
size_t rtm_available(rtm_pool* pool);

/** @brief Returns the system-default real-time memory pool.
 *  @return Pointer to the default pool.
 */
rtm_pool* rtm_default_pool();

#ifdef __cplusplus
}
#endif

#endif // _REALTIME_ALLOC_H
