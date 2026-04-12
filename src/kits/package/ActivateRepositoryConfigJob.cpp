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
 *   Copyright 2011-2018, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe <zooey@hirschkaefer.de>
 *       Andrew Lindesay <apl@lindesay.co.nz>
 */


/**
 * @file ActivateRepositoryConfigJob.cpp
 * @brief Job that creates a repository configuration file from an archived repo info entry.
 *
 * ActivateRepositoryConfigJob reads a BRepositoryInfo from a temporary archive
 * file, optionally prompts the user when a configuration for the same repository
 * already exists, and writes a new BRepositoryConfig to the target directory,
 * injecting the base URL that was actually used during the fetch.
 *
 * @see ActivateRepositoryCacheJob, AddRepositoryRequest
 */


#include "ActivateRepositoryConfigJob.h"

#include <package/Context.h>
#include <package/RepositoryConfig.h>
#include <package/RepositoryInfo.h>


namespace BPackageKit {

namespace BPrivate {


/**
 * @brief Construct the job with all parameters needed to activate a config.
 *
 * @param context                Reference to the package-kit context.
 * @param title                  Human-readable title shown in progress UI.
 * @param archivedRepoInfoEntry  BEntry pointing to the downloaded repo-info
 *                               archive from which the BRepositoryInfo is read.
 * @param repositoryBaseURL      The base URL actually used for the fetch;
 *                               injected into the resulting config.  If empty,
 *                               the URL stored in the repo info is used instead.
 * @param targetDirectory        Directory where the config file is written.
 */
ActivateRepositoryConfigJob::ActivateRepositoryConfigJob(
	const BContext& context, const BString& title,
	const BEntry& archivedRepoInfoEntry, const BString& repositoryBaseURL,
	const BDirectory& targetDirectory)
	:
	inherited(context, title),
	fArchivedRepoInfoEntry(archivedRepoInfoEntry),
	fRepositoryBaseURL(repositoryBaseURL),
	fTargetDirectory(targetDirectory)
{
}


/**
 * @brief Destructor.
 */
ActivateRepositoryConfigJob::~ActivateRepositoryConfigJob()
{
}


/**
 * @brief Parse the repo info archive, optionally prompt the user, then write the config.
 *
 * Reads a BRepositoryInfo from the archived entry, resolves the destination
 * path, asks the decision provider whether to overwrite if a config already
 * exists, and stores the resulting BRepositoryConfig.  On success the
 * activated repository name is cached for retrieval via RepositoryName().
 *
 * @return B_OK on success, B_CANCELED if the user declined to overwrite,
 *         or an error code on failure.
 */
status_t
ActivateRepositoryConfigJob::Execute()
{
	BRepositoryInfo repoInfo(fArchivedRepoInfoEntry);
	status_t result = repoInfo.InitCheck();
	if (result != B_OK)
		return result;

	result = fTargetEntry.SetTo(&fTargetDirectory, repoInfo.Name().String());
	if (result != B_OK)
		return result;

	if (fTargetEntry.Exists()) {
		BString description = BString("A repository configuration for ")
			<< repoInfo.Name() << " already exists.";
		BString question("overwrite?");
		bool yes = fContext.DecisionProvider().YesNoDecisionNeeded(
			description, question, "yes", "no", "no");
		if (!yes) {
			fTargetEntry.Unset();
			return B_CANCELED;
		}
	}

	// create and store the configuration (injecting the BaseURL that was
	// actually used).
	BRepositoryConfig repoConfig;
	repoConfig.SetName(repoInfo.Name());
	repoConfig.SetBaseURL(fRepositoryBaseURL);
	repoConfig.SetIdentifier(repoInfo.Identifier());
	repoConfig.SetPriority(repoInfo.Priority());

	if (fRepositoryBaseURL.IsEmpty()) {
		repoConfig.SetBaseURL(repoInfo.BaseURL());
	} else {
		repoConfig.SetBaseURL(fRepositoryBaseURL);
	}

	if ((result = repoConfig.Store(fTargetEntry)) != B_OK)
		return result;

	// store name of activated repository as result
	fRepositoryName = repoConfig.Name();

	return B_OK;
}


/**
 * @brief Remove the partially written config file if the job did not succeed.
 *
 * Called by the job framework after Execute() completes (or is aborted).
 * If the job failed for a reason other than a user abort, and a target entry
 * was already established, this method removes it to avoid leaving a corrupt
 * or incomplete configuration file on disk.
 *
 * @param jobResult  The status code returned by Execute().
 */
void
ActivateRepositoryConfigJob::Cleanup(status_t jobResult)
{
	if (jobResult != B_OK && State() != BSupportKit::B_JOB_STATE_ABORTED
		&& fTargetEntry.InitCheck() == B_OK)
		fTargetEntry.Remove();
}


/**
 * @brief Return the name of the repository that was activated.
 *
 * Only meaningful after a successful Execute() call.
 *
 * @return Const reference to the activated repository name string.
 */
const BString&
ActivateRepositoryConfigJob::RepositoryName() const
{
	return fRepositoryName;
}


}	// namespace BPrivate

}	// namespace BPackageKit
