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
 * @file RemoteEventStream.cpp
 * @brief Replays RP_* input events arriving from the remote viewer as
 *        BMessages on the app_server's input path.
 *
 * The stream maintains the running mouse position, button mask, and
 * modifier state because individual RP messages only carry the fields that
 * have actually changed; everything else is filled in from this cache.
 */


#include "RemoteEventStream.h"

#include "RemoteMessage.h"
#include "StreamingRingBuffer.h"

#include <Autolock.h>

#include <new>


/**
 * @brief Constructs an empty event queue and notification semaphore.
 */
RemoteEventStream::RemoteEventStream()
	:
	fEventList(10),
	fEventListLocker("remote event list"),
	fEventNotification(-1),
	fWaitingOnEvent(false),
	fLatestMouseMovedEvent(NULL),
	fMousePosition(0, 0),
	fMouseButtons(0),
	fModifiers(0)
{
	fEventNotification = create_sem(0, "remote event notification");
}


/**
 * @brief Releases the notification semaphore; queued events are owned by
 *        the BObjectList and freed automatically.
 */
RemoteEventStream::~RemoteEventStream()
{
	delete_sem(fEventNotification);
}


/**
 * @brief Hook invoked when the screen bounds change.
 *
 * The remote stream tracks the viewer's reported geometry implicitly via
 * incoming events, so this is a no-op.
 *
 * @param bounds  New screen bounds (ignored).
 */
void
RemoteEventStream::UpdateScreenBounds(BRect bounds)
{
}


/**
 * @brief Blocks until an event is available and returns the oldest one.
 *
 * Called by the EventDispatcher loop. When the queue is empty the call
 * parks on fEventNotification until EventReceived() releases it.
 *
 * @param _event  Output, receives the dequeued message; ownership transfers
 *                to the caller.
 * @return        true on success, false if the lock could not be reacquired
 *                after waking.
 */
bool
RemoteEventStream::GetNextEvent(BMessage** _event)
{
	BAutolock lock(fEventListLocker);
	while (fEventList.CountItems() == 0) {
		fWaitingOnEvent = true;
		lock.Unlock();

		status_t result;
		do {
			result = acquire_sem(fEventNotification);
		} while (result == B_INTERRUPTED);

		lock.Lock();
		if (!lock.IsLocked())
			return false;
	}

	*_event = fEventList.RemoveItemAt(0);
	return true;
}


/**
 * @brief Pushes a synthesised event onto the queue (used internally and by
 *        callers that want to inject synthetic input).
 *
 * @param event  Message to enqueue; ownership transfers to the stream.
 * @return       B_OK on success, B_ERROR if locking or list insertion fails.
 */
status_t
RemoteEventStream::InsertEvent(BMessage* event)
{
	BAutolock lock(fEventListLocker);
	if (!lock.IsLocked())
		return B_ERROR;

	if (!fEventList.AddItem(event))
		return B_ERROR;

	if (event->what == B_MOUSE_MOVED)
		fLatestMouseMovedEvent = event;

	return B_OK;
}


/**
 * @brief Returns the most recent B_MOUSE_MOVED message without dequeueing.
 *
 * @return     Pointer to the cached event, or NULL if none has been seen.
 */
BMessage*
RemoteEventStream::PeekLatestMouseMoved()
{
	return fLatestMouseMovedEvent;
}


/**
 * @brief Decodes one RP_* input message and enqueues the resulting BMessage.
 *
 * Recognises mouse motion / buttons / wheel, key down/up, and modifier
 * changes. Unknown codes are silently ignored. Updates the cached running
 * mouse position, button mask, and modifier state so that subsequent partial
 * messages can be filled in.
 *
 * @param message  Decoded RemoteMessage with payload positioned at the
 *                 input-event fields.
 * @return         true if a message was synthesised and enqueued, false if
 *                 the code was not an input event or memory allocation
 *                 failed.
 */
bool
RemoteEventStream::EventReceived(RemoteMessage& message)
{
	uint16 code = message.Code();
	uint32 what = 0;
	switch (code) {
		case RP_MOUSE_MOVED:
			what = B_MOUSE_MOVED;
			break;
		case RP_MOUSE_DOWN:
			what = B_MOUSE_DOWN;
			break;
		case RP_MOUSE_UP:
			what = B_MOUSE_UP;
			break;
		case RP_MOUSE_WHEEL_CHANGED:
			what = B_MOUSE_WHEEL_CHANGED;
			break;
		case RP_KEY_DOWN:
			what = B_KEY_DOWN;
			break;
		case RP_KEY_UP:
			what = B_KEY_UP;
			break;
		case RP_MODIFIERS_CHANGED:
			what = B_MODIFIERS_CHANGED;
			break;
	}

	if (what == 0)
		return false;

	BMessage* event = new BMessage(what);
	if (event == NULL)
		return false;

	event->AddInt64("when", system_time());

	switch (code) {
		case RP_MOUSE_MOVED:
		case RP_MOUSE_DOWN:
		case RP_MOUSE_UP:
		{
			message.Read(fMousePosition);
			if (code != RP_MOUSE_MOVED)
				message.Read(fMouseButtons);

			event->AddPoint("where", fMousePosition);
			event->AddInt32("buttons", fMouseButtons);
			event->AddInt32("modifiers", fModifiers);

			if (code == RP_MOUSE_DOWN) {
				int32 clicks;
				if (message.Read(clicks) == B_OK)
					event->AddInt32("clicks", clicks);
			}

			if (code == RP_MOUSE_MOVED)
				fLatestMouseMovedEvent = event;
			break;
		}

		case RP_MOUSE_WHEEL_CHANGED:
		{
			float xDelta, yDelta;
			message.Read(xDelta);
			message.Read(yDelta);
			event->AddFloat("be:wheel_delta_x", xDelta);
			event->AddFloat("be:wheel_delta_y", yDelta);
			break;
		}

		case RP_KEY_DOWN:
		case RP_KEY_UP:
		{
			int32 numBytes;
			if (message.Read(numBytes) != B_OK)
				break;

			if (numBytes > 1000)
				break;

			char* bytes = (char*)malloc(numBytes + 1);
			if (bytes == NULL)
				break;

			if (message.ReadList(bytes, numBytes) != B_OK) {
				free(bytes);
				break;
			}

			for (int32 i = 0; i < numBytes; i++)
				event->AddInt8("byte", (int8)bytes[i]);

			bytes[numBytes] = 0;
			event->AddData("bytes", B_STRING_TYPE, bytes, numBytes + 1, false);
			event->AddInt32("modifiers", fModifiers);

			int32 rawChar;
			if (message.Read(rawChar) == B_OK)
				event->AddInt32("raw_char", rawChar);

			int32 key;
			if (message.Read(key) == B_OK)
				event->AddInt32("key", key);

			free(bytes);
			break;
		}

		case RP_MODIFIERS_CHANGED:
		{
			event->AddInt32("be:old_modifiers", fModifiers);
			message.Read(fModifiers);
			event->AddInt32("modifiers", fModifiers);
			break;
		}
	}

	BAutolock lock(fEventListLocker);
	fEventList.AddItem(event);
	if (fWaitingOnEvent) {
		fWaitingOnEvent = false;
		lock.Unlock();
		release_sem(fEventNotification);
	}

	return true;
}
