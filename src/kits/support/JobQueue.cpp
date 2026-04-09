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
 *       Axel Dörfler <axeld@pinc-software.de>
 *       Oliver Tappe <zooey@hirschkaefer.de>
 */


/**
 * @file JobQueue.cpp
 * @brief Implementation of JobQueue in the BSupportKit::BPrivate namespace.
 *
 * JobQueue manages a priority-ordered set of BJob instances. Jobs are kept in
 * a std::set sorted by JobPriorityLess: jobs with fewer outstanding
 * dependencies sort before those with more (i.e. runnable jobs come first),
 * with ties broken by ascending ticket number (FIFO within the same priority
 * level).
 *
 * A semaphore (fHaveRunnableJobSem) is released once for each runnable job
 * that enters the queue; Pop() acquires the semaphore before returning a job,
 * blocking until one becomes available or a timeout expires.
 *
 * Thread safety: all public methods acquire fLock before touching internal
 * state.
 *
 * @see BJob, BJobStateListener
 */


#include <JobQueue.h>

#include <set>

#include <Autolock.h>
#include <Job.h>

#include <JobPrivate.h>


namespace BSupportKit {

namespace BPrivate {


struct JobQueue::JobPriorityLess {
	bool operator()(const BJob* left, const BJob* right) const;
};


/*!	Sort jobs by:
		1. descending count of dependencies (only jobs without dependencies are
		   runnable)
		2. job ticket number (order in which jobs were added to the queue)
*/
/**
 * @brief Compare two jobs for priority ordering.
 *
 * Jobs are ordered first by ascending dependency count (jobs with zero
 * dependencies — i.e. runnable jobs — sort before jobs that are still
 * waiting on prerequisites). Ties are broken by ascending ticket number so
 * that earlier-enqueued jobs run first (FIFO).
 *
 * @param left  The first job to compare.
 * @param right The second job to compare.
 * @return true if \a left should be executed before \a right.
 */
bool
JobQueue::JobPriorityLess::operator()(const BJob* left, const BJob* right) const
{
	int32 difference = left->CountDependencies() - right->CountDependencies();
	if (difference < 0)
		return true;
	if (difference > 0)
		return false;

	return left->TicketNumber() < right->TicketNumber();
};


class JobQueue::JobPriorityQueue
	: public std::set<BJob*, JobPriorityLess> {
};


// #pragma mark -


/**
 * @brief Construct a JobQueue and initialise all internal resources.
 *
 * Calls _Init() to allocate the priority queue and the runnable-job semaphore.
 * Check InitCheck() after construction to verify that initialisation succeeded.
 */
JobQueue::JobQueue()
	:
	fLock("job queue"),
	fNextTicketNumber(1)
{
	fInitStatus = _Init();
}


/**
 * @brief Destroy the JobQueue, closing it and releasing all resources.
 *
 * Calls Close() to delete all queued jobs and the semaphore, then frees the
 * internal priority-queue object. Any BJob pointers still held by callers
 * become invalid after this call.
 */
JobQueue::~JobQueue()
{
	Close();
	delete fQueuedJobs;
}


/**
 * @brief Return the initialisation status of the queue.
 *
 * @return B_OK if the queue was successfully initialised and is ready to use,
 *         or an error code (e.g. B_NO_MEMORY, B_NO_MORE_SEMS) if _Init() failed.
 */
status_t
JobQueue::InitCheck() const
{
	return fInitStatus;
}


/**
 * @brief Insert \a job into the queue and assign it a ticket number.
 *
 * The job is added to the internal priority set. If the job is immediately
 * runnable (no dependencies), the runnable-job semaphore is released so that
 * any blocked Pop() call can proceed. The queue also registers itself as a
 * state listener on the job.
 *
 * @param job The job to enqueue. Must not already be present in the queue.
 * @return B_OK on success.
 *         B_NO_INIT if the queue was not successfully initialised.
 *         B_NAME_IN_USE if \a job is already in the queue.
 *         B_NO_MEMORY if the std::set insertion throws std::bad_alloc.
 *         B_ERROR on any other failure or if the queue lock cannot be acquired.
 */
status_t
JobQueue::AddJob(BJob* job)
{
	if (fQueuedJobs == NULL)
		return B_NO_INIT;

	BAutolock lock(&fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	try {
		if (!fQueuedJobs->insert(job).second)
			return B_NAME_IN_USE;
	} catch (const std::bad_alloc& e) {
		return B_NO_MEMORY;
	} catch (...) {
		return B_ERROR;
	}
	BJob::Private(*job).SetTicketNumber(fNextTicketNumber++);
	job->AddStateListener(this);
	if (job->IsRunnable())
		release_sem(fHaveRunnableJobSem);

	return B_OK;
}


/**
 * @brief Remove \a job from the queue without executing or deleting it.
 *
 * Clears the job's ticket number and unregisters the queue as a state
 * listener. The job object itself is not deleted.
 *
 * @param job The job to remove.
 * @return B_OK on success.
 *         B_NO_INIT if the queue was not successfully initialised.
 *         B_NAME_NOT_FOUND if \a job is not currently in the queue.
 *         B_ERROR on other failures.
 */
status_t
JobQueue::RemoveJob(BJob* job)
{
	if (fQueuedJobs == NULL)
		return B_NO_INIT;

	BAutolock lock(&fLock);
	if (lock.IsLocked()) {
		try {
			if (fQueuedJobs->erase(job) == 0)
				return B_NAME_NOT_FOUND;
		} catch (...) {
			return B_ERROR;
		}
		BJob::Private(*job).ClearTicketNumber();
		job->RemoveStateListener(this);
	}

	return B_OK;
}


/**
 * @brief BJobStateListener callback — called when \a job succeeds.
 *
 * Acquires the queue lock and re-sorts any jobs that were waiting on \a job,
 * making them eligible to run if all their remaining dependencies are gone.
 *
 * @param job The job that just succeeded.
 * @see _RequeueDependantJobsOf()
 */
void
JobQueue::JobSucceeded(BJob* job)
{
	BAutolock lock(&fLock);
	if (lock.IsLocked())
		_RequeueDependantJobsOf(job);
}


/**
 * @brief BJobStateListener callback — called when \a job fails.
 *
 * Acquires the queue lock and recursively removes (and aborts) all jobs that
 * were waiting on \a job.
 *
 * @param job The job that just failed.
 * @see _RemoveDependantJobsOf()
 */
void
JobQueue::JobFailed(BJob* job)
{
	BAutolock lock(&fLock);
	if (lock.IsLocked())
		_RemoveDependantJobsOf(job);
}


/**
 * @brief Remove and return the next runnable job, blocking indefinitely.
 *
 * Convenience wrapper around Pop(bigtime_t, bool, BJob**) that waits forever
 * and treats an empty queue as a reason to wait rather than return.
 *
 * @return A pointer to the dequeued job, or NULL if the queue is closed or an
 *         error occurs.
 */
BJob*
JobQueue::Pop()
{
	BJob* job;
	if (Pop(B_INFINITE_TIMEOUT, true, &job) == B_OK)
		return job;

	return NULL;
}


/**
 * @brief Remove and return the next runnable job, with timeout and empty-queue
 *        control.
 *
 * If the head of the priority queue is runnable it is removed and returned
 * immediately. Otherwise the method releases the queue lock, waits on the
 * runnable-job semaphore for up to \a timeout microseconds, re-acquires the
 * lock, and retries. If \a returnWhenEmpty is true and the queue is empty the
 * method returns B_ENTRY_NOT_FOUND immediately instead of waiting.
 *
 * @param timeout         Maximum wait time in microseconds
 *                        (B_INFINITE_TIMEOUT to wait forever).
 * @param returnWhenEmpty If true, return B_ENTRY_NOT_FOUND when the queue is
 *                        empty instead of blocking.
 * @param _job            Output pointer that receives the dequeued job on
 *                        success.
 * @return B_OK and a valid *_job on success.
 *         B_ENTRY_NOT_FOUND if the queue is empty and \a returnWhenEmpty is
 *         true.
 *         B_TIMED_OUT if \a timeout elapses before a runnable job is available.
 *         B_ERROR if the lock cannot be acquired.
 */
status_t
JobQueue::Pop(bigtime_t timeout, bool returnWhenEmpty, BJob** _job)
{
	BAutolock lock(&fLock);
	if (lock.IsLocked()) {
		while (true) {
			JobPriorityQueue::iterator head = fQueuedJobs->begin();
			if (head != fQueuedJobs->end()) {
				if ((*head)->IsRunnable()) {
					*_job = *head;
					fQueuedJobs->erase(head);
					return B_OK;
				}
			} else if (returnWhenEmpty)
				return B_ENTRY_NOT_FOUND;

			// we need to wait until a job becomes available/runnable
			status_t result;
			do {
				lock.Unlock();
				result = acquire_sem_etc(fHaveRunnableJobSem, 1,
					B_RELATIVE_TIMEOUT, timeout);
				if (!lock.Lock())
					return B_ERROR;
			} while (result == B_INTERRUPTED);
			if (result != B_OK)
				return result;
		}
	}

	return B_ERROR;
}


/**
 * @brief Return the number of jobs currently in the queue.
 *
 * @return The count of jobs (both runnable and waiting) currently enqueued.
 */
size_t
JobQueue::CountJobs() const
{
	BAutolock locker(fLock);
	return fQueuedJobs->size();
}


/**
 * @brief Close the queue, delete all remaining jobs, and destroy the semaphore.
 *
 * After Close() returns, AddJob() and Pop() must not be called. Any jobs still
 * in the queue at the time of the call are deleted. Close() is idempotent: a
 * second call is a no-op.
 */
void
JobQueue::Close()
{
	if (fHaveRunnableJobSem < 0)
		return;

	BAutolock lock(&fLock);
	if (lock.IsLocked()) {
		delete_sem(fHaveRunnableJobSem);
		fHaveRunnableJobSem = -1;

		if (fQueuedJobs != NULL) {
			// get rid of all jobs
			for (JobPriorityQueue::iterator iter = fQueuedJobs->begin();
				iter != fQueuedJobs->end(); ++iter) {
				delete (*iter);
			}
			fQueuedJobs->clear();
		}
	}
}


/**
 * @brief Initialise the queue's internal data structures.
 *
 * Verifies the lock, allocates the JobPriorityQueue, and creates the
 * runnable-job semaphore. Called once from the constructor.
 *
 * @return B_OK on success, B_NO_MEMORY if the priority queue cannot be
 *         allocated, or a negative semaphore error code.
 */
status_t
JobQueue::_Init()
{
	status_t result = fLock.InitCheck();
	if (result != B_OK)
		return result;

	fQueuedJobs = new (std::nothrow) JobPriorityQueue();
	if (fQueuedJobs == NULL)
		return B_NO_MEMORY;

	fHaveRunnableJobSem = create_sem(0, "have runnable job");
	if (fHaveRunnableJobSem < 0)
		return fHaveRunnableJobSem;

	return B_OK;
}


/**
 * @brief Re-insert dependant jobs of \a job after it has succeeded.
 *
 * For each job that listed \a job as a dependency: the dependant job is
 * temporarily removed from the set, its dependency on \a job is cleared via
 * RemoveDependency(), and then it is re-inserted so that the set re-sorts it
 * based on its new (reduced) dependency count. If the re-inserted job is now
 * runnable the semaphore is released.
 *
 * @param job The job that succeeded; its dependant list is drained.
 */
void
JobQueue::_RequeueDependantJobsOf(BJob* job)
{
	while (BJob* dependantJob = job->DependantJobAt(0)) {
		JobPriorityQueue::iterator found = fQueuedJobs->find(dependantJob);
		bool removed = false;
		if (found != fQueuedJobs->end()) {
			try {
				fQueuedJobs->erase(dependantJob);
				removed = true;
			} catch (...) {
			}
		}
		dependantJob->RemoveDependency(job);
		if (removed) {
			// Only insert a job if it was in our queue before
			try {
				fQueuedJobs->insert(dependantJob);
				if (dependantJob->IsRunnable())
					release_sem(fHaveRunnableJobSem);
			} catch (...) {
			}
		}
	}
}


/**
 * @brief Recursively remove and abort all jobs that depended on \a job.
 *
 * Called when \a job fails. Each dependant job is erased from the queue,
 * transitioned to B_JOB_STATE_ABORTED (if not already), notified, and then
 * its own dependants are processed recursively before it is deleted.
 *
 * @param job The failed job whose dependant tree should be pruned.
 */
void
JobQueue::_RemoveDependantJobsOf(BJob* job)
{
	while (BJob* dependantJob = job->DependantJobAt(0)) {
		try {
			fQueuedJobs->erase(dependantJob);
		} catch (...) {
		}

		if (dependantJob->State() != B_JOB_STATE_ABORTED) {
			BJob::Private(*dependantJob).SetState(B_JOB_STATE_ABORTED);
			BJob::Private(*dependantJob).NotifyStateListeners();
		}

		_RemoveDependantJobsOf(dependantJob);
		dependantJob->RemoveDependency(job);
		// TODO: we need some sort of ownership management
		delete dependantJob;
	}
}


}	// namespace BPrivate

}	// namespace BPackageKit
