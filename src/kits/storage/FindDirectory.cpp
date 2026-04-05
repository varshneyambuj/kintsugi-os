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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file FindDirectory.cpp
 * @brief C++ wrapper for find_directory() using BPath.
 *
 * Provides a convenient overload of find_directory() that fills a BPath
 * object rather than a raw character buffer, optionally scoped to a
 * specific BVolume.
 *
 * @see find_directory
 */

#include <FindDirectory.h>
#include <Path.h>
#include <Volume.h>


/**
 * @brief Returns the path of a well-known directory as a BPath.
 *
 * Queries the kernel for the directory identified by @p which on the
 * volume identified by @p volume (or the boot volume if @p volume is NULL
 * or uninitialised) and initialises @p path with the result.
 *
 * @param which    A directory_which constant identifying the desired
 *                 well-known directory (e.g. B_USER_SETTINGS_DIRECTORY).
 * @param path     Output BPath to initialise with the directory path.
 *                 Must not be NULL.
 * @param createIt If \c true, the directory is created if it does not
 *                 already exist.
 * @param volume   Optional BVolume on which to locate the directory.
 *                 Pass NULL to use the boot volume.
 * @return B_OK on success, B_BAD_VALUE if @p path is NULL, or another
 *         error code on failure.
 */
status_t
find_directory(directory_which which, BPath* path, bool createIt,
			   BVolume* volume)
{
	if (path == NULL)
		return B_BAD_VALUE;

	dev_t device = (dev_t)-1;
	if (volume && volume->InitCheck() == B_OK)
		device = volume->Device();

	char buffer[B_PATH_NAME_LENGTH];
	status_t error = find_directory(which, device, createIt, buffer,
		B_PATH_NAME_LENGTH);
	if (error == B_OK)
		error = path->SetTo(buffer);

	return error;
}
