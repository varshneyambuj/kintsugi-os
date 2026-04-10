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
 * @file RepairJob.cpp
 * @brief Disk device job that checks or repairs a partition's file system.
 *
 * Implements the RepairJob class which invokes the kernel repair syscall for
 * a given partition. The job can be configured for a read-only check or a
 * full repair pass depending on the \c checkOnly flag supplied at construction.
 *
 * @see DiskDeviceJob
 */

#include "RepairJob.h"

#include <syscalls.h>

#include "PartitionReference.h"


/**
 * @brief Constructs a RepairJob for the specified partition.
 *
 * @param partition Reference to the partition to check or repair.
 * @param checkOnly When \c true only a consistency check is performed;
 *                  when \c false the file system is also repaired.
 */
RepairJob::RepairJob(PartitionReference* partition, bool checkOnly)
	:
	DiskDeviceJob(partition),
	fCheckOnly(checkOnly)
{
}


/**
 * @brief Destroys the RepairJob and releases any held resources.
 */
RepairJob::~RepairJob()
{
}


/**
 * @brief Executes the check or repair operation on the target partition.
 *
 * Calls the kernel repair syscall with the current change counter. On
 * success the partition's change counter is updated to reflect the new
 * on-disk state.
 *
 * @return B_OK on success, or an error code returned by the kernel.
 */
status_t
RepairJob::Do()
{
	int32 changeCounter = fPartition->ChangeCounter();
	status_t error = _kern_repair_partition(fPartition->PartitionID(),
		&changeCounter, fCheckOnly);
	if (error != B_OK)
		return error;

	fPartition->SetChangeCounter(changeCounter);

	return B_OK;
}
