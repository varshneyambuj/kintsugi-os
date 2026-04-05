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
 *   Copyright 2006-2012 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file ServerMemoryAllocator.cpp
 *  @brief BPrivate::ServerMemoryAllocator implementation for shared memory management.
 *
 *  Manages cloned memory areas shared between the application and the app_server.
 *  Areas are reference-counted so that multiple clients can share the same
 *  local clone. This class provides no internal locking; callers must hold
 *  an AppServerLink (which provides the necessary synchronization).
 */

/*!	Note, this class don't provide any locking whatsoever - you are
	supposed to have a BPrivate::AppServerLink object around which
	does the necessary locking.
	However, this is not enforced in the methods here, you have to
	take care for yourself!
*/


#include "ServerMemoryAllocator.h"

#include <new>

#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#	include <syscalls.h>
#endif


/** @brief Total virtual address space to reserve for cloned areas (128 MB). */
static const size_t kReservedSize = 128 * 1024 * 1024;

/** @brief Maximum area size eligible for address space reservation (32 MB). */
static const size_t kReserveMaxSize = 32 * 1024 * 1024;


namespace BPrivate {


/** @brief Default constructor. */
ServerMemoryAllocator::ServerMemoryAllocator()
{
}


/** @brief Destructor. Deletes all locally cloned areas. */
ServerMemoryAllocator::~ServerMemoryAllocator()
{
	while (!fAreas.empty()) {
		std::map<area_id, area_mapping>::iterator it = fAreas.begin();
		area_mapping& mapping = it->second;
		delete_area(mapping.local_area);
		fAreas.erase(it);
	}
}


/** @brief Checks initialization status.
 *  @return Always returns B_OK.
 */
status_t
ServerMemoryAllocator::InitCheck()
{
	return B_OK;
}


/** @brief Clones a server-side memory area into this process's address space.
 *
 *  If the area has already been cloned, the existing clone's reference count
 *  is incremented. Otherwise, a new clone is created. For writable areas
 *  smaller than kReserveMaxSize, virtual address space is pre-reserved to
 *  allow future area resizing without relocation.
 *
 *  @param serverArea The server-side area ID to clone.
 *  @param _area Output: receives the local clone's area ID.
 *  @param _base Output: receives a pointer to the clone's base address.
 *  @param size The size of the area to clone.
 *  @param readOnly If true, the clone is created as read-only.
 *  @return B_OK on success, B_NO_MEMORY on allocation failure, or another
 *          error code if cloning fails.
 */
status_t
ServerMemoryAllocator::AddArea(area_id serverArea, area_id& _area,
	uint8*& _base, size_t size, bool readOnly)
{
	std::map<area_id, area_mapping>::iterator it = fAreas.find(serverArea);
	if (it != fAreas.end()) {
		area_mapping& mapping = it->second;
		mapping.reference_count++;

		_area = mapping.local_area;
		_base = mapping.local_base;

		return B_OK;
	}

	area_mapping* mapping;
	try {
		mapping = &fAreas[serverArea];
	} catch (const std::bad_alloc&) {
		return B_NO_MEMORY;
	}
	mapping->reference_count = 1;

	status_t status = B_ERROR;
	uint32 addressSpec = B_ANY_ADDRESS;
	void* base;
#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
	if (!readOnly && size < kReserveMaxSize) {
		// Reserve 128 MB of space for the area, but only if the area
		// is smaller than 32 MB (else the address space waste would
		// likely to be too large)
		base = (void*)0x60000000;
		status = _kern_reserve_address_range((addr_t*)&base, B_BASE_ADDRESS,
			kReservedSize);
		addressSpec = status == B_OK ? B_EXACT_ADDRESS : B_BASE_ADDRESS;
	}
#endif

	mapping->local_area = clone_area(readOnly
			? "server read-only memory" : "server_memory", &base, addressSpec,
		B_CLONEABLE_AREA | B_READ_AREA | (readOnly ? 0 : B_WRITE_AREA),
		serverArea);
	if (mapping->local_area < B_OK) {
		status = mapping->local_area;

		fAreas.erase(serverArea);

		return status;
	}

	mapping->server_area = serverArea;
	mapping->local_base = (uint8*)base;

	_area = mapping->local_area;
	_base = mapping->local_base;

	return B_OK;
}


/** @brief Removes a reference to a cloned area, deleting it when unreferenced.
 *
 *  Decrements the reference count for the specified server area. When the
 *  count reaches zero, the local clone is deleted and the mapping is removed.
 *
 *  @param serverArea The server-side area ID whose clone should be released.
 */
void
ServerMemoryAllocator::RemoveArea(area_id serverArea)
{
	std::map<area_id, area_mapping>::iterator it = fAreas.find(serverArea);
	if (it != fAreas.end()) {
		area_mapping& mapping = it->second;
		if (mapping.reference_count-- == 1) {
			delete_area(mapping.local_area);
			fAreas.erase(serverArea);
		}
	}
}


}	// namespace BPrivate
