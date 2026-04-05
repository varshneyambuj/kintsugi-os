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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */
#ifndef _MESSAGE_QUEUE_H
#define _MESSAGE_QUEUE_H

/**
 * @file MessageQueue.h
 * @brief Defines the BMessageQueue class for thread-safe FIFO message queuing.
 */

#include <Locker.h>
#include <Message.h>
	// For convenience


/**
 * @brief A thread-safe FIFO queue for BMessage objects.
 *
 * BMessageQueue implements a linked-list-based first-in-first-out queue for
 * BMessage pointers. It is used internally by BLooper to queue incoming
 * messages for dispatch. The queue provides locking for thread-safe access.
 *
 * Messages added to the queue are not copied; the queue takes ownership of
 * the message pointers. Messages removed via NextMessage() transfer ownership
 * to the caller.
 *
 * @see BMessage
 * @see BLooper
 */
class BMessageQueue {
public:
	/**
	 * @brief Default constructor.
	 *
	 * Creates an empty message queue.
	 */
								BMessageQueue();

	/**
	 * @brief Destructor.
	 *
	 * Destroys the queue and deletes any remaining messages.
	 */
	virtual						~BMessageQueue();

	/**
	 * @brief Adds a message to the end of the queue.
	 *
	 * The queue takes ownership of the message. The message must have been
	 * allocated with new.
	 *
	 * @param message  The message to add. Must not be NULL.
	 */
			void				AddMessage(BMessage* message);

	/**
	 * @brief Removes a specific message from the queue.
	 *
	 * The message is removed from the queue but not deleted. The caller
	 * regains ownership.
	 *
	 * @param message  The message to remove.
	 */
			void				RemoveMessage(BMessage* message);

	/**
	 * @brief Returns the number of messages in the queue.
	 *
	 * @return The message count.
	 */
			int32				CountMessages() const;

	/**
	 * @brief Checks whether the queue is empty.
	 *
	 * @return true if the queue contains no messages, false otherwise.
	 */
			bool				IsEmpty() const;

	/**
	 * @brief Finds a message by index.
	 *
	 * @param index  The zero-based index of the message to find.
	 * @return A pointer to the message, or NULL if the index is out of range.
	 *         The queue retains ownership.
	 */
			BMessage*			FindMessage(int32 index) const;

	/**
	 * @brief Finds a message by command and occurrence index.
	 *
	 * Searches for messages with a matching 'what' field and returns the
	 * Nth match (zero-based).
	 *
	 * @param what   The message command to search for.
	 * @param index  The zero-based occurrence index (default 0 for the first match).
	 * @return A pointer to the matching message, or NULL if not found.
	 *         The queue retains ownership.
	 */
			BMessage*			FindMessage(uint32 what, int32 index = 0) const;

	/**
	 * @brief Locks the queue for exclusive access.
	 *
	 * Blocks until the lock is acquired. Use Unlock() to release.
	 *
	 * @return true if the lock was acquired, false on error.
	 */
			bool				Lock();

	/**
	 * @brief Unlocks the queue.
	 *
	 * Releases the lock previously acquired with Lock().
	 */
			void				Unlock();

	/**
	 * @brief Checks whether the queue is currently locked by the calling thread.
	 *
	 * @return true if locked by the calling thread, false otherwise.
	 */
			bool				IsLocked() const;

	/**
	 * @brief Removes and returns the first message in the queue.
	 *
	 * Ownership of the message transfers to the caller, who is responsible
	 * for deleting it.
	 *
	 * @return A pointer to the first message, or NULL if the queue is empty.
	 */
			BMessage*			NextMessage();

	/**
	 * @brief Checks whether a specific message is at the head of the queue.
	 *
	 * @param message  The message to check.
	 * @return true if the message is the next to be dequeued, false otherwise.
	 */
			bool				IsNextMessage(const BMessage* message) const;

private:
			// Reserved space in the vtable for future changes to BMessageQueue
	virtual	void				_ReservedMessageQueue1();
	virtual	void				_ReservedMessageQueue2();
	virtual	void				_ReservedMessageQueue3();

								BMessageQueue(const BMessageQueue &);
			BMessageQueue&		operator=(const BMessageQueue &);

			bool				IsLocked();
				// this needs to be exported for R5 compatibility and should
				// be dropped as soon as possible

private:
			BMessage*			fHead;
			BMessage*			fTail;
			int32				fMessageCount;
	mutable	BLocker				fLock;

			uint32				_reserved[3];
};


#endif	// _MESSAGE_QUEUE_H
