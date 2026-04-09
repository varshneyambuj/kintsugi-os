/*
 * Copyright 2025, Kintsugi OS Contributors.
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright (c) 2006, Haiku, Inc.
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file RegionPool.h
 *  @brief Pool allocator for BRegion objects to reduce allocation overhead. */

#ifndef REGION_POOL_H
#define REGION_POOL_H

#include <List.h>

class BRegion;

#define DEBUG_LEAK 0

/** @brief Maintains a free-list of BRegion objects for efficient reuse. */
class RegionPool {
 public:
								RegionPool();
	virtual						~RegionPool();

	/** @brief Retrieves a blank BRegion from the pool (allocates if needed).
	 *  @return Pointer to an available BRegion. */
			BRegion*			GetRegion();

	/** @brief Retrieves a BRegion from the pool initialised as a copy of @a other.
	 *  @param other Source region to copy.
	 *  @return Pointer to the copied BRegion. */
			BRegion*			GetRegion(const BRegion& other);

	/** @brief Returns a previously obtained BRegion to the pool for reuse.
	 *  @param region The region to recycle; must have been obtained from this pool. */
			void				Recycle(BRegion* region);

 private:
			BList				fAvailable;
#if DEBUG_LEAK
			BList				fUsed;
#endif
};

#endif // REGION_POOL_H
