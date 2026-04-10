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
 *   Copyright 2006-2011, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2003-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file KDiskDevice.cpp
 * @brief Kernel object representing a physical or virtual disk device.
 *
 * KDiskDevice is the root of the partition tree for a single disk. It wraps
 * the underlying file descriptor or device node, provides the device path,
 * geometry, and read/write capabilities, and owns the top-level KPartition
 * that covers the entire device.
 *
 * @see KPartition.cpp, KDiskDeviceManager.cpp
 */

#include "KDiskDevice.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <KernelExport.h>
#include <Drivers.h>

#include "ddm_userland_interface.h"
#include "KDiskDeviceUtils.h"
#include "KPath.h"
#include "UserDataWriter.h"


// debugging
//#define DBG(x)
#define DBG(x) x
#define OUT dprintf


/**
 * @brief Constructs a KDiskDevice with the given partition ID.
 *
 * Initialises the read/write lock, resets device state, sets the device
 * pointer to itself, and sets the published name to "raw".
 *
 * @param id The partition_id to assign to this disk device.
 */
KDiskDevice::KDiskDevice(partition_id id)
	:
	KPartition(id),
	fDeviceData(),
	fFD(-1),
	fMediaStatus(B_ERROR)
{
	rw_lock_init(&fLocker, "disk device");

	Unset();
	fDevice = this;
	fPublishedName = (char*)"raw";
}


/**
 * @brief Destroys the KDiskDevice, releasing all acquired resources.
 *
 * Calls Unset() to close the file descriptor and free the device path.
 */
KDiskDevice::~KDiskDevice()
{
	Unset();
}


/**
 * @brief Opens and initialises the disk device at the given path.
 *
 * Opens the device file read-only, queries its media status and geometry,
 * updates the device flags, and initialises the partition data.
 *
 * @param path Null-terminated path to the device node (e.g. "/dev/disk/...").
 * @return B_OK on success.
 * @retval B_BAD_VALUE  @a path is NULL.
 * @retval B_NO_MEMORY  Memory allocation for the path copy failed.
 * @retval errno        The open(2) system call failed.
 * @retval other        GetMediaStatus() or GetGeometry() reported an error.
 */
status_t
KDiskDevice::SetTo(const char* path)
{
	if (!path)
		return B_BAD_VALUE;
	Unset();

	status_t error = set_string(fDeviceData.path, path);
	if (error != B_OK)
		return error;

	// open the device
	fFD = open(path, O_RDONLY);
	if (fFD < 0)
		return errno;
	// get media status
	error = GetMediaStatus(&fMediaStatus);
	if (error != B_OK)
		return error;
	if (fMediaStatus == B_DEV_MEDIA_CHANGED)
		fMediaStatus = B_OK;

	// get device geometry
	if (fMediaStatus == B_OK) {
		error = GetGeometry(&fDeviceData.geometry);
		if (error != B_OK)
			return error;
	} else {
		// no media present: reset the geometry
		_ResetGeometry();
	}

	_UpdateDeviceFlags();
	_InitPartitionData();
	return B_OK;
}


/**
 * @brief Resets the device to an uninitialised state.
 *
 * Closes the open file descriptor if one is held, resets the media status,
 * clears the device ID and flags, frees the path string, and resets geometry.
 */
void
KDiskDevice::Unset()
{
	if (fFD >= 0) {
		close(fFD);
		fFD = -1;
	}
	fMediaStatus = B_ERROR;
	fDeviceData.id = -1;
	fDeviceData.flags = 0;
	if (fDeviceData.path) {
		free(fDeviceData.path);
		fDeviceData.path = NULL;
	}
	_ResetGeometry();
}


/**
 * @brief Acquires the device read lock.
 *
 * Multiple concurrent readers are allowed. Blocks until the lock is acquired.
 *
 * @return @c true if the lock was acquired successfully, @c false otherwise.
 */
bool
KDiskDevice::ReadLock()
{
	return rw_lock_read_lock(&fLocker) == B_OK;
}


/**
 * @brief Releases the device read lock.
 */
void
KDiskDevice::ReadUnlock()
{
	rw_lock_read_unlock(&fLocker);
}


/**
 * @brief Acquires the device write lock.
 *
 * Exclusive access; blocks until all readers and any existing writer have
 * released the lock.
 *
 * @return @c true if the lock was acquired successfully, @c false otherwise.
 */
bool
KDiskDevice::WriteLock()
{
	return rw_lock_write_lock(&fLocker) == B_OK;
}


/**
 * @brief Releases the device write lock.
 */
void
KDiskDevice::WriteUnlock()
{
	rw_lock_write_unlock(&fLocker);
}


/**
 * @brief Sets the partition/device ID, keeping the internal device data in sync.
 *
 * Forwards the call to KPartition::SetID() and also updates fDeviceData.id.
 *
 * @param id The new partition_id for this device.
 */
void
KDiskDevice::SetID(partition_id id)
{
	KPartition::SetID(id);
	fDeviceData.id = id;
}


/**
 * @brief No-op: disk devices are always published.
 *
 * @return B_OK always.
 */
status_t
KDiskDevice::PublishDevice()
{
	// PublishDevice(), UnpublishDevice() and Republish are no-ops
	// for KDiskDevices, since they are always published.
	return B_OK;
}


/**
 * @brief No-op: disk devices are always published.
 *
 * @return B_OK always.
 */
status_t
KDiskDevice::UnpublishDevice()
{
	// PublishDevice(), UnpublishDevice() and Republish are no-ops
	// for KDiskDevices, since they are always published.
	return B_OK;
}


/**
 * @brief No-op: disk devices are always published.
 *
 * @return B_OK always.
 */
status_t
KDiskDevice::RepublishDevice()
{
	// PublishDevice(), UnpublishDevice() and Republish are no-ops
	// for KDiskDevices, since they are always published.
	return B_OK;
}


/**
 * @brief Sets the raw device flags (B_DISK_DEVICE_REMOVABLE, etc.).
 *
 * @param flags Bitmask of device flags to store.
 */
void
KDiskDevice::SetDeviceFlags(uint32 flags)
{
	fDeviceData.flags = flags;
}


/**
 * @brief Returns the current device flags bitmask.
 *
 * @return The device flags stored in fDeviceData.flags.
 */
uint32
KDiskDevice::DeviceFlags() const
{
	return fDeviceData.flags;
}


/**
 * @brief Reports whether the media is read-only.
 *
 * @return @c true if the geometry marks the device as read-only.
 */
bool
KDiskDevice::IsReadOnlyMedia() const
{
	return fDeviceData.geometry.read_only;
}


/**
 * @brief Reports whether the media is write-once (e.g. CD-R).
 *
 * @return @c true if the geometry marks the device as write-once.
 */
bool
KDiskDevice::IsWriteOnce() const
{
	return fDeviceData.geometry.write_once;
}


/**
 * @brief Reports whether the media is removable (e.g. USB stick, CD).
 *
 * @return @c true if the geometry marks the device as removable.
 */
bool
KDiskDevice::IsRemovable() const
{
	return fDeviceData.geometry.removable;
}


/**
 * @brief Reports whether media is currently present in the drive.
 *
 * Considers both B_OK and B_DEV_MEDIA_CHANGED as "has media".
 *
 * @return @c true if media is present.
 */
bool
KDiskDevice::HasMedia() const
{
	return fMediaStatus == B_OK || fMediaStatus == B_DEV_MEDIA_CHANGED;
}


/**
 * @brief Reports whether the media has changed since the last check.
 *
 * @return @c true if fMediaStatus is B_DEV_MEDIA_CHANGED.
 */
bool
KDiskDevice::MediaChanged() const
{
	return fMediaStatus == B_DEV_MEDIA_CHANGED;
}


/**
 * @brief Re-queries the driver for the current media status.
 *
 * Intended to be called periodically; a future improvement would allow the
 * driver to push notifications instead of requiring polling.
 */
void
KDiskDevice::UpdateMediaStatusIfNeeded()
{
	// TODO: allow a device to notify us about its media status!
	// This will then also need to clear any B_DEV_MEDIA_CHANGED
	GetMediaStatus(&fMediaStatus);
}


/**
 * @brief Uninitialises the media contents and resets geometry and device flags.
 *
 * Called when the media is removed or becomes unavailable. Clears the
 * partition tree contents, resets the geometry to a blank state, updates
 * device flags to reflect the absent media, and re-initialises partition data.
 */
void
KDiskDevice::UninitializeMedia()
{
	UninitializeContents();
	_ResetGeometry();
	_UpdateDeviceFlags();
	_InitPartitionData();
}


/**
 * @brief Re-reads device geometry from the driver and updates internal state.
 *
 * If the ioctl fails the method returns immediately without modifying state.
 * On success, device flags and partition data are refreshed to match the
 * new geometry.
 */
void
KDiskDevice::UpdateGeometry()
{
	if (GetGeometry(&fDeviceData.geometry) != B_OK)
		return;

	_UpdateDeviceFlags();
	_InitPartitionData();
}


/**
 * @brief Returns the device path string (e.g. "/dev/disk/ata/0/master/raw").
 *
 * @return Pointer to the null-terminated device path, or NULL if not set.
 */
const char*
KDiskDevice::Path() const
{
	return fDeviceData.path;
}


/**
 * @brief Copies the device file name "raw" into the supplied buffer.
 *
 * The published name for a raw disk device is always "raw".
 *
 * @param buffer  Destination buffer to receive the name.
 * @param size    Size of @a buffer in bytes.
 * @return B_OK on success.
 * @retval B_NAME_TOO_LONG  The buffer is too small to hold "raw".
 */
status_t
KDiskDevice::GetFileName(char* buffer, size_t size) const
{
	if (strlcpy(buffer, "raw", size) >= size)
		return B_NAME_TOO_LONG;
	return B_OK;
}


/**
 * @brief Fills a KPath object with the full device path.
 *
 * @param path  Pointer to an initialised KPath to receive the path.
 * @return B_OK on success.
 * @retval B_BAD_VALUE  @a path is NULL or failed its own InitCheck().
 * @retval B_NO_INIT    SetTo() has not been called yet (path not set).
 * @retval other        KPath::SetPath() error.
 */
status_t
KDiskDevice::GetPath(KPath* path) const
{
	if (!path || path->InitCheck() != B_OK)
		return B_BAD_VALUE;
	if (!fDeviceData.path)
		return B_NO_INIT;
	return path->SetPath(fDeviceData.path);
}


/**
 * @brief Returns the open file descriptor for the device node.
 *
 * @return The file descriptor, or -1 if the device is not open.
 */
int
KDiskDevice::FD() const
{
	return fFD;
}


/**
 * @brief Returns a mutable pointer to the raw disk_device_data structure.
 *
 * @return Pointer to fDeviceData.
 */
disk_device_data*
KDiskDevice::DeviceData()
{
	return &fDeviceData;
}


/**
 * @brief Returns a read-only pointer to the raw disk_device_data structure.
 *
 * @return Const pointer to fDeviceData.
 */
const disk_device_data*
KDiskDevice::DeviceData() const
{
	return &fDeviceData;
}


/**
 * @brief Serialises the partition portion of this device to a UserDataWriter.
 *
 * Delegates directly to KPartition::WriteUserData().
 *
 * @param writer  The UserDataWriter accumulating the output buffer.
 * @param data    Pointer to the user_partition_data area to fill.
 */
void
KDiskDevice::WriteUserData(UserDataWriter& writer, user_partition_data* data)
{
	KPartition::WriteUserData(writer, data);
}


/**
 * @brief Serialises the full device (flags, path, and partition tree) to a
 *        UserDataWriter for later copying to user space.
 *
 * Allocates a user_disk_device_data header, places the path string, records
 * the relocation entry for the path pointer, then delegates the partition
 * tree serialisation to KPartition::WriteUserData().
 *
 * @param writer  The UserDataWriter accumulating the output buffer.
 */
void
KDiskDevice::WriteUserData(UserDataWriter& writer)
{
	KPartition* partition = this;
	user_disk_device_data* data
		= writer.AllocateDeviceData(partition->CountChildren());
	char* path = writer.PlaceString(Path());
	if (data != NULL) {
		data->device_flags = DeviceFlags();
		data->path = path;
		writer.AddRelocationEntry(&data->path);
		partition->WriteUserData(writer, &data->device_partition_data);
	} else
		partition->WriteUserData(writer, NULL);
}


/**
 * @brief Prints a human-readable dump of the device and its partition tree.
 *
 * Emits the device ID, path, media status, and device flags via dprintf.
 * If media is present, also dumps the full partition tree.
 *
 * @param deep   If @c true, recursively dump child partitions.
 * @param level  Indentation level (passed through to KPartition::Dump()).
 */
void
KDiskDevice::Dump(bool deep, int32 level)
{
	OUT("device %" B_PRId32 ": %s\n", ID(), Path());
	OUT("  media status:      %s\n", strerror(fMediaStatus));
	OUT("  device flags:      %" B_PRIx32 "\n", DeviceFlags());
	if (fMediaStatus == B_OK)
		KPartition::Dump(deep, 0);
}


/**
 * @brief Queries the driver for the current media status via ioctl.
 *
 * Falls back to querying the geometry: if the geometry ioctl succeeds and the
 * device is non-removable, reports B_OK even when B_GET_MEDIA_STATUS fails.
 *
 * @param mediaStatus  Output pointer; receives the media status code.
 * @return B_OK if the status could be determined.
 * @retval errno  The B_GET_MEDIA_STATUS ioctl failed and the fallback also
 *                failed or the device is removable.
 */
status_t
KDiskDevice::GetMediaStatus(status_t* mediaStatus)
{
	status_t error = B_OK;
	if (ioctl(fFD, B_GET_MEDIA_STATUS, mediaStatus, sizeof(*mediaStatus)) != 0)
		error = errno;
	// maybe the device driver doesn't implement this ioctl -- see, if getting
	// the device geometry succeeds
	if (error != B_OK) {
		device_geometry geometry;
		if (GetGeometry(&geometry) == B_OK) {
			// if the device is not removable, we can ignore the failed ioctl
			// and return a media status of B_OK
			if (!geometry.removable) {
				error = B_OK;
				*mediaStatus = B_OK;
			}
		}
	}
	return error;
}


/**
 * @brief Queries the driver for the device geometry via ioctl.
 *
 * @param geometry  Output pointer; receives the device_geometry structure.
 * @return B_OK on success.
 * @retval errno  The B_GET_GEOMETRY ioctl failed.
 */
status_t
KDiskDevice::GetGeometry(device_geometry* geometry)
{
	if (ioctl(fFD, B_GET_GEOMETRY, geometry, sizeof(*geometry)) != 0)
		return errno;
	return B_OK;
}


/**
 * @brief Synchronises fPartitionData fields with the current device geometry.
 *
 * Sets the partition block size, physical block size, offset, and total size
 * derived from the geometry, marks the partition as a device, and attempts
 * to retrieve and store the human-readable device name.
 */
void
KDiskDevice::_InitPartitionData()
{
	fDeviceData.id = fPartitionData.id;
	fPartitionData.block_size = fDeviceData.geometry.bytes_per_sector;
	fPartitionData.physical_block_size = fDeviceData.geometry.bytes_per_physical_sector;
	fPartitionData.offset = 0;
	fPartitionData.size = (off_t)fPartitionData.block_size
		* fDeviceData.geometry.sectors_per_track
		* fDeviceData.geometry.cylinder_count
		* fDeviceData.geometry.head_count;
	fPartitionData.flags |= B_PARTITION_IS_DEVICE;

	char name[B_FILE_NAME_LENGTH];
	if (ioctl(fFD, B_GET_DEVICE_NAME, name, sizeof(name)) == B_OK)
		fPartitionData.name = strdup(name);
}


/**
 * @brief Resets the geometry to a safe blank state (no sectors, removable,
 *        read-only, non-write-once, type B_DISK).
 *
 * Called when no media is present or the device is uninitialised, so that
 * the partition data derived from geometry does not contain stale values.
 */
void
KDiskDevice::_ResetGeometry()
{
	fDeviceData.geometry.bytes_per_sector = 0;
	fDeviceData.geometry.sectors_per_track = 0;
	fDeviceData.geometry.cylinder_count = 0;
	fDeviceData.geometry.head_count = 0;
	fDeviceData.geometry.device_type = B_DISK;
	fDeviceData.geometry.removable = true;
	fDeviceData.geometry.read_only = true;
	fDeviceData.geometry.write_once = false;
}


/**
 * @brief Updates the device flags to reflect the current geometry and media
 *        state.
 *
 * Sets or clears B_DISK_DEVICE_REMOVABLE, B_DISK_DEVICE_HAS_MEDIA,
 * B_DISK_DEVICE_READ_ONLY, and B_DISK_DEVICE_WRITE_ONCE as appropriate.
 */
void
KDiskDevice::_UpdateDeviceFlags()
{
	if (fDeviceData.geometry.removable)
		SetDeviceFlags(DeviceFlags() | B_DISK_DEVICE_REMOVABLE);
	if (HasMedia())
		SetDeviceFlags(DeviceFlags() | B_DISK_DEVICE_HAS_MEDIA);
	else
		SetDeviceFlags(DeviceFlags() & ~B_DISK_DEVICE_HAS_MEDIA);

	if (fDeviceData.geometry.read_only)
		SetDeviceFlags(DeviceFlags() | B_DISK_DEVICE_READ_ONLY);
	if (fDeviceData.geometry.write_once)
		SetDeviceFlags(DeviceFlags() | B_DISK_DEVICE_WRITE_ONCE);
}
