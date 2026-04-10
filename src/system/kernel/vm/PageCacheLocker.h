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
 *   Copyright 2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file PageCacheLocker.h
 *  @brief RAII helper that locks the VMCache holding a given vm_page. */

#ifndef PAGE_CACHE_LOCKER_H
#define PAGE_CACHE_LOCKER_H


#include <null.h>

struct vm_page;


/** @brief Scoped locker for the VMCache that owns a vm_page.
 *
 * Acquires the cache lock for a page in its constructor (or via Lock()) and
 * releases it in the destructor (or via Unlock()). The locker also filters
 * out pages that should be ignored — e.g. busy or cacheless pages — so callers
 * can simply check IsLocked() to know whether they hold a usable lock. */
class PageCacheLocker {
public:
	/** @brief Locks the cache that owns @p page.
	 *  @param page     The page whose cache should be locked.
	 *  @param dontWait If true, fail immediately when the lock would block. */
	inline						PageCacheLocker(vm_page* page,
									bool dontWait = true);
	/** @brief Releases the lock if one is currently held. */
	inline						~PageCacheLocker();

	/** @brief Returns true if a cache lock is currently held. */
			bool				IsLocked() { return fPage != NULL; }

	/** @brief Acquires the cache lock for @p page.
	 *  @param page     The page whose cache should be locked.
	 *  @param dontWait If true, fail immediately when the lock would block.
	 *  @return True if the lock was acquired, false otherwise. */
			bool				Lock(vm_page* page, bool dontWait = true);
	/** @brief Releases the cache lock if one is held. */
			void				Unlock();

private:
	/** @brief Returns true if @p page has no usable cache to lock. */
			bool				_IgnorePage(vm_page* page);

			vm_page*			fPage;  /**< Page whose cache lock is currently held, or NULL. */
};


PageCacheLocker::PageCacheLocker(vm_page* page, bool dontWait)
	:
	fPage(NULL)
{
	Lock(page, dontWait);
}


PageCacheLocker::~PageCacheLocker()
{
	Unlock();
}


#endif	// PAGE_CACHE_LOCKER_H
