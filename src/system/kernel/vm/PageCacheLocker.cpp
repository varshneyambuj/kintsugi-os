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

/**
 * @file PageCacheLocker.cpp
 * @brief Helper that locks a page's VMCache, handling the case where the cache
 *        may be replaced during the lock attempt.
 *
 * @see VMCache
 */

#include "PageCacheLocker.h"

#include <vm/VMCache.h>


/**
 * @brief Determine whether a page should be skipped during cache-locking.
 *
 * A page is ignored when it is in a transient or unmanaged state that makes
 * locking its cache pointless or unsafe (busy, wired, free, clear, unused, or
 * has outstanding wire references).
 *
 * @param page  The page to evaluate.
 * @return      @c true if the page should be skipped; @c false if it is safe
 *              to proceed with acquiring the cache lock.
 */
bool
PageCacheLocker::_IgnorePage(vm_page* page)
{
	if (page->busy || page->State() == PAGE_STATE_WIRED
		|| page->State() == PAGE_STATE_FREE || page->State() == PAGE_STATE_CLEAR
		|| page->State() == PAGE_STATE_UNUSED || page->WiredCount() > 0)
		return true;

	return false;
}


/**
 * @brief Acquire a reference to and lock the VMCache that owns @p page.
 *
 * The function first screens the page through _IgnorePage(). It then calls
 * vm_cache_acquire_locked_page_cache() to atomically grab a reference and
 * the cache lock. After the lock is held the page is screened a second time
 * because its state may have changed while the lock was being acquired; if it
 * has become ignorable the cache reference is released and the function
 * returns @c false.
 *
 * On success the locker takes ownership: the reference and lock are held until
 * Unlock() is called.
 *
 * @param page      The page whose backing cache should be locked.
 * @param dontWait  When @c true the call returns immediately rather than
 *                  blocking if the cache lock is contended.
 * @retval true   The cache was successfully locked and @p page is managed by
 *                this locker instance.
 * @retval false  The page was ignorable, the cache could not be acquired, or
 *                the page became ignorable after the lock was taken.
 */
bool
PageCacheLocker::Lock(vm_page* page, bool dontWait)
{
	if (_IgnorePage(page))
		return false;

	// Grab a reference to this cache.
	VMCache* cache = vm_cache_acquire_locked_page_cache(page, dontWait);
	if (cache == NULL)
		return false;

	if (_IgnorePage(page)) {
		cache->ReleaseRefAndUnlock();
		return false;
	}

	fPage = page;
	return true;
}


/**
 * @brief Release the VMCache lock and reference held by this locker.
 *
 * If no page is currently managed (i.e. Lock() was never called or Unlock()
 * has already been called) the function is a no-op.
 */
void
PageCacheLocker::Unlock()
{
	if (fPage == NULL)
		return;

	fPage->Cache()->ReleaseRefAndUnlock();

	fPage = NULL;
}
