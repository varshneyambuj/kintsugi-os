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
 * @file Partition.cpp
 * @brief Implementation of the BPartition class.
 *
 * BPartition represents a single partition within a disk device hierarchy.
 * It provides accessors for partition metadata (offset, size, type, name,
 * flags), operations for mounting and unmounting volumes, and a rich set
 * of modification methods (resize, move, rename, initialize, create/delete
 * children) that operate on a shadow structure when the device is prepared
 * for modifications via BDiskDevice::PrepareModifications().
 *
 * @see BDiskDevice
 * @see BDiskDeviceVisitor
 */


#include <errno.h>
#include <new>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>

#include <Directory.h>
#include <DiskDevice.h>
#include <DiskDevicePrivate.h>
#include <DiskDeviceVisitor.h>
#include <DiskSystem.h>
#include <fs_volume.h>
#include <Message.h>
#include <ObjectList.h>
#include <Partition.h>
#include <PartitioningInfo.h>
#include <Path.h>
#include <String.h>
#include <Volume.h>

#include <AutoDeleter.h>

#include <ddm_userland_interface_defs.h>
#include <syscalls.h>

#include "PartitionDelegate.h"


//#define TRACE_PARTITION
#undef TRACE
#ifdef TRACE_PARTITION
# define TRACE(x...) printf(x)
#else
# define TRACE(x...) do {} while (false)
#endif


using std::nothrow;

static const char *skAutoCreatePrefix = "_HaikuAutoCreated";


/**
 * @brief A BPartition object represents a partition and provides methods to
 *        retrieve information about it and some to manipulate it.
 *
 * Not all BPartitions represent actual on-disk partitions. Some exist only
 * to make all devices fit smoothly into the framework (e.g. for floppies,
 * see IsVirtual()), others represent merely partition slots (see IsEmpty()).
 */


/**
 * @brief NULL-aware strcmp helper.
 *
 * NULL is considered the least of all strings; NULL equals NULL.
 *
 * @param str1 First string (may be NULL).
 * @param str2 Second string (may be NULL).
 * @return Negative if str1 < str2, 0 if equal, positive if str1 > str2.
 */
static inline int
compare_string(const char* str1, const char* str2)
{
	if (str1 == NULL) {
		if (str2 == NULL)
			return 0;
		return 1;
	}
	if (str2 == NULL)
		return -1;
	return strcmp(str1, str2);
}


// #pragma mark -


/**
 * @brief Creates an uninitialized BPartition object.
 */
BPartition::BPartition()
	:
	fDevice(NULL),
	fParent(NULL),
	fPartitionData(NULL),
	fDelegate(NULL)
{
}


/**
 * @brief Frees all resources associated with this object.
 */
BPartition::~BPartition()
{
	_Unset();
}


/**
 * @brief Returns the partition's offset relative to the beginning of the device.
 *
 * @return The partition's offset in bytes from the start of the device.
 */
off_t
BPartition::Offset() const
{
	return _PartitionData()->offset;
}


/**
 * @brief Returns the size of the partition.
 *
 * @return The size of the partition in bytes.
 */
off_t
BPartition::Size() const
{
	return _PartitionData()->size;
}


/**
 * @brief Returns the size of the partition content (used by the disk system).
 *
 * @return The content size in bytes as reported by the disk system.
 */
off_t
BPartition::ContentSize() const
{
	return _PartitionData()->content_size;
}


/**
 * @brief Returns the logical block size of the device.
 *
 * @return The block size in bytes.
 */
uint32
BPartition::BlockSize() const
{
	return _PartitionData()->block_size;
}


/**
 * @brief Returns the physical block size of the device.
 *
 * @return The physical block size in bytes.
 */
uint32
BPartition::PhysicalBlockSize() const
{
	return _PartitionData()->physical_block_size;
}


/**
 * @brief Returns the index of this partition among its siblings.
 *
 * @return The zero-based index of this partition in the parent's child list.
 */
int32
BPartition::Index() const
{
	return _PartitionData()->index;
}


/**
 * @brief Returns the status flags for this partition.
 *
 * @return The status value from the underlying partition data.
 */
uint32
BPartition::Status() const
{
	return _PartitionData()->status;
}


/**
 * @brief Returns whether this partition contains a recognized file system.
 *
 * @return \c true if the B_PARTITION_FILE_SYSTEM flag is set.
 */
bool
BPartition::ContainsFileSystem() const
{
	return _PartitionData()->flags & B_PARTITION_FILE_SYSTEM;
}


/**
 * @brief Returns whether this partition contains a partitioning system.
 *
 * @return \c true if the B_PARTITION_PARTITIONING_SYSTEM flag is set.
 */
bool
BPartition::ContainsPartitioningSystem() const
{
	return _PartitionData()->flags & B_PARTITION_PARTITIONING_SYSTEM;
}


/**
 * @brief Returns whether this partition represents the device itself.
 *
 * @return \c true if the B_PARTITION_IS_DEVICE flag is set.
 */
bool
BPartition::IsDevice() const
{
	return _PartitionData()->flags & B_PARTITION_IS_DEVICE;
}


/**
 * @brief Returns whether this partition is read-only.
 *
 * @return \c true if the B_PARTITION_READ_ONLY flag is set.
 */
bool
BPartition::IsReadOnly() const
{
	return _PartitionData()->flags & B_PARTITION_READ_ONLY;
}


/**
 * @brief Returns whether the volume on this partition is currently mounted.
 *
 * @return \c true if the partition is mounted, \c false otherwise.
 */
bool
BPartition::IsMounted() const
{
	return _PartitionData()->flags & B_PARTITION_MOUNTED;
	// alternatively:
	// return _PartitionData()->volume >= 0;
}


/**
 * @brief Returns whether this partition is currently busy.
 *
 * @return \c true if the B_PARTITION_BUSY flag is set.
 */
bool
BPartition::IsBusy() const
{
	return _PartitionData()->flags & B_PARTITION_BUSY;
}


/**
 * @brief Returns whether child partitions of this partition support names.
 *
 * @return \c true if the parent disk system supports child partition names.
 */
bool
BPartition::SupportsChildName() const
{
	return _SupportsChildOperation(NULL, B_DISK_SYSTEM_SUPPORTS_NAME);
}


/**
 * @brief Returns the partition flags.
 *
 * The flags are a bitwise combination of B_HIDDEN_PARTITION,
 * B_VIRTUAL_PARTITION, and B_EMPTY_PARTITION, among others.
 *
 * @return The flags for this partition.
 */
uint32
BPartition::Flags() const
{
	return _PartitionData()->flags;
}


/**
 * @brief Returns the name of the partition as assigned by its partitioning system.
 *
 * Not all partitioning systems support names; returns \c NULL in that case.
 *
 * @return The partition name string, or \c NULL if not supported.
 */
const char*
BPartition::Name() const
{
	return _PartitionData()->name;
}


/**
 * @brief Returns the content name, generating a default if the name is empty.
 *
 * For unnamed file system volumes, a name is derived from the content size
 * and type (e.g. "10 GiB ext4 volume").
 *
 * @return A BString containing the content name or a generated default.
 */
BString
BPartition::ContentName() const
{
	if ((_PartitionData()->content_name == NULL || strlen(_PartitionData()->content_name) == 0)
		&& ContainsFileSystem()) {
		// Give a default name to unnamed volumes
		off_t divisor = 1ULL << 40;
		off_t diskSize = _PartitionData()->content_size;
		char unit = 'T';
		if (diskSize < divisor) {
			divisor = 1UL << 30;
			unit = 'G';
			if (diskSize < divisor) {
				divisor = 1UL << 20;
				unit = 'M';
			}
		}
		double size = double((10 * diskSize + divisor - 1) / divisor);
		BString name;
		name.SetToFormat("%g %ciB %s volume", size / 10, unit, _PartitionData()->content_type);
		return name;
	}

	return _PartitionData()->content_name;
}


/**
 * @brief Returns the raw content name without any default substitution.
 *
 * @return The content name string as stored, which may be \c NULL or empty.
 */
const char*
BPartition::RawContentName() const
{
	return _PartitionData()->content_name;
}


/**
 * @brief Returns a human-readable string describing the partition type.
 *
 * @return The type string for this partition, or \c NULL if unavailable.
 */
const char*
BPartition::Type() const
{
	return _PartitionData()->type;
}


/**
 * @brief Returns the content type string (e.g. file system identifier).
 *
 * @return The content type string, or \c NULL if unavailable.
 */
const char*
BPartition::ContentType() const
{
	return _PartitionData()->content_type;
}


/**
 * @brief Returns a unique identifier for this partition (not persistent across reboots).
 *
 * @return The non-persistent partition ID.
 * @see BDiskDeviceRoster::GetPartitionWithID()
 */
int32
BPartition::ID() const
{
	return _PartitionData()->id;
}


/**
 * @brief Returns the raw parameters string for this partition.
 *
 * @return The parameters string, or \c NULL if none.
 */
const char*
BPartition::Parameters() const
{
	return _PartitionData()->parameters;
}


/**
 * @brief Returns the content parameters string for this partition.
 *
 * @return The content parameters string, or \c NULL if none.
 */
const char*
BPartition::ContentParameters() const
{
	return _PartitionData()->content_parameters;
}


/**
 * @brief Retrieves the disk system that handles this partition's content.
 *
 * @param diskSystem Pointer to a pre-allocated BDiskSystem to be initialized.
 * @return \c B_OK on success, \c B_BAD_VALUE if arguments are invalid,
 *         \c B_ENTRY_NOT_FOUND if no disk system is associated.
 */
status_t
BPartition::GetDiskSystem(BDiskSystem* diskSystem) const
{
	const user_partition_data* data = _PartitionData();
	if (data == NULL || diskSystem == NULL)
		return B_BAD_VALUE;

	if (data->disk_system < 0)
		return B_ENTRY_NOT_FOUND;

	return diskSystem->_SetTo(data->disk_system);
}


/**
 * @brief Constructs the device-node path for this partition.
 *
 * The path is built on the fly by combining the parent's path with this
 * partition's index.
 *
 * @param path Pointer to a BPath to be set to the partition's device node path.
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BPartition::GetPath(BPath* path) const
{
	// The path is constructed on the fly using our parent
	if (path == NULL || Parent() == NULL || Index() < 0)
		return B_BAD_VALUE;

	// get the parent's path
	status_t error = Parent()->GetPath(path);
	if (error != B_OK)
		return error;

	char indexBuffer[24];

	if (Parent()->IsDevice()) {
		// Our parent is a device, so we replace `raw' by our index.
		const char* leaf = path->Leaf();
		if (!leaf || strcmp(leaf, "raw") != B_OK)
			return B_ERROR;

		snprintf(indexBuffer, sizeof(indexBuffer), "%" B_PRId32, Index());
	} else {
		// Our parent is a normal partition, no device: Append our index.
		snprintf(indexBuffer, sizeof(indexBuffer), "%s_%" B_PRId32,
			path->Leaf(), Index());
	}

	error = path->GetParent(path);
	if (error == B_OK)
		error = path->Append(indexBuffer);

	return error;
}


/**
 * @brief Returns a BVolume for the mounted volume on this partition.
 *
 * Can only succeed if the partition is currently mounted.
 *
 * @param volume Pointer to a pre-allocated BVolume to initialize.
 * @return \c B_OK if the partition is mounted and the volume is initialized,
 *         another error code otherwise.
 */
status_t
BPartition::GetVolume(BVolume* volume) const
{
	if (volume == NULL)
		return B_BAD_VALUE;

	return volume->SetTo(_PartitionData()->volume);
}


/**
 * @brief Returns an icon for this partition (per-device icon).
 *
 * If mounted, the icon is retrieved from the volume. Otherwise it is obtained
 * directly from the device. Currently icons are per-device, not per-partition.
 *
 * @param icon Pointer to a pre-allocated BBitmap to be set to the icon.
 * @param which The desired icon size (\c B_MINI_ICON or \c B_LARGE_ICON).
 * @return \c B_OK on success, another error code otherwise.
 */
status_t
BPartition::GetIcon(BBitmap* icon, icon_size which) const
{
	if (icon == NULL)
		return B_BAD_VALUE;

	status_t error;

	if (IsMounted()) {
		// mounted: get the icon from the volume
		BVolume volume;
		error = GetVolume(&volume);
		if (error == B_OK)
			error = volume.GetIcon(icon, which);
	} else {
		// not mounted: retrieve the icon ourselves
		if (BDiskDevice* device = Device()) {
			BPath path;
			error = device->GetPath(&path);
			// get the icon
			if (error == B_OK)
				error = get_device_icon(path.Path(), icon, which);
		} else
			error = B_ERROR;
	}
	return error;
}


/**
 * @brief Returns a raw icon data buffer for this partition.
 *
 * If mounted, the icon is retrieved from the volume. Otherwise it is obtained
 * directly from the device.
 *
 * @param _data Set to a newly allocated buffer containing the icon data.
 * @param _size Set to the size of the icon data buffer.
 * @param _type Set to the MIME type code of the icon data.
 * @return \c B_OK on success, another error code otherwise.
 */
status_t
BPartition::GetIcon(uint8** _data, size_t* _size, type_code* _type) const
{
	if (_data == NULL || _size == NULL || _type == NULL)
		return B_BAD_VALUE;

	status_t error;

	if (IsMounted()) {
		// mounted: get the icon from the volume
		BVolume volume;
		error = GetVolume(&volume);
		if (error == B_OK)
			error = volume.GetIcon(_data, _size, _type);
	} else {
		// not mounted: retrieve the icon ourselves
		if (BDiskDevice* device = Device()) {
			BPath path;
			error = device->GetPath(&path);
			// get the icon
			if (error == B_OK)
				error = get_device_icon(path.Path(), _data, _size, _type);
		} else
			error = B_ERROR;
	}
	return error;
}


/**
 * @brief Returns the current or potential mount point path for this partition.
 *
 * If mounted, returns the actual mount point. If unmounted but contains a
 * file system, constructs a unique path under the root directory derived from
 * the volume name. Returns an error for partitions without a file system.
 *
 * @param mountPoint Pointer to a BPath to be set to the (potential) mount point.
 * @return \c B_OK on success, \c B_BAD_VALUE if no file system is present, or
 *         another error code on failure.
 */
status_t
BPartition::GetMountPoint(BPath* mountPoint) const
{
	if (mountPoint == NULL || !ContainsFileSystem())
		return B_BAD_VALUE;

	// if the partition is mounted, return the actual mount point
	BVolume volume;
	if (GetVolume(&volume) == B_OK) {
		BDirectory dir;
		status_t error = volume.GetRootDirectory(&dir);
		if (error == B_OK)
			error = mountPoint->SetTo(&dir, NULL);
		return error;
	}

	// partition not mounted
	// get the volume name
	const char* volumeName = ContentName();
	if (volumeName == NULL || strlen(volumeName) == 0)
		volumeName = Name();
	if (volumeName == NULL || strlen(volumeName) == 0)
		volumeName = "unnamed volume";

	// construct a path name from the volume name
	// replace '/'s and prepend a '/'
	BString mountPointPath(volumeName);
	mountPointPath.ReplaceAll('/', '-');
	mountPointPath.Insert("/", 0);

	// make the name unique
	BString basePath(mountPointPath);
	int counter = 1;
	while (true) {
		BEntry entry;
		status_t error = entry.SetTo(mountPointPath.String());
		if (error != B_OK)
			return error;

		if (!entry.Exists())
			break;
		mountPointPath = basePath;
		mountPointPath << counter;
		counter++;
	}

	return mountPoint->SetTo(mountPointPath.String());
}


/**
 * @brief Mounts the volume on this partition.
 *
 * The partition must contain a recognized file system and must not already
 * be mounted. If no mount point is given, one is created automatically under
 * the root directory based on the volume name.
 *
 * @param mountPoint Directory path at which to mount. May be \c NULL to
 *        auto-create a mount point.
 * @param mountFlags Mount flags; currently only \c B_MOUNT_READ_ONLY is defined.
 * @param parameters File-system-specific mount parameters.
 * @return \c B_OK on success, \c B_BUSY if already mounted, \c B_BAD_VALUE if
 *         no file system is present, or another error code.
 */
status_t
BPartition::Mount(const char* mountPoint, uint32 mountFlags, const char* parameters)
{
	if (IsMounted())
		return B_BUSY;

	if (!ContainsFileSystem())
		return B_BAD_VALUE;

	// get the partition path
	BPath partitionPath;
	status_t error = GetPath(&partitionPath);
	if (error != B_OK)
		return error;

	// create a mount point, if none is given
	bool deleteMountPoint = false;
	BPath mountPointPath, markerPath;
	if (!mountPoint) {
		// get a unique mount point
		error = GetMountPoint(&mountPointPath);
		if (error != B_OK)
			return error;

		mountPoint = mountPointPath.Path();
		markerPath = mountPointPath;
		markerPath.Append(skAutoCreatePrefix);

		// create the directory
		if (mkdir(mountPoint, S_IRWXU | S_IRWXG | S_IRWXO) < 0)
			return errno;

		if (mkdir(markerPath.Path(), S_IRWXU | S_IRWXG | S_IRWXO) < 0) {
			rmdir(mountPoint);
			return errno;
		}

		deleteMountPoint = true;
	}

	// mount the partition
	dev_t device = fs_mount_volume(mountPoint, partitionPath.Path(), NULL,
		mountFlags, parameters);

	// delete the mount point on error, if we created it
	if (device < B_OK && deleteMountPoint) {
		rmdir(markerPath.Path());
		rmdir(mountPoint);
	}

	// update object, if successful
	if (device >= 0)
		return Device()->Update();

	return device;
}


/**
 * @brief Unmounts the volume on this partition.
 *
 * The partition must currently be mounted. If the mount point was
 * auto-created by Mount(), it is removed after a successful unmount.
 *
 * @param unmountFlags Unmount flags; currently only \c B_FORCE_UNMOUNT is
 *        defined. Use with caution as it may cause data loss.
 * @return \c B_OK on success, \c B_BAD_VALUE if not mounted, or another error.
 */
status_t
BPartition::Unmount(uint32 unmountFlags)
{
	if (!IsMounted())
		return B_BAD_VALUE;

	// get the partition path
	BPath path;
	status_t status = GetMountPoint(&path);
	if (status != B_OK)
		return status;

	// unmount
	status = fs_unmount_volume(path.Path(), unmountFlags);

	// update object, if successful
	if (status == B_OK) {
		status = Device()->Update();

		// Check if we created this mount point on the fly.
		// If so, clean it up.
		BPath markerPath = path;
		markerPath.Append(skAutoCreatePrefix);
		BEntry pathEntry (markerPath.Path());
		if (pathEntry.InitCheck() == B_OK && pathEntry.Exists()) {
			rmdir(markerPath.Path());
			rmdir(path.Path());
		}
	}

	return status;
}


/**
 * @brief Returns the BDiskDevice this partition resides on.
 *
 * @return Pointer to the owning BDiskDevice.
 */
BDiskDevice*
BPartition::Device() const
{
	return fDevice;
}


/**
 * @brief Returns the parent partition of this partition, or NULL for top-level.
 *
 * @return Pointer to the parent BPartition, or \c NULL if this is a top-level partition.
 */
BPartition*
BPartition::Parent() const
{
	return fParent;
}


/**
 * @brief Returns the child partition at the given index.
 *
 * If a delegate is active, the delegate's child list is used.
 *
 * @param index Zero-based index of the child to retrieve.
 * @return Pointer to the child BPartition, or \c NULL if index is out of range.
 */
BPartition*
BPartition::ChildAt(int32 index) const
{
	if (fDelegate != NULL) {
		Delegate* child = fDelegate->ChildAt(index);
		return child ? child->Partition() : NULL;
	}

	return _ChildAt(index);
}


/**
 * @brief Returns the number of direct child partitions.
 *
 * @return The count of immediate child partitions.
 */
int32
BPartition::CountChildren() const
{
	if (fDelegate != NULL)
		return fDelegate->CountChildren();

	return _CountChildren();
}


/**
 * @brief Returns the total number of descendants (self + all children recursively).
 *
 * @return The total descendant count including this partition.
 */
int32
BPartition::CountDescendants() const
{
	int32 count = 1;
	for (int32 i = 0; BPartition* child = ChildAt(i); i++)
		count += child->CountDescendants();
	return count;
}


/**
 * @brief Finds a descendant partition by its ID.
 *
 * @param id The partition ID to search for.
 * @return Pointer to the matching BPartition, or \c NULL if not found.
 */
BPartition*
BPartition::FindDescendant(partition_id id) const
{
	IDFinderVisitor visitor(id);
	return VisitEachDescendant(&visitor);
}


/**
 * @brief Retrieves partitioning information for this partition.
 *
 * Requires the device to be prepared for modifications (delegate present).
 *
 * @param info Pointer to a pre-allocated BPartitioningInfo to be filled.
 * @return \c B_OK on success, \c B_BAD_VALUE if \a info is \c NULL,
 *         \c B_NO_INIT if no delegate is present.
 */
status_t
BPartition::GetPartitioningInfo(BPartitioningInfo* info) const
{
	if (!info)
		return B_BAD_VALUE;
	if (fDelegate == NULL)
		return B_NO_INIT;

	return fDelegate->GetPartitioningInfo(info);
}


/**
 * @brief Iterates through all direct children using a visitor.
 *
 * @param visitor The visitor to invoke for each child partition.
 * @return The child at which iteration was terminated, or \c NULL.
 */
BPartition*
BPartition::VisitEachChild(BDiskDeviceVisitor* visitor) const
{
	if (visitor != NULL) {
		int32 level = _Level();
		for (int32 i = 0; BPartition* child = ChildAt(i); i++) {
			if (child->_AcceptVisitor(visitor, level))
				return child;
		}
	}
	return NULL;
}


/**
 * @brief Pre-order traversal of this partition and all its descendants.
 *
 * @param visitor The visitor to invoke for each partition in the subtree.
 * @return The partition at which iteration was terminated, or \c NULL.
 */
BPartition*
BPartition::VisitEachDescendant(BDiskDeviceVisitor* visitor) const
{
	if (visitor != NULL)
		return const_cast<BPartition*>(this)->_VisitEachDescendant(visitor);
	return NULL;
}


/**
 * @brief Returns whether this partition can be defragmented.
 *
 * @param whileMounted If non-NULL, set to \c true if defragmentation is
 *        possible while the partition is mounted.
 * @return \c true if defragmentation is supported.
 */
bool
BPartition::CanDefragment(bool* whileMounted) const
{
	return _SupportsOperation(B_DISK_SYSTEM_SUPPORTS_DEFRAGMENTING,
		B_DISK_SYSTEM_SUPPORTS_DEFRAGMENTING_WHILE_MOUNTED, whileMounted);
}


/**
 * @brief Defragments this partition's file system.
 *
 * @return \c B_OK on success, \c B_NO_INIT if no delegate is present.
 */
status_t
BPartition::Defragment() const
{
	if (fDelegate == NULL)
		return B_NO_INIT;

	return fDelegate->Defragment();
}


/**
 * @brief Returns whether this partition can be repaired or checked.
 *
 * @param checkOnly If \c true, queries check-only capability; otherwise
 *        queries full repair capability.
 * @param whileMounted If non-NULL, set to \c true if the operation is
 *        supported while mounted.
 * @return \c true if the requested repair/check operation is supported.
 */
bool
BPartition::CanRepair(bool checkOnly, bool* whileMounted) const
{
	uint32 flag;
	uint32 whileMountedFlag;
	if (checkOnly) {
		flag = B_DISK_SYSTEM_SUPPORTS_CHECKING;
		whileMountedFlag = B_DISK_SYSTEM_SUPPORTS_CHECKING_WHILE_MOUNTED;
	} else {
		flag = B_DISK_SYSTEM_SUPPORTS_REPAIRING;
		whileMountedFlag = B_DISK_SYSTEM_SUPPORTS_REPAIRING_WHILE_MOUNTED;
	}

	return _SupportsOperation(flag, whileMountedFlag, whileMounted);
}


/**
 * @brief Checks or repairs this partition's file system.
 *
 * @param checkOnly If \c true, only check the file system; if \c false, repair it.
 * @return \c B_OK on success, \c B_NO_INIT if no delegate is present.
 */
status_t
BPartition::Repair(bool checkOnly) const
{
	if (fDelegate == NULL)
		return B_NO_INIT;

	return fDelegate->Repair(checkOnly);
}


/**
 * @brief Returns whether this partition can be resized.
 *
 * @param canResizeContents If non-NULL, set to indicate whether content can
 *        also be resized (unused parameter, reserved for future use).
 * @param whileMounted If non-NULL, set to \c true if resizing is possible
 *        while the partition is mounted.
 * @return \c true if the partition can be resized.
 */
bool
BPartition::CanResize(bool* canResizeContents, bool* whileMounted) const
{
	BPartition* parent = Parent();
	if (parent == NULL)
		return false;

	if (!parent->_SupportsChildOperation(this,
			B_DISK_SYSTEM_SUPPORTS_RESIZING_CHILD)) {
		return false;
	}

	if (!_HasContent())
		return true;

	return _SupportsOperation(B_DISK_SYSTEM_SUPPORTS_RESIZING,
		B_DISK_SYSTEM_SUPPORTS_RESIZING_WHILE_MOUNTED, whileMounted);
}


/**
 * @brief Validates and adjusts a proposed new size for this partition.
 *
 * @param size In/out parameter for the proposed size; may be adjusted to a
 *        valid value by the disk system.
 * @return \c B_OK if the size is valid (possibly adjusted), or an error code.
 */
status_t
BPartition::ValidateResize(off_t* size) const
{
	BPartition* parent = Parent();
	if (parent == NULL || fDelegate == NULL)
		return B_NO_INIT;

	status_t error = parent->fDelegate->ValidateResizeChild(fDelegate, size);
	if (error != B_OK)
		return error;

	if (_HasContent()) {
		// TODO: We would actually need the parameter for the content size.
		off_t contentSize = *size;
		error = fDelegate->ValidateResize(&contentSize);
		if (error != B_OK)
			return error;

		if (contentSize > *size)
			return B_BAD_VALUE;
	}

	return B_OK;
}


/**
 * @brief Resizes this partition to the given size.
 *
 * When shrinking, content is resized first; when growing, content is resized
 * last. Requires the device to be prepared for modifications.
 *
 * @param size The desired new size in bytes.
 * @return \c B_OK on success, \c B_NO_INIT if parent or delegate is missing,
 *         or another error code.
 */
status_t
BPartition::Resize(off_t size)
{
	BPartition* parent = Parent();
	if (!parent || !fDelegate)
		return B_NO_INIT;

	status_t error;
	off_t contentSize = size;
	if (_HasContent()) {
		error = fDelegate->ValidateResize(&contentSize);
		if (error != B_OK)
			return error;

		if (contentSize > size)
			return B_BAD_VALUE;
	}

	// If shrinking the partition, resize content first, otherwise last.
	bool shrink = Size() > size;

	if (shrink && ContentType() != NULL) {
		error = fDelegate->Resize(contentSize);
		if (error != B_OK)
			return error;
	}

	error = parent->fDelegate->ResizeChild(fDelegate, size);
	if (error != B_OK)
		return error;

	if (!shrink && ContentType() != NULL) {
		error = fDelegate->Resize(contentSize);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Returns whether this partition can be moved.
 *
 * @param unmovableDescendants List to be filled with descendants that cannot
 *        be moved.
 * @param movableOnlyIfUnmounted List to be filled with descendants that can
 *        only be moved if unmounted.
 * @return \c true if the partition can be moved.
 */
bool
BPartition::CanMove(BObjectList<BPartition>* unmovableDescendants,
	BObjectList<BPartition>* movableOnlyIfUnmounted) const
{
	BPartition* parent = Parent();
	if (parent == NULL || fDelegate == NULL)
		return false;

	if (!parent->_SupportsChildOperation(this,
			B_DISK_SYSTEM_SUPPORTS_MOVING_CHILD)) {
		return false;
	}

	bool whileMounted;
	bool movable = _SupportsOperation(B_DISK_SYSTEM_SUPPORTS_MOVING,
		B_DISK_SYSTEM_SUPPORTS_MOVING_WHILE_MOUNTED, &whileMounted);
	if (!movable)
		return false;

	if (!whileMounted)
		movableOnlyIfUnmounted->AddItem(const_cast<BPartition*>(this));

	// collect descendent partitions
	// TODO: ...
// TODO: Currently there's no interface for asking descendents. They'll still
// have the same offset (relative to their parent) after moving. The only thing
// we really have to ask is whether they need to be unmounted.

	return true;
}


/**
 * @brief Validates and adjusts a proposed new offset for this partition.
 *
 * @param offset In/out parameter for the proposed offset; may be adjusted.
 * @return \c B_OK if valid (possibly adjusted), or an error code.
 */
status_t
BPartition::ValidateMove(off_t* offset) const
{
	BPartition* parent = Parent();
	if (parent == NULL || fDelegate == NULL)
		return B_NO_INIT;

	status_t error = parent->fDelegate->ValidateMoveChild(fDelegate, offset);
	if (error != B_OK)
		return error;

	if (_HasContent()) {
		off_t contentOffset = *offset;
		error = fDelegate->ValidateMove(&contentOffset);
		if (error != B_OK)
			return error;

		if (contentOffset != *offset)
			return B_BAD_VALUE;
	}

	return B_OK;
}


/**
 * @brief Moves this partition to a new offset within its parent.
 *
 * @param offset The new byte offset for the partition's start.
 * @return \c B_OK on success, \c B_NO_INIT if parent or delegate is missing.
 */
status_t
BPartition::Move(off_t offset)
{
	BPartition* parent = Parent();
	if (parent == NULL || fDelegate == NULL)
		return B_NO_INIT;

	status_t error = parent->fDelegate->MoveChild(fDelegate, offset);
	if (error != B_OK)
		return error;

	if (_HasContent()) {
		error = fDelegate->Move(offset);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Returns whether the name of this partition can be changed.
 *
 * @return \c true if the parent disk system supports renaming child partitions.
 */
bool
BPartition::CanSetName() const
{
	BPartition* parent = Parent();
	if (parent == NULL || fDelegate == NULL)
		return false;

	return parent->_SupportsChildOperation(this,
		B_DISK_SYSTEM_SUPPORTS_SETTING_NAME);
}


/**
 * @brief Validates and adjusts a proposed new name for this partition.
 *
 * @param name In/out BString; may be adjusted to a valid name by the disk system.
 * @return \c B_OK on success, or an error code.
 */
status_t
BPartition::ValidateSetName(BString* name) const
{
	BPartition* parent = Parent();
	if (parent == NULL || fDelegate == NULL)
		return B_NO_INIT;

	return parent->fDelegate->ValidateSetName(fDelegate, name);
}


/**
 * @brief Sets the name of this partition.
 *
 * @param name The new name for this partition.
 * @return \c B_OK on success, \c B_NO_INIT if parent or delegate is missing.
 */
status_t
BPartition::SetName(const char* name)
{
	BPartition* parent = Parent();
	if (parent == NULL || fDelegate == NULL)
		return B_NO_INIT;

	return parent->fDelegate->SetName(fDelegate, name);
}


/**
 * @brief Returns whether the content name (volume name) can be changed.
 *
 * @param whileMounted If non-NULL, set to \c true if it can be changed while mounted.
 * @return \c true if the content name can be set.
 */
bool
BPartition::CanSetContentName(bool* whileMounted) const
{
	return _SupportsOperation(B_DISK_SYSTEM_SUPPORTS_SETTING_CONTENT_NAME,
		B_DISK_SYSTEM_SUPPORTS_SETTING_CONTENT_NAME_WHILE_MOUNTED,
		whileMounted);
}


/**
 * @brief Validates and adjusts a proposed new content (volume) name.
 *
 * @param name In/out BString; may be adjusted to a valid name.
 * @return \c B_OK on success, \c B_NO_INIT if no delegate is present.
 */
status_t
BPartition::ValidateSetContentName(BString* name) const
{
	if (fDelegate == NULL)
		return B_NO_INIT;

	return fDelegate->ValidateSetContentName(name);
}


/**
 * @brief Sets the content (volume) name of this partition.
 *
 * @param name The new volume name.
 * @return \c B_OK on success, \c B_NO_INIT if no delegate is present.
 */
status_t
BPartition::SetContentName(const char* name)
{
	if (fDelegate == NULL)
		return B_NO_INIT;

	return fDelegate->SetContentName(name);
}


/**
 * @brief Returns whether the type of this partition can be changed.
 *
 * @return \c true if the parent disk system supports changing the partition type.
 */
bool
BPartition::CanSetType() const
{
	BPartition* parent = Parent();
	if (parent == NULL)
		return false;

	return parent->_SupportsChildOperation(this,
		B_DISK_SYSTEM_SUPPORTS_SETTING_TYPE);
}


/**
 * @brief Validates a proposed new type string for this partition.
 *
 * @param type The proposed type string to validate.
 * @return \c B_OK if the type is valid, or an error code.
 */
status_t
BPartition::ValidateSetType(const char* type) const
{
	BPartition* parent = Parent();
	if (parent == NULL || parent->fDelegate == NULL || fDelegate == NULL)
		return B_NO_INIT;

	return parent->fDelegate->ValidateSetType(fDelegate, type);
}


/**
 * @brief Sets the type of this partition.
 *
 * @param type The new type string for this partition.
 * @return \c B_OK on success, \c B_NO_INIT if parent or delegate is missing.
 */
status_t
BPartition::SetType(const char* type)
{
	BPartition* parent = Parent();
	if (parent == NULL || parent->fDelegate == NULL || fDelegate == NULL)
		return B_NO_INIT;

	return parent->fDelegate->SetType(fDelegate, type);
}


/**
 * @brief Returns whether the parameters of this partition can be edited.
 *
 * @return \c true if the parent disk system supports setting partition parameters.
 */
bool
BPartition::CanEditParameters() const
{
	BPartition* parent = Parent();
	if (parent == NULL)
		return false;

	return parent->_SupportsChildOperation(this,
		B_DISK_SYSTEM_SUPPORTS_SETTING_PARAMETERS);
}


/**
 * @brief Returns a parameter editor for creating or modifying partition parameters.
 *
 * For B_CREATE_PARAMETER_EDITOR, the editor is retrieved from this partition's
 * delegate. For other types, it is retrieved from the parent's delegate.
 *
 * @param type The type of parameter editor to retrieve.
 * @param editor Set to the created BPartitionParameterEditor on success.
 * @return \c B_OK on success, \c B_NO_INIT if required delegates are missing.
 */
status_t
BPartition::GetParameterEditor(B_PARAMETER_EDITOR_TYPE type,
	BPartitionParameterEditor** editor)
{
	// When creating a new partition, this will be called for parent inside
	// which we are creating a partition.
	// When modifying an existing partition, this will be called for the
	// partition itself, but the parameters are in fact managed by the parent
	// (see SetParameters)
	if (type == B_CREATE_PARAMETER_EDITOR) {
		if (fDelegate == NULL)
			return B_NO_INIT;
		return fDelegate->GetParameterEditor(type, editor);
	} else {
		BPartition* parent = Parent();
		if (parent == NULL || parent->fDelegate == NULL)
			return B_NO_INIT;

		return parent->fDelegate->GetParameterEditor(type, editor);
	}
}


/**
 * @brief Sets the parameters for this partition (managed by the parent disk system).
 *
 * @param parameters The new parameters string.
 * @return \c B_OK on success, \c B_NO_INIT if parent or delegate is missing.
 */
status_t
BPartition::SetParameters(const char* parameters)
{
	BPartition* parent = Parent();
	if (parent == NULL || parent->fDelegate == NULL || fDelegate == NULL)
		return B_NO_INIT;

	return parent->fDelegate->SetParameters(fDelegate, parameters);
}


/**
 * @brief Returns whether the content parameters of this partition can be edited.
 *
 * @param whileMounted If non-NULL, set to \c true if editing is possible while mounted.
 * @return \c true if the content parameters can be set.
 */
bool
BPartition::CanEditContentParameters(bool* whileMounted) const
{
	return _SupportsOperation(B_DISK_SYSTEM_SUPPORTS_SETTING_CONTENT_PARAMETERS,
		B_DISK_SYSTEM_SUPPORTS_SETTING_CONTENT_PARAMETERS_WHILE_MOUNTED,
		whileMounted);
}


/**
 * @brief Sets the content parameters for this partition's disk system.
 *
 * @param parameters The new content parameters string.
 * @return \c B_OK on success, \c B_NO_INIT if no delegate is present.
 */
status_t
BPartition::SetContentParameters(const char* parameters)
{
	if (fDelegate == NULL)
		return B_NO_INIT;

	return fDelegate->SetContentParameters(parameters);
}


/**
 * @brief Iterates through the supported types for this partition.
 *
 * Requires the device to be prepared for modifications (delegate present).
 *
 * @param cookie Iteration cookie; initialize to 0 before the first call.
 * @param type Set to the next supported type string on each successful call.
 * @return \c B_OK while types remain, \c B_ENTRY_NOT_FOUND when exhausted.
 */
status_t
BPartition::GetNextSupportedType(int32* cookie, BString* type) const
{
	TRACE("%p->BPartition::GetNextSupportedType(%ld)\n", this, *cookie);

	BPartition* parent = Parent();
	if (parent == NULL || fDelegate == NULL) {
		TRACE("  not prepared (parent: %p, fDelegate: %p)!\n", parent,
			fDelegate);
		return B_NO_INIT;
	}

	return parent->fDelegate->GetNextSupportedChildType(fDelegate, cookie,
		type);
}


/**
 * @brief Iterates through the supported child types for this partition.
 *
 * @param cookie Iteration cookie; initialize to 0 before the first call.
 * @param type Set to the next supported child type string on each successful call.
 * @return \c B_OK while types remain, \c B_ENTRY_NOT_FOUND when exhausted.
 */
status_t
BPartition::GetNextSupportedChildType(int32* cookie, BString* type) const
{
	TRACE("%p->BPartition::GetNextSupportedChildType(%ld)\n", this, *cookie);

	if (fDelegate == NULL) {
		TRACE("  not prepared!\n");
		return B_NO_INIT;
	}

	return fDelegate->GetNextSupportedChildType(NULL, cookie, type);
}


/**
 * @brief Returns whether this partition is a sub-system of the named disk system.
 *
 * @param diskSystem The short name of the disk system to check against.
 * @return \c true if this partition is identified as a sub-system of \a diskSystem.
 */
bool
BPartition::BPartition::IsSubSystem(const char* diskSystem) const
{
	BPartition* parent = Parent();
	if (parent == NULL || fDelegate == NULL)
		return false;

	return parent->fDelegate->IsSubSystem(fDelegate, diskSystem);
}


/**
 * @brief Returns whether this partition can be initialized with a given disk system.
 *
 * @param diskSystem The short name of the disk system to check.
 * @return \c true if initialization with \a diskSystem is possible.
 */
bool
BPartition::CanInitialize(const char* diskSystem) const
{
	if (Size() == 0 || BlockSize() == 0 || fDelegate == NULL)
		return false;

	return fDelegate->CanInitialize(diskSystem);
}


/**
 * @brief Validates the parameters for initializing this partition with a disk system.
 *
 * @param diskSystem The short name of the disk system to use.
 * @param name In/out BString for the volume name; may be adjusted.
 * @param parameters Disk-system-specific initialization parameters.
 * @return \c B_OK if valid, or an error code.
 */
status_t
BPartition::ValidateInitialize(const char* diskSystem, BString* name,
	const char* parameters)
{
	if (fDelegate == NULL)
		return B_NO_INIT;

	return fDelegate->ValidateInitialize(diskSystem, name, parameters);
}


/**
 * @brief Initializes this partition with the specified disk system.
 *
 * @param diskSystem The short name of the disk system to use.
 * @param name The volume name to assign.
 * @param parameters Disk-system-specific initialization parameters.
 * @return \c B_OK on success, \c B_NO_INIT if no delegate is present.
 */
status_t
BPartition::Initialize(const char* diskSystem, const char* name,
	const char* parameters)
{
	if (fDelegate == NULL)
		return B_NO_INIT;

	return fDelegate->Initialize(diskSystem, name, parameters);
}


/**
 * @brief Removes all disk system content from this partition.
 *
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BPartition::Uninitialize()
{
	return fDelegate->Uninitialize();
}


/**
 * @brief Returns whether a new child partition can be created within this partition.
 *
 * @return \c true if the disk system supports creating child partitions here.
 */
bool
BPartition::CanCreateChild() const
{
	return _SupportsChildOperation(NULL, B_DISK_SYSTEM_SUPPORTS_CREATING_CHILD);
}


/**
 * @brief Validates the parameters for creating a new child partition.
 *
 * @param offset In/out parameter for the proposed offset; may be adjusted.
 * @param size In/out parameter for the proposed size; may be adjusted.
 * @param type The desired partition type string.
 * @param name In/out BString for the partition name; may be adjusted.
 * @param parameters Disk-system-specific creation parameters.
 * @return \c B_OK if valid (possibly adjusted), or an error code.
 */
status_t
BPartition::ValidateCreateChild(off_t* offset, off_t* size, const char* type,
	BString* name, const char* parameters) const
{
	if (fDelegate == NULL)
		return B_NO_INIT;

	return fDelegate->ValidateCreateChild(offset, size, type, name, parameters);
}


/**
 * @brief Creates a new child partition within this partition.
 *
 * @param offset The byte offset at which to create the partition.
 * @param size The size of the new partition in bytes.
 * @param type The partition type string.
 * @param name The partition name.
 * @param parameters Disk-system-specific creation parameters.
 * @param child Set to the newly created BPartition on success.
 * @return \c B_OK on success, \c B_NO_INIT if no delegate is present.
 */
status_t
BPartition::CreateChild(off_t offset, off_t size, const char* type,
	const char* name, const char* parameters, BPartition** child)
{
	if (fDelegate == NULL)
		return B_NO_INIT;

	return fDelegate->CreateChild(offset, size, type, name, parameters, child);
}


/**
 * @brief Returns whether the child at the given index can be deleted.
 *
 * @param index The index of the child partition to check.
 * @return \c true if the child can be deleted.
 */
bool
BPartition::CanDeleteChild(int32 index) const
{
	BPartition* child = ChildAt(index);
	if (fDelegate == NULL || child == NULL)
		return false;

	return _SupportsChildOperation(child,
		B_DISK_SYSTEM_SUPPORTS_DELETING_CHILD);
}


/**
 * @brief Deletes the child partition at the given index.
 *
 * @param index The index of the child partition to delete.
 * @return \c B_OK on success, \c B_NO_INIT if no delegate is present,
 *         \c B_BAD_VALUE if the index is invalid.
 */
status_t
BPartition::DeleteChild(int32 index)
{
	if (fDelegate == NULL)
		return B_NO_INIT;

	BPartition* child = ChildAt(index);
	if (child == NULL || child->Parent() != this)
		return B_BAD_VALUE;

	return fDelegate->DeleteChild(child->fDelegate);
}


/**
 * @brief Privatized copy constructor to avoid usage.
 */
BPartition::BPartition(const BPartition &)
{
}


/**
 * @brief Privatized assignment operator to avoid usage.
 */
BPartition &
BPartition::operator=(const BPartition &)
{
	return *this;
}


/**
 * @brief Initializes this partition object from raw partition data.
 *
 * Recursively creates child BPartition objects for all child partitions.
 * On failure, _Unset() is called to clean up partial state.
 *
 * @param device The owning BDiskDevice.
 * @param parent The parent BPartition, or \c NULL for top-level partitions.
 * @param data The raw user_partition_data structure for this partition.
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BPartition::_SetTo(BDiskDevice* device, BPartition* parent,
	user_partition_data* data)
{
	_Unset();
	if (device == NULL || data == NULL)
		return B_BAD_VALUE;

	fPartitionData = data;
	fDevice = device;
	fParent = parent;
	fPartitionData->user_data = this;

	// create and init children
	status_t error = B_OK;
	for (int32 i = 0; error == B_OK && i < fPartitionData->child_count; i++) {
		BPartition* child = new(nothrow) BPartition;
		if (child) {
			error = child->_SetTo(fDevice, this, fPartitionData->children[i]);
			if (error != B_OK)
				delete child;
		} else
			error = B_NO_MEMORY;
	}

	// cleanup on error
	if (error != B_OK)
		_Unset();
	return error;
}


/**
 * @brief Releases all resources and resets this object to an uninitialized state.
 *
 * Deletes all child BPartition objects and clears all data pointers.
 */
void
BPartition::_Unset()
{
	// delete children
	if (fPartitionData != NULL) {
		for (int32 i = 0; i < fPartitionData->child_count; i++) {
			if (BPartition* child = ChildAt(i))
				delete child;
		}
		fPartitionData->user_data = NULL;
	}

	fDevice = NULL;
	fParent = NULL;
	fPartitionData = NULL;
	fDelegate = NULL;
}


/**
 * @brief Removes child partitions that no longer exist in the updated data.
 *
 * Compares current children against the new partition data and removes any
 * that are no longer present. Also recursively removes obsolete descendants.
 *
 * @param data The updated partition data to compare against.
 * @param updated Set to \c true if any partitions were removed.
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BPartition::_RemoveObsoleteDescendants(user_partition_data* data, bool* updated)
{
	// remove all children not longer persistent
	// Not exactly efficient: O(n^2), considering BList::RemoveItem()
	// O(1). We could do better (O(n*log(n))), when sorting the arrays before,
	// but then the list access is more random and we had to find the
	// BPartition to remove, which makes the list operation definitely O(n).
	int32 count = CountChildren();
	for (int32 i = count - 1; i >= 0; i--) {
		BPartition* child = ChildAt(i);
		bool found = false;
		for (int32 k = data->child_count - 1; k >= 0; k--) {
			if (data->children[k]->id == child->ID()) {
				// found partition: ask it to remove its obsolete descendants
				found = true;
				status_t error = child->_RemoveObsoleteDescendants(
					data->children[k], updated);
				if (error != B_OK)
					return error;

				// set the user data to the BPartition object to find it
				// quicker later
				data->children[k]->user_data = child;
				break;
			}
		}

		// if partition is obsolete, remove it
		if (!found) {
			*updated = true;
			_RemoveChild(i);
		}
	}
	return B_OK;
}


/**
 * @brief Updates this partition's data from newly fetched raw partition data.
 *
 * Replaces the current partition data, detects changes, and recursively
 * updates or creates child BPartition objects.
 *
 * @param data The new raw partition data.
 * @param updated Set to \c true if any changes were detected.
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BPartition::_Update(user_partition_data* data, bool* updated)
{
	user_partition_data* oldData = fPartitionData;
	fPartitionData = data;
	// check for changes
	if (data->offset != oldData->offset
		|| data->size != oldData->size
		|| data->block_size != oldData->block_size
		|| data->physical_block_size != oldData->physical_block_size
		|| data->status != oldData->status
		|| data->flags != oldData->flags
		|| data->volume != oldData->volume
		|| data->disk_system != oldData->disk_system	// not needed
		|| compare_string(data->name, oldData->name)
		|| compare_string(data->content_name, oldData->content_name)
		|| compare_string(data->type, oldData->type)
		|| compare_string(data->content_type, oldData->content_type)
		|| compare_string(data->parameters, oldData->parameters)
		|| compare_string(data->content_parameters,
				oldData->content_parameters)) {
		*updated = true;
	}

	// add new children and update existing ones
	status_t error = B_OK;
	for (int32 i = 0; i < data->child_count; i++) {
		user_partition_data* childData = data->children[i];
		BPartition* child = (BPartition*)childData->user_data;
		if (child) {
			// old partition
			error = child->_Update(childData, updated);
			if (error != B_OK)
				return error;
		} else {
			// new partition
			*updated = true;
			child = new(nothrow) BPartition;
			if (!child)
				return B_NO_MEMORY;

			error = child->_SetTo(fDevice, this, childData);
			if (error != B_OK) {
				delete child;
				return error;
			}

			childData->user_data = child;
		}
	}
	return error;
}


/**
 * @brief Removes the child partition at the given index and deletes it.
 *
 * Compacts the children array after removal.
 *
 * @param index The index of the child to remove.
 */
void
BPartition::_RemoveChild(int32 index)
{
	int32 count = CountChildren();
	if (!fPartitionData || index < 0 || index >= count)
		return;

	// delete the BPartition and its children
	delete ChildAt(index);

	// compact the children array
	for (int32 i = index + 1; i < count; i++)
		fPartitionData->children[i - 1] = fPartitionData->children[i];
	fPartitionData->child_count--;
}


/**
 * @brief Returns the child BPartition at the given index via raw partition data.
 *
 * @param index Zero-based index of the child to retrieve.
 * @return Pointer to the child BPartition, or \c NULL if index is out of range.
 */
BPartition*
BPartition::_ChildAt(int32 index) const
{
	if (index < 0 || index >= fPartitionData->child_count)
		return NULL;
	return (BPartition*)fPartitionData->children[index]->user_data;
}


/**
 * @brief Returns the number of direct children via the raw partition data.
 *
 * @return The child_count from the underlying partition data.
 */
int32
BPartition::_CountChildren() const
{
	return fPartitionData->child_count;
}


/**
 * @brief Recursively counts this partition and all its descendants.
 *
 * @return Total count of this partition plus all descendants.
 */
int32
BPartition::_CountDescendants() const
{
	int32 count = 1;
	for (int32 i = 0; BPartition* child = _ChildAt(i); i++)
		count += child->_CountDescendants();
	return count;
}


/**
 * @brief Returns the depth level of this partition in the partition tree.
 *
 * The root partition (no parent) is at level 0.
 *
 * @return The depth level of this partition.
 */
int32
BPartition::_Level() const
{
	int32 level = 0;
	const BPartition* ancestor = this;
	while ((ancestor = ancestor->Parent()))
		level++;
	return level;
}


/**
 * @brief Dispatches the visitor to this partition's Visit(BPartition*, int32) overload.
 *
 * @param visitor The visitor to invoke.
 * @param level The depth level at which this partition resides.
 * @return The return value of visitor->Visit(this, level).
 */
bool
BPartition::_AcceptVisitor(BDiskDeviceVisitor* visitor, int32 level)
{
	return visitor->Visit(this, level);
}


/**
 * @brief Internal recursive implementation of VisitEachDescendant.
 *
 * Visits this partition first (pre-order), then recursively visits all children.
 *
 * @param visitor The visitor to invoke.
 * @param level The depth level; if negative, computed from _Level().
 * @return The partition at which iteration was terminated, or \c NULL.
 */
BPartition*
BPartition::_VisitEachDescendant(BDiskDeviceVisitor* visitor, int32 level)
{
	if (level < 0)
		level = _Level();
	if (_AcceptVisitor(visitor, level))
		return this;
	for (int32 i = 0; BPartition* child = ChildAt(i); i++) {
		if (BPartition* result = child->_VisitEachDescendant(visitor,
				level + 1)) {
			return result;
		}
	}
	return NULL;
}


/**
 * @brief Returns the effective partition data, preferring delegate data if available.
 *
 * When a delegate is active (device prepared for modifications), the delegate's
 * partition data is used; otherwise the raw fPartitionData is returned.
 *
 * @return Pointer to the effective user_partition_data structure.
 */
const user_partition_data*
BPartition::_PartitionData() const
{
	return fDelegate ? fDelegate->PartitionData() : fPartitionData;
}


/**
 * @brief Returns whether this partition has any disk system content.
 *
 * @return \c true if ContentType() returns a non-NULL value.
 */
bool
BPartition::_HasContent() const
{
	return ContentType() != NULL;
}


/**
 * @brief Tests whether this partition's disk system supports a given operation.
 *
 * @param flag The capability flag to test.
 * @param whileMountedFlag The while-mounted variant of the capability flag.
 * @param whileMounted If non-NULL, set to \c true if the operation is supported
 *        while the partition is mounted.
 * @return \c true if the operation is supported.
 */
bool
BPartition::_SupportsOperation(uint32 flag, uint32 whileMountedFlag,
	bool* whileMounted) const
{
	if (fDelegate == NULL)
		return false;

	uint32 supported = fDelegate->SupportedOperations(flag | whileMountedFlag);

	if (whileMounted)
		*whileMounted = supported & whileMountedFlag;

	return (supported & flag) != 0;
}


/**
 * @brief Tests whether this partition's disk system supports a child operation.
 *
 * @param child The child partition for which to check support, or \c NULL
 *        to check generic child support.
 * @param flag The child capability flag to test.
 * @return \c true if the child operation is supported.
 */
bool
BPartition::_SupportsChildOperation(const BPartition* child, uint32 flag) const
{
	if (fDelegate == NULL || (child != NULL && child->fDelegate == NULL))
		return false;

	uint32 supported = fDelegate->SupportedChildOperations(
		child != NULL ? child->fDelegate : NULL, flag);

	return (supported & flag) != 0;
}


/**
 * @brief Creates delegates for this partition and all its descendants.
 *
 * Called during PrepareModifications() to set up the shadow modification
 * hierarchy. Each partition gets a Delegate that mediates all modification calls.
 *
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BPartition::_CreateDelegates()
{
	if (fDelegate != NULL || fPartitionData == NULL)
		return B_NO_INIT;

	// create and init delegate
	fDelegate = new(nothrow) Delegate(this);
	if (fDelegate == NULL)
		return B_NO_MEMORY;

	status_t error = fDelegate->InitHierarchy(fPartitionData,
		fParent != NULL ? fParent->fDelegate : NULL);
	if (error != B_OK)
		return error;

	// create child delegates
	int32 count = _CountChildren();
	for (int32 i = 0; i < count; i++) {
		BPartition* child = _ChildAt(i);
		error = child->_CreateDelegates();
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Finalizes delegate initialization after the full hierarchy is created.
 *
 * Called after _CreateDelegates() completes for the entire tree. Allows each
 * delegate to perform initialization that requires knowledge of adjacent delegates.
 *
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BPartition::_InitDelegates()
{
	// init delegate
	status_t error = fDelegate->InitAfterHierarchy();
	if (error != B_OK)
		return error;

	// recursively init child delegates
	int32 count = CountChildren();
	for (int32 i = 0; i < count; i++) {
		error = ChildAt(i)->_InitDelegates();
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Recursively deletes all delegates in this partition's subtree.
 *
 * Children are deleted first (post-order). Partitions with no backing
 * physical data (fPartitionData == NULL) delete themselves when their
 * delegate is deleted.
 */
void
BPartition::_DeleteDelegates()
{
	// recursively delete child delegates
	int32 count = CountChildren();
	for (int32 i = count - 1; i >= 0; i--)
		ChildAt(i)->_DeleteDelegates();

	// delete delegate
	delete fDelegate;
	fDelegate = NULL;

	// Commit suicide, if the delegate was our only link to reality (i.e.
	// there's no physically existing partition we represent).
	if (fPartitionData == NULL)
		delete this;
}


/**
 * @brief Returns whether this partition has uncommitted modifications.
 *
 * @return \c true if the delegate reports pending modifications, \c false if
 *         no delegate is present or no modifications have been made.
 */
bool
BPartition::_IsModified() const
{
	if (fDelegate == NULL)
		return false;

	return fDelegate->IsModified();
}
