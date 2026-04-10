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
 *   Copyright 2025, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file slab_queue.h
 *  @brief Singly-linked free list used inside slabs and depot magazines.
 *
 * Two flavours are provided: a FIFO queue when PARANOID_KERNEL_FREE is set
 * (delays object reuse to maximise the chance of catching use-after-free)
 * and a faster LIFO stack otherwise (best cache locality). The structure is
 * intentionally lock-free; the surrounding cache or depot is responsible
 * for any required synchronisation. */

#ifndef SLAB_QUEUE_H
#define SLAB_QUEUE_H


#include <stddef.h>

#include "kernel_debug_config.h"


/** @brief Free-list link header embedded inside every free object. */
struct slab_queue_link {
	slab_queue_link* next;  /**< Next free object in the queue. */
};

#if PARANOID_KERNEL_FREE
/** @brief FIFO free queue used when PARANOID_KERNEL_FREE is enabled. */
struct slab_queue {
	slab_queue_link* head;  /**< Oldest free object — the next one returned by Pop(). */
	slab_queue_link* tail;  /**< Newest free object — the most recent Push() target. */

	/** @brief Initialises an empty queue. */
	void Init()
	{
		head = tail = NULL;
	}

	/** @brief Appends @p item to the tail of the queue. */
	void Push(slab_queue_link* item)
	{
		item->next = NULL;

		if (tail == NULL) {
			head = tail = item;
			return;
		}

		tail->next = item;
		tail = item;
	}

	/** @brief Removes and returns the head of the queue. */
	slab_queue_link* Pop()
	{
		slab_queue_link* item = head;
		head = item->next;
		if (head == NULL)
			tail = NULL;
		return item;
	}
};
#else /* LIFO queue */
/** @brief LIFO free stack used in non-paranoid builds. */
struct slab_queue {
	slab_queue_link* head;  /**< Most recently freed object — the next one returned by Pop(). */

	/** @brief Initialises an empty stack. */
	void Init()
	{
		head = NULL;
	}

	/** @brief Pushes @p item onto the top of the stack. */
	void Push(slab_queue_link* item)
	{
		item->next = head;
		head = item;
	}

	/** @brief Pops and returns the top of the stack. */
	slab_queue_link* Pop()
	{
		slab_queue_link* item = head;
		head = item->next;
		return item;
	}
};
#endif


#endif	// SLAB_QUEUE_H
