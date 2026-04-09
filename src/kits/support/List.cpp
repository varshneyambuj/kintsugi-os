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
 *   Copyright 2001-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       The Storage Kit Team
 *       Stephan Aßmus
 *       Rene Gollent
 *       John Scipione, jscipione@gmail.com
 *       Isaac Yonemoto
 */


/**
 * @file List.cpp
 * @brief Implementation of BList, a generic pointer container.
 *
 * BList stores an ordered sequence of untyped (void*) pointers. It manages
 * its own heap-allocated array and resizes it automatically using a
 * power-of-two doubling strategy (see _ResizeArray()). The class is not
 * thread-safe; callers must synchronise concurrent access externally.
 *
 * Key capabilities:
 *   - O(1) amortised append, O(n) insertion at an arbitrary index.
 *   - Full copy semantics via the copy constructor and operator=.
 *   - Value equality comparison via operator== (pointer identity per element).
 *   - In-place sorting via qsort(), item swapping, and single-item moves.
 *   - Iteration helpers DoForEach() with optional extra argument.
 */


#include <List.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/** @brief Shift \a count pointers starting at \a items by \a offset slots. */
static inline void
move_items(void** items, int32 offset, int32 count)
{
	if (count > 0 && offset != 0)
		memmove(items + offset, items, count * sizeof(void*));
}


/**
 * @brief Construct a BList with the given allocation block size.
 *
 * No heap memory is allocated at construction time; the internal array is
 * grown lazily on the first AddItem() call. The \a blockSize controls the
 * minimum allocation unit and the initial capacity.
 *
 * @param blockSize Minimum number of slots to allocate at a time (must be > 0;
 *                  non-positive values are silently clamped to 1).
 */
BList::BList(int32 blockSize)
	:
	fObjectList(NULL),
	fPhysicalSize(0),
	fItemCount(0),
	fBlockSize(blockSize),
	fResizeThreshold(0)
{
	if (fBlockSize <= 0)
		fBlockSize = 1;
}


/**
 * @brief Construct a BList as a deep copy of \a other.
 *
 * The new list gets its own independent heap array initialised with the same
 * pointer values as \a other. Modifications to either list after construction
 * do not affect the other.
 *
 * @param other The source list to copy.
 */
BList::BList(const BList& other)
	:
	fObjectList(NULL),
	fPhysicalSize(0),
	fItemCount(0),
	fBlockSize(other.fBlockSize)
{
	*this = other;
}


/**
 * @brief Destroy the BList and free its internal pointer array.
 *
 * The pointers stored in the list are not freed; ownership of the pointed-to
 * objects remains with the caller.
 */
BList::~BList()
{
	free(fObjectList);
}


/**
 * @brief Replace the contents of this list with a copy of \a other.
 *
 * Self-assignment is a no-op. On success the list contains the same pointer
 * values in the same order as \a other. If the internal reallocation fails,
 * the list may be left in an inconsistent state.
 *
 * @param other The source list to copy.
 * @return A reference to this list.
 */
BList&
BList::operator=(const BList& other)
{
	if (&other != this) {
		fBlockSize = other.fBlockSize;
		if (_ResizeArray(other.fItemCount)) {
			fItemCount = other.fItemCount;
			memcpy(fObjectList, other.fObjectList, fItemCount * sizeof(void*));
		}
	}

	return *this;
}


/**
 * @brief Test whether this list is equal to \a other by value.
 *
 * Two lists are equal when they contain the same number of items and each
 * corresponding pair of pointers is identical (pointer equality, not deep
 * equality of the pointed-to objects).
 *
 * @param other The list to compare against.
 * @return true if the lists have the same count and the same pointer values in
 *         the same order, false otherwise.
 */
bool
BList::operator==(const BList& other) const
{
	if (&other == this)
		return true;

	if (other.fItemCount != fItemCount)
		return false;

	if (fItemCount > 0) {
		return memcmp(fObjectList, other.fObjectList,
			fItemCount * sizeof(void*)) == 0;
	}

	return true;
}


/**
 * @brief Test whether this list differs from \a other.
 *
 * @param other The list to compare against.
 * @return true if the lists are not equal, false if they are equal.
 * @see operator==()
 */
bool
BList::operator!=(const BList& other) const
{
	return !(*this == other);
}


/**
 * @brief Insert \a item at position \a index, shifting subsequent items right.
 *
 * @param item  The pointer to insert.
 * @param index Zero-based index at which to insert (0 <= index <= CountItems()).
 * @return true on success, false if \a index is out of range or memory
 *         allocation fails.
 */
bool
BList::AddItem(void* item, int32 index)
{
	if (index < 0 || index > fItemCount)
		return false;

	bool result = true;

	if (fItemCount + 1 > fPhysicalSize)
		result = _ResizeArray(fItemCount + 1);
	if (result) {
		++fItemCount;
		move_items(fObjectList + index, 1, fItemCount - index - 1);
		fObjectList[index] = item;
	}
	return result;
}


/**
 * @brief Append \a item to the end of the list.
 *
 * @param item The pointer to append.
 * @return true on success, false if memory allocation fails.
 */
bool
BList::AddItem(void* item)
{
	bool result = true;
	if (fPhysicalSize > fItemCount) {
		fObjectList[fItemCount] = item;
		++fItemCount;
	} else {
		if ((result = _ResizeArray(fItemCount + 1))) {
			fObjectList[fItemCount] = item;
			++fItemCount;
		}
	}
	return result;
}


/**
 * @brief Insert all items from \a list at position \a index.
 *
 * All items of \a list are inserted contiguously starting at \a index;
 * existing items at and after \a index are shifted right.
 *
 * @param list  The source list whose items are to be inserted. Must not be NULL.
 * @param index Zero-based insertion index (0 <= index <= CountItems()).
 * @return true on success, false if \a list is NULL, \a index is out of range,
 *         or memory allocation fails.
 */
bool
BList::AddList(const BList* list, int32 index)
{
	bool result = (list && index >= 0 && index <= fItemCount);
	if (result && list->fItemCount > 0) {
		int32 count = list->fItemCount;
		if (fItemCount + count > fPhysicalSize)
			result = _ResizeArray(fItemCount + count);

		if (result) {
			fItemCount += count;
			move_items(fObjectList + index, count, fItemCount - index - count);
			memcpy(fObjectList + index, list->fObjectList,
				list->fItemCount * sizeof(void*));
		}
	}

	return result;
}


/**
 * @brief Append all items from \a list to the end of this list.
 *
 * @param list The source list whose items are to be appended. Must not be NULL.
 * @return true on success, false if \a list is NULL or memory allocation fails.
 */
bool
BList::AddList(const BList* list)
{
	bool result = (list != NULL);
	if (result && list->fItemCount > 0) {
		int32 index = fItemCount;
		int32 count = list->fItemCount;
		if (fItemCount + count > fPhysicalSize)
			result = _ResizeArray(fItemCount + count);

		if (result) {
			fItemCount += count;
			memcpy(fObjectList + index, list->fObjectList,
				list->fItemCount * sizeof(void*));
		}
	}

	return result;
}


/**
 * @brief Remove the first occurrence of \a item from the list.
 *
 * Searches by pointer identity and removes the first matching entry,
 * shifting subsequent items left.
 *
 * @param item The pointer to remove.
 * @return true if the item was found and removed, false if it was not present.
 */
bool
BList::RemoveItem(void* item)
{
	int32 index = IndexOf(item);
	bool result = (index >= 0);
	if (result)
		RemoveItem(index);
	return result;
}


/**
 * @brief Remove and return the item at position \a index.
 *
 * Subsequent items are shifted left to fill the gap. If the list shrinks
 * below the resize threshold the backing array is compacted.
 *
 * @param index Zero-based index of the item to remove.
 * @return The removed pointer, or NULL if \a index is out of range.
 */
void*
BList::RemoveItem(int32 index)
{
	void* item = NULL;
	if (index >= 0 && index < fItemCount) {
		item = fObjectList[index];
		move_items(fObjectList + index + 1, -1, fItemCount - index - 1);
		--fItemCount;
		if (fItemCount <= fResizeThreshold)
			_ResizeArray(fItemCount);
	}
	return item;
}


/**
 * @brief Remove \a count consecutive items starting at \a index.
 *
 * @param index Zero-based starting index (must be in range).
 * @param count Number of items to remove; clamped to the number of items
 *              remaining from \a index.
 * @return true on success, false if \a index is out of range or \a count is 0
 *         after clamping.
 */
bool
BList::RemoveItems(int32 index, int32 count)
{
	bool result = (index >= 0 && index <= fItemCount);
	if (result) {
		if (index + count > fItemCount)
			count = fItemCount - index;
		if (count > 0) {
			move_items(fObjectList + index + count, -count,
					   fItemCount - index - count);
			fItemCount -= count;
			if (fItemCount <= fResizeThreshold)
				_ResizeArray(fItemCount);
		} else
			result = false;
	}
	return result;
}


/**
 * @brief Replace the item at \a index with \a item.
 *
 * @param index Zero-based index of the slot to replace.
 * @param item  The new pointer value.
 * @return true on success, false if \a index is out of range.
 */
bool
BList::ReplaceItem(int32 index, void* item)
{
	bool result = false;

	if (index >= 0 && index < fItemCount) {
		fObjectList[index] = item;
		result = true;
	}
	return result;
}


/**
 * @brief Remove all items from the list and release excess memory.
 *
 * After this call CountItems() returns 0 and the backing array is shrunk to
 * the minimum allocation.
 */
void
BList::MakeEmpty()
{
	fItemCount = 0;
	_ResizeArray(0);
}


// #pragma mark - Reordering items.


/**
 * @brief Sort the list in-place using \a compareFunc as the ordering predicate.
 *
 * Delegates directly to qsort(). The comparison function receives two
 * const void** pointers (pointers to the stored void* values).
 *
 * @param compareFunc A comparator compatible with qsort(): returns negative,
 *                    zero, or positive to indicate less-than, equal, or
 *                    greater-than. If NULL the call is a no-op.
 */
void
BList::SortItems(int (*compareFunc)(const void*, const void*))
{
	if (compareFunc)
		qsort(fObjectList, fItemCount, sizeof(void*), compareFunc);
}


/**
 * @brief Swap the items at positions \a indexA and \a indexB.
 *
 * @param indexA Zero-based index of the first item.
 * @param indexB Zero-based index of the second item.
 * @return true on success, false if either index is out of range.
 */
bool
BList::SwapItems(int32 indexA, int32 indexB)
{
	bool result = false;

	if (indexA >= 0 && indexA < fItemCount
		&& indexB >= 0 && indexB < fItemCount) {

		void* tmpItem = fObjectList[indexA];
		fObjectList[indexA] = fObjectList[indexB];
		fObjectList[indexB] = tmpItem;

		result = true;
	}

	return result;
}


/*! This moves a list item from posititon a to position b, moving the
	appropriate block of list elements to make up for the move. For example,
	in the array:
	A B C D E F G H I J
		Moveing 1(B)->6(G) would result in this:
	A C D E F G B H I J
*/
/**
 * @brief Move the item at position \a from to position \a to.
 *
 * Items between the two positions are shifted to fill the vacated slot. For
 * example, moving index 1 to index 6 in {A B C D E F G H I J} produces
 * {A C D E F G B H I J}.
 *
 * @param from Zero-based source index.
 * @param to   Zero-based destination index.
 * @return true on success, false if either index is out of range.
 */
bool
BList::MoveItem(int32 from, int32 to)
{
	if ((from >= fItemCount) || (to >= fItemCount) || (from < 0) || (to < 0))
		return false;

	void* tmpMover = fObjectList[from];
	if (from < to) {
		memmove(fObjectList + from, fObjectList + from + 1,
			(to - from) * sizeof(void*));
	} else if (from > to) {
		memmove(fObjectList + to + 1, fObjectList + to,
			(from - to) * sizeof(void*));
	}
	fObjectList[to] = tmpMover;

	return true;
}


// #pragma mark - Retrieving items.


/**
 * @brief Return the item at position \a index, or NULL if out of range.
 *
 * @param index Zero-based index to look up.
 * @return The stored pointer, or NULL if \a index < 0 or >= CountItems().
 */
void*
BList::ItemAt(int32 index) const
{
	void* item = NULL;
	if (index >= 0 && index < fItemCount)
		item = fObjectList[index];
	return item;
}


/**
 * @brief Return the first item in the list, or NULL if the list is empty.
 *
 * @return The pointer at index 0, or NULL.
 */
void*
BList::FirstItem() const
{
	void* item = NULL;
	if (fItemCount > 0)
		item = fObjectList[0];
	return item;
}


/**
 * @brief Return the item at \a index without bounds checking.
 *
 * This is the fast, unchecked accessor. Passing an out-of-range index results
 * in undefined behaviour; use ItemAt() for safe access.
 *
 * @param index Zero-based index to look up.
 * @return The stored pointer at the given index.
 */
void*
BList::ItemAtFast(int32 index) const
{
	return fObjectList[index];
}


/**
 * @brief Return a pointer to the raw internal array of stored pointers.
 *
 * The returned pointer is valid only until the next structural modification
 * (add, remove, resize) of the list.
 *
 * @return A void* that is actually a void** pointing to the first element, or
 *         NULL if the list is empty.
 */
void*
BList::Items() const
{
	return fObjectList;
}


/**
 * @brief Return the last item in the list, or NULL if the list is empty.
 *
 * @return The pointer at index CountItems()-1, or NULL.
 */
void*
BList::LastItem() const
{
	void* item = NULL;
	if (fItemCount > 0)
		item = fObjectList[fItemCount - 1];
	return item;
}


// #pragma mark - Querying the list.


/**
 * @brief Test whether \a item is present in the list.
 *
 * @param item The pointer to search for (by identity).
 * @return true if the item is found, false otherwise.
 */
bool
BList::HasItem(void* item) const
{
	return (IndexOf(item) >= 0);
}


/**
 * @brief Test whether the const pointer \a item is present in the list.
 *
 * @param item The pointer to search for (by identity).
 * @return true if the item is found, false otherwise.
 */
bool
BList::HasItem(const void* item) const
{
	return (IndexOf(item) >= 0);
}


/**
 * @brief Return the index of the first occurrence of \a item.
 *
 * Linear search by pointer identity.
 *
 * @param item The pointer to search for.
 * @return The zero-based index of the first match, or -1 if not found.
 */
int32
BList::IndexOf(void* item) const
{
	for (int32 i = 0; i < fItemCount; i++) {
		if (fObjectList[i] == item)
			return i;
	}
	return -1;
}


/**
 * @brief Return the index of the first occurrence of the const pointer \a item.
 *
 * @param item The pointer to search for.
 * @return The zero-based index of the first match, or -1 if not found.
 */
int32
BList::IndexOf(const void* item) const
{
	for (int32 i = 0; i < fItemCount; i++) {
		if (fObjectList[i] == item)
			return i;
	}
	return -1;
}


/**
 * @brief Return the number of items currently stored in the list.
 *
 * @return The item count (>= 0).
 */
int32
BList::CountItems() const
{
	return fItemCount;
}


/**
 * @brief Test whether the list contains no items.
 *
 * @return true if CountItems() == 0, false otherwise.
 */
bool
BList::IsEmpty() const
{
	return fItemCount == 0;
}


// #pragma mark - Iterating over the list.

/*!	Iterate a function over the whole list. If the function outputs a true
	value, then the process is terminated.
*/
/**
 * @brief Call \a func on each item in order, stopping early on true.
 *
 * Iterates from index 0 upward, passing each stored pointer to \a func. If
 * \a func returns true the iteration is terminated immediately.
 *
 * @param func A function pointer that receives each item; returning true
 *             stops iteration. If NULL the call is a no-op.
 */
void
BList::DoForEach(bool (*func)(void*))
{
	if (func == NULL)
		return;

	bool terminate = false;
	int32 index = 0;

	while ((!terminate) && (index < fItemCount)) {
		terminate = func(fObjectList[index]);
		index++;
	}
}


/*!	Iterate a function over the whole list. If the function outputs a true
	value, then the process is terminated. This version takes an additional
	argument which is passed to the function.
*/
/**
 * @brief Call \a func on each item with an extra argument, stopping early on
 *        true.
 *
 * Identical to DoForEach(bool (*)(void*)) except that \a arg is forwarded as a
 * second argument to every call of \a func.
 *
 * @param func A two-argument function pointer; returning true stops iteration.
 *             If NULL the call is a no-op.
 * @param arg  An arbitrary pointer passed as the second argument to \a func.
 */
void
BList::DoForEach(bool (*func)(void*, void*), void* arg)
{
	if (func == NULL)
		return;

	bool terminate = false; int32 index = 0;
	while ((!terminate) && (index < fItemCount)) {
		terminate = func(fObjectList[index], arg);
		index++;
	}
}


#if (__GNUC__ == 2)

// This is somewhat of a hack for backwards compatibility -
// the reason these functions are defined this way rather than simply
// being made private members is that if they are included, then whenever
// gcc encounters someone calling AddList() with a non-const BList pointer,
// it will try to use the private version and fail with a compiler error.

// obsolete AddList(BList* list, int32 index) and AddList(BList* list)
// AddList
extern "C" bool
AddList__5BListP5BListl(BList* self, BList* list, int32 index)
{
	return self->AddList((const BList*)list, index);
}

// AddList
extern "C" bool
AddList__5BListP5BList(BList* self, BList* list)
{
	return self->AddList((const BList*)list);
}
#endif

// FBC
void BList::_ReservedList1() {}
void BList::_ReservedList2() {}


/**
 * @brief Resize the internal pointer array to hold at least \a count items.
 *
 * Uses a power-of-two doubling strategy: starting from the current physical
 * size (or fBlockSize if no allocation exists), the size is left-shifted until
 * it can accommodate \a count items. When shrinking, the array is only
 * reallocated if \a count falls at or below fResizeThreshold (one quarter of
 * the current physical size). After reallocation fResizeThreshold is updated
 * to one quarter of the new physical size (or 0 if that falls below fBlockSize).
 *
 * @param count The minimum number of slots the array must hold.
 * @return true if the array was successfully resized (or no resize was needed),
 *         false if realloc() failed.
 */
bool
BList::_ResizeArray(int32 count)
{
	if (fPhysicalSize == count)
		return true;

	// calculate the new physical size
	// by doubling the existing size
	// until we can hold at least count items
	int32 newSize = fPhysicalSize > 0 ? fPhysicalSize : fBlockSize;
	int32 targetSize = count;
	if (targetSize <= 0)
		targetSize = fBlockSize;

	if (targetSize > fPhysicalSize) {
		while (newSize < targetSize)
			newSize <<= 1;
	} else if (targetSize <= fResizeThreshold)
		newSize = fResizeThreshold;

	// resize if necessary
	bool result = true;
	if (newSize != fPhysicalSize) {
		void** newObjectList
			= (void**)realloc(fObjectList, newSize * sizeof(void*));
		if (newObjectList) {
			fObjectList = newObjectList;
			fPhysicalSize = newSize;

			fResizeThreshold = (fPhysicalSize / 4);
			if (fResizeThreshold < fBlockSize)
				fResizeThreshold = 0;
		} else
			result = false;
	}

	return result;
}
