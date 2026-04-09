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
 *   Copyright 2005, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file EventStream.cpp
 *  @brief Abstract event stream and input-server-backed event stream implementation. */


#include "EventStream.h"

#include <InputServerTypes.h>
#include <ServerProtocol.h>
#include <shared_cursor_area.h>

#include <AppMisc.h>
#include <AutoDeleter.h>

#include <new>
#include <stdio.h>
#include <string.h>


/**
 * @brief Constructs a base EventStream object.
 */
EventStream::EventStream()
{
}


/**
 * @brief Destroys the EventStream.
 */
EventStream::~EventStream()
{
}


/**
 * @brief Indicates whether this stream supports a dedicated cursor thread.
 * @return false by default; subclasses may override.
 */
bool
EventStream::SupportsCursorThread() const
{
	return false;
}


/**
 * @brief Waits up to @a timeout microseconds for the next cursor position.
 *
 * The base implementation always returns B_ERROR. Subclasses that support a
 * cursor thread must override this method.
 *
 * @param where   Output parameter filled with the new cursor position.
 * @param timeout Maximum wait time in microseconds.
 * @return B_ERROR in the base implementation.
 */
status_t
EventStream::GetNextCursorPosition(BPoint& where, bigtime_t timeout)
{
	return B_ERROR;
}


//	#pragma mark -


/**
 * @brief Constructs an InputServerStream connected to the given input server messenger.
 *
 * Sends an IS_ACQUIRE_INPUT message to the input server and records the
 * returned event port and cursor semaphore.
 *
 * @param messenger A BMessenger targeting the input server.
 */
InputServerStream::InputServerStream(BMessenger& messenger)
	:
	fInputServer(messenger),
	fPort(-1),
	fQuitting(false),
	fLatestMouseMoved(NULL)
{
	BMessage message(IS_ACQUIRE_INPUT);
	message.AddInt32("remote team", BPrivate::current_team());

	fCursorArea = create_area("shared cursor", (void **)&fCursorBuffer, B_ANY_ADDRESS,
		B_PAGE_SIZE, B_LAZY_LOCK, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
	if (fCursorArea >= B_OK)
		message.AddInt32("cursor area", fCursorArea);

	BMessage reply;
	if (messenger.SendMessage(&message, &reply) != B_OK)
		return;

	if (reply.FindInt32("event port", &fPort) != B_OK)
		fPort = -1;
	if (reply.FindInt32("cursor semaphore", &fCursorSemaphore) != B_OK)
		fCursorSemaphore = -1;
}


#if TEST_MODE
/**
 * @brief Constructs an InputServerStream for test mode using a named port.
 */
InputServerStream::InputServerStream()
	:
	fQuitting(false),
	fCursorSemaphore(-1),
	fLatestMouseMoved(NULL)
{
	fPort = find_port(SERVER_INPUT_PORT);
}
#endif


/**
 * @brief Destroys the InputServerStream, releasing the shared cursor area.
 */
InputServerStream::~InputServerStream()
{
	delete_area(fCursorArea);
}


/**
 * @brief Checks whether the underlying event port is still valid.
 * @return true if the port is usable, false otherwise.
 */
bool
InputServerStream::IsValid()
{
	port_info portInfo;
	if (fPort < B_OK || get_port_info(fPort, &portInfo) != B_OK)
		return false;

	return true;
}


/**
 * @brief Signals the stream to stop processing events and exit its loops.
 */
void
InputServerStream::SendQuit()
{
	fQuitting = true;
	write_port(fPort, 'quit', NULL, 0);
	release_sem(fCursorSemaphore);
}


/**
 * @brief Notifies the input server of updated screen bounds.
 * @param bounds The new screen bounding rectangle.
 */
void
InputServerStream::UpdateScreenBounds(BRect bounds)
{
	BMessage update(IS_SCREEN_BOUNDS_UPDATED);
	update.AddRect("screen_bounds", bounds);

	fInputServer.SendMessage(&update);
}


/**
 * @brief Blocks until the next input event is available, then returns it.
 *
 * Drains the event port completely before returning so that the internal
 * queue stays fresh. The latest B_MOUSE_MOVED event pointer is tracked for
 * B_NO_POINTER_HISTORY support.
 *
 * @param _event Output pointer set to the next BMessage event (caller owns it).
 * @return true while events are available, false when the port has been deleted
 *         (i.e. the input server has died).
 */
bool
InputServerStream::GetNextEvent(BMessage** _event)
{
	while (fEvents.IsEmpty()) {
		// wait for new events
		BMessage* event;
		status_t status = _MessageFromPort(&event);
		if (status == B_OK) {
			if (event->what == B_MOUSE_MOVED)
				fLatestMouseMoved = event;

			fEvents.AddMessage(event);
		} else if (status == B_BAD_PORT_ID) {
			// our port got deleted - the input_server must have died
			fPort = -1;
			return false;
		}

		int32 count = port_count(fPort);
		if (count > 0) {
			// empty port queue completely while we're at it
			for (int32 i = 0; i < count; i++) {
				if (_MessageFromPort(&event, 0) == B_OK) {
					if (event->what == B_MOUSE_MOVED)
						fLatestMouseMoved = event;
					fEvents.AddMessage(event);
				}
			}
		}
	}

	// there are items in our list, so just work through them

	*_event = fEvents.NextMessage();
	return true;
}


/**
 * @brief Reads the next cursor position from the shared cursor area.
 *
 * Blocks on the cursor semaphore for up to @a timeout microseconds.
 * After reading, signals the input server that the position has been consumed.
 *
 * @param where   Output parameter filled with the cursor position.
 * @param timeout Maximum wait time in microseconds.
 * @return B_OK on success, B_TIMED_OUT on timeout, B_ERROR if the semaphore
 *         has become invalid.
 */
status_t
InputServerStream::GetNextCursorPosition(BPoint &where, bigtime_t timeout)
{
	status_t status;

	do {
		status = acquire_sem_etc(fCursorSemaphore, 1, B_RELATIVE_TIMEOUT,
			timeout);
	} while (status == B_INTERRUPTED);

	if (status == B_TIMED_OUT)
		return status;

	if (status == B_BAD_SEM_ID) {
		// the semaphore is no longer valid - the input_server must have died
		fCursorSemaphore = -1;
		return B_ERROR;
	}

#ifdef HAIKU_TARGET_PLATFORM_HAIKU
	uint32 pos = atomic_get((int32*)&fCursorBuffer->pos);
#else
	uint32 pos = fCursorBuffer->pos;
#endif

	where.x = pos >> 16UL;
	where.y = pos & 0xffff;

	atomic_and(&fCursorBuffer->read, 0);
		// this tells the input_server that we've read the
		// cursor position and want to be notified if updated

	if (fQuitting) {
		fQuitting = false;
		return B_ERROR;
	}

	return B_OK;
}


/**
 * @brief Inserts an already-constructed event at the front of the internal queue.
 *
 * Also writes a marker to the event port so that a blocked GetNextEvent() call
 * wakes up.
 *
 * @param event The BMessage event to insert (stream takes ownership).
 * @return B_OK on success, B_BAD_PORT_ID if the port is no longer valid.
 */
status_t
InputServerStream::InsertEvent(BMessage* event)
{
	fEvents.AddMessage(event);
	status_t status = write_port_etc(fPort, 'insm', NULL, 0, B_RELATIVE_TIMEOUT,
		0);
	if (status == B_BAD_PORT_ID)
		return status;

	// If the port is full, we obviously don't care to report this, as we
	// already placed our message.
	return B_OK;
}


/**
 * @brief Returns the most recent B_MOUSE_MOVED event seen by this stream.
 * @return Pointer to the latest mouse-moved BMessage, or NULL if none received.
 */
BMessage*
InputServerStream::PeekLatestMouseMoved()
{
	return fLatestMouseMoved;
}


/**
 * @brief Reads and deserializes a single message from the event port.
 *
 * Blocks for up to @a timeout microseconds. Returns B_BAD_PORT_ID when the
 * port has been deleted and B_INTERRUPTED when an "inserted message" marker
 * ('insm') is read.
 *
 * @param _message Output pointer set to the deserialized BMessage.
 * @param timeout  Maximum wait time in microseconds (default B_INFINITE_TIMEOUT).
 * @return B_OK on success, or an error code on failure.
 */
status_t
InputServerStream::_MessageFromPort(BMessage** _message, bigtime_t timeout)
{
	uint8 *buffer = NULL;
	ssize_t bufferSize;

	// read message from port

	do {
		bufferSize = port_buffer_size_etc(fPort, B_RELATIVE_TIMEOUT, timeout);
	} while (bufferSize == B_INTERRUPTED);

	if (bufferSize < B_OK)
		return bufferSize;

	if (bufferSize > 0) {
		buffer = new (std::nothrow) uint8[bufferSize];
		if (buffer == NULL)
			return B_NO_MEMORY;
	}

	int32 code;
	bufferSize = read_port_etc(fPort, &code, buffer, bufferSize,
		B_RELATIVE_TIMEOUT, 0);
	if (bufferSize < B_OK) {
		delete[] buffer;
		return bufferSize;
	}

	if (code == 'quit') {
		// this will cause GetNextEvent() to return false
		return B_BAD_PORT_ID;
	}
	if (code == 'insm') {
		// a message has been inserted into our queue
		return B_INTERRUPTED;
	}

	// we have the message, now let's unflatten it

	ObjectDeleter<BMessage> message(new BMessage(code));
	if (!message.IsSet())
		return B_NO_MEMORY;

	if (buffer == NULL) {
		*_message = message.Detach();
		return B_OK;
	}

	status_t status = message->Unflatten((const char*)buffer);
	delete[] buffer;

	if (status != B_OK) {
		printf("Unflatten event failed: %s, port message code was: %" B_PRId32
			" - %c%c%c%c\n", strerror(status), code, (int8)(code >> 24),
			(int8)(code >> 16), (int8)(code >> 8), (int8)code);
		return status;
	}

	*_message = message.Detach();
	return B_OK;
}
