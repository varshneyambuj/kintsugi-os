/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2007, Haiku.
 * Original author: Ingo Weinhold.
 */

/** @file DiskDeviceJob.h
    @brief Abstract base class for queued disk-device modification jobs. */

#ifndef _DISK_DEVICE_JOB_H
#define _DISK_DEVICE_JOB_H

#include <SupportDefs.h>


namespace BPrivate {


class PartitionReference;


/** @brief Base class for an atomic disk modification step executed by the job queue. */
class DiskDeviceJob {
public:
								DiskDeviceJob(PartitionReference* partition,
									PartitionReference* child = NULL);
	virtual						~DiskDeviceJob();

	virtual	status_t			Do() = 0;

protected:
			PartitionReference*	fPartition;
			PartitionReference*	fChild;
};


}	// namespace BPrivate

using BPrivate::DiskDeviceJob;

#endif	// _DISK_DEVICE_JOB_H
