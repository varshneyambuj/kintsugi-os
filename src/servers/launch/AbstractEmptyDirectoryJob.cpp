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
 *   Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file AbstractEmptyDirectoryJob.cpp
 *  @brief Implements the base class for jobs that create and empty directories. */


#include "AbstractEmptyDirectoryJob.h"

#include <stdio.h>

#include <Directory.h>
#include <Entry.h>


/**
 * @brief Constructs the abstract empty-directory job with the given name.
 *
 * @param name The human-readable job name used for logging and identification.
 */
AbstractEmptyDirectoryJob::AbstractEmptyDirectoryJob(const BString& name)
	:
	BJob(name)
{
}


/**
 * @brief Ensures a directory exists at @a path and removes all its contents.
 *
 * If the directory does not exist, it is created with mode 0777. Then all
 * entries within it (including subdirectories) are recursively removed.
 *
 * @param path Absolute filesystem path of the directory to create and empty.
 * @return B_OK on success, or an error code if creation or emptying fails.
 */
status_t
AbstractEmptyDirectoryJob::CreateAndEmpty(const char* path) const
{
	BEntry entry(path);
	if (!entry.Exists()) {
		create_directory(path, 0777);

		status_t status = entry.SetTo(path);
		if (status != B_OK) {
			fprintf(stderr, "Cannot create directory \"%s\": %s\n", path,
				strerror(status));
			return status;
		}
	}

	return _EmptyDirectory(entry, false);
}


/**
 * @brief Recursively empties a directory, optionally removing it afterwards.
 *
 * Iterates over all entries in @a directoryEntry. Subdirectories are emptied
 * recursively and then removed; plain files are removed immediately.
 *
 * @param directoryEntry The BEntry for the directory to empty.
 * @param remove         If @c true, remove the directory entry itself after emptying.
 * @return B_OK on success, or the error from BEntry::Remove() on failure.
 */
status_t
AbstractEmptyDirectoryJob::_EmptyDirectory(BEntry& directoryEntry,
	bool remove) const
{
	BDirectory directory(&directoryEntry);
	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		if (entry.IsDirectory())
			_EmptyDirectory(entry, true);
		else
			entry.Remove();
	}

	return remove ? directoryEntry.Remove() : B_OK;
}
