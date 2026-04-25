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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TargetAddressRangeList.cpp
 * @brief Ordered list of TargetAddressRange entries used by image debug info.
 *
 * Mirrors a typical DWARF range list. Entries are stored in insertion
 * order without coalescing, so callers can rely on relative ordering for
 * later post-processing.
 */

#include "TargetAddressRangeList.h"

#include <algorithm>


/** @brief Construct an empty range list. */
TargetAddressRangeList::TargetAddressRangeList()
{
}


/**
 * @brief Construct with a single initial range.
 *
 * @param range  Range added at index 0.
 */
TargetAddressRangeList::TargetAddressRangeList(const TargetAddressRange& range)
{
	AddRange(range);
}


/**
 * @brief Copy-construct from another list.
 *
 * @param other  Source list.
 */
TargetAddressRangeList::TargetAddressRangeList(
	const TargetAddressRangeList& other)
	:
	fRanges(other.fRanges)
{
}


/** @brief Drop all stored ranges. */
void
TargetAddressRangeList::Clear()
{
	fRanges.Clear();
}


/**
 * @brief Append a range to the end of the list.
 *
 * @param range  Range to append.
 * @return true on success, false on allocation failure.
 */
bool
TargetAddressRangeList::AddRange(const TargetAddressRange& range)
{
	return fRanges.Add(range);
}


/** @brief Return the number of ranges stored. */
int32
TargetAddressRangeList::CountRanges() const
{
	return fRanges.Size();
}


/**
 * @brief Return the range at @a index.
 *
 * @param index  Zero-based index.
 * @return The range at @a index, or a default-constructed range if out of range.
 */
TargetAddressRange
TargetAddressRangeList::RangeAt(int32 index) const
{
	return index >= 0 && index < fRanges.Size()
		? fRanges[index] : TargetAddressRange();
}


/**
 * @brief Return the lowest start address across all ranges.
 *
 * @return The lowest start address, or 0 if the list is empty.
 */
target_addr_t
TargetAddressRangeList::LowestAddress() const
{
	int32 count = fRanges.Size();
	if (count == 0)
		return 0;

	target_addr_t lowest = fRanges[0].Start();
	for (int32 i = 0; i < count; i++)
		lowest = std::min(lowest, fRanges[i].Start());

	return lowest;
}


/**
 * @brief Return a single range that covers all stored ranges.
 *
 * @return The bitwise-OR (union) of every range in the list.
 */
TargetAddressRange
TargetAddressRangeList::CoveringRange() const
{
	TargetAddressRange range;
	int32 count = fRanges.Size();
	for (int32 i = 0; i < count; i++)
		range |= fRanges[i];

	return range;
}


/**
 * @brief Test whether any range contains @a address.
 *
 * @param address  Address to test.
 * @return true if at least one range contains @a address.
 */
bool
TargetAddressRangeList::Contains(target_addr_t address) const
{
	int32 count = fRanges.Size();
	for (int32 i = 0; i < count; i++) {
		if (fRanges[i].Contains(address))
			return true;
	}

	return false;
}


/**
 * @brief Copy-assign from another list.
 *
 * @param other  Source list.
 * @return Reference to *this.
 */
TargetAddressRangeList&
TargetAddressRangeList::operator=(const TargetAddressRangeList& other)
{
	fRanges = other.fRanges;
	return *this;
}
