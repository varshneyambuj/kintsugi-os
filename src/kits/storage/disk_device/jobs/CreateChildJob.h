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

/** @file CreateChildJob.h
    @brief Job that creates a new child partition inside a parent. */

#ifndef _CREATE_CHILD_JOB_H
#define _CREATE_CHILD_JOB_H

#include "DiskDeviceJob.h"


namespace BPrivate {


/** @brief Job that adds a new child partition with the requested geometry, type, and parameters. */
class CreateChildJob : public DiskDeviceJob {
public:

								CreateChildJob(PartitionReference* partition,
									PartitionReference* child);
	virtual						~CreateChildJob();

			status_t			Init(off_t offset, off_t size,
									const char* type, const char* name,
									const char* parameters);

	virtual	status_t			Do();

protected:
			off_t				fOffset;
			off_t				fSize;
			char*				fType;
			char*				fName;
			char*				fParameters;
};


}	// namespace BPrivate

using BPrivate::CreateChildJob;

#endif	// _CREATE_CHILD_JOB_H
