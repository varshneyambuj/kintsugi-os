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
 * @file ResizeJob.cpp
 * @brief Disk device job that resizes a child partition and its content area.
 *
 * Implements the ResizeJob class which calls the kernel resize syscall to
 * change both the raw partition size and the usable content size of a child
 * partition. Both the parent and the child change counters are updated on
 * success.
 *
 * @see DiskDeviceJob
 */

#include "ResizeJob.h"

#include <syscalls.h>

#include "DiskDeviceUtils.h"
#include "PartitionReference.h"


/**
 * @brief Constructs a ResizeJob with the target sizes for the child partition.
 *
 * @param partition   Reference to the parent partition that contains the child.
 * @param child       Reference to the child partition to be resized.
 * @param size        New raw partition size in bytes.
 * @param contentSize New usable content size in bytes.
 */
ResizeJob::ResizeJob(PartitionReference* partition, PartitionReference* child,
		off_t size, off_t contentSize)
	:
	DiskDeviceJob(partition, child),
	fSize(size),
	fContentSize(contentSize)
{
}


/**
 * @brief Destroys the ResizeJob and releases any held resources.
 */
ResizeJob::~ResizeJob()
{
}


/**
 * @brief Executes the resize operation by calling the kernel syscall.
 *
 * On success both the parent and child change counters are updated to
 * reflect the new on-disk state.
 *
 * @return B_OK on success, or an error code returned by the kernel.
 */
status_t
ResizeJob::Do()
{
	int32 changeCounter = fPartition->ChangeCounter();
	int32 childChangeCounter = fChild->ChangeCounter();
	status_t error = _kern_resize_partition(fPartition->PartitionID(),
		&changeCounter, fChild->PartitionID(), &childChangeCounter, fSize,
		fContentSize);
	if (error != B_OK)
		return error;

	fPartition->SetChangeCounter(changeCounter);
	fChild->SetChangeCounter(childChangeCounter);

	return B_OK;
}
