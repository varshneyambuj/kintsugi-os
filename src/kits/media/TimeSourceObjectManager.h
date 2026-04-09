/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work originally licensed under the MIT License.
 * Copyright 2002, Marcus Overhagen. All Rights Reserved.
 */

/** @file TimeSourceObjectManager.h
    @brief Manages the lifecycle of TimeSourceObject proxies keyed by media_node_id. */

#ifndef TIME_SOURCE_OBJECT_MANAGER_H
#define TIME_SOURCE_OBJECT_MANAGER_H


#include <map>

#include <Locker.h>
#include <MediaDefs.h>


class BTimeSource;


namespace BPrivate {
namespace media {


/** @brief Thread-safe registry that maps media_node_id values to their
           corresponding TimeSourceObject proxy instances. */
class TimeSourceObjectManager : BLocker {
public:
								TimeSourceObjectManager();
								~TimeSourceObjectManager();

			/** @brief Returns the TimeSourceObject proxy for the given node, creating one if needed.
			    @param node The media_node whose time source proxy is requested.
			    @return Pointer to the BTimeSource proxy, or NULL on failure. */
			BTimeSource*		GetTimeSource(const media_node& node);

			/** @brief Removes a TimeSourceObject from the registry when it is deleted.
			    @param timeSource The BTimeSource being destroyed. */
			void				ObjectDeleted(BTimeSource* timeSource);

private:
			typedef std::map<media_node_id, BTimeSource*> NodeMap;

			NodeMap				fMap;
};


/** @brief Global TimeSourceObjectManager instance. */
extern TimeSourceObjectManager* gTimeSourceObjectManager;


}	// namespace media
}	// namespace BPrivate


using BPrivate::media::gTimeSourceObjectManager;


#endif	// _TIME_SOURCE_OBJECT_MANAGER_H_
