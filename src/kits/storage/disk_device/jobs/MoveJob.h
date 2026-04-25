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

/** @file MoveJob.h
    @brief Job that relocates a partition (and its dependent contents) to a
    new offset. */

#ifndef _MOVE_JOB_H
#define _MOVE_JOB_H

#include "DiskDeviceJob.h"


namespace BPrivate {


/** @brief Job that moves a child partition to a new offset, carrying along
    any contained partitions that follow. */
class MoveJob : public DiskDeviceJob {
public:

								MoveJob(PartitionReference* partition,
									PartitionReference* child);
	virtual						~MoveJob();

			status_t			Init(off_t offset,
									PartitionReference** contents,
									int32 contentsCount);

	virtual	status_t			Do();

protected:
			off_t				fOffset;
			PartitionReference** fContents;
			int32				fContentsCount;
};


}	// namespace BPrivate

using BPrivate::MoveJob;

#endif	// _MOVE_JOB_H
