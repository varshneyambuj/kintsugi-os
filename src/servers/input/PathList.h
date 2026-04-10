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
 *   Copyright 2008 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file PathList.h
 *  @brief Owning list of filesystem paths used by the input add-on manager. */

#ifndef PATH_LIST_H
#define PATH_LIST_H


#include <ObjectList.h>


/** @brief Insertion-ordered owning list of filesystem path strings.
 *
 * Used by the input add-on manager to remember the directories it is
 * monitoring for input filter, device, and method add-ons. */
class PathList {
public:
							PathList();
							~PathList();

	/** @brief Returns true if @p path is already in the list.
	 *  @param _index Optional out parameter receiving the matching index. */
			bool			HasPath(const char* path,
								int32* _index = NULL) const;
	/** @brief Inserts a copy of @p path. */
			status_t		AddPath(const char* path);
	/** @brief Removes the entry matching @p path. */
			status_t		RemovePath(const char* path);

	/** @brief Returns the number of paths in the list. */
			int32			CountPaths() const;
	/** @brief Returns the path stored at @p index. */
			const char*		PathAt(int32 index) const;
private:
	struct path_entry;

			BObjectList<path_entry, true> fPaths;  /**< Owned path-entry storage. */
};

#endif	// _DEVICE_MANAGER_H
