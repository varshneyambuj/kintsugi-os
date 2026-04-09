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
 * Copyright 2009, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file BufferGroup.h
 *  @brief Defines BBufferGroup, a collection of shared media buffers.
 */

#ifndef _BUFFER_GROUP_H
#define _BUFFER_GROUP_H


#include <MediaDefs.h>


class BBuffer;
namespace BPrivate {
	class SharedBufferList;
}


/** @brief Manages a pool of shared BBuffer objects used for media data transfer.
 *
 *  A buffer group owns a set of BBuffer objects allocated in shared memory so
 *  that both producers and consumers can access the data without copying.
 */
class BBufferGroup {
public:
	/** @brief Creates a buffer group with count buffers of the given size.
	 *  @param size The size of each buffer in bytes.
	 *  @param count Number of buffers to allocate (default 3).
	 *  @param placement Memory placement flag (default B_ANY_ADDRESS).
	 *  @param lock Memory locking flag (default B_FULL_LOCK).
	 */
							BBufferGroup(size_t size, int32 count = 3,
								uint32 placement = B_ANY_ADDRESS,
								uint32 lock = B_FULL_LOCK);

	/** @brief Creates an empty buffer group; add buffers with AddBuffer(). */
	explicit				BBufferGroup();

	/** @brief Creates a buffer group from an array of existing buffer IDs.
	 *  @param count Number of buffer IDs in the array.
	 *  @param buffers Array of media_buffer_id values to clone into this group.
	 */
							BBufferGroup(int32 count,
								const media_buffer_id* buffers);

	/** @brief Destroys the buffer group and releases all owned buffers. */
							~BBufferGroup();

	/** @brief Returns the initialization status of the buffer group.
	 *  @return B_OK if the group was initialized successfully, or an error code.
	 */
			status_t		InitCheck();

	/** @brief Adds a buffer described by a clone-info structure to this group.
	 *  @param info Clone information describing the buffer to add.
	 *  @param _buffer If non-NULL, receives a pointer to the added BBuffer.
	 *  @return B_OK on success, or an error code.
	 */
			status_t		AddBuffer(const buffer_clone_info& info,
								BBuffer** _buffer = NULL);

	/** @brief Requests a buffer of at least the given size, blocking until one is free.
	 *  @param size Minimum required buffer size in bytes.
	 *  @param timeout Maximum time to wait in microseconds (default infinite).
	 *  @return A pointer to the requested BBuffer, or NULL on timeout/error.
	 */
			BBuffer*		RequestBuffer(size_t size,
								bigtime_t timeout = B_INFINITE_TIMEOUT);

	/** @brief Requests a specific buffer, blocking until it becomes available.
	 *  @param buffer The buffer to request.
	 *  @param timeout Maximum time to wait in microseconds (default infinite).
	 *  @return B_OK on success, B_TIMED_OUT if the timeout elapsed.
	 */
			status_t		RequestBuffer(BBuffer* buffer,
								bigtime_t timeout = B_INFINITE_TIMEOUT);

	/** @brief Returns the error code from the most recent RequestBuffer() call.
	 *  @return B_OK if the last request succeeded, or an error code.
	 */
			status_t		RequestError();

	/** @brief Returns the number of buffers in this group.
	 *  @param _count On return, the buffer count.
	 *  @return B_OK on success, or an error code.
	 */
			status_t		CountBuffers(int32* _count);

	/** @brief Retrieves up to bufferCount buffer pointers into the supplied array.
	 *  @param bufferCount Capacity of the _buffers array.
	 *  @param _buffers Array that receives BBuffer pointers.
	 *  @return B_OK on success, or an error code.
	 */
			status_t		GetBufferList(int32 bufferCount,
								BBuffer** _buffers);

	/** @brief Blocks until all buffers in the group have been recycled.
	 *  @return B_OK when all buffers are back, or an error code.
	 */
			status_t		WaitForBuffers();

	/** @brief Asks all consumers to return every buffer in this group immediately.
	 *  @return B_OK on success, or an error code.
	 */
			status_t		ReclaimAllBuffers();

private:
			// deprecated BeOS R4 API
			status_t 		AddBuffersTo(BMessage* message, const char* name,
								bool needLock);

							BBufferGroup(const BBufferGroup& other);
			BBufferGroup&	operator=(const BBufferGroup& other);

			status_t		_Init();

private:
	friend class BPrivate::SharedBufferList;

			status_t		fInitError;
			status_t		fRequestError;
			int32			fBufferCount;
			BPrivate::SharedBufferList* fBufferList;
			sem_id			fReclaimSem;

			uint32			_reserved[9];
};


#endif	// _BUFFER_GROUP_H
