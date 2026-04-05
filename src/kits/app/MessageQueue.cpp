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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Unknown? Eric?
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file MessageQueue.cpp
 * @brief Implementation of BMessageQueue, a thread-safe FIFO queue for BMessage objects.
 *
 * BMessageQueue provides a locked, singly-linked list that stores BMessage pointers
 * in insertion order. It is used internally by BLooper to queue incoming messages
 * for sequential dispatch.
 */


#include <MessageQueue.h>
#include <Autolock.h>
#include <Message.h>


/** @brief Construct an empty message queue. */
BMessageQueue::BMessageQueue()
	:
	fHead(NULL),
	fTail(NULL),
	fMessageCount(0),
	fLock("BMessageQueue Lock")
{
}


/** @brief Destroy the message queue, deleting all remaining messages. */
BMessageQueue::~BMessageQueue()
{
	if (!Lock())
		return;

	BMessage* message = fHead;
	while (message != NULL) {
		BMessage* next = message->fQueueLink;

		delete message;
		message = next;
	}
}


/** @brief Append a message to the tail of the queue.
 *
 *  The message's queue link is set to NULL and it becomes the new tail.
 *  If the message is NULL or the queue lock cannot be acquired, the call
 *  is silently ignored.
 *
 *  @param message The BMessage to add. Ownership is transferred to the queue.
 */
void
BMessageQueue::AddMessage(BMessage* message)
{
	if (message == NULL)
		return;

	BAutolock _(fLock);
	if (!IsLocked())
		return;

	// The message passed in will be the last message on the queue so its
	// link member should be set to null.
	message->fQueueLink = NULL;

	fMessageCount++;

	if (fTail == NULL) {
		// there are no messages in the queue yet
		fHead = fTail = message;
	} else {
		// just add it after the tail
		fTail->fQueueLink = message;
		fTail = message;
	}
}


/** @brief Remove a specific message from the queue without deleting it.
 *
 *  Walks the linked list to locate the given message and unlinks it.
 *  The caller regains ownership of the message. If the message is not
 *  found, or is NULL, the call is silently ignored.
 *
 *  @param message The BMessage to remove from the queue.
 */
void
BMessageQueue::RemoveMessage(BMessage* message)
{
	if (message == NULL)
		return;

	BAutolock _(fLock);
	if (!IsLocked())
		return;

	BMessage* last = NULL;
	for (BMessage* entry = fHead; entry != NULL; entry = entry->fQueueLink) {
		if (entry == message) {
			// remove this one
			if (entry == fHead)
				fHead = entry->fQueueLink;
			else
				last->fQueueLink = entry->fQueueLink;

			if (entry == fTail)
				fTail = last;

			fMessageCount--;
			return;
		}
		last = entry;
	}
}


/** @brief Return the number of messages currently in the queue.
 *  @return The message count.
 */
int32
BMessageQueue::CountMessages() const
{
    return fMessageCount;
}


/** @brief Check whether the queue contains no messages.
 *  @return true if the queue is empty, false otherwise.
 */
bool
BMessageQueue::IsEmpty() const
{
    return fMessageCount == 0;
}


/** @brief Find a message by its zero-based position in the queue.
 *  @param index The zero-based index of the desired message.
 *  @return Pointer to the message at the given index, or NULL if the index
 *          is out of range or the lock cannot be acquired.
 */
BMessage*
BMessageQueue::FindMessage(int32 index) const
{
	BAutolock _(fLock);
	if (!IsLocked())
		return NULL;

	if (index < 0 || index >= fMessageCount)
		return NULL;

	for (BMessage* message = fHead; message != NULL; message = message->fQueueLink) {
		// If the index reaches zero, then we have found a match.
		if (index == 0)
			return message;

		index--;
	}

    return NULL;
}


/** @brief Find the nth message with a specific command code.
 *  @param what The message command code to search for.
 *  @param index The zero-based occurrence index among messages matching @a what.
 *  @return Pointer to the matching message, or NULL if no match is found,
 *          the index is out of range, or the lock cannot be acquired.
 */
BMessage*
BMessageQueue::FindMessage(uint32 what, int32 index) const
{
	BAutolock _(fLock);
	if (!IsLocked())
		return NULL;

	if (index < 0 || index >= fMessageCount)
		return NULL;

	for (BMessage* message = fHead; message != NULL; message = message->fQueueLink) {
		if (message->what == what) {
			// If the index reaches zero, then we have found a match.
			if (index == 0)
				return message;

			index--;
		}
	}

    return NULL;
}


/** @brief Acquire the queue's lock, blocking until it is available.
 *  @return true if the lock was successfully acquired, false on error.
 */
bool
BMessageQueue::Lock()
{
    return fLock.Lock();
}


/** @brief Release the queue's lock. */
void
BMessageQueue::Unlock()
{
	fLock.Unlock();
}


/** @brief Check whether the queue's lock is currently held by any thread.
 *  @return true if the lock is held, false otherwise.
 */
bool
BMessageQueue::IsLocked() const
{
	return fLock.IsLocked();
}


/** @brief Remove and return the message at the head of the queue.
 *
 *  The caller takes ownership of the returned message and is responsible
 *  for deleting it. If the queue is empty or the lock cannot be acquired,
 *  NULL is returned.
 *
 *  @return Pointer to the dequeued message, or NULL if the queue is empty.
 */
BMessage*
BMessageQueue::NextMessage()
{
	BAutolock _(fLock);
	if (!IsLocked())
		return NULL;

	// remove the head of the queue, if any, and return it

	BMessage* head = fHead;
	if (head == NULL)
		return NULL;

	fMessageCount--;
	fHead = head->fQueueLink;

	if (fHead == NULL) {
		// If the queue is empty after removing the front element,
		// we need to set the tail of the queue to NULL since the queue
		// is now empty.
		fTail = NULL;
	}

	return head;
}


/** @brief Check whether a given message is at the head of the queue.
 *  @param message The message to test.
 *  @return true if @a message is the next message to be dequeued, false otherwise.
 */
bool
BMessageQueue::IsNextMessage(const BMessage* message) const
{
	BAutolock _(fLock);
	return fHead == message;
}


/** @brief Non-const overload of IsLocked for R5 binary compatibility.
 *
 *  This method exists solely for backward compatibility with the R5 ABI
 *  and should not be used in new code. Prefer the const overload.
 *
 *  @return true if the lock is held, false otherwise.
 */
bool
BMessageQueue::IsLocked()
{
	return fLock.IsLocked();
}


void BMessageQueue::_ReservedMessageQueue1() {}
void BMessageQueue::_ReservedMessageQueue2() {}
void BMessageQueue::_ReservedMessageQueue3() {}

