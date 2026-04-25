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
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file RangeList.cpp
 * @brief Sorted, coalescing list of inclusive integer ranges.
 *
 * Maintains an ordered list of [lowerBound, upperBound] ranges that
 * automatically merge on overlap. Used by the UI helpers and DWARF
 * code that need to track sets of indices or addresses.
 */


#include "RangeList.h"

#include <AutoDeleter.h>


/** @brief Construct an empty range list with an initial capacity of 20 entries. */
RangeList::RangeList()
	:
	BObjectList<Range, true>(20)
{
}


/** @brief Destructor; the BObjectList base owns and frees the Range entries. */
RangeList::~RangeList()
{
}


/**
 * @brief Insert the inclusive range [@a lowValue, @a highValue], coalescing overlap.
 *
 * Walks the list once to find the insertion point, extending or merging an
 * existing range when the new range overlaps it, and otherwise inserting a
 * fresh Range entry in sort order.
 *
 * @param lowValue   Lower bound (inclusive).
 * @param highValue  Upper bound (inclusive); must be >= @a lowValue.
 * @retval B_OK         Range added or merged.
 * @retval B_BAD_VALUE  @a lowValue is greater than @a highValue.
 * @retval B_NO_MEMORY  Allocation failed.
 */
status_t
RangeList::AddRange(int32 lowValue, int32 highValue)
{
	if (lowValue > highValue)
		return B_BAD_VALUE;

	int32 i = 0;
	if (CountItems() != 0) {
		for (; i < CountItems(); i++) {
			Range* range = ItemAt(i);
			if (lowValue < range->lowerBound) {
				if (highValue < range->lowerBound) {
					// the new range is completely below the bounds
					// of the ranges we currently contain,
					// insert it here.
					break;
				} else if (highValue <= range->upperBound) {
					// the new range partly overlaps the lower
					// current range
					range->lowerBound = lowValue;
					return B_OK;
				} else {
					// the new range completely encompasses
					// the current range
					range->lowerBound = lowValue;
					range->upperBound = highValue;
					_CollapseOverlappingRanges(i +1, highValue);
				}

			} else if (lowValue < range->upperBound) {
				if (highValue <= range->upperBound) {
					// the requested range is already completely contained
					// within our existing range list
					return B_OK;
				} else {
					range->upperBound = highValue;
					_CollapseOverlappingRanges(i + 1, highValue);
					return B_OK;
				}
			}
		}
	}

	Range* range = new(std::nothrow) Range(lowValue, highValue);
	if (range == NULL)
		return B_NO_MEMORY;

	BPrivate::ObjectDeleter<Range> rangeDeleter(range);
	if (!AddItem(range, i))
		return B_NO_MEMORY;

	rangeDeleter.Detach();
	return B_OK;
}


/**
 * @brief Convenience overload that inserts an existing Range value.
 *
 * @param range  Range to insert; forwarded to AddRange(int32, int32).
 * @return Same status codes as AddRange(int32, int32).
 */
status_t
RangeList::AddRange(const Range& range)
{
	return AddRange(range.lowerBound, range.upperBound);
}


/**
 * @brief Remove the range stored at @a index from the list.
 *
 * @param index  Zero-based index. Out-of-range values are silently ignored.
 */
void
RangeList::RemoveRangeAt(int32 index)
{
	if (index < 0 || index >= CountItems())
		return;

	RemoveItem(ItemAt(index));
}


/**
 * @brief Return whether @a value lies within any range stored in the list.
 *
 * @param value  Value to test.
 * @return true if @a value is inside an inclusive range.
 */
bool
RangeList::Contains(int32 value) const
{
	for (int32 i = 0; i < CountItems(); i++) {
		const Range* range = ItemAt(i);
		if (value < range->lowerBound || value > range->upperBound)
			break;
		else if (value >= range->lowerBound && value <= range->upperBound)
			return true;
	}

	return false;
}


/**
 * @brief Return the number of stored ranges.
 *
 * @return Number of distinct ranges currently in the list.
 */
int32
RangeList::CountRanges() const
{
	return CountItems();
}


/**
 * @brief Return the range at @a index without bounds checking.
 *
 * @param index  Zero-based index into the range list.
 * @return Pointer to the requested range, or NULL if @a index is out of range.
 */
const Range*
RangeList::RangeAt(int32 index) const
{
	return ItemAt(index);
}


/**
 * @brief Drop or trim ranges that overlap a freshly extended upper bound.
 *
 * Walks forward from @a startIndex, removing fully covered ranges and
 * trimming the next non-covered range so that the list remains a sorted
 * disjoint set after AddRange() extends an existing entry.
 *
 * @param startIndex  Index at which to begin scanning.
 * @param highValue   Newly extended upper bound.
 */
void
RangeList::_CollapseOverlappingRanges(int32 startIndex, int32 highValue)
{
	for (int32 i = startIndex; i < CountItems();) {
		// check if it also overlaps any of the following
		// ranges.
		Range* nextRange = ItemAt(i);
		if (nextRange->lowerBound > highValue)
			return;
		else if (nextRange->upperBound < highValue) {
			RemoveItem(nextRange);
			continue;
		} else {
			nextRange->lowerBound = highValue + 1;
			return;
		}
	}
}
