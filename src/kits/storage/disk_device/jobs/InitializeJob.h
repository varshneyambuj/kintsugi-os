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

/** @file InitializeJob.h
    @brief Job that initializes a partition with a chosen disk system. */

#ifndef _INITIALIZE_JOB_H
#define _INITIALIZE_JOB_H

#include "DiskDeviceJob.h"


namespace BPrivate {


/** @brief Job that formats a partition with the requested disk system, name, and parameters. */
class InitializeJob : public DiskDeviceJob {
public:

								InitializeJob(PartitionReference* partition);
	virtual						~InitializeJob();

			status_t			Init(const char* diskSystem, const char* name,
									const char* parameters);

	virtual	status_t			Do();

protected:
			char*				fDiskSystem;
			char*				fName;
			char*				fParameters;
};


}	// namespace BPrivate

using BPrivate::InitializeJob;

#endif	// _INITIALIZE_JOB_H
