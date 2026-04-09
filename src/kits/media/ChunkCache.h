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
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2009, Axel Dörfler, axeld@pinc-software.de.
 */

/** @file ChunkCache.h
    @brief Bounded real-time chunk cache used by media track decoders. */

#ifndef _CHUNK_CACHE_H
#define _CHUNK_CACHE_H


#include <Locker.h>
#include <MediaDefs.h>
#include <RealtimeAlloc.h>
#include <queue>
#include <deque>

#include "ReaderPlugin.h"


namespace BPrivate {
namespace media {

/** @brief Maximum number of chunk_buffer entries held in the cache at once. */
// Limit to 10 entries, we might want to instead limit to a length of time
#define CACHE_MAX_ENTRIES 10

/** @brief A single raw data chunk read from a media stream, including its header and status. */
struct chunk_buffer {
	void*			buffer;
	size_t			size;
	size_t			capacity;
	media_header	header;
	status_t		status;
};

/** @brief FIFO queue of chunk_buffer pointers awaiting consumption. */
typedef std::queue<chunk_buffer*> ChunkQueue;

/** @brief Double-ended list of chunk_buffer pointers available for reuse. */
typedef std::deque<chunk_buffer*> ChunkList;

/** @brief Thread-safe, size-bounded cache of raw media chunks backed by a real-time pool. */
class ChunkCache : public BLocker {
public:
								ChunkCache(sem_id waitSem, size_t maxBytes);
								~ChunkCache();

			/** @brief Returns B_OK if the cache was successfully initialised.
			    @return B_OK on success, or an error code. */
			status_t			InitCheck() const;

			/** @brief Discards all cached chunks and returns their buffers to the free list. */
			void				MakeEmpty();

			/** @brief Indicates whether the cache has room for at least one more chunk.
			    @return true if space is available, false if the cache is full. */
			bool				SpaceLeft() const;

			/** @brief Returns the next available chunk, blocking until one is ready.
			    @param reader The Reader that supplies raw data.
			    @param cookie Opaque cookie passed to the reader.
			    @return Pointer to the next chunk_buffer, or NULL on error. */
			chunk_buffer*		NextChunk(Reader* reader, void* cookie);

			/** @brief Returns a chunk_buffer to the free pool after it has been processed.
			    @param chunk The chunk_buffer to recycle. */
			void				RecycleChunk(chunk_buffer* chunk);

			/** @brief Reads one chunk from the reader into the cache.
			    @param reader The Reader that supplies raw data.
			    @param cookie Opaque cookie passed to the reader.
			    @return true if a chunk was successfully enqueued. */
			bool				ReadNextChunk(Reader* reader, void* cookie);

private:
			rtm_pool*			fRealTimePool;
			sem_id				fWaitSem;
			size_t				fMaxBytes;
			ChunkQueue			fChunkCache;
			ChunkList			fUnusedChunks;
};


}	// namespace media
}	// namespace BPrivate

using namespace BPrivate::media;

#endif	// _CHUNK_CACHE_H
