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
 * MIT License. Copyright 2003-2007, Haiku.
 * Original author: Ingo Weinhold.
 */

/** @file DiskDeviceJobGenerator.h
    @brief Walks a modified BDiskDevice tree and emits the ordered list of
    DiskDeviceJob steps needed to commit the changes. */

#ifndef _DISK_DEVICE_JOB_GENERATOR_H
#define _DISK_DEVICE_JOB_GENERATOR_H

#include <DiskDeviceDefs.h>


class BDiskDevice;
class BMutablePartition;
class BPartition;


namespace BPrivate {


class DiskDeviceJob;
class DiskDeviceJobQueue;
class PartitionReference;


/** @brief Diffs a BDiskDevice against its on-disk state and produces the job queue
    that realizes the pending modifications. */
class DiskDeviceJobGenerator {
public:
								DiskDeviceJobGenerator(BDiskDevice* device,
									DiskDeviceJobQueue* jobQueue);
								~DiskDeviceJobGenerator();

			status_t			GenerateJobs();

private:
			status_t			_AddJob(DiskDeviceJob* job);

			status_t			_GenerateCleanupJobs(BPartition* partition);
			status_t			_GeneratePlacementJobs(BPartition* partition);
			status_t			_GenerateChildPlacementJobs(
									BPartition* partition);
			status_t			_GenerateRemainingJobs(BPartition* parent,
									BPartition* partition);

			BMutablePartition*	_GetMutablePartition(BPartition* partition);

			status_t			_GenerateInitializeJob(BPartition* partition);
			status_t			_GenerateUninitializeJob(BPartition* partition);
			status_t			_GenerateSetContentNameJob(
									BPartition* partition);
			status_t			_GenerateSetContentParametersJob(
									BPartition* partition);
			status_t			_GenerateDefragmentJob(BPartition* partition);
			status_t			_GenerateRepairJob(BPartition* partition,
									bool repair);

			status_t			_GenerateCreateChildJob(BPartition* parent,
									BPartition* partition);
			status_t			_GenerateDeleteChildJob(BPartition* parent,
									BPartition* partition);
			status_t			_GenerateResizeJob(BPartition* partition);
			status_t			_GenerateMoveJob(BPartition* partition);
			status_t 			_GenerateSetNameJob(BPartition* parent,
									BPartition* partition);
			status_t			_GenerateSetTypeJob(BPartition* parent,
									BPartition* partition);
			status_t			_GenerateSetParametersJob(BPartition* parent,
									BPartition* partition);

			status_t			_CollectContentsToMove(BPartition* partition);
			status_t			_PushContentsToMove(BPartition* partition);

			status_t			_GetPartitionReference(BPartition* partition,
									PartitionReference*& reference);

	static	int					_CompareMoveInfoPosition(const void* _a,
									const void* _b);

private:
			struct MoveInfo;
			struct PartitionRefInfo;

			BDiskDevice*		fDevice;
			DiskDeviceJobQueue*	fJobQueue;
			MoveInfo*			fMoveInfos;
			int32				fPartitionCount;
			PartitionRefInfo*	fPartitionRefs;
			PartitionReference** fContentsToMove;
			int32				fContentsToMoveCount;
};


}	// namespace BPrivate

using BPrivate::DiskDeviceJobGenerator;

#endif	// _DISK_DEVICE_JOB_GENERATOR_H
