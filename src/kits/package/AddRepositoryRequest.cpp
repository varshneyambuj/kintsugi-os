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
 *   Copyright 2011-2015, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe <zooey@hirschkaefer.de>
 */


/**
 * @file AddRepositoryRequest.cpp
 * @brief High-level request that fetches and activates a new package repository.
 *
 * AddRepositoryRequest orchestrates a two-job pipeline: a FetchFileJob that
 * downloads the repo.info file from the given base URL, followed by an
 * ActivateRepositoryConfigJob that parses the info and writes the repository
 * configuration to either the user or system config directory.
 *
 * @see DropRepositoryRequest, FetchFileJob, ActivateRepositoryConfigJob
 */


#include <package/AddRepositoryRequest.h>

#include <Directory.h>
#include <JobQueue.h>
#include <Path.h>

#include <package/PackageRoster.h>

#include "ActivateRepositoryConfigJob.h"
#include "FetchFileJob.h"


namespace BPackageKit {


using namespace BPrivate;


/**
 * @brief Construct the request for a given repository base URL.
 *
 * @param context             Package-kit context for temp files and decisions.
 * @param repositoryBaseURL   Base URL of the repository to add (without trailing slash).
 * @param asUserRepository    If true, the config is written to the per-user
 *                            config path; otherwise to the system-wide path.
 */
AddRepositoryRequest::AddRepositoryRequest(const BContext& context,
	const BString& repositoryBaseURL, bool asUserRepository)
	:
	inherited(context),
	fRepositoryBaseURL(repositoryBaseURL),
	fAsUserRepository(asUserRepository),
	fActivateJob(NULL)
{
}


/**
 * @brief Destructor.
 */
AddRepositoryRequest::~AddRepositoryRequest()
{
}


/**
 * @brief Build and queue the fetch and activate jobs that implement this request.
 *
 * Creates a FetchFileJob for the repo.info file and an
 * ActivateRepositoryConfigJob that depends on it.  On success both jobs are
 * enqueued and the activate job pointer is cached for use in JobSucceeded().
 *
 * @return B_OK if both jobs were queued, B_NO_INIT if InitCheck() fails,
 *         B_NO_MEMORY on allocation failure, or another error code on failure.
 */
status_t
AddRepositoryRequest::CreateInitialJobs()
{
	status_t result = InitCheck();
	if (result != B_OK)
		return B_NO_INIT;

	BEntry tempEntry;
	result = fContext.GetNewTempfile("repoinfo-", &tempEntry);
	if (result != B_OK)
		return result;
	BString repoInfoURL = BString(fRepositoryBaseURL) << "/" << "repo.info";
	FetchFileJob* fetchJob = new (std::nothrow) FetchFileJob(fContext,
		BString("Fetching repository info from ") << fRepositoryBaseURL,
		repoInfoURL, tempEntry);
	if (fetchJob == NULL)
		return B_NO_MEMORY;
	if ((result = QueueJob(fetchJob)) != B_OK) {
		delete fetchJob;
		return result;
	}

	BPackageRoster roster;
	BPath targetRepoConfigPath;
	result = fAsUserRepository
		? roster.GetUserRepositoryConfigPath(&targetRepoConfigPath, true)
		: roster.GetCommonRepositoryConfigPath(&targetRepoConfigPath, true);
	if (result != B_OK)
		return result;
	BDirectory targetDirectory(targetRepoConfigPath.Path());
	ActivateRepositoryConfigJob* activateJob
		= new (std::nothrow) ActivateRepositoryConfigJob(fContext,
			BString("Activating repository config from ") << fRepositoryBaseURL,
			tempEntry, fRepositoryBaseURL, targetDirectory);
	if (activateJob == NULL)
		return B_NO_MEMORY;
	result = activateJob->AddDependency(fetchJob);
	if (result != B_OK)
		return result;
	if ((result = QueueJob(activateJob)) != B_OK) {
		delete activateJob;
		return result;
	}
	fActivateJob = activateJob;

	return B_OK;
}


/**
 * @brief Capture the repository name once the activate job completes.
 *
 * Called by the job framework when any job in this request succeeds.  If
 * \a job is the activate job, the repository name is read back and cached.
 *
 * @param job  The job that just succeeded.
 */
void
AddRepositoryRequest::JobSucceeded(BSupportKit::BJob* job)
{
	if (job == fActivateJob)
		fRepositoryName = fActivateJob->RepositoryName();
}


/**
 * @brief Return the name of the repository that was added.
 *
 * Only meaningful after the request has completed successfully.
 *
 * @return Const reference to the activated repository name.
 */
const BString&
AddRepositoryRequest::RepositoryName() const
{
	return fRepositoryName;
}


}	// namespace BPackageKit
