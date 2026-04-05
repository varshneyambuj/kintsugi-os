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
 * @file CreateChildJob.cpp
 * @brief Disk device job that creates a new child partition inside a parent.
 *
 * Implements the CreateChildJob class which calls the kernel create-child
 * syscall to add a new partition to an existing parent. The job stores the
 * offset, size, type string, name string, and parameters string that define
 * the new partition, and updates the child's partition reference with the
 * kernel-assigned ID after the operation succeeds.
 *
 * @see DiskDeviceJob
 */


#include "CreateChildJob.h"

#include <syscalls.h>

#include "DiskDeviceUtils.h"
#include "PartitionReference.h"


/**
 * @brief Constructs a CreateChildJob for the given parent and child references.
 *
 * @param partition Reference to the parent partition that will contain the new child.
 * @param child     Reference that will receive the newly created partition's ID.
 */
CreateChildJob::CreateChildJob(PartitionReference* partition,
		PartitionReference* child)
	:
	DiskDeviceJob(partition, child),
	fOffset(0),
	fSize(0),
	fType(NULL),
	fName(NULL),
	fParameters(NULL)
{
}


/**
 * @brief Destroys the CreateChildJob, freeing all copied string parameters.
 */
CreateChildJob::~CreateChildJob()
{
	free(fType);
	free(fName);
	free(fParameters);
}


/**
 * @brief Initialises the job with the properties for the new child partition.
 *
 * Copies the provided strings so that the caller is free to release them
 * immediately after this call returns.
 *
 * @param offset     Byte offset of the new partition within its parent.
 * @param size       Size in bytes of the new partition.
 * @param type       Partition type string (e.g. GUID or legacy type string).
 * @param name       Human-readable name for the new partition.
 * @param parameters Optional disk-system-specific parameter string.
 * @return B_OK on success, B_NO_MEMORY if any string copy fails.
 */
status_t
CreateChildJob::Init(off_t offset, off_t size, const char* type,
	const char* name, const char* parameters)
{
	fOffset = offset;
	fSize = size;

	SET_STRING_RETURN_ON_ERROR(fType, type);
	SET_STRING_RETURN_ON_ERROR(fName, name);
	SET_STRING_RETURN_ON_ERROR(fParameters, parameters);

	return B_OK;
}


/**
 * @brief Executes the create-child operation by calling the kernel syscall.
 *
 * On success the parent's change counter is updated and the child reference
 * is initialised with the kernel-assigned partition ID and change counter.
 *
 * @return B_OK on success, or an error code returned by the kernel.
 */
status_t
CreateChildJob::Do()
{
	int32 changeCounter = fPartition->ChangeCounter();
	partition_id childID;
	int32 childChangeCounter;
	status_t error = _kern_create_child_partition(fPartition->PartitionID(),
		&changeCounter, fOffset, fSize, fType, fName, fParameters, &childID,
		&childChangeCounter);
	if (error != B_OK)
		return error;

	fPartition->SetChangeCounter(changeCounter);
	fChild->SetTo(childID, childChangeCounter);

	return B_OK;
}
