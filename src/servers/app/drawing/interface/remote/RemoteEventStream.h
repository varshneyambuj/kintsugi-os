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

/** @file RemoteEventStream.h
    @brief EventStream that synthesises BMessages from remote-protocol input
           events arriving over the wire. */

#ifndef REMOTE_EVENT_STREAM_H
#define REMOTE_EVENT_STREAM_H

#include "EventStream.h"

#include <Locker.h>
#include <ObjectList.h>

class RemoteMessage;

/** @brief EventStream subclass that turns RP_* input messages decoded from
           the remote viewer into the BMessages app_server normally receives
           from input_server, and feeds them to the EventDispatcher. */
class RemoteEventStream : public EventStream {
public:
								RemoteEventStream();
virtual							~RemoteEventStream();

/** @brief Always reports the stream as valid; the wire link is the only
           failure point and is handled at the transport layer. */
virtual	bool					IsValid() { return true; }
/** @brief No-op: shutdown is driven by the owning RemoteHWInterface. */
virtual	void					SendQuit() {}

virtual	void					UpdateScreenBounds(BRect bounds);
virtual	bool					GetNextEvent(BMessage** _event);
virtual	status_t				InsertEvent(BMessage* event);
virtual	BMessage*				PeekLatestMouseMoved();

		bool					EventReceived(RemoteMessage& message);

private:
		BObjectList<BMessage, true> fEventList;
		BLocker					fEventListLocker;
		sem_id					fEventNotification;
		bool					fWaitingOnEvent;
		BMessage*				fLatestMouseMovedEvent;

		BPoint					fMousePosition;
		uint32					fMouseButtons;
		uint32					fModifiers;
};

#endif // REMOTE_EVENT_STREAM_H
