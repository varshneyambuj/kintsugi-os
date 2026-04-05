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
 *   Copyright 2007, Ingo Weinhold, bonefish@users.sf.net.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file DiskSystemAddOn.cpp
 * @brief Base classes for disk system add-on plug-ins and partition handles.
 *
 * Provides the default implementations of BDiskSystemAddOn, which plug-ins
 * derive from to advertise and operate a partitioning scheme or file system,
 * and BPartitionHandle, which mediates all structural operations on a mounted
 * partition. Both classes return conservative defaults (unsupported or bad
 * value) so that subclasses only need to override the operations they support.
 *
 * @see DiskSystemAddOnManager
 * @see BDiskSystem
 */

#include <DiskSystemAddOn.h>

#include <DiskDeviceDefs.h>
#include <Errors.h>


// #pragma mark - BDiskSystemAddOn


/**
 * @brief Constructs a BDiskSystemAddOn with the given system name.
 *
 * @param name The canonical name string that identifies this disk system.
 */
BDiskSystemAddOn::BDiskSystemAddOn(const char* name)
	:
	fName(name)
{
}


/**
 * @brief Destroys the BDiskSystemAddOn.
 */
BDiskSystemAddOn::~BDiskSystemAddOn()
{
}


/**
 * @brief Returns the canonical name of this disk system.
 *
 * @return A null-terminated string containing the disk system name.
 */
const char*
BDiskSystemAddOn::Name() const
{
	return fName.String();
}


/**
 * @brief Indicates whether this add-on can initialize the given partition.
 *
 * The base class always returns false. Subclasses should override this to
 * inspect the partition and return true when initialization is feasible.
 *
 * @param partition The candidate partition to evaluate.
 * @return false by default; true if the add-on supports initializing the
 *         partition.
 */
bool
BDiskSystemAddOn::CanInitialize(const BMutablePartition* partition)
{
	return false;
}


/**
 * @brief Returns a parameter editor for the given editor type.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param type   The kind of parameter editor requested.
 * @param editor Output pointer set to the newly created editor on success.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BDiskSystemAddOn::GetParameterEditor(B_PARAMETER_EDITOR_TYPE type,
	BPartitionParameterEditor** editor)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Validates the parameters for initializing a partition.
 *
 * The base class always returns B_BAD_VALUE. Subclasses should override
 * this to sanitise or adjust the proposed name and parameters.
 *
 * @param partition  The partition to be initialized.
 * @param name       In/out parameter holding the desired content name.
 * @param parameters The initialization parameters string.
 * @return B_BAD_VALUE by default.
 */
status_t
BDiskSystemAddOn::ValidateInitialize(const BMutablePartition* partition,
	BString* name, const char* parameters)
{
	return B_BAD_VALUE;
}


/**
 * @brief Initializes the partition with the given name and parameters.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param partition  The partition to initialize.
 * @param name       The desired content name.
 * @param parameters The initialization parameters string.
 * @param handle     Output pointer set to the resulting BPartitionHandle.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BDiskSystemAddOn::Initialize(BMutablePartition* partition, const char* name,
	const char* parameters, BPartitionHandle** handle)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Maps a content type string to a partition type string.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param contentType The content (file-system) type to look up.
 * @param type        Output BString set to the corresponding partition type.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BDiskSystemAddOn::GetTypeForContentType(const char* contentType, BString* type)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Returns whether this disk system can act as a sub-system for child.
 *
 * The base class always returns false.
 *
 * @param child The child partition to evaluate.
 * @return false by default.
 */
bool
BDiskSystemAddOn::IsSubSystemFor(const BMutablePartition* child)
{
	return false;
}


// #pragma mark - BPartitionHandle


/**
 * @brief Constructs a BPartitionHandle bound to the given mutable partition.
 *
 * @param partition The mutable partition this handle will operate on.
 */
BPartitionHandle::BPartitionHandle(BMutablePartition* partition)
	:
	fPartition(partition)
{
}


/**
 * @brief Destroys the BPartitionHandle.
 */
BPartitionHandle::~BPartitionHandle()
{
}


/**
 * @brief Returns the mutable partition associated with this handle.
 *
 * @return Pointer to the BMutablePartition supplied at construction.
 */
BMutablePartition*
BPartitionHandle::Partition() const
{
	return fPartition;
}


/**
 * @brief Returns the set of operations supported by this handle.
 *
 * The base class always returns 0 (no operations supported). Subclasses
 * should return the bitwise OR of relevant B_DISK_SYSTEM_SUPPORTS_* flags
 * that are enabled for the given mask.
 *
 * @param mask Bitmask of operations the caller is interested in.
 * @return 0 by default.
 */
uint32
BPartitionHandle::SupportedOperations(uint32 mask)
{
	return 0;
}


/**
 * @brief Returns the set of operations supported on a child partition.
 *
 * The base class always returns 0.
 *
 * @param child The child partition to query.
 * @param mask  Bitmask of operations the caller is interested in.
 * @return 0 by default.
 */
uint32
BPartitionHandle::SupportedChildOperations(const BMutablePartition* child,
	uint32 mask)
{
	return 0;
}


/**
 * @brief Returns whether this handle allows the child to be initialized with
 *        the named disk system.
 *
 * The base class always returns false.
 *
 * @param child      The child partition to evaluate.
 * @param diskSystem Name of the disk system to test.
 * @return false by default.
 */
bool
BPartitionHandle::SupportsInitializingChild(const BMutablePartition* child,
	const char* diskSystem)
{
	return false;
}


/**
 * @brief Iterates through the supported partition types for a child.
 *
 * The base class always returns B_ENTRY_NOT_FOUND, indicating an empty list.
 *
 * @param child  The child partition being queried.
 * @param cookie Iteration state cookie; initialize to 0 before the first call.
 * @param type   Output BString set to the next supported type.
 * @return B_ENTRY_NOT_FOUND by default.
 */
status_t
BPartitionHandle::GetNextSupportedType(const BMutablePartition* child,
	int32* cookie, BString* type)
{
	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Returns partitioning information for this partition.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param info Output object to be filled with partitioning information.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::GetPartitioningInfo(BPartitioningInfo* info)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Defragments the partition's content.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::Defragment()
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Checks or repairs the partition's content.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param checkOnly If true, only check for errors without repairing them.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::Repair(bool checkOnly)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Validates and adjusts the proposed new size for this partition.
 *
 * The base class always returns B_BAD_VALUE.
 *
 * @param size In/out parameter with the desired size; may be adjusted to a
 *             valid value.
 * @return B_BAD_VALUE by default.
 */
status_t
BPartitionHandle::ValidateResize(off_t* size)
{
	return B_BAD_VALUE;
}


/**
 * @brief Validates and adjusts the proposed new size for a child partition.
 *
 * The base class always returns B_BAD_VALUE.
 *
 * @param child The child partition to resize.
 * @param size  In/out parameter with the desired size.
 * @return B_BAD_VALUE by default.
 */
status_t
BPartitionHandle::ValidateResizeChild(const BMutablePartition* child,
	off_t* size)
{
	return B_BAD_VALUE;
}


/**
 * @brief Resizes this partition to the given size.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param size The new size in bytes.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::Resize(off_t size)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Resizes a child partition to the given size.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param child The child partition to resize.
 * @param size  The new size in bytes.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::ResizeChild(BMutablePartition* child, off_t size)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Validates and adjusts the proposed offset for moving this partition.
 *
 * Moving a content disk system is usually a no-op, so the base class returns
 * B_OK without modifying the offset.
 *
 * @param offset In/out parameter with the desired start offset.
 * @return B_OK by default.
 */
status_t
BPartitionHandle::ValidateMove(off_t* offset)
{
	// Usually moving a disk system is a no-op for the content disk system,
	// so we default to true here.
	return B_OK;
}


/**
 * @brief Validates and adjusts the proposed offset for moving a child.
 *
 * The base class always returns B_BAD_VALUE.
 *
 * @param child  The child partition to move.
 * @param offset In/out parameter with the desired start offset.
 * @return B_BAD_VALUE by default.
 */
status_t
BPartitionHandle::ValidateMoveChild(const BMutablePartition* child,
	off_t* offset)
{
	return B_BAD_VALUE;
}


/**
 * @brief Moves this partition to the given offset.
 *
 * Moving a content disk system is usually a no-op, so the base class returns
 * B_OK.
 *
 * @param offset The new start offset in bytes.
 * @return B_OK by default.
 */
status_t
BPartitionHandle::Move(off_t offset)
{
	// Usually moving a disk system is a no-op for the content disk system,
	// so we default to OK here.
	return B_OK;
}


/**
 * @brief Moves a child partition to the given offset.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param child  The child partition to move.
 * @param offset The new start offset in bytes.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::MoveChild(BMutablePartition* child, off_t offset)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Validates the proposed new content name for this partition.
 *
 * The base class always returns B_BAD_VALUE.
 *
 * @param name In/out parameter holding the desired content name.
 * @return B_BAD_VALUE by default.
 */
status_t
BPartitionHandle::ValidateSetContentName(BString* name)
{
	return B_BAD_VALUE;
}


/**
 * @brief Validates the proposed name for a child partition.
 *
 * The base class always returns B_BAD_VALUE.
 *
 * @param child The child partition to rename.
 * @param name  In/out parameter holding the desired name.
 * @return B_BAD_VALUE by default.
 */
status_t
BPartitionHandle::ValidateSetName(const BMutablePartition* child,
	BString* name)
{
	return B_BAD_VALUE;
}


/**
 * @brief Sets the content name of this partition.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param name The new content name string.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::SetContentName(const char* name)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Sets the name of a child partition.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param child The child partition to rename.
 * @param name  The new name string.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::SetName(BMutablePartition* child, const char* name)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Validates the proposed type string for a child partition.
 *
 * The base class always returns B_BAD_VALUE.
 *
 * @param child The child partition whose type is being changed.
 * @param type  The proposed new type string.
 * @return B_BAD_VALUE by default.
 */
status_t
BPartitionHandle::ValidateSetType(const BMutablePartition* child,
	const char* type)
{
	return B_BAD_VALUE;
}


/**
 * @brief Sets the type of a child partition.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param child The child partition to update.
 * @param type  The new type string.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::SetType(BMutablePartition* child, const char* type)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Returns a parameter editor for the content of this partition.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param editor Output pointer set to the newly created editor on success.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::GetContentParameterEditor(BPartitionParameterEditor** editor)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Returns a parameter editor of the specified type.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param type   The kind of parameter editor requested.
 * @param editor Output pointer set to the newly created editor on success.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::GetParameterEditor(B_PARAMETER_EDITOR_TYPE type,
	BPartitionParameterEditor** editor)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Validates the proposed content parameters string.
 *
 * The base class always returns B_BAD_VALUE.
 *
 * @param parameters The proposed parameters string to validate.
 * @return B_BAD_VALUE by default.
 */
status_t
BPartitionHandle::ValidateSetContentParameters(const char* parameters)
{
	return B_BAD_VALUE;
}


/**
 * @brief Validates the proposed parameters string for a child partition.
 *
 * The base class always returns B_BAD_VALUE.
 *
 * @param child      The child partition to validate parameters for.
 * @param parameters The proposed parameters string.
 * @return B_BAD_VALUE by default.
 */
status_t
BPartitionHandle::ValidateSetParameters(const BMutablePartition* child,
	const char* parameters)
{
	return B_BAD_VALUE;
}


/**
 * @brief Applies new content parameters to this partition.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param parameters The new parameters string.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::SetContentParameters(const char* parameters)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Applies new parameters to a child partition.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param child      The child partition to update.
 * @param parameters The new parameters string.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::SetParameters(BMutablePartition* child,
	const char* parameters)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Validates the attributes of a new child partition before creation.
 *
 * The base class always returns B_BAD_VALUE.
 *
 * @param offset     In/out proposed start offset for the new child.
 * @param size       In/out proposed size for the new child.
 * @param type       The partition type string.
 * @param name       In/out proposed name for the new child.
 * @param parameters The creation parameters string.
 * @return B_BAD_VALUE by default.
 */
status_t
BPartitionHandle::ValidateCreateChild(off_t* offset, off_t* size,
	const char* type, BString* name, const char* parameters)
{
	return B_BAD_VALUE;
}


/**
 * @brief Creates a new child partition with the specified attributes.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param offset     Start offset in bytes for the new child.
 * @param size       Size in bytes for the new child.
 * @param type       The partition type string.
 * @param name       The name for the new child.
 * @param parameters The creation parameters string.
 * @param child      Output pointer set to the newly created BMutablePartition.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::CreateChild(off_t offset, off_t size, const char* type,
	const char* name, const char* parameters, BMutablePartition** child)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Deletes a child partition.
 *
 * The base class always returns B_NOT_SUPPORTED.
 *
 * @param child The child partition to delete.
 * @return B_NOT_SUPPORTED by default.
 */
status_t
BPartitionHandle::DeleteChild(BMutablePartition* child)
{
	return B_NOT_SUPPORTED;
}
