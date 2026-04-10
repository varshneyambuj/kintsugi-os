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

/** @file Queue.cpp
 *  @brief Implementation of the thread-safe producer-consumer queue. */


/*!	This is a simple multi thread save queue.

	One thread calls AddItem() to add items, and when it is finished doing
	so, it calls Terminate(). Another thread calls RemoveItem() to remove
	items from the queue. RemoveItem() blocks when no items are available.
	As soon as Terminate() is called and the queue is empty, RemoveItem()
	returns NULL.
*/


#include "Queue.h"

#include <Autolock.h>
#include <OS.h>


/** @brief Constructs the queue with its internal lock and semaphore. */
Queue::Queue()
	:
	BLocker("queue locker"),
	fSem(create_sem(0, "queue sem"))
{
}


/** @brief Destroys the queue and deletes the semaphore if still valid. */
Queue::~Queue()
{
	if (fSem >= 0)
		delete_sem(fSem);
}


/**
 * @brief Terminates the queue by deleting its semaphore.
 *
 * This unblocks any thread waiting in RemoveItem() and prevents further
 * additions.
 *
 * @return B_OK on success, B_ERROR if already terminated.
 */
status_t
Queue::Terminate()
{
	BAutolock _(this);

	if (fSem < 0)
		return B_ERROR;

	delete_sem(fSem);
	fSem = -1;
	return B_OK;
}


/**
 * @brief Adds an item to the queue and signals the consumer.
 *
 * @param item The item pointer to enqueue.
 * @return B_OK on success, B_ERROR if terminated, B_NO_MEMORY on allocation failure.
 */
status_t
Queue::AddItem(void* item)
{
	BAutolock _(this);

	if (fSem < 0)
		return B_ERROR;

	if (!fList.AddItem(item))
		return B_NO_MEMORY;

	release_sem(fSem);
	return B_OK;
}


/**
 * @brief Blocks until an item is available, then removes and returns it.
 *
 * Returns NULL when the queue has been terminated and is empty.
 *
 * @return The dequeued item pointer, or NULL if the queue is terminated and empty.
 */
void*
Queue::RemoveItem()
{
	// if the semaphore is deleted by Terminate(),
	// this will no longer block
	while (acquire_sem(fSem) == B_INTERRUPTED)
		;

	BAutolock _(this);

	// if the list is empty, which can only happen after
	// Terminate() was called, item will be NULL
	return fList.RemoveItem((int32)0);
}
