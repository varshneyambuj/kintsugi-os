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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file PartitioningInfo.cpp
 * @brief Tracks the free (partitionable) spaces within a partition.
 *
 * BPartitioningInfo maintains a dynamic array of non-overlapping free-space
 * intervals on a disk or partition. Disk-system add-ons call SetTo() to seed
 * the initial free space and ExcludeOccupiedSpace() to punch holes for each
 * existing child partition. The resulting list is queried by the DriveSetup
 * application to determine where new partitions may be created.
 *
 * @see BDiskSystemAddOn
 */

#include <stdio.h>
#include <string.h>

#include <new>

#include <disk_device_manager.h>
#include <PartitioningInfo.h>

#include <syscalls.h>
#include <ddm_userland_interface_defs.h>


using namespace std;


//#define TRACING 1
#if TRACING
#  define TRACE(x) printf x
#else
#  define TRACE(x)
#endif


/**
 * @brief Constructs an empty BPartitioningInfo with no free spaces.
 */
BPartitioningInfo::BPartitioningInfo()
	:
	fPartitionID(-1),
	fSpaces(NULL),
	fCount(0),
	fCapacity(0)
{
}


/**
 * @brief Destroys the BPartitioningInfo and frees the free-space array.
 */
BPartitioningInfo::~BPartitioningInfo()
{
	Unset();
}


/**
 * @brief Initialises the free-space list with a single contiguous region.
 *
 * Replaces any existing free-space data. If \a size is zero or negative
 * the list is left empty.
 *
 * @param offset Byte offset of the available region.
 * @param size   Size in bytes of the available region.
 * @return B_OK on success, B_NO_MEMORY if the array cannot be allocated.
 */
status_t
BPartitioningInfo::SetTo(off_t offset, off_t size)
{
	TRACE(("%p - BPartitioningInfo::SetTo(offset = %lld, size = %lld)\n", this, offset, size));

	fPartitionID = -1;
	delete[] fSpaces;

	if (size > 0) {
		fSpaces = new(nothrow) partitionable_space_data[1];
		if (!fSpaces)
			return B_NO_MEMORY;

		fCount = 1;
		fSpaces[0].offset = offset;
		fSpaces[0].size = size;
	} else {
		fSpaces = NULL;
		fCount = 0;
	}

	fCapacity = fCount;

	return B_OK;
}


/**
 * @brief Resets the object to its default empty state, freeing all storage.
 */
void
BPartitioningInfo::Unset()
{
	delete[] fSpaces;
	fPartitionID = -1;
	fSpaces = NULL;
	fCount = 0;
	fCapacity = 0;
}


/**
 * @brief Removes the region [\a offset, \a offset + \a size) from the free-space list.
 *
 * The occupied region may fully or partially overlap one or more free-space
 * entries. Entries that are fully covered are deleted; partially overlapping
 * entries are trimmed; a single entry that is split in the middle becomes two
 * entries.
 *
 * @param offset Byte offset of the occupied region.
 * @param size   Size in bytes of the occupied region; ignored if <= 0.
 * @return B_OK on success, B_NO_MEMORY if a split requires reallocation
 *         that fails.
 */
status_t
BPartitioningInfo::ExcludeOccupiedSpace(off_t offset, off_t size)
{
	if (size <= 0)
		return B_OK;

	TRACE(("%p - BPartitioningInfo::ExcludeOccupiedSpace(offset = %lld, "
		"size = %lld)\n", this, offset, size));

	int32 leftIndex = -1;
	int32 rightIndex = -1;
	for (int32 i = 0; i < fCount; i++) {
		if (leftIndex == -1
			&& offset < fSpaces[i].offset + fSpaces[i].size) {
			leftIndex = i;
		}

		if (fSpaces[i].offset < offset + size)
			rightIndex = i;
	}

	TRACE(("  leftIndex = %ld, rightIndex = %ld\n", leftIndex, rightIndex));

	// If there's no intersection with any free space, we're done.
	if (leftIndex == -1 || rightIndex == -1 || leftIndex > rightIndex)
		return B_OK;

	partitionable_space_data& leftSpace = fSpaces[leftIndex];
	partitionable_space_data& rightSpace = fSpaces[rightIndex];

	off_t rightSpaceEnd = rightSpace.offset + rightSpace.size;

	// special case: split a single space in two
	if (leftIndex == rightIndex && leftSpace.offset < offset
		&& rightSpaceEnd > offset + size) {

		TRACE(("  splitting space at %ld\n", leftIndex));

		// add a space after this one
		status_t error = _InsertSpaces(leftIndex + 1, 1);
		if (error != B_OK)
			return error;

		// IMPORTANT: after calling _InsertSpace(), one can not
		// use the partitionable_space_data references "leftSpace"
		// and "rightSpace" anymore (declared above)!

		partitionable_space_data& space = fSpaces[leftIndex];
		partitionable_space_data& newSpace = fSpaces[leftIndex + 1];

		space.size = offset - space.offset;

		newSpace.offset = offset + size;
		newSpace.size = rightSpaceEnd - newSpace.offset;

		#ifdef TRACING
			PrintToStream();
		#endif
		return B_OK;
	}

	// check whether the first affected space is eaten completely
	int32 deleteFirst = leftIndex;
	if (leftSpace.offset < offset) {
		leftSpace.size = offset - leftSpace.offset;

		TRACE(("  left space remains, new size is %lld\n", leftSpace.size));

		deleteFirst++;
	}

	// check whether the last affected space is eaten completely
	int32 deleteLast = rightIndex;
	if (rightSpaceEnd > offset + size) {
		rightSpace.offset = offset + size;
		rightSpace.size = rightSpaceEnd - rightSpace.offset;

		TRACE(("  right space remains, new offset = %lld, size = %lld\n",
			rightSpace.offset, rightSpace.size));

		deleteLast--;
	}

	// remove all spaces that are completely eaten
	if (deleteFirst <= deleteLast)
		_RemoveSpaces(deleteFirst, deleteLast - deleteFirst + 1);

	#ifdef TRACING
		PrintToStream();
	#endif
	return B_OK;
}


/**
 * @brief Returns the partition ID associated with this info object.
 *
 * @return The partition ID, or -1 if not set.
 */
partition_id
BPartitioningInfo::PartitionID() const
{
	return fPartitionID;
}


/**
 * @brief Returns the free-space interval at the given index.
 *
 * @param index  Zero-based index into the free-space list.
 * @param offset Set to the byte offset of the space on success.
 * @param size   Set to the size in bytes of the space on success.
 * @return B_OK on success, B_NO_INIT if not initialised, B_BAD_VALUE if
 *         \a offset or \a size is NULL, or B_BAD_INDEX if out of range.
 */
status_t
BPartitioningInfo::GetPartitionableSpaceAt(int32 index, off_t* offset,
										   off_t *size) const
{
	if (!fSpaces)
		return B_NO_INIT;
	if (!offset || !size)
		return B_BAD_VALUE;
	if (index < 0 || index >= fCount)
		return B_BAD_INDEX;
	*offset = fSpaces[index].offset;
	*size = fSpaces[index].size;
	return B_OK;
}


/**
 * @brief Returns the number of free-space intervals in the list.
 *
 * @return Count of partitionable spaces.
 */
int32
BPartitioningInfo::CountPartitionableSpaces() const
{
	return fCount;
}


/**
 * @brief Prints the free-space list to standard output for debugging.
 *
 * Outputs "not initialized" if the object has no data, otherwise prints
 * the count and the offset/size of each free-space interval.
 */
void
BPartitioningInfo::PrintToStream() const
{
	if (!fSpaces) {
		printf("BPartitioningInfo is not initialized\n");
		return;
	}
	printf("BPartitioningInfo has %" B_PRId32 " spaces:\n", fCount);
	for (int32 i = 0; i < fCount; i++) {
		printf("  space at %" B_PRId32 ": offset = %" B_PRId64 ", size = %"
			B_PRId64 "\n", i, fSpaces[i].offset, fSpaces[i].size);
	}
}


// #pragma mark -


/**
 * @brief Inserts \a count empty slots into the free-space array at \a index.
 *
 * Grows the array if necessary, using a doubling strategy to amortize
 * allocations. The newly inserted slots are uninitialised.
 *
 * @param index Position at which to insert; must satisfy 0 < index <= fCount.
 * @param count Number of slots to insert; must be > 0.
 * @return B_OK on success, B_BAD_VALUE for invalid arguments, or
 *         B_NO_MEMORY if reallocation fails.
 */
status_t
BPartitioningInfo::_InsertSpaces(int32 index, int32 count)
{
	if (index <= 0 || index > fCount || count <= 0)
		return B_BAD_VALUE;

	// If the capacity is sufficient, we just need to copy the spaces to create
	// a gap.
	if (fCount + count <= fCapacity) {
		memmove(fSpaces + index + count, fSpaces + index,
			(fCount - index) * sizeof(partitionable_space_data));
		fCount += count;
		return B_OK;
	}

	// alloc new array
	int32 capacity = (fCount + count) * 2;
		// add a bit room for further resizing

	partitionable_space_data* spaces
		= new(nothrow) partitionable_space_data[capacity];
	if (!spaces)
		return B_NO_MEMORY;

	// copy items
	memcpy(spaces, fSpaces, index * sizeof(partitionable_space_data));
	memcpy(spaces + index + count, fSpaces + index,
		(fCount - index) * sizeof(partitionable_space_data));

	delete[] fSpaces;
	fSpaces = spaces;
	fCapacity = capacity;
	fCount += count;

	return B_OK;
}


/**
 * @brief Removes \a count consecutive slots from the free-space array starting at \a index.
 *
 * Adjusts the count but does not shrink the allocated capacity. Out-of-range
 * arguments are silently ignored.
 *
 * @param index Zero-based start index of the range to remove.
 * @param count Number of slots to remove.
 */
void
BPartitioningInfo::_RemoveSpaces(int32 index, int32 count)
{
	if (index < 0 || count <= 0 || index + count > fCount)
		return;

	if (count < fCount) {
		int32 endIndex = index + count;
		memmove(fSpaces + index, fSpaces + endIndex,
			(fCount - endIndex) * sizeof(partitionable_space_data));
	}

	fCount -= count;
}
