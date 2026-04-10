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

/** @file VMNullCache.h
 *  @brief Empty VMCache used as a placeholder in cache chains. */

#ifndef _KERNEL_VM_STORE_NULL_H
#define _KERNEL_VM_STORE_NULL_H


#include <vm/VMCache.h>


/** @brief VMCache that holds no pages and reads back as zeros.
 *
 * Used as a terminator in cache chains where the upper layer needs a real
 * cache object to point at but where there is no actual backing store. Any
 * page fault that reaches this cache simply yields a fresh zero-filled page. */
class VMNullCache final : public VMCache {
public:
	/** @brief Initialises the empty cache.
	 *  @param allocationFlags Allocation flags forwarded to the base class.
	 *  @return B_OK on success, or an error code on failure. */
			status_t			Init(uint32 allocationFlags);

protected:
	/** @brief Releases per-instance resources before the object is freed. */
	virtual	void				DeleteObject();
};


#endif	/* _KERNEL_VM_STORE_NULL_H */
