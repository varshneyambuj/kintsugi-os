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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the Be Sample Code License.
 */


/**
 * @file SlowMenu.cpp
 * @brief Implementation of BSlowMenu, a lazily-built dynamic BMenu.
 *
 * BSlowMenu drives the incremental population of a BMenu by calling
 * AddNextItem() in small time-bounded chunks across successive
 * AddDynamicItem() invocations.  Subclasses override StartBuildingItemList(),
 * AddNextItem(), DoneBuildingItemList(), and ClearMenuBuildingState() to
 * supply the actual items.
 *
 * @see BMenu
 */


#include "SlowMenu.h"


const int32 kItemsToAddChunk = 20;
const bigtime_t kMaxTimeBuildingMenu = 200000;


//	#pragma mark - BSlowMenu


/**
 * @brief Construct a BSlowMenu with the given title and layout.
 *
 * @param title   Display title shown in the menu bar.
 * @param layout  BMenu layout constant (e.g. B_ITEMS_IN_COLUMN).
 */
BSlowMenu::BSlowMenu(const char* title, menu_layout layout)
	:
	BMenu(title, layout),
	fMenuBuilt(false)
{
}


/**
 * @brief BMenu override that incrementally adds items during menu tracking.
 *
 * Manages the lifecycle of the incremental build: it calls
 * StartBuildingItemList() on the first invocation, adds up to
 * kItemsToAddChunk items per call within kMaxTimeBuildingMenu microseconds,
 * and cleans up via DoneBuildingItemList() / ClearMenuBuildingState() when
 * all items are added or when the build is aborted.
 *
 * @param state  B_INITIAL_ADD on first call, B_NORMAL_ADD on subsequent
 *               calls, or B_ABORT if the menu should stop building.
 * @return true if more items remain to be added, false when done.
 */
bool
BSlowMenu::AddDynamicItem(add_state state)
{
	if (fMenuBuilt)
		return false;

	if (state == B_ABORT) {
		ClearMenuBuildingState();
		return false;
	}

	if (state == B_INITIAL_ADD && !StartBuildingItemList()) {
		ClearMenuBuildingState();
		return false;
	}

	bigtime_t timeToBail = system_time() + kMaxTimeBuildingMenu;
	for (int32 count = 0; count < kItemsToAddChunk; count++) {
		if (!AddNextItem()) {
			fMenuBuilt = true;
			DoneBuildingItemList();
			ClearMenuBuildingState();
			return false;
				// done with menu, don't call again
		}

		if (system_time() > timeToBail) {
			// we've been in here long enough, come back later
			break;
		}
	}

	return true;
		// call me again, got more to show
}


/**
 * @brief Hook called once before the first AddNextItem() invocation.
 *
 * Subclasses should perform any initialisation needed for the item build.
 *
 * @return true if the build should proceed, false to abort immediately.
 */
bool
BSlowMenu::StartBuildingItemList()
{
	return true;
}


/**
 * @brief Hook called repeatedly to append the next menu item.
 *
 * Subclasses must override this method to add one item to the menu.
 * The base implementation asserts (TRESPASS) because the method is
 * effectively pure-virtual.
 *
 * @return true if more items are available, false when the list is exhausted.
 */
bool
BSlowMenu::AddNextItem()
{
	TRESPASS();
		// pure virtual, shouldn't be here
	return true;
}


/**
 * @brief Hook called once after all items have been added.
 *
 * Subclasses override this to perform any post-build work such as
 * sorting or enabling items.  The base implementation asserts.
 */
void
BSlowMenu::DoneBuildingItemList()
{
	TRESPASS();
		// pure virtual, shouldn't be here
}


/**
 * @brief Hook called to release any state allocated during the build.
 *
 * Called on completion or abort; subclasses should free iterators or
 * other temporary resources here.  The base implementation asserts.
 */
void
BSlowMenu::ClearMenuBuildingState()
{
	TRESPASS();
		// pure virtual, shouldn't be here
}
