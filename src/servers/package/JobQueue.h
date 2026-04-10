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

/** @file JobQueue.h
 *  @brief Thread-safe queue for scheduling and dispatching package daemon jobs */

#ifndef JOB_QUEUE_H
#define JOB_QUEUE_H


#include <pthread.h>

#include "Job.h"


/** @brief Thread-safe FIFO queue that dispatches Job objects to a worker thread */
class JobQueue {
public:
			class Filter;

public:
	/** @brief Construct an uninitialized job queue */
								JobQueue();
	/** @brief Destroy the queue and release any remaining job references */
								~JobQueue();

	/** @brief Initialize the mutex and condition variable; must be called before use */
			status_t			Init();
	/** @brief Close the queue and wake any blocked consumer thread */
			void				Close();

	/** @brief Enqueue a job, acquiring a reference on success */
			bool				QueueJob(Job* job);
									// acquires a reference, if successful
	/** @brief Block until a job is available and return it with a reference */
			Job*				DequeueJob();
									// returns a reference

	/** @brief Remove and delete all jobs accepted by the given filter */
			void				DeleteJobs(Filter* filter);

private:
			typedef DoublyLinkedList<Job> JobList;

private:
			pthread_mutex_t		fMutex;              /**< Protects the job list */
			pthread_cond_t		fNewJobCondition;    /**< Signals new job availability */
			bool				fMutexInitialized;
			bool				fNewJobConditionInitialized;
			JobList				fJobs;               /**< Pending jobs in FIFO order */
			bool				fClosed;             /**< True after Close() is called */
};


/** @brief Predicate interface for selectively removing jobs from the queue */
class JobQueue::Filter {
public:
	/** @brief Destructor */
	virtual						~Filter();

	/** @brief Return true if the given job should be removed and deleted */
	virtual	bool				FilterJob(Job* job) = 0;
};


#endif	// JOB_QUEUE_H
