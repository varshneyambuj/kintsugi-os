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
 *   Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file MoveJob.cpp
 * @brief Disk device job that moves a child partition to a new offset.
 *
 * Implements the MoveJob class which relocates a child partition within its
 * parent by invoking the kernel move syscall. The job also tracks all
 * descendant partitions whose contents must be moved along with the child,
 * updating their change counters after the operation completes.
 *
 * @see DiskDeviceJob
 */

#include "MoveJob.h"

#include <new>

#include <AutoDeleter.h>

#include <syscalls.h>

#include "DiskDeviceUtils.h"
#include "PartitionReference.h"


using std::nothrow;


/**
 * @brief Constructs a MoveJob for the given parent and child partition references.
 *
 * @param partition Reference to the parent partition that contains the child.
 * @param child     Reference to the child partition to be moved.
 */
MoveJob::MoveJob(PartitionReference* partition, PartitionReference* child)
	: DiskDeviceJob(partition, child),
	  fContents(NULL),
	  fContentsCount(0)
{
}


/**
 * @brief Destroys the MoveJob, releasing references to all tracked content partitions.
 */
MoveJob::~MoveJob()
{
	if (fContents) {
		for (int32 i = 0; i < fContentsCount; i++)
			fContents[i]->ReleaseReference();
		delete[] fContents;
	}
}


/**
 * @brief Initialises the move job with the target offset and list of content partitions.
 *
 * Acquires a reference to each entry in \a contents so that the references
 * remain valid until the job is destroyed.
 *
 * @param offset        The target byte offset for the child partition.
 * @param contents      Array of partition references whose contents will move.
 * @param contentsCount Number of entries in \a contents.
 * @return B_OK on success, B_NO_MEMORY if allocation fails.
 */
status_t
MoveJob::Init(off_t offset, PartitionReference** contents, int32 contentsCount)
{
	fContents = new(nothrow) PartitionReference*[contentsCount];
	if (!fContents)
		return B_NO_MEMORY;

	fContentsCount = contentsCount;
	for (int32 i = 0; i < contentsCount; i++) {
		fContents[i] = contents[i];
		fContents[i]->AcquireReference();
	}

	fOffset = offset;

	return B_OK;
}


/**
 * @brief Executes the move operation by calling the kernel move syscall.
 *
 * Builds arrays of descendant partition IDs and change counters, then
 * delegates to the kernel. On success all affected change counters are
 * updated.
 *
 * @return B_OK on success, B_NO_MEMORY if temporary arrays cannot be
 *         allocated, or an error code from the kernel.
 */
status_t
MoveJob::Do()
{
	int32 changeCounter = fPartition->ChangeCounter();
	int32 childChangeCounter = fChild->ChangeCounter();

	partition_id* descendantIDs = new(nothrow) partition_id[fContentsCount];
	int32* descendantChangeCounters = new(nothrow) int32[fContentsCount];
	ArrayDeleter<partition_id> _(descendantIDs);
	ArrayDeleter<int32> _2(descendantChangeCounters);

	if (!descendantIDs || !descendantChangeCounters)
		return B_NO_MEMORY;

	for (int32 i = 0; i < fContentsCount; i++) {
		descendantIDs[i] = fContents[i]->PartitionID();
		descendantChangeCounters[i] = fContents[i]->ChangeCounter();
	}

	status_t error = _kern_move_partition(fPartition->PartitionID(),
		&changeCounter, fChild->PartitionID(), &childChangeCounter, fOffset,
		descendantIDs, descendantChangeCounters, fContentsCount);

	if (error != B_OK)
		return error;

	fPartition->SetChangeCounter(changeCounter);
	fChild->SetChangeCounter(childChangeCounter);

	for (int32 i = 0; i < fContentsCount; i++)
		fContents[i]->SetChangeCounter(descendantChangeCounters[i]);

	return B_OK;
}
