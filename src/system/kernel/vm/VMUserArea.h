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
 *   Copyright 2009-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the NewOS License.
 */

/** @file VMUserArea.h
 *  @brief VMArea subclass and AVL tree definition for VMUserAddressSpace. */

#ifndef VM_USER_AREA_H
#define VM_USER_AREA_H


#include <util/AVLTree.h>

#include <vm/VMArea.h>


struct VMUserAddressSpace;


/** @brief VMArea subclass used in user address spaces.
 *
 * VMUserArea inherits from AVLTreeNode so each instance can live directly
 * in the owning address space's area tree without an extra wrapper. */
struct VMUserArea : VMArea, AVLTreeNode {
								VMUserArea(VMAddressSpace* addressSpace,
									uint32 wiring, uint32 protection);
								~VMUserArea();

	/** @brief Allocates a regular user area for @p addressSpace. */
	static	VMUserArea*			Create(VMAddressSpace* addressSpace,
									const char* name, uint32 wiring,
									uint32 protection, uint32 allocationFlags);
	/** @brief Allocates a placeholder area used to mark a reserved address range. */
	static	VMUserArea*			CreateReserved(VMAddressSpace* addressSpace,
									uint32 flags, uint32 allocationFlags);
};


struct VMUserAreaTreeDefinition {
	typedef addr_t					Key;
	typedef VMUserArea				Value;

	AVLTreeNode* GetAVLTreeNode(Value* value) const
	{
		return value;
	}

	Value* GetValue(AVLTreeNode* node) const
	{
		return static_cast<Value*>(node);
	}

	int Compare(addr_t a, const Value* _b) const
	{
		addr_t b = _b->Base();
		if (a == b)
			return 0;
		return a < b ? -1 : 1;
	}

	int Compare(const Value* a, const Value* b) const
	{
		return Compare(a->Base(), b);
	}
};

typedef AVLTree<VMUserAreaTreeDefinition> VMUserAreaTree;


#endif	// VM_USER_AREA_H
