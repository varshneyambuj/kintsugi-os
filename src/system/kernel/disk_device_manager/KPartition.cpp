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
 *   Copyright 2009, Bryce Groff, bgroff@hawaii.edu.
 *   Copyright 2004-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2003-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file KPartition.cpp
 * @brief Kernel representation of a single partition on a disk device.
 *
 * KPartition stores the geometry (offset, size, block size), type, name,
 * content type, parameters, and child partitions. It is the node type in
 * the partition tree managed by KDiskDeviceManager. Provides the interface
 * for partitioning system add-ons to create, delete, resize, and move
 * partition entries.
 *
 * @see KDiskDevice.cpp, KDiskDeviceManager.cpp, KPartitioningSystem.cpp
 */


#include <KPartition.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <DiskDeviceRoster.h>
#include <Drivers.h>
#include <Errors.h>
#include <fs_volume.h>
#include <KernelExport.h>
#include <StackOrHeapArray.h>

#include <ddm_userland_interface.h>
#include <fs/devfs.h>
#include <KDiskDevice.h>
#include <KDiskDeviceManager.h>
#include <KDiskDeviceUtils.h>
#include <KDiskSystem.h>
#include <KPartitionListener.h>
#include <KPartitionVisitor.h>
#include <KPath.h>
#include <util/kernel_cpp.h>
#include <VectorSet.h>
#include <vfs.h>

#include "UserDataWriter.h"


using namespace std;


// debugging
//#define DBG(x)
#define DBG(x) x
#define OUT dprintf


struct KPartition::ListenerSet : VectorSet<KPartitionListener*> {};


int32 KPartition::sNextID = 0;


/**
 * @brief Construct a new KPartition with an optional explicit partition ID.
 *
 * Initialises all geometry fields to zero, sets the status to
 * B_PARTITION_UNRECOGNIZED, marks the partition busy, and assigns a
 * unique monotonically increasing ID if @p id is negative.
 *
 * @param id  Desired partition ID, or a negative value to auto-assign one.
 */
KPartition::KPartition(partition_id id)
	:
	fPartitionData(),
	fChildren(),
	fDevice(NULL),
	fParent(NULL),
	fDiskSystem(NULL),
	fDiskSystemPriority(-1),
	fListeners(NULL),
	fChangeFlags(0),
	fChangeCounter(0),
	fAlgorithmData(0),
	fReferenceCount(0),
	fObsolete(false),
	fPublishedName(NULL)
{
	fPartitionData.id = id >= 0 ? id : _NextID();
	fPartitionData.offset = 0;
	fPartitionData.size = 0;
	fPartitionData.content_size = 0;
	fPartitionData.block_size = 0;
	fPartitionData.physical_block_size = 0;
	fPartitionData.child_count = 0;
	fPartitionData.index = -1;
	fPartitionData.status = B_PARTITION_UNRECOGNIZED;
	fPartitionData.flags = B_PARTITION_BUSY;
	fPartitionData.volume = -1;
	fPartitionData.name = NULL;
	fPartitionData.content_name = NULL;
	fPartitionData.type = NULL;
	fPartitionData.content_type = NULL;
	fPartitionData.parameters = NULL;
	fPartitionData.content_parameters = NULL;
	fPartitionData.cookie = NULL;
	fPartitionData.content_cookie = NULL;
}


/**
 * @brief Destroy the KPartition, releasing all owned resources.
 *
 * Deletes the listener set, unloads the associated disk system, and frees
 * all heap-allocated string fields (name, content name, type, parameters,
 * content parameters).
 */
KPartition::~KPartition()
{
	delete fListeners;
	SetDiskSystem(NULL);
	free(fPartitionData.name);
	free(fPartitionData.content_name);
	free(fPartitionData.type);
	free(fPartitionData.parameters);
	free(fPartitionData.content_parameters);
}


/**
 * @brief Increment the reference count of this partition.
 *
 * Must be paired with a corresponding call to Unregister(). The partition
 * will not be deleted by the manager while any references are held.
 */
void
KPartition::Register()
{
	fReferenceCount++;
}


/**
 * @brief Decrement the reference count and delete the partition if obsolete.
 *
 * When the reference count reaches zero and the partition has been marked
 * obsolete, the KDiskDeviceManager is asked to perform the actual deletion
 * under the manager lock.
 */
void
KPartition::Unregister()
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	ManagerLocker locker(manager);
	fReferenceCount--;
	if (IsObsolete() && fReferenceCount == 0) {
		// let the manager delete object
		manager->DeletePartition(this);
	}
}


/**
 * @brief Return the current reference count for this partition.
 *
 * @return Number of active Register() calls that have not yet been
 *         balanced by Unregister().
 */
int32
KPartition::CountReferences() const
{
	return fReferenceCount;
}


/**
 * @brief Mark this partition as obsolete so it is deleted when dereferenced.
 *
 * @note Once marked obsolete the flag cannot be cleared. The partition will
 *       be destroyed by KDiskDeviceManager as soon as its reference count
 *       drops to zero.
 */
void
KPartition::MarkObsolete()
{
	fObsolete = true;
}


/**
 * @brief Query whether this partition has been marked obsolete.
 *
 * @return @c true if MarkObsolete() has been called, @c false otherwise.
 */
bool
KPartition::IsObsolete() const
{
	return fObsolete;
}


/**
 * @brief Prepare this partition for removal from the device tree.
 *
 * Recursively removes all children, uninitialises content, unpublishes the
 * devfs node, and frees the partitioning-system and file-system cookies.
 *
 * @return @c true on success, @c false if removing a child partition failed.
 */
bool
KPartition::PrepareForRemoval()
{
	bool result = RemoveAllChildren();
	UninitializeContents();
	UnpublishDevice();
	if (ParentDiskSystem())
		ParentDiskSystem()->FreeCookie(this);
	if (DiskSystem())
		DiskSystem()->FreeContentCookie(this);
	return result;
}


/**
 * @brief Prepare this partition for deletion (currently a no-op placeholder).
 *
 * @return Always @c true.
 */
bool
KPartition::PrepareForDeletion()
{
	return true;
}


/**
 * @brief Open the partition's underlying device node with the given flags.
 *
 * Resolves the devfs path for this partition and opens it with ::open().
 *
 * @param flags  POSIX open flags (e.g. O_RDONLY, O_RDWR).
 * @param fd     Output parameter that receives the opened file descriptor.
 * @retval B_OK          File descriptor opened successfully.
 * @retval B_BAD_VALUE   @p fd is NULL.
 * @retval errno         The ::open() call failed; the errno value is returned.
 */
status_t
KPartition::Open(int flags, int* fd)
{
	if (!fd)
		return B_BAD_VALUE;

	// get the path
	KPath path;
	status_t error = GetPath(&path);
	if (error != B_OK)
		return error;

	// open the device
	*fd = open(path.Path(), flags);
	if (*fd < 0)
		return errno;

	return B_OK;
}


/**
 * @brief Publish this partition as a node under devfs.
 *
 * Constructs a partition_info descriptor and calls devfs_publish_partition().
 * Does nothing (returns B_OK) if the partition is already published.
 *
 * @retval B_OK           Published successfully.
 * @retval B_NO_MEMORY    Could not duplicate the node name string.
 * @retval B_NAME_TOO_LONG The device path exceeded the partition_info buffer.
 * @retval other          Error returned by devfs_publish_partition().
 */
status_t
KPartition::PublishDevice()
{
	if (fPublishedName)
		return B_OK;

	// get the name to publish
	char buffer[B_FILE_NAME_LENGTH];
	status_t error = GetFileName(buffer, B_FILE_NAME_LENGTH);
	if (error != B_OK)
		return error;

	// prepare a partition_info
	partition_info info;
	info.offset = Offset();
	info.size = Size();
	info.logical_block_size = BlockSize();
	info.physical_block_size = PhysicalBlockSize();
	info.session = 0;
	info.partition = ID();
	if (strlcpy(info.device, Device()->Path(), sizeof(info.device))
			>= sizeof(info.device)) {
		return B_NAME_TOO_LONG;
	}

	fPublishedName = strdup(buffer);
	if (!fPublishedName)
		return B_NO_MEMORY;

	error = devfs_publish_partition(buffer, &info);
	if (error != B_OK) {
		dprintf("KPartition::PublishDevice(): Failed to publish partition "
			"%" B_PRId32 ": %s\n", ID(), strerror(error));
		free(fPublishedName);
		fPublishedName = NULL;
		return error;
	}

	return B_OK;
}


/**
 * @brief Remove this partition's node from devfs.
 *
 * Calls devfs_unpublish_partition() and frees the cached published name.
 * Does nothing (returns B_OK) if the partition is not currently published.
 *
 * @retval B_OK    Unpublished successfully (or was not published).
 * @retval other   Error returned by devfs_unpublish_partition().
 */
status_t
KPartition::UnpublishDevice()
{
	if (!fPublishedName)
		return B_OK;

	// get the path
	KPath path;
	status_t error = GetPath(&path);
	if (error != B_OK) {
		dprintf("KPartition::UnpublishDevice(): Failed to get path for "
			"partition %" B_PRId32 ": %s\n", ID(), strerror(error));
		return error;
	}

	error = devfs_unpublish_partition(path.Path());
	if (error != B_OK) {
		dprintf("KPartition::UnpublishDevice(): Failed to unpublish partition "
			"%" B_PRId32 ": %s\n", ID(), strerror(error));
	}

	free(fPublishedName);
	fPublishedName = NULL;

	return error;
}


/**
 * @brief Re-publish this partition under its current file name if it changed.
 *
 * Recursively republishes children first, then calls devfs_rename_partition()
 * if the stored published name differs from the name computed by GetFileName().
 * If the rename fails the partition is fully unpublished.
 *
 * @retval B_OK           The name is up to date or was renamed successfully.
 * @retval B_NO_MEMORY    Could not allocate the new name string.
 * @retval other          Error returned by devfs_rename_partition().
 */
status_t
KPartition::RepublishDevice()
{
	if (!fPublishedName)
		return B_OK;

	char newNameBuffer[B_FILE_NAME_LENGTH];
	status_t error = GetFileName(newNameBuffer, B_FILE_NAME_LENGTH);
	if (error != B_OK) {
		UnpublishDevice();
		return error;
	}

	if (strcmp(fPublishedName, newNameBuffer) == 0)
		return B_OK;

	for (int i = 0; i < CountChildren(); i++)
		ChildAt(i)->RepublishDevice();

	char* newName = strdup(newNameBuffer);
	if (!newName) {
		UnpublishDevice();
		return B_NO_MEMORY;
	}

	error = devfs_rename_partition(Device()->Path(), fPublishedName, newName);

	if (error != B_OK) {
		free(newName);
		UnpublishDevice();
		dprintf("KPartition::RepublishDevice(): Failed to republish partition "
			"%" B_PRId32 ": %s\n", ID(), strerror(error));
		return error;
	}

	free(fPublishedName);
	fPublishedName = newName;

	return B_OK;
}


/**
 * @brief Query whether this partition currently has a devfs node.
 *
 * @return @c true if PublishDevice() has succeeded and UnpublishDevice() has
 *         not been called since; @c false otherwise.
 */
bool
KPartition::IsPublished() const
{
	return fPublishedName != NULL;
}


/**
 * @brief Set or clear the B_PARTITION_BUSY flag on this partition.
 *
 * @param busy  @c true to add the busy flag, @c false to clear it.
 */
void
KPartition::SetBusy(bool busy)
{
	if (busy)
		AddFlags(B_PARTITION_BUSY);
	else
		ClearFlags(B_PARTITION_BUSY);
}


/**
 * @brief Query whether the B_PARTITION_BUSY flag is set on this partition.
 *
 * @return @c true if the partition is currently busy.
 */
bool
KPartition::IsBusy() const
{
	return (fPartitionData.flags & B_PARTITION_BUSY) != 0;
}


/**
 * @brief Query whether this partition (and optionally its descendants) is busy.
 *
 * When @p includeDescendants is @c true the entire sub-tree is visited; if any
 * node is busy the method returns @c true immediately.
 *
 * @param includeDescendants  If @c true, also check all descendant partitions.
 * @return @c true if at least one checked partition has the busy flag set.
 */
bool
KPartition::IsBusy(bool includeDescendants)
{
	if (!includeDescendants)
		return IsBusy();

	struct IsBusyVisitor : KPartitionVisitor {
		virtual bool VisitPre(KPartition* partition)
		{
			return partition->IsBusy();
		}
	} checkVisitor;

	return VisitEachDescendant(&checkVisitor) != NULL;
}


/**
 * @brief Atomically check that no partition in scope is busy and mark them busy.
 *
 * If any partition in the checked scope is already busy the method returns
 * @c false without modifying any flags. Otherwise all partitions in scope are
 * marked busy.
 *
 * @param includeDescendants  Extend the check and mark to the whole sub-tree.
 * @return @c true if the busy flag was successfully acquired; @c false if any
 *         partition was already busy.
 */
bool
KPartition::CheckAndMarkBusy(bool includeDescendants)
{
	if (IsBusy(includeDescendants))
		return false;

	MarkBusy(includeDescendants);

	return true;
}


/**
 * @brief Set the B_PARTITION_BUSY flag on this partition and optionally descendants.
 *
 * @param includeDescendants  If @c true, mark all descendant partitions busy as well.
 */
void
KPartition::MarkBusy(bool includeDescendants)
{
	if (includeDescendants) {
		struct MarkBusyVisitor : KPartitionVisitor {
			virtual bool VisitPre(KPartition* partition)
			{
				partition->AddFlags(B_PARTITION_BUSY);
				return false;
			}
		} markVisitor;

		VisitEachDescendant(&markVisitor);
	} else
		SetBusy(true);
}


/**
 * @brief Clear the B_PARTITION_BUSY flag on this partition and optionally descendants.
 *
 * @param includeDescendants  If @c true, unmark all descendant partitions as well.
 */
void
KPartition::UnmarkBusy(bool includeDescendants)
{
	if (includeDescendants) {
		struct UnmarkBusyVisitor : KPartitionVisitor {
			virtual bool VisitPre(KPartition* partition)
			{
				partition->ClearFlags(B_PARTITION_BUSY);
				return false;
			}
		} visitor;

		VisitEachDescendant(&visitor);
	} else
		SetBusy(false);
}


/**
 * @brief Set the byte offset of this partition from the beginning of the device.
 *
 * Fires an OffsetChanged notification to all registered listeners if the
 * value actually changes.
 *
 * @param offset  New byte offset from device start.
 */
void
KPartition::SetOffset(off_t offset)
{
	if (fPartitionData.offset != offset) {
		fPartitionData.offset = offset;
		FireOffsetChanged(offset);
	}
}


/**
 * @brief Return the byte offset of this partition from the beginning of the device.
 *
 * @return Partition start offset in bytes.
 */
off_t
KPartition::Offset() const
{
	return fPartitionData.offset;
}


/**
 * @brief Set the total size of this partition in bytes.
 *
 * Fires a SizeChanged notification to all registered listeners if the value
 * actually changes.
 *
 * @param size  New partition size in bytes.
 */
void
KPartition::SetSize(off_t size)
{
	if (fPartitionData.size != size) {
		fPartitionData.size = size;
		FireSizeChanged(size);
	}
}


/**
 * @brief Return the total size of this partition in bytes.
 *
 * @return Partition size in bytes.
 */
off_t
KPartition::Size() const
{
	return fPartitionData.size;
}


/**
 * @brief Set the usable content size reported by the disk system.
 *
 * Fires a ContentSizeChanged notification to all registered listeners if the
 * value actually changes.
 *
 * @param size  New content size in bytes.
 */
void
KPartition::SetContentSize(off_t size)
{
	if (fPartitionData.content_size != size) {
		fPartitionData.content_size = size;
		FireContentSizeChanged(size);
	}
}


/**
 * @brief Return the usable content size of this partition in bytes.
 *
 * @return Content size in bytes as reported by the owning disk system.
 */
off_t
KPartition::ContentSize() const
{
	return fPartitionData.content_size;
}


/**
 * @brief Set the logical block size for this partition.
 *
 * Fires a BlockSizeChanged notification to all registered listeners if the
 * value actually changes.
 *
 * @param blockSize  New logical block size in bytes.
 */
void
KPartition::SetBlockSize(uint32 blockSize)
{
	if (fPartitionData.block_size != blockSize) {
		fPartitionData.block_size = blockSize;
		FireBlockSizeChanged(blockSize);
	}
}


/**
 * @brief Return the logical block size of this partition.
 *
 * @return Logical block size in bytes.
 */
uint32
KPartition::BlockSize() const
{
	return fPartitionData.block_size;
}


/**
 * @brief Return the physical block size of the underlying storage medium.
 *
 * The physical block size may differ from the logical block size for
 * Advanced Format (4K-native) drives.
 *
 * @return Physical block size in bytes.
 */
uint32
KPartition::PhysicalBlockSize() const
{
	return fPartitionData.physical_block_size;
}


/**
 * @brief Set the physical block size of the underlying storage medium.
 *
 * Does not fire a change notification; callers that need listener
 * notifications should fire them manually after calling this method.
 *
 * @param blockSize  New physical block size in bytes.
 */
void
KPartition::SetPhysicalBlockSize(uint32 blockSize)
{
	if (fPartitionData.physical_block_size != blockSize)
		fPartitionData.physical_block_size = blockSize;
}


/**
 * @brief Set the zero-based index of this partition within its parent.
 *
 * Fires an IndexChanged notification to all registered listeners if the
 * value actually changes.
 *
 * @param index  New zero-based sibling index.
 */
void
KPartition::SetIndex(int32 index)
{
	if (fPartitionData.index != index) {
		fPartitionData.index = index;
		FireIndexChanged(index);
	}
}


/**
 * @brief Return the zero-based index of this partition within its parent.
 *
 * @return Sibling index, or -1 if the partition has no parent.
 */
int32
KPartition::Index() const
{
	return fPartitionData.index;
}


/**
 * @brief Set the recognition status of this partition.
 *
 * Fires a StatusChanged notification to all registered listeners if the
 * value actually changes.
 *
 * @param status  One of the B_PARTITION_* status constants
 *                (e.g. B_PARTITION_UNRECOGNIZED, B_PARTITION_UNINITIALIZED,
 *                B_PARTITION_OK).
 */
void
KPartition::SetStatus(uint32 status)
{
	if (fPartitionData.status != status) {
		fPartitionData.status = status;
		FireStatusChanged(status);
	}
}


/**
 * @brief Return the current recognition status of this partition.
 *
 * @return One of the B_PARTITION_* status constants.
 */
uint32
KPartition::Status() const
{
	return fPartitionData.status;
}


/**
 * @brief Query whether this partition has status B_PARTITION_UNINITIALIZED.
 *
 * @return @c true if no disk system has initialised this partition's content.
 */
bool
KPartition::IsUninitialized() const
{
	return Status() == B_PARTITION_UNINITIALIZED;
}


/**
 * @brief Replace the complete flags word for this partition.
 *
 * Fires a FlagsChanged notification to all registered listeners if the
 * value actually changes.
 *
 * @param flags  New flags bitmask (combination of B_PARTITION_* flags).
 */
void
KPartition::SetFlags(uint32 flags)
{
	if (fPartitionData.flags != flags) {
		fPartitionData.flags = flags;
		FireFlagsChanged(flags);
	}
}


/**
 * @brief Set one or more flag bits without clearing others.
 *
 * Fires a FlagsChanged notification only if at least one new bit is added.
 *
 * @param flags  Bitmask of flag bits to add.
 */
void
KPartition::AddFlags(uint32 flags)
{
	if (~fPartitionData.flags & flags) {
		fPartitionData.flags |= flags;
		FireFlagsChanged(fPartitionData.flags);
	}
}


/**
 * @brief Clear one or more flag bits without affecting others.
 *
 * Fires a FlagsChanged notification only if at least one bit is actually cleared.
 *
 * @param flags  Bitmask of flag bits to clear.
 */
void
KPartition::ClearFlags(uint32 flags)
{
	if (fPartitionData.flags & flags) {
		fPartitionData.flags &= ~flags;
		FireFlagsChanged(fPartitionData.flags);
	}
}


/**
 * @brief Return the current flags bitmask for this partition.
 *
 * @return Combination of B_PARTITION_* flag constants.
 */
uint32
KPartition::Flags() const
{
	return fPartitionData.flags;
}


/**
 * @brief Query whether this partition contains a recognised file system.
 *
 * @return @c true if B_PARTITION_FILE_SYSTEM is set.
 */
bool
KPartition::ContainsFileSystem() const
{
	return (fPartitionData.flags & B_PARTITION_FILE_SYSTEM) != 0;
}


/**
 * @brief Query whether this partition contains a partitioning system.
 *
 * @return @c true if B_PARTITION_PARTITIONING_SYSTEM is set.
 */
bool
KPartition::ContainsPartitioningSystem() const
{
	return (fPartitionData.flags & B_PARTITION_PARTITIONING_SYSTEM) != 0;
}


/**
 * @brief Query whether this partition is read-only.
 *
 * @return @c true if B_PARTITION_READ_ONLY is set, for example because the
 *         underlying media is write-protected.
 */
bool
KPartition::IsReadOnly() const
{
	return (fPartitionData.flags & B_PARTITION_READ_ONLY) != 0;
}


/**
 * @brief Query whether this partition is currently mounted as a volume.
 *
 * @return @c true if B_PARTITION_MOUNTED is set (i.e. VolumeID() >= 0).
 */
bool
KPartition::IsMounted() const
{
	return (fPartitionData.flags & B_PARTITION_MOUNTED) != 0;
}


/**
 * @brief Query whether any descendant partition is currently mounted.
 *
 * Walks the entire sub-tree with a visitor that stops at the first mounted
 * descendant found.
 *
 * @return @c true if at least one descendant partition has the mounted flag set.
 */
bool
KPartition::IsChildMounted()
{
	struct IsMountedVisitor : KPartitionVisitor {
		virtual bool VisitPre(KPartition* partition)
		{
			return partition->IsMounted();
		}
	} checkVisitor;

	return VisitEachDescendant(&checkVisitor) != NULL;
}


/**
 * @brief Query whether this object represents the raw disk device itself.
 *
 * @return @c true if B_PARTITION_IS_DEVICE is set.
 */
bool
KPartition::IsDevice() const
{
	return (fPartitionData.flags & B_PARTITION_IS_DEVICE) != 0;
}


/**
 * @brief Set the human-readable name of this partition entry.
 *
 * The name is typically assigned by the partitioning scheme (e.g. GPT
 * partition name). Fires a NameChanged notification to all listeners.
 *
 * @param name  New NUL-terminated name string, or NULL to clear.
 * @retval B_OK        Name updated successfully.
 * @retval B_NO_MEMORY Heap allocation for the string copy failed.
 */
status_t
KPartition::SetName(const char* name)
{
	status_t error = set_string(fPartitionData.name, name);
	FireNameChanged(fPartitionData.name);
	return error;
}


/**
 * @brief Return the human-readable name of this partition entry.
 *
 * @return NUL-terminated name string, or NULL if not set.
 */
const char*
KPartition::Name() const
{
	return fPartitionData.name;
}


/**
 * @brief Set the content (volume) name reported by the disk system.
 *
 * Fires a ContentNameChanged notification to all listeners.
 *
 * @param name  New NUL-terminated content name, or NULL to clear.
 * @retval B_OK        Name updated successfully.
 * @retval B_NO_MEMORY Heap allocation for the string copy failed.
 */
status_t
KPartition::SetContentName(const char* name)
{
	status_t error = set_string(fPartitionData.content_name, name);
	FireContentNameChanged(fPartitionData.content_name);
	return error;
}


/**
 * @brief Return the content (volume) name reported by the disk system.
 *
 * @return NUL-terminated content name string, or NULL if not set.
 */
const char*
KPartition::ContentName() const
{
	return fPartitionData.content_name;
}


/**
 * @brief Set the partition type string (e.g. a GUID or MBR type code string).
 *
 * Fires a TypeChanged notification to all listeners.
 *
 * @param type  New NUL-terminated type string, or NULL to clear.
 * @retval B_OK        Type updated successfully.
 * @retval B_NO_MEMORY Heap allocation for the string copy failed.
 */
status_t
KPartition::SetType(const char* type)
{
	status_t error = set_string(fPartitionData.type, type);
	FireTypeChanged(fPartitionData.type);
	return error;
}


/**
 * @brief Return the partition type string.
 *
 * @return NUL-terminated type string, or NULL if not set.
 */
const char*
KPartition::Type() const
{
	return fPartitionData.type;
}


/**
 * @brief Return the content type MIME string supplied by the disk system.
 *
 * Unlike Type(), this is set automatically when a disk system is associated
 * and reflects the detected file-system or partitioning-system identifier.
 *
 * @return NUL-terminated content type string, or NULL if no disk system is set.
 */
const char*
KPartition::ContentType() const
{
	return fPartitionData.content_type;
}


/**
 * @brief Return a mutable pointer to the raw partition_data structure.
 *
 * Intended for use by disk-system add-ons that need direct field access.
 *
 * @return Pointer to the internal partition_data.
 */
partition_data*
KPartition::PartitionData()
{
	return &fPartitionData;
}


/**
 * @brief Return a read-only pointer to the raw partition_data structure.
 *
 * @return Const pointer to the internal partition_data.
 */
const partition_data*
KPartition::PartitionData() const
{
	return &fPartitionData;
}


/**
 * @brief Assign a new partition ID and notify listeners.
 *
 * Fires an IDChanged notification to all registered listeners if the value
 * actually changes.
 *
 * @param id  New partition_id value.
 */
void
KPartition::SetID(partition_id id)
{
	if (fPartitionData.id != id) {
		fPartitionData.id = id;
		FireIDChanged(id);
	}
}


/**
 * @brief Return the unique numeric identifier of this partition.
 *
 * @return Partition ID as assigned at construction time or via SetID().
 */
partition_id
KPartition::ID() const
{
	return fPartitionData.id;
}


/**
 * @brief Build the leaf file name of this partition as it appears in devfs.
 *
 * For a direct child of the device the name is simply the decimal index.
 * For deeper partitions the parent name is prepended with an underscore
 * separator (e.g. "0_1" for the second child of partition 0).
 *
 * @param buffer  Output buffer to receive the NUL-terminated name.
 * @param size    Size of @p buffer in bytes.
 * @retval B_OK           Name written successfully.
 * @retval B_NAME_TOO_LONG The generated name did not fit in @p buffer.
 */
status_t
KPartition::GetFileName(char* buffer, size_t size) const
{
	// If the parent is the device, the name is the index of the partition.
	if (Parent() == NULL || Parent()->IsDevice()) {
		if (snprintf(buffer, size, "%" B_PRId32, Index()) >= (int)size)
			return B_NAME_TOO_LONG;
		return B_OK;
	}

	// The partition has a non-device parent, so we append the index to the
	// parent partition's name.
	status_t error = Parent()->GetFileName(buffer, size);
	if (error != B_OK)
		return error;

	size_t len = strlen(buffer);
	if (snprintf(buffer + len, size - len, "_%" B_PRId32, Index()) >= int(size - len))
		return B_NAME_TOO_LONG;
	return B_OK;
}


/**
 * @brief Compute the full devfs path for this partition.
 *
 * Initialises @p path with the parent device path and replaces the leaf
 * component with the name returned by GetFileName().
 *
 * @param path  Output KPath object to receive the full path.
 * @retval B_OK         Path computed successfully.
 * @retval B_BAD_VALUE  @p path is NULL, uninitialised, or Parent()/Index() invalid.
 * @retval other        Error from KPath::SetPath(), GetFileName(), or ReplaceLeaf().
 */
status_t
KPartition::GetPath(KPath* path) const
{
	// For a KDiskDevice this version is never invoked, so the check for
	// Parent() is correct.
	if (!path || path->InitCheck() != B_OK || !Parent() || Index() < 0)
		return B_BAD_VALUE;

	// init the path with the device path
	status_t error = path->SetPath(Device()->Path());
	if (error != B_OK)
		return error;

	// replace the leaf name with the partition's file name
	char name[B_FILE_NAME_LENGTH];
	error = GetFileName(name, sizeof(name));
	if (error == B_OK)
		error = path->ReplaceLeaf(name);

	return error;
}


/**
 * @brief Suggest a suitable VFS mount point path for this partition's volume.
 *
 * Uses the content name, falling back to the partition name, then
 * "unnamed volume". Slashes in the name are replaced with dashes.
 * Appends a numeric suffix if the path already exists in the filesystem.
 *
 * @param mountPoint  Output KPath object to receive the suggested path.
 * @retval B_OK        Mount point path written successfully.
 * @retval B_BAD_VALUE @p mountPoint is NULL or the partition has no file system.
 * @retval B_NO_MEMORY Heap allocation for the path buffer failed.
 */
status_t
KPartition::GetMountPoint(KPath* mountPoint) const
{
	if (!mountPoint || !ContainsFileSystem())
		return B_BAD_VALUE;

	ASSERT(!IsMounted());
		// fetching the actual mounted point isn't implemented (yet)

	int nameLength = 0;
	const char* volumeName = ContentName();
	if (volumeName != NULL)
		nameLength = strlen(volumeName);
	if (nameLength == 0) {
		volumeName = Name();
		if (volumeName != NULL)
			nameLength = strlen(volumeName);
		if (nameLength == 0) {
			volumeName = "unnamed volume";
			nameLength = strlen(volumeName);
		}
	}

	BStackOrHeapArray<char, 128> basePath(nameLength + 2);
	if (!basePath.IsValid())
		return B_NO_MEMORY;
	int32 len = snprintf(basePath, nameLength + 2, "/%s", volumeName);
	for (int32 i = 1; i < len; i++)
		if (basePath[i] == '/')
			basePath[i] = '-';
	char* path = mountPoint->LockBuffer();
	int32 pathLen = mountPoint->BufferSize();
	strncpy(path, basePath, pathLen);

	struct stat dummy;
	for (int i = 1; ; i++) {
		if (stat(path, &dummy) != 0)
			break;
		snprintf(path, pathLen, "%s%d", (char*)basePath, i);
	}

	mountPoint->UnlockBuffer();
	return B_OK;
}


/**
 * @brief Associate or dissociate a mounted VFS volume with this partition.
 *
 * Stores the volume ID, updates the B_PARTITION_MOUNTED flag accordingly,
 * and broadcasts a B_DEVICE_PARTITION_MOUNTED or B_DEVICE_PARTITION_UNMOUNTED
 * notification through KDiskDeviceManager.
 *
 * @param volumeID  The dev_t of the newly mounted volume, or -1 to mark as
 *                  unmounted.
 */
void
KPartition::SetVolumeID(dev_t volumeID)
{
	if (fPartitionData.volume == volumeID)
		return;

	fPartitionData.volume = volumeID;
	FireVolumeIDChanged(volumeID);
	if (VolumeID() >= 0)
		AddFlags(B_PARTITION_MOUNTED);
	else
		ClearFlags(B_PARTITION_MOUNTED);

	KDiskDeviceManager* manager = KDiskDeviceManager::Default();

	char messageBuffer[512];
	KMessage message;
	message.SetTo(messageBuffer, sizeof(messageBuffer), B_DEVICE_UPDATE);
	message.AddInt32("event", volumeID >= 0
		? B_DEVICE_PARTITION_MOUNTED : B_DEVICE_PARTITION_UNMOUNTED);
	message.AddInt32("id", ID());
	if (volumeID >= 0)
		message.AddInt32("volume", volumeID);

	manager->Notify(message, B_DEVICE_REQUEST_MOUNTING);
}


/**
 * @brief Return the dev_t of the VFS volume mounted on this partition.
 *
 * @return A valid dev_t if the partition is mounted, or -1 if unmounted.
 */
dev_t
KPartition::VolumeID() const
{
	return fPartitionData.volume;
}


/**
 * @brief Set the raw parameters string for this partition entry.
 *
 * The parameters string is an opaque, disk-system-defined configuration
 * payload. Fires a ParametersChanged notification to all listeners.
 *
 * @param parameters  New NUL-terminated parameters string, or NULL to clear.
 * @retval B_OK        Updated successfully.
 * @retval B_NO_MEMORY Heap allocation for the string copy failed.
 */
status_t
KPartition::SetParameters(const char* parameters)
{
	status_t error = set_string(fPartitionData.parameters, parameters);
	FireParametersChanged(fPartitionData.parameters);
	return error;
}


/**
 * @brief Return the raw parameters string for this partition entry.
 *
 * @return NUL-terminated parameters string, or NULL if not set.
 */
const char*
KPartition::Parameters() const
{
	return fPartitionData.parameters;
}


/**
 * @brief Set the content parameters string supplied by the content disk system.
 *
 * Fires a ContentParametersChanged notification to all listeners.
 *
 * @param parameters  New NUL-terminated content parameters string, or NULL.
 * @retval B_OK        Updated successfully.
 * @retval B_NO_MEMORY Heap allocation for the string copy failed.
 */
status_t
KPartition::SetContentParameters(const char* parameters)
{
	status_t error = set_string(fPartitionData.content_parameters, parameters);
	FireContentParametersChanged(fPartitionData.content_parameters);
	return error;
}


/**
 * @brief Return the content parameters string supplied by the content disk system.
 *
 * @return NUL-terminated content parameters string, or NULL if not set.
 */
const char*
KPartition::ContentParameters() const
{
	return fPartitionData.content_parameters;
}


/**
 * @brief Bind this partition to its owning KDiskDevice.
 *
 * Also sets B_PARTITION_READ_ONLY if the device media is read-only.
 *
 * @param device  Pointer to the owning KDiskDevice, or NULL to detach.
 */
void
KPartition::SetDevice(KDiskDevice* device)
{
	fDevice = device;
	if (fDevice != NULL && fDevice->IsReadOnlyMedia())
		AddFlags(B_PARTITION_READ_ONLY);
}


/**
 * @brief Return the KDiskDevice that owns this partition.
 *
 * @return Pointer to the owning KDiskDevice, or NULL if detached.
 */
KDiskDevice*
KPartition::Device() const
{
	return fDevice;
}


/**
 * @brief Return the direct parent partition of this partition.
 *
 * @return Pointer to the parent KPartition, or NULL if this partition is
 *         a direct child of the device (or unattached).
 */
KPartition*
KPartition::Parent() const
{
	return fParent;
}


/**
 * @brief Insert a child partition at the given index.
 *
 * Registers the child with KDiskDeviceManager, updates sibling indices,
 * publishes the partition to devfs, and fires a ChildAdded notification.
 *
 * @param partition  The KPartition to add; must not be NULL.
 * @param index      Zero-based insertion index, or -1 to append.
 * @retval B_OK         Child added successfully.
 * @retval B_BAD_VALUE  @p index is out of range or @p partition is NULL.
 * @retval B_NO_MEMORY  Manager could not register the new partition.
 * @retval B_ERROR      Could not acquire the manager lock.
 */
status_t
KPartition::AddChild(KPartition* partition, int32 index)
{
	// check parameters
	int32 count = fPartitionData.child_count;
	if (index == -1)
		index = count;
	if (index < 0 || index > count || !partition)
		return B_BAD_VALUE;

	// add partition
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	if (ManagerLocker locker = manager) {
		status_t error = fChildren.Insert(partition, index);
		if (error != B_OK)
			return error;
		if (!manager->PartitionAdded(partition)) {
			fChildren.Erase(index);
			return B_NO_MEMORY;
		}
		// update siblings index's
		partition->SetIndex(index);
		_UpdateChildIndices(count, index);
		fPartitionData.child_count++;

		partition->fParent = this;
		partition->SetDevice(Device());
		partition->SetPhysicalBlockSize(PhysicalBlockSize());

		// publish to devfs
		partition->PublishDevice();

		// notify listeners
		FireChildAdded(partition, index);
		return B_OK;
	}
	return B_ERROR;
}


/**
 * @brief Allocate a new KPartition and add it as a child at the given index.
 *
 * Combines allocation, geometry initialisation, and AddChild() in one call.
 *
 * @param id      Desired partition ID, or a negative value to auto-assign.
 * @param index   Zero-based insertion index, or -1 to append.
 * @param offset  Byte offset of the new partition from the device start.
 * @param size    Size of the new partition in bytes.
 * @param _child  Optional output pointer that receives the new KPartition.
 * @retval B_OK        Child created and added successfully.
 * @retval B_BAD_VALUE @p index is out of range.
 * @retval B_NO_MEMORY Allocation of the KPartition object failed.
 * @retval other       Error forwarded from AddChild().
 */
status_t
KPartition::CreateChild(partition_id id, int32 index, off_t offset, off_t size,
	KPartition** _child)
{
	// check parameters
	int32 count = fPartitionData.child_count;
	if (index == -1)
		index = count;
	if (index < 0 || index > count)
		return B_BAD_VALUE;

	// create and add partition
	KPartition* child = new(std::nothrow) KPartition(id);
	if (child == NULL)
		return B_NO_MEMORY;

	child->SetOffset(offset);
	child->SetSize(size);

	status_t error = AddChild(child, index);

	// cleanup / set result
	if (error != B_OK)
		delete child;
	else if (_child)
		*_child = child;

	return error;
}


/**
 * @brief Remove the child at the given index from this partition.
 *
 * Unregisters the child from KDiskDeviceManager, repairs sibling indices,
 * detaches parent/device pointers, and fires a ChildRemoved notification.
 *
 * @param index  Zero-based index of the child to remove.
 * @return @c true on success; @c false if @p index is out of range or the
 *         manager lock could not be acquired.
 */
bool
KPartition::RemoveChild(int32 index)
{
	if (index < 0 || index >= fPartitionData.child_count)
		return false;

	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	if (ManagerLocker locker = manager) {
		KPartition* partition = fChildren.ElementAt(index);
		PartitionRegistrar _(partition);
		if (!partition || !manager->PartitionRemoved(partition)
			|| !fChildren.Erase(index)) {
			return false;
		}
		_UpdateChildIndices(index, fChildren.Count());
		partition->SetIndex(-1);
		fPartitionData.child_count--;
		partition->fParent = NULL;
		partition->SetDevice(NULL);
		// notify listeners
		FireChildRemoved(partition, index);
		return true;
	}
	return false;
}


/**
 * @brief Remove a specific child partition by pointer.
 *
 * Looks up the child's index and delegates to RemoveChild(int32).
 *
 * @param child  Pointer to the child KPartition to remove.
 * @return @c true on success; @c false if @p child is NULL or not found.
 */
bool
KPartition::RemoveChild(KPartition* child)
{
	if (child) {
		int32 index = fChildren.IndexOf(child);
		if (index >= 0)
			return RemoveChild(index);
	}
	return false;
}


/**
 * @brief Remove all direct children of this partition in reverse index order.
 *
 * @return @c true if all children were removed successfully; @c false if any
 *         individual removal failed.
 */
bool
KPartition::RemoveAllChildren()
{
	int32 count = CountChildren();
	for (int32 i = count - 1; i >= 0; i--) {
		if (!RemoveChild(i))
			return false;
	}
	return true;
}


/**
 * @brief Return the child partition at the given index.
 *
 * @param index  Zero-based child index.
 * @return Pointer to the child KPartition, or NULL if @p index is out of range.
 */
KPartition*
KPartition::ChildAt(int32 index) const
{
	return index >= 0 && index < fChildren.Count()
		? fChildren.ElementAt(index) : NULL;
}


/**
 * @brief Return the number of direct children of this partition.
 *
 * @return Direct child count.
 */
int32
KPartition::CountChildren() const
{
	return fPartitionData.child_count;
}


/**
 * @brief Return the total number of partitions in this sub-tree, including self.
 *
 * @return One plus the recursive descendant count of all children.
 */
int32
KPartition::CountDescendants() const
{
	int32 count = 1;
	for (int32 i = 0; KPartition* child = ChildAt(i); i++)
		count += child->CountDescendants();
	return count;
}


/**
 * @brief Walk the entire descendant sub-tree with a KPartitionVisitor.
 *
 * Performs a pre-order/post-order depth-first traversal. The walk stops
 * early and returns the current partition if the visitor returns @c true
 * from either VisitPre() or VisitPost().
 *
 * @param visitor  Pointer to the visitor object; must not be NULL.
 * @return The first partition for which the visitor returned @c true, or
 *         NULL if the traversal completed without early termination.
 */
KPartition*
KPartition::VisitEachDescendant(KPartitionVisitor* visitor)
{
	if (!visitor)
		return NULL;
	if (visitor->VisitPre(this))
		return this;
	for (int32 i = 0; KPartition* child = ChildAt(i); i++) {
		if (KPartition* result = child->VisitEachDescendant(visitor))
			return result;
	}
	if (visitor->VisitPost(this))
		return this;
	return NULL;
}


/**
 * @brief Associate a KDiskSystem (file system or partitioning system) with this partition.
 *
 * Unloads any previously associated disk system, loads the new one, updates
 * the content_type field and the file-system / partitioning-system flags, and
 * fires DiskSystemChanged and B_DEVICE_PARTITION_INITIALIZED notifications.
 *
 * @param diskSystem  Pointer to the KDiskSystem to associate, or NULL to clear.
 * @param priority    Recognition priority reported by the disk system scanner.
 */
void
KPartition::SetDiskSystem(KDiskSystem* diskSystem, float priority)
{
	// unload former disk system
	if (fDiskSystem) {
		fPartitionData.content_type = NULL;
		fDiskSystem->Unload();
		fDiskSystem = NULL;
		fDiskSystemPriority = -1;
	}
	// set and load new one
	fDiskSystem = diskSystem;
	if (fDiskSystem) {
		fDiskSystem->Load();
			// can't fail, since it's already loaded
	}
	// update concerned partition flags
	if (fDiskSystem) {
		fPartitionData.content_type = fDiskSystem->PrettyName();
		fDiskSystemPriority = priority;
		if (fDiskSystem->IsFileSystem())
			AddFlags(B_PARTITION_FILE_SYSTEM);
		else
			AddFlags(B_PARTITION_PARTITIONING_SYSTEM);
	}
	// notify listeners
	FireDiskSystemChanged(fDiskSystem);

	KDiskDeviceManager* manager = KDiskDeviceManager::Default();

	char messageBuffer[512];
	KMessage message;
	message.SetTo(messageBuffer, sizeof(messageBuffer), B_DEVICE_UPDATE);
	message.AddInt32("event", B_DEVICE_PARTITION_INITIALIZED);
	message.AddInt32("id", ID());

	manager->Notify(message, B_DEVICE_REQUEST_PARTITION);
}


/**
 * @brief Return the KDiskSystem currently associated with this partition's content.
 *
 * @return Pointer to the associated KDiskSystem, or NULL if none is set.
 */
KDiskSystem*
KPartition::DiskSystem() const
{
	return fDiskSystem;
}


/**
 * @brief Return the recognition priority of the currently associated disk system.
 *
 * A higher value means the disk system was more confident during scanning.
 *
 * @return Priority value passed to the last SetDiskSystem() call, or -1 if
 *         no disk system is associated.
 */
float
KPartition::DiskSystemPriority() const
{
	return fDiskSystemPriority;
}


/**
 * @brief Return the disk system of the parent partition.
 *
 * Convenience wrapper that returns Parent()->DiskSystem() safely.
 *
 * @return Pointer to the parent's KDiskSystem, or NULL if there is no parent
 *         or the parent has no disk system.
 */
KDiskSystem*
KPartition::ParentDiskSystem() const
{
	return Parent() ? Parent()->DiskSystem() : NULL;
}


/**
 * @brief Set the opaque cookie stored by the parent disk system for this entry.
 *
 * Fires a CookieChanged notification to all listeners if the value changes.
 *
 * @param cookie  New cookie value; may be NULL.
 */
void
KPartition::SetCookie(void* cookie)
{
	if (fPartitionData.cookie != cookie) {
		fPartitionData.cookie = cookie;
		FireCookieChanged(cookie);
	}
}


/**
 * @brief Return the opaque cookie stored by the parent disk system.
 *
 * @return Cookie pointer, or NULL if not set.
 */
void*
KPartition::Cookie() const
{
	return fPartitionData.cookie;
}


/**
 * @brief Set the opaque cookie stored by the content disk system.
 *
 * Fires a ContentCookieChanged notification to all listeners if the value changes.
 *
 * @param cookie  New content cookie value; may be NULL.
 */
void
KPartition::SetContentCookie(void* cookie)
{
	if (fPartitionData.content_cookie != cookie) {
		fPartitionData.content_cookie = cookie;
		FireContentCookieChanged(cookie);
	}
}


/**
 * @brief Return the opaque cookie stored by the content disk system.
 *
 * @return Content cookie pointer, or NULL if not set.
 */
void*
KPartition::ContentCookie() const
{
	return fPartitionData.content_cookie;
}


/**
 * @brief Register a KPartitionListener to receive change notifications.
 *
 * The listener set is created lazily on first use.
 *
 * @param listener  Non-NULL pointer to the listener to add.
 * @return @c true if the listener was added; @c false if @p listener is NULL
 *         or heap allocation for the set failed.
 */
bool
KPartition::AddListener(KPartitionListener* listener)
{
	if (!listener)
		return false;
	// lazy create listeners
	if (!fListeners) {
		fListeners = new(nothrow) ListenerSet;
		if (!fListeners)
			return false;
	}
	// add listener
	return fListeners->Insert(listener) == B_OK;
}


/**
 * @brief Unregister a previously added KPartitionListener.
 *
 * If the listener set becomes empty after removal it is deleted.
 *
 * @param listener  Pointer to the listener to remove.
 * @return @c true if the listener was found and removed; @c false otherwise.
 */
bool
KPartition::RemoveListener(KPartitionListener* listener)
{
	if (!listener || !fListeners)
		return false;
	// remove listener and delete the set, if empty now
	bool result = (fListeners->Remove(listener) > 0);
	if (fListeners->IsEmpty()) {
		delete fListeners;
		fListeners = NULL;
	}
	return result;
}


/**
 * @brief Record that one or more properties of this partition have changed.
 *
 * Sets or clears bits in the change-flags word, increments the change counter,
 * and propagates B_PARTITION_CHANGED_DESCENDANTS up to the parent.
 *
 * @param flags       Bitmask of B_PARTITION_CHANGED_* bits to add.
 * @param clearFlags  Bitmask of B_PARTITION_CHANGED_* bits to clear first.
 */
void
KPartition::Changed(uint32 flags, uint32 clearFlags)
{
	fChangeFlags &= ~clearFlags;
	fChangeFlags |= flags;
	fChangeCounter++;
	if (Parent())
		Parent()->Changed(B_PARTITION_CHANGED_DESCENDANTS);
}


/**
 * @brief Overwrite the entire change-flags word.
 *
 * @param flags  New change-flags bitmask.
 */
void
KPartition::SetChangeFlags(uint32 flags)
{
	fChangeFlags = flags;
}


/**
 * @brief Return the current change-flags bitmask.
 *
 * @return Combination of B_PARTITION_CHANGED_* bits set since the last reset.
 */
uint32
KPartition::ChangeFlags() const
{
	return fChangeFlags;
}


/**
 * @brief Return the monotonically increasing change counter.
 *
 * The counter is incremented on every call to Changed() and can be used by
 * user-space clients to detect whether the partition has been modified.
 *
 * @return Current change counter value.
 */
int32
KPartition::ChangeCounter() const
{
	return fChangeCounter;
}


/**
 * @brief Strip content-related state from this partition and reset it to uninitialized.
 *
 * Removes all children, force-unmounts any mounted volume, clears content
 * name, content parameters, content size, and block size, unloads the disk
 * system, resets the status to B_PARTITION_UNINITIALIZED, and clears
 * file-system/partitioning-system flags. Optionally records all changes via
 * Changed().
 *
 * @param logChanges  If @c true, call Changed() with the accumulated flags
 *                    so the change is visible to user-space.
 * @retval B_OK     Contents uninitialised successfully.
 * @retval B_ERROR  Removing a child partition failed.
 * @retval other    Error from vfs_unmount().
 */
status_t
KPartition::UninitializeContents(bool logChanges)
{
	if (DiskSystem()) {
		uint32 flags = B_PARTITION_CHANGED_INITIALIZATION
			| B_PARTITION_CHANGED_CONTENT_TYPE
			| B_PARTITION_CHANGED_STATUS
			| B_PARTITION_CHANGED_FLAGS;

		// children
		if (CountChildren() > 0) {
			if (!RemoveAllChildren())
				return B_ERROR;
			flags |= B_PARTITION_CHANGED_CHILDREN;
		}

		// volume
		if (VolumeID() >= 0) {
			status_t error = vfs_unmount(VolumeID(),
				B_FORCE_UNMOUNT | B_UNMOUNT_BUSY_PARTITION);
			if (error != B_OK) {
				dprintf("KPartition::UninitializeContents(): Failed to unmount "
					"device %" B_PRIdDEV ": %s\n", VolumeID(), strerror(error));
			}

			SetVolumeID(-1);
			flags |= B_PARTITION_CHANGED_VOLUME;
		}

		// content name
		if (ContentName()) {
			SetContentName(NULL);
			flags |= B_PARTITION_CHANGED_CONTENT_NAME;
		}

		// content parameters
		if (ContentParameters()) {
			SetContentParameters(NULL);
			flags |= B_PARTITION_CHANGED_CONTENT_PARAMETERS;
		}

		// content size
		if (ContentSize() > 0) {
			SetContentSize(0);
			flags |= B_PARTITION_CHANGED_CONTENT_SIZE;
		}

		// block size
		if (Parent() && Parent()->BlockSize() != BlockSize()) {
			SetBlockSize(Parent()->BlockSize());
			flags |= B_PARTITION_CHANGED_BLOCK_SIZE;
		}

		// disk system
		DiskSystem()->FreeContentCookie(this);
		SetDiskSystem(NULL);

		// status
		SetStatus(B_PARTITION_UNINITIALIZED);

		// flags
		ClearFlags(B_PARTITION_FILE_SYSTEM | B_PARTITION_PARTITIONING_SYSTEM);
		if (!Device()->IsReadOnlyMedia())
			ClearFlags(B_PARTITION_READ_ONLY);

		// log changes
		if (logChanges) {
			Changed(flags, B_PARTITION_CHANGED_DEFRAGMENTATION
				| B_PARTITION_CHANGED_CHECK | B_PARTITION_CHANGED_REPAIR);
		}
	}

	return B_OK;
}


/**
 * @brief Store an arbitrary 32-bit value for use by scanning algorithms.
 *
 * This field is not persisted and is intended as scratch space during
 * partition scanning passes.
 *
 * @param data  Value to store.
 */
void
KPartition::SetAlgorithmData(uint32 data)
{
	fAlgorithmData = data;
}


/**
 * @brief Return the algorithm scratch data previously stored with SetAlgorithmData().
 *
 * @return The stored 32-bit value (default zero).
 */
uint32
KPartition::AlgorithmData() const
{
	return fAlgorithmData;
}


/**
 * @brief Serialise this partition and its children into a UserDataWriter buffer.
 *
 * Writes all scalar fields, places all string fields through the writer
 * (which handles relocation for user-space address fixup), and recurses
 * into children. The @p data pointer may be NULL for a dry-run size
 * calculation pass.
 *
 * @param writer  The UserDataWriter that manages the output buffer and
 *                relocation table.
 * @param data    Pointer to the user_partition_data structure to fill, or
 *                NULL if only buffer space should be reserved.
 */
void
KPartition::WriteUserData(UserDataWriter& writer, user_partition_data* data)
{
	// allocate
	char* name = writer.PlaceString(Name());
	char* contentName = writer.PlaceString(ContentName());
	char* type = writer.PlaceString(Type());
	char* contentType = writer.PlaceString(ContentType());
	char* parameters = writer.PlaceString(Parameters());
	char* contentParameters = writer.PlaceString(ContentParameters());
	// fill in data
	if (data) {
		data->id = ID();
		data->offset = Offset();
		data->size = Size();
		data->content_size = ContentSize();
		data->block_size = BlockSize();
		data->physical_block_size = PhysicalBlockSize();
		data->status = Status();
		data->flags = Flags();
		data->volume = VolumeID();
		data->index = Index();
		data->change_counter = ChangeCounter();
		data->disk_system = (DiskSystem() ? DiskSystem()->ID() : -1);
		data->name = name;
		data->content_name = contentName;
		data->type = type;
		data->content_type = contentType;
		data->parameters = parameters;
		data->content_parameters = contentParameters;
		data->child_count = CountChildren();
		// make buffer relocatable
		writer.AddRelocationEntry(&data->name);
		writer.AddRelocationEntry(&data->content_name);
		writer.AddRelocationEntry(&data->type);
		writer.AddRelocationEntry(&data->content_type);
		writer.AddRelocationEntry(&data->parameters);
		writer.AddRelocationEntry(&data->content_parameters);
	}
	// children
	for (int32 i = 0; KPartition* child = ChildAt(i); i++) {
		user_partition_data* childData
			= writer.AllocatePartitionData(child->CountChildren());
		if (data) {
			data->children[i] = childData;
			writer.AddRelocationEntry(&data->children[i]);
		}
		child->WriteUserData(writer, childData);
	}
}


/**
 * @brief Print a human-readable summary of this partition to the kernel log.
 *
 * Outputs geometry, flags, disk system, names, type, and parameters.
 * If @p deep is @c true the dump recurses into all children with increased
 * indentation.
 *
 * @param deep   If @c true, recursively dump child partitions as well.
 * @param level  Current indentation level (0 for the root of the dump).
 *               Values outside [0, 255] are silently ignored.
 */
void
KPartition::Dump(bool deep, int32 level)
{
	if (level < 0 || level > 255)
		return;

	char prefix[256];
	sprintf(prefix, "%*s%*s", (int)level, "", (int)level, "");
	KPath path;
	GetPath(&path);
	if (level > 0)
		OUT("%spartition %" B_PRId32 ": %s\n", prefix, ID(), path.Path());
	OUT("%s  offset:            %" B_PRIdOFF "\n", prefix, Offset());
	OUT("%s  size:              %" B_PRIdOFF " (%.2f MB)\n", prefix, Size(),
		Size() / (1024.0*1024));
	OUT("%s  content size:      %" B_PRIdOFF "\n", prefix, ContentSize());
	OUT("%s  block size:        %" B_PRIu32 "\n", prefix, BlockSize());
	OUT("%s  physical block size: %" B_PRIu32 "\n", prefix, PhysicalBlockSize());
	OUT("%s  child count:       %" B_PRId32 "\n", prefix, CountChildren());
	OUT("%s  index:             %" B_PRId32 "\n", prefix, Index());
	OUT("%s  status:            %" B_PRIu32 "\n", prefix, Status());
	OUT("%s  flags:             %" B_PRIx32 "\n", prefix, Flags());
	OUT("%s  volume:            %" B_PRIdDEV "\n", prefix, VolumeID());
	OUT("%s  disk system:       %s\n", prefix,
		(DiskSystem() ? DiskSystem()->Name() : NULL));
	OUT("%s  name:              %s\n", prefix, Name());
	OUT("%s  content name:      %s\n", prefix, ContentName());
	OUT("%s  type:              %s\n", prefix, Type());
	OUT("%s  content type:      %s\n", prefix, ContentType());
	OUT("%s  params:            %s\n", prefix, Parameters());
	OUT("%s  content params:    %s\n", prefix, ContentParameters());
	if (deep) {
		for (int32 i = 0; KPartition* child = ChildAt(i); i++)
			child->Dump(true, level + 1);
	}
}


/**
 * @brief Notify all listeners that the partition offset has changed.
 *
 * @param offset  The new byte offset value.
 */
void
KPartition::FireOffsetChanged(off_t offset)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->OffsetChanged(this, offset);
		}
	}
}


/**
 * @brief Notify all listeners that the partition size has changed.
 *
 * @param size  The new size in bytes.
 */
void
KPartition::FireSizeChanged(off_t size)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->SizeChanged(this, size);
		}
	}
}


/**
 * @brief Notify all listeners that the content size has changed.
 *
 * @param size  The new content size in bytes.
 */
void
KPartition::FireContentSizeChanged(off_t size)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->ContentSizeChanged(this, size);
		}
	}
}


/**
 * @brief Notify all listeners that the logical block size has changed.
 *
 * @param blockSize  The new logical block size in bytes.
 */
void
KPartition::FireBlockSizeChanged(uint32 blockSize)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->BlockSizeChanged(this, blockSize);
		}
	}
}


/**
 * @brief Notify all listeners that the partition's sibling index has changed.
 *
 * @param index  The new zero-based index within the parent's child list.
 */
void
KPartition::FireIndexChanged(int32 index)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->IndexChanged(this, index);
		}
	}
}


/**
 * @brief Notify all listeners that the partition status has changed.
 *
 * @param status  The new status value (one of the B_PARTITION_* constants).
 */
void
KPartition::FireStatusChanged(uint32 status)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->StatusChanged(this, status);
		}
	}
}


/**
 * @brief Notify all listeners that the flags bitmask has changed.
 *
 * @param flags  The new combined flags value.
 */
void
KPartition::FireFlagsChanged(uint32 flags)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->FlagsChanged(this, flags);
		}
	}
}


/**
 * @brief Notify all listeners that the partition name has changed.
 *
 * @param name  The new NUL-terminated partition name, or NULL.
 */
void
KPartition::FireNameChanged(const char* name)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->NameChanged(this, name);
		}
	}
}


/**
 * @brief Notify all listeners that the content name has changed.
 *
 * @param name  The new NUL-terminated content (volume) name, or NULL.
 */
void
KPartition::FireContentNameChanged(const char* name)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->ContentNameChanged(this, name);
		}
	}
}


/**
 * @brief Notify all listeners that the partition type string has changed.
 *
 * @param type  The new NUL-terminated type string, or NULL.
 */
void
KPartition::FireTypeChanged(const char* type)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->TypeChanged(this, type);
		}
	}
}


/**
 * @brief Notify all listeners that the partition ID has changed.
 *
 * @param id  The new partition_id value.
 */
void
KPartition::FireIDChanged(partition_id id)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->IDChanged(this, id);
		}
	}
}


/**
 * @brief Notify all listeners that the associated volume ID has changed.
 *
 * @param volumeID  The new dev_t volume ID, or -1 if unmounted.
 */
void
KPartition::FireVolumeIDChanged(dev_t volumeID)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->VolumeIDChanged(this, volumeID);
		}
	}
}


/**
 * @brief Notify all listeners that the partition parameters string has changed.
 *
 * @param parameters  The new NUL-terminated parameters string, or NULL.
 */
void
KPartition::FireParametersChanged(const char* parameters)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->ParametersChanged(this, parameters);
		}
	}
}


/**
 * @brief Notify all listeners that the content parameters string has changed.
 *
 * @param parameters  The new NUL-terminated content parameters string, or NULL.
 */
void
KPartition::FireContentParametersChanged(const char* parameters)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->ContentParametersChanged(this, parameters);
		}
	}
}


/**
 * @brief Notify all listeners that a child partition has been added.
 *
 * @param child  Pointer to the newly added child KPartition.
 * @param index  Zero-based index at which the child was inserted.
 */
void
KPartition::FireChildAdded(KPartition* child, int32 index)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->ChildAdded(this, child, index);
		}
	}
}


/**
 * @brief Notify all listeners that a child partition has been removed.
 *
 * @param child  Pointer to the removed child KPartition.
 * @param index  Zero-based index the child occupied before removal.
 */
void
KPartition::FireChildRemoved(KPartition* child, int32 index)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->ChildRemoved(this, child, index);
		}
	}
}


/**
 * @brief Notify all listeners that the associated disk system has changed.
 *
 * @param diskSystem  Pointer to the new KDiskSystem, or NULL if cleared.
 */
void
KPartition::FireDiskSystemChanged(KDiskSystem* diskSystem)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->DiskSystemChanged(this, diskSystem);
		}
	}
}


/**
 * @brief Notify all listeners that the parent-disk-system cookie has changed.
 *
 * @param cookie  The new cookie value.
 */
void
KPartition::FireCookieChanged(void* cookie)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->CookieChanged(this, cookie);
		}
	}
}


/**
 * @brief Notify all listeners that the content-disk-system cookie has changed.
 *
 * @param cookie  The new content cookie value.
 */
void
KPartition::FireContentCookieChanged(void* cookie)
{
	if (fListeners) {
		for (ListenerSet::Iterator it = fListeners->Begin();
			 it != fListeners->End(); ++it) {
			(*it)->ContentCookieChanged(this, cookie);
		}
	}
}


/**
 * @brief Update the sibling indices and re-publish devfs nodes for a range of children.
 *
 * When children are inserted or removed the indices of siblings in the
 * affected range must be renumbered and their devfs names potentially
 * updated. Iterates forward (start < end) or backward (start > end).
 *
 * @param start  Index of the first child to update.
 * @param end    One-past-last index (exclusive) when iterating forward, or
 *               exclusive lower bound when iterating backward.
 */
void
KPartition::_UpdateChildIndices(int32 start, int32 end)
{
	if (start < end) {
		for (int32 i = start; i < end; i++) {
			fChildren.ElementAt(i)->SetIndex(i);
			fChildren.ElementAt(i)->RepublishDevice();
		}
	} else {
		for (int32 i = start; i > end; i--) {
			fChildren.ElementAt(i)->SetIndex(i);
			fChildren.ElementAt(i)->RepublishDevice();
		}
	}
}


/**
 * @brief Atomically allocate the next unique partition ID.
 *
 * Uses atomic_add on the class-static counter so that IDs remain unique
 * across concurrent partition creation.
 *
 * @return A new, globally unique partition_id value.
 */
int32
KPartition::_NextID()
{
	return atomic_add(&sNextID, 1);
}
