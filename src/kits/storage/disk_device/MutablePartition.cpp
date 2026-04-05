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
 *   Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file MutablePartition.cpp
 * @brief Mutable shadow representation of a disk partition used during editing.
 *
 * BMutablePartition is a writable mirror of the read-only BPartition data.
 * It is created by a BPartition::Delegate and accumulates property changes
 * (size, offset, name, type, parameters, etc.) while recording which fields
 * have been modified via a change-flags bitmask. The DiskDeviceJobGenerator
 * later reads these flags to decide which kernel jobs to generate.
 *
 * @see BPartition
 */

#include <MutablePartition.h>

#include <stdlib.h>
#include <string.h>

#include <new>

#include <Partition.h>

#include <ddm_userland_interface_defs.h>

#include "DiskDeviceUtils.h"
#include "PartitionDelegate.h"


using std::nothrow;


/**
 * @brief Resets all content-level state, leaving the partition uninitialized.
 *
 * Deletes all children, clears the volume ID, content name, content
 * parameters, content size, content type, and resets the status to
 * B_PARTITION_UNINITIALIZED. The block size is inherited from the parent.
 */
void
BMutablePartition::UninitializeContents()
{
	DeleteAllChildren();
	SetVolumeID(-1);
	SetContentName(NULL);
	SetContentParameters(NULL);
	SetContentSize(0);
	SetBlockSize(Parent()->BlockSize());
	SetContentType(NULL);
	SetStatus(B_PARTITION_UNINITIALIZED);
	ClearFlags(B_PARTITION_FILE_SYSTEM | B_PARTITION_PARTITIONING_SYSTEM);
//	if (!Device()->IsReadOnlyMedia())
//		ClearFlags(B_PARTITION_READ_ONLY);
}


/**
 * @brief Returns the byte offset of the partition on the device.
 *
 * @return Current offset in bytes.
 */
off_t
BMutablePartition::Offset() const
{
	return fData->offset;
}


/**
 * @brief Sets the byte offset of the partition and records the change.
 *
 * @param offset New offset in bytes.
 */
void
BMutablePartition::SetOffset(off_t offset)
{
	if (fData->offset != offset) {
		fData->offset = offset;
		Changed(B_PARTITION_CHANGED_OFFSET);
	}
}


/**
 * @brief Returns the size of the partition in bytes.
 *
 * @return Current partition size.
 */
off_t
BMutablePartition::Size() const
{
	return fData->size;
}


/**
 * @brief Sets the partition size and records the change.
 *
 * @param size New size in bytes.
 */
void
BMutablePartition::SetSize(off_t size)
{
	if (fData->size != size) {
		fData->size = size;
		Changed(B_PARTITION_CHANGED_SIZE);
	}
}


/**
 * @brief Returns the usable content size of the partition in bytes.
 *
 * @return Current content size.
 */
off_t
BMutablePartition::ContentSize() const
{
	return fData->content_size;
}


/**
 * @brief Sets the usable content size and records the change.
 *
 * @param size New content size in bytes.
 */
void
BMutablePartition::SetContentSize(off_t size)
{
	if (fData->content_size != size) {
		fData->content_size = size;
		Changed(B_PARTITION_CHANGED_CONTENT_SIZE);
	}
}


/**
 * @brief Returns the logical block size of the partition.
 *
 * @return Current block size in bytes.
 */
off_t
BMutablePartition::BlockSize() const
{
	return fData->block_size;
}


/**
 * @brief Sets the logical block size and records the change.
 *
 * @param blockSize New block size in bytes.
 */
void
BMutablePartition::SetBlockSize(off_t blockSize)
{
	if (fData->block_size != blockSize) {
		fData->block_size = blockSize;
		Changed(B_PARTITION_CHANGED_BLOCK_SIZE);
	}
}


/**
 * @brief Returns the current status of the partition.
 *
 * @return One of the B_PARTITION_* status constants.
 */
uint32
BMutablePartition::Status() const
{
	return fData->status;
}


/**
 * @brief Sets the partition status and records the change.
 *
 * @param status New status value.
 */
void
BMutablePartition::SetStatus(uint32 status)
{
	if (fData->status != status) {
		fData->status = status;
		Changed(B_PARTITION_CHANGED_STATUS);
	}
}


/**
 * @brief Returns the current flags bitmask of the partition.
 *
 * @return Current flags.
 */
uint32
BMutablePartition::Flags() const
{
	return fData->flags;
}


/**
 * @brief Replaces the entire flags bitmask and records the change.
 *
 * @param flags New flags value.
 */
void
BMutablePartition::SetFlags(uint32 flags)
{
	if (fData->flags != flags) {
		fData->flags = flags;
		Changed(B_PARTITION_CHANGED_FLAGS);
	}
}


/**
 * @brief Clears the specified flag bits and records the change if any bit was set.
 *
 * @param flags Bitmask of flags to clear.
 */
void
BMutablePartition::ClearFlags(uint32 flags)
{
	if (flags & fData->flags) {
		fData->flags &= ~flags;
		Changed(B_PARTITION_CHANGED_FLAGS);
	}
}


/**
 * @brief Returns the volume ID associated with this partition's mounted file system.
 *
 * @return Volume device ID, or -1 if not mounted.
 */
dev_t
BMutablePartition::VolumeID() const
{
	return fData->volume;
}


/**
 * @brief Sets the volume ID and records the change.
 *
 * @param volumeID New volume device ID.
 */
void
BMutablePartition::SetVolumeID(dev_t volumeID)
{
	if (fData->volume != volumeID) {
		fData->volume = volumeID;
		Changed(B_PARTITION_CHANGED_VOLUME);
	}
}


/**
 * @brief Returns the index of this partition within its parent's child list.
 *
 * @return Zero-based index.
 */
int32
BMutablePartition::Index() const
{
	return fData->index;
}


/**
 * @brief Returns the human-readable name of the partition.
 *
 * @return Partition name string, or \c NULL.
 */
const char*
BMutablePartition::Name() const
{
	return fData->name;
}


/**
 * @brief Sets the partition name and records the change.
 *
 * Does nothing if the new name is identical to the current name.
 *
 * @param name New name string; may be \c NULL.
 * @return B_OK on success, B_NO_MEMORY if the string copy fails.
 */
status_t
BMutablePartition::SetName(const char* name)
{
	if (compare_string(name, fData->name) == 0)
		return B_OK;

	if (set_string(fData->name, name) != B_OK)
		return B_NO_MEMORY;

	Changed(B_PARTITION_CHANGED_NAME);
	return B_OK;
}


/**
 * @brief Returns the content name of the partition (e.g. volume label).
 *
 * @return Content name as a BString.
 */
BString
BMutablePartition::ContentName() const
{
	return fData->content_name;
}


/**
 * @brief Sets the content name (volume label) and records the change.
 *
 * @param name New content name; may be \c NULL.
 * @return B_OK on success, B_NO_MEMORY if the string copy fails.
 */
status_t
BMutablePartition::SetContentName(const char* name)
{
	if (compare_string(name, fData->content_name) == 0)
		return B_OK;

	if (set_string(fData->content_name, name) != B_OK)
		return B_NO_MEMORY;

	Changed(B_PARTITION_CHANGED_CONTENT_NAME);
	return B_OK;
}


/**
 * @brief Returns the type string of the partition (e.g. partition table entry type).
 *
 * @return Partition type string, or \c NULL.
 */
const char*
BMutablePartition::Type() const
{
	return fData->type;
}


/**
 * @brief Sets the partition type string and records the change.
 *
 * @param type New type string; may be \c NULL.
 * @return B_OK on success, B_NO_MEMORY if the string copy fails.
 */
status_t
BMutablePartition::SetType(const char* type)
{
	if (compare_string(type, fData->type) == 0)
		return B_OK;

	if (set_string(fData->type, type) != B_OK)
		return B_NO_MEMORY;

	Changed(B_PARTITION_CHANGED_TYPE);
	return B_OK;
}


/**
 * @brief Returns the content type string identifying the disk system.
 *
 * @return Content type string, or \c NULL if uninitialized.
 */
const char*
BMutablePartition::ContentType() const
{
	return fData->content_type;
}


/**
 * @brief Sets the content type string and records an initialization change.
 *
 * Changing the content type implies a re-initialization of the partition,
 * so both B_PARTITION_CHANGED_CONTENT_TYPE and
 * B_PARTITION_CHANGED_INITIALIZATION flags are set.
 *
 * @param type New content type string; may be \c NULL.
 * @return B_OK on success, B_NO_MEMORY if the string copy fails.
 */
status_t
BMutablePartition::SetContentType(const char* type)
{
	if (compare_string(type, fData->content_type) == 0)
		return B_OK;

	if (set_string(fData->content_type, type) != B_OK)
		return B_NO_MEMORY;

	Changed(B_PARTITION_CHANGED_CONTENT_TYPE
		| B_PARTITION_CHANGED_INITIALIZATION);
	return B_OK;
}


/**
 * @brief Returns the partition-level parameters string.
 *
 * @return Parameters string, or \c NULL.
 */
const char*
BMutablePartition::Parameters() const
{
	return fData->parameters;
}


/**
 * @brief Sets the partition-level parameters string and records the change.
 *
 * @param parameters New parameters string; may be \c NULL.
 * @return B_OK on success, B_NO_MEMORY if the string copy fails.
 */
status_t
BMutablePartition::SetParameters(const char* parameters)
{
	if (compare_string(parameters, fData->parameters) == 0)
		return B_OK;

	if (set_string(fData->parameters, parameters) != B_OK)
		return B_NO_MEMORY;

	Changed(B_PARTITION_CHANGED_PARAMETERS);
	return B_OK;
}


/**
 * @brief Returns the content-level parameters string used by the disk system.
 *
 * @return Content parameters string, or \c NULL.
 */
const char*
BMutablePartition::ContentParameters() const
{
	return fData->content_parameters;
}


/**
 * @brief Sets the content-level parameters string and records the change.
 *
 * @param parameters New content parameters string; may be \c NULL.
 * @return B_OK on success, B_NO_MEMORY if the string copy fails.
 */
status_t
BMutablePartition::SetContentParameters(const char* parameters)
{
	if (compare_string(parameters, fData->content_parameters) == 0)
		return B_OK;

	if (set_string(fData->content_parameters, parameters) != B_OK)
		return B_NO_MEMORY;

	Changed(B_PARTITION_CHANGED_CONTENT_PARAMETERS);
	return B_OK;
}


/**
 * @brief Creates a new uninitialised child partition at the specified index.
 *
 * Allocates both a BPartition and its Delegate, inserts the new child into
 * the internal child list, and initialises its data structure to a clean
 * uninitialized state.
 *
 * @param index  Zero-based insertion index; -1 appends to the end.
 * @param _child Set to the newly created BMutablePartition on success.
 * @return B_OK on success, B_BAD_VALUE if \a index is out of range, or
 *         B_NO_MEMORY if any allocation fails.
 */
status_t
BMutablePartition::CreateChild(int32 index, BMutablePartition** _child)
{
	if (index < 0)
		index = fChildren.CountItems();
	else if (index > fChildren.CountItems())
		return B_BAD_VALUE;

	// create the BPartition
	BPartition* partition = new(nothrow) BPartition;
	if (!partition)
		return B_NO_MEMORY;

	// create the delegate
	BPartition::Delegate* delegate
		= new(nothrow) BPartition::Delegate(partition);
	if (!delegate) {
		delete partition;
		return B_NO_MEMORY;
	}
	partition->fDelegate = delegate;

	// add the child
	BMutablePartition* child = delegate->MutablePartition();
	if (!fChildren.AddItem(child, index)) {
		delete partition;
		return B_NO_MEMORY;
	}
	child->fParent = this;
	child->fData = new(nothrow) user_partition_data;
	if (!child->fData) {
		fChildren.RemoveItem(child);
		delete partition;
		return B_NO_MEMORY;
	}

	memset(child->fData, 0, sizeof(user_partition_data));

	child->fData->id = -1;
	child->fData->status = B_PARTITION_UNINITIALIZED;
	child->fData->volume = -1;
	child->fData->index = -1;
	child->fData->disk_system = -1;

	*_child = child;

	Changed(B_PARTITION_CHANGED_CHILDREN);
	return B_OK;
}


/**
 * @brief Creates a new child partition and immediately sets its type, name, and parameters.
 *
 * Calls the index-only CreateChild() overload and then applies the provided
 * strings. On any error the partially-created child is cleaned up.
 *
 * @param index      Insertion index; -1 appends.
 * @param type       Partition type string for the new child.
 * @param name       Human-readable name for the new child.
 * @param parameters Optional disk-system-specific parameters.
 * @param _child     Set to the newly created BMutablePartition on success.
 * @return B_OK on success, or an error code if creation or string-setting fails.
 */
status_t
BMutablePartition::CreateChild(int32 index, const char* type, const char* name,
	const char* parameters, BMutablePartition** _child)
{
	// create the child
	BMutablePartition* child;
	status_t error = CreateChild(index, &child);
	if (error != B_OK)
		return error;

	// set the name, type, and parameters
	error = child->SetType(type);
	if (error == B_OK)
		error = child->SetName(name);
	if (error == B_OK)
		error = child->SetParameters(parameters);

	// cleanup on error
	if (error != B_OK) {
		DeleteChild(child);
		return error;
	}

	*_child = child;

	Changed(B_PARTITION_CHANGED_CHILDREN);
	return B_OK;
}


/**
 * @brief Removes and destroys the child at the given index.
 *
 * Triggers recursive deletion of the child's delegate hierarchy and the
 * associated BPartition objects.
 *
 * @param index Zero-based index of the child to delete.
 * @return B_OK on success, B_BAD_VALUE if \a index is out of range.
 */
status_t
BMutablePartition::DeleteChild(int32 index)
{
	BMutablePartition* child = (BMutablePartition*)fChildren.RemoveItem(index);
	if (!child)
		return B_BAD_VALUE;

	// This will delete not only all delegates in the child's hierarchy, but
	// also the respective partitions themselves, if they are no longer
	// referenced.
	child->fDelegate->Partition()->_DeleteDelegates();

	Changed(B_PARTITION_CHANGED_CHILDREN);
	return B_OK;
}


/**
 * @brief Removes and destroys the specified child partition.
 *
 * Convenience wrapper that resolves the child pointer to its index.
 *
 * @param child Pointer to the child to delete.
 * @return B_OK on success, B_BAD_VALUE if \a child is not found.
 */
status_t
BMutablePartition::DeleteChild(BMutablePartition* child)
{
	return DeleteChild(IndexOfChild(child));
}


/**
 * @brief Removes and destroys all child partitions.
 *
 * Iterates in reverse order to avoid index shifting issues.
 */
void
BMutablePartition::DeleteAllChildren()
{
	int32 count = CountChildren();
	for (int32 i = count - 1; i >= 0; i--)
		DeleteChild(i);
}


/**
 * @brief Returns the parent mutable partition, or \c NULL if this is the root.
 *
 * @return Pointer to the parent BMutablePartition.
 */
BMutablePartition*
BMutablePartition::Parent() const
{
	return fParent;
}


/**
 * @brief Returns the child at the given index.
 *
 * @param index Zero-based child index.
 * @return Pointer to the child, or \c NULL if out of range.
 */
BMutablePartition*
BMutablePartition::ChildAt(int32 index) const
{
	return (BMutablePartition*)fChildren.ItemAt(index);
}


/**
 * @brief Returns the number of direct children of this partition.
 *
 * @return Child count.
 */
int32
BMutablePartition::CountChildren() const
{
	return fChildren.CountItems();
}


/**
 * @brief Returns the index of the given child within the child list.
 *
 * @param child The child to look up.
 * @return Zero-based index, or -1 if \a child is NULL or not found.
 */
int32
BMutablePartition::IndexOfChild(BMutablePartition* child) const
{
	if (!child)
		return -1;
	return fChildren.IndexOf(child);
}


/**
 * @brief Replaces the change-flags bitmask with the supplied value.
 *
 * @param flags New change-flags value.
 */
void
BMutablePartition::SetChangeFlags(uint32 flags)
{
	fChangeFlags = flags;
}


/**
 * @brief Returns the current change-flags bitmask.
 *
 * @return Bitmask of B_PARTITION_CHANGED_* flags.
 */
uint32
BMutablePartition::ChangeFlags() const
{
	return fChangeFlags;
}


/**
 * @brief Sets the given change flags and optionally clears others, then propagates upward.
 *
 * After updating the local change-flags bitmask, the parent partition
 * is notified with B_PARTITION_CHANGED_DESCENDANTS so the whole ancestor
 * chain knows that a descendant has been modified.
 *
 * @param flags      Flags to set.
 * @param clearFlags Flags to clear before setting \a flags.
 */
void
BMutablePartition::Changed(uint32 flags, uint32 clearFlags)
{
	fChangeFlags &= ~clearFlags;
	fChangeFlags |= flags;

	if (Parent())
		Parent()->Changed(B_PARTITION_CHANGED_DESCENDANTS);
}


/**
 * @brief Returns the opaque child cookie stored by the disk system add-on.
 *
 * @return The cookie value previously set by SetChildCookie().
 */
void*
BMutablePartition::ChildCookie() const
{
	return fChildCookie;
}


/**
 * @brief Stores an opaque cookie for use by the disk system add-on.
 *
 * @param cookie Arbitrary pointer to store.
 */
void
BMutablePartition::SetChildCookie(void* cookie)
{
	fChildCookie = cookie;
}


/**
 * @brief Constructs a BMutablePartition bound to the given delegate.
 *
 * Called only by BPartition::Delegate. All fields are set to safe defaults;
 * Init() must be called before the object is used.
 *
 * @param delegate The owning delegate.
 */
BMutablePartition::BMutablePartition(BPartition::Delegate* delegate)
	: fDelegate(delegate),
	  fData(NULL),
	  fParent(NULL),
	  fChangeFlags(0),
	  fChildCookie(NULL)
{
}


/**
 * @brief Initialises the mutable partition from live partition data.
 *
 * Registers this partition with its parent's child list, allocates and
 * deep-copies the user_partition_data structure including all string fields.
 *
 * @param partitionData Source partition data to copy.
 * @param parent        Pointer to the parent BMutablePartition, or \c NULL.
 * @return B_OK on success, B_NO_MEMORY if any allocation fails.
 */
status_t
BMutablePartition::Init(const user_partition_data* partitionData,
	BMutablePartition* parent)
{
	fParent = parent;

	// add to the parent's child list
	if (fParent) {
		if (!fParent->fChildren.AddItem(this))
			return B_NO_MEMORY;
	}

	// allocate data structure
	fData = new(nothrow) user_partition_data;
	if (!fData)
		return B_NO_MEMORY;

	memset(fData, 0, sizeof(user_partition_data));

	// copy the flat data
	fData->id = partitionData->id;
	fData->offset = partitionData->offset;
	fData->size = partitionData->size;
	fData->content_size = partitionData->content_size;
	fData->block_size = partitionData->block_size;
	fData->physical_block_size = partitionData->physical_block_size;
	fData->status = partitionData->status;
	fData->flags = partitionData->flags;
	fData->volume = partitionData->volume;
	fData->index = partitionData->index;
	fData->change_counter = partitionData->change_counter;
	fData->disk_system = partitionData->disk_system;

	// copy the strings
	SET_STRING_RETURN_ON_ERROR(fData->name, partitionData->name);
	SET_STRING_RETURN_ON_ERROR(fData->content_name,
		partitionData->content_name);
	SET_STRING_RETURN_ON_ERROR(fData->type, partitionData->type);
	SET_STRING_RETURN_ON_ERROR(fData->content_type,
		partitionData->content_type);
	SET_STRING_RETURN_ON_ERROR(fData->parameters, partitionData->parameters);
	SET_STRING_RETURN_ON_ERROR(fData->content_parameters,
		partitionData->content_parameters);

	return B_OK;
}


/**
 * @brief Destroys the BMutablePartition and frees all owned string fields.
 */
BMutablePartition::~BMutablePartition()
{
	if (fData) {
		free(fData->name);
		free(fData->content_name);
		free(fData->type);
		free(fData->content_type);
		free(fData->parameters);
		free(fData->content_parameters);
		delete fData;
	}
}


/**
 * @brief Returns a read-only pointer to the underlying user_partition_data.
 *
 * @return Pointer to the internal data structure.
 */
const user_partition_data*
BMutablePartition::PartitionData() const
{
	return fData;
}


/**
 * @brief Returns the BPartition::Delegate that owns this mutable partition.
 *
 * @return Pointer to the owning delegate.
 */
BPartition::Delegate*
BMutablePartition::GetDelegate() const
{
	return fDelegate;
}
