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
 *   Copyright 2002, Marcus Overhagen. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file Queue.h
 *  @brief Thread-safe producer-consumer queue using a semaphore for blocking removal. */

#ifndef QUEUE_H
#define QUEUE_H


#include <List.h>
#include <Locker.h>


/** @brief A simple thread-safe queue for inter-thread message passing. */
class Queue : BLocker {
public:
	/** @brief Construct the queue and create the counting semaphore. */
								Queue();
	/** @brief Destroy the queue and delete the semaphore if still open. */
								~Queue();

	/** @brief Signal the queue to stop accepting items and unblock consumers. */
			status_t			Terminate();

	/** @brief Add an item to the tail of the queue. */
			status_t			AddItem(void* item);
	/** @brief Remove and return the head item, blocking until one is available. */
			void*				RemoveItem();

private:
			BList				fList;
			sem_id				fSem;
};


#endif	// QUEUE_H
