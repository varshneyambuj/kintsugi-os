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
 * @file DiskDeviceJob.cpp
 * @brief Base class for disk device modification jobs.
 *
 * Defines the DiskDeviceJob base class, which represents a single unit of
 * work to be performed on a disk device or partition. Each job holds
 * references to the affected partition and an optional child partition,
 * keeping them alive for the duration of the job.
 *
 * @see DiskDeviceJobQueue
 */

#include "DiskDeviceJob.h"

#include "PartitionReference.h"


/**
 * @brief Constructs a DiskDeviceJob targeting the given partition references.
 *
 * Acquires a reference on each non-null partition reference so that the
 * objects remain valid for the lifetime of the job.
 *
 * @param partition Reference to the primary partition this job operates on.
 * @param child     Reference to the child partition involved in the job,
 *                  or NULL if none.
 */
DiskDeviceJob::DiskDeviceJob(PartitionReference* partition,
		PartitionReference* child)
	:
	fPartition(partition),
	fChild(child)
{
	if (fPartition)
		fPartition->AcquireReference();

	if (fChild)
		fChild->AcquireReference();
}


/**
 * @brief Destroys the DiskDeviceJob and releases held partition references.
 *
 * Releases the reference on each non-null partition reference that was
 * acquired during construction.
 */
DiskDeviceJob::~DiskDeviceJob()
{
	if (fPartition)
		fPartition->ReleaseReference();

	if (fChild)
		fChild->ReleaseReference();
}
