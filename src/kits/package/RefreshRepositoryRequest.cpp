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
 * @file RefreshRepositoryRequest.cpp
 * @brief Implementation of BRefreshRepositoryRequest for updating a repository cache.
 *
 * BRefreshRepositoryRequest orchestrates the sequence of jobs needed to
 * refresh a remote package repository: it fetches the remote checksum file,
 * compares it against the local cache, and — when they differ — downloads the
 * new cache and atomically installs it. The job graph is built in
 * CreateInitialJobs() and is driven by the BRequest base class machinery.
 *
 * @see BRequest, BRepositoryConfig, ValidateChecksumJob, FetchFileJob
 */


#include <package/RefreshRepositoryRequest.h>

#include <Catalog.h>
#include <Directory.h>
#include <Path.h>

#include <JobQueue.h>

#include <package/ChecksumAccessors.h>
#include <package/RepositoryCache.h>
#include <package/RepositoryConfig.h>
#include <package/PackageRoster.h>

#include "ActivateRepositoryCacheJob.h"
#include "FetchFileJob.h"
#include "ValidateChecksumJob.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "RefreshRepositoryRequest"


namespace BPackageKit {


using namespace BPrivate;


/**
 * @brief Construct a BRefreshRepositoryRequest for the given repository.
 *
 * @param context     The BContext providing shared services (temp files, etc.).
 * @param repoConfig  The repository configuration describing what to refresh.
 */
BRefreshRepositoryRequest::BRefreshRepositoryRequest(const BContext& context,
	const BRepositoryConfig& repoConfig)
	:
	inherited(context),
	fRepoConfig(repoConfig)
{
}


/**
 * @brief Destroy the request.
 */
BRefreshRepositoryRequest::~BRefreshRepositoryRequest()
{
}


/**
 * @brief Build the initial job graph for refreshing the repository cache.
 *
 * Queues a FetchFileJob that downloads the remote repo.sha256 checksum, then
 * chains a ValidateChecksumJob that compares it to the local cache. If the
 * checksums already match the graph terminates there; otherwise JobSucceeded()
 * will append further jobs to download the new cache.
 *
 * @return B_OK on success, B_NO_INIT if the request is uninitialised,
 *         B_NO_MEMORY on allocation failure, or another error code on failure.
 */
status_t
BRefreshRepositoryRequest::CreateInitialJobs()
{
	status_t result = InitCheck();
	if (result != B_OK)
		return B_NO_INIT;

	if ((result = fRepoConfig.InitCheck()) != B_OK)
		return result;

	// fetch the current checksum and compare with our cache's checksum,
	// if they differ, fetch the updated cache
	result = fContext.GetNewTempfile("repochecksum-", &fFetchedChecksumFile);
	if (result != B_OK)
		return result;
	BString repoChecksumURL
		= BString(fRepoConfig.BaseURL()) << "/" << "repo.sha256";
	BString title = B_TRANSLATE("Fetching repository checksum from %url");
	title.ReplaceAll("%url", fRepoConfig.BaseURL());
	FetchFileJob* fetchChecksumJob = new (std::nothrow) FetchFileJob(
		fContext, title, repoChecksumURL, fFetchedChecksumFile);
	if (fetchChecksumJob == NULL)
		return B_NO_MEMORY;
	if ((result = QueueJob(fetchChecksumJob)) != B_OK) {
		delete fetchChecksumJob;
		return result;
	}

	BRepositoryCache repoCache;
	BPackageRoster roster;
	// We purposely don't check this error, because this may be for a new repo,
	// which doesn't have a cache file yet. The true passed to
	// GeneralFileChecksumAccessor below will handle this case, and cause the
	// repo data to be fetched and cached for the future in JobSucceeded below.
	roster.GetRepositoryCache(fRepoConfig.Name(), &repoCache);

	title = B_TRANSLATE("Validating checksum for %repositoryName");
	title.ReplaceAll("%repositoryName", fRepoConfig.Name());
	ValidateChecksumJob* validateChecksumJob
		= new (std::nothrow) ValidateChecksumJob(fContext,
			title,
			new (std::nothrow) ChecksumFileChecksumAccessor(
				fFetchedChecksumFile),
			new (std::nothrow) GeneralFileChecksumAccessor(repoCache.Entry(),
				true),
			false);
	if (validateChecksumJob == NULL)
		return B_NO_MEMORY;
	validateChecksumJob->AddDependency(fetchChecksumJob);
	if ((result = QueueJob(validateChecksumJob)) != B_OK) {
		delete validateChecksumJob;
		return result;
	}
	fValidateChecksumJob = validateChecksumJob;

	return B_OK;
}


/**
 * @brief React to a completed job by triggering a cache download when needed.
 *
 * When the validate-checksum job completes and the checksums differ, this
 * method queues additional jobs via _FetchRepositoryCache() to download
 * and activate the updated cache.
 *
 * @param job  The job that just succeeded.
 */
void
BRefreshRepositoryRequest::JobSucceeded(BSupportKit::BJob* job)
{
	if (job == fValidateChecksumJob
		&& !fValidateChecksumJob->ChecksumsMatch()) {
		// the remote repo cache has a different checksum, we fetch it
		fValidateChecksumJob = NULL;
			// don't re-trigger fetching if anything goes wrong, fail instead
		_FetchRepositoryCache();
	}
}


/**
 * @brief Queue the jobs that download and activate an updated repository cache.
 *
 * Creates a FetchFileJob to download the remote "repo" file, a
 * ValidateChecksumJob to verify its integrity, and an
 * ActivateRepositoryCacheJob to atomically install it.
 *
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or another
 *         error code if a job cannot be queued.
 */
status_t
BRefreshRepositoryRequest::_FetchRepositoryCache()
{
	// download repository cache and put it in either the common/user cache
	// path, depending on where the corresponding repo-config lives

	// job fetching the cache
	BEntry tempRepoCache;
	status_t result = fContext.GetNewTempfile("repocache-", &tempRepoCache);
	if (result != B_OK)
		return result;
	BString repoCacheURL = BString(fRepoConfig.BaseURL()) << "/" << "repo";
	BString title = B_TRANSLATE("Fetching repository-cache from %url");
	title.ReplaceAll("%url", fRepoConfig.BaseURL());
	FetchFileJob* fetchCacheJob = new (std::nothrow) FetchFileJob(fContext,
		title, repoCacheURL, tempRepoCache);
	if (fetchCacheJob == NULL)
		return B_NO_MEMORY;
	if ((result = QueueJob(fetchCacheJob)) != B_OK) {
		delete fetchCacheJob;
		return result;
	}

	// job validating the cache's checksum
	title = B_TRANSLATE("Validating checksum for %repositoryName");
	title.ReplaceAll("%repositoryName", fRepoConfig.Name());
	ValidateChecksumJob* validateChecksumJob
		= new (std::nothrow) ValidateChecksumJob(fContext,
			title,
			new (std::nothrow) ChecksumFileChecksumAccessor(
				fFetchedChecksumFile),
			new (std::nothrow) GeneralFileChecksumAccessor(tempRepoCache));
	if (validateChecksumJob == NULL)
		return B_NO_MEMORY;
	validateChecksumJob->AddDependency(fetchCacheJob);
	if ((result = QueueJob(validateChecksumJob)) != B_OK) {
		delete validateChecksumJob;
		return result;
	}

	// job activating the cache
	BPath targetRepoCachePath;
	BPackageRoster roster;
	result = fRepoConfig.IsUserSpecific()
		? roster.GetUserRepositoryCachePath(&targetRepoCachePath, true)
		: roster.GetCommonRepositoryCachePath(&targetRepoCachePath, true);
	if (result != B_OK)
		return result;
	BDirectory targetDirectory(targetRepoCachePath.Path());
	ActivateRepositoryCacheJob* activateJob
		= new (std::nothrow) ActivateRepositoryCacheJob(fContext,
			BString("Activating repository cache for ") << fRepoConfig.Name(),
			tempRepoCache, fRepoConfig.Name(), targetDirectory);
	if (activateJob == NULL)
		return B_NO_MEMORY;
	activateJob->AddDependency(validateChecksumJob);
	if ((result = QueueJob(activateJob)) != B_OK) {
		delete activateJob;
		return result;
	}

	return B_OK;
}


}	// namespace BPackageKit
