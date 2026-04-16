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
 *   Copyright 2003-2006, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file list.cpp
 * @brief Intrusive doubly-linked list with a configurable link offset.
 *
 * Each `struct list` holds a sentinel `list_link` whose next/prev point at
 * itself when the list is empty. The `offset` stored on the list tells the
 * API where the `list_link` sub-object lives inside each item, so items can
 * embed the link at any position. The implementation is not thread-safe —
 * callers synchronize externally.
 */


#include <util/list.h>
#include <util/DoublyLinkedList.h>
#include <BytePointer.h>


#define GET_ITEM(list, item) ({ BytePointer<void> pointer((uint8*)item \
	- list->offset); &pointer; })
#define GET_LINK(list, item) ({ BytePointer<list_link> pointer((uint8*)item \
	+ list->offset); &pointer; })

STATIC_ASSERT(sizeof(DoublyLinkedListLink<void*>) == sizeof(list_link));


/**
 * @brief Initialize a list whose item links live at @a offset bytes into each item.
 * @param list   List to reset.
 * @param offset Byte offset of the `list_link` sub-object in each item.
 */
void
list_init_etc(struct list *list, int32 offset)
{
	list->link.next = list->link.prev = &list->link;
	list->offset = offset;
}


/**
 * @brief Initialize a list assuming the link lives at offset 0 in each item.
 * @param list List to reset.
 */
void
list_init(struct list *list)
{
	list_init_etc(list, 0);
}


/**
 * @brief Insert @a _link as the new head of @a list.
 * @param list  Target list.
 * @param _link Raw list_link pointer to splice in.
 */
void
list_add_link_to_head(struct list *list, void *_link)
{
	list_link *link = (list_link *)_link;

	link->next = list->link.next;
	link->prev = &list->link;

	list->link.next->prev = link;
	list->link.next = link;

#if DEBUG_DOUBLY_LINKED_LIST
	ASSERT(link->next != link);
#endif
}


/**
 * @brief Insert @a _link as the new tail of @a list.
 * @param list  Target list.
 * @param _link Raw list_link pointer to splice in.
 */
void
list_add_link_to_tail(struct list *list, void *_link)
{
	list_link *link = (list_link *)_link;

	link->next = &list->link;
	link->prev = list->link.prev;

	list->link.prev->next = link;
	list->link.prev = link;

#if DEBUG_DOUBLY_LINKED_LIST
	ASSERT(link->prev != link);
#endif
}


/**
 * @brief Unlink @a link from whichever list currently holds it.
 *
 * The caller must guarantee that @a link is actually linked; there is no
 * way to detect a free-standing link from the link alone.
 *
 * @param link Link to splice out.
 */
void
list_remove_link(list_link *link)
{
	link->next->prev = link->prev;
	link->prev->next = link->next;

#if DEBUG_DOUBLY_LINKED_LIST
	link->prev = link->next = NULL;
#endif
}


/**
 * @brief Step forward one link, returning NULL at the sentinel.
 * @param list List whose sentinel marks the end.
 * @param link Starting link.
 * @return Next link, or NULL if @a link is the last real element.
 */
static inline list_link *
get_next_link(struct list *list, list_link *link)
{
	if (link->next == &list->link)
		return NULL;

	return link->next;
}


/**
 * @brief Step backward one link, returning NULL at the sentinel.
 * @param list List whose sentinel marks the start.
 * @param link Starting link.
 * @return Previous link, or NULL if @a link is the first real element.
 */
static inline list_link *
get_prev_link(struct list *list, list_link *link)
{
	if (link->prev == &list->link)
		return NULL;

	return link->prev;
}


/**
 * @brief Return the item that follows @a item in the list.
 *
 * Passing NULL for @a item returns the first item, enabling cursor-style
 * iteration that starts by passing NULL.
 *
 * @param list List being iterated.
 * @param item Current item, or NULL to get the first.
 * @return Next item, or NULL when no more items exist.
 */
void *
list_get_next_item(struct list *list, void *item)
{
	list_link *link;

	if (item == NULL)
		return list_is_empty(list) ? NULL : GET_ITEM(list, list->link.next);

	link = get_next_link(list, GET_LINK(list, item));
	return link != NULL ? GET_ITEM(list, link) : NULL;
}


/**
 * @brief Return the item that precedes @a item in the list.
 *
 * Passing NULL for @a item returns the last item, enabling reverse-cursor
 * iteration that starts by passing NULL.
 *
 * @param list List being iterated.
 * @param item Current item, or NULL to get the last.
 * @return Previous item, or NULL when no more items exist.
 */
void *
list_get_prev_item(struct list *list, void *item)
{
	list_link *link;

	if (item == NULL)
		return list_is_empty(list) ? NULL : GET_ITEM(list, list->link.prev);

	link = get_prev_link(list, GET_LINK(list, item));
	return link != NULL ? GET_ITEM(list, link) : NULL;
}


/**
 * @brief Return the last item, or NULL if the list is empty.
 * @param list Target list.
 * @return Last item pointer, or NULL.
 */
void *
list_get_last_item(struct list *list)
{
	return list_is_empty(list) ? NULL : GET_ITEM(list, list->link.prev);
}


/**
 * @brief Append @a item to the tail of the list.
 *
 * Item-level convenience wrapper around list_add_link_to_tail().
 *
 * @param list List to append to.
 * @param item Item to enqueue.
 */
void
list_add_item(struct list *list, void *item)
{
	list_add_link_to_tail(list, GET_LINK(list, item));
}


/**
 * @brief Remove @a item from its list.
 *
 * Item-level convenience wrapper around list_remove_link().
 *
 * @param list List holding the item.
 * @param item Item to remove; must be linked.
 */
void
list_remove_item(struct list *list, void *item)
{
	list_remove_link(GET_LINK(list, item));
}


/**
 * @brief Insert @a item immediately before @a before.
 *
 * When @a before is NULL the item is appended instead (convenient for
 * sorted-insert code paths that walked off the end).
 *
 * @param list   List to insert into.
 * @param before Reference item, or NULL to append.
 * @param item   Item to insert.
 */
void
list_insert_item_before(struct list *list, void *before, void *item)
{
	list_link *beforeLink;
	list_link *link;

	if (before == NULL) {
		list_add_item(list, item);
		return;
	}

	beforeLink = GET_LINK(list, before);
	link = GET_LINK(list, item);

	link->prev = beforeLink->prev;
	link->next = beforeLink;

	beforeLink->prev->next = link;
	beforeLink->prev = link;
}


/**
 * @brief Remove and return the first item.
 * @param list List to dequeue from.
 * @return First item, or NULL if the list is empty.
 */
void *
list_remove_head_item(struct list *list)
{
	list_link *link;

	if (list_is_empty(list))
		return NULL;

	list_remove_link(link = list->link.next);
	return GET_ITEM(list, link);
}


/**
 * @brief Remove and return the last item.
 * @param list List to dequeue from.
 * @return Last item, or NULL if the list is empty.
 */
void *
list_remove_tail_item(struct list *list)
{
	list_link *link;

	if (list_is_empty(list))
		return NULL;

	list_remove_link(link = list->link.prev);
	return GET_ITEM(list, link);
}


/**
 * @brief O(1) move of every item from @a sourceList onto @a targetList.
 *
 * The target list is overwritten, so any items it previously held are
 * abandoned. Link sentinels are re-pointed at the new head and tail so
 * that existing iterators on the moved items stay valid.
 *
 * @param sourceList Donor list (left empty on return).
 * @param targetList Recipient list (previous contents abandoned).
 */
void
list_move_to_list(struct list *sourceList, struct list *targetList)
{
	if (list_is_empty(sourceList)) {
		targetList->link.next = targetList->link.prev = &targetList->link;
		return;
	}

	*targetList = *sourceList;

	// correct link pointers to this list
	targetList->link.next->prev = &targetList->link;
	targetList->link.prev->next = &targetList->link;

	// empty source list
	sourceList->link.next = sourceList->link.prev = &sourceList->link;
}


