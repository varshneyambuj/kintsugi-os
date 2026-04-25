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

/** @file UninitializeJob.h
    @brief Job that strips an existing disk system from a partition. */

#ifndef _UNINITIALIZE_JOB_H
#define _UNINITIALIZE_JOB_H

#include "DiskDeviceJob.h"


namespace BPrivate {


/** @brief Job that clears an initialized partition's disk-system content. */
class UninitializeJob : public DiskDeviceJob {
public:

								UninitializeJob(PartitionReference* partition,
									PartitionReference* parent = NULL);
	virtual						~UninitializeJob();

	virtual	status_t			Do();
};


}	// namespace BPrivate

using BPrivate::UninitializeJob;

#endif	// _UNINITIALIZE_JOB_H
