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
 *   Copyright 2009-2012, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2002, Marcus Overhagen. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file SharedBufferList.cpp
 *  @brief Shared cross-team list of BBuffer/BBufferGroup state used by the media server. */


/*!	Used for BBufferGroup and BBuffer management across teams.
	Created in the media server, cloned into each BBufferGroup (visible in
	all address spaces).
*/

// TODO: don't use a simple list!


#include <SharedBufferList.h>

#include <string.h>

#include <Autolock.h>
#include <Buffer.h>
#include <Locker.h>

#include <MediaDebug.h>
#include <DataExchange.h>


static BPrivate::SharedBufferList* sList = NULL;
static area_id sArea = -1;
static int32 sRefCount = 0;
static BLocker sLocker("shared buffer list");


namespace BPrivate {


/** @brief Creates a new SharedBufferList in a cloneable shared area.
 *
 *  Called by the media server at start-up.  The returned area is cloned by
 *  each client process that calls Get().
 *
 *  @param _list Out-parameter set to a pointer to the newly created list.
 *  @return The area_id of the shared area on success, or a negative error code. */
/*static*/ area_id
SharedBufferList::Create(SharedBufferList** _list)
{
	CALLED();

	size_t size = (sizeof(SharedBufferList) + (B_PAGE_SIZE - 1))
		& ~(B_PAGE_SIZE - 1);
	SharedBufferList* list;

	area_id area = create_area("shared buffer list", (void**)&list,
		B_ANY_ADDRESS, size, B_LAZY_LOCK,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
	if (area < 0)
		return area;

	status_t status = list->_Init();
	if (status != B_OK) {
		delete_area(area);
		return status;
	}

	return area;
}


/** @brief Clones the media server's shared buffer list into this address space.
 *
 *  Thread-safe; uses a reference count so the area is only cloned once per
 *  process.  Callers must eventually call Put() to release their reference.
 *
 *  @return Pointer to the SharedBufferList, or NULL on failure. */
/*static*/ SharedBufferList*
SharedBufferList::Get()
{
	CALLED();

	BAutolock _(sLocker);

	if (atomic_add(&sRefCount, 1) > 0 && sList != NULL)
		return sList;

	// ask media_server to get the area_id of the shared buffer list
	server_get_shared_buffer_area_request areaRequest;
	server_get_shared_buffer_area_reply areaReply;
	if (QueryServer(SERVER_GET_SHARED_BUFFER_AREA, &areaRequest,
			sizeof(areaRequest), &areaReply, sizeof(areaReply)) != B_OK) {
		ERROR("SharedBufferList::Get() SERVER_GET_SHARED_BUFFER_AREA failed\n");
		return NULL;
	}

	sArea = clone_area("shared buffer list clone", (void**)&sList,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, areaReply.area);
	if (sArea < 0) {
		ERROR("SharedBufferList::Get() clone area %" B_PRId32 ": %s\n",
			areaReply.area, strerror(sArea));
		return NULL;
	}

	return sList;
}


/** @brief Unmaps the locally cloned shared buffer list area.
 *
 *  Called internally when the reference count drops to zero. */
/*static*/ void
SharedBufferList::Invalidate()
{
	delete_area(sArea);
	sList = NULL;
}


/** @brief Releases one reference to the shared buffer list.
 *
 *  When the reference count reaches zero Invalidate() is called to unmap the area. */
void
SharedBufferList::Put()
{
	CALLED();
	BAutolock _(sLocker);

	if (atomic_add(&sRefCount, -1) == 1)
		Invalidate();
}


/** @brief Deletes all BBuffers belonging to the group identified by \a groupReclaimSem, then releases the reference.
 *
 *  @param groupReclaimSem The reclaim semaphore that uniquely identifies the buffer group. */
void
SharedBufferList::DeleteGroupAndPut(sem_id groupReclaimSem)
{
	CALLED();

	if (Lock() == B_OK) {
		for (int32 i = 0; i < fCount; i++) {
			if (fInfos[i].reclaim_sem == groupReclaimSem) {
				// delete the associated buffer
				delete fInfos[i].buffer;

				// Decrement buffer count by one, and fill the gap
				// in the list with its last entry
				fCount--;
				if (fCount > 0)
					fInfos[i--] = fInfos[fCount];
			}
		}

		Unlock();
	}

	Put();
}


/** @brief Acquires the shared list's internal spin-semaphore lock.
 *
 *  Uses an atomic counter to avoid unnecessary semaphore operations when
 *  the lock is uncontested.
 *
 *  @return B_OK on success, or a semaphore error code. */
status_t
SharedBufferList::Lock()
{
	if (atomic_add(&fAtom, 1) > 0) {
		status_t status;
		do {
			status = acquire_sem(fSemaphore);
		} while (status == B_INTERRUPTED);

		return status;
	}
	return B_OK;
}


/** @brief Releases the shared list's internal spin-semaphore lock.
 *  @return B_OK on success, or a semaphore error code. */
status_t
SharedBufferList::Unlock()
{
	if (atomic_add(&fAtom, -1) > 1)
		return release_sem(fSemaphore);

	return B_OK;
}


/** @brief Creates a new BBuffer from \a info and adds it to the group.
 *
 *  If \a info.buffer refers to an existing buffer ID it must not already belong
 *  to the same group (checked via CheckID()).
 *
 *  @param groupReclaimSem Reclaim semaphore identifying the target group.
 *  @param info            Clone information for the new buffer.
 *  @param _buffer         Optional out-parameter receiving the created BBuffer pointer.
 *  @return B_OK on success, or an error code. */
status_t
SharedBufferList::AddBuffer(sem_id groupReclaimSem,
	const buffer_clone_info& info, BBuffer** _buffer)
{
	status_t status = Lock();
	if (status != B_OK)
		return status;

	// Check if the id exists
	status = CheckID(groupReclaimSem, info.buffer);
	if (status != B_OK) {
		Unlock();
		return status;
	}
	BBuffer* buffer = new(std::nothrow) BBuffer(info);
	if (buffer == NULL) {
		Unlock();
		return B_NO_MEMORY;
	}

	if (buffer->Data() == NULL) {
		// BBuffer::Data() will return NULL if an error occured
		ERROR("BBufferGroup: error while creating buffer\n");
		delete buffer;
		Unlock();
		return B_ERROR;
	}

	status = AddBuffer(groupReclaimSem, buffer);
	if (status != B_OK) {
		delete buffer;
		Unlock();
		return status;
	} else if (_buffer != NULL)
		*_buffer = buffer;

	return Unlock();
}


/** @brief Adds an already-constructed BBuffer to the group, releasing the reclaim semaphore once.
 *
 *  The list must be locked by the caller before this overload is invoked.
 *
 *  @param groupReclaimSem Reclaim semaphore identifying the target group.
 *  @param buffer          The BBuffer to add; must not be NULL.
 *  @return B_OK on success, B_BAD_VALUE if \a buffer is NULL, or B_MEDIA_TOO_MANY_BUFFERS
 *          if the global buffer limit is reached. */
status_t
SharedBufferList::AddBuffer(sem_id groupReclaimSem, BBuffer* buffer)
{
	CALLED();

	if (buffer == NULL)
		return B_BAD_VALUE;

	if (fCount == kMaxBuffers) {
		return B_MEDIA_TOO_MANY_BUFFERS;
	}

	fInfos[fCount].id = buffer->ID();
	fInfos[fCount].buffer = buffer;
	fInfos[fCount].reclaim_sem = groupReclaimSem;
	fInfos[fCount].reclaimed = true;
	fCount++;

	return release_sem_etc(groupReclaimSem, 1, B_DO_NOT_RESCHEDULE);
}


/** @brief Checks that a buffer ID does not already exist within a given group.
 *
 *  @param groupSem The reclaim semaphore of the group to search.
 *  @param id       The buffer ID to check; 0 is always considered valid.
 *  @return B_OK if the ID is not a duplicate in the group, B_BAD_VALUE for a negative ID,
 *          or B_ERROR if the ID is already present in the group. */
status_t
SharedBufferList::CheckID(sem_id groupSem, media_buffer_id id) const
{
	CALLED();

	if (id == 0)
		return B_OK;
	if (id < 0)
		return B_BAD_VALUE;

	for (int32 i = 0; i < fCount; i++) {
		if (fInfos[i].id == id
			&& fInfos[i].reclaim_sem == groupSem) {
			return B_ERROR;
		}
	}
	return B_OK;
}


/** @brief Requests a free buffer from the group, blocking until one is available or the timeout expires.
 *
 *  Searches for a buffer matching the size, ID, or pointer criteria.  When
 *  a buffer shared with other groups is found it is also marked as requested
 *  in those other groups.
 *
 *  @param groupReclaimSem  Reclaim semaphore identifying the source group.
 *  @param buffersInGroup   Total number of buffers in the group (loop bound).
 *  @param size             Required minimum size; 0 if not used as a criterion.
 *  @param wantID           Specific buffer ID to look for; 0 if not used.
 *  @param _buffer          In/out: on entry, a specific BBuffer pointer to match (or NULL);
 *                          on success, receives the found BBuffer pointer.
 *  @param timeout          Absolute or relative timeout in microseconds.
 *  @return B_OK on success, or an error code (e.g. B_TIMED_OUT). */
status_t
SharedBufferList::RequestBuffer(sem_id groupReclaimSem, int32 buffersInGroup,
	size_t size, media_buffer_id wantID, BBuffer** _buffer, bigtime_t timeout)
{
	CALLED();
	// We always search for a buffer from the group indicated by groupReclaimSem
	// first.
	// If "size" != 0, we search for a buffer that is "size" bytes or larger.
	// If "wantID" != 0, we search for a buffer with this ID.
	// If "*_buffer" != NULL, we search for a buffer at this address.
	//
	// If we found a buffer, we also need to mark it in all other groups as
	// requested and also once need to acquire the reclaim_sem of the other
	// groups

	uint32 acquireFlags;

	if (timeout <= 0) {
		timeout = 0;
		acquireFlags = B_RELATIVE_TIMEOUT;
	} else if (timeout == B_INFINITE_TIMEOUT) {
		acquireFlags = B_RELATIVE_TIMEOUT;
	} else {
		timeout += system_time();
		acquireFlags = B_ABSOLUTE_TIMEOUT;
	}

	// With each itaration we request one more buffer, since we need to skip
	// the buffers that don't fit the request
	int32 count = 1;

	do {
		status_t status;
		do {
			status = acquire_sem_etc(groupReclaimSem, count, acquireFlags,
				timeout);
		} while (status == B_INTERRUPTED);

		if (status != B_OK)
			return status;

		// try to exit savely if the lock fails
		status = Lock();
		if (status != B_OK) {
			ERROR("SharedBufferList:: RequestBuffer: Lock failed: %s\n",
				strerror(status));
			release_sem_etc(groupReclaimSem, count, 0);
			return B_ERROR;
		}

		for (int32 i = 0; i < fCount; i++) {
			// We need a BBuffer from the group, and it must be marked as
			// reclaimed
			if (fInfos[i].reclaim_sem == groupReclaimSem
				&& fInfos[i].reclaimed) {
				if ((size != 0 && size <= fInfos[i].buffer->SizeAvailable())
					|| (*_buffer != 0 && fInfos[i].buffer == *_buffer)
					|| (wantID != 0 && fInfos[i].id == wantID)) {
				   	// we found a buffer
					fInfos[i].reclaimed = false;
					*_buffer = fInfos[i].buffer;

					// if we requested more than one buffer, release the rest
					if (count > 1) {
						release_sem_etc(groupReclaimSem, count - 1,
							B_DO_NOT_RESCHEDULE);
					}

					// And mark all buffers with the same ID as requested in
					// all other buffer groups
					_RequestBufferInOtherGroups(groupReclaimSem,
						fInfos[i].buffer->ID());

					Unlock();
					return B_OK;
				}
			}
		}

		release_sem_etc(groupReclaimSem, count, B_DO_NOT_RESCHEDULE);
		if (Unlock() != B_OK) {
			ERROR("SharedBufferList:: RequestBuffer: unlock failed\n");
			return B_ERROR;
		}
		// prepare to request one more buffer next time
		count++;
	} while (count <= buffersInGroup);

	ERROR("SharedBufferList:: RequestBuffer: no buffer found\n");
	return B_ERROR;
}


/** @brief Marks a buffer as reclaimed and releases the reclaim semaphore in every group that owns it.
 *
 *  @param buffer The BBuffer to recycle; identified by its media_buffer_id.
 *  @return B_OK on success, or an error code if the buffer was not found or was already reclaimed. */
status_t
SharedBufferList::RecycleBuffer(BBuffer* buffer)
{
	CALLED();

	media_buffer_id id = buffer->ID();

	if (Lock() != B_OK)
		return B_ERROR;

	int32 reclaimedCount = 0;

	for (int32 i = 0; i < fCount; i++) {
		// find the buffer id, and reclaim it in all groups it belongs to
		if (fInfos[i].id == id) {
			reclaimedCount++;
			if (fInfos[i].reclaimed) {
				ERROR("SharedBufferList::RecycleBuffer, BBuffer %p, id = %"
					B_PRId32 " already reclaimed\n", buffer, id);
				DEBUG_ONLY(debugger("buffer already reclaimed"));
				continue;
			}
			fInfos[i].reclaimed = true;
			release_sem_etc(fInfos[i].reclaim_sem, 1, B_DO_NOT_RESCHEDULE);
		}
	}

	if (Unlock() != B_OK)
		return B_ERROR;

	if (reclaimedCount == 0) {
		ERROR("shared_buffer_list::RecycleBuffer, BBuffer %p, id = %" B_PRId32
			" NOT reclaimed\n", buffer, id);
		return B_ERROR;
	}

	return B_OK;
}


/** @brief Removes a buffer from the shared list without recycling it.
 *
 *  Used when a buffer is being deleted while it is still logically owned by a group.
 *  The buffer must already be in the reclaimed state.
 *
 *  @param buffer The BBuffer to remove; identified by its media_buffer_id.
 *  @return B_OK on success, or an error code. */
status_t
SharedBufferList::RemoveBuffer(BBuffer* buffer)
{
	CALLED();

	media_buffer_id id = buffer->ID();

	if (Lock() != B_OK)
		return B_ERROR;

	int32 notRemovedCount = 0;

	for (int32 i = 0; i < fCount; i++) {
		// find the buffer id, and remove it in all groups it belongs to
		if (fInfos[i].id == id) {
			if (!fInfos[i].reclaimed) {
				notRemovedCount++;
				ERROR("SharedBufferList::RequestBuffer, BBuffer %p, id = %"
					B_PRId32 " not reclaimed\n", buffer, id);
				DEBUG_ONLY(debugger("buffer not reclaimed"));
				continue;
			}
			fInfos[i].buffer = NULL;
			fInfos[i].id = -1;
			fInfos[i].reclaim_sem = -1;
		}
	}

	if (Unlock() != B_OK)
		return B_ERROR;

	if (notRemovedCount != 0) {
		ERROR("SharedBufferList::RemoveBuffer, BBuffer %p, id = %" B_PRId32
			" not reclaimed\n", buffer, id);
		return B_ERROR;
	}

	return B_OK;
}



/** @brief Returns exactly \a bufferCount buffers from the group identified by \a groupReclaimSem.
 *
 *  @param groupReclaimSem The reclaim semaphore identifying the group.
 *  @param bufferCount     Number of buffer pointers to retrieve.
 *  @param buffers         Caller-supplied array of at least \a bufferCount BBuffer* elements.
 *  @return B_OK if exactly \a bufferCount buffers were found, or B_ERROR otherwise. */
status_t
SharedBufferList::GetBufferList(sem_id groupReclaimSem, int32 bufferCount,
	BBuffer** buffers)
{
	CALLED();

	if (Lock() != B_OK)
		return B_ERROR;

	int32 found = 0;

	for (int32 i = 0; i < fCount; i++)
		if (fInfos[i].reclaim_sem == groupReclaimSem) {
			buffers[found++] = fInfos[i].buffer;
			if (found == bufferCount)
				break;
		}

	if (Unlock() != B_OK)
		return B_ERROR;

	return found == bufferCount ? B_OK : B_ERROR;
}


/** @brief Initialises the SharedBufferList in-place after the shared area is created.
 *
 *  Creates the internal semaphore and zeroes all buffer info entries.
 *
 *  @return B_OK on success, or a negative semaphore creation error code. */
status_t
SharedBufferList::_Init()
{
	CALLED();

	fSemaphore = create_sem(0, "shared buffer list lock");
	if (fSemaphore < 0)
		return fSemaphore;

	fAtom = 0;

	for (int32 i = 0; i < kMaxBuffers; i++) {
		fInfos[i].id = -1;
	}
	fCount = 0;

	return B_OK;
}


/** @brief Marks a buffer as requested in all groups other than the one currently requesting it.
 *
 *  Must be called with the list locked.  Acquires the reclaim semaphore of
 *  each other group that contains the buffer to prevent a double-request.
 *
 *  @param groupReclaimSem The group that is currently performing the request.
 *  @param id              The media_buffer_id shared across groups. */
void
SharedBufferList::_RequestBufferInOtherGroups(sem_id groupReclaimSem,
	media_buffer_id id)
{
	for (int32 i = 0; i < fCount; i++) {
		// find buffers with same id, but belonging to other groups
		if (fInfos[i].id == id && fInfos[i].reclaim_sem != groupReclaimSem) {
			// and mark them as requested
			// TODO: this can deadlock if BBuffers with same media_buffer_id
			// exist in more than one BBufferGroup, and RequestBuffer()
			// is called on both groups (which should not be done).
			status_t status;
			do {
				status = acquire_sem(fInfos[i].reclaim_sem);
			} while (status == B_INTERRUPTED);

			// try to skip entries that belong to crashed teams
			if (status != B_OK)
				continue;

			if (fInfos[i].reclaimed == false) {
				ERROR("SharedBufferList:: RequestBufferInOtherGroups BBuffer "
					"%p, id = %" B_PRId32 " not reclaimed while requesting\n",
					fInfos[i].buffer, id);
				continue;
			}

			fInfos[i].reclaimed = false;
		}
	}
}


}	// namespace BPrivate
