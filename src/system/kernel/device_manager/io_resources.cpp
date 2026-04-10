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
 *   Copyright 2004-2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Copyright 2002-2004, Thomas Kurschel. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file io_resources.cpp
 * @brief I/O resource management — allocation and freeing of ISA I/O port, memory, and DMA resources.
 */


#include "io_resources.h"

#include <stdlib.h>
#include <string.h>


//#define TRACE_IO_RESOURCES
#ifdef TRACE_IO_RESOURCES
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


typedef DoublyLinkedList<io_resource_private,
	DoublyLinkedListMemberGetLink<io_resource_private,
		&io_resource_private::fTypeLink> > ResourceTypeList;


static ResourceTypeList sMemoryList;
static ResourceTypeList sPortList;
static ResourceTypeList sDMAChannelList;


/**
 * @brief Default constructor — zero-initialises all resource fields.
 */
io_resource_private::io_resource_private()
{
	_Init();
}


/**
 * @brief Destructor — releases the resource if it was previously acquired.
 */
io_resource_private::~io_resource_private()
{
	Release();
}


/**
 * @brief Resets all resource descriptor fields to zero.
 *
 * Sets type, base, and length to 0, marking this object as holding no
 * resource. Called both at construction and after Release().
 */
void
io_resource_private::_Init()
{
	type = 0;
	base = 0;
	length = 0;
}


/**
 * @brief Claims ownership of an I/O resource range, failing if it overlaps
 *        an existing allocation.
 *
 * Validates the supplied @p resource descriptor, copies its fields into this
 * object, selects the appropriate type-keyed list (memory, port, or DMA
 * channel), checks for range overlap with all existing allocations, and — if
 * no conflict is found — inserts this object into the list.
 *
 * @param resource  The resource descriptor describing the type, base address,
 *                  and length to acquire.
 * @retval B_OK                   Resource was acquired successfully.
 * @retval B_BAD_VALUE            @p resource failed the validity check.
 * @retval B_RESOURCE_UNAVAILABLE The requested range overlaps an existing
 *                                allocation.
 */
status_t
io_resource_private::Acquire(const io_resource& resource)
{
	if (!_IsValid(resource))
		return B_BAD_VALUE;

	type = resource.type;
	base = resource.base;

	if (type != B_ISA_DMA_CHANNEL)
		length = resource.length;
	else
		length = 1;

	ResourceTypeList* list = NULL;

	switch (type) {
		case B_IO_MEMORY:
			list = &sMemoryList;
			break;
		case B_IO_PORT:
			list = &sPortList;
			break;
		case B_ISA_DMA_CHANNEL:
			list = &sDMAChannelList;
			break;
	}

	ResourceTypeList::Iterator iterator = list->GetIterator();
	while (iterator.HasNext()) {
		io_resource* resource = iterator.Next();

		// we need the "base + length - 1" trick to avoid wrap around at 4 GB
		if (resource->base >= base
			&& resource->base + length - 1 <= base + length - 1) {
			// This range is already covered by someone else
			// TODO: we might want to ignore resources that belong to
			// a node that isn't used.
			_Init();
			return B_RESOURCE_UNAVAILABLE;
		}
	}

	list->Add(this);
	return B_OK;
}


/**
 * @brief Removes this resource from its type list and resets all fields.
 *
 * If this object currently holds no resource (type == 0) the call is a
 * no-op. Otherwise the object is unlinked from the appropriate global list
 * and _Init() is called to zero the descriptor fields.
 */
void
io_resource_private::Release()
{
	if (type == 0)
		return;

	switch (type) {
		case B_IO_MEMORY:
			sMemoryList.Remove(this);
			break;
		case B_IO_PORT:
			sPortList.Remove(this);
			break;
		case B_ISA_DMA_CHANNEL:
			sDMAChannelList.Remove(this);
			break;
	}

	_Init();
}


/**
 * @brief Checks whether an io_resource descriptor contains coherent values.
 *
 * Applies type-specific range and alignment constraints:
 * - @c B_IO_MEMORY: base + length must not overflow.
 * - @c B_IO_PORT: base and length must each fit in a @c uint16 and the
 *   range must not overflow.
 * - @c B_ISA_DMA_CHANNEL: base (channel number) must be <= 8.
 *
 * @param resource  The resource descriptor to validate.
 * @return @c true if @p resource is well-formed; @c false otherwise.
 */
/*static*/ bool
io_resource_private::_IsValid(const io_resource& resource)
{
	switch (resource.type) {
		case B_IO_MEMORY:
			return resource.base + resource.length > resource.base;
		case B_IO_PORT:
			return (uint16)resource.base == resource.base
				&& (uint16)resource.length == resource.length
				&& resource.base + resource.length > resource.base;
		case B_ISA_DMA_CHANNEL:
			return resource.base <= 8;

		default:
			return false;
	}
}


//	#pragma mark -


/**
 * @brief Initialises the global I/O resource tracking lists.
 *
 * Placement-constructs sMemoryList, sPortList, and sDMAChannelList. Must be
 * called once during device-manager initialisation before any
 * io_resource_private objects are used.
 */
void
dm_init_io_resources(void)
{
	new(&sMemoryList) ResourceTypeList;
	new(&sPortList) ResourceTypeList;
	new(&sDMAChannelList) ResourceTypeList;
}
