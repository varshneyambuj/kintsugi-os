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
 *   Copyright (c) 2006, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 */

/** @file RegionPool.cpp
 *  @brief Object pool for BRegion instances to reduce heap allocation overhead. */

#include "RegionPool.h"

#include <new>
#include <stdio.h>

#if DEBUG_LEAK
#include <debugger.h>
#endif

#include <Region.h>

using std::nothrow;

/**
 * @brief Constructs an empty RegionPool.
 */
RegionPool::RegionPool()
	: fAvailable(4)
#if DEBUG_LEAK
	  ,fUsed(4)
#endif
{
}


/**
 * @brief Destroys the RegionPool, deleting all pooled BRegion objects.
 *
 * In DEBUG_LEAK mode, triggers a debugger if any regions are still checked out.
 */
RegionPool::~RegionPool()
{
#if DEBUG_LEAK
	if (fUsed.CountItems() > 0)
		debugger("RegionPool::~RegionPool() - some regions still in use!");
#endif
	int32 count = fAvailable.CountItems();
	for (int32 i = 0; i < count; i++)
		delete (BRegion*)fAvailable.ItemAtFast(i);
}


/**
 * @brief Returns an empty BRegion from the pool, creating one if necessary.
 *
 * Prints to stderr and returns NULL if allocation fails.
 *
 * @return A pointer to an empty BRegion, or NULL on allocation failure.
 */
BRegion*
RegionPool::GetRegion()
{
	BRegion* region = (BRegion*)fAvailable.RemoveItem(
		fAvailable.CountItems() - 1);
	if (!region) {
		region = new (nothrow) BRegion();
		if (!region) {
			// whoa
			fprintf(stderr, "RegionPool::GetRegion() - "
							"no memory!\n");
		}
	}
#if DEBUG_LEAK
	fUsed.AddItem(region);
#endif
	return region;
}


/**
 * @brief Returns a BRegion initialized as a copy of @a other from the pool.
 *
 * Prints to stderr and returns NULL if allocation fails.
 *
 * @param other The source BRegion to copy.
 * @return A BRegion equal to @a other, or NULL on allocation failure.
 */
BRegion*
RegionPool::GetRegion(const BRegion& other)
{
	BRegion* region;
	int32 count = fAvailable.CountItems();
	if (count > 0) {
		region = (BRegion*)fAvailable.RemoveItem(count - 1);
		*region = other;
	} else {
		region = new (nothrow) BRegion(other);
		if (!region) {
			// whoa
			fprintf(stderr, "RegionPool::GetRegion() - "
							"no memory!\n");
		}
	}

#if DEBUG_LEAK
	fUsed.AddItem(region);
#endif
	return region;
}


/**
 * @brief Returns a BRegion to the pool for future reuse.
 *
 * The region is cleared (MakeEmpty) before being returned to the pool. If
 * the pool cannot hold the region it is deleted to prevent a memory leak.
 *
 * @param region The BRegion to recycle (must have been obtained from this pool).
 */
void
RegionPool::Recycle(BRegion* region)
{
	if (!fAvailable.AddItem(region)) {
		// at least don't leak the region...
		fprintf(stderr, "RegionPool::Recycle() - "
						"no memory!\n");
		delete region;
	} else {
		// prepare for next usage
		region->MakeEmpty();
	}
#if DEBUG_LEAK
	fUsed.RemoveItem(region);
#endif
}
