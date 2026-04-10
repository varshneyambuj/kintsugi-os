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

/** @file AbstractEmptyDirectoryJob.h
 *  @brief Common base for boot jobs that need to create-and-empty a directory at startup. */

#ifndef ABSTRACT_EMPTY_DIRECTORY_JOB_H
#define ABSTRACT_EMPTY_DIRECTORY_JOB_H


#include <Job.h>


class BEntry;


/** @brief BJob base for "create and clear directory" startup jobs.
 *
 * Subclasses pick a target path and call CreateAndEmpty() from Execute();
 * the helper takes care of recursively removing previous contents and
 * recreating the directory if needed. */
class AbstractEmptyDirectoryJob : public BSupportKit::BJob {
public:
								AbstractEmptyDirectoryJob(const BString& name);

protected:
	/** @brief Creates @p path if missing and removes everything inside it. */
			status_t			CreateAndEmpty(const char* path) const;

private:
			status_t			_EmptyDirectory(BEntry& directoryEntry,
									bool remove) const;
};


#endif // ABSTRACT_EMPTY_DIRECTORY_JOB_H
