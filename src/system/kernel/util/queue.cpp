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
 *   Copyright 2001, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file queue.cpp
 * @brief Plain and fixed-size FIFO queues used by kernel subsystems.
 *
 * Two independent implementations live here:
 *
 *  - An intrusive singly-linked list queue (`queue`, `queue_element`) where
 *    each enqueued object stores a `next` pointer at its head.
 *  - A bounded circular queue (`fixed_queue`) that stores opaque `void*`
 *    entries in a pre-allocated array.
 *
 * Neither queue is thread-safe; callers synchronize externally.
 */

#include <kernel.h>
#include <queue.h>
#include <malloc.h>
#include <errno.h>

typedef struct queue_element {
	void *next;
} queue_element;

typedef struct queue_typed {
	queue_element *head;
	queue_element *tail;
	int count;
} queue_typed;


/**
 * @brief Initialize an empty intrusive queue.
 * @param q Queue to reset.
 * @return Always 0.
 */
int
queue_init(queue *q)
{
	q->head = q->tail = NULL;
	q->count = 0;
	return 0;
}


/**
 * @brief Remove the first occurrence of @a e from the queue.
 * @param _q Queue to search.
 * @param e  Element pointer (must have been previously enqueued).
 * @return 0 if removed, -1 if @a e was not on the queue.
 */
int
queue_remove_item(queue *_q, void *e)
{
	queue_typed *q = (queue_typed *)_q;
	queue_element *elem = (queue_element *)e;
	queue_element *temp, *last = NULL;

	temp = (queue_element *)q->head;
	while (temp) {
		if (temp == elem) {
			if (last)
				last->next = temp->next;
			else
				q->head = (queue_element*)temp->next;

			if (q->tail == temp)
				q->tail = last;
			q->count--;
			return 0;
		}
		last = temp;
		temp = (queue_element*)temp->next;
	}

	return -1;
}


/**
 * @brief Append an element to the tail of the queue.
 * @param _q Queue receiving the element.
 * @param e  Element to enqueue; its first word is used as a next-pointer.
 * @return Always 0.
 */
int
queue_enqueue(queue *_q, void *e)
{
	queue_typed *q = (queue_typed *)_q;
	queue_element *elem = (queue_element *)e;

	if (q->tail == NULL) {
		q->tail = elem;
		q->head = elem;
	} else {
		q->tail->next = elem;
		q->tail = elem;
	}
	elem->next = NULL;
	q->count++;
	return 0;
}


/**
 * @brief Remove and return the head element.
 * @param _q Queue to dequeue from.
 * @return Head element, or NULL if the queue is empty.
 */
void *
queue_dequeue(queue *_q)
{
	queue_typed *q = (queue_typed *)_q;
	queue_element *elem;

	elem = q->head;
	if (q->head != NULL)
		q->head = (queue_element*)q->head->next;
	if (q->tail == elem)
		q->tail = NULL;

	if (elem != NULL)
		q->count--;

	return elem;
}


/**
 * @brief Return the head element without removing it.
 * @param q Queue to peek.
 * @return Head element, or NULL if the queue is empty.
 */
void *
queue_peek(queue *q)
{
	return q->head;
}


//	#pragma mark -
/* fixed queue stuff */


/**
 * @brief Allocate the backing array for a bounded ring buffer.
 * @param q    Queue to initialize.
 * @param size Capacity in entries; must be > 0.
 * @return 0 on success, EINVAL for bad size, ENOMEM on allocation failure.
 */
int
fixed_queue_init(fixed_queue *q, int size)
{
	if (size <= 0)
		return EINVAL;

	q->table = (void**)malloc(size * sizeof(void *));
	if (!q->table)
		return ENOMEM;
	q->head = 0;
	q->tail = 0;
	q->count = 0;
	q->size = size;

	return 0;
}


/**
 * @brief Release the backing array of a fixed-size queue.
 * @param q Queue to destroy.
 */
void
fixed_queue_destroy(fixed_queue *q)
{
	free(q->table);
}


/**
 * @brief Append an entry to a fixed-size queue.
 * @param q Queue receiving the entry.
 * @param e Opaque pointer to store.
 * @return 0 on success, ENOMEM when the queue is full.
 */
int
fixed_queue_enqueue(fixed_queue *q, void *e)
{
	if (q->count == q->size)
		return ENOMEM;

	q->table[q->head++] = e;
	if (q->head >= q->size)
		q->head = 0;
	q->count++;

	return 0;
}


/**
 * @brief Remove and return the oldest entry.
 * @param q Queue to dequeue from.
 * @return Entry pointer, or NULL if the queue is empty.
 */
void *
fixed_queue_dequeue(fixed_queue *q)
{
	void *e;

	if (q->count <= 0)
		return NULL;

	e = q->table[q->tail++];
 	if (q->tail >= q->size)
 		q->tail = 0;
	q->count--;

	return e;
}


/**
 * @brief Return the oldest entry without removing it.
 * @param q Queue to peek.
 * @return Entry pointer, or NULL if the queue is empty.
 */
void *
fixed_queue_peek(fixed_queue *q)
{
	if (q->count <= 0)
		return NULL;

	return q->table[q->tail];
}

