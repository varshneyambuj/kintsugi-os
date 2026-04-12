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
 *   Copyright 2011, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe <zooey@hirschkaefer.de>
 */


/**
 * @file ActivateRepositoryCacheJob.cpp
 * @brief Job that moves a fetched repository cache file into its final location.
 *
 * ActivateRepositoryCacheJob is a single-step job in the package-kit job queue.
 * It takes a freshly downloaded repository cache file (a temporary BEntry) and
 * moves it into the target directory under the canonical repository name,
 * overwriting any pre-existing file atomically.
 *
 * @see ActivateRepositoryConfigJob, FetchFileJob
 */


#include "ActivateRepositoryCacheJob.h"

#include <File.h>

#include <package/Context.h>


namespace BPackageKit {

namespace BPrivate {


/**
 * @brief Construct the job with the fetched cache entry and its destination.
 *
 * @param context                Reference to the package-kit context providing
 *                               temporary-file management and decision support.
 * @param title                  Human-readable title displayed in progress UI.
 * @param fetchedRepoCacheEntry  The temporary BEntry pointing to the downloaded
 *                               repository cache file to be activated.
 * @param repositoryName         Name used as the destination file name inside
 *                               \a targetDirectory.
 * @param targetDirectory        Directory into which the cache file is moved.
 */
ActivateRepositoryCacheJob::ActivateRepositoryCacheJob(const BContext& context,
	const BString& title, const BEntry& fetchedRepoCacheEntry,
	const BString& repositoryName, const BDirectory& targetDirectory)
	:
	inherited(context, title),
	fFetchedRepoCacheEntry(fetchedRepoCacheEntry),
	fRepositoryName(repositoryName),
	fTargetDirectory(targetDirectory)
{
}


/**
 * @brief Destructor.
 */
ActivateRepositoryCacheJob::~ActivateRepositoryCacheJob()
{
}


/**
 * @brief Move the fetched cache file into the target directory.
 *
 * Performs the actual activation by calling BEntry::MoveTo() to relocate
 * the downloaded temporary file into the configured target directory under
 * the repository name.  Existing files at the destination are replaced.
 *
 * @return B_OK on success, or a system error code if the move fails.
 */
status_t
ActivateRepositoryCacheJob::Execute()
{
	status_t result = fFetchedRepoCacheEntry.MoveTo(&fTargetDirectory,
		fRepositoryName.String(), true);
	if (result != B_OK)
		return result;

	// TODO: propagate some repository attributes to file attributes

	return B_OK;
}


}	// namespace BPrivate

}	// namespace BPackageKit
