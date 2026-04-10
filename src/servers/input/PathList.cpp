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

/** @file PathList.cpp
 *  @brief Implementation of the owning filesystem-path list used by the add-on manager. */


#include "PathList.h"

#include <new>
#include <stdlib.h>
#include <string.h>


/** @brief Internal path entry with reference counting. */
struct PathList::path_entry {
	/** @brief Constructs a path entry by duplicating the given path string.
	 *  @param _path Filesystem path to store (will be strdup'd). */
	path_entry(const char* _path)
		:
		ref_count(1)
	{
		path = strdup(_path);
	}

	/** @brief Destructor; frees the duplicated path string. */
	~path_entry()
	{
		free((char*)path);
	}

	const char* path;   /**< @brief The duplicated filesystem path string. */
	int32 ref_count;    /**< @brief Number of active references to this path. */
};


/**
 * @brief Constructs an empty path list with an initial capacity of 10 entries.
 */
PathList::PathList()
	:
	fPaths(10)
{
}


/** @brief Destructor. */
PathList::~PathList()
{
}


/**
 * @brief Checks whether the given path is already in the list.
 *
 * Performs a reverse linear search comparing path strings.
 *
 * @param path   The path to search for.
 * @param _index Optional output; set to the index of the found entry.
 * @return @c true if the path exists in the list, @c false otherwise.
 */
bool
PathList::HasPath(const char* path, int32* _index) const
{
	for (int32 i = fPaths.CountItems(); i-- > 0;) {
		if (!strcmp(fPaths.ItemAt(i)->path, path)) {
			if (_index != NULL)
				*_index = i;
			return true;
		}
	}

	return false;
}


/**
 * @brief Adds a path to the list, or increments its reference count if it already exists.
 *
 * @param path The filesystem path to add; must not be NULL.
 * @return B_OK on success, B_BAD_VALUE if @a path is NULL, or B_NO_MEMORY on
 *         allocation failure.
 */
status_t
PathList::AddPath(const char* path)
{
	if (path == NULL)
		return B_BAD_VALUE;

	int32 index;
	if (HasPath(path, &index)) {
		fPaths.ItemAt(index)->ref_count++;
		return B_OK;
	}

	path_entry* entry = new(std::nothrow) path_entry(path);
	if (entry == NULL || entry->path == NULL || !fPaths.AddItem(entry)) {
		delete entry;
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Decrements the reference count for the given path, removing it when it reaches zero.
 *
 * @param path The filesystem path to remove.
 * @return B_OK on success, or B_ENTRY_NOT_FOUND if the path is not in the list.
 */
status_t
PathList::RemovePath(const char* path)
{
	int32 index;
	if (!HasPath(path, &index))
		return B_ENTRY_NOT_FOUND;

	if (--fPaths.ItemAt(index)->ref_count == 0)
		delete fPaths.RemoveItemAt(index);

	return B_OK;
}


/**
 * @brief Returns the number of unique paths currently in the list.
 *
 * @return The path count.
 */
int32
PathList::CountPaths() const
{
	return fPaths.CountItems();
}


/**
 * @brief Returns the path string at the given index.
 *
 * @param index Zero-based index into the path list.
 * @return The path string, or NULL if @a index is out of range.
 */
const char*
PathList::PathAt(int32 index) const
{
	path_entry* entry = fPaths.ItemAt(index);
	if (entry == NULL)
		return NULL;

	return entry->path;
}
