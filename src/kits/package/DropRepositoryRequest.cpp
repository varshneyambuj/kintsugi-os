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
 * @file DropRepositoryRequest.cpp
 * @brief Request that removes a package repository configuration from the system.
 *
 * DropRepositoryRequest creates and enqueues a single RemoveRepositoryJob
 * identified by the repository name.  On completion the config file for that
 * repository is deleted from the appropriate config directory.
 *
 * @see AddRepositoryRequest, RemoveRepositoryJob
 */


#include <package/DropRepositoryRequest.h>

#include <Directory.h>
#include <JobQueue.h>
#include <Path.h>

#include "RemoveRepositoryJob.h"


namespace BPackageKit {


using namespace BPrivate;


/**
 * @brief Construct the request for a named repository.
 *
 * @param context         Package-kit context.
 * @param repositoryName  Name of the repository whose config should be removed.
 */
DropRepositoryRequest::DropRepositoryRequest(const BContext& context,
	const BString& repositoryName)
	:
	inherited(context),
	fRepositoryName(repositoryName)
{
}


/**
 * @brief Destructor.
 */
DropRepositoryRequest::~DropRepositoryRequest()
{
}


/**
 * @brief Create and queue the repository-removal job.
 *
 * @return B_OK if the job was queued successfully, B_NO_INIT if InitCheck()
 *         fails, or B_NO_MEMORY on allocation failure.
 */
status_t
DropRepositoryRequest::CreateInitialJobs()
{
	status_t result = InitCheck();
	if (result != B_OK)
		return B_NO_INIT;

	RemoveRepositoryJob* removeRepoJob
		= new (std::nothrow) RemoveRepositoryJob(fContext,
			BString("Removing repository ") << fRepositoryName,
			fRepositoryName);
	if (removeRepoJob == NULL)
		return B_NO_MEMORY;
	if ((result = QueueJob(removeRepoJob)) != B_OK) {
		delete removeRepoJob;
		return result;
	}

	return B_OK;
}


}	// namespace BPackageKit
