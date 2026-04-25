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

/** @file ResizeJob.h
    @brief Job that resizes a child partition and, if needed, its content. */

#ifndef _RESIZE_JOB_H
#define _RESIZE_JOB_H

#include "DiskDeviceJob.h"


namespace BPrivate {


/** @brief Job that changes the on-disk size of a child partition and its
    contained content to the requested values. */
class ResizeJob : public DiskDeviceJob {
public:

								ResizeJob(PartitionReference* partition,
									PartitionReference* child, off_t size,
									off_t contentSize);
	virtual						~ResizeJob();

	virtual	status_t			Do();

protected:
			off_t				fSize;
			off_t				fContentSize;
};


}	// namespace BPrivate

using BPrivate::ResizeJob;

#endif	// _RESIZE_JOB_H
