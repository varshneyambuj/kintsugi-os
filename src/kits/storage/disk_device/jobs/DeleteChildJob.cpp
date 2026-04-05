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
 *   Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file DeleteChildJob.cpp
 * @brief Disk device job that removes a child partition from its parent.
 *
 * Implements the DeleteChildJob class which invokes the kernel delete-child
 * syscall to permanently remove a child partition. After a successful
 * deletion the child's partition reference is invalidated and the parent's
 * change counter is updated.
 *
 * @see DiskDeviceJob
 */

#include "DeleteChildJob.h"

#include <syscalls.h>

#include "DiskDeviceUtils.h"
#include "PartitionReference.h"


/**
 * @brief Constructs a DeleteChildJob for the given parent and child references.
 *
 * @param partition Reference to the parent partition that contains the child.
 * @param child     Reference to the child partition to be deleted.
 */
DeleteChildJob::DeleteChildJob(PartitionReference* partition,
		PartitionReference* child)
	: DiskDeviceJob(partition, child)
{
}


/**
 * @brief Destroys the DeleteChildJob and releases any held resources.
 */
DeleteChildJob::~DeleteChildJob()
{
}


/**
 * @brief Executes the delete-child operation by calling the kernel syscall.
 *
 * On success the parent's change counter is updated and the child's
 * partition reference is reset to an invalid state (ID -1, counter 0).
 *
 * @return B_OK on success, or an error code returned by the kernel.
 */
status_t
DeleteChildJob::Do()
{
	int32 changeCounter = fPartition->ChangeCounter();
	status_t error = _kern_delete_child_partition(fPartition->PartitionID(),
		&changeCounter, fChild->PartitionID(), fChild->ChangeCounter());
	if (error != B_OK)
		return error;

	fPartition->SetChangeCounter(changeCounter);
	fChild->SetTo(-1, 0);

	return B_OK;
}
