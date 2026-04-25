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
 *   Copyright 2009, 2017, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Lotz <mmlr@mlotz.ch>
 */


/**
 * @file StreamingRingBuffer.cpp
 * @brief Implementation of the blocking single-producer / single-consumer
 *        byte ring buffer used between protocol encode/decode threads and
 *        the network I/O threads.
 *
 * The reader and writer paths each take their own outer lock (so two
 * readers cannot race), then take the shared data lock to mutate the
 * ring state. When a side runs out of room or data it parks on a
 * dedicated semaphore; the opposite side releases the semaphore once it
 * makes progress. MakeEmpty() additionally cancels parked threads.
 */


#include "StreamingRingBuffer.h"

#include <Autolock.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#ifdef CLIENT_COMPILE
#define TRACE_ALWAYS(x...)		printf("StreamingRingBuffer: " x)
#else
#define TRACE_ALWAYS(x...)		debug_printf("StreamingRingBuffer: " x)
#endif

#define TRACE(x...)				/*TRACE_ALWAYS(x)*/
#define TRACE_ERROR(x...)		TRACE_ALWAYS(x)


/**
 * @brief Allocates the ring storage and the reader / writer semaphores.
 *
 * If allocation of the storage fails, fBufferSize is reset to zero and
 * InitCheck() will report the failure.
 *
 * @param bufferSize  Capacity of the ring in bytes.
 */
StreamingRingBuffer::StreamingRingBuffer(size_t bufferSize)
	:
	fReaderWaiting(false),
	fWriterWaiting(false),
	fCancelRead(false),
	fCancelWrite(false),
	fReaderNotifier(-1),
	fWriterNotifier(-1),
	fReaderLocker("StreamingRingBuffer reader"),
	fWriterLocker("StreamingRingBuffer writer"),
	fDataLocker("StreamingRingBuffer data"),
	fBuffer(NULL),
	fBufferSize(bufferSize),
	fReadable(0),
	fReadPosition(0),
	fWritePosition(0)
{
	fReaderNotifier = create_sem(0, "StreamingRingBuffer read notify");
	fWriterNotifier = create_sem(0, "StreamingRingBuffer write notify");

	fBuffer = (uint8 *)malloc(fBufferSize);
	if (fBuffer == NULL)
		fBufferSize = 0;
}


/**
 * @brief Releases the semaphores and frees the ring storage.
 */
StreamingRingBuffer::~StreamingRingBuffer()
{
	delete_sem(fReaderNotifier);
	delete_sem(fWriterNotifier);
	free(fBuffer);
}


/**
 * @brief Reports whether construction succeeded.
 *
 * @return     B_OK if both semaphores and the backing storage are valid;
 *             otherwise the negative semaphore error code or B_NO_MEMORY.
 */
status_t
StreamingRingBuffer::InitCheck()
{
	if (fReaderNotifier < 0)
		return fReaderNotifier;
	if (fWriterNotifier < 0)
		return fWriterNotifier;
	if (fBuffer == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


/**
 * @brief Reads up to @a length bytes from the ring, blocking when empty.
 *
 * Pass @a buffer NULL to discard input (useful for skipping). When
 * @a onlyBlockOnNoData is true the call returns as soon as some data has
 * been copied even if @a length is not yet satisfied.
 *
 * @param buffer             Destination, or NULL to drop the bytes.
 * @param length             Maximum number of bytes to read.
 * @param onlyBlockOnNoData  If true, only block when no bytes have been
 *                           read yet; otherwise block until @a length is
 *                           fully satisfied.
 * @return                   Number of bytes read on success, B_CANCELED if
 *                           woken by MakeEmpty(), or a negative error code
 *                           on lock or semaphore failure.
 */
int32
StreamingRingBuffer::Read(void *buffer, size_t length, bool onlyBlockOnNoData)
{
	BAutolock readerLock(fReaderLocker);
	if (!readerLock.IsLocked())
		return B_ERROR;

	BAutolock dataLock(fDataLocker);
	if (!dataLock.IsLocked())
		return B_ERROR;

	int32 readSize = 0;
	while (length > 0) {
		size_t copyLength = min_c(length, fBufferSize - fReadPosition);
		copyLength = min_c(copyLength, fReadable);

		if (copyLength == 0) {
			if (onlyBlockOnNoData && readSize > 0)
				return readSize;

			fReaderWaiting = true;
			dataLock.Unlock();

			status_t result;
			do {
				TRACE("waiting in reader\n");
				result = acquire_sem(fReaderNotifier);
				TRACE("done waiting in reader with status: %#" B_PRIx32 "\n",
					result);
			} while (result == B_INTERRUPTED);

			if (result != B_OK)
				return result;

			if (!dataLock.Lock()) {
				TRACE_ERROR("failed to acquire data lock\n");
				return B_ERROR;
			}

			if (fCancelRead) {
				TRACE("read canceled\n");
				fCancelRead = false;
				return B_CANCELED;
			}

			continue;
		}

		// support discarding input
		if (buffer != NULL) {
			memcpy(buffer, fBuffer + fReadPosition, copyLength);
			buffer = (uint8 *)buffer + copyLength;
		}

		fReadPosition = (fReadPosition + copyLength) % fBufferSize;
		fReadable -= copyLength;
		readSize += copyLength;
		length -= copyLength;

		if (fWriterWaiting) {
			release_sem_etc(fWriterNotifier, 1, B_DO_NOT_RESCHEDULE);
			fWriterWaiting = false;
		}
	}

	return readSize;
}


/**
 * @brief Writes @a length bytes into the ring, blocking when full.
 *
 * Wakes a parked reader exactly when bytes become available. Returns
 * B_CANCELED if MakeEmpty() releases the writer while it is parked.
 *
 * @param buffer  Source bytes; must be non-NULL when @a length > 0.
 * @param length  Number of bytes to write.
 * @return        B_OK on success, B_CANCELED if woken by MakeEmpty(),
 *                or a negative error code on lock or semaphore failure.
 */
status_t
StreamingRingBuffer::Write(const void *buffer, size_t length)
{
	BAutolock writerLock(fWriterLocker);
	if (!writerLock.IsLocked())
		return B_ERROR;

	BAutolock dataLock(fDataLocker);
	if (!dataLock.IsLocked())
		return B_ERROR;

	while (length > 0) {
		size_t copyLength = min_c(length, fBufferSize - fWritePosition);
		copyLength = min_c(copyLength, fBufferSize - fReadable);

		if (copyLength == 0) {
			fWriterWaiting = true;
			dataLock.Unlock();

			status_t result;
			do {
				TRACE("waiting in writer\n");
				result = acquire_sem(fWriterNotifier);
				TRACE("done waiting in writer with status: %#" B_PRIx32 "\n",
					result);
			} while (result == B_INTERRUPTED);

			if (result != B_OK)
				return result;

			if (!dataLock.Lock()) {
				TRACE_ERROR("failed to acquire data lock\n");
				return B_ERROR;
			}

			if (fCancelWrite) {
				TRACE("write canceled\n");
				fCancelWrite = false;
				return B_CANCELED;
			}

			continue;
		}

		memcpy(fBuffer + fWritePosition, buffer, copyLength);
		fWritePosition = (fWritePosition + copyLength) % fBufferSize;
		fReadable += copyLength;

		buffer = (uint8 *)buffer + copyLength;
		length -= copyLength;

		if (fReaderWaiting) {
			release_sem_etc(fReaderNotifier, 1, B_DO_NOT_RESCHEDULE);
			fReaderWaiting = false;
		}
	}

	return B_OK;
}


/**
 * @brief Discards every buffered byte and wakes any parked reader/writer
 *        with a cancellation.
 *
 * Used by RemoteHWInterface when a connection is dropped to flush stale
 * outbound data and unblock the I/O threads.
 */
void
StreamingRingBuffer::MakeEmpty()
{
	BAutolock dataLock(fDataLocker);
	if (!dataLock.IsLocked())
		return;

	fReadPosition = fWritePosition = 0;
	fReadable = 0;

	if (fWriterWaiting) {
		release_sem_etc(fWriterNotifier, 1, 0);
		fWriterWaiting = false;
		fCancelWrite = true;
	}

	if (fReaderWaiting) {
		release_sem_etc(fReaderNotifier, 1, 0);
		fReaderWaiting = false;
		fCancelRead = true;
	}
}
