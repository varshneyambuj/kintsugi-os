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
 *   Copyright 2008-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2005-2007, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/** @file VMDeviceCache.h
 *  @brief VMCache that maps a fixed range of physical device memory. */

#ifndef _KERNEL_VM_STORE_DEVICE_H
#define _KERNEL_VM_STORE_DEVICE_H


#include <vm/VMCache.h>


/** @brief VMCache backed by a contiguous physical memory region.
 *
 * Used to map MMIO and framebuffer regions: every offset in the cache
 * corresponds to a fixed physical address @c fBaseAddress + offset rather
 * than to a managed page. The cache itself owns no memory and cannot be
 * paged out. */
class VMDeviceCache final : public VMCache {
public:
	/** @brief Initialises the cache to start at @p baseAddress in physical memory.
	 *  @param baseAddress      Physical base address of the device region.
	 *  @param allocationFlags  Allocation flags forwarded to the base class.
	 *  @return B_OK on success, or an error code on failure. */
			status_t			Init(addr_t baseAddress,
									uint32 allocationFlags);

	/** @brief Reads device memory directly into the supplied I/O vectors. */
	virtual	status_t			Read(off_t offset, const generic_io_vec *vecs,
									 size_t count, uint32 flags,
									 generic_size_t *_numBytes);
	/** @brief Writes device memory directly from the supplied I/O vectors. */
	virtual	status_t			Write(off_t offset, const generic_io_vec *vecs,
									  size_t count, uint32 flags,
									  generic_size_t *_numBytes);

protected:
	/** @brief Releases per-instance resources before the object is freed. */
	virtual	void				DeleteObject();

private:
			addr_t				fBaseAddress;  /**< Physical base address of the mapped device region. */
};


#endif	/* _KERNEL_VM_STORE_DEVICE_H */
