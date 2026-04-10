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
 *   Copyright 2007, Ingo Weinhold, bonefish@users.sf.net.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file PartitionDelegate.cpp
 * @brief BPartition::Delegate implementation bridging BPartition to disk-system add-ons.
 *
 * The Delegate class sits between the public BPartition API and the
 * BDiskSystemAddOn / BPartitionHandle layer. It owns the BMutablePartition
 * shadow, holds a reference to the disk-system add-on for the content type,
 * and forwards all mutating operations (resize, move, create child, etc.) to
 * the BPartitionHandle. It also handles initialisation and uninitialisation
 * of the add-on lifecycle.
 *
 * @see BMutablePartition
 */

#include "PartitionDelegate.h"

#include <stdio.h>

#include <DiskSystemAddOn.h>
#include <DiskSystemAddOnManager.h>

//#define TRACE_PARTITION_DELEGATE
#undef TRACE
#ifdef TRACE_PARTITION_DELEGATE
# define TRACE(x...) printf(x)
#else
# define TRACE(x...) do {} while (false)
#endif


/**
 * @brief Constructs a Delegate bound to the given BPartition.
 *
 * Initialises all pointers to NULL; InitHierarchy() and InitAfterHierarchy()
 * must be called before the delegate is usable.
 *
 * @param partition The BPartition that owns this delegate.
 */
BPartition::Delegate::Delegate(BPartition* partition)
	:
	fPartition(partition),
	fMutablePartition(this),
	fDiskSystem(NULL),
	fPartitionHandle(NULL)
{
}


/**
 * @brief Destroys the Delegate.
 *
 * The partition handle and disk-system add-on reference are released by
 * _FreeHandle() which callers must invoke before destruction if needed.
 */
BPartition::Delegate::~Delegate()
{
}


/**
 * @brief Returns the mutable partition shadow (non-const overload).
 *
 * @return Pointer to the owned BMutablePartition.
 */
BMutablePartition*
BPartition::Delegate::MutablePartition()
{
	return &fMutablePartition;
}


/**
 * @brief Returns the mutable partition shadow (const overload).
 *
 * @return Const pointer to the owned BMutablePartition.
 */
const BMutablePartition*
BPartition::Delegate::MutablePartition() const
{
	return &fMutablePartition;
}


/**
 * @brief Initialises the mutable partition from live partition data.
 *
 * Must be called once for each delegate during the hierarchy-building phase.
 *
 * @param partitionData Live partition data to copy into the shadow.
 * @param parent        The parent delegate, or \c NULL for the root.
 * @return B_OK on success, or B_NO_MEMORY if allocation fails.
 */
status_t
BPartition::Delegate::InitHierarchy(
	const user_partition_data* partitionData, Delegate* parent)
{
	return fMutablePartition.Init(partitionData,
		parent ? &parent->fMutablePartition : NULL);
}


/**
 * @brief Loads the disk-system add-on and creates the partition handle.
 *
 * Called after the full hierarchy has been built. If the partition has no
 * content type the function returns B_OK immediately. On failure the add-on
 * reference is released and the delegate operates without a handle.
 *
 * @return B_OK on success or if no content type is present.
 */
status_t
BPartition::Delegate::InitAfterHierarchy()
{
	TRACE("%p->BPartition::Delegate::InitAfterHierarchy()\n", this);

	if (!fMutablePartition.ContentType()) {
		TRACE("  no content type\n");
		return B_OK;
	}

	// init disk system and handle
	DiskSystemAddOnManager* manager = DiskSystemAddOnManager::Default();
	BDiskSystemAddOn* addOn = manager->GetAddOn(
		fMutablePartition.ContentType());
	if (!addOn) {
		TRACE("  add-on for disk system \"%s\" not found\n",
			fMutablePartition.ContentType());
		return B_OK;
	}

	BPartitionHandle* handle;
	status_t error = addOn->CreatePartitionHandle(&fMutablePartition, &handle);
	if (error != B_OK) {
		TRACE("  failed to create partition handle for partition %ld, disk "
			"system: \"%s\": %s\n",
			Partition()->ID(), addOn->Name(), strerror(error));
		manager->PutAddOn(addOn);
		return error;
	}

	// everything went fine -- keep the disk system add-on reference and the
	// handle
	fDiskSystem = addOn;
	fPartitionHandle = handle;

	return B_OK;
}


/**
 * @brief Returns the current user_partition_data from the mutable shadow.
 *
 * @return Pointer to the internal partition data structure.
 */
const user_partition_data*
BPartition::Delegate::PartitionData() const
{
	return fMutablePartition.PartitionData();
}


/**
 * @brief Returns the child delegate at the given index.
 *
 * @param index Zero-based child index.
 * @return Pointer to the child delegate, or \c NULL if out of range.
 */
BPartition::Delegate*
BPartition::Delegate::ChildAt(int32 index) const
{
	BMutablePartition* child = fMutablePartition.ChildAt(index);
	return child ? child->GetDelegate() : NULL;
}


/**
 * @brief Returns the number of direct child delegates.
 *
 * @return Child count.
 */
int32
BPartition::Delegate::CountChildren() const
{
	return fMutablePartition.CountChildren();
}


/**
 * @brief Returns whether any property of this partition has been modified.
 *
 * @return \c true if any change flag is set, \c false otherwise.
 */
bool
BPartition::Delegate::IsModified() const
{
	return fMutablePartition.ChangeFlags() != 0;
}


/**
 * @brief Returns the subset of \a mask that the partition handle supports.
 *
 * @param mask Bitmask of operation flags to query.
 * @return Supported operations bitmask, or 0 if no handle is present.
 */
uint32
BPartition::Delegate::SupportedOperations(uint32 mask)
{
	if (!fPartitionHandle)
		return 0;

	return fPartitionHandle->SupportedOperations(mask);
}


/**
 * @brief Returns the child operations supported by the partition handle for a given child.
 *
 * @param child The child delegate to query operations for.
 * @param mask  Bitmask of operation flags to check.
 * @return Supported child operations bitmask, or 0 if no handle is present.
 */
uint32
BPartition::Delegate::SupportedChildOperations(Delegate* child,
	uint32 mask)
{
	if (!fPartitionHandle)
		return 0;

	return fPartitionHandle->SupportedChildOperations(child->MutablePartition(),
		mask);
}


/**
 * @brief Defragments the partition's file system.
 *
 * @return B_BAD_VALUE (not yet implemented).
 */
status_t
BPartition::Delegate::Defragment()
{
// TODO: Implement!
	return B_BAD_VALUE;
}


/**
 * @brief Checks or repairs the partition's file system.
 *
 * @param checkOnly When \c true only a consistency check is performed.
 * @return B_NO_INIT if no handle is present, or the result from the handle.
 */
status_t
BPartition::Delegate::Repair(bool checkOnly)
{
	if (fPartitionHandle == NULL)
		return B_NO_INIT;

	return fPartitionHandle->Repair(checkOnly);
}


/**
 * @brief Validates and adjusts a proposed new size for this partition.
 *
 * @param size In/out: the requested size; adjusted to a valid value on return.
 * @return B_NO_INIT if no handle, or the result from the handle.
 */
status_t
BPartition::Delegate::ValidateResize(off_t* size) const
{
	if (!fPartitionHandle)
		return B_NO_INIT;

	return fPartitionHandle->ValidateResize(size);
}


/**
 * @brief Validates and adjusts a proposed new size for a child partition.
 *
 * @param child The child to resize.
 * @param size  In/out: the requested size; adjusted on return.
 * @return B_NO_INIT if no handle or child, or the result from the handle.
 */
status_t
BPartition::Delegate::ValidateResizeChild(Delegate* child, off_t* size) const
{
	if (!fPartitionHandle || !child)
		return B_NO_INIT;

	return fPartitionHandle->ValidateResizeChild(&child->fMutablePartition,
		size);
}


/**
 * @brief Resizes this partition to the given size.
 *
 * @param size New size in bytes.
 * @return B_NO_INIT if no handle, or the result from the handle.
 */
status_t
BPartition::Delegate::Resize(off_t size)
{
	if (!fPartitionHandle)
		return B_NO_INIT;

	return fPartitionHandle->Resize(size);
}


/**
 * @brief Resizes a child partition to the given size.
 *
 * @param child The child to resize.
 * @param size  New size in bytes.
 * @return B_NO_INIT if no handle or child, or the result from the handle.
 */
status_t
BPartition::Delegate::ResizeChild(Delegate* child, off_t size)
{
	if (!fPartitionHandle || !child)
		return B_NO_INIT;

	return fPartitionHandle->ResizeChild(&child->fMutablePartition, size);
}


/**
 * @brief Validates and adjusts a proposed new offset for this partition.
 *
 * @param offset In/out: the requested offset; adjusted on return.
 * @return B_NO_INIT if no handle, or the result from the handle.
 */
status_t
BPartition::Delegate::ValidateMove(off_t* offset) const
{
	if (!fPartitionHandle)
		return B_NO_INIT;

	return fPartitionHandle->ValidateMove(offset);
}


/**
 * @brief Validates and adjusts a proposed new offset for a child partition.
 *
 * @param child  The child to move.
 * @param offset In/out: the requested offset; adjusted on return.
 * @return B_NO_INIT if no handle or child, or the result from the handle.
 */
status_t
BPartition::Delegate::ValidateMoveChild(Delegate* child, off_t* offset) const
{
	if (!fPartitionHandle || !child)
		return B_NO_INIT;

	return fPartitionHandle->ValidateMoveChild(&child->fMutablePartition,
		offset);
}


/**
 * @brief Moves this partition to the given offset.
 *
 * @param offset New byte offset on the device.
 * @return B_NO_INIT if no handle, or the result from the handle.
 */
status_t
BPartition::Delegate::Move(off_t offset)
{
	if (!fPartitionHandle)
		return B_NO_INIT;

	return fPartitionHandle->Move(offset);
}


/**
 * @brief Moves a child partition to the given offset.
 *
 * @param child  The child to move.
 * @param offset New byte offset on the device.
 * @return B_NO_INIT if no handle or child, or the result from the handle.
 */
status_t
BPartition::Delegate::MoveChild(Delegate* child, off_t offset)
{
	if (!fPartitionHandle || !child)
		return B_NO_INIT;

	return fPartitionHandle->MoveChild(&child->fMutablePartition, offset);
}


/**
 * @brief Validates and adjusts a proposed content name for this partition.
 *
 * @param name In/out: the requested name; adjusted on return.
 * @return B_NO_INIT if no handle, or the result from the handle.
 */
status_t
BPartition::Delegate::ValidateSetContentName(BString* name) const
{
	if (!fPartitionHandle)
		return B_NO_INIT;

	return fPartitionHandle->ValidateSetContentName(name);
}


/**
 * @brief Validates and adjusts a proposed name for a child partition.
 *
 * @param child The child whose name is to be validated.
 * @param name  In/out: the requested name; adjusted on return.
 * @return B_NO_INIT if no handle or child, or the result from the handle.
 */
status_t
BPartition::Delegate::ValidateSetName(Delegate* child, BString* name) const
{
	if (!fPartitionHandle || !child)
		return B_NO_INIT;

	return fPartitionHandle->ValidateSetName(&child->fMutablePartition, name);
}


/**
 * @brief Sets the content name of this partition.
 *
 * @param name New content name string.
 * @return B_NO_INIT if no handle, or the result from the handle.
 */
status_t
BPartition::Delegate::SetContentName(const char* name)
{
	if (!fPartitionHandle)
		return B_NO_INIT;

	return fPartitionHandle->SetContentName(name);
}


/**
 * @brief Sets the name of a child partition.
 *
 * @param child The child whose name is to be changed.
 * @param name  New name string.
 * @return B_NO_INIT if no handle or child, or the result from the handle.
 */
status_t
BPartition::Delegate::SetName(Delegate* child, const char* name)
{
	if (!fPartitionHandle || !child)
		return B_NO_INIT;

	return fPartitionHandle->SetName(&child->fMutablePartition, name);
}


/**
 * @brief Validates a proposed type string for a child partition.
 *
 * @param child The child whose type is to be validated.
 * @param type  The proposed type string.
 * @return B_NO_INIT if no handle or child, or the result from the handle.
 */
status_t
BPartition::Delegate::ValidateSetType(Delegate* child, const char* type) const
{
	if (!fPartitionHandle || !child)
		return B_NO_INIT;

	return fPartitionHandle->ValidateSetType(&child->fMutablePartition, type);
}


/**
 * @brief Sets the type string of a child partition.
 *
 * @param child The child whose type is to be changed.
 * @param type  New type string.
 * @return B_NO_INIT if no handle or child, or the result from the handle.
 */
status_t
BPartition::Delegate::SetType(Delegate* child, const char* type)
{
	if (!fPartitionHandle || !child)
		return B_NO_INIT;

	return fPartitionHandle->SetType(&child->fMutablePartition, type);
}


/**
 * @brief Sets the content-level parameters of this partition.
 *
 * @param parameters New content parameters string.
 * @return B_NO_INIT if no handle, or the result from the handle.
 */
status_t
BPartition::Delegate::SetContentParameters(const char* parameters)
{
	if (!fPartitionHandle)
		return B_NO_INIT;

	return fPartitionHandle->SetContentParameters(parameters);
}


/**
 * @brief Sets the partition-level parameters of a child partition.
 *
 * @param child      The child whose parameters are to be changed.
 * @param parameters New parameters string.
 * @return B_NO_INIT if no handle or child, or the result from the handle.
 */
status_t
BPartition::Delegate::SetParameters(Delegate* child, const char* parameters)
{
	if (!fPartitionHandle || !child)
		return B_NO_INIT;

	return fPartitionHandle->SetParameters(&child->fMutablePartition,
		parameters);
}


/**
 * @brief Iterates over the partition types supported for a child slot.
 *
 * @param child  The child for which to enumerate types; may be \c NULL.
 * @param cookie Iteration cookie; initialised to 0 on first call.
 * @param type   Set to the next supported type string on success.
 * @return B_OK while types remain, B_ENTRY_NOT_FOUND when exhausted, or
 *         B_NO_INIT if no handle is present.
 */
status_t
BPartition::Delegate::GetNextSupportedChildType(Delegate* child,
	int32* cookie, BString* type) const
{
	TRACE("%p->BPartition::Delegate::GetNextSupportedChildType(child: %p, "
		"cookie: %ld)\n", this, child, *cookie);

	if (!fPartitionHandle) {
		TRACE("  no partition handle!\n");
		return B_NO_INIT;
	}

	return fPartitionHandle->GetNextSupportedType(
		child ? &child->fMutablePartition : NULL, cookie, type);
}


/**
 * @brief Checks whether a given disk system is a valid sub-system for a child.
 *
 * Loads the named add-on temporarily to call its IsSubSystemFor() predicate.
 *
 * @param child      The child partition to test against.
 * @param diskSystem Name of the disk system add-on to query.
 * @return \c true if the disk system can be used as a sub-system, \c false
 *         otherwise.
 */
bool
BPartition::Delegate::IsSubSystem(Delegate* child,
	const char* diskSystem) const
{
	// get the disk system add-on
	DiskSystemAddOnManager* manager = DiskSystemAddOnManager::Default();
	BDiskSystemAddOn* addOn = manager->GetAddOn(diskSystem);
	if (!addOn)
		return false;

	bool result = addOn->IsSubSystemFor(&child->fMutablePartition);

	// put the add-on
	manager->PutAddOn(addOn);

	return result;
}


/**
 * @brief Checks whether this partition can be initialised with the given disk system.
 *
 * @param diskSystem Name of the disk system add-on to query.
 * @return \c true if initialization is possible, \c false otherwise.
 */
bool
BPartition::Delegate::CanInitialize(const char* diskSystem) const
{
	// get the disk system add-on
	DiskSystemAddOnManager* manager = DiskSystemAddOnManager::Default();
	BDiskSystemAddOn* addOn = manager->GetAddOn(diskSystem);
	if (!addOn)
		return false;

	bool result = addOn->CanInitialize(&fMutablePartition);

	// put the add-on
	manager->PutAddOn(addOn);

	return result;
}


/**
 * @brief Validates and adjusts the name and parameters for a proposed initialization.
 *
 * @param diskSystem Name of the disk system to initialize with.
 * @param name       In/out: proposed content name; adjusted on return.
 * @param parameters Disk-system-specific initialization parameters.
 * @return B_ENTRY_NOT_FOUND if the add-on is missing, or the result from
 *         the add-on's ValidateInitialize().
 */
status_t
BPartition::Delegate::ValidateInitialize(const char* diskSystem,
	BString* name, const char* parameters)
{
	// get the disk system add-on
	DiskSystemAddOnManager* manager = DiskSystemAddOnManager::Default();
	BDiskSystemAddOn* addOn = manager->GetAddOn(diskSystem);
	if (!addOn)
		return B_ENTRY_NOT_FOUND;

	status_t result = addOn->ValidateInitialize(&fMutablePartition,
		name, parameters);

	// put the add-on
	manager->PutAddOn(addOn);

	return result;
}


/**
 * @brief Initialises the partition with the specified disk system.
 *
 * Loads the add-on, calls its Initialize() method, and on success replaces
 * the current handle with the newly created one. On failure the add-on
 * reference is released.
 *
 * @param diskSystem Name of the disk system add-on to use.
 * @param name       Content name (volume label) for the new file system.
 * @param parameters Disk-system-specific initialization parameters.
 * @return B_ENTRY_NOT_FOUND if the add-on is missing, or the result from
 *         the add-on's Initialize().
 */
status_t
BPartition::Delegate::Initialize(const char* diskSystem,
	const char* name, const char* parameters)
{
	// get the disk system add-on
	DiskSystemAddOnManager* manager = DiskSystemAddOnManager::Default();
	BDiskSystemAddOn* addOn = manager->GetAddOn(diskSystem);
	if (!addOn)
		return B_ENTRY_NOT_FOUND;

	BPartitionHandle* handle;
	status_t result = addOn->Initialize(&fMutablePartition, name, parameters,
		&handle);

	// keep the add-on or put it on error
	if (result == B_OK) {
		// TODO: This won't suffice. If this partition had children, we have
		// to delete them before the new disk system plays with it.
		_FreeHandle();
		fDiskSystem = addOn;
		fPartitionHandle = handle;
	} else {
		manager->PutAddOn(addOn);
	}

	return result;
}


/**
 * @brief Uninitialises the partition, releasing its disk-system add-on.
 *
 * Calls _FreeHandle() and then resets the mutable partition contents to
 * the uninitialized state.
 *
 * @return B_OK always.
 */
status_t
BPartition::Delegate::Uninitialize()
{
	if (fPartitionHandle) {
		_FreeHandle();

		fMutablePartition.UninitializeContents();
	}

	return B_OK;
}


/**
 * @brief Retrieves the partitionable space information from the handle.
 *
 * @param info Object to fill with available partitionable space data.
 * @return B_NO_INIT if no handle, or the result from the handle.
 */
status_t
BPartition::Delegate::GetPartitioningInfo(BPartitioningInfo* info)
{
	if (!fPartitionHandle)
		return B_NO_INIT;

	return fPartitionHandle->GetPartitioningInfo(info);
}


/**
 * @brief Retrieves a parameter editor for the given editor type.
 *
 * @param type   The kind of parameter editor requested.
 * @param editor Set to the newly created editor on success.
 * @return B_NO_INIT if no handle, or the result from the handle.
 */
status_t
BPartition::Delegate::GetParameterEditor(B_PARAMETER_EDITOR_TYPE type,
	BPartitionParameterEditor** editor) const
{
	if (!fPartitionHandle)
		return B_NO_INIT;

	return fPartitionHandle->GetParameterEditor(type, editor);
}


/**
 * @brief Validates the parameters for creating a new child partition.
 *
 * @param start      In/out: proposed start offset; adjusted on return.
 * @param size       In/out: proposed size; adjusted on return.
 * @param type       Partition type string for the proposed child.
 * @param name       In/out: proposed name; adjusted on return.
 * @param parameters Disk-system-specific creation parameters.
 * @return B_NO_INIT if no handle, or the result from the handle.
 */
status_t
BPartition::Delegate::ValidateCreateChild(off_t* start, off_t* size,
	const char* type, BString* name, const char* parameters) const
{
	if (!fPartitionHandle)
		return B_NO_INIT;

	return fPartitionHandle->ValidateCreateChild(start, size, type, name,
		parameters);
}


/**
 * @brief Creates a new child partition via the partition handle.
 *
 * On success the optional \a child pointer is set to the newly created
 * BPartition.
 *
 * @param start      Byte offset for the new child partition.
 * @param size       Size of the new child partition.
 * @param type       Partition type string.
 * @param name       Human-readable name.
 * @param parameters Disk-system-specific creation parameters.
 * @param child      Optional output: set to the new BPartition on success.
 * @return B_NO_INIT if no handle, or the result from the handle.
 */
status_t
BPartition::Delegate::CreateChild(off_t start, off_t size, const char* type,
	const char* name, const char* parameters, BPartition** child)
{
	if (!fPartitionHandle)
		return B_NO_INIT;

	BMutablePartition* mutableChild;
	status_t error = fPartitionHandle->CreateChild(start, size, type, name,
		parameters, &mutableChild);
	if (error != B_OK)
		return error;

	if (child)
		*child = mutableChild->GetDelegate()->Partition();

	return B_OK;
}


/**
 * @brief Deletes a child partition via the partition handle.
 *
 * @param child The delegate for the child partition to delete.
 * @return B_NO_INIT if no handle or child is NULL, or the result from the
 *         handle.
 */
status_t
BPartition::Delegate::DeleteChild(Delegate* child)
{
	if (!fPartitionHandle || !child)
		return B_NO_INIT;

	return fPartitionHandle->DeleteChild(&child->fMutablePartition);
}


/**
 * @brief Releases the partition handle and the associated disk-system add-on reference.
 *
 * Safe to call when no handle is present; does nothing in that case.
 */
void
BPartition::Delegate::_FreeHandle()
{
	if (fPartitionHandle) {
		delete fPartitionHandle;
		fPartitionHandle = NULL;

		DiskSystemAddOnManager* manager = DiskSystemAddOnManager::Default();
		manager->PutAddOn(fDiskSystem);
		fDiskSystem = NULL;
	}
}
