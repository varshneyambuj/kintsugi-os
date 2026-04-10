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
   This software is part of the Haiku distribution and is covered
   by the MIT License.
 */

/** @file PriorityMessageQueue.cpp
 *  @brief Thread-safe priority queue for BMessage objects awaiting dispatch. */

#include <Message.h>

#include "PriorityMessageQueue.h"

/** @brief Internal helper that pairs a BMessage with its dispatch priority. */
class PriorityMessageQueue::MessageInfo {
public:
	/**
	 * @brief Constructs a MessageInfo binding a message to its priority.
	 *
	 * @param message  The BMessage to store.
	 * @param priority The dispatch priority for this message.
	 */
	MessageInfo(BMessage *message, int32 priority)
		: fMessage(message),
		  fPriority(priority)
	{
	}

	/** @brief Returns the stored BMessage. */
	BMessage *Message() const	{ return fMessage; }
	/** @brief Returns the message's priority. */
	int32 Priority() const		{ return fPriority; }

private:
	BMessage	*fMessage;
	int32		fPriority;
};


/** @brief Constructs an empty PriorityMessageQueue with an initial capacity of 20. */
PriorityMessageQueue::PriorityMessageQueue()
	: fLock(),
	  fMessages(20)
{
}

/**
 * @brief Destroys the queue and deletes all remaining BMessage objects.
 */
PriorityMessageQueue::~PriorityMessageQueue()
{
	// delete the messages
	for (int32 i = 0; MessageInfo *info = fMessages.ItemAt(i); i++)
		delete info->Message();
	// the infos are deleted automatically
}

/**
 * @brief Acquires the queue's internal lock.
 *
 * @return @c true if the lock was acquired, @c false on failure.
 */
bool
PriorityMessageQueue::Lock()
{
	return fLock.Lock();
}

/** @brief Releases the queue's internal lock. */
void
PriorityMessageQueue::Unlock()
{
	fLock.Unlock();
}

/**
 * @brief Inserts a message into the queue at the correct priority position.
 *
 * Messages with higher priority values are placed before messages with lower
 * priority values. The queue is locked during the insertion.
 *
 * @param message  The BMessage to enqueue; must not be NULL.
 * @param priority The dispatch priority for this message.
 * @return @c true on success, @c false on failure (NULL message, lock failure,
 *         or out of memory).
 */
bool
PriorityMessageQueue::PushMessage(BMessage *message, int32 priority)
{
	bool result = (message);
	if (result)
		result = Lock();
	if (result) {
		if (MessageInfo *info = new MessageInfo(message, priority)) {
			// find the insertion index
			int32 index = _FindInsertionIndex(priority);
			if (!fMessages.AddItem(info, index)) {
				result = false;
				delete info;
			}
		} else	// no memory
			result = false;
		Unlock();
	}
	return result;
}

/**
 * @brief Removes and returns the highest-priority message from the queue.
 *
 * The caller takes ownership of the returned BMessage.
 *
 * @return The highest-priority BMessage, or NULL if the queue is empty or
 *         the lock cannot be acquired.
 */
BMessage *
PriorityMessageQueue::PopMessage()
{
	BMessage *result = NULL;
	if (Lock()) {
		if (MessageInfo *info = fMessages.RemoveItemAt(0)) {
			result = info->Message();
			delete info;
		}
		Unlock();
	}
	return result;
}

/**
 * @brief Returns the number of messages currently in the queue.
 *
 * @return The message count, or 0 if the lock cannot be acquired.
 */
int32
PriorityMessageQueue::CountMessages() const
{
	int32 result = 0;
	if (fLock.Lock()) {
		result = fMessages.CountItems();
		fLock.Unlock();
	}
	return result;
}

/**
 * @brief Returns whether the queue contains no messages.
 *
 * @return @c true if the queue is empty, @c false otherwise.
 */
bool
PriorityMessageQueue::IsEmpty() const
{
	return (CountMessages() == 0);
}

/**
 * @brief Performs a binary search to find the insertion index for a given priority.
 *
 * Messages are stored in descending priority order; this method returns the
 * index at which a new message with the given priority should be inserted to
 * maintain that ordering.
 *
 * @param priority The priority value to search for.
 * @return The zero-based insertion index.
 */
int32
PriorityMessageQueue::_FindInsertionIndex(int32 priority)
{
	int32 lower = 0;
	int32 upper = fMessages.CountItems();
	while (lower < upper) {
		int32 mid = (lower + upper) / 2;
		MessageInfo *info = fMessages.ItemAt(mid);
		if (info->Priority() >= priority)
			lower = mid + 1;
		else
			upper = mid;
	}
	return lower;
}

