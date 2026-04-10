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

/** @file BufferManager.cpp
 *  @brief Implementation of shared media buffer registration, cloning, and cleanup. */


#include "BufferManager.h"

#include <Autolock.h>

#include "MediaDebug.h"
#include "SharedBufferList.h"


/**
 * @brief Constructs the buffer manager and creates the shared buffer list area.
 */
BufferManager::BufferManager()
	:
	fSharedBufferList(NULL),
	fSharedBufferListArea(-1),
	fNextBufferID(1),
	fLocker("buffer manager locker")
{
	fSharedBufferListArea
		= BPrivate::SharedBufferList::Create(&fSharedBufferList);
}


/** @brief Destroys the buffer manager and releases the shared buffer list. */
BufferManager::~BufferManager()
{
	fSharedBufferList->Put();
}


/** @brief Returns the area ID of the shared buffer list. */
area_id
BufferManager::SharedBufferListArea()
{
	return fSharedBufferListArea;
}


/**
 * @brief Registers an existing buffer for a new team and returns its properties.
 *
 * @param team     The team acquiring a reference to the buffer.
 * @param bufferID The existing buffer ID to register for.
 * @param _size    Output: size of the buffer.
 * @param _flags   Output: buffer flags.
 * @param _offset  Output: offset within the area.
 * @param _area    Output: area ID of the buffer.
 * @return B_OK on success, B_ERROR if the buffer ID is not found.
 */
status_t
BufferManager::RegisterBuffer(team_id team, media_buffer_id bufferID,
	size_t* _size, int32* _flags, size_t* _offset, area_id* _area)
{
	BAutolock lock(fLocker);

	TRACE("RegisterBuffer team = %" B_PRId32 ", bufferid = %" B_PRId32 "\n",
		team, bufferID);

	buffer_info* info;
	if (!fBufferInfoMap.Get(bufferID, info)) {
		ERROR("failed to register buffer! team = %" B_PRId32 ", bufferid = %"
			B_PRId32 "\n", team, bufferID);
		return B_ERROR;
	}

	info->teams.insert(team);

	*_area = info->area;
	*_offset = info->offset;
	*_size = info->size,
	*_flags = info->flags;

	return B_OK;
}


/**
 * @brief Registers a new buffer from a team's area and assigns a buffer ID.
 *
 * Clones the provided area and creates a new buffer_info entry.
 *
 * @param team      The team creating the buffer.
 * @param size      Buffer size in bytes.
 * @param flags     Buffer flags.
 * @param offset    Offset within the area.
 * @param area      The team's area containing the buffer data.
 * @param _bufferID Output: the newly assigned buffer ID.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BufferManager::RegisterBuffer(team_id team, size_t size, int32 flags,
	size_t offset, area_id area, media_buffer_id* _bufferID)
{
	BAutolock lock(fLocker);
	TRACE("RegisterBuffer team = %" B_PRId32 ", area = %"
		B_PRId32 ", offset = %" B_PRIuSIZE ", size = %" B_PRIuSIZE "\n",
		team, area, offset, size);

	area_id clonedArea = _CloneArea(area);
	if (clonedArea < 0) {
		ERROR("RegisterBuffer: failed to clone buffer! error = %#" B_PRIx32
			", team = %" B_PRId32 ", area = %" B_PRId32 ", offset = %"
			B_PRIuSIZE ", size = %" B_PRIuSIZE "\n", clonedArea, team,
			area, offset, size);
		return clonedArea;
	}

	buffer_info info;
	info.id = fNextBufferID++;
	info.area = clonedArea;
	info.offset = offset;
	info.size = size;
	info.flags = flags;

	try {
		info.teams.insert(team);
		if (fBufferInfoMap.Put(info.id, info) != B_OK)
			throw std::bad_alloc();
	} catch (std::bad_alloc& exception) {
		_ReleaseClonedArea(clonedArea);
		return B_NO_MEMORY;
	}

	TRACE("RegisterBuffer: done, bufferID = %" B_PRId32 "\n", info.id);

	*_bufferID = info.id;
	return B_OK;
}


/**
 * @brief Removes a team's reference to a buffer, deleting it if no teams remain.
 *
 * @param team     The team releasing the buffer.
 * @param bufferID The buffer ID to release.
 * @return B_OK on success, B_ERROR if the buffer or team reference is not found.
 */
status_t
BufferManager::UnregisterBuffer(team_id team, media_buffer_id bufferID)
{
	BAutolock lock(fLocker);
	TRACE("UnregisterBuffer: team = %" B_PRId32 ", bufferID = %" B_PRId32 "\n",
		team, bufferID);

	buffer_info* info;
	if (!fBufferInfoMap.Get(bufferID, info)) {
		ERROR("UnregisterBuffer: failed to unregister buffer! team = %"
			B_PRId32 ", bufferID = %" B_PRId32 "\n", team, bufferID);
		return B_ERROR;
	}

	if (info->teams.find(team) == info->teams.end()) {
		ERROR("UnregisterBuffer: failed to find team = %" B_PRId32 " belonging"
			" to bufferID = %" B_PRId32 "\n", team, bufferID);
		return B_ERROR;
	}

	info->teams.erase(team);

	TRACE("UnregisterBuffer: team = %" B_PRId32 " removed from bufferID = %"
		B_PRId32 "\n", team, bufferID);

	if (info->teams.empty()) {
		_ReleaseClonedArea(info->area);
		fBufferInfoMap.Remove(bufferID);

		TRACE("UnregisterBuffer: bufferID = %" B_PRId32 " removed\n", bufferID);
	}

	return B_OK;
}


/**
 * @brief Removes all buffer references for a team, deleting orphaned buffers.
 *
 * @param team The team ID whose buffer references should be cleaned up.
 */
void
BufferManager::CleanupTeam(team_id team)
{
	BAutolock lock(fLocker);

	TRACE("BufferManager::CleanupTeam: team %" B_PRId32 "\n", team);

	BufferInfoMap::Iterator iterator = fBufferInfoMap.GetIterator();
	while (iterator.HasNext()) {
		BufferInfoMap::Entry entry = iterator.Next();

		entry.value.teams.erase(team);

		if (entry.value.teams.empty()) {
			PRINT(1, "BufferManager::CleanupTeam: removing buffer id %"
				B_PRId32 " that has no teams\n", entry.key.GetHashCode());
			_ReleaseClonedArea(entry.value.area);
			fBufferInfoMap.Remove(iterator);
		}
	}
}


/** @brief Prints a diagnostic dump of all registered buffers and their team assignments. */
void
BufferManager::Dump()
{
	BAutolock lock(fLocker);

	printf("\n");
	printf("BufferManager: list of buffers follows:\n");

	BufferInfoMap::Iterator iterator = fBufferInfoMap.GetIterator();
	while (iterator.HasNext()) {
		buffer_info info = iterator.Next().value;
		printf(" buffer-id %" B_PRId32 ", area-id %" B_PRId32 ", offset %ld, "
			"size %ld, flags %#08" B_PRIx32 "\n", info.id, info.area,
			info.offset, info.size, info.flags);
		printf("   assigned teams: ");

		std::set<team_id>::iterator teamIterator = info.teams.begin();
		for (; teamIterator != info.teams.end(); teamIterator++) {
			printf("%" B_PRId32 ", ", *teamIterator);
		}
		printf("\n");
	}
	printf("BufferManager: list end\n");
}


/**
 * @brief Clones an area for server-side access, reusing existing clones via ref counting.
 *
 * @param area The source area to clone.
 * @return The cloned area ID, or a negative error code.
 */
area_id
BufferManager::_CloneArea(area_id area)
{
	{
		clone_info* info;
		if (fCloneInfoMap.Get(area, info)) {
			// we have already cloned this particular area
			TRACE("BufferManager::_CloneArea() area %" B_PRId32 " has already"
				" been cloned (id %" B_PRId32 ")\n", area, info->clone);

			info->ref_count++;
			return info->clone;
		}
	}

	void* address;
	area_id clonedArea = clone_area("media_server cloned buffer", &address,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA, area);

	TRACE("BufferManager::_CloneArea() cloned area %" B_PRId32 ", clone id %"
		B_PRId32 "\n", area, clonedArea);

	if (clonedArea < 0)
		return clonedArea;

	clone_info info;
	info.clone = clonedArea;
	info.ref_count = 1;

	if (fCloneInfoMap.Put(area, info) == B_OK) {
		if (fSourceInfoMap.Put(clonedArea, area) == B_OK)
			return clonedArea;

		fCloneInfoMap.Remove(area);
	}

	delete_area(clonedArea);
	return B_NO_MEMORY;
}


/**
 * @brief Decrements the ref count of a cloned area, deleting it when it reaches zero.
 *
 * @param clone The cloned area ID to release.
 */
void
BufferManager::_ReleaseClonedArea(area_id clone)
{
	area_id source = fSourceInfoMap.Get(clone);

	clone_info* info;
	if (!fCloneInfoMap.Get(source, info)) {
		ERROR("BufferManager::_ReleaseClonedArea(): could not find clone info "
			"for id %" B_PRId32 " (clone %" B_PRId32 ")\n", source, clone);
		return;
	}

	if (--info->ref_count == 0) {
		TRACE("BufferManager::_ReleaseClonedArea(): delete cloned area %"
			B_PRId32 " (source %" B_PRId32 ")\n", clone, source);

		fSourceInfoMap.Remove(clone);
		fCloneInfoMap.Remove(source);
		delete_area(clone);
	} else {
		TRACE("BufferManager::_ReleaseClonedArea(): released cloned area %"
			B_PRId32 " (source %" B_PRId32 ")\n", clone, source);
	}
}
