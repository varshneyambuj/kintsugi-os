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
 * @file NetReceiver.cpp
 * @brief Worker thread that pulls bytes off a BNetEndpoint and pushes them
 *        into a StreamingRingBuffer for the remote-protocol decoder.
 *
 * Two operating modes coexist: when constructed with a NewConnectionCallback
 * the thread runs as a listener, accepting incoming connections and giving
 * the callback a chance to validate each one before transferring data.
 * Without a callback the supplied endpoint is treated as already connected
 * and the thread simply pumps bytes into the target ring.
 */


#include "NetReceiver.h"
#include "RemoteMessage.h"

#include "StreamingRingBuffer.h"

#include <NetEndpoint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRACE(x...)			/*debug_printf("NetReceiver: " x)*/
#define TRACE_ERROR(x...)	debug_printf("NetReceiver: " x)


/**
 * @brief Constructs the receiver and immediately starts its worker thread.
 *
 * If @a newConnectionCallback is NULL the @a listener endpoint is treated
 * as already-connected and adopted by fEndpoint; otherwise it is held as
 * a listener and accepted connections are stored in fEndpoint.
 *
 * @param listener               Network endpoint to listen or read on.
 * @param target                 Ring buffer that receives incoming bytes.
 * @param newConnectionCallback  Optional gating callback invoked after
 *                               accept(); may be NULL.
 * @param newConnectionCookie    Opaque value passed to @a newConnectionCallback.
 */
NetReceiver::NetReceiver(BNetEndpoint *listener, StreamingRingBuffer *target,
	NewConnectionCallback newConnectionCallback, void *newConnectionCookie)
	:
	fListener(listener),
	fTarget(target),
	fReceiverThread(-1),
	fStopThread(false),
	fNewConnectionCallback(newConnectionCallback),
	fNewConnectionCookie(newConnectionCookie),
	fEndpoint(newConnectionCallback == NULL ? listener : NULL)
{
	fReceiverThread = spawn_thread(_NetworkReceiverEntry, "network receiver",
		B_NORMAL_PRIORITY, this);
	resume_thread(fReceiverThread);
}


/**
 * @brief Tells the worker to stop, releases the connected endpoint, and
 *        unblocks the thread from any pending recv().
 */
NetReceiver::~NetReceiver()
{
	fStopThread = true;
	fEndpoint.Unset();

	suspend_thread(fReceiverThread);
	resume_thread(fReceiverThread);
}


/**
 * @brief Static thread trampoline that selects listener or transfer mode.
 *
 * @param data  Pointer to the owning NetReceiver instance.
 * @return      The status_t result of the chosen loop.
 */
int32
NetReceiver::_NetworkReceiverEntry(void *data)
{
	NetReceiver *receiver = (NetReceiver *)data;
	if (receiver->fNewConnectionCallback)
		return receiver->_Listen();
	else
		return receiver->_Transfer();
}


/**
 * @brief Listens on the listener endpoint, accepts clients, runs the
 *        connection callback, and hands off to _Transfer() per client.
 *
 * Loops until fStopThread is set; rejected connections are discarded and
 * the loop continues. The accept() timeout (5 seconds) gives the loop a
 * chance to observe shutdown requests.
 *
 * @return     B_OK when the loop exits cleanly; otherwise the error code
 *             returned by Listen().
 */
status_t
NetReceiver::_Listen()
{
	status_t result = fListener->Listen();
	if (result != B_OK) {
		TRACE_ERROR("failed to listen on port: %s\n", strerror(result));
		return result;
	}

	while (!fStopThread) {
		fEndpoint.SetTo(fListener->Accept(5000));
		if (!fEndpoint.IsSet()) {
			TRACE("got NULL endpoint from accept\n");
			continue;
		}

		TRACE("new endpoint connection: %p\n", fEndpoint);

		if (fNewConnectionCallback != NULL
			&& fNewConnectionCallback(
				fNewConnectionCookie, *fEndpoint.Get()) != B_OK)
		{
			TRACE("connection callback rejected connection\n");
			continue;
		}

		_Transfer();
	}

	return B_OK;
}


/**
 * @brief Pumps bytes from the connected endpoint into the target ring buffer.
 *
 * Recovers from transient zero-byte reads up to five times; treats anything
 * past that as a disconnect. Returns when the socket reports an error, the
 * ring buffer rejects a write, or the receiver is asked to stop.
 *
 * @return     B_OK on clean shutdown, B_ERROR on inferred disconnect, or the
 *             negative error code from receive() / ring write().
 */
status_t
NetReceiver::_Transfer()
{
	int32 errorCount = 0;

	while (!fStopThread) {
		uint8 buffer[4096];
		int32 readSize = fEndpoint->Receive(buffer, sizeof(buffer));
		if (readSize < 0) {
			TRACE_ERROR("read failed, closing connection: %s\n",
				strerror(readSize));
			return readSize;
		}

		if (readSize == 0) {
			TRACE("read 0 bytes, retrying\n");
			snooze(100 * 1000);
			errorCount++;
			if (errorCount == 5) {
				TRACE_ERROR("failed to read, assuming disconnect\n");
				return B_ERROR;
			}

			continue;
		}

		errorCount = 0;
		status_t result = fTarget->Write(buffer, readSize);
		if (result != B_OK) {
			TRACE_ERROR("writing to ring buffer failed: %s\n",
				strerror(result));
			return result;
		}
	}

	return B_OK;
}
