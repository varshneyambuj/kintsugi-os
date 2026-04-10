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
 * @file SetStringJob.cpp
 * @brief Disk device job that sets a string property (name, type, or parameters) on a partition.
 *
 * Implements the SetStringJob class which dispatches to the appropriate
 * kernel syscall based on a job-type selector. Supported operations are
 * setting the partition name, content name, type, parameters, and content
 * parameters. Change counters for both the parent and child are updated
 * as required by each operation.
 *
 * @see DiskDeviceJob
 */


#include "SetStringJob.h"

#include <syscalls.h>

#include "DiskDeviceUtils.h"
#include "PartitionReference.h"


/**
 * @brief Constructs a SetStringJob for the given parent and optional child references.
 *
 * @param partition Reference to the parent (or sole target) partition.
 * @param child     Reference to the child partition; may be \c NULL for
 *                  content-level operations that target the parent directly.
 */
SetStringJob::SetStringJob(PartitionReference* partition,
		PartitionReference* child)
	:
	DiskDeviceJob(partition, child),
	fString(NULL)
{
}


/**
 * @brief Destroys the SetStringJob, freeing the stored string copy.
 */
SetStringJob::~SetStringJob()
{
	free(fString);
}


/**
 * @brief Initialises the job with the string value and operation type selector.
 *
 * Validates that \a jobType is one of the recognised set-string job types
 * before storing a copy of the string.
 *
 * @param string  The string value to apply (name, type, or parameters text).
 * @param jobType One of B_DISK_DEVICE_JOB_SET_NAME,
 *                B_DISK_DEVICE_JOB_SET_CONTENT_NAME,
 *                B_DISK_DEVICE_JOB_SET_TYPE,
 *                B_DISK_DEVICE_JOB_SET_PARAMETERS, or
 *                B_DISK_DEVICE_JOB_SET_CONTENT_PARAMETERS.
 * @return B_OK on success, B_BAD_VALUE for an unrecognised job type, or
 *         B_NO_MEMORY if the string copy fails.
 */
status_t
SetStringJob::Init(const char* string, uint32 jobType)
{
	switch (jobType) {
		case B_DISK_DEVICE_JOB_SET_NAME:
		case B_DISK_DEVICE_JOB_SET_CONTENT_NAME:
		case B_DISK_DEVICE_JOB_SET_TYPE:
		case B_DISK_DEVICE_JOB_SET_PARAMETERS:
		case B_DISK_DEVICE_JOB_SET_CONTENT_PARAMETERS:
			break;
		default:
			return B_BAD_VALUE;
	}

	fJobType = jobType;
	SET_STRING_RETURN_ON_ERROR(fString, string);

	return B_OK;
}


/**
 * @brief Executes the string-set operation by dispatching to the correct kernel syscall.
 *
 * Selects the kernel call based on the job type stored by Init(). Operations
 * that modify a child partition also update the child change counter on
 * success.
 *
 * @return B_OK on success, B_BAD_VALUE for an unrecognised job type, or an
 *         error code returned by the kernel.
 */
status_t
SetStringJob::Do()
{
	int32 changeCounter = fPartition->ChangeCounter();
	int32 childChangeCounter = (fChild ? fChild->ChangeCounter() : 0);
	status_t error;
	bool updateChildChangeCounter = false;

	switch (fJobType) {
		case B_DISK_DEVICE_JOB_SET_NAME:
			error = _kern_set_partition_name(fPartition->PartitionID(),
				&changeCounter, fChild->PartitionID(), &childChangeCounter,
				fString);
			updateChildChangeCounter = true;
			break;
		case B_DISK_DEVICE_JOB_SET_CONTENT_NAME:
			error = _kern_set_partition_content_name(fPartition->PartitionID(),
				&changeCounter, fString);
			break;
		case B_DISK_DEVICE_JOB_SET_TYPE:
			error = _kern_set_partition_type(fPartition->PartitionID(),
				&changeCounter, fChild->PartitionID(), &childChangeCounter,
				fString);
			updateChildChangeCounter = true;
			break;
		case B_DISK_DEVICE_JOB_SET_PARAMETERS:
			error = _kern_set_partition_parameters(fPartition->PartitionID(),
				&changeCounter, fChild->PartitionID(), &childChangeCounter,
				fString);
			updateChildChangeCounter = true;
			break;
		case B_DISK_DEVICE_JOB_SET_CONTENT_PARAMETERS:
			error = _kern_set_partition_content_parameters(
				fPartition->PartitionID(), &changeCounter, fString);
			break;
		default:
			return B_BAD_VALUE;
	}

	if (error != B_OK)
		return error;

	fPartition->SetChangeCounter(changeCounter);
	if (updateChildChangeCounter)
		fChild->SetChangeCounter(childChangeCounter);

	return B_OK;
}
