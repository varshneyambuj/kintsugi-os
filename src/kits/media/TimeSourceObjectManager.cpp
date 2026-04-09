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
 *   Copyright 2002 Marcus Overhagen. All Rights Reserved.
 *   This file may be used under the terms of the MIT License.
 */

/** @file TimeSourceObjectManager.cpp
 *  @brief Per-team cache ensuring each time source node has exactly one
 *         TimeSourceObject proxy. */


/*!	This works like a cache for time source objects, to make sure
	each team only has one object representation for each time source.
*/


#include "TimeSourceObjectManager.h"

#include <stdio.h>

#include <Autolock.h>
#include <MediaRoster.h>

#include <MediaDebug.h>
#include <MediaMisc.h>

#include "TimeSourceObject.h"


namespace BPrivate {
namespace media {


TimeSourceObjectManager* gTimeSourceObjectManager;
	// initialized by BMediaRoster.


/** @brief Constructs the manager and initialises the underlying BLocker. */
TimeSourceObjectManager::TimeSourceObjectManager()
	:
	BLocker("time source object manager")
{
}


/** @brief Destructor; force-releases all cached TimeSourceObject instances. */
TimeSourceObjectManager::~TimeSourceObjectManager()
{
	CALLED();

	// force unloading all currently loaded time sources
	NodeMap::iterator iterator = fMap.begin();
	for (; iterator != fMap.end(); iterator++) {
		BTimeSource* timeSource = iterator->second;

		PRINT(1, "Forcing release of TimeSource id %ld...\n", timeSource->ID());
		int32 debugCount = 0;
		while (timeSource->Release() != NULL)
			debugCount++;

		PRINT(1, "Forcing release of TimeSource done, released %d times\n",
			debugCount);
	}
}


/** @brief Returns a (possibly cached) TimeSourceObject for the given node.
 *
 *  BMediaRoster::MakeTimeSourceFor() uses this function to request a time source
 *  object. If one already exists in the cache it is Acquire()'d and returned;
 *  otherwise a new TimeSourceObject is created, inserted into the cache, and
 *  returned.
 *
 *  @param node  The media_node describing the time source.
 *  @return A pointer to the BTimeSource proxy, or \c NULL on allocation failure. */
BTimeSource*
TimeSourceObjectManager::GetTimeSource(const media_node& node)
{
	CALLED();
	BAutolock _(this);

	PRINT(1, "TimeSourceObjectManager::GetTimeSource, node id %ld\n",
		node.node);

	NodeMap::iterator found = fMap.find(node.node);
	if (found != fMap.end())
		return dynamic_cast<BTimeSource*>(found->second->Acquire());

	// time sources are not accounted in node reference counting
	BTimeSource* timeSource = new(std::nothrow) TimeSourceObject(node);
	if (timeSource == NULL)
		return NULL;

	fMap.insert(std::make_pair(node.node, timeSource));
	return timeSource;
}


/** @brief Removes the given TimeSourceObject from the cache and releases the
 *         corresponding media node reference.
 *
 *  This function is called during deletion of the time source object.
 *
 *  @param timeSource  The TimeSourceObject being deleted. */
void
TimeSourceObjectManager::ObjectDeleted(BTimeSource* timeSource)
{
	CALLED();
	BAutolock _(this);

	PRINT(1, "TimeSourceObjectManager::ObjectDeleted, node id %ld\n",
		timeSource->ID());

	fMap.erase(timeSource->ID());

	status_t status = BMediaRoster::Roster()->ReleaseNode(timeSource->Node());
	if (status != B_OK) {
		ERROR("TimeSourceObjectManager::ObjectDeleted, ReleaseNode failed\n");
	}
}


}	// namespace media
}	// namespace BPrivate
