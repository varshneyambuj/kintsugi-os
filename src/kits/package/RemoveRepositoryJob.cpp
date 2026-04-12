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
 * @file RemoveRepositoryJob.cpp
 * @brief Implementation of RemoveRepositoryJob, which deletes a repository's config and cache.
 *
 * RemoveRepositoryJob is an internal job that, after user confirmation, removes
 * both the repository configuration file and any associated repository cache file
 * for the named repository. It is queued by the higher-level repository-removal
 * logic in the package management subsystem.
 *
 * @see BPackageRoster, BRepositoryConfig, BRepositoryCache
 */


#include "RemoveRepositoryJob.h"

#include <Entry.h>

#include <package/Context.h>
#include <package/PackageRoster.h>
#include <package/RepositoryCache.h>
#include <package/RepositoryConfig.h>


namespace BPackageKit {

namespace BPrivate {


/**
 * @brief Construct a RemoveRepositoryJob.
 *
 * @param context         The shared BContext providing decision and progress services.
 * @param title           Human-readable title for progress reporting.
 * @param repositoryName  Name of the repository to remove.
 */
RemoveRepositoryJob::RemoveRepositoryJob(const BContext& context,
	const BString& title, const BString& repositoryName)
	:
	inherited(context, title),
	fRepositoryName(repositoryName)
{
}


/**
 * @brief Destroy the job.
 */
RemoveRepositoryJob::~RemoveRepositoryJob()
{
}


/**
 * @brief Execute the repository removal by deleting config and cache files.
 *
 * Looks up the repository by name, asks the user for confirmation, then
 * removes the configuration file. If a cache file also exists it is removed.
 *
 * @return B_OK on success, B_CANCELED if the user declined, B_ENTRY_NOT_FOUND
 *         if the repository does not exist, or another error code on failure.
 */
status_t
RemoveRepositoryJob::Execute()
{
	BPackageRoster roster;
	BRepositoryConfig repoConfig;
	status_t result = roster.GetRepositoryConfig(fRepositoryName, &repoConfig);
	if (result != B_OK) {
		if (result == B_ENTRY_NOT_FOUND) {
			BString error = BString("repository '") << fRepositoryName
				<< "' not found!";
			SetErrorString(error);
		}
		return result;
	}

	BString question = BString("Really remove the repository '")
		<< fRepositoryName << "'?";
	bool yes = fContext.DecisionProvider().YesNoDecisionNeeded("", question,
		"yes", "no", "no");
	if (!yes)
		return B_CANCELED;

	BEntry repoConfigEntry = repoConfig.Entry();
	if ((result = repoConfigEntry.Remove()) != B_OK)
		return result;

	BRepositoryCache repoCache;
	if (roster.GetRepositoryCache(fRepositoryName, &repoCache) == B_OK) {
		BEntry repoCacheEntry = repoCache.Entry();
		if ((result = repoCacheEntry.Remove()) != B_OK)
			return result;
	}

	return B_OK;
}


}	// namespace BPrivate

}	// namespace BPackageKit
