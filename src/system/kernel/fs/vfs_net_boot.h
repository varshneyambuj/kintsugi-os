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

/** @file vfs_net_boot.h
 *  @brief BootMethod implementation that recognises network-loaded boot images. */

#ifndef _VFS_NET_BOOT_H
#define _VFS_NET_BOOT_H


#include "vfs_boot.h"


/** @brief qsort comparator that orders boot images by preferred load order. */
int compare_image_boot(const void *_a, const void *_b);


/** @brief BootMethod that recognises a network-booted root file system. */
class NetBootMethod : public BootMethod {
public:
	NetBootMethod(const KMessage& bootVolume, int32 method);
	virtual ~NetBootMethod();

	/** @brief Performs deferred initialisation; called once VFS is up. */
	virtual status_t Init();

	/** @brief Returns true if @p device looks like a network-loaded boot image. */
	virtual bool IsBootDevice(KDiskDevice* device, bool strict);
	/** @brief Returns true if @p partition is (or could be) the network boot partition. */
	virtual bool IsBootPartition(KPartition* partition, bool& foundForSure);
	/** @brief Sorts @p partitions into preferred network-boot order. */
	virtual void SortPartitions(KPartition** partitions, int32 count);
};


#endif	// _VFS_NET_BOOT_H
