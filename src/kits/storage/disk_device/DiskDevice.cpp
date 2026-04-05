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
 * @file DiskDevice.cpp
 * @brief Implementation of the BDiskDevice class representing a storage device.
 *
 * BDiskDevice extends BPartition to represent a complete physical or virtual
 * storage device known to the disk device manager. It provides methods to
 * query device properties such as media presence, removability, and read-only
 * status, as well as operations for ejecting media, updating device state,
 * and managing the modification lifecycle (PrepareModifications,
 * CommitModifications, CancelModifications).
 *
 * @see BPartition
 * @see BDiskDeviceRoster
 */

#include <DiskDevice.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <DiskDevice.h>
#include <DiskDeviceVisitor.h>
#include <Drivers.h>
#include <Message.h>
#include <Path.h>

#include <syscalls.h>
#include <ddm_userland_interface_defs.h>

#include "DiskDeviceJob.h"
#include "DiskDeviceJobGenerator.h"
#include "DiskDeviceJobQueue.h"
#include <DiskSystemAddOnManager.h>


//#define TRACE_DISK_DEVICE
#undef TRACE
#ifdef TRACE_DISK_DEVICE
# define TRACE(x...) printf(x)
#else
# define TRACE(x...) do {} while (false)
#endif


/**
 * @brief A BDiskDevice object represents a storage device.
 */


/**
 * @brief Creates an uninitialized BDiskDevice object.
 */
BDiskDevice::BDiskDevice()
	:
	fDeviceData(NULL)
{
}


/**
 * @brief Frees all resources associated with this object.
 */
BDiskDevice::~BDiskDevice()
{
	CancelModifications();
}


/**
 * @brief Returns whether the device contains media.
 *
 * @return \c true if the device contains media, \c false otherwise.
 */
bool
BDiskDevice::HasMedia() const
{
	return fDeviceData
		&& (fDeviceData->device_flags & B_DISK_DEVICE_HAS_MEDIA) != 0;
}


/**
 * @brief Returns whether the device media is removable.
 *
 * @return \c true if the device media is removable, \c false otherwise.
 */
bool
BDiskDevice::IsRemovableMedia() const
{
	return fDeviceData
		&& (fDeviceData->device_flags & B_DISK_DEVICE_REMOVABLE) != 0;
}


/**
 * @brief Returns whether the device media is read-only.
 *
 * @return \c true if the device media is read-only, \c false otherwise.
 */
bool
BDiskDevice::IsReadOnlyMedia() const
{
	return fDeviceData
		&& (fDeviceData->device_flags & B_DISK_DEVICE_READ_ONLY) != 0;
}


/**
 * @brief Returns whether the device media is write-once.
 *
 * @return \c true if the device media is write-once, \c false otherwise.
 */
bool
BDiskDevice::IsWriteOnceMedia() const
{
	return fDeviceData
		&& (fDeviceData->device_flags & B_DISK_DEVICE_WRITE_ONCE) != 0;
}


/**
 * @brief Ejects the device's media.
 *
 * The device media must be removable, and the device must support ejecting
 * the media.
 *
 * @param update If \c true, Update() is invoked after successful ejection.
 * @return \c B_OK on success, \c B_NO_INIT if the device is not properly
 *         initialized, \c B_BAD_VALUE if the device media is not removable,
 *         or another error code on failure.
 */
status_t
BDiskDevice::Eject(bool update)
{
	if (fDeviceData == NULL)
		return B_NO_INIT;

	// check whether the device media is removable
	if (!IsRemovableMedia())
		return B_BAD_VALUE;

	// open, eject and close the device
	int fd = open(fDeviceData->path, O_RDONLY);
	if (fd < 0)
		return errno;

	status_t status = B_OK;
	if (ioctl(fd, B_EJECT_DEVICE) != 0)
		status = errno;

	close(fd);

	if (status == B_OK && update)
		status = Update();

	return status;
}


/**
 * @brief Initializes this object to represent the device with the given ID.
 *
 * @param id The partition ID of the device to set this object to.
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BDiskDevice::SetTo(partition_id id)
{
	return _SetTo(id, true, 0);
}


/**
 * @brief Updates the object to reflect the latest changes to the device.
 *
 * Note that subobjects (BPartitions) may be deleted during this operation.
 * It is also possible that the device no longer exists (e.g. hot-pluggable
 * device was removed), in which case an error is returned and the object is
 * uninitialized.
 *
 * @param updated Pointer to a bool variable set to \c true if the object
 *        needed to be updated and to \c false otherwise. May be \c NULL.
 *        Not touched if the method fails.
 * @return \c B_OK if the update went fine, another error code otherwise.
 */
status_t
BDiskDevice::Update(bool* updated)
{
	if (InitCheck() != B_OK)
		return InitCheck();

	// get the device data
	user_disk_device_data* data = NULL;
	status_t error = _GetData(ID(), true, 0, &data);

	// set the data
	if (error == B_OK)
		error = _Update(data, updated);

	// cleanup on error
	if (error != B_OK && data)
		free(data);

	return error;
}


/**
 * @brief Resets the object to an uninitialized state and frees device data.
 */
void
BDiskDevice::Unset()
{
	BPartition::_Unset();
	free(fDeviceData);
	fDeviceData = NULL;
}


/**
 * @brief Returns the initialization status of this object.
 *
 * @return \c B_OK if the object is properly initialized, \c B_NO_INIT otherwise.
 */
status_t
BDiskDevice::InitCheck() const
{
	return fDeviceData ? B_OK : B_NO_INIT;
}


/**
 * @brief Retrieves the path to the device node.
 *
 * @param path Pointer to a BPath to be set to the device path.
 * @return \c B_OK on success, \c B_BAD_VALUE if \a path is \c NULL or the
 *         device is uninitialized.
 */
status_t
BDiskDevice::GetPath(BPath* path) const
{
	if (!path || !fDeviceData)
		return B_BAD_VALUE;
	return path->SetTo(fDeviceData->path);
}


/**
 * @brief Returns whether any partition in the hierarchy has pending modifications.
 *
 * @return \c true if one or more partitions have been modified since the last
 *         commit or cancel, \c false otherwise.
 */
bool
BDiskDevice::IsModified() const
{
	if (InitCheck() != B_OK)
		return false;

	struct IsModifiedVisitor : public BDiskDeviceVisitor {
		virtual bool Visit(BDiskDevice* device)
		{
			return Visit(device, 0);
		}

		virtual bool Visit(BPartition* partition, int32 level)
		{
			return partition->_IsModified();
		}
	} visitor;

	return (VisitEachDescendant(&visitor) != NULL);
}


/**
 * @brief Initializes the partition hierarchy for modifications.
 *
 * Subsequent modifications are performed on a shadow structure and not
 * written to the device until CommitModifications() is called.
 *
 * @note This call locks the device. You must call CommitModifications() or
 *       CancelModifications() to unlock it.
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BDiskDevice::PrepareModifications()
{
	TRACE("%p->BDiskDevice::PrepareModifications()\n", this);

	// check initialization
	status_t error = InitCheck();
	if (error != B_OK) {
		TRACE("  InitCheck() failed\n");
		return error;
	}
	if (fDelegate) {
		TRACE("  already prepared!\n");
		return B_BAD_VALUE;
	}

	// make sure the disk system add-ons are loaded
	error = DiskSystemAddOnManager::Default()->LoadDiskSystems();
	if (error != B_OK) {
		TRACE("  failed to load disk systems\n");
		return error;
	}

	// recursively create the delegates
	error = _CreateDelegates();

	// init them
	if (error == B_OK)
		error = _InitDelegates();
	else
		TRACE("  failed to create delegates\n");

	// delete all of them, if something went wrong
	if (error != B_OK) {
		TRACE("  failed to init delegates\n");
		_DeleteDelegates();
		DiskSystemAddOnManager::Default()->UnloadDiskSystems();
	}

	return error;
}


/**
 * @brief Commits modifications to the device.
 *
 * Creates a set of jobs that perform all changes made after
 * PrepareModifications(). Changes are written to the device and all
 * BPartition children are deleted and recreated to reflect the new state.
 * Cached pointers to BPartition objects must not be used after this call.
 * Unlocks the device for further use.
 *
 * @param synchronously Reserved for future use.
 * @param progressMessenger Reserved for future use.
 * @param receiveCompleteProgressUpdates Reserved for future use.
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BDiskDevice::CommitModifications(bool synchronously,
	BMessenger progressMessenger, bool receiveCompleteProgressUpdates)
{
// TODO: Support parameters!
	status_t error = InitCheck();
	if (error != B_OK)
		return error;

	if (!fDelegate)
		return B_BAD_VALUE;

	// generate jobs
	DiskDeviceJobQueue jobQueue;
	error = DiskDeviceJobGenerator(this, &jobQueue).GenerateJobs();

	// do the jobs
	if (error == B_OK)
		error = jobQueue.Execute();

	_DeleteDelegates();
	DiskSystemAddOnManager::Default()->UnloadDiskSystems();

	if (error == B_OK)
		error = _SetTo(ID(), true, 0);

	return error;
}


/**
 * @brief Cancels all modifications performed on the device.
 *
 * Nothing is written to the device and it is unlocked for further use.
 *
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BDiskDevice::CancelModifications()
{
	status_t error = InitCheck();
	if (error != B_OK)
		return error;

	if (!fDelegate)
		return B_BAD_VALUE;

	_DeleteDelegates();
	DiskSystemAddOnManager::Default()->UnloadDiskSystems();

	if (error == B_OK)
		error = _SetTo(ID(), true, 0);

	return error;
}


/**
 * @brief Returns whether this device is a virtual device backed by a file.
 *
 * @return \c true if the device is backed by a file, \c false otherwise.
 */
bool
BDiskDevice::IsFile() const
{
	return fDeviceData
		&& (fDeviceData->device_flags & B_DISK_DEVICE_IS_FILE) != 0;
}


/**
 * @brief Retrieves the path of the file backing this virtual disk device.
 *
 * @param path Pointer to a BPath to be set to the backing file path.
 * @return \c B_OK on success, \c B_BAD_VALUE if \a path is \c NULL,
 *         \c B_BAD_TYPE if the device is not file-backed, or another error code.
 */
status_t
BDiskDevice::GetFilePath(BPath* path) const
{
	if (path == NULL)
		return B_BAD_VALUE;
	if (!IsFile())
		return B_BAD_TYPE;

	char pathBuffer[B_PATH_NAME_LENGTH];
	status_t status = _kern_get_file_disk_device_path(
		fDeviceData->device_partition_data.id, pathBuffer, sizeof(pathBuffer));
	if (status != B_OK)
		return status;

	return path->SetTo(pathBuffer);
}


/**
 * @brief Privatized copy constructor to avoid usage.
 */
BDiskDevice::BDiskDevice(const BDiskDevice&)
{
}


/**
 * @brief Privatized assignment operator to avoid usage.
 */
BDiskDevice&
BDiskDevice::operator=(const BDiskDevice&)
{
	return *this;
}


/**
 * @brief Fetches the raw device data from the kernel for a given partition ID.
 *
 * Allocates a buffer of sufficient size and fills it via a kernel call,
 * retrying with a larger buffer if B_BUFFER_OVERFLOW is returned.
 *
 * @param id The partition ID of the device to retrieve.
 * @param deviceOnly If \c true, retrieve only device-level data.
 * @param neededSize Hint for the initial buffer size (0 to let the call decide).
 * @param data Out-parameter set to the allocated device data on success.
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BDiskDevice::_GetData(partition_id id, bool deviceOnly, size_t neededSize,
	user_disk_device_data** data)
{
	// get the device data
	void* buffer = NULL;
	size_t bufferSize = 0;
	if (neededSize > 0) {
		// allocate initial buffer
		buffer = malloc(neededSize);
		if (!buffer)
			return B_NO_MEMORY;
		bufferSize = neededSize;
	}

	status_t error = B_OK;
	do {
		error = _kern_get_disk_device_data(id, deviceOnly,
			(user_disk_device_data*)buffer, bufferSize, &neededSize);
		if (error == B_BUFFER_OVERFLOW) {
			// buffer to small re-allocate it
			free(buffer);

			buffer = malloc(neededSize);

			if (buffer)
				bufferSize = neededSize;
			else
				error = B_NO_MEMORY;
		}
	} while (error == B_BUFFER_OVERFLOW);

	// set result / cleanup on error
	if (error == B_OK)
		*data = (user_disk_device_data*)buffer;
	else
		free(buffer);

	return error;
}


/**
 * @brief Initializes this object by fetching device data by partition ID.
 *
 * Clears the current state, then fetches and applies new device data.
 *
 * @param id The partition ID of the device to set this object to.
 * @param deviceOnly If \c true, only device-level data is fetched.
 * @param neededSize Hint for the initial buffer size.
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BDiskDevice::_SetTo(partition_id id, bool deviceOnly, size_t neededSize)
{
	Unset();

	// get the device data
	user_disk_device_data* data = NULL;
	status_t error = _GetData(id, deviceOnly, neededSize, &data);

	// set the data
	if (error == B_OK)
		error = _SetTo(data);

	// cleanup on error
	if (error != B_OK && data)
		free(data);

	return error;
}


/**
 * @brief Initializes this object from a pre-fetched user_disk_device_data buffer.
 *
 * Takes ownership of \a data on success. On failure the caller retains
 * ownership and must free it.
 *
 * @param data Pointer to the device data structure to consume.
 * @return \c B_OK on success, \c B_BAD_VALUE if \a data is \c NULL, or
 *         another error code on failure.
 */
status_t
BDiskDevice::_SetTo(user_disk_device_data* data)
{
	Unset();

	if (!data)
		return B_BAD_VALUE;

	fDeviceData = data;

	status_t error = BPartition::_SetTo(this, NULL,
		&fDeviceData->device_partition_data);
	if (error != B_OK) {
		// If _SetTo() fails, the caller retains ownership of the supplied
		// data. So, unset fDeviceData before calling Unset().
		fDeviceData = NULL;
		Unset();
	}

	return error;
}


/**
 * @brief Updates this object's internal state from newly fetched device data.
 *
 * Removes obsolete descendant partitions, updates existing ones, and adds
 * new ones. Frees the old device data on success.
 *
 * @param data The newly fetched device data.
 * @param updated Set to \c true if any changes were detected.
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BDiskDevice::_Update(user_disk_device_data* data, bool* updated)
{
	if (!data || !fDeviceData || ID() != data->device_partition_data.id)
		return B_BAD_VALUE;
	bool _updated;
	if (!updated)
		updated = &_updated;
	*updated = false;

	// clear the user_data fields first
	_ClearUserData(&data->device_partition_data);

	// remove obsolete partitions
	status_t error = _RemoveObsoleteDescendants(&data->device_partition_data,
		updated);
	if (error != B_OK)
		return error;

	// update existing partitions and add new ones
	error = BPartition::_Update(&data->device_partition_data, updated);
	if (error == B_OK) {
		user_disk_device_data* oldData = fDeviceData;
		fDeviceData = data;
		// check for changes
		if (data->device_flags != oldData->device_flags
			|| strcmp(data->path, oldData->path)) {
			*updated = true;
		}
		free(oldData);
	}

	return error;
}


/**
 * @brief Dispatches the visitor to this device's Visit(BDiskDevice*) overload.
 *
 * @param visitor The visitor to invoke.
 * @param level The depth level in the device/partition hierarchy (unused here).
 * @return The return value of visitor->Visit(this).
 */
bool
BDiskDevice::_AcceptVisitor(BDiskDeviceVisitor* visitor, int32 level)
{
	return visitor->Visit(this);
}


/**
 * @brief Recursively clears the user_data pointer in a partition data tree.
 *
 * Used before updating the device data so that stale BPartition pointers
 * stored in user_data fields are removed prior to repopulation.
 *
 * @param data The root of the partition data tree to clear.
 */
void
BDiskDevice::_ClearUserData(user_partition_data* data)
{
	data->user_data = NULL;

	// recurse
	for (int i = 0; i < data->child_count; i++)
		_ClearUserData(data->children[i]);
}
