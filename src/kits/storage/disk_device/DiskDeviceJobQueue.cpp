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
 * @file DiskDeviceJobQueue.cpp
 * @brief Ordered queue for sequencing and executing disk device jobs.
 *
 * Implements DiskDeviceJobQueue, which collects DiskDeviceJob instances and
 * executes them in the order they were added. If any job fails, execution
 * halts and the error is returned to the caller.
 *
 * @see DiskDeviceJob
 */

#include "DiskDeviceJobQueue.h"

#include <stdio.h>
#include <string.h>

#include <typeinfo>

#include "DiskDeviceJob.h"


#undef TRACE
//#define TRACE(x...)
#define TRACE(x...)	printf(x)


/**
 * @brief Constructs an empty DiskDeviceJobQueue.
 *
 * Initialises the internal job list with an initial capacity of 20 entries.
 */
DiskDeviceJobQueue::DiskDeviceJobQueue()
	: fJobs(20)
{
}


/**
 * @brief Destroys the DiskDeviceJobQueue.
 *
 * The queue does not own the job objects; callers are responsible for
 * freeing any jobs that were added.
 */
DiskDeviceJobQueue::~DiskDeviceJobQueue()
{
}


/**
 * @brief Appends a job to the end of the queue.
 *
 * @param job The job to add. Must not be NULL.
 * @return B_OK on success, B_BAD_VALUE if job is NULL, or B_NO_MEMORY if the
 *         internal list could not be extended.
 */
status_t
DiskDeviceJobQueue::AddJob(DiskDeviceJob* job)
{
	if (!job)
		return B_BAD_VALUE;

	return fJobs.AddItem(job) ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Executes all queued jobs in insertion order.
 *
 * Iterates through every job and calls its Do() method. Execution stops
 * immediately if any job returns a non-B_OK status code.
 *
 * @return B_OK if all jobs completed successfully, otherwise the error code
 *         returned by the first failing job.
 */
status_t
DiskDeviceJobQueue::Execute()
{
	int32 count = fJobs.CountItems();
	for (int32 i = 0; i < count; i++) {
		DiskDeviceJob* job = fJobs.ItemAt(i);

		TRACE("DiskDeviceJobQueue::Execute(): executing job: %s\n",
			typeid(*job).name());

		status_t error = job->Do();
		if (error != B_OK) {
			TRACE("DiskDeviceJobQueue::Execute(): executing job failed: %s\n",
				strerror(error));
			return error;
		}
	}

	return B_OK;
}
