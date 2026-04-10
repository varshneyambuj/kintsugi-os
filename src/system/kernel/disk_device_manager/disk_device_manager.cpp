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
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2003-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file disk_device_manager.cpp
 * @brief Kernel disk device manager initialization and C API entry points.
 *
 * Implements the flat C interface used by disk system add-ons and other kernel
 * consumers to lock, query, and manipulate disk devices and their partitions
 * through the KDiskDeviceManager singleton.
 */


#include "disk_device_manager.h"

#include <stdio.h>

#include <KernelExport.h>

#include "KDiskDevice.h"
#include "KDiskDeviceManager.h"
#include "KDiskDeviceUtils.h"
#include "KDiskSystem.h"
#include "KPartition.h"


// debugging
//#define DBG(x)
#define DBG(x) x
#define OUT dprintf


/**
 * @brief Registers the device containing @a partitionID and acquires its write lock.
 *
 * The caller must release the lock with write_unlock_disk_device() passing the
 * same @a partitionID.  The device remains registered (reference count elevated)
 * until the lock is released.
 *
 * @param partitionID ID of any partition on the target device.
 * @return Pointer to the device's disk_device_data on success, or NULL if the
 *         device could not be found or the write lock could not be acquired.
 */
disk_device_data*
write_lock_disk_device(partition_id partitionID)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	if (KDiskDevice* device = manager->RegisterDevice(partitionID, false)) {
		if (device->WriteLock())
			return device->DeviceData();
		// Only unregister, when the locking fails. The guarantees, that the
		// lock owner also has a reference.
		device->Unregister();
	}
	return NULL;
}


/**
 * @brief Releases the write lock on the device identified by @a partitionID.
 *
 * Must be called once for every successful write_lock_disk_device() call on
 * the same @a partitionID.
 *
 * @param partitionID ID of any partition on the previously locked device.
 */
void
write_unlock_disk_device(partition_id partitionID)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	if (KDiskDevice* device = manager->RegisterDevice(partitionID, false)) {
		device->WriteUnlock();
		device->Unregister();

		device->Unregister();
	}
}


/**
 * @brief Registers the device containing @a partitionID and acquires its read lock.
 *
 * Multiple concurrent readers are permitted.  The caller must release the lock
 * with read_unlock_disk_device().
 *
 * @param partitionID ID of any partition on the target device.
 * @return Pointer to the device's disk_device_data on success, or NULL if the
 *         device could not be found or the read lock could not be acquired.
 */
disk_device_data*
read_lock_disk_device(partition_id partitionID)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	if (KDiskDevice* device = manager->RegisterDevice(partitionID, false)) {
		if (device->ReadLock())
			return device->DeviceData();
		// Only unregister, when the locking fails. The guarantees, that the
		// lock owner also has a reference.
		device->Unregister();
	}
	return NULL;
}


/**
 * @brief Releases the read lock on the device identified by @a partitionID.
 *
 * Must be called once for every successful read_lock_disk_device() call on
 * the same @a partitionID.
 *
 * @param partitionID ID of any partition on the previously locked device.
 */
void
read_unlock_disk_device(partition_id partitionID)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	if (KDiskDevice* device = manager->RegisterDevice(partitionID, false)) {
		device->ReadUnlock();
		device->Unregister();

		device->Unregister();
	}
}


/**
 * @brief Looks up the device whose node path matches @a path and returns its ID.
 *
 * @param path Absolute path to the device node (e.g. "/dev/disk/...").
 * @return The partition_id of the matching device, or -1 if not found.
 */
int32
find_disk_device(const char* path)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	partition_id id = -1;
	if (KDiskDevice* device = manager->RegisterDevice(path)) {
		id = device->ID();
		device->Unregister();
	}
	return id;
}


/**
 * @brief Looks up the partition whose node path matches @a path and returns its ID.
 *
 * @param path Absolute path to the partition node.
 * @return The partition_id of the matching partition, or -1 if not found.
 */
int32
find_partition(const char* path)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	partition_id id = -1;
	if (KPartition* partition = manager->RegisterPartition(path)) {
		id = partition->ID();
		partition->Unregister();
	}
	return id;
}


/**
 * @brief Returns a pointer to the disk_device_data for the device that owns
 *        @a partitionID without acquiring any lock.
 *
 * The pointer is only valid while the caller holds an appropriate lock on the
 * device.
 *
 * @param partitionID ID of any partition on the target device.
 * @return Pointer to disk_device_data, or NULL if not found.
 */
disk_device_data*
get_disk_device(partition_id partitionID)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KDiskDevice* device = manager->FindDevice(partitionID, false);
	return (device ? device->DeviceData() : NULL);
}


/**
 * @brief Returns a pointer to the partition_data for the partition identified
 *        by @a partitionID without acquiring any lock.
 *
 * @param partitionID Identifier of the requested partition.
 * @return Pointer to partition_data, or NULL if not found.
 */
partition_data*
get_partition(partition_id partitionID)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->FindPartition(partitionID);
	return (partition ? partition->PartitionData() : NULL);
}


/**
 * @brief Returns the partition_data of the parent of the partition identified
 *        by @a partitionID.
 *
 * @param partitionID Identifier of the child partition.
 * @return Pointer to the parent's partition_data, or NULL if the partition has
 *         no parent or was not found.
 */
partition_data*
get_parent_partition(partition_id partitionID)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->FindPartition(partitionID);
	if (partition && partition->Parent())
		return partition->Parent()->PartitionData();
	return NULL;
}


/**
 * @brief Returns the partition_data of the child at position @a index within
 *        the partition identified by @a partitionID.
 *
 * @param partitionID Identifier of the parent partition.
 * @param index       Zero-based child index.
 * @return Pointer to the child's partition_data, or NULL if not found.
 */
partition_data*
get_child_partition(partition_id partitionID, int32 index)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	if (KPartition* partition = manager->FindPartition(partitionID)) {
		if (KPartition* child = partition->ChildAt(index))
			return child->PartitionData();
	}
	return NULL;
}


/**
 * @brief Opens the partition identified by @a partitionID with the given mode.
 *
 * @param partitionID Identifier of the partition to open.
 * @param openMode    Open flags (O_RDONLY, O_RDWR, etc.).
 * @return A valid file descriptor on success, -1 on failure (including
 *         B_BAD_VALUE if the partition was not found).
 */
int
open_partition(partition_id partitionID, int openMode)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->FindPartition(partitionID);
	if (partition == NULL)
		return B_BAD_VALUE;

	int fd = -1;
	status_t result = partition->Open(openMode, &fd);
	if (result != B_OK)
		return -1;

	return fd;
}


/**
 * @brief Creates a new child partition within the partition identified by
 *        @a partitionID.
 *
 * @param partitionID Identifier of the parent partition.
 * @param index       Desired child index.
 * @param offset      Byte offset of the new partition within the parent.
 * @param size        Size of the new partition in bytes.
 * @param childID     Requested partition_id for the new child, or -1 for auto.
 * @return Pointer to the new child's partition_data on success, or NULL on
 *         failure.
 */
partition_data*
create_child_partition(partition_id partitionID, int32 index, off_t offset,
	off_t size, partition_id childID)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	if (KPartition* partition = manager->FindPartition(partitionID)) {
		KPartition* child = NULL;
		if (partition->CreateChild(childID, index, offset, size, &child)
				== B_OK) {
			return child->PartitionData();
		} else {
			DBG(OUT("  creating child (%" B_PRId32 ", %" B_PRId32 ") failed\n",
				partitionID, index));
		}
	} else
		DBG(OUT("  partition %" B_PRId32 " not found\n", partitionID));

	return NULL;
}


/**
 * @brief Removes and destroys the partition identified by @a partitionID from
 *        its parent.
 *
 * @param partitionID Identifier of the partition to delete.
 * @return true if the partition was successfully removed, false otherwise.
 */
bool
delete_partition(partition_id partitionID)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	if (KPartition* partition = manager->FindPartition(partitionID)) {
		if (KPartition* parent = partition->Parent())
			return parent->RemoveChild(partition);
	}
	return false;
}


/**
 * @brief Notifies the disk device manager that @a partitionID has been modified.
 *
 * Currently a placeholder; no action is taken.
 *
 * @param partitionID Identifier of the modified partition.
 */
void
partition_modified(partition_id partitionID)
{
	// TODO: implemented
}


/**
 * @brief Triggers an asynchronous scan of the partition identified by
 *        @a partitionID to detect its disk system and child partitions.
 *
 * The partition is registered (reference count incremented) for the duration
 * of the scan and released when the scan completes.
 *
 * @param partitionID Identifier of the partition to scan.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if the partition does not exist,
 *         or another error code on scan failure.
 */
status_t
scan_partition(partition_id partitionID)
{
	// get the partition
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->RegisterPartition(partitionID);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;
	PartitionRegistrar _(partition, true);

	// scan it
	return manager->ScanPartition(partition);
}


/**
 * @brief Generates a human-readable default content name for a partition.
 *
 * The name follows the pattern "<FileSystem> Volume (X.Y ZB)" where ZB is the
 * appropriate binary size suffix.  The kernel snprintf() precision workaround
 * computes one decimal digit manually.
 *
 * @param partitionID     Identifier of the partition whose size is used.
 * @param fileSystemName  Display name of the file system (e.g. "BFS").
 * @param buffer          Destination buffer for the generated name.
 * @param bufferSize      Size of @a buffer in bytes.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if the partition does not exist.
 */
status_t
get_default_partition_content_name(partition_id partitionID,
	const char* fileSystemName, char* buffer, size_t bufferSize)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->RegisterPartition(partitionID);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;

	double size = partition->ContentSize();
	partition->Unregister();

	const char* const suffixes[] = {
		"", "K", "M", "G", "T", "P", "E", NULL
	};

	int index = 0;
	while (size >= 1024 && suffixes[index + 1]) {
		size /= 1024;
		index++;
	}

	// Our kernel snprintf() ignores the precision argument, so we manually
	// do one digit precision.
	uint64 result = uint64(size * 10 + 0.5);

	snprintf(buffer, bufferSize, "%s Volume (%" B_PRId32 ".%" B_PRId32 " %sB)",
		fileSystemName, int32(result / 10), int32(result % 10), suffixes[index]);

	return B_OK;
}


/**
 * @brief Looks up a disk system by name and returns its identifier.
 *
 * The manager lock is held for the duration of the lookup.
 *
 * @param name Unique name of the disk system (e.g. "Intel Partition Map").
 * @return The disk_system_id on success, or -1 if not found.
 */
disk_system_id
find_disk_system(const char* name)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	if (ManagerLocker locker = manager) {
		if (KDiskSystem* diskSystem = manager->FindDiskSystem(name))
			return diskSystem->ID();
	}
	return -1;
}


/**
 * @brief Updates the progress value for a disk device job.
 *
 * Currently disabled (guarded by @c #if 0).  Always returns false.
 *
 * @param jobID    Identifier of the running job.
 * @param progress Progress value in the range [0.0, 1.0].
 * @return false (not yet implemented).
 */
bool
update_disk_device_job_progress(disk_job_id jobID, float progress)
{
#if 0
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	if (ManagerLocker locker = manager) {
		if (KDiskDeviceJob* job = manager->FindJob(jobID)) {
			job->UpdateProgress(progress);
			return true;
		}
	}
#endif
	return false;
}


/**
 * @brief Updates the extra progress string for a disk device job.
 *
 * Currently disabled (guarded by @c #if 0).  Always returns false.
 *
 * @param jobID Identifier of the running job.
 * @param info  Human-readable status string.
 * @return false (not yet implemented).
 */
bool
update_disk_device_job_extra_progress(disk_job_id jobID, const char* info)
{
#if 0
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	if (ManagerLocker locker = manager) {
		if (KDiskDeviceJob* job = manager->FindJob(jobID)) {
			job->UpdateExtraProgress(info);
			return true;
		}
	}
#endif
	return false;
}


/**
 * @brief Sets a human-readable error message on a disk device job.
 *
 * Currently disabled (guarded by @c #if 0).  Always returns false.
 *
 * @param jobID   Identifier of the running job.
 * @param message Error description string.
 * @return false (not yet implemented).
 */
bool
set_disk_device_job_error_message(disk_job_id jobID, const char* message)
{
#if 0
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	if (ManagerLocker locker = manager) {
		if (KDiskDeviceJob* job = manager->FindJob(jobID)) {
			job->SetErrorMessage(message);
			return true;
		}
	}
#endif
	return false;
}


/**
 * @brief Updates the interrupt properties for a disk device job and handles
 *        pause/cancel requests.
 *
 * Currently disabled (guarded by @c #if 0).  Always returns
 * B_DISK_DEVICE_JOB_CONTINUE.
 *
 * @param jobID               Identifier of the running job.
 * @param interruptProperties New interrupt property bitmask.
 * @return B_DISK_DEVICE_JOB_CONTINUE (not yet implemented).
 */
uint32
update_disk_device_job_interrupt_properties(disk_job_id jobID,
	uint32 interruptProperties)
{
#if 0
	bool paused = false;
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	do {
		sem_id pauseSemaphore = -1;
		if (ManagerLocker locker = manager) {
			// get the job and the respective job queue
			if (KDiskDeviceJob* job = manager->FindJob(jobID)) {
				if (KDiskDeviceJobQueue* jobQueue = job->JobQueue()) {
					// terminate if canceled.
					if (jobQueue->IsCanceled()) {
						if (jobQueue->ShallReverse())
							return B_DISK_DEVICE_JOB_REVERSE;
						return B_DISK_DEVICE_JOB_CANCEL;
					}
					// set the new interrupt properties only when not
					// requested to pause
					if (jobQueue->IsPauseRequested())
						pauseSemaphore = jobQueue->ReadyToPause();
					else
						job->SetInterruptProperties(interruptProperties);
				}
			}
		}
		// pause, if requested; redo the loop then
		paused = (pauseSemaphore >= 0);
		if (paused) {
			acquire_sem(pauseSemaphore);
			pauseSemaphore = -1;
		}
	} while (paused);
#endif
	return B_DISK_DEVICE_JOB_CONTINUE;
}
