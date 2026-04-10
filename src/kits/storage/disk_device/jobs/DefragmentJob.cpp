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
 * @file DefragmentJob.cpp
 * @brief Disk device job that defragments a partition's file system.
 *
 * Implements the DefragmentJob class which issues a kernel-level defragment
 * syscall for a given partition. The job tracks the partition change counter
 * so that subsequent operations can detect concurrent modifications.
 *
 * @see DiskDeviceJob
 */

#include "DefragmentJob.h"

#include <syscalls.h>

#include "PartitionReference.h"


/**
 * @brief Constructs a DefragmentJob for the specified partition.
 *
 * @param partition Reference to the partition that will be defragmented.
 */
DefragmentJob::DefragmentJob(PartitionReference* partition)
	: DiskDeviceJob(partition)
{
}


/**
 * @brief Destroys the DefragmentJob and releases any held resources.
 */
DefragmentJob::~DefragmentJob()
{
}


/**
 * @brief Executes the defragmentation operation on the target partition.
 *
 * Calls the kernel defragment syscall with the current change counter.
 * On success the partition's change counter is updated to reflect the new
 * on-disk state.
 *
 * @return B_OK on success, or an error code returned by the kernel.
 */
status_t
DefragmentJob::Do()
{
	int32 changeCounter = fPartition->ChangeCounter();
	status_t error = _kern_defragment_partition(fPartition->PartitionID(),
		&changeCounter);
	if (error != B_OK)
		return error;

	fPartition->SetChangeCounter(changeCounter);

	return B_OK;
}
