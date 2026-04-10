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
 *   Copyright 2007, Ingo Weinhold, bonefish@cs.tu-berlin.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file vfs_boot.h
 *  @brief Abstract policy used by the VFS to identify and rank the boot device. */

#ifndef _VFS_BOOT_H
#define _VFS_BOOT_H


#include <disk_device_manager/KDiskDevice.h>
#include <util/KMessage.h>


/** @brief Strategy interface used by vfs_boot to find the root file system.
 *
 * Each concrete subclass implements one boot method (disk, network, custom)
 * and is consulted to recognise the matching device or partition and to
 * sort candidate partitions in preferred-boot order. */
class BootMethod {
public:
	/** @brief Stores the kernel-supplied boot volume description.
	 *  @param bootVolume Message describing the requested boot volume.
	 *  @param method     Numeric tag identifying which boot method this is. */
	BootMethod(const KMessage& bootVolume, int32 method);
	virtual ~BootMethod();

	/** @brief Performs deferred initialisation; called once VFS is up. */
	virtual status_t Init();

	/** @brief Returns true if @p device might be the boot device.
	 *  @param strict If true, demand a definitive match instead of a guess. */
	virtual bool IsBootDevice(KDiskDevice* device, bool strict) = 0;
	/** @brief Returns true if @p partition is (or could be) the boot partition.
	 *  @param foundForSure Set to true if the match is definitive. */
	virtual bool IsBootPartition(KPartition* partition, bool& foundForSure) = 0;
	/** @brief Sorts @p partitions into the preferred boot order. */
	virtual void SortPartitions(KPartition** partitions, int32 count) = 0;

protected:
	const KMessage&	fBootVolume;  /**< Boot volume description supplied by the kernel. */
	int32			fMethod;      /**< Numeric tag identifying this boot method. */
};


#endif	// _VFS_BOOT_H
