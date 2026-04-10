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
 * @file UninitializeJob.cpp
 * @brief Disk device job that removes a partition's file-system initialization.
 *
 * Implements the UninitializeJob class which calls the kernel uninitialize
 * syscall to strip content-type and associated metadata from a partition.
 * An optional parent reference is accepted so that the parent's change
 * counter can also be updated after the operation.
 *
 * @see DiskDeviceJob
 */

#include "UninitializeJob.h"

#include <syscalls.h>

#include "PartitionReference.h"


/**
 * @brief Constructs a UninitializeJob for the given partition and its parent.
 *
 * @param partition Reference to the partition to be uninitialized (stored
 *                  internally as the child role).
 * @param parent    Optional reference to the containing parent partition;
 *                  may be \c NULL if the partition has no parent.
 */
UninitializeJob::UninitializeJob(PartitionReference* partition,
		PartitionReference* parent)
	: DiskDeviceJob(parent, partition)
{
}


/**
 * @brief Destroys the UninitializeJob and releases any held resources.
 */
UninitializeJob::~UninitializeJob()
{
}


/**
 * @brief Executes the uninitialize operation on the target partition.
 *
 * Calls the kernel uninitialize syscall. When a parent reference is
 * available its change counter is also updated on success.
 *
 * @return B_OK on success, or an error code returned by the kernel.
 */
status_t
UninitializeJob::Do()
{
	bool haveParent = fPartition != NULL;
	int32 changeCounter = fChild->ChangeCounter();
	int32 parentChangeCounter = haveParent ? fPartition->ChangeCounter() : 0;
	partition_id parentID = haveParent ? fPartition->PartitionID() : -1;

	status_t error = _kern_uninitialize_partition(fChild->PartitionID(),
		&changeCounter, parentID, &parentChangeCounter);

	if (error != B_OK)
		return error;

	fChild->SetChangeCounter(changeCounter);
	if (haveParent)
		fPartition->SetChangeCounter(parentChangeCounter);

	return B_OK;
}
