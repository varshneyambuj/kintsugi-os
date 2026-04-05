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
 *   Copyright 2003-2009, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file DiskDeviceTypes.cpp
 * @brief Global string constants for disk device and partition type names.
 *
 * Provides the definitions of all public type-name constants declared in
 * DiskDeviceTypes.h. These constants map to the canonical name strings
 * defined in disk_device_types.h and are used throughout the storage kit
 * to identify device classes, partitioning schemes, and file-system formats.
 *
 * @see BDiskDevice
 * @see BPartition
 */


#include <DiskDeviceTypes.h>

#include <disk_device_types.h>


// Device Types

const char* kDeviceTypeFloppyDisk		= FLOPPY_DEVICE_NAME;
const char* kDeviceTypeHardDisk			= HARD_DISK_DEVICE_NAME;
const char* kDeviceTypeOptical			= OPTICAL_DEVICE_NAME;

// Partition types

const char* kPartitionTypeUnrecognized	= UNRECOGNIZED_PARTITION_NAME;

const char* kPartitionTypeMultisession	= MULTISESSION_PARTITION_NAME;
const char* kPartitionTypeAudioSession	= AUDIO_SESSION_PARTITION_NAME;
const char* kPartitionTypeDataSession	= DATA_SESSION_PARTITION_NAME;

const char* kPartitionTypeAmiga			= AMIGA_PARTITION_NAME;
const char* kPartitionTypeApple			= APPLE_PARTITION_NAME;
const char* kPartitionTypeEFI			= EFI_PARTITION_NAME;
const char* kPartitionTypeIntel			= INTEL_PARTITION_NAME;
const char* kPartitionTypeIntelExtended	= INTEL_EXTENDED_PARTITION_NAME;
const char* kPartitionTypeVMDK			= VMDK_PARTITION_NAME;

const char* kPartitionTypeAmigaFFS		= AMIGA_FFS_NAME;
const char* kPartitionTypeBFS			= BFS_NAME;
const char* kPartitionTypeBTRFS			= BTRFS_NAME;
const char* kPartitionTypeEXFAT			= EXFAT_FS_NAME;
const char* kPartitionTypeEXT2			= EXT2_FS_NAME;
const char* kPartitionTypeEXT3			= EXT3_FS_NAME;
const char* kPartitionTypeFAT12			= FAT12_FS_NAME;
const char* kPartitionTypeFAT16			= FAT16_FS_NAME;
const char* kPartitionTypeFAT32			= FAT32_FS_NAME;
const char* kPartitionTypeHFS			= HFS_NAME;
const char* kPartitionTypeHFSPlus		= HFS_PLUS_NAME;
const char* kPartitionTypeISO9660		= ISO9660_FS_NAME;
const char* kPartitionTypeReiser		= REISER_FS_NAME;
const char* kPartitionTypeUDF			= UDF_FS_NAME;
