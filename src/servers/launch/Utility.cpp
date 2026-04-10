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
 *   Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file Utility.cpp
 *  @brief Implements utility functions for volume read-only detection, media ejection, and path translation. */


#include "Utility.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <device/scsi.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <fs_info.h>
#include <Volume.h>


namespace {


/**
 * @brief Sends an ioctl command to the device backing the given path.
 *
 * Resolves the raw device from @a path (falling back to the device_name
 * if the path lives on a non-devfs filesystem), opens it read-only, and
 * issues the requested @a opcode via ioctl().
 *
 * @param path       Filesystem path or raw device path.
 * @param opcode     The ioctl opcode to send.
 * @param buffer     Data buffer passed to ioctl (may be NULL).
 * @param bufferSize Size of @a buffer in bytes.
 * @return B_OK on success, or an errno-based error code on failure.
 */
status_t
IssueDeviceCommand(const char* path, int opcode, void* buffer,
	size_t bufferSize)
{
	fs_info info;
	if (fs_stat_dev(dev_for_path(path), &info) == B_OK) {
		if (strcmp(info.fsh_name, "devfs") != 0)
			path = info.device_name;
	}

	int device = open(path, O_RDONLY);
	if (device < 0)
		return device;

	status_t status = B_OK;

	if (ioctl(device, opcode, buffer, bufferSize) != 0) {
		fprintf(stderr, "Failed to process %d on %s: %s\n", opcode, path,
			strerror(errno));
		status = errno;
	}
	close(device);
	return status;
}


}	// private namespace


namespace Utility {


/**
 * @brief Tests whether the volume identified by @a device is read-only.
 *
 * Looks up the volume's partition via the disk device roster and queries
 * its read-only flag.
 *
 * @param device The device ID of the volume to check.
 * @return @c true if the volume's partition is read-only, @c false otherwise
 *         or on lookup failure.
 */
bool
IsReadOnlyVolume(dev_t device)
{
	BVolume volume;
	status_t status = volume.SetTo(device);
	if (status != B_OK) {
		fprintf(stderr, "Failed to get BVolume for device %" B_PRIdDEV
			": %s\n", device, strerror(status));
		return false;
	}

	BDiskDeviceRoster roster;
	BDiskDevice diskDevice;
	BPartition* partition;
	status = roster.FindPartitionByVolume(volume, &diskDevice, &partition);
	if (status != B_OK) {
		fprintf(stderr, "Failed to get partition for device %" B_PRIdDEV
			": %s\n", device, strerror(status));
		return false;
	}

	return partition->IsReadOnly();
}


/**
 * @brief Tests whether the volume containing @a path is read-only.
 *
 * @param path A filesystem path residing on the volume to check.
 * @return @c true if the volume is read-only, @c false otherwise.
 */
bool
IsReadOnlyVolume(const char* path)
{
	return IsReadOnlyVolume(dev_for_path(path));
}


/**
 * @brief Blocks or unblocks media removal for the device at @a path.
 *
 * Issues a SCSI PREVENT/ALLOW MEDIA REMOVAL command to the underlying device.
 *
 * @param path  Filesystem path or raw device path.
 * @param block @c true to prevent removal, @c false to allow it.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BlockMedia(const char* path, bool block)
{
	return IssueDeviceCommand(path, B_SCSI_PREVENT_ALLOW, &block,
		sizeof(block));
}


/**
 * @brief Ejects the removable media at @a path.
 *
 * @param path Filesystem path or raw device path of the media to eject.
 * @return B_OK on success, or an error code on failure.
 */
status_t
EjectMedia(const char* path)
{
	return IssueDeviceCommand(path, B_EJECT_DEVICE, NULL, 0);
}


/**
 * @brief Expands shell-style home-directory references in a path string.
 *
 * Replaces occurrences of "$HOME", "${HOME}", and a leading "~/" with the
 * hard-coded home directory path "/boot/home".
 *
 * @param originalPath The input path potentially containing home-directory tokens.
 * @return A new BString with all home-directory tokens expanded.
 */
BString
TranslatePath(const char* originalPath)
{
	BString path = originalPath;

	// TODO: get actual home directory!
	const char* home = "/boot/home";
	path.ReplaceAll("$HOME", home);
	path.ReplaceAll("${HOME}", home);
	if (path.StartsWith("~/"))
		path.ReplaceFirst("~", home);

	return path;
}


}	// namespace Utility
