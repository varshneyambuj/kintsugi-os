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
 * MIT License. Copyright 2009, 2017, Haiku, Inc.
 * Original author: Michael Lotz.
 */

/** @file NetReceiver.h
    @brief Background thread that listens or reads on a BNetEndpoint and
           feeds bytes into a StreamingRingBuffer for the protocol decoder. */

#ifndef NET_RECEIVER_H
#define NET_RECEIVER_H

#include <AutoDeleter.h>
#include <OS.h>
#include <SupportDefs.h>

class BNetEndpoint;
class StreamingRingBuffer;

/** @brief Callback invoked once a new incoming connection has been
           accepted, allowing the owner to negotiate or reject the link.
    @param cookie    Opaque value supplied to the NetReceiver constructor.
    @param endpoint  Newly accepted endpoint; valid only for the duration
                     of the call.
    @return B_OK to keep the connection, anything else to drop it. */
typedef status_t (*NewConnectionCallback)(void *cookie, BNetEndpoint &endpoint);


/** @brief Owns a worker thread that either accepts incoming connections
           on a listener endpoint or transfers bytes from an already
           connected endpoint into a StreamingRingBuffer for the decoder. */
class NetReceiver {
public:
								NetReceiver(BNetEndpoint *endpoint,
									StreamingRingBuffer *target,
									NewConnectionCallback callback = NULL,
									void *newConnectionCookie = NULL);
								~NetReceiver();

		/** @brief Returns the currently active endpoint (the accepted
		           client when listening, or the original endpoint when
		           in transfer-only mode). */
		BNetEndpoint *			Endpoint() { return fEndpoint.Get(); }

private:
static	int32					_NetworkReceiverEntry(void *data);
		status_t				_Listen();
		status_t				_Transfer();

		BNetEndpoint *			fListener;
		StreamingRingBuffer *	fTarget;

		thread_id				fReceiverThread;
		bool					fStopThread;

		NewConnectionCallback	fNewConnectionCallback;
		void *					fNewConnectionCookie;

		ObjectDeleter<BNetEndpoint>
								fEndpoint;
};

#endif // NET_RECEIVER_H
