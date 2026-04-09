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
 *   Copyright 2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file ChunkCache.cpp
 *  @brief Real-time-memory-backed cache of encoded media chunks for decoder pipeline use. */


#include "ChunkCache.h"

#include <new>
#include <stdlib.h>
#include <string.h>

#include "MediaDebug.h"

// #pragma mark -


/** @brief Constructs the ChunkCache and allocates its real-time memory pool.
 *
 *  @param waitSem   Semaphore released whenever a chunk becomes available or the cache empties.
 *  @param maxBytes  Maximum number of bytes to allocate from the real-time memory pool. */
ChunkCache::ChunkCache(sem_id waitSem, size_t maxBytes)
	:
	BLocker("media chunk cache"),
	fWaitSem(waitSem)
{
	rtm_create_pool(&fRealTimePool, maxBytes, "media chunk cache");
	fMaxBytes = rtm_available(fRealTimePool);
}


/** @brief Destructor; frees the real-time memory pool and all chunk buffers within it. */
ChunkCache::~ChunkCache()
{
	rtm_delete_pool(fRealTimePool);
}


/** @brief Returns B_OK if the real-time pool was successfully allocated, or B_NO_MEMORY.
 *  @return B_OK on success, B_NO_MEMORY if pool creation failed. */
status_t
ChunkCache::InitCheck() const
{
	if (fRealTimePool == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


/** @brief Discards all cached chunks and releases the wait semaphore.
 *
 *  The cache lock must be held by the caller (ASSERT(IsLocked())). */
void
ChunkCache::MakeEmpty()
{
	ASSERT(IsLocked());

	while (!fChunkCache.empty()) {
		RecycleChunk(fChunkCache.front());
		fChunkCache.pop();
	}

	release_sem(fWaitSem);
}


/** @brief Returns true if there is room in the cache to store another chunk.
 *
 *  The cache lock must be held by the caller (ASSERT(IsLocked())).
 *
 *  @return true if the entry count and pool memory allow an additional chunk. */
bool
ChunkCache::SpaceLeft() const
{
	ASSERT(IsLocked());

	if (fChunkCache.size() >= CACHE_MAX_ENTRIES) {
		return false;
	}

	// If there is no more memory we are likely to fail soon after
	return sizeof(chunk_buffer) + 2048 < rtm_available(fRealTimePool);
}


/** @brief Returns the next available chunk, reading from the reader if the cache is empty.
 *
 *  If no chunk is queued, attempts a synchronous read via ReadNextChunk() and
 *  recurses once.  When a cached chunk is returned the wait semaphore is released
 *  to signal that space is now available.
 *
 *  The cache lock must be held by the caller (ASSERT(IsLocked())).
 *
 *  @param reader  The Reader used to fetch new chunk data when the cache is empty.
 *  @param cookie  Opaque cookie passed through to Reader::GetNextChunk().
 *  @return Pointer to the next chunk_buffer, or NULL if no data is available. */
chunk_buffer*
ChunkCache::NextChunk(Reader* reader, void* cookie)
{
	ASSERT(IsLocked());

	chunk_buffer* chunk = NULL;

	if (fChunkCache.empty()) {
		TRACE("ChunkCache is empty, going direct to reader\n");
		if (ReadNextChunk(reader, cookie)) {
			return NextChunk(reader, cookie);
		}
	} else {
		chunk = fChunkCache.front();
		fChunkCache.pop();

		release_sem(fWaitSem);
	}

	return chunk;
}


/** @brief Moves a chunk back to the unused list so its memory can be reused.
 *
 *  The cache lock must be held by the caller (ASSERT(IsLocked())).
 *
 *  @param chunk The chunk_buffer to recycle; its data buffer is freed back to the pool. */
void
ChunkCache::RecycleChunk(chunk_buffer* chunk)
{
	ASSERT(IsLocked());

	rtm_free(chunk->buffer);
	chunk->capacity = 0;
	chunk->size = 0;
	chunk->buffer = NULL;
	fUnusedChunks.push_back(chunk);
}


/** @brief Reads the next chunk from the reader into the cache.
 *
 *  Allocates or reuses a chunk_buffer entry and grows its data buffer as
 *  needed.  The chunk is pushed onto the cache queue regardless of read
 *  status so the caller can inspect the error code.
 *
 *  The cache lock must be held by the caller (ASSERT(IsLocked())).
 *
 *  @param reader The Reader to call GetNextChunk() on.
 *  @param cookie Opaque cookie forwarded to Reader::GetNextChunk().
 *  @return true if the chunk was read successfully (status == B_OK), false otherwise. */
bool
ChunkCache::ReadNextChunk(Reader* reader, void* cookie)
{
	ASSERT(IsLocked());

	// retrieve chunk buffer
	chunk_buffer* chunk = NULL;
	if (fUnusedChunks.empty()) {
		// allocate a new one
		chunk = (chunk_buffer*)rtm_alloc(fRealTimePool, sizeof(chunk_buffer));
		if (chunk == NULL) {
			ERROR("RTM Pool empty allocating chunk buffer structure");
			return false;
		}

		chunk->size = 0;
		chunk->capacity = 0;
		chunk->buffer = NULL;

	} else {
		chunk = fUnusedChunks.front();
		fUnusedChunks.pop_front();
	}

	const void* buffer;
	size_t bufferSize;
	chunk->status = reader->GetNextChunk(cookie, &buffer, &bufferSize,
		&chunk->header);
	if (chunk->status == B_OK) {
		if (chunk->capacity < bufferSize) {
			// adapt buffer size
			rtm_free(chunk->buffer);
			chunk->capacity = (bufferSize + 2047) & ~2047;
			chunk->buffer = rtm_alloc(fRealTimePool, chunk->capacity);
			if (chunk->buffer == NULL) {
				rtm_free(chunk);
				ERROR("RTM Pool empty allocating chunk buffer\n");
				return false;
			}
		}

		memcpy(chunk->buffer, buffer, bufferSize);
		chunk->size = bufferSize;
	}

	fChunkCache.push(chunk);
	return chunk->status == B_OK;
}
