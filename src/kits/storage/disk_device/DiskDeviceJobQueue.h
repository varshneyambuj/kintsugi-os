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

/** @file DiskDeviceJobQueue.h
    @brief Owning FIFO queue of DiskDeviceJob objects executed in order. */

#ifndef _DISK_DEVICE_JOB_QUEUE_H
#define _DISK_DEVICE_JOB_QUEUE_H

#include <DiskDeviceDefs.h>
#include <ObjectList.h>


namespace BPrivate {


class DiskDeviceJob;


/** @brief Sequentially executes a list of disk-device jobs and owns their lifetime. */
class DiskDeviceJobQueue {
public:
								DiskDeviceJobQueue();
								~DiskDeviceJobQueue();

			status_t			AddJob(DiskDeviceJob* job);

			status_t			Execute();

private:
	typedef	BObjectList<DiskDeviceJob, true> JobList;

			JobList				fJobs;
};


}	// namespace BPrivate

using BPrivate::DiskDeviceJobQueue;

#endif	// _DISK_DEVICE_JOB_QUEUE_H
