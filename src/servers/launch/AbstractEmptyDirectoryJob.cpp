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


AbstractEmptyDirectoryJob::AbstractEmptyDirectoryJob(const BString& name)
	:
	BJob(name)
{
}


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
