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
 *   Copyright 2003-2007, Ingo Weinhold, bonefish@cs.tu-berlin.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file DiskSystem.cpp
 * @brief Represents a disk system (partitioning scheme or file system).
 *
 * BDiskSystem encapsulates the identity and capabilities of a single disk
 * system as reported by the kernel. It provides predicates for querying which
 * structural operations the system supports and exposes the system's names and
 * flags. Private _SetTo() helpers populate the object from kernel info
 * structures or system IDs.
 *
 * @see BDiskDevice
 * @see BPartition
 */

#include <DiskSystem.h>

#include <DiskSystemAddOn.h>
#include <Partition.h>

#include <ddm_userland_interface_defs.h>
#include <syscalls.h>

#include "DiskSystemAddOnManager.h"


/**
 * @brief Constructs a default, uninitialised BDiskSystem.
 *
 * InitCheck() will return B_NO_INIT until the object is populated via one
 * of the private _SetTo() methods.
 */
BDiskSystem::BDiskSystem()
	:
	fID(B_NO_INIT),
	fFlags(0)
{
}


/**
 * @brief Copy-constructs a BDiskSystem from another instance.
 *
 * @param other The source BDiskSystem to copy.
 */
BDiskSystem::BDiskSystem(const BDiskSystem& other)
	:
	fID(other.fID),
	fName(other.fName),
	fShortName(other.fShortName),
	fPrettyName(other.fPrettyName),
	fFlags(other.fFlags)
{
}


/**
 * @brief Destroys the BDiskSystem.
 */
BDiskSystem::~BDiskSystem()
{
}


/**
 * @brief Returns whether the object has been successfully initialised.
 *
 * @return B_OK if the object holds valid disk system information, otherwise
 *         a negative error code such as B_NO_INIT.
 */
status_t
BDiskSystem::InitCheck() const
{
	return fID > 0 ? B_OK : fID;
}


/**
 * @brief Returns the canonical (internal) name of this disk system.
 *
 * @return Null-terminated string with the disk system's canonical name.
 */
const char*
BDiskSystem::Name() const
{
	return fName.String();
}


/**
 * @brief Returns the short name of this disk system.
 *
 * @return Null-terminated string with the disk system's short name.
 */
const char*
BDiskSystem::ShortName() const
{
	return fShortName.String();
}


/**
 * @brief Returns the human-readable display name of this disk system.
 *
 * @return Null-terminated string with the disk system's pretty name.
 */
const char*
BDiskSystem::PrettyName() const
{
	return fPrettyName.String();
}


/**
 * @brief Returns whether this disk system supports defragmenting.
 *
 * @param whileMounted If non-NULL, set to true when defragmentation is also
 *                     supported while the volume is mounted.
 * @return true if defragmenting is supported, false otherwise.
 */
bool
BDiskSystem::SupportsDefragmenting(bool* whileMounted) const
{
	if (InitCheck() != B_OK
		|| !(fFlags & B_DISK_SYSTEM_SUPPORTS_DEFRAGMENTING)) {
		if (whileMounted)
			*whileMounted = false;
		return false;
	}

	if (whileMounted) {
		*whileMounted = IsFileSystem() && (fFlags
				& B_DISK_SYSTEM_SUPPORTS_DEFRAGMENTING_WHILE_MOUNTED) != 0;
	}

	return true;
}


/**
 * @brief Returns whether this disk system supports repairing or checking.
 *
 * @param checkOnly    If true, query check-only support; otherwise query full
 *                     repair support.
 * @param whileMounted If non-NULL, set to true when the operation is also
 *                     supported while the volume is mounted.
 * @return true if the requested operation is supported, false otherwise.
 */
bool
BDiskSystem::SupportsRepairing(bool checkOnly, bool* whileMounted) const
{
	uint32 mainBit = B_DISK_SYSTEM_SUPPORTS_REPAIRING;
	uint32 mountedBit = B_DISK_SYSTEM_SUPPORTS_REPAIRING_WHILE_MOUNTED;

	if (checkOnly) {
		mainBit = B_DISK_SYSTEM_SUPPORTS_CHECKING;
		mountedBit = B_DISK_SYSTEM_SUPPORTS_CHECKING_WHILE_MOUNTED;
	}

	if (InitCheck() != B_OK || !(fFlags & mainBit)) {
		if (whileMounted)
			*whileMounted = false;
		return false;
	}

	if (whileMounted)
		*whileMounted = (IsFileSystem() && (fFlags & mountedBit));

	return true;
}


/**
 * @brief Returns whether this disk system supports resizing its content.
 *
 * @param whileMounted If non-NULL, set to true when resizing is also
 *                     supported while the volume is mounted.
 * @return true if resizing is supported, false otherwise.
 */
bool
BDiskSystem::SupportsResizing(bool* whileMounted) const
{
	if (InitCheck() != B_OK
		|| !(fFlags & B_DISK_SYSTEM_SUPPORTS_RESIZING)) {
		if (whileMounted)
			*whileMounted = false;
		return false;
	}

	if (whileMounted) {
		*whileMounted = (IsFileSystem()
			&& (fFlags & B_DISK_SYSTEM_SUPPORTS_RESIZING_WHILE_MOUNTED));
	}

	return true;
}


/**
 * @brief Returns whether this partitioning system supports resizing child
 *        partitions.
 *
 * @return true if child resizing is supported, false otherwise.
 */
bool
BDiskSystem::SupportsResizingChild() const
{
	return (InitCheck() == B_OK && IsPartitioningSystem()
		&& (fFlags & B_DISK_SYSTEM_SUPPORTS_RESIZING_CHILD));
}


/**
 * @brief Returns whether this disk system supports moving its content.
 *
 * @param whileMounted If non-NULL, set to true when moving is also supported
 *                     while the volume is mounted.
 * @return true if moving is supported, false otherwise.
 */
bool
BDiskSystem::SupportsMoving(bool* whileMounted) const
{
	if (InitCheck() != B_OK
		|| !(fFlags & B_DISK_SYSTEM_SUPPORTS_MOVING)) {
		if (whileMounted)
			*whileMounted = false;
		return false;
	}

	if (whileMounted) {
		*whileMounted = (IsFileSystem()
			&& (fFlags & B_DISK_SYSTEM_SUPPORTS_MOVING_WHILE_MOUNTED));
	}

	return true;
}


/**
 * @brief Returns whether this partitioning system supports moving child
 *        partitions.
 *
 * @return true if child moving is supported, false otherwise.
 */
bool
BDiskSystem::SupportsMovingChild() const
{
	return (InitCheck() == B_OK && IsPartitioningSystem()
		&& (fFlags & B_DISK_SYSTEM_SUPPORTS_MOVING_CHILD));
}


/**
 * @brief Returns whether this partitioning system supports partition names.
 *
 * @return true if partition names are supported, false otherwise.
 */
bool
BDiskSystem::SupportsName() const
{
	return (InitCheck() == B_OK && IsPartitioningSystem()
		&& (fFlags & B_DISK_SYSTEM_SUPPORTS_NAME));
}


/**
 * @brief Returns whether this disk system supports a content name.
 *
 * @return true if content names are supported, false otherwise.
 */
bool
BDiskSystem::SupportsContentName() const
{
	return (InitCheck() == B_OK
		&& (fFlags & B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME));
}


/**
 * @brief Returns whether this partitioning system supports setting partition
 *        names.
 *
 * @return true if setting names is supported, false otherwise.
 */
bool
BDiskSystem::SupportsSettingName() const
{
	return (InitCheck() == B_OK && IsPartitioningSystem()
		&& (fFlags & B_DISK_SYSTEM_SUPPORTS_SETTING_NAME));
}


/**
 * @brief Returns whether this disk system supports setting the content name.
 *
 * @param whileMounted If non-NULL, set to true when the operation is also
 *                     supported while the volume is mounted.
 * @return true if setting the content name is supported, false otherwise.
 */
bool
BDiskSystem::SupportsSettingContentName(bool* whileMounted) const
{
	if (InitCheck() != B_OK
		|| !(fFlags & B_DISK_SYSTEM_SUPPORTS_SETTING_CONTENT_NAME)) {
		if (whileMounted)
			*whileMounted = false;
		return false;
	}

	if (whileMounted) {
		*whileMounted = (IsFileSystem()
			&& (fFlags
				& B_DISK_SYSTEM_SUPPORTS_SETTING_CONTENT_NAME_WHILE_MOUNTED));
	}

	return true;
}


/**
 * @brief Returns whether this partitioning system supports setting partition
 *        types.
 *
 * @return true if setting the partition type is supported, false otherwise.
 */
bool
BDiskSystem::SupportsSettingType() const
{
	return (InitCheck() == B_OK && IsPartitioningSystem()
		&& (fFlags & B_DISK_SYSTEM_SUPPORTS_SETTING_TYPE));
}


/**
 * @brief Returns whether this partitioning system supports setting partition
 *        parameters.
 *
 * @return true if setting partition parameters is supported, false otherwise.
 */
bool
BDiskSystem::SupportsSettingParameters() const
{
	return (InitCheck() == B_OK && IsPartitioningSystem()
		&& (fFlags & B_DISK_SYSTEM_SUPPORTS_SETTING_PARAMETERS));
}


/**
 * @brief Returns whether this disk system supports setting content parameters.
 *
 * @param whileMounted If non-NULL, set to true when the operation is also
 *                     supported while the volume is mounted.
 * @return true if setting content parameters is supported, false otherwise.
 */
bool
BDiskSystem::SupportsSettingContentParameters(bool* whileMounted) const
{
	if (InitCheck() != B_OK
		|| !(fFlags & B_DISK_SYSTEM_SUPPORTS_SETTING_CONTENT_PARAMETERS)) {
		if (whileMounted)
			*whileMounted = false;
		return false;
	}

	if (whileMounted) {
		uint32 whileMountedFlag
			= B_DISK_SYSTEM_SUPPORTS_SETTING_CONTENT_PARAMETERS_WHILE_MOUNTED;
		*whileMounted = (IsFileSystem() && (fFlags & whileMountedFlag));
	}

	return true;
}


/**
 * @brief Returns whether this partitioning system supports creating child
 *        partitions.
 *
 * @return true if creating children is supported, false otherwise.
 */
bool
BDiskSystem::SupportsCreatingChild() const
{
	return (InitCheck() == B_OK && IsPartitioningSystem()
		&& (fFlags & B_DISK_SYSTEM_SUPPORTS_CREATING_CHILD));
}


/**
 * @brief Returns whether this partitioning system supports deleting child
 *        partitions.
 *
 * @return true if deleting children is supported, false otherwise.
 */
bool
BDiskSystem::SupportsDeletingChild() const
{
	return (InitCheck() == B_OK && IsPartitioningSystem()
		&& (fFlags & B_DISK_SYSTEM_SUPPORTS_DELETING_CHILD));
}


/**
 * @brief Returns whether this disk system supports initialising a partition.
 *
 * @return true if initialisation is supported, false otherwise.
 */
bool
BDiskSystem::SupportsInitializing() const
{
	return (InitCheck() == B_OK
		&& (fFlags & B_DISK_SYSTEM_SUPPORTS_INITIALIZING));
}


/**
 * @brief Returns whether this file system supports write access.
 *
 * Always returns false for partitioning systems.
 *
 * @return true if the file system supports writing, false otherwise.
 */
bool
BDiskSystem::SupportsWriting() const
{
	if (InitCheck() != B_OK
		|| !IsFileSystem())
		return false;

	return (fFlags & B_DISK_SYSTEM_SUPPORTS_WRITING) != 0;
}


/**
 * @brief Converts a content type string to a partition type string.
 *
 * Delegates to the underlying BDiskSystemAddOn obtained from the
 * DiskSystemAddOnManager.
 *
 * @param contentType The content (file-system) type name to look up.
 * @param type        Output BString set to the corresponding partition type.
 * @return B_OK on success, B_BAD_VALUE for invalid arguments, B_ENTRY_NOT_FOUND
 *         if the add-on is not loaded, or another error from the add-on.
 */
status_t
BDiskSystem::GetTypeForContentType(const char* contentType, BString* type) const
{
	if (InitCheck() != B_OK)
		return InitCheck();

	if (!contentType || !type || !IsPartitioningSystem())
		return B_BAD_VALUE;

	DiskSystemAddOnManager* manager = DiskSystemAddOnManager::Default();
	BDiskSystemAddOn* addOn = manager->GetAddOn(fName.String());
	if (!addOn)
		return B_ENTRY_NOT_FOUND;

	status_t result = addOn->GetTypeForContentType(contentType, type);

	manager->PutAddOn(addOn);

	return result;
}


/**
 * @brief Returns true if this disk system is a partitioning system.
 *
 * @return true if the B_DISK_SYSTEM_IS_FILE_SYSTEM flag is not set and the
 *         object is initialised.
 */
bool
BDiskSystem::IsPartitioningSystem() const
{
	return InitCheck() == B_OK && !(fFlags & B_DISK_SYSTEM_IS_FILE_SYSTEM);
}


/**
 * @brief Returns true if this disk system is a file system.
 *
 * @return true if the B_DISK_SYSTEM_IS_FILE_SYSTEM flag is set and the
 *         object is initialised.
 */
bool
BDiskSystem::IsFileSystem() const
{
	return InitCheck() == B_OK && (fFlags & B_DISK_SYSTEM_IS_FILE_SYSTEM);
}


/**
 * @brief Assigns another BDiskSystem to this object.
 *
 * @param other The source BDiskSystem to copy from.
 * @return Reference to this object.
 */
BDiskSystem&
BDiskSystem::operator=(const BDiskSystem& other)
{
	fID = other.fID;
	fName = other.fName;
	fShortName = other.fShortName;
	fPrettyName = other.fPrettyName;
	fFlags = other.fFlags;

	return *this;
}


/**
 * @brief Populates this object from the kernel using a disk system ID.
 *
 * Queries the kernel for the user_disk_system_info associated with id and
 * delegates to the info-based overload.
 *
 * @param id The kernel disk system identifier.
 * @return B_OK on success, or an error code from the kernel call.
 */
status_t
BDiskSystem::_SetTo(disk_system_id id)
{
	_Unset();

	if (id < 0)
		return fID;

	user_disk_system_info info;
	status_t error = _kern_get_disk_system_info(id, &info);
	if (error != B_OK)
		return (fID = error);

	return _SetTo(&info);
}


/**
 * @brief Populates this object from a user_disk_system_info structure.
 *
 * @param info Pointer to the info structure returned by the kernel.
 * @return B_OK on success, B_BAD_VALUE if info is NULL.
 */
status_t
BDiskSystem::_SetTo(const user_disk_system_info* info)
{
	_Unset();

	if (!info)
		return (fID = B_BAD_VALUE);

	fID = info->id;
	fName = info->name;
	fShortName = info->short_name;
	fPrettyName = info->pretty_name;
	fFlags = info->flags;

	return B_OK;
}


/**
 * @brief Resets this object to the uninitialised state.
 *
 * Clears the ID, names, and flags so that InitCheck() returns B_NO_INIT.
 */
void
BDiskSystem::_Unset()
{
	fID = B_NO_INIT;
	fName = (const char*)NULL;
	fPrettyName = (const char*)NULL;
	fFlags = 0;
}
