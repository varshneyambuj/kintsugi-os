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
 * MIT License. Copyright 2009, Haiku, Inc.
 * Original author: Michael Lotz.
 */

/** @file StreamingRingBuffer.h
    @brief Single-producer / single-consumer ring buffer with blocking I/O,
           used between the protocol encoder/decoder and the network
           sender/receiver threads. */

#ifndef STREAMING_RING_BUFFER_H
#define STREAMING_RING_BUFFER_H

#include <OS.h>
#include <SupportDefs.h>
#include <Locker.h>

/** @brief Fixed-capacity byte ring buffer that blocks the reader when
           empty and the writer when full, using semaphores for handoff
           and BLockers to serialise reader vs writer state. */
class StreamingRingBuffer {
public:
								StreamingRingBuffer(size_t bufferSize);
								~StreamingRingBuffer();

		status_t				InitCheck();

		// blocking read and write
		int32					Read(void *buffer, size_t length,
									bool onlyBlockOnNoData = false);
		status_t				Write(const void *buffer, size_t length);

		void					MakeEmpty();

private:
		bool					fReaderWaiting;
		bool					fWriterWaiting;
		bool					fCancelRead;
		bool					fCancelWrite;
		sem_id					fReaderNotifier;
		sem_id					fWriterNotifier;

		BLocker					fReaderLocker;
		BLocker					fWriterLocker;
		BLocker					fDataLocker;

		uint8 *					fBuffer;
		size_t					fBufferSize;
		size_t					fReadable;
		int32					fReadPosition;
		int32					fWritePosition;
};

#endif // STREAMING_RING_BUFFER_H
