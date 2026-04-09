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
 *   Copyright (c) 2002, 2003 Marcus Overhagen <Marcus@Overhagen.de>
 *
 *   Permission is hereby granted, free of charge, to any person obtaining
 *   a copy of this software and associated documentation files or portions
 *   thereof (the "Software"), to deal in the Software without restriction,
 *   including without limitation the rights to use, copy, modify, merge,
 *   publish, distribute, sublicense, and/or sell copies of the Software,
 *   and to permit persons to whom the Software is furnished to do so, subject
 *   to the following conditions:
 *
 *    * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *    * Redistributions in binary form must reproduce the above copyright notice
 *      in the  binary, as well as this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided with
 *      the distribution.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *   THE SOFTWARE.
 */

/** @file BufferGroup.cpp
 *  @brief Implements BBufferGroup, a collection of shared media buffers. */


#include <BufferGroup.h>

#include <Buffer.h>

#include "MediaDebug.h"
#include "DataExchange.h"
#include "SharedBufferList.h"


/** @brief Constructs a BBufferGroup containing \a count pre-allocated buffers.
 *
 *  All buffers are packed into a single area with the given memory placement
 *  and locking policy.  Each BBuffer clones that area independently so the
 *  group area can be deleted immediately after construction.
 *
 *  @param size      Size in bytes of each individual buffer.
 *  @param count     Number of buffers to create.
 *  @param placement Memory placement flag (B_ANY_ADDRESS or B_ANY_KERNEL_ADDRESS).
 *  @param lock      Area locking policy passed to create_area(). */
BBufferGroup::BBufferGroup(size_t size, int32 count, uint32 placement,
	uint32 lock)
{
	CALLED();
	fInitError = _Init();
	if (fInitError != B_OK)
		return;

	// This one is easy. We need to create "count" BBuffers,
	// each one "size" bytes large. They all go into one
	// area, with "placement" and "lock" attributes.
	// The BBuffers created will clone the area, and
	// then we delete our area. This way BBuffers are
	// independent from the BBufferGroup

	// don't allow all placement parameter values
	if (placement != B_ANY_ADDRESS && placement != B_ANY_KERNEL_ADDRESS) {
		ERROR("BBufferGroup: placement != B_ANY_ADDRESS "
			"&& placement != B_ANY_KERNEL_ADDRESS (0x%#" B_PRIx32 ")\n",
			placement);
		placement = B_ANY_ADDRESS;
	}

	// first we roundup for a better placement in memory
	size_t allocSize = (size + 63) & ~63;

	// now we create the area
	size_t areaSize
		= ((allocSize * count) + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);

	void* startAddress;
	area_id bufferArea = create_area("some buffers area", &startAddress,
		placement, areaSize, lock, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
	if (bufferArea < 0) {
		ERROR("BBufferGroup: failed to allocate %ld bytes area\n", areaSize);
		fInitError = (status_t)bufferArea;
		return;
	}

	buffer_clone_info info;

	for (int32 i = 0; i < count; i++) {
		info.area = bufferArea;
		info.offset = i * allocSize;
		info.size = size;

		fInitError = AddBuffer(info);
		if (fInitError != B_OK)
			break;
	}

	delete_area(bufferArea);
}


/** @brief Constructs an empty BBufferGroup with no pre-allocated buffers.
 *
 *  Buffers can be added later via AddBuffer(). */
BBufferGroup::BBufferGroup()
{
	CALLED();
	fInitError = _Init();
	if (fInitError != B_OK)
		return;

	// this one simply creates an empty BBufferGroup
}


/** @brief Constructs a BBufferGroup from an array of existing media_buffer_id values.
 *
 *  Each ID is wrapped in a buffer_clone_info and passed to AddBuffer(), which
 *  contacts the media server to resolve the underlying shared area.
 *
 *  @param count   Number of buffer IDs in the \a buffers array.
 *  @param buffers Array of media_buffer_id values identifying existing buffers. */
BBufferGroup::BBufferGroup(int32 count, const media_buffer_id* buffers)
{
	CALLED();
	fInitError = _Init();
	if (fInitError != B_OK)
		return;

	// This one creates "BBuffer"s from "media_buffer_id"s passed
	// by the application.

	buffer_clone_info info;

	for (int32 i = 0; i < count; i++) {
		info.buffer = buffers[i];

		fInitError = AddBuffer(info);
		if (fInitError != B_OK)
			break;
	}
}


/** @brief Destructor; deletes all BBuffers belonging to this group and releases the reclaim semaphore. */
BBufferGroup::~BBufferGroup()
{
	CALLED();
	if (fBufferList != NULL)
		fBufferList->DeleteGroupAndPut(fReclaimSem);

	delete_sem(fReclaimSem);
}


/** @brief Returns the initialisation status of the group.
 *  @return B_OK on success, or an error code set during construction. */
status_t
BBufferGroup::InitCheck()
{
	CALLED();
	return fInitError;
}


/** @brief Adds a new BBuffer to the group described by \a info.
 *
 *  @param info    Clone information for the new buffer.
 *  @param _buffer Optional out-parameter that receives a pointer to the created BBuffer.
 *  @return B_OK on success, B_NO_INIT if the group is not initialised, or another error code. */
status_t
BBufferGroup::AddBuffer(const buffer_clone_info& info, BBuffer** _buffer)
{
	CALLED();
	if (fInitError != B_OK)
		return B_NO_INIT;

	status_t status = fBufferList->AddBuffer(fReclaimSem, info, _buffer);
	if (status != B_OK) {
		ERROR("BBufferGroup: error when adding buffer\n");
		return status;
	}
	atomic_add(&fBufferCount, 1);
	return B_OK;
}


/** @brief Requests a free buffer of at least \a size bytes from the group.
 *
 *  Blocks until a suitable buffer is available or the timeout expires.
 *
 *  @param size    Minimum required buffer size in bytes.
 *  @param timeout Maximum time to wait in microseconds (B_INFINITE_TIMEOUT to wait forever).
 *  @return Pointer to the acquired BBuffer, or NULL on failure. */
BBuffer*
BBufferGroup::RequestBuffer(size_t size, bigtime_t timeout)
{
	CALLED();
	if (fInitError != B_OK)
		return NULL;

	if (size <= 0)
		return NULL;

	BBuffer *buffer = NULL;
	fRequestError = fBufferList->RequestBuffer(fReclaimSem, fBufferCount,
		size, 0, &buffer, timeout);

	return fRequestError == B_OK ? buffer : NULL;
}


/** @brief Requests a specific BBuffer from the group.
 *
 *  Blocks until the buffer is available or the timeout expires.
 *
 *  @param buffer  The specific buffer to request.
 *  @param timeout Maximum time to wait in microseconds (B_INFINITE_TIMEOUT to wait forever).
 *  @return B_OK on success, or an error code. */
status_t
BBufferGroup::RequestBuffer(BBuffer* buffer, bigtime_t timeout)
{
	CALLED();
	if (fInitError != B_OK)
		return B_NO_INIT;

	if (buffer == NULL)
		return B_BAD_VALUE;

	fRequestError = fBufferList->RequestBuffer(fReclaimSem, fBufferCount, 0, 0,
		&buffer, timeout);

	return fRequestError;
}


/** @brief Returns the error code from the most recent RequestBuffer() call.
 *  @return B_OK if the last request succeeded, or an error code. */
status_t
BBufferGroup::RequestError()
{
	CALLED();
	if (fInitError != B_OK)
		return B_NO_INIT;

	return fRequestError;
}


/** @brief Returns the current number of buffers in this group.
 *  @param _count Out-parameter that receives the buffer count.
 *  @return B_OK on success, or B_NO_INIT if the group is not initialised. */
status_t
BBufferGroup::CountBuffers(int32* _count)
{
	CALLED();
	if (fInitError != B_OK)
		return B_NO_INIT;

	*_count = fBufferCount;
	return B_OK;
}


/** @brief Fills \a _buffers with pointers to up to \a bufferCount buffers in the group.
 *
 *  @param bufferCount Number of pointers to retrieve; must be between 1 and CountBuffers().
 *  @param _buffers    Caller-supplied array of at least \a bufferCount BBuffer* elements.
 *  @return B_OK on success, B_BAD_VALUE for an out-of-range count, or B_NO_INIT. */
status_t
BBufferGroup::GetBufferList(int32 bufferCount, BBuffer** _buffers)
{
	CALLED();
	if (fInitError != B_OK)
		return B_NO_INIT;

	if (bufferCount <= 0 || bufferCount > fBufferCount)
		return B_BAD_VALUE;

	return fBufferList->GetBufferList(fReclaimSem, bufferCount, _buffers);
}


/** @brief Blocks until at least one buffer belonging to this group has been recycled.
 *  @return B_OK when a buffer becomes available, or an error code. */
status_t
BBufferGroup::WaitForBuffers()
{
	CALLED();
	if (fInitError != B_OK)
		return B_NO_INIT;

	// TODO: this function is not really useful anyway, and will
	// not work exactly as documented, but it is close enough

	if (fBufferCount < 0)
		return B_BAD_VALUE;
	if (fBufferCount == 0)
		return B_OK;

	// We need to wait until at least one buffer belonging to this group is
	// reclaimed.
	// This has happened when can aquire "fReclaimSem"

	status_t status;
	while ((status = acquire_sem(fReclaimSem)) == B_INTERRUPTED)
		;
	if (status != B_OK)
		return status;

	// we need to release the "fReclaimSem" now, else we would block
	// requesting of new buffers

	return release_sem(fReclaimSem);
}


/** @brief Blocks until all buffers in the group have been reclaimed.
 *  @return B_OK when all buffers are available, or an error code. */
status_t
BBufferGroup::ReclaimAllBuffers()
{
	CALLED();
	if (fInitError != B_OK)
		return B_NO_INIT;

	// because additional BBuffers might get added to this group betweeen
	// acquire and release
	int32 count = fBufferCount;

	if (count < 0)
		return B_BAD_VALUE;
	if (count == 0)
		return B_OK;

	// we need to wait until all BBuffers belonging to this group are reclaimed.
	// this has happened when the "fReclaimSem" can be aquired "fBufferCount"
	// times

	status_t status = B_ERROR;
	do {
		status = acquire_sem_etc(fReclaimSem, count, B_RELATIVE_TIMEOUT, 0);
	} while (status == B_INTERRUPTED);

	if (status != B_OK)
		return status;

	// we need to release the "fReclaimSem" now, else we would block
	// requesting of new buffers

	return release_sem_etc(fReclaimSem, count, 0);
}


//	#pragma mark - deprecated BeOS R4 API


/** @brief Deprecated BeOS R4 API: adds all buffer IDs to a BMessage.
 *
 *  Internally uses GetBufferList() to enumerate buffers; \a needLock is ignored.
 *
 *  @param message  Destination BMessage to which buffer IDs are appended.
 *  @param name     Field name used when adding the IDs.
 *  @param needLock Ignored; locking is handled internally.
 *  @return B_OK on success, or an error code. */
status_t
BBufferGroup::AddBuffersTo(BMessage* message, const char* name, bool needLock)
{
	CALLED();
	if (fInitError != B_OK)
		return B_NO_INIT;

	// BeOS R4 legacy API. Implemented as a wrapper around GetBufferList
	// "needLock" is ignored, GetBufferList will do locking

	if (message == NULL)
		return B_BAD_VALUE;

	if (name == NULL || strlen(name) == 0)
		return B_BAD_VALUE;

	BBuffer* buffers[fBufferCount];
	status_t status = GetBufferList(fBufferCount, buffers);
	if (status != B_OK)
		return status;

	for (int32 i = 0; i < fBufferCount; i++) {
		status = message->AddInt32(name, int32(buffers[i]->ID()));
		if (status != B_OK)
			return status;
	}

	return B_OK;
}


//	#pragma mark - private methods


/* not implemented */
//BBufferGroup::BBufferGroup(const BBufferGroup &)
//BBufferGroup & BBufferGroup::operator=(const BBufferGroup &)


/** @brief Shared initialisation helper: creates the reclaim semaphore and acquires the SharedBufferList.
 *  @return B_OK on success, or an error code if the semaphore or buffer list could not be created. */
status_t
BBufferGroup::_Init()
{
	CALLED();

	// some defaults in case we drop out early
	fBufferList = NULL;
	fRequestError = B_ERROR;
	fBufferCount = 0;

	// Create the reclaim semaphore
	// This is also used as a system wide unique identifier for this group
	fReclaimSem = create_sem(0, "buffer reclaim sem");
	if (fReclaimSem < 0) {
		ERROR("BBufferGroup::InitBufferGroup: couldn't create fReclaimSem\n");
		return (status_t)fReclaimSem;
	}

	fBufferList = BPrivate::SharedBufferList::Get();
	if (fBufferList == NULL) {
		ERROR("BBufferGroup::InitBufferGroup: SharedBufferList::Get() "
			"failed\n");
		return B_ERROR;
	}

	return B_OK;
}

