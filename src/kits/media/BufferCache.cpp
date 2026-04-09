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
 *   Copyright 2019, Ryan Leavengood.
 *   Copyright 2009, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2002, Marcus Overhagen. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file BufferCache.cpp
 *  @brief A cache for BBuffers received by BBufferConsumer::BufferReceived(). */


//! A cache for BBuffers to be received by BBufferConsumer::BufferReceived().


#include "BufferCache.h"

#include <Buffer.h>

#include "MediaDebug.h"
#include "MediaMisc.h"
#include "SharedBufferList.h"


namespace BPrivate {


/** @brief Default constructor; initialises an empty buffer cache. */
BufferCache::BufferCache()
{
}


/** @brief Destructor; deletes all BBuffer objects held in the cache. */
BufferCache::~BufferCache()
{
	BufferMap::Iterator iterator = fMap.GetIterator();
	while (iterator.HasNext()) {
		BufferMap::Entry entry = iterator.Next();
		delete entry.value.buffer;
	}
}


/** @brief Returns a BBuffer for the given ID, creating and caching it if necessary.
 *
 *  If the buffer is already in the cache it is marked as pending reclaim and
 *  returned directly.  Otherwise a new BBuffer is constructed from the media
 *  server and inserted into the cache.
 *
 *  @param id   The media_buffer_id identifying the desired buffer.
 *  @param port The source port associated with this buffer (used for cache flushing).
 *  @return Pointer to the BBuffer on success, or NULL if creation fails or \a id <= 0. */
BBuffer*
BufferCache::GetBuffer(media_buffer_id id, port_id port)
{
	if (id <= 0)
		return NULL;

	buffer_cache_entry* existing;
	if (fMap.Get(id, existing)) {
		existing->buffer->fFlags |= BUFFER_TO_RECLAIM;
		return existing->buffer;
	}

	buffer_clone_info info;
	info.buffer = id;
	BBuffer* buffer = new(std::nothrow) BBuffer(info);
	if (buffer == NULL || buffer->ID() <= 0
			|| buffer->Data() == NULL) {
		delete buffer;
		return NULL;
	}

	if (buffer->ID() != id)
		debugger("BufferCache::GetBuffer: IDs mismatch");

	buffer_cache_entry entry;
	entry.buffer = buffer;
	entry.port = port;
	status_t error = fMap.Put(id, entry);
	if (error != B_OK) {
		delete buffer;
		return NULL;
	}

	buffer->fFlags |= BUFFER_TO_RECLAIM;
	return buffer;
}


/** @brief Removes and destroys all cached buffers associated with the given port.
 *
 *  If a buffer is still in use (not yet reclaimed) it is marked for deletion
 *  so it will be deleted when it is eventually recycled.
 *
 *  @param port The port whose associated buffers should be flushed from the cache. */
void
BufferCache::FlushCacheForPort(port_id port)
{
	BufferMap::Iterator iterator = fMap.GetIterator();
	while (iterator.HasNext()) {
		BufferMap::Entry entry = iterator.Next();
		if (entry.value.port == port) {
			BBuffer* buffer = entry.value.buffer;
			bool isReclaimed = (buffer->fFlags & BUFFER_TO_RECLAIM) == 0;
			if (isReclaimed && buffer->fBufferList != NULL)
				isReclaimed = buffer->fBufferList->RemoveBuffer(buffer) == B_OK;

			if (isReclaimed)
				delete buffer;
			else {
				// mark the buffer for deletion
				buffer->fFlags |= BUFFER_MARKED_FOR_DELETION;
			}
			// Then remove it from the map
			fMap.Remove(iterator);
		}
	}
}


}	// namespace BPrivate
