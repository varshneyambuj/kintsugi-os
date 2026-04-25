/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2007, Haiku.
 * Original author: Ingo Weinhold.
 */

/** @file PartitionDelegate.h
    @brief Private bridge between BPartition and the disk-system add-on that
    backs it, holding the mutable view and forwarding operations. */

#ifndef _PARTITION_DELEGATE_H
#define _PARTITION_DELEGATE_H

#include <DiskSystemAddOn.h>
#include <MutablePartition.h>
#include <Partition.h>


class BDiskSystemAddOn;
class BPartitionHandle;


/** @brief Per-partition delegate that routes BPartition calls to the disk-system add-on. */
class BPartition::Delegate {
public:
								Delegate(BPartition* partition);
								~Delegate();

			BPartition*			Partition() const	{ return fPartition; }

			BMutablePartition*	MutablePartition();
			const BMutablePartition* MutablePartition() const;

			status_t			InitHierarchy(
									const user_partition_data* partitionData,
									Delegate* parent);
			status_t			InitAfterHierarchy();

			const user_partition_data* PartitionData() const;

			Delegate*			ChildAt(int32 index) const;
			int32				CountChildren() const;

			bool				IsModified() const;

			uint32				SupportedOperations(uint32 mask);
			uint32				SupportedChildOperations(Delegate* child,
									uint32 mask);

			// Self Modification

			status_t			Defragment();
			status_t			Repair(bool checkOnly);

			status_t			ValidateResize(off_t* size) const;
			status_t			ValidateResizeChild(Delegate* child,
									off_t* size) const;
			status_t			Resize(off_t size);
			status_t			ResizeChild(Delegate* child, off_t size);

			status_t			ValidateMove(off_t* offset) const;
			status_t			ValidateMoveChild(Delegate* child,
									off_t* offset) const;
			status_t			Move(off_t offset);
			status_t			MoveChild(Delegate* child, off_t offset);

			status_t			ValidateSetContentName(BString* name) const;
			status_t			ValidateSetName(Delegate* child,
									BString* name) const;
			status_t			SetContentName(const char* name);
			status_t			SetName(Delegate* child, const char* name);

			status_t			ValidateSetType(Delegate* child,
									const char* type) const;
			status_t			SetType(Delegate* child, const char* type);

			status_t			SetContentParameters(const char* parameters);
			status_t			SetParameters(Delegate* child,
									const char* parameters);

			status_t			GetParameterEditor(
									B_PARAMETER_EDITOR_TYPE type,
									BPartitionParameterEditor** editor) const;
			status_t			GetNextSupportedChildType(Delegate* child,
									int32* cookie, BString* type) const;
			bool				IsSubSystem(Delegate* child,
									const char* diskSystem) const;

			bool				CanInitialize(const char* diskSystem) const;
			status_t			ValidateInitialize(const char* diskSystem,
									BString* name, const char* parameters);
			status_t			Initialize(const char* diskSystem,
									const char* name,
									const char* parameters);
			status_t			Uninitialize();
	
			// Modification of child partitions

			status_t			GetPartitioningInfo(BPartitioningInfo* info);

			status_t			ValidateCreateChild(off_t* start, off_t* size,
									const char* type, BString* name,
									const char* parameters) const;
			status_t			CreateChild(off_t start, off_t size,
									const char* type, const char* name,
									const char* parameters, BPartition** child);
	
			status_t			DeleteChild(Delegate* child);

private:
			void				_FreeHandle();

private:
			BPartition*			fPartition;
			BMutablePartition	fMutablePartition;
			BDiskSystemAddOn*	fDiskSystem;
			BPartitionHandle*	fPartitionHandle;
};


#endif	// _PARTITION_DELEGATE_H

