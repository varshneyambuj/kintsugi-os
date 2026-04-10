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
   /*
    * Copyright 2013-2014, Haiku, Inc. All Rights Reserved.
    * Distributed under the terms of the MIT License.
    *
    * Authors:
    *		Ingo Weinhold <ingo_weinhold@gmx.de>
    */
 */

/** @file JobQueue.cpp
 *  @brief Implements the mutex-protected job queue with condition-variable signaling */



#include "JobQueue.h"

#include <PthreadMutexLocker.h>


// #pragma mark - JobQueue


JobQueue::JobQueue()
	:
	fMutexInitialized(false),
	fNewJobConditionInitialized(false),
	fJobs(),
	fClosed(false)
{
}


JobQueue::~JobQueue()
{
	if (fMutexInitialized) {
		PthreadMutexLocker mutexLocker(fMutex);
		while (Job* job = fJobs.RemoveHead())
			job->ReleaseReference();
	}

	if (fNewJobConditionInitialized)
		pthread_cond_destroy(&fNewJobCondition);

	if (fMutexInitialized)
		pthread_mutex_destroy(&fMutex);
}


status_t
JobQueue::Init()
{
	status_t error = pthread_mutex_init(&fMutex, NULL);
	if (error != B_OK)
		return error;
	fMutexInitialized = true;

	error = pthread_cond_init(&fNewJobCondition, NULL);
	if (error != B_OK)
		return error;
	fNewJobConditionInitialized = true;

	return B_OK;
}


void
JobQueue::Close()
{
	if (fMutexInitialized && fNewJobConditionInitialized) {
		PthreadMutexLocker mutexLocker(fMutex);
		fClosed = true;
		pthread_cond_broadcast(&fNewJobCondition);
	}
}


bool
JobQueue::QueueJob(Job* job)
{
	PthreadMutexLocker mutexLocker(fMutex);
	if (fClosed)
		return false;

	fJobs.Add(job);
	job->AcquireReference();

	pthread_cond_signal(&fNewJobCondition);
	return true;
}


Job*
JobQueue::DequeueJob()
{
	PthreadMutexLocker mutexLocker(fMutex);

	while (!fClosed) {
		Job* job = fJobs.RemoveHead();
		if (job != NULL)
			return job;

		if (!fClosed)
			pthread_cond_wait(&fNewJobCondition, &fMutex);
	}

	return NULL;
}


void
JobQueue::DeleteJobs(Filter* filter)
{
	PthreadMutexLocker mutexLocker(fMutex);

	for (JobList::Iterator it = fJobs.GetIterator(); Job* job = it.Next();) {
		if (filter->FilterJob(job)) {
			it.Remove();
			delete job;
		}
	}
}


// #pragma mark - Filter


JobQueue::Filter::~Filter()
{
}
