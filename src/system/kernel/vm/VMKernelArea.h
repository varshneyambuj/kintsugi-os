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
 *   Copyright 2009-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the NewOS License.
 */

/** @file VMKernelArea.h
 *  @brief Kernel area type and the address-range descriptor used by VMKernelAddressSpace. */

#ifndef VM_KERNEL_AREA_H
#define VM_KERNEL_AREA_H


#include <util/AVLTree.h>

#include <vm/VMArea.h>


struct ObjectCache;
struct VMKernelAddressSpace;
struct VMKernelArea;


/** @brief One contiguous range in the kernel address space.
 *
 * Each range covers either a live area, a reservation, or a free hole. The
 * descriptors live in an AVL tree (sorted by base address), an in-order
 * doubly-linked list, and — when free — a per-size free list. The union at
 * the end stores the per-type payload to keep the structure compact. */
struct VMKernelAddressRange : AVLTreeNode {
public:
	/** @brief Discriminator for the union below. */
	enum {
		RANGE_FREE,      /**< Range is unallocated and lives on a free list. */
		RANGE_RESERVED,  /**< Range is reserved for future use but has no area yet. */
		RANGE_AREA       /**< Range is occupied by a live VMKernelArea. */
	};

public:
	DoublyLinkedListLink<VMKernelAddressRange>		listLink;  /**< In-order list link. */
	addr_t											base;      /**< First byte of the range. */
	size_t											size;      /**< Length of the range in bytes. */
	/** @brief Per-type payload (which member is valid is determined by @c type). */
	union {
		VMKernelArea*								area;          /**< RANGE_AREA: live area in this range. */
		struct {
			addr_t									base;          /**< Original base supplied to the reservation. */
			uint32									flags;         /**< Reservation flags. */
		} reserved;
		DoublyLinkedListLink<VMKernelAddressRange>	freeListLink;  /**< RANGE_FREE: link in the size-bucket free list. */
	};
	int												type;      /**< One of RANGE_FREE / RANGE_RESERVED / RANGE_AREA. */

public:
	VMKernelAddressRange(addr_t base, size_t size, int type)
		:
		base(base),
		size(size),
		type(type)
	{
	}

	VMKernelAddressRange(addr_t base, size_t size,
		const VMKernelAddressRange* other)
		:
		base(base),
		size(size),
		type(other->type)
	{
		if (type == RANGE_RESERVED) {
			reserved.base = other->reserved.base;
			reserved.flags = other->reserved.flags;
		}
	}
};


struct VMKernelAddressRangeTreeDefinition {
	typedef addr_t					Key;
	typedef VMKernelAddressRange	Value;

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
		addr_t b = _b->base;
		if (a == b)
			return 0;
		return a < b ? -1 : 1;
	}

	int Compare(const Value* a, const Value* b) const
	{
		return Compare(a->base, b);
	}
};

typedef AVLTree<VMKernelAddressRangeTreeDefinition> VMKernelAddressRangeTree;


struct VMKernelAddressRangeGetFreeListLink {
	typedef DoublyLinkedListLink<VMKernelAddressRange> Link;

	inline Link* operator()(VMKernelAddressRange* range) const
	{
		return &range->freeListLink;
	}

	inline const Link* operator()(const VMKernelAddressRange* range) const
	{
		return &range->freeListLink;
	}
};


/** @brief VMArea subclass used in the kernel address space.
 *
 * Each kernel area carries a back-pointer to the VMKernelAddressRange that
 * tracks its position in the address space, so deletion can update both the
 * area tree and the range tree without an extra lookup. */
struct VMKernelArea final : VMArea, AVLTreeNode {
								VMKernelArea(VMAddressSpace* addressSpace,
									uint32 wiring, uint32 protection);
								~VMKernelArea();

	/** @brief Allocates a kernel area through the given object cache.
	 *  @param objectCache Object cache used to obtain the descriptor. */
	static	VMKernelArea*		Create(VMAddressSpace* addressSpace,
									const char* name, uint32 wiring,
									uint32 protection, ObjectCache* objectCache,
									uint32 allocationFlags);

	/** @brief Returns the address range descriptor associated with this area. */
			VMKernelAddressRange* Range() const
									{ return fRange; }
	/** @brief Sets the address range descriptor associated with this area. */
			void				SetRange(VMKernelAddressRange* range)
									{ fRange = range; }

private:
			VMKernelAddressRange* fRange;  /**< Range descriptor in the owning kernel address space. */
};


#endif	// VM_KERNEL_AREA_H
