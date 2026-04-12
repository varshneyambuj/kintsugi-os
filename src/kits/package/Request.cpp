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
 * @file Request.cpp
 * @brief Base implementation of BRequest, the package-kit asynchronous operation container.
 *
 * BRequest owns a JobQueue and provides the glue between a high-level
 * package operation (install, refresh, etc.) and the job-runner infrastructure.
 * Subclasses override CreateInitialJobs() to populate the queue; BRequest::Process()
 * then drives execution by repeatedly dequeuing and running jobs.
 *
 * @see BRefreshRepositoryRequest, BSupportKit::BJob
 */


#include <package/Request.h>

#include <new>

#include <JobQueue.h>

#include <package/Context.h>


namespace BPackageKit {


using BSupportKit::BPrivate::JobQueue;


/**
 * @brief Construct a BRequest associated with the given context.
 *
 * Allocates an internal JobQueue; fInitStatus is set to B_NO_MEMORY if
 * allocation fails, B_OK otherwise.
 *
 * @param context  The BContext providing shared services.
 */
BRequest::BRequest(const BContext& context)
	:
	fContext(context),
	fJobQueue(new (std::nothrow) JobQueue())
{
	fInitStatus = fJobQueue == NULL ? B_NO_MEMORY : B_OK;
}


/**
 * @brief Destroy the request and its job queue.
 */
BRequest::~BRequest()
{
}


/**
 * @brief Check whether this request was successfully initialised.
 *
 * @return B_OK if the job queue was allocated, B_NO_MEMORY otherwise.
 */
status_t
BRequest::InitCheck() const
{
	return fInitStatus;
}


/**
 * @brief Dequeue and return the next runnable job, or NULL when the queue is empty.
 *
 * @return A pointer to the next BJob ready to run, or NULL if none remain.
 */
BSupportKit::BJob*
BRequest::PopRunnableJob()
{
	if (fJobQueue == NULL)
		return NULL;

	return fJobQueue->Pop();
}


/**
 * @brief Execute all jobs in the queue until completion or error.
 *
 * Calls CreateInitialJobs() to populate the queue, then runs each dequeued job
 * in turn. When @a failIfCanceledOnly is true, only B_CANCELED stops the loop
 * early; other errors are silently ignored.
 *
 * @param failIfCanceledOnly  If true, errors other than B_CANCELED are ignored.
 * @return B_OK on full success, or an error code on failure.
 */
status_t
BRequest::Process(bool failIfCanceledOnly)
{
	status_t error = InitCheck();
	if (error != B_OK)
		return error;

	error = CreateInitialJobs();
	if (error != B_OK)
		return error;

	while (BSupportKit::BJob* job = PopRunnableJob()) {
		error = job->Run();
		delete job;
		if (error != B_OK) {
			if (!failIfCanceledOnly || error == B_CANCELED)
				return error;
		}
	}

	return B_OK;
}


/**
 * @brief Add a job to the queue and register this request as a state listener.
 *
 * The request's context job-state listener is also registered so progress
 * can be forwarded to the UI layer.
 *
 * @param job  The job to enqueue; ownership is transferred to the queue.
 * @return B_OK on success, B_NO_INIT if the queue is NULL, or another error
 *         code if the job cannot be added.
 */
status_t
BRequest::QueueJob(BSupportKit::BJob* job)
{
	if (fJobQueue == NULL)
		return B_NO_INIT;

	job->AddStateListener(this);
	job->AddStateListener(&fContext.JobStateListener());

	return fJobQueue->AddJob(job);
}


}	// namespace BPackageKit
