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

/** @file NetSender.h
    @brief Background thread that ships bytes from a StreamingRingBuffer
           out over a BNetEndpoint to the remote viewer. */

#ifndef NET_SENDER_H
#define NET_SENDER_H

#include <OS.h>
#include <SupportDefs.h>

class BNetEndpoint;
class StreamingRingBuffer;

/** @brief Owns a worker thread that drains a StreamingRingBuffer and
           writes the data into the supplied BNetEndpoint. Used by the
           RemoteHWInterface to push encoded RemoteMessage frames to the
           remote display. */
class NetSender {
public:
								NetSender(BNetEndpoint *endpoint,
									StreamingRingBuffer *source);
								~NetSender();

private:
static	int32					_NetworkSenderEntry(void *data);
		status_t				_NetworkSender();

		BNetEndpoint *			fEndpoint;
		StreamingRingBuffer *	fSource;

		thread_id				fSenderThread;
		bool					fStopThread;
};

#endif // NET_SENDER_H
