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
 *   Copyright 2003-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file KFileDiskDevice.cpp
 * @brief KDiskDevice subclass backed by a regular file instead of a physical device.
 *
 * KFileDiskDevice wraps an ordinary file as a virtual disk device, publishing
 * it through devfs under /dev/disk/virtual/files/<id>/raw.  The geometry is
 * derived from the file size assuming 512-byte sectors and a single track per
 * head, scaling the head count to stay within the uint32 cylinder limit for
 * files larger than roughly 2 TB.
 */


#include <KFileDiskDevice.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <devfs.h>
#include <Drivers.h>
#include <KernelExport.h>

#include <KDiskDeviceUtils.h>
#include <KPath.h>


static const char* kFileDevicesDir = "/dev/disk/virtual/files";


/**
 * @brief Constructs a KFileDiskDevice with the given partition/device ID.
 *
 * Sets the B_DISK_DEVICE_IS_FILE flag to indicate this is a file-backed
 * virtual device.
 *
 * @param id The partition_id to assign to this device.
 */
KFileDiskDevice::KFileDiskDevice(partition_id id)
	:
	KDiskDevice(id),
	fFilePath(NULL)
{
	SetDeviceFlags(DeviceFlags() | B_DISK_DEVICE_IS_FILE);
}


/**
 * @brief Destructor. Unregisters the devfs entry and frees the file path.
 */
KFileDiskDevice::~KFileDiskDevice()
{
	Unset();
}


/**
 * @brief Associates this device with @a filePath and optionally creates a devfs
 *        entry at @a devicePath.
 *
 * If @a devicePath is NULL a new virtual device entry is created under
 * /dev/disk/virtual/files/<id>/raw.  The file must be a regular file; symbolic
 * links and block devices are rejected.  After a successful call the device is
 * fully initialised and ready for use.
 *
 * @param filePath   Absolute path to the backing regular file.
 * @param devicePath Absolute devfs path for the device node, or NULL to auto-
 *                   create one.
 * @return B_OK on success, B_BAD_VALUE for invalid paths or non-regular files,
 *         or another error code on failure.
 */
status_t
KFileDiskDevice::SetTo(const char* filePath, const char* devicePath)
{
	if (!filePath || strlen(filePath) > B_PATH_NAME_LENGTH
		|| (devicePath && strlen(devicePath) > B_PATH_NAME_LENGTH)) {
		return B_BAD_VALUE;
	}

	// normalize the file path
	// (should actually not be necessary, since this method is only invoked
	// by the DDM, which has already normalized the path)
	KPath tmpFilePath;
	status_t error = tmpFilePath.SetTo(filePath, KPath::NORMALIZE);
	if (error != B_OK)
		return error;

	// check the file
	struct stat st;
	if (stat(filePath, &st) != 0)
		return errno;
	if (!S_ISREG(st.st_mode))
		return B_BAD_VALUE;

	// create the device, if requested
	KPath tmpDevicePath;
	if (devicePath == NULL) {
		// no device path: we shall create a new device entry
		if (tmpDevicePath.InitCheck() != B_OK)
			return tmpDevicePath.InitCheck();

		// make the directory
		status_t error = _GetDirectoryPath(ID(), &tmpDevicePath);
		if (error != B_OK)
			return error;

		// get the device path name
		error = tmpDevicePath.Append("raw");
		if (error != B_OK)
			return error;
		devicePath = tmpDevicePath.Path();

		// register the file as virtual disk device
		error = _RegisterDevice(filePath, devicePath);
		if (error != B_OK)
			return error;
	}
	error = set_string(fFilePath, filePath);
	if (error != B_OK)
		return error;

	error = KDiskDevice::SetTo(devicePath);
	if (error != B_OK)
		return error;

	// reset the B_DISK_DEVICE_IS_FILE flag -- KDiskDevice::SetTo() has cleared it
	SetDeviceFlags(DeviceFlags() | B_DISK_DEVICE_IS_FILE);

	return B_OK;
}


/**
 * @brief Unregisters the devfs device entry and resets all state.
 *
 * Removes the virtual device node from devfs (if one was created), frees the
 * stored file path string, and delegates further cleanup to KDiskDevice::Unset().
 */
void
KFileDiskDevice::Unset()
{
	// remove the device and the directory it resides in
	if (Path() && ID() >= 0) {
		_UnregisterDevice(Path());
// TODO: Cleanup. The devfs will automatically remove the directory.
//		KPath dirPath;
//		if (_GetDirectoryPath(ID(), &dirPath) == B_OK)
//			rmdir(dirPath.Path());
	}

	free(fFilePath);
	fFilePath = NULL;

	KDiskDevice::Unset();
}


/**
 * @brief Returns the absolute path of the backing file.
 * @return NUL-terminated path string, or NULL if the device has not been
 *         initialised.
 */
const char*
KFileDiskDevice::FilePath() const
{
	return fFilePath;
}


/**
 * @brief Checks whether the backing file is accessible and reports media status.
 *
 * Sets *mediaStatus to B_OK if the backing file exists and is a regular file,
 * or to B_DEV_NO_MEDIA otherwise.
 *
 * @param mediaStatus Output parameter that receives the media status code.
 * @return Always returns B_OK.
 */
status_t
KFileDiskDevice::GetMediaStatus(status_t* mediaStatus)
{
	// check the file
	struct stat st;
	if (stat(fFilePath, &st) == 0 && S_ISREG(st.st_mode))
		*mediaStatus = B_OK;
	else
		*mediaStatus = B_DEV_NO_MEDIA;
	return B_OK;
}


/**
 * @brief Computes a synthetic device geometry derived from the backing file size.
 *
 * Assumes 512-byte blocks.  The head count is set to the minimum value that
 * keeps the cylinder count within a uint32, enabling support for files up to
 * the theoretical maximum (limited by off_t).
 *
 * @param geometry Output structure to fill with computed geometry values.
 * @return B_OK on success, B_BAD_VALUE if the file cannot be stat-ed or is not
 *         a regular file.
 */
status_t
KFileDiskDevice::GetGeometry(device_geometry* geometry)
{
	// check the file
	struct stat st;
	if (stat(fFilePath, &st) != 0 || !S_ISREG(st.st_mode))
		return B_BAD_VALUE;

	// fill in the geometry
	// default to 512 bytes block size
	uint32 blockSize = 512;
	// Optimally we have only 1 block per sector and only one head.
	// Since we have only a uint32 for the cylinder count, this won't work
	// for files > 2TB. So, we set the head count to the minimally possible
	// value.
	off_t blocks = st.st_size / blockSize;
	uint32 heads = (blocks + ULONG_MAX - 1) / ULONG_MAX;
	if (heads == 0)
		heads = 1;
	geometry->bytes_per_sector = blockSize;
	geometry->sectors_per_track = 1;
	geometry->cylinder_count = blocks / heads;
	geometry->head_count = heads;
	geometry->device_type = B_DISK;	// TODO: Add a new constant.
	geometry->removable = false;
	geometry->read_only = false;
	geometry->write_once = false;

	return B_OK;
}


/**
 * @brief Publishes the backing file as a virtual disk device in devfs.
 *
 * Strips the leading "/dev/" prefix from @a device before passing the path to
 * devfs_publish_file_device().
 *
 * @param file   Absolute path to the backing file.
 * @param device Absolute devfs path for the new device node (must start with
 *               "/dev/").
 * @return B_OK on success or an error code from devfs.
 */
status_t
KFileDiskDevice::_RegisterDevice(const char* file, const char* device)
{
	return devfs_publish_file_device(device + 5, file);
		// we need to remove the "/dev/" part from the path
}


/**
 * @brief Removes a previously published virtual disk device from devfs.
 *
 * Strips the leading "/dev/" prefix from @a _device before passing the path to
 * devfs_unpublish_file_device().
 *
 * @param _device Absolute devfs path of the device node to remove (must start
 *                with "/dev/").
 * @return B_OK on success or an error code from devfs.
 */
status_t
KFileDiskDevice::_UnregisterDevice(const char* _device)
{
	return devfs_unpublish_file_device(_device + 5);
		// we need to remove the "/dev/" part from the path
}


/**
 * @brief Constructs the devfs directory path for the virtual device with the
 *        given @a id.
 *
 * The resulting path is kFileDevicesDir/<id> (e.g.
 * /dev/disk/virtual/files/42).
 *
 * @param id   Partition ID used as the directory name component.
 * @param path KPath object to receive the constructed directory path.
 * @return B_OK on success, B_BAD_VALUE if @a path is NULL, or the error from
 *         KPath operations.
 */
status_t
KFileDiskDevice::_GetDirectoryPath(partition_id id, KPath* path)
{
	if (path == NULL)
		return B_BAD_VALUE;

	if (path->InitCheck() != B_OK)
		return path->InitCheck();

	status_t error = path->SetPath(kFileDevicesDir);
	if (error == B_OK) {
		char idBuffer[12];
		snprintf(idBuffer, sizeof(idBuffer), "%" B_PRId32, id);
		error = path->Append(idBuffer);
	}
	return error;
}
