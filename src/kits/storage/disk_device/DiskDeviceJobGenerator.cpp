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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2003-2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file DiskDeviceJobGenerator.cpp
 * @brief Translates pending partition modifications into an ordered job queue.
 *
 * DiskDeviceJobGenerator walks the shadow (mutable) partition tree of a
 * BDiskDevice and compares it against the live partition tree to determine
 * which operations are required. It emits cleanup jobs (delete/uninitialize),
 * placement jobs (resize/move), and remaining jobs (initialize, create, rename,
 * retype, set-parameters) in the correct dependency order into a
 * DiskDeviceJobQueue.
 *
 * @see DiskDeviceJobQueue
 */

#include "DiskDeviceJobGenerator.h"

#include <new>

#include <stdlib.h>
#include <string.h>

#include <DiskDevice.h>
#include <MutablePartition.h>

#include <ddm_userland_interface_defs.h>

#include "DiskDeviceJob.h"
#include "DiskDeviceJobQueue.h"
#include "PartitionDelegate.h"
#include "PartitionReference.h"

#include "CreateChildJob.h"
#include "DeleteChildJob.h"
#include "DefragmentJob.h"
#include "InitializeJob.h"
#include "MoveJob.h"
#include "RepairJob.h"
#include "ResizeJob.h"
#include "SetStringJob.h"
#include "UninitializeJob.h"


#undef TRACE
#define TRACE(x...)
//#define TRACE(x...)	printf(x)


using std::nothrow;


/**
 * @brief NULL-aware strcmp helper used when comparing partition property strings.
 *
 * NULL is considered the smallest possible value; two NULL pointers compare
 * equal.
 *
 * @param str1 First string, may be \c NULL.
 * @param str2 Second string, may be \c NULL.
 * @return Negative if \a str1 < \a str2, zero if equal, positive if greater.
 */
static inline int
compare_string(const char* str1, const char* str2)
{
	if (str1 == NULL) {
		if (str2 == NULL)
			return 0;
		return 1;
	} else if (str2 == NULL)
		return -1;

	return strcmp(str1, str2);
}


// MoveInfo
struct DiskDeviceJobGenerator::MoveInfo {
	BPartition*	partition;
	off_t		position;
	off_t		target_position;
	off_t		size;
};


// PartitionRefInfo
struct DiskDeviceJobGenerator::PartitionRefInfo {
	PartitionRefInfo()
		: partition(NULL),
		  reference(NULL)
	{
	}

	~PartitionRefInfo()
	{
		if (reference)
			reference->ReleaseReference();
	}

	BPartition*			partition;
	PartitionReference*	reference;
};


/**
 * @brief Constructs a DiskDeviceJobGenerator for the given device and job queue.
 *
 * Pre-allocates internal arrays large enough to handle the worst-case
 * scenario where all existing partitions are deleted and replaced by new
 * ones.
 *
 * @param device   The disk device whose modifications will be translated.
 * @param jobQueue The queue that will receive the generated jobs.
 */
DiskDeviceJobGenerator::DiskDeviceJobGenerator(BDiskDevice* device,
		DiskDeviceJobQueue* jobQueue)
	: fDevice(device),
	  fJobQueue(jobQueue),
	  fMoveInfos(NULL),
	  fPartitionRefs(NULL),
	  fContentsToMove(NULL),
	  fContentsToMoveCount(0)
{
	// Make sure the arrays are big enough (worst case: all old partitions have
	// been deleted and new ones been created).
	fPartitionCount = fDevice->CountDescendants()
		+ fDevice->_CountDescendants();

	fMoveInfos = new(nothrow) MoveInfo[fPartitionCount];
	fPartitionRefs = new(nothrow) PartitionRefInfo[fPartitionCount];
	fContentsToMove = new(nothrow) PartitionReference*[fPartitionCount];
}


/**
 * @brief Destroys the DiskDeviceJobGenerator and frees all pre-allocated arrays.
 */
DiskDeviceJobGenerator::~DiskDeviceJobGenerator()
{
	delete[] fMoveInfos;
	delete[] fPartitionRefs;
	delete[] fContentsToMove;
}


/**
 * @brief Generates all required jobs and appends them to the job queue.
 *
 * The generation proceeds in three ordered passes: cleanup (deletions and
 * uninitializations), placement (resizes and moves), and remaining changes
 * (initializations, creations, and property changes).
 *
 * @return B_OK on success, B_BAD_VALUE if the device or queue is NULL,
 *         B_NO_MEMORY if array allocation failed, or the first error
 *         encountered during job generation.
 */
status_t
DiskDeviceJobGenerator::GenerateJobs()
{
	// check parameters
	if (!fDevice || !fJobQueue)
		return B_BAD_VALUE;

	if (!fMoveInfos || !fPartitionRefs || !fContentsToMove)
		return B_NO_MEMORY;

	// 1) Generate jobs for all physical partitions that don't have an
	// associated shadow partition, i.e. those that shall be deleted.
	// 2) Generate uninitialize jobs for all partition whose initialization
	// changes, also those that shall be initialized with a disk system.
	// This simplifies moving and resizing.
	status_t error = _GenerateCleanupJobs(fDevice);
	if (error != B_OK) {
		TRACE("DiskDeviceJobGenerator::GenerateJobs(): _GenerateCleanupJobs() "
			"failed\n");
		return error;
	}

	// Generate jobs that move and resize the remaining physical partitions
	// to their final position/size.
	error = _GeneratePlacementJobs(fDevice);
	if (error != B_OK) {
		TRACE("DiskDeviceJobGenerator::GenerateJobs(): "
			"_GeneratePlacementJobs() failed\n");
		return error;
	}

	// Generate the remaining jobs in one run: initialization, creation of
	// partitions, and changing of name, content name, type, parameters, and
	// content parameters.
	error = _GenerateRemainingJobs(NULL, fDevice);
	if (error != B_OK) {
		TRACE("DiskDeviceJobGenerator::GenerateJobs(): "
			"_GenerateRemainingJobs() failed\n");
		return error;
	}

	TRACE("DiskDeviceJobGenerator::GenerateJobs(): succeeded\n");

	return B_OK;
}


/**
 * @brief Adds a job to the queue, deleting it on failure.
 *
 * @param job The job to add; a NULL pointer is treated as B_NO_MEMORY.
 * @return B_OK on success, B_NO_MEMORY if \a job is NULL, or the error
 *         returned by DiskDeviceJobQueue::AddJob().
 */
status_t
DiskDeviceJobGenerator::_AddJob(DiskDeviceJob* job)
{
	if (!job)
		return B_NO_MEMORY;

	status_t error = fJobQueue->AddJob(job);
	if (error != B_OK)
		delete job;

	return error;
}


/**
 * @brief Recursively generates delete and uninitialize jobs for the cleanup pass.
 *
 * Partitions that have no matching shadow are scheduled for deletion.
 * Partitions whose initialization state is changing are scheduled for
 * uninitialization before the placement pass runs.
 *
 * @param partition The root partition to start the recursive walk from.
 * @return B_OK on success, or the first error encountered.
 */
status_t
DiskDeviceJobGenerator::_GenerateCleanupJobs(BPartition* partition)
{
// TODO: Depending on how this shall be handled, we might want to unmount
// all descendants of a partition to be uninitialized or removed.
	if (BMutablePartition* shadow = _GetMutablePartition(partition)) {
		if ((shadow->ChangeFlags() & B_PARTITION_CHANGED_INITIALIZATION)
			&& partition->fPartitionData->content_type) {
			// partition changes initialization
			status_t error = _GenerateUninitializeJob(partition);
			if (error != B_OK)
				return error;
		} else {
			// recurse
			for (int32 i = 0; BPartition* child = partition->_ChildAt(i); i++) {
				status_t error = _GenerateCleanupJobs(child);
				if (error != B_OK)
					return error;
			}
		}
	} else if (BPartition* parent = partition->Parent()) {
		// create job and add it to the queue
		status_t error = _GenerateDeleteChildJob(parent, partition);
		if (error != B_OK)
			return error;
	}
	return B_OK;
}


/**
 * @brief Generates resize and move jobs to bring a partition to its target layout.
 *
 * Handles the grow-before-move and shrink-after-move ordering rules so that
 * partitions never overlap during relocation.
 *
 * @param partition The partition to place; children are handled recursively.
 * @return B_OK on success, B_ERROR if an unrecognised partition needs
 *         resizing, or the first error from a generated job.
 */
status_t
DiskDeviceJobGenerator::_GeneratePlacementJobs(BPartition* partition)
{
	if (BMutablePartition* shadow = _GetMutablePartition(partition)) {
		// Don't resize/move partitions that have an unrecognized contents.
		// They must have been uninitialized before.
		if (shadow->Status() == B_PARTITION_UNRECOGNIZED
			&& (shadow->Size() != partition->Size()
				|| shadow->Offset() != partition->Offset())) {
			return B_ERROR;
		}

		if (shadow->Size() > partition->Size()) {
			// size grows: resize first
			status_t error = _GenerateResizeJob(partition);
			if (error != B_OK)
				return error;
		}

		// place the children
		status_t error = _GenerateChildPlacementJobs(partition);
		if (error != B_OK)
			return error;

		if (shadow->Size() < partition->Size()) {
			// size shrinks: resize now
			status_t error = _GenerateResizeJob(partition);
			if (error != B_OK)
				return error;
		}
	}

	return B_OK;
}


/**
 * @brief Generates ordered move and resize jobs for all children of a partition.
 *
 * First shrinks children that need to shrink (so they do not block moves),
 * then resolves move conflicts using a greedy sort-and-sweep algorithm, and
 * finally processes children that grow or keep their size.
 *
 * @param partition The parent whose children are to be placed.
 * @return B_OK on success, B_ERROR if a deadlock is detected in the move
 *         ordering, or the first error from a generated job.
 */
status_t
DiskDeviceJobGenerator::_GenerateChildPlacementJobs(BPartition* partition)
{
	BMutablePartition* shadow = _GetMutablePartition(partition);

	// nothing to do, if the partition contains no partitioning system or
	// shall be re-initialized
	if (!shadow->ContentType()
		|| (shadow->ChangeFlags() & B_PARTITION_CHANGED_INITIALIZATION)) {
		return B_OK;
	}

	// first resize all children that shall shrink and place their descendants
	int32 childCount = 0;
	int32 moveForth = 0;
	int32 moveBack = 0;

	for (int32 i = 0; BPartition* child = partition->_ChildAt(i); i++) {
		if (BMutablePartition* childShadow = _GetMutablePartition(child)) {
			// add a MoveInfo for the child
			MoveInfo& info = fMoveInfos[childCount];
			childCount++;
			info.partition = child;
			info.position = child->Offset();
			info.target_position = childShadow->Offset();
			info.size = child->Size();

			if (info.position < info.target_position)
				moveForth++;
			else if (info.position > info.target_position)
				moveBack++;

			// resize the child, if it shall shrink
			if (childShadow->Size() < child->Size()) {
				status_t error = _GeneratePlacementJobs(child);
				if (error != B_OK)
					return error;
				info.size = childShadow->Size();
			}
		}
	}

	// sort the move infos
	if (childCount > 0 && moveForth + moveBack > 0) {
		qsort(fMoveInfos, childCount, sizeof(MoveInfo),
			_CompareMoveInfoPosition);
	}

	// move the children to their final positions
	while (moveForth + moveBack > 0) {
		int32 moved = 0;
		if (moveForth < moveBack) {
			// move children back
			for (int32 i = 0; i < childCount; i++) {
				MoveInfo& info = fMoveInfos[i];
				if (info.position > info.target_position) {
					if (i == 0
						|| info.target_position >= fMoveInfos[i - 1].position
							+ fMoveInfos[i - 1].size) {
						// check OK -- the partition wouldn't be moved before
						// the end of the preceding one
						status_t error = _GenerateMoveJob(info.partition);
						if (error != B_OK)
							return error;
						info.position = info.target_position;
						moved++;
						moveBack--;
					}
				}
			}
		} else {
			// move children forth
			for (int32 i = childCount - 1; i >= 0; i--) {
				MoveInfo &info = fMoveInfos[i];
				if (info.position > info.target_position) {
					if (i == childCount - 1
						|| info.target_position + info.size
							<= fMoveInfos[i - 1].position) {
						// check OK -- the partition wouldn't be moved before
						// the end of the preceding one
						status_t error = _GenerateMoveJob(info.partition);
						if (error != B_OK)
							return error;
						info.position = info.target_position;
						moved++;
						moveForth--;
					}
				}
			}
		}

		// terminate, if no partition could be moved
		if (moved == 0)
			return B_ERROR;
	}

	// now resize all children that shall grow/keep their size and place
	// their descendants
	for (int32 i = 0; BPartition* child = partition->_ChildAt(i); i++) {
		if (BMutablePartition* childShadow = _GetMutablePartition(child)) {
			if (childShadow->Size() >= child->Size()) {
				status_t error = _GeneratePlacementJobs(child);
				if (error != B_OK)
					return error;
			}
		}
	}

	return B_OK;
}


/**
 * @brief Generates initialization, creation, and property-change jobs for a partition tree.
 *
 * Handles new partitions (create), property changes on existing partitions
 * (name, type, parameters, content name, content parameters, defragmentation,
 * repair), and initialization changes, then recurses into children.
 *
 * @param parent    The parent partition, or \c NULL if \a partition is the root.
 * @param partition The partition to process.
 * @return B_OK on success, B_BAD_VALUE if a parent is required but absent,
 *         or the first error from a generated job.
 */
status_t
DiskDeviceJobGenerator::_GenerateRemainingJobs(BPartition* parent,
	BPartition* partition)
{
	user_partition_data* partitionData = partition->fPartitionData;

	uint32 changeFlags
		= partition->fDelegate->MutablePartition()->ChangeFlags();

	// create the partition, if not existing yet
	if (!partitionData) {
		if (!parent)
			return B_BAD_VALUE;

		status_t error = _GenerateCreateChildJob(parent, partition);
		if (error != B_OK)
			return error;
	} else {
		// partition already exists: set non-content properties

		// name
		if ((changeFlags & B_PARTITION_CHANGED_NAME)
			|| compare_string(partition->Name(), partitionData->name)) {
			if (!parent)
				return B_BAD_VALUE;

			status_t error = _GenerateSetNameJob(parent, partition);
			if (error != B_OK)
				return error;
		}

		// type
		if ((changeFlags & B_PARTITION_CHANGED_TYPE)
			|| compare_string(partition->Type(), partitionData->type)) {
			if (!parent)
				return B_BAD_VALUE;

			status_t error = _GenerateSetTypeJob(parent, partition);
			if (error != B_OK)
				return error;
		}

		// parameters
		if ((partition->Parameters() != NULL)
			&& ((changeFlags & B_PARTITION_CHANGED_PARAMETERS) != 0
				|| compare_string(partition->Parameters(),
					partitionData->parameters))) {
			if (!parent)
				return B_BAD_VALUE;

			status_t error = _GenerateSetParametersJob(parent, partition);
			if (error != B_OK)
				return error;
		}
	}

	if (partition->ContentType()) {
		// initialize the partition, if required
		if (changeFlags & B_PARTITION_CHANGED_INITIALIZATION) {
			status_t error = _GenerateInitializeJob(partition);
			if (error != B_OK)
				return error;
		} else {
			// partition not (re-)initialized, set content properties

			// content name
			if ((changeFlags & B_PARTITION_CHANGED_NAME)
				|| compare_string(partition->RawContentName(),
					partitionData->content_name)) {
				status_t error = _GenerateSetContentNameJob(partition);
				if (error != B_OK)
					return error;
			}

			// content parameters
			if ((partition->ContentParameters() != NULL)
				&& ((changeFlags & B_PARTITION_CHANGED_PARAMETERS) != 0
					|| compare_string(partition->ContentParameters(),
						partitionData->content_parameters))) {
				status_t error = _GenerateSetContentParametersJob(partition);
				if (error != B_OK)
					return error;
			}

			// defragment
			if (changeFlags & B_PARTITION_CHANGED_DEFRAGMENTATION) {
				status_t error = _GenerateDefragmentJob(partition);
				if (error != B_OK)
					return error;
			}

			// check / repair
			bool repair = (changeFlags & B_PARTITION_CHANGED_REPAIR);
			if ((changeFlags & B_PARTITION_CHANGED_CHECK)
				|| repair) {
				status_t error = _GenerateRepairJob(partition, repair);
				if (error != B_OK)
					return error;
			}
		}
	}

	// recurse
	for (int32 i = 0; BPartition* child = partition->ChildAt(i); i++) {
		status_t error = _GenerateRemainingJobs(partition, child);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Returns the BMutablePartition shadow for the given live partition.
 *
 * @param partition The live partition whose shadow is requested.
 * @return The associated BMutablePartition, or \c NULL if none exists.
 */
BMutablePartition*
DiskDeviceJobGenerator::_GetMutablePartition(BPartition* partition)
{
	if (!partition)
		return NULL;

	return partition->fDelegate
		? partition->fDelegate->MutablePartition() : NULL;
}


/**
 * @brief Creates and enqueues an InitializeJob for the given partition.
 *
 * @param partition The partition to initialize.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DiskDeviceJobGenerator::_GenerateInitializeJob(BPartition* partition)
{
	PartitionReference* reference;
	status_t error = _GetPartitionReference(partition, reference);
	if (error != B_OK)
		return error;

	InitializeJob* job = new(nothrow) InitializeJob(reference);
	if (!job)
		return B_NO_MEMORY;

	error = job->Init(partition->ContentType(),
		partition->RawContentName(), partition->ContentParameters());
	if (error != B_OK) {
		delete job;
		return error;
	}

	return _AddJob(job);
}


/**
 * @brief Creates and enqueues a UninitializeJob for the given partition.
 *
 * @param partition The partition to uninitialize.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DiskDeviceJobGenerator::_GenerateUninitializeJob(BPartition* partition)
{
	PartitionReference* reference;
	status_t error = _GetPartitionReference(partition, reference);
	if (error != B_OK)
		return error;

	BPartition* parent = partition->Parent();
	PartitionReference* parentReference = NULL;
	if (parent != NULL) {
		error = _GetPartitionReference(parent, parentReference);
		if (error != B_OK)
			return error;
	}

	return _AddJob(new(nothrow) UninitializeJob(reference, parentReference));
}


/**
 * @brief Creates and enqueues a SetStringJob to update a partition's content name.
 *
 * @param partition The partition whose content name is to be changed.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DiskDeviceJobGenerator::_GenerateSetContentNameJob(BPartition* partition)
{
	PartitionReference* reference;
	status_t error = _GetPartitionReference(partition, reference);
	if (error != B_OK)
		return error;

	SetStringJob* job = new(nothrow) SetStringJob(reference);
	if (!job)
		return B_NO_MEMORY;

	error = job->Init(partition->RawContentName(),
		B_DISK_DEVICE_JOB_SET_CONTENT_NAME);
	if (error != B_OK) {
		delete job;
		return error;
	}

	return _AddJob(job);
}


/**
 * @brief Creates and enqueues a SetStringJob to update a partition's content parameters.
 *
 * @param partition The partition whose content parameters are to be changed.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DiskDeviceJobGenerator::_GenerateSetContentParametersJob(BPartition* partition)
{
	PartitionReference* reference;
	status_t error = _GetPartitionReference(partition, reference);
	if (error != B_OK)
		return error;

	SetStringJob* job = new(nothrow) SetStringJob(reference);
	if (!job)
		return B_NO_MEMORY;

	error = job->Init(partition->ContentParameters(),
		B_DISK_DEVICE_JOB_SET_CONTENT_PARAMETERS);
	if (error != B_OK) {
		delete job;
		return error;
	}

	return _AddJob(job);
}


/**
 * @brief Creates and enqueues a DefragmentJob for the given partition.
 *
 * @param partition The partition to defragment.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DiskDeviceJobGenerator::_GenerateDefragmentJob(BPartition* partition)
{
	PartitionReference* reference;
	status_t error = _GetPartitionReference(partition, reference);
	if (error != B_OK)
		return error;

	return _AddJob(new(nothrow) DefragmentJob(reference));
}


/**
 * @brief Creates and enqueues a RepairJob for the given partition.
 *
 * @param partition The partition to check or repair.
 * @param repair    When \c true a full repair is performed; when \c false
 *                  only a consistency check is run.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DiskDeviceJobGenerator::_GenerateRepairJob(BPartition* partition, bool repair)
{
	PartitionReference* reference;
	status_t error = _GetPartitionReference(partition, reference);
	if (error != B_OK)
		return error;

	return _AddJob(new(nothrow) RepairJob(reference, repair));
}


/**
 * @brief Creates and enqueues a CreateChildJob for the given parent/child pair.
 *
 * @param parent    The parent partition that will contain the new child.
 * @param partition The shadow partition describing the new child's properties.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DiskDeviceJobGenerator::_GenerateCreateChildJob(BPartition* parent,
	BPartition* partition)
{
	PartitionReference* parentReference;
	status_t error = _GetPartitionReference(parent, parentReference);
	if (error != B_OK)
		return error;

	PartitionReference* reference;
	error = _GetPartitionReference(partition, reference);
	if (error != B_OK)
		return error;

	CreateChildJob* job = new(nothrow) CreateChildJob(parentReference,
		reference);
	if (!job)
		return B_NO_MEMORY;

	error = job->Init(partition->Offset(), partition->Size(), partition->Type(),
		partition->Name(), partition->Parameters());
	if (error != B_OK) {
		delete job;
		return error;
	}

	return _AddJob(job);
}


/**
 * @brief Creates and enqueues a DeleteChildJob for the given parent/child pair.
 *
 * @param parent    The parent partition that owns the child.
 * @param partition The child partition to be deleted.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DiskDeviceJobGenerator::_GenerateDeleteChildJob(BPartition* parent,
	BPartition* partition)
{
	PartitionReference* parentReference;
	status_t error = _GetPartitionReference(parent, parentReference);
	if (error != B_OK)
		return error;

	PartitionReference* reference;
	error = _GetPartitionReference(partition, reference);
	if (error != B_OK)
		return error;

	return _AddJob(new(nothrow) DeleteChildJob(parentReference, reference));
}


/**
 * @brief Creates and enqueues a ResizeJob for the given partition.
 *
 * Retrieves the parent reference automatically from the partition hierarchy.
 *
 * @param partition The partition to resize; must have a parent.
 * @return B_OK on success, B_BAD_VALUE if the partition has no parent, or
 *         an error code on failure.
 */
status_t
DiskDeviceJobGenerator::_GenerateResizeJob(BPartition* partition)
{
	BPartition* parent = partition->Parent();
	if (!parent)
		return B_BAD_VALUE;

	PartitionReference* parentReference;
	status_t error = _GetPartitionReference(parent, parentReference);
	if (error != B_OK)
		return error;

	PartitionReference* reference;
	error = _GetPartitionReference(partition, reference);
	if (error != B_OK)
		return error;

	return _AddJob(new(nothrow) ResizeJob(parentReference, reference,
		partition->Size(), partition->ContentSize()));
}


/**
 * @brief Creates and enqueues a MoveJob for the given partition.
 *
 * Collects all descendants with active contents into the move list so that
 * they can be moved together with their parent.
 *
 * @param partition The partition to move; must have a parent.
 * @return B_OK on success, B_BAD_VALUE if the partition has no parent, or
 *         an error code on failure.
 */
status_t
DiskDeviceJobGenerator::_GenerateMoveJob(BPartition* partition)
{
	BPartition* parent = partition->Parent();
	if (!parent)
		return B_BAD_VALUE;

	PartitionReference* parentReference;
	status_t error = _GetPartitionReference(parent, parentReference);
	if (error != B_OK)
		return error;

	PartitionReference* reference;
	error = _GetPartitionReference(partition, reference);
	if (error != B_OK)
		return error;

	// collect all descendants whose contents need to be moved
	fContentsToMoveCount = 0;
	error = _CollectContentsToMove(partition);
	if (error != B_OK)
		return B_OK;

	// create and init the job
	MoveJob* job = new(nothrow) MoveJob(parentReference, reference);
	if (!job)
		return B_NO_MEMORY;

	error = job->Init(partition->Offset(), fContentsToMove,
		fContentsToMoveCount);
	if (error != B_OK) {
		delete job;
		return error;
	}

	return _AddJob(job);
}


/**
 * @brief Creates and enqueues a SetStringJob to rename a child partition.
 *
 * @param parent    The parent partition that owns the child.
 * @param partition The child partition whose name is to be changed.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DiskDeviceJobGenerator::_GenerateSetNameJob(BPartition* parent,
	BPartition* partition)
{
	PartitionReference* parentReference;
	status_t error = _GetPartitionReference(parent, parentReference);
	if (error != B_OK)
		return error;

	PartitionReference* reference;
	error = _GetPartitionReference(partition, reference);
	if (error != B_OK)
		return error;

	SetStringJob* job = new(nothrow) SetStringJob(parentReference, reference);
	if (!job)
		return B_NO_MEMORY;

	error = job->Init(partition->Name(), B_DISK_DEVICE_JOB_SET_NAME);
	if (error != B_OK) {
		delete job;
		return error;
	}

	return _AddJob(job);
}


/**
 * @brief Creates and enqueues a SetStringJob to change a child partition's type.
 *
 * @param parent    The parent partition that owns the child.
 * @param partition The child partition whose type is to be changed.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DiskDeviceJobGenerator::_GenerateSetTypeJob(BPartition* parent,
	BPartition* partition)
{
	PartitionReference* parentReference;
	status_t error = _GetPartitionReference(parent, parentReference);
	if (error != B_OK)
		return error;

	PartitionReference* reference;
	error = _GetPartitionReference(partition, reference);
	if (error != B_OK)
		return error;

	SetStringJob* job = new(nothrow) SetStringJob(parentReference, reference);
	if (!job)
		return B_NO_MEMORY;

	error = job->Init(partition->Type(), B_DISK_DEVICE_JOB_SET_TYPE);
	if (error != B_OK) {
		delete job;
		return error;
	}

	return _AddJob(job);
}


/**
 * @brief Creates and enqueues a SetStringJob to update a child partition's parameters.
 *
 * @param parent    The parent partition that owns the child.
 * @param partition The child partition whose parameters are to be changed.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DiskDeviceJobGenerator::_GenerateSetParametersJob(BPartition* parent,
	BPartition* partition)
{
	PartitionReference* parentReference;
	status_t error = _GetPartitionReference(parent, parentReference);
	if (error != B_OK)
		return error;

	PartitionReference* reference;
	error = _GetPartitionReference(partition, reference);
	if (error != B_OK)
		return error;

	SetStringJob* job = new(nothrow) SetStringJob(parentReference, reference);
	if (!job)
		return B_NO_MEMORY;

	error = job->Init(partition->Parameters(),
		B_DISK_DEVICE_JOB_SET_PARAMETERS);
	if (error != B_OK) {
		delete job;
		return error;
	}

	return _AddJob(job);
}


/**
 * @brief Recursively collects partitions with content that must be moved.
 *
 * Adds the partition itself if it has an active, non-reinitializing content
 * type, then recurses into children. Returns B_ERROR if a partition with
 * unrecognised contents is encountered.
 *
 * @param partition The root of the subtree to inspect.
 * @return B_OK on success, B_ERROR if an unrecognised partition is found.
 */
status_t
DiskDeviceJobGenerator::_CollectContentsToMove(BPartition* partition)
{
	BMutablePartition* shadow = _GetMutablePartition(partition);
	if (shadow->Status() == B_PARTITION_UNRECOGNIZED)
		return B_ERROR;

	// if the partition has contents, push its ID
	if (shadow->ContentType()
		&& !(shadow->ChangeFlags() & B_PARTITION_CHANGED_INITIALIZATION)) {
		status_t error = _PushContentsToMove(partition);
		if (error != B_OK)
			return error;
	}

	// recurse
	for (int32 i = 0; BPartition* child = partition->ChildAt(i); i++) {
		status_t error = _CollectContentsToMove(child);
		if (error != B_OK)
			return error;
	}
	return B_OK;
}


/**
 * @brief Appends a partition reference to the contents-to-move array.
 *
 * @param partition The partition whose reference should be pushed.
 * @return B_OK on success, B_ERROR if the array is full.
 */
status_t
DiskDeviceJobGenerator::_PushContentsToMove(BPartition* partition)
{
	if (fContentsToMoveCount >= fPartitionCount)
		return B_ERROR;

	PartitionReference* reference;
	status_t error = _GetPartitionReference(partition, reference);
	if (error != B_OK)
		return error;

	fContentsToMove[fContentsToMoveCount++] = reference;

	return B_OK;
}


/**
 * @brief Looks up or creates a PartitionReference for the given BPartition.
 *
 * The reference cache is scanned linearly; on a miss a new entry is created
 * and initialised from the partition's live data (if present).
 *
 * @param partition The partition for which a reference is needed.
 * @param reference Set to the found or newly created PartitionReference.
 * @return B_OK on success, B_BAD_VALUE if \a partition is NULL,
 *         B_NO_MEMORY if a new reference cannot be allocated, or B_ERROR
 *         if the internal cache is full.
 */
status_t
DiskDeviceJobGenerator::_GetPartitionReference(BPartition* partition,
	PartitionReference*& reference)
{
	if (!partition)
		return B_BAD_VALUE;

	for (int32 i = 0; i < fPartitionCount; i++) {
		PartitionRefInfo& info = fPartitionRefs[i];

		if (info.partition == partition) {
			reference = info.reference;
			return B_OK;
		}

		if (info.partition == NULL) {
			// create partition reference
			info.reference = new(nothrow) PartitionReference();
			if (!info.reference)
				return B_NO_MEMORY;

			// set partition ID and change counter
			user_partition_data* partitionData = partition->fPartitionData;
			if (partitionData) {
				info.reference->SetPartitionID(partitionData->id);
				info.reference->SetChangeCounter(partitionData->change_counter);
			}

			info.partition = partition;
			reference = info.reference;
			return B_OK;
		}
	}

	// Out of slots -- that can't happen.
	return B_ERROR;
}


/**
 * @brief qsort comparator that orders MoveInfo entries by current position.
 *
 * @param _a Pointer to the first MoveInfo.
 * @param _b Pointer to the second MoveInfo.
 * @return Negative, zero, or positive according to ascending position order.
 */
int
DiskDeviceJobGenerator::_CompareMoveInfoPosition(const void* _a, const void* _b)
{
	const MoveInfo* a = static_cast<const MoveInfo*>(_a);
	const MoveInfo* b = static_cast<const MoveInfo*>(_b);
	if (a->position < b->position)
		return -1;
	if (a->position > b->position)
		return 1;
	return 0;
}
