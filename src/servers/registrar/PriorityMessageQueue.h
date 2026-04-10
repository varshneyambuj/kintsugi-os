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

/** @file PriorityMessageQueue.h
 *  @brief Thread-safe priority queue for ordering and dispatching BMessage objects. */

#ifndef PRIORITY_MESSAGE_QUEUE_H
#define PRIORITY_MESSAGE_QUEUE_H

#include <Locker.h>
#include <ObjectList.h>

class BMessage;

/** @brief Queues BMessages by priority for ordered retrieval. */
class PriorityMessageQueue {
public:
	PriorityMessageQueue();
	~PriorityMessageQueue();

	/** @brief Acquires the queue lock for thread-safe access. */
	bool Lock();
	/** @brief Releases the queue lock. */
	void Unlock();

	/** @brief Inserts a message into the queue at the given priority level. */
	bool PushMessage(BMessage *message, int32 priority = 0);
	/** @brief Removes and returns the highest-priority message from the queue. */
	BMessage *PopMessage();

	/** @brief Returns the number of messages currently in the queue. */
	int32 CountMessages() const;
	/** @brief Returns whether the queue contains no messages. */
	bool IsEmpty() const;

private:
	int32 _FindInsertionIndex(int32 priority);

private:
	class MessageInfo;

private:
	mutable BLocker				fLock;
	BObjectList<MessageInfo, true> fMessages;
};

#endif	// PRIORITY_MESSAGE_QUEUE_H
