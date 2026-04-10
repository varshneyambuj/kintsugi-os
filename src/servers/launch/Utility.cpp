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


bool
IsReadOnlyVolume(const char* path)
{
	return IsReadOnlyVolume(dev_for_path(path));
}


status_t
BlockMedia(const char* path, bool block)
{
	return IssueDeviceCommand(path, B_SCSI_PREVENT_ALLOW, &block,
		sizeof(block));
}


status_t
EjectMedia(const char* path)
{
	return IssueDeviceCommand(path, B_EJECT_DEVICE, NULL, 0);
}


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
