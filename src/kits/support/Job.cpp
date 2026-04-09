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
 *       Rene Gollent <rene@gollent.com>
 */


/**
 * @file Job.cpp
 * @brief Implementation of BJob and BJobStateListener in the BSupportKit namespace.
 *
 * BJobStateListener is a pure observer interface that receives state-change
 * notifications from BJob. Subclasses override the virtual notification methods
 * to react to job lifecycle events.
 *
 * BJob models a unit of work that can declare dependencies on other BJob
 * instances. The JobQueue drives execution: a job becomes runnable once all
 * its dependencies have succeeded. During Run(), a job transitions through the
 * states WAITING_TO_RUN -> STARTED -> IN_PROGRESS -> SUCCEEDED/FAILED/ABORTED,
 * notifying all registered BJobStateListener observers at each transition.
 *
 * @see BJobStateListener, JobQueue
 */


#include <Job.h>

#include <Errors.h>


namespace BSupportKit {


/**
 * @brief Destroy the BJobStateListener.
 *
 * Virtual destructor ensures that deleting a BJobStateListener pointer
 * correctly calls the most-derived destructor.
 */
BJobStateListener::~BJobStateListener()
{
}


/**
 * @brief Called when \a job transitions to B_JOB_STATE_STARTED.
 *
 * The default implementation is a no-op. Subclasses may override to react
 * to the job beginning execution.
 *
 * @param job The job whose state has changed.
 */
void
BJobStateListener::JobStarted(BJob* job)
{
}


/**
 * @brief Called when \a job transitions to B_JOB_STATE_IN_PROGRESS.
 *
 * The default implementation is a no-op. Subclasses may override to update
 * progress indicators or perform intermediate work.
 *
 * @param job The job whose state has changed.
 */
void
BJobStateListener::JobProgress(BJob* job)
{
}


/**
 * @brief Called when \a job transitions to B_JOB_STATE_SUCCEEDED.
 *
 * The default implementation is a no-op. Subclasses may override to handle
 * successful completion, e.g. to trigger dependent processing.
 *
 * @param job The job whose state has changed.
 */
void
BJobStateListener::JobSucceeded(BJob* job)
{
}


/**
 * @brief Called when \a job transitions to B_JOB_STATE_FAILED.
 *
 * The default implementation is a no-op. Subclasses may override to handle
 * job failure, e.g. to log errors or retry.
 *
 * @param job The job whose state has changed.
 */
void
BJobStateListener::JobFailed(BJob* job)
{
}


/**
 * @brief Called when \a job transitions to B_JOB_STATE_ABORTED.
 *
 * The default implementation is a no-op. Subclasses may override to perform
 * cleanup after a job is cancelled.
 *
 * @param job The job whose state has changed.
 */
void
BJobStateListener::JobAborted(BJob* job)
{
}


// #pragma mark -


/**
 * @brief Construct a BJob with the given \a title.
 *
 * The job is initialised in B_JOB_STATE_WAITING_TO_RUN with no dependencies
 * and no registered listeners. If \a title is empty, InitCheck() will return
 * B_BAD_VALUE and the job must not be added to a queue.
 *
 * @param title A non-empty human-readable label for this job.
 * @see InitCheck(), Run()
 */
BJob::BJob(const BString& title)
	:
	fTitle(title),
	fState(B_JOB_STATE_WAITING_TO_RUN),
	fTicketNumber(0xFFFFFFFFUL)
{
	if (fTitle.Length() == 0)
		fInitStatus = B_BAD_VALUE;
	else
		fInitStatus = B_OK;
}


/**
 * @brief Destroy the BJob.
 *
 * No cleanup of dependencies or listeners is performed here; callers must
 * remove dependencies and listeners before destruction to avoid dangling
 * references.
 */
BJob::~BJob()
{
}


/**
 * @brief Return the initialisation status of the job.
 *
 * @return B_OK if the job was constructed with a non-empty title and is ready
 *         to be enqueued, or B_BAD_VALUE if the title was empty.
 */
status_t
BJob::InitCheck() const
{
	return fInitStatus;
}


/**
 * @brief Return the human-readable title of this job.
 *
 * @return A reference to the immutable title string set at construction time.
 */
const BString&
BJob::Title() const
{
	return fTitle;
}


/**
 * @brief Return the current execution state of this job.
 *
 * @return One of the BJobState enumeration values reflecting the last state
 *         transition.
 */
BJobState
BJob::State() const
{
	return fState;
}


/**
 * @brief Return the status code produced by the most recent Execute() call.
 *
 * Meaningful only after Run() has returned. B_OK indicates success;
 * B_CANCELED indicates the job was aborted; any other error indicates failure.
 *
 * @return The status_t returned by Execute(), or an uninitialised value if
 *         Run() has not yet been called.
 */
status_t
BJob::Result() const
{
	return fResult;
}


/**
 * @brief Return the human-readable error description, if any.
 *
 * @return A reference to the error string set by SetErrorString(), or an
 *         empty string if none has been set.
 */
const BString&
BJob::ErrorString() const
{
	return fErrorString;
}


/**
 * @brief Return the queue ticket number assigned to this job.
 *
 * The ticket number determines execution order within the JobQueue (lower
 * numbers run first among jobs with equal dependency counts). The sentinel
 * value 0xFFFFFFFF indicates the job has not yet been enqueued.
 *
 * @return The current ticket number.
 * @see _SetTicketNumber(), _ClearTicketNumber()
 */
uint32
BJob::TicketNumber() const
{
	return fTicketNumber;
}


/**
 * @brief Assign a queue ticket number to this job.
 *
 * Called by the JobQueue when the job is inserted. Should not be called
 * directly by application code.
 *
 * @param ticketNumber The sequential ticket number to assign.
 */
void
BJob::_SetTicketNumber(uint32 ticketNumber)
{
	fTicketNumber = ticketNumber;
}


/**
 * @brief Reset the ticket number to the sentinel value 0xFFFFFFFF.
 *
 * Called by the JobQueue when the job is removed. Should not be called
 * directly by application code.
 */
void
BJob::_ClearTicketNumber()
{
	fTicketNumber = 0xFFFFFFFFUL;
}


/**
 * @brief Set the human-readable error description for this job.
 *
 * Typically called from within Execute() or Cleanup() to provide context
 * about a failure before returning a non-B_OK status.
 *
 * @param error A string describing what went wrong.
 */
void
BJob::SetErrorString(const BString& error)
{
	fErrorString = error;
}


/**
 * @brief Execute the job and drive the full state-machine lifecycle.
 *
 * Transitions the job through the states WAITING_TO_RUN -> STARTED ->
 * IN_PROGRESS -> SUCCEEDED / FAILED / ABORTED, notifying all registered
 * BJobStateListener observers after the STARTED and final transitions.
 * Calls the overridable Execute() method to perform the actual work and
 * Cleanup() afterwards regardless of the result.
 *
 * @return The status_t returned by Execute(): B_OK on success, B_CANCELED if
 *         aborted, or another error code on failure. Returns B_NOT_ALLOWED if
 *         the job is not currently in B_JOB_STATE_WAITING_TO_RUN.
 * @see Execute(), Cleanup(), NotifyStateListeners()
 */
status_t
BJob::Run()
{
	if (fState != B_JOB_STATE_WAITING_TO_RUN)
		return B_NOT_ALLOWED;

	fState = B_JOB_STATE_STARTED;
	NotifyStateListeners();

	fState = B_JOB_STATE_IN_PROGRESS;
	fResult = Execute();
	Cleanup(fResult);

	fState = fResult == B_OK
		? B_JOB_STATE_SUCCEEDED
		: fResult == B_CANCELED
			? B_JOB_STATE_ABORTED
			: B_JOB_STATE_FAILED;
	NotifyStateListeners();

	return fResult;
}


/**
 * @brief Perform any post-execution cleanup for this job.
 *
 * Called by Run() immediately after Execute() returns, regardless of success
 * or failure. The default implementation is a no-op. Subclasses may override
 * to release resources allocated during Execute().
 *
 * @param jobResult The status_t returned by Execute().
 */
void
BJob::Cleanup(status_t /*jobResult*/)
{
}


/**
 * @brief Register \a listener to receive state-change notifications from this job.
 *
 * The listener will receive calls to JobStarted(), JobProgress(),
 * JobSucceeded(), JobFailed(), or JobAborted() as the job transitions states.
 * The same listener may not be added twice.
 *
 * @param listener The observer to register. Must remain valid until it is
 *                 removed or the job is destroyed.
 * @return B_OK on success, or B_ERROR if the internal list allocation fails.
 */
status_t
BJob::AddStateListener(BJobStateListener* listener)
{
	return fStateListeners.AddItem(listener) ? B_OK : B_ERROR;
}


/**
 * @brief Unregister \a listener so it no longer receives notifications.
 *
 * @param listener The observer to remove.
 * @return B_OK if the listener was found and removed, or B_ERROR if it was
 *         not registered.
 */
status_t
BJob::RemoveStateListener(BJobStateListener* listener)
{
	return fStateListeners.RemoveItem(listener) ? B_OK : B_ERROR;
}


/**
 * @brief Declare that this job depends on \a job.
 *
 * This job will not be considered runnable until \a job has succeeded. The
 * relationship is bidirectional: this job records \a job in its dependency
 * list, and \a job records this job in its dependant-jobs list.
 *
 * @param job The prerequisite job. Must already exist and must not be the same
 *            as this job.
 * @return B_OK on success, or B_ERROR if the dependency is already present or
 *         if the internal list operations fail.
 */
status_t
BJob::AddDependency(BJob* job)
{
	if (fDependencies.HasItem(job))
		return B_ERROR;

	if (fDependencies.AddItem(job) && job->fDependantJobs.AddItem(this))
		return B_OK;

	return B_ERROR;
}


/**
 * @brief Remove a previously declared dependency on \a job.
 *
 * Updates both the dependency list of this job and the dependant-jobs list of
 * \a job.
 *
 * @param job The prerequisite job to remove.
 * @return B_OK on success, or B_ERROR if \a job is not a registered dependency.
 */
status_t
BJob::RemoveDependency(BJob* job)
{
	if (!fDependencies.HasItem(job))
		return B_ERROR;

	if (fDependencies.RemoveItem(job) && job->fDependantJobs.RemoveItem(this))
		return B_OK;

	return B_ERROR;
}


/**
 * @brief Test whether all dependencies of this job have been satisfied.
 *
 * A job is runnable when its dependency list is empty (all prerequisites have
 * completed successfully and been removed via RemoveDependency()).
 *
 * @return true if the dependency list is empty, false otherwise.
 */
bool
BJob::IsRunnable() const
{
	return fDependencies.IsEmpty();
}


/**
 * @brief Return the number of outstanding dependencies.
 *
 * @return The count of prerequisite jobs that have not yet completed.
 */
int32
BJob::CountDependencies() const
{
	return fDependencies.CountItems();
}


/**
 * @brief Return the job that depends on this job at position \a index.
 *
 * Returns entries from the dependant-jobs list (jobs that listed this job as a
 * dependency), not from the dependencies list.
 *
 * @param index Zero-based index into the dependant-jobs list.
 * @return A pointer to the dependant BJob, or NULL if \a index is out of range.
 */
BJob*
BJob::DependantJobAt(int32 index) const
{
	return fDependantJobs.ItemAt(index);
}


/**
 * @brief Forcibly set the job's state without triggering notifications.
 *
 * Intended for use by the JobQueue infrastructure to override state during
 * dependency-failure cascades. Application code should generally not call this
 * directly.
 *
 * @param state The new BJobState value to assign.
 */
void
BJob::SetState(BJobState state)
{
	fState = state;
}


/**
 * @brief Notify all registered state listeners of the current state.
 *
 * Dispatches the appropriate virtual method on each BJobStateListener based on
 * the current value of fState. Called automatically by Run() after the STARTED
 * and terminal state transitions.
 */
void
BJob::NotifyStateListeners()
{
	int32 count = fStateListeners.CountItems();
	for (int i = 0; i < count; ++i) {
		BJobStateListener* listener = fStateListeners.ItemAt(i);
		if (listener == NULL)
			continue;
		switch (fState) {
			case B_JOB_STATE_STARTED:
				listener->JobStarted(this);
				break;
			case B_JOB_STATE_IN_PROGRESS:
				listener->JobProgress(this);
				break;
			case B_JOB_STATE_SUCCEEDED:
				listener->JobSucceeded(this);
				break;
			case B_JOB_STATE_FAILED:
				listener->JobFailed(this);
				break;
			case B_JOB_STATE_ABORTED:
				listener->JobAborted(this);
				break;
			default:
				break;
		}
	}
}


}	// namespace BPackageKit
