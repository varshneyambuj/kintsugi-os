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
 * @file InitializeJob.cpp
 * @brief Disk-device job that initialises a partition with a new file system.
 *
 * InitializeJob is a DiskDeviceJob subclass that wraps the
 * _kern_initialize_partition() syscall.  It stores the disk-system name,
 * optional partition name, and optional parameter string, then executes the
 * kernel operation in Do() while tracking the partition change counter so
 * that subsequent jobs see an up-to-date view of the partition state.
 *
 * @see DiskDeviceJob, PartitionReference
 */


#include "InitializeJob.h"

#include <syscalls.h>

#include "DiskDeviceUtils.h"
#include "PartitionReference.h"


/**
 * @brief Constructs an InitializeJob for the given partition.
 *
 * All string parameters (disk system, name, parameters) are NULL until
 * Init() is called.
 *
 * @param partition PartitionReference identifying the partition to initialise.
 */
InitializeJob::InitializeJob(PartitionReference* partition)
	:
	DiskDeviceJob(partition),
	fDiskSystem(NULL),
	fName(NULL),
	fParameters(NULL)
{
}


/**
 * @brief Destroys the InitializeJob and frees all stored strings.
 */
InitializeJob::~InitializeJob()
{
	free(fDiskSystem);
	free(fName);
	free(fParameters);
}


/**
 * @brief Sets the disk-system name, partition name, and parameter string.
 *
 * Each string is duplicated into heap memory.  If any duplication fails the
 * method returns immediately with the appropriate error code.
 *
 * @param diskSystem Name of the disk system (file system) to use (e.g. "BFS").
 * @param name       Optional partition name; may be NULL or empty.
 * @param parameters Optional file-system-specific parameter string; may be NULL.
 * @return B_OK on success, B_NO_MEMORY if string duplication fails.
 */
status_t
InitializeJob::Init(const char* diskSystem, const char* name,
	const char* parameters)
{
	SET_STRING_RETURN_ON_ERROR(fDiskSystem, diskSystem);
	SET_STRING_RETURN_ON_ERROR(fName, name);
	SET_STRING_RETURN_ON_ERROR(fParameters, parameters);

	return B_OK;
}


/**
 * @brief Executes the partition initialisation via the kernel syscall.
 *
 * Passes the current change counter to _kern_initialize_partition() and
 * updates the PartitionReference with the new counter on success so that
 * subsequent jobs observe the updated state.
 *
 * @return B_OK on success, or a kernel error code on failure.
 */
status_t
InitializeJob::Do()
{
	int32 changeCounter = fPartition->ChangeCounter();

	status_t error = _kern_initialize_partition(fPartition->PartitionID(),
		&changeCounter, fDiskSystem, fName, fParameters);
	if (error != B_OK)
		return error;

	fPartition->SetChangeCounter(changeCounter);

	return B_OK;
}
