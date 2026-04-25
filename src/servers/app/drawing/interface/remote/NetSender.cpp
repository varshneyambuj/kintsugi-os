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
 *   Copyright 2009, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Lotz <mmlr@mlotz.ch>
 */


/**
 * @file NetSender.cpp
 * @brief Worker thread that drains a StreamingRingBuffer and pushes the
 *        bytes onto a BNetEndpoint for the remote display protocol.
 */


#include "NetSender.h"

#include "StreamingRingBuffer.h"

#include <NetEndpoint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRACE(x...)			/*debug_printf("NetSender: " x)*/
#define TRACE_ERROR(x...)	debug_printf("NetSender: " x)


/**
 * @brief Constructs the sender and immediately starts its worker thread.
 *
 * @param endpoint  Connected network endpoint owned by the caller.
 * @param source    Ring buffer fed by the encoder side; bytes are drained
 *                  in blocking mode until the destructor stops the thread.
 */
NetSender::NetSender(BNetEndpoint *endpoint, StreamingRingBuffer *source)
	:
	fEndpoint(endpoint),
	fSource(source),
	fSenderThread(-1),
	fStopThread(false)
{
	fSenderThread = spawn_thread(_NetworkSenderEntry, "network sender",
		B_NORMAL_PRIORITY, this);
	resume_thread(fSenderThread);
}


/**
 * @brief Asks the worker to exit and unblocks it from any blocking read.
 *
 * @note  The thread observes fStopThread between blocking reads; the
 *        suspend/resume pair forces an immediate wake-up.
 */
NetSender::~NetSender()
{
	fStopThread = true;

	suspend_thread(fSenderThread);
	resume_thread(fSenderThread);
}


/**
 * @brief Static thread trampoline that forwards into the member loop.
 *
 * @param data  Pointer to the owning NetSender instance.
 * @return      The status_t result of _NetworkSender().
 */
int32
NetSender::_NetworkSenderEntry(void *data)
{
	return ((NetSender *)data)->_NetworkSender();
}


/**
 * @brief Runs the read-from-ring, write-to-socket loop until stopped.
 *
 * Reads up to 4 KiB from the source ring buffer (blocking) and re-sends
 * it through the endpoint, looping until either the ring or the socket
 * reports an error.
 *
 * @return     B_OK when the thread is asked to stop cleanly, otherwise the
 *             negative error code returned by the ring read or the socket
 *             send.
 */
status_t
NetSender::_NetworkSender()
{
	while (!fStopThread) {
		uint8 buffer[4096];
		int32 readSize = fSource->Read(buffer, sizeof(buffer), true);
		if (readSize < 0) {
			TRACE_ERROR("read failed, stopping sender thread: %s\n",
				strerror(readSize));
			return readSize;
		}

		while (readSize > 0) {
			int32 sendSize = fEndpoint->Send(buffer, readSize);
			if (sendSize < 0) {
				TRACE_ERROR("sending data failed: %s\n", strerror(sendSize));
				return sendSize;
			}

			readSize -= sendSize;
		}
	}

	return B_OK;
}
