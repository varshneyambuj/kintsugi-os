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
 * MIT License. Copyright 2005, Haiku, Inc. All Rights Reserved.
 * Original author: Axel Dörfler, axeld@pinc-software.de.
 */

/** @file EventStream.h
    @brief Abstract input event source and its input_server-backed implementation. */

#ifndef EVENT_STREAM_H
#define EVENT_STREAM_H


#include <LinkReceiver.h>
#include <MessageQueue.h>
#include <Messenger.h>


struct shared_cursor;


/** @brief Abstract interface that supplies input events (keyboard, mouse, etc.)
           to the EventDispatcher. */
class EventStream {
	public:
		EventStream();
		virtual ~EventStream();

		/** @brief Returns true if this stream is connected and delivering events.
		    @return true if valid. */
		virtual bool IsValid() = 0;

		/** @brief Signals the stream to stop producing events and terminate. */
		virtual void SendQuit() = 0;

		/** @brief Returns true if this stream provides a dedicated cursor update thread.
		    @return true if a cursor thread is supported. */
		virtual bool SupportsCursorThread() const;

		/** @brief Updates the stream with the new screen bounds (for clipping cursor position).
		    @param bounds New screen bounding rectangle. */
		virtual void UpdateScreenBounds(BRect bounds) = 0;

		/** @brief Retrieves the next queued event from the stream.
		    @param _event Output pointer to the next BMessage; caller takes ownership.
		    @return true if an event was returned. */
		virtual bool GetNextEvent(BMessage** _event) = 0;

		/** @brief Retrieves the next cursor position update from the cursor thread.
		    @param where Output cursor position.
		    @param timeout Maximum wait time in microseconds.
		    @return B_OK on success, B_TIMED_OUT if no update arrived. */
		virtual status_t GetNextCursorPosition(BPoint& where,
				bigtime_t timeout = B_INFINITE_TIMEOUT);

		/** @brief Inserts a synthetic event at the front of the queue.
		    @param event The BMessage to inject; stream takes ownership.
		    @return B_OK on success, or an error code. */
		virtual status_t InsertEvent(BMessage* event) = 0;

		/** @brief Returns the most recent mouse-moved event without removing it.
		    @return Pointer to the latest mouse-moved BMessage, or NULL. */
		virtual BMessage* PeekLatestMouseMoved() = 0;
};


/** @brief EventStream implementation that reads events from the input_server via a port. */
class InputServerStream : public EventStream {
	public:
		/** @brief Constructs a stream connected to the input_server messenger.
		    @param inputServerMessenger BMessenger targeting the input_server. */
		InputServerStream(BMessenger& inputServerMessenger);
#if TEST_MODE
		InputServerStream();
#endif

		virtual ~InputServerStream();

		/** @brief Returns true if the port connection to input_server is healthy.
		    @return true if valid. */
		virtual bool IsValid();

		/** @brief Sends a quit request to the input_server event thread. */
		virtual void SendQuit();

		/** @brief Returns true if a cursor semaphore is available.
		    @return true if cursor thread is supported. */
		virtual bool SupportsCursorThread() const { return fCursorSemaphore >= B_OK; }

		/** @brief Updates the input_server with new screen bounds.
		    @param bounds New screen bounding rectangle. */
		virtual void UpdateScreenBounds(BRect bounds);

		/** @brief Reads the next event from the input_server port queue.
		    @param _event Output pointer to the next BMessage.
		    @return true if an event was returned. */
		virtual bool GetNextEvent(BMessage** _event);

		/** @brief Waits for a cursor position update via the cursor semaphore.
		    @param where Output cursor position.
		    @param timeout Maximum wait time in microseconds.
		    @return B_OK on success, B_TIMED_OUT if no update arrived. */
		virtual status_t GetNextCursorPosition(BPoint& where,
				bigtime_t timeout = B_INFINITE_TIMEOUT);

		/** @brief Inserts a synthetic event at the front of the queue.
		    @param event The BMessage to inject.
		    @return B_OK on success, or an error code. */
		virtual status_t InsertEvent(BMessage* event);

		/** @brief Returns the most recent mouse-moved event without removing it.
		    @return Pointer to the latest mouse-moved BMessage, or NULL. */
		virtual BMessage* PeekLatestMouseMoved();

	private:
		status_t _MessageFromPort(BMessage** _message,
			bigtime_t timeout = B_INFINITE_TIMEOUT);

		BMessenger fInputServer;
		BMessageQueue fEvents;
		port_id	fPort;
		bool	fQuitting;
		sem_id	fCursorSemaphore;
		area_id	fCursorArea;
		shared_cursor* fCursorBuffer;
		BMessage* fLatestMouseMoved;
};

#endif	/* EVENT_STREAM_H */
