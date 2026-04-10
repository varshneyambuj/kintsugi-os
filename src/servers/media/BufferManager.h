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
 *   Copyright 2002, Marcus Overhagen. All rights reserved.
 *   Copyright 2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file BufferManager.h
 *  @brief Manages shared media buffer registration, cloning, and per-team cleanup. */

#ifndef _BUFFER_MANAGER_H
#define _BUFFER_MANAGER_H

#include <set>

#include <Locker.h>
#include <MediaDefs.h>

#include <HashMap.h>


namespace BPrivate {
	class SharedBufferList;
}


/** @brief Manages shared media buffers across teams using cloned memory areas. */
class BufferManager {
public:
	/** @brief Construct the buffer manager and create the shared buffer list. */
							BufferManager();
	/** @brief Destroy the buffer manager and release the shared buffer list. */
							~BufferManager();

	/** @brief Return the area_id of the shared buffer list. */
			area_id			SharedBufferListArea();

	/** @brief Register an existing buffer by ID and retrieve its properties. */
			status_t		RegisterBuffer(team_id team,
								media_buffer_id bufferID, size_t* _size,
								int32* _flags, size_t* _offset, area_id* _area);

	/** @brief Register a new buffer from a memory area and obtain a buffer ID. */
			status_t		RegisterBuffer(team_id team, size_t size,
								int32 flags, size_t offset, area_id area,
								media_buffer_id* _bufferID);

	/** @brief Unregister a buffer for a given team, deleting it if no teams remain. */
			status_t		UnregisterBuffer(team_id team,
								media_buffer_id bufferID);

	/** @brief Remove all buffers associated with the given team. */
			void			CleanupTeam(team_id team);

	/** @brief Dump the list of registered buffers to stdout. */
			void			Dump();

private:
			area_id			_CloneArea(area_id area);
			void			_ReleaseClonedArea(area_id clone);

private:
	struct clone_info {
		area_id				clone;     /**< Cloned area identifier */
		vint32				ref_count; /**< Reference count for the clone */
	};

	struct buffer_info {
		media_buffer_id		id;     /**< Unique buffer identifier */
		area_id				area;   /**< Cloned memory area for the buffer */
		size_t				offset; /**< Offset within the area */
		size_t				size;   /**< Size of the buffer in bytes */
		int32				flags;  /**< Buffer flags */
		std::set<team_id>	teams;  /**< Teams that have registered this buffer */
	};

	typedef HashMap<HashKey32<media_buffer_id>, buffer_info> BufferInfoMap;
	typedef HashMap<HashKey32<area_id>, clone_info> CloneInfoMap;
	typedef HashMap<HashKey32<area_id>, area_id> SourceInfoMap;

			BPrivate::SharedBufferList* fSharedBufferList;
			area_id			fSharedBufferListArea;
			media_buffer_id	fNextBufferID;
			BLocker			fLocker;
			BufferInfoMap	fBufferInfoMap;
			CloneInfoMap	fCloneInfoMap;
			SourceInfoMap	fSourceInfoMap;
};

#endif // _BUFFER_MANAGER_H
