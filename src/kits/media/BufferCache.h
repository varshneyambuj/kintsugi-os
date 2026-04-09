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
 * MIT License. Copyright 2019, Ryan Leavengood.
 * Copyright 2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2002, Marcus Overhagen. All Rights Reserved.
 */

/** @file BufferCache.h
    @brief Cache mapping media buffer IDs to BBuffer instances and their source ports. */

#ifndef _BUFFER_CACHE_H_
#define _BUFFER_CACHE_H_


#include <HashMap.h>
#include <MediaDefs.h>


class BBuffer;


namespace BPrivate {


/** @brief Associates a BBuffer pointer with the port it was received from. */
struct buffer_cache_entry {
	BBuffer*	buffer;
	port_id		port;
};


/** @brief Thread-safe cache that maps media_buffer_id values to BBuffer objects. */
class BufferCache {
public:
								BufferCache();
								~BufferCache();

			/** @brief Looks up a buffer by its media buffer ID and expected source port.
			    @param id The media buffer identifier to look up.
			    @param port The port ID associated with the buffer.
			    @return Pointer to the BBuffer if found, or NULL. */
			BBuffer*			GetBuffer(media_buffer_id id, port_id port);

			/** @brief Removes all cached buffer entries associated with a given port.
			    @param port The port whose buffers should be evicted from the cache. */
			void				FlushCacheForPort(port_id port);

private:
	typedef HashMap<HashKey32<media_buffer_id>, buffer_cache_entry> BufferMap;

			BufferMap			fMap;
};


}	// namespace BPrivate


#endif	// _BUFFER_CACHE_H_
