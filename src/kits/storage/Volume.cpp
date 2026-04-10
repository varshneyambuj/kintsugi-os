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
 *   Copyright 2002-2009, Haiku Inc. All Rights Reserved.
 *   Authors: Tyler Dauwalder, Ingo Weinhold
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file Volume.cpp
 * @brief Implementation of the BVolume class for filesystem volume access.
 *
 * BVolume represents a mounted filesystem volume and exposes its properties
 * such as capacity, free space, block size, name, and capability flags. It
 * also provides methods for retrieving the volume's root directory and icon,
 * and for setting the volume name. Volumes are identified by their device ID.
 *
 * @see BVolume
 */

#include <errno.h>
#include <string.h>

#include <Bitmap.h>
#include <Directory.h>
#include <fs_info.h>
#include <Node.h>
#include <Path.h>
#include <Volume.h>

#include <storage_support.h>
#include <syscalls.h>

#include <fs_interface.h>


/**
 * @brief Default constructor; creates an uninitialized BVolume object.
 */
BVolume::BVolume()
	: fDevice((dev_t)-1),
	  fCStatus(B_NO_INIT)
{
}


/**
 * @brief Constructs a BVolume and initializes it to the specified device.
 *
 * @param device The device ID of the volume to represent.
 */
BVolume::BVolume(dev_t device)
	: fDevice((dev_t)-1),
	  fCStatus(B_NO_INIT)
{
	SetTo(device);
}


/**
 * @brief Copy constructor; creates a BVolume that is a copy of volume.
 *
 * @param volume The source BVolume to copy.
 */
BVolume::BVolume(const BVolume &volume)
	: fDevice(volume.fDevice),
	  fCStatus(volume.fCStatus)
{
}


/**
 * @brief Destructor; frees all resources associated with this BVolume.
 */
BVolume::~BVolume()
{
}


/**
 * @brief Returns the initialization status of this BVolume.
 *
 * @return B_OK if properly initialized, or an error code otherwise.
 */
status_t
BVolume::InitCheck(void) const
{
	return fCStatus;
}


/**
 * @brief Initializes the BVolume to the volume specified by device.
 *
 * @param device The device ID of the volume to open.
 * @return B_OK on success, B_BAD_VALUE if device is invalid, or errno on failure.
 */
status_t
BVolume::SetTo(dev_t device)
{
	// uninitialize
	Unset();
	// check the parameter
	status_t error = (device >= 0 ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		fs_info info;
		if (fs_stat_dev(device, &info) != 0)
			error = errno;
	}
	// set the new value
	if (error == B_OK)
		fDevice = device;
	// set the init status variable
	fCStatus = error;
	return fCStatus;
}


/**
 * @brief Resets the BVolume to an uninitialized state.
 */
void
BVolume::Unset()
{
	fDevice = (dev_t)-1;
	fCStatus = B_NO_INIT;
}


/**
 * @brief Returns the device ID of the volume this object represents.
 *
 * @return The device ID, or (dev_t)-1 if not initialized.
 */
dev_t
BVolume::Device() const
{
	return fDevice;
}


/**
 * @brief Fills in directory with the root directory of this volume.
 *
 * @param directory Pointer to a BDirectory to be initialized to the root; must not be NULL.
 * @return B_OK on success, B_BAD_VALUE if directory is NULL or not initialized, or errno.
 */
status_t
BVolume::GetRootDirectory(BDirectory *directory) const
{
	// check parameter and initialization
	status_t error = (directory && InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	// get FS stat
	fs_info info;
	if (error == B_OK && fs_stat_dev(fDevice, &info) != 0)
		error = errno;
	// init the directory
	if (error == B_OK) {
		node_ref ref;
		ref.device = info.dev;
		ref.node = info.root;
		error = directory->SetTo(&ref);
	}
	return error;
}


/**
 * @brief Returns the total storage capacity of the volume in bytes.
 *
 * @return The total byte capacity, or a negative error code if not initialized.
 */
off_t
BVolume::Capacity() const
{
	// check initialization
	status_t error = (InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	// get FS stat
	fs_info info;
	if (error == B_OK && fs_stat_dev(fDevice, &info) != 0)
		error = errno;
	return (error == B_OK ? info.total_blocks * info.block_size : error);
}


/**
 * @brief Returns the amount of free space on the volume in bytes.
 *
 * @return The number of free bytes, or a negative error code if not initialized.
 */
off_t
BVolume::FreeBytes() const
{
	// check initialization
	status_t error = (InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	// get FS stat
	fs_info info;
	if (error == B_OK && fs_stat_dev(fDevice, &info) != 0)
		error = errno;
	return (error == B_OK ? info.free_blocks * info.block_size : error);
}


/**
 * @brief Returns the block size of the volume in bytes.
 *
 * @return The block size on success, B_NO_INIT if not initialized, or errno on error.
 */
off_t
BVolume::BlockSize() const
{
	// check initialization
	if (InitCheck() != B_OK)
		return B_NO_INIT;

	// get FS stat
	fs_info info;
	if (fs_stat_dev(fDevice, &info) != 0)
		return errno;

	return info.block_size;
}


/**
 * @brief Copies the volume's name into the provided buffer.
 *
 * @param name A buffer of at least B_FILE_NAME_LENGTH bytes to receive the name.
 * @return B_OK on success, B_BAD_VALUE if name is NULL or not initialized, or errno.
 */
status_t
BVolume::GetName(char *name) const
{
	// check parameter and initialization
	status_t error = (name && InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	// get FS stat
	fs_info info;
	if (error == B_OK && fs_stat_dev(fDevice, &info) != 0)
		error = errno;
	// copy the name
	if (error == B_OK)
		strncpy(name, info.volume_name, B_FILE_NAME_LENGTH);
	return error;
}


/**
 * @brief Sets the name of the volume, also renaming the mount-point entry if applicable.
 *
 * Following R5 behavior, if a directory named after the old volume name exists
 * at the root and is the volume's mount point, it is also renamed.
 *
 * @param name The new name for the volume; must be shorter than B_FILE_NAME_LENGTH.
 * @return B_OK on success, B_BAD_VALUE if not initialized, B_NAME_TOO_LONG, or errno.
 */
status_t
BVolume::SetName(const char *name)
{
	// check initialization
	if (!name || InitCheck() != B_OK)
		return B_BAD_VALUE;
	if (strlen(name) >= B_FILE_NAME_LENGTH)
		return B_NAME_TOO_LONG;
	// get the FS stat (including the old name) first
	fs_info oldInfo;
	if (fs_stat_dev(fDevice, &oldInfo) != 0)
		return errno;
	if (strcmp(name, oldInfo.volume_name) == 0)
		return B_OK;
	// set the volume name
	fs_info newInfo;
	strlcpy(newInfo.volume_name, name, sizeof(newInfo.volume_name));
	status_t error = _kern_write_fs_info(fDevice, &newInfo,
		FS_WRITE_FSINFO_NAME);
	if (error != B_OK)
		return error;

	// change the name of the mount point

	// R5 implementation checks if an entry with the volume's old name
	// exists in the root directory and renames that entry, if it is indeed
	// the mount point of the volume (or a link referring to it). In all other
	// cases, nothing is done (even if the mount point is named like the
	// volume, but lives in a different directory).
	// We follow suit for the time being.
	// NOTE: If the volume name itself is actually "boot", then this code
	// tries to rename /boot, but that is prevented in the kernel.

	BPath entryPath;
	BEntry entry;
	BEntry traversedEntry;
	node_ref entryNodeRef;
	if (BPrivate::Storage::check_entry_name(name) == B_OK
		&& BPrivate::Storage::check_entry_name(oldInfo.volume_name) == B_OK
		&& entryPath.SetTo("/", oldInfo.volume_name) == B_OK
		&& entry.SetTo(entryPath.Path(), false) == B_OK
		&& entry.Exists()
		&& traversedEntry.SetTo(entryPath.Path(), true) == B_OK
		&& traversedEntry.GetNodeRef(&entryNodeRef) == B_OK
		&& entryNodeRef.device == fDevice
		&& entryNodeRef.node == oldInfo.root) {
		entry.Rename(name, false);
	}
	return error;
}


/**
 * @brief Retrieves the volume's bitmap icon into icon.
 *
 * @param icon  A BBitmap sized appropriately for which.
 * @param which The desired icon size (B_MINI_ICON or B_LARGE_ICON).
 * @return B_OK on success, B_NO_INIT if not initialized, or errno on error.
 */
status_t
BVolume::GetIcon(BBitmap *icon, icon_size which) const
{
	// check initialization
	if (InitCheck() != B_OK)
		return B_NO_INIT;

	// get FS stat for the device name
	fs_info info;
	if (fs_stat_dev(fDevice, &info) != 0)
		return errno;

	// get the icon
	return get_device_icon(info.device_name, icon, which);
}


/**
 * @brief Retrieves the volume's raw icon data.
 *
 * @param _data  Set to a newly allocated buffer with the icon data.
 * @param _size  Set to the size of the buffer in bytes.
 * @param _type  Set to the type code of the icon data.
 * @return B_OK on success, B_NO_INIT if not initialized, or errno on error.
 */
status_t
BVolume::GetIcon(uint8** _data, size_t* _size, type_code* _type) const
{
	// check initialization
	if (InitCheck() != B_OK)
		return B_NO_INIT;

	// get FS stat for the device name
	fs_info info;
	if (fs_stat_dev(fDevice, &info) != 0)
		return errno;

	// get the icon
	return get_device_icon(info.device_name, _data, _size, _type);
}


/**
 * @brief Returns whether the volume is removable (e.g. a CD or USB drive).
 *
 * @return true if the volume has the B_FS_IS_REMOVABLE flag set, false otherwise.
 */
bool
BVolume::IsRemovable() const
{
	// check initialization
	status_t error = (InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	// get FS stat
	fs_info info;
	if (error == B_OK && fs_stat_dev(fDevice, &info) != 0)
		error = errno;
	return (error == B_OK && (info.flags & B_FS_IS_REMOVABLE));
}


/**
 * @brief Returns whether the volume is read-only.
 *
 * @return true if the volume has the B_FS_IS_READONLY flag set, false otherwise.
 */
bool
BVolume::IsReadOnly(void) const
{
	// check initialization
	status_t error = (InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	// get FS stat
	fs_info info;
	if (error == B_OK && fs_stat_dev(fDevice, &info) != 0)
		error = errno;
	return (error == B_OK && (info.flags & B_FS_IS_READONLY));
}


/**
 * @brief Returns whether the volume is persistent (i.e. data survives reboots).
 *
 * @return true if the volume has the B_FS_IS_PERSISTENT flag set, false otherwise.
 */
bool
BVolume::IsPersistent(void) const
{
	// check initialization
	status_t error = (InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	// get FS stat
	fs_info info;
	if (error == B_OK && fs_stat_dev(fDevice, &info) != 0)
		error = errno;
	return (error == B_OK && (info.flags & B_FS_IS_PERSISTENT));
}


/**
 * @brief Returns whether the volume is shared over a network.
 *
 * @return true if the volume has the B_FS_IS_SHARED flag set, false otherwise.
 */
bool
BVolume::IsShared(void) const
{
	// check initialization
	status_t error = (InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	// get FS stat
	fs_info info;
	if (error == B_OK && fs_stat_dev(fDevice, &info) != 0)
		error = errno;
	return (error == B_OK && (info.flags & B_FS_IS_SHARED));
}


/**
 * @brief Returns whether the volume supports MIME-type metadata.
 *
 * @return true if the volume has the B_FS_HAS_MIME flag set, false otherwise.
 */
bool
BVolume::KnowsMime(void) const
{
	// check initialization
	status_t error = (InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	// get FS stat
	fs_info info;
	if (error == B_OK && fs_stat_dev(fDevice, &info) != 0)
		error = errno;
	return (error == B_OK && (info.flags & B_FS_HAS_MIME));
}


/**
 * @brief Returns whether the volume supports extended attributes.
 *
 * @return true if the volume has the B_FS_HAS_ATTR flag set, false otherwise.
 */
bool
BVolume::KnowsAttr(void) const
{
	// check initialization
	status_t error = (InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	// get FS stat
	fs_info info;
	if (error == B_OK && fs_stat_dev(fDevice, &info) != 0)
		error = errno;
	return (error == B_OK && (info.flags & B_FS_HAS_ATTR));
}


/**
 * @brief Returns whether the volume supports queries.
 *
 * @return true if the volume has the B_FS_HAS_QUERY flag set, false otherwise.
 */
bool
BVolume::KnowsQuery(void) const
{
	// check initialization
	status_t error = (InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	// get FS stat
	fs_info info;
	if (error == B_OK && fs_stat_dev(fDevice, &info) != 0)
		error = errno;
	return (error == B_OK && (info.flags & B_FS_HAS_QUERY));
}


/**
 * @brief Equality operator; two BVolumes are equal if they refer to the same device.
 *
 * Two uninitialized BVolumes are also considered equal.
 *
 * @param volume The BVolume to compare against.
 * @return true if the volumes are equal, false otherwise.
 */
bool
BVolume::operator==(const BVolume &volume) const
{
	return ((InitCheck() != B_OK && volume.InitCheck() != B_OK)
			|| fDevice == volume.fDevice);
}

/**
 * @brief Inequality operator.
 *
 * @param volume The BVolume to compare against.
 * @return true if the volumes are not equal, false otherwise.
 */
bool
BVolume::operator!=(const BVolume &volume) const
{
	return !(*this == volume);
}


/**
 * @brief Assignment operator; makes this BVolume refer to the same device as volume.
 *
 * @param volume The source BVolume to copy.
 * @return A reference to this BVolume.
 */
BVolume&
BVolume::operator=(const BVolume &volume)
{
	if (&volume != this) {
		this->fDevice = volume.fDevice;
		this->fCStatus = volume.fCStatus;
	}
	return *this;
}


// FBC
void BVolume::_TurnUpTheVolume1() {}
void BVolume::_TurnUpTheVolume2() {}
void BVolume::_TurnUpTheVolume3() {}
void BVolume::_TurnUpTheVolume4() {}
void BVolume::_TurnUpTheVolume5() {}
void BVolume::_TurnUpTheVolume6() {}
void BVolume::_TurnUpTheVolume7() {}
void BVolume::_TurnUpTheVolume8() {}
