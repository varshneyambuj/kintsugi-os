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
 *
 * Copyright 2013-2014, Haiku, Inc. All Rights Reserved.
  * Distributed under the terms of the MIT License.
  *
  * Authors:
  *		Ingo Weinhold <ingo_weinhold@gmx.de>
 */

/** @file JobQueue.cpp
 *  @brief Implements the mutex-protected job queue with condition-variable signaling */



#include "JobQueue.h"

#include <PthreadMutexLocker.h>


// #pragma mark - JobQueue


/**
 * @brief Constructs a JobQueue in an uninitialized state.
 *
 * Init() must be called before queueing or dequeueing jobs.
 */
JobQueue::JobQueue()
	:
	fMutexInitialized(false),
	fNewJobConditionInitialized(false),
	fJobs(),
	fClosed(false)
{
}


/**
 * @brief Destroys the JobQueue, releasing all remaining jobs and destroying
 *        synchronization primitives.
 */
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


/**
 * @brief Initializes the mutex and condition variable used by the queue.
 *
 * @return B_OK on success, or a pthread error code on failure.
 */
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


/**
 * @brief Closes the queue and wakes all threads waiting in DequeueJob().
 *
 * After closing, QueueJob() will reject new jobs and DequeueJob() will
 * return NULL once the queue is empty.
 */
void
JobQueue::Close()
{
	if (fMutexInitialized && fNewJobConditionInitialized) {
		PthreadMutexLocker mutexLocker(fMutex);
		fClosed = true;
		pthread_cond_broadcast(&fNewJobCondition);
	}
}


/**
 * @brief Adds a job to the queue and signals a waiting consumer thread.
 *
 * Acquires a reference on the job. If the queue has been closed, the
 * job is not added and @c false is returned.
 *
 * @param job The job to enqueue; must not be NULL.
 * @return @c true if the job was added, @c false if the queue is closed.
 */
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


/**
 * @brief Removes and returns the next job from the queue, blocking if empty.
 *
 * Blocks on the condition variable until a job is available or the queue
 * is closed. The caller receives ownership of the job's reference.
 *
 * @return The next Job, or NULL if the queue has been closed.
 */
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


/**
 * @brief Removes and deletes all jobs matching the supplied filter.
 *
 * Iterates the queue under the mutex and deletes every job for which
 * the filter returns @c true.
 *
 * @param filter A Filter whose FilterJob() method selects jobs for deletion.
 */
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


/** @brief Destroys the Filter. */
JobQueue::Filter::~Filter()
{
}
