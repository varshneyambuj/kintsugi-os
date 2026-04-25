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

/** @file RepairJob.h
    @brief Job that checks or repairs the on-disk content of a partition. */

#ifndef _REPAIR_JOB_H
#define _REPAIR_JOB_H

#include "DiskDeviceJob.h"


namespace BPrivate {


/** @brief Job that runs the disk system's check or repair pass on a partition. */
class RepairJob : public DiskDeviceJob {
public:

								RepairJob(PartitionReference* partition,
									bool checkOnly);
	virtual						~RepairJob();

	virtual	status_t			Do();

private:
			bool				fCheckOnly;
};


}	// namespace BPrivate

using BPrivate::RepairJob;

#endif	// _REPAIR_JOB_H
