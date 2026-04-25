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
 *   Copyright 2012-2014, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Worker.cpp
 * @brief Asynchronous job queue and worker thread used throughout the debugger kit.
 *
 * Implements the JobKey, SimpleJobKey, JobListener, Job, and Worker classes
 * that schedule and run debugger background tasks (symbol loading, stack
 * walking, type resolution, etc.) on a dedicated worker thread. Jobs may
 * declare dependencies on other jobs or block waiting for user input; the
 * Worker takes care of state transitions, abort propagation, and listener
 * notification.
 *
 * @see Job, JobListener
 */


#include "Worker.h"

#include <AutoDeleter.h>
#include <AutoLocker.h>


// pragma mark - JobKey


/** @brief Virtual destructor anchor for the abstract job-key base type. */
JobKey::~JobKey()
{
}


// pragma mark - SimpleJobKey


/**
 * @brief Construct a key from an arbitrary object pointer and a caller-defined type.
 *
 * @param object  Opaque object pointer that, together with @a type, identifies the job.
 * @param type    Caller-assigned type tag distinguishing kinds of jobs on the same object.
 */
SimpleJobKey::SimpleJobKey(const void* object, uint32 type)
	:
	object(object),
	type(type)
{
}


/**
 * @brief Copy-construct a SimpleJobKey from another instance.
 *
 * @param other  Key whose @c object and @c type are copied.
 */
SimpleJobKey::SimpleJobKey(const SimpleJobKey& other)
	:
	object(other.object),
	type(other.type)
{
}


/**
 * @brief Compute a hash combining the object pointer and the type tag.
 *
 * @return A hash usable in BOpenHashTable-based job indexes.
 */
size_t
SimpleJobKey::HashValue() const
{
	return (size_t)(addr_t)object ^ (size_t)type;
}


/**
 * @brief Compare against another JobKey.
 *
 * @param other  Polymorphic key to compare with.
 * @return true if @a other is a SimpleJobKey with matching object and type.
 */
bool
SimpleJobKey::operator==(const JobKey& other) const
{
	const SimpleJobKey* otherKey = dynamic_cast<const SimpleJobKey*>(&other);
	return otherKey != NULL && object == otherKey->object
		&& type == otherKey->type;
}


/**
 * @brief Copy-assign from another SimpleJobKey.
 *
 * @param other  Source key.
 * @return Reference to *this.
 */
SimpleJobKey&
SimpleJobKey::operator=(const SimpleJobKey& other)
{
	object = other.object;
	type = other.type;
	return *this;
}


// #pragma mark - JobListener


/** @brief Virtual destructor anchor for the JobListener interface. */
JobListener::~JobListener()
{
}


/**
 * @brief Notification hook fired when a job transitions to the active state.
 *
 * Default implementation is a no-op; subclasses override as needed.
 *
 * @param job  Job that just started running.
 */
void
JobListener::JobStarted(Job* job)
{
}


/**
 * @brief Notification hook fired when a job completes successfully.
 *
 * @param job  Job that finished with JOB_STATE_SUCCEEDED.
 */
void
JobListener::JobDone(Job* job)
{
}


/**
 * @brief Notification hook fired when a job suspends pending user input.
 *
 * @param job  Job that is now waiting for input.
 */
void
JobListener::JobWaitingForInput(Job* job)
{
}


/**
 * @brief Notification hook fired when a job ends in failure.
 *
 * @param job  Job that finished with JOB_STATE_FAILED.
 */
void
JobListener::JobFailed(Job* job)
{
}


/**
 * @brief Notification hook fired when a job is aborted.
 *
 * @param job  Job that ended in JOB_STATE_ABORTED.
 */
void
JobListener::JobAborted(Job* job)
{
}


// #pragma mark - Job


/**
 * @brief Construct a job in the unscheduled state with no worker or listeners.
 */
Job::Job()
	:
	fWorker(NULL),
	fState(JOB_STATE_UNSCHEDULED),
	fDependency(NULL),
	fWaitStatus(JOB_DEPENDENCY_NOT_FOUND),
	fListeners(10)
{
}


/** @brief Virtual destructor anchor. */
Job::~Job()
{
}


/**
 * @brief Block this job until another job identified by @a key reaches a terminal state.
 *
 * @param key  Identifier of the job to wait on.
 * @return Wait status describing how the dependency resolved.
 */
job_wait_status
Job::WaitFor(const JobKey& key)
{
	return fWorker->WaitForJob(this, key);
}


/**
 * @brief Suspend this job pending input from the user interface layer.
 *
 * @return B_OK once input has been provided, or an error code if the worker
 *         is shutting down.
 */
status_t
Job::WaitForUserInput()
{
	return fWorker->WaitForUserInput(this);
}


/**
 * @brief Set the human-readable job description from a printf-style format.
 *
 * @param format  printf-style format string; remaining arguments fill the
 *                placeholders. The result is stored in fDescription.
 */
void
Job::SetDescription(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	fDescription.SetToFormatVarArgs(format, args);
	va_end(args);
}


/**
 * @brief Associate this job with the worker that owns it.
 *
 * @param worker  Worker that will execute the job.
 */
void
Job::SetWorker(Worker* worker)
{
	fWorker = worker;
}


/**
 * @brief Update the lifecycle state of this job.
 *
 * @param state  New state value.
 */
void
Job::SetState(job_state state)
{
	fState = state;
}


/**
 * @brief Record a dependency on another job.
 *
 * @param job  Job that this job is currently waiting on, or NULL to clear.
 */
void
Job::SetDependency(Job* job)
{
	fDependency = job;
}


/**
 * @brief Update the wait status and synchronize the lifecycle state accordingly.
 *
 * Active dependencies and user-input waits transition the job to
 * JOB_STATE_WAITING; everything else marks it active.
 *
 * @param status  New wait status.
 */
void
Job::SetWaitStatus(job_wait_status status)
{
	fWaitStatus = status;
	switch (fWaitStatus) {
		case JOB_DEPENDENCY_ACTIVE:
		case JOB_USER_INPUT_WAITING:
			fState = JOB_STATE_WAITING;
			break;
		default:
			fState = JOB_STATE_ACTIVE;
			break;
	}
}


/**
 * @brief Register a listener for state transitions on this job.
 *
 * @param listener  Listener to add. Ownership is not transferred.
 * @retval B_OK         Listener registered.
 * @retval B_NO_MEMORY  Out of memory.
 */
status_t
Job::AddListener(JobListener* listener)
{
	return fListeners.AddItem(listener) ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Remove a previously registered listener.
 *
 * @param listener  Listener to remove. Silently no-ops if not registered.
 */
void
Job::RemoveListener(JobListener* listener)
{
	fListeners.RemoveItem(listener);
}


/**
 * @brief Dispatch the appropriate notification for the current state to all listeners.
 *
 * Iterates listeners in reverse order so that listeners that detach during
 * the callback do not skip later listeners.
 */
void
Job::NotifyListeners()
{
	int32 count = fListeners.CountItems();
	for (int32 i = count - 1; i >= 0; i--) {
		JobListener* listener = fListeners.ItemAt(i);
		switch (fState) {
			case JOB_STATE_ACTIVE:
				listener->JobStarted(this);
				break;
			case JOB_STATE_WAITING:
				if (fWaitStatus == JOB_USER_INPUT_WAITING)
					listener->JobWaitingForInput(this);
				break;
			case JOB_STATE_SUCCEEDED:
				listener->JobDone(this);
				break;
			case JOB_STATE_FAILED:
				listener->JobFailed(this);
				break;
			case JOB_STATE_ABORTED:
			default:
				listener->JobAborted(this);
				break;
		}
	}
}


// #pragma mark - Worker


/**
 * @brief Construct a worker with no thread or jobs yet.
 *
 * Init() must be called before the worker can accept jobs.
 */
Worker::Worker()
	:
	fLock("worker"),
	fWorkerThread(-1),
	fTerminating(false)
{
}


/**
 * @brief Shut the worker down and wait for the worker thread to exit.
 *
 * Calls ShutDown() and joins the worker thread so that no jobs continue
 * running after destruction.
 */
Worker::~Worker()
{
	ShutDown();

	if (fWorkerThread >= 0)
		wait_for_thread(fWorkerThread, NULL);
}


/**
 * @brief Bring the worker online: lock, job table, semaphore, and worker thread.
 *
 * @return Status code from the first failing initialization step.
 * @retval B_OK  The worker is ready to accept ScheduleJob() calls.
 */
status_t
Worker::Init()
{
	// check lock
	status_t error = fLock.InitCheck();
	if (error != B_OK)
		return error;

	// init jobs table
	error = fJobs.Init();
	if (error != B_OK)
		return error;

	// create semaphore for the worker
	fWorkToDoSem = create_sem(0, "work to do");
	if (fWorkToDoSem < 0)
		return fWorkToDoSem;

	// spawn worker thread
	fWorkerThread = spawn_thread(_WorkerLoopEntry, "worker", B_NORMAL_PRIORITY,
		this);
	if (fWorkerThread < 0)
		return fWorkerThread;

	resume_thread(fWorkerThread);

	return B_OK;
}


/**
 * @brief Abort all pending jobs and signal the worker thread to terminate.
 *
 * Idempotent: subsequent calls have no effect once @c fTerminating is set.
 * The worker thread itself is not joined here; the destructor does that.
 */
void
Worker::ShutDown()
{
	AutoLocker<Worker> locker(this);

	if (fTerminating)
		return;

	fTerminating = true;

	// abort all jobs
	Job* job = fJobs.Clear(true);
	while (job != NULL) {
		Job* nextJob = job->fNext;
		_AbortJob(job, false);
		job = nextJob;

	}

	// let the work thread terminate
	delete_sem(fWorkToDoSem);
	fWorkToDoSem = -1;
}


/**
 * @brief Submit a job for asynchronous execution by the worker thread.
 *
 * Optionally registers @a listener for callbacks on @a job before queuing
 * the job. Releases the work-to-do semaphore so the worker thread wakes
 * up if it was idle.
 *
 * @param job       Job to enqueue. The worker takes a reference; on success
 *                  the original reference held by the caller is consumed.
 * @param listener  Optional listener attached prior to scheduling.
 * @retval B_OK         Job scheduled.
 * @retval B_NO_MEMORY  @a job is NULL or the listener could not be added.
 * @retval B_ERROR      The worker is shutting down.
 */
status_t
Worker::ScheduleJob(Job* job, JobListener* listener)
{
	if (job == NULL)
		return B_NO_MEMORY;

	BReference<Job> jobReference(job, true);
	AutoLocker<Worker> locker(this);

	if (fTerminating)
		return B_ERROR;

	if (listener != NULL) {
		status_t error = job->AddListener(listener);
		if (error != B_OK)
			return error;
	}

	bool notify = fUnscheduledJobs.IsEmpty() && fAbortedJobs.IsEmpty();

	job->SetWorker(this);
	job->SetState(JOB_STATE_UNSCHEDULED);
	fJobs.Insert(job);
	fUnscheduledJobs.Add(jobReference.Detach());

	if (notify)
		release_sem(fWorkToDoSem);

	return B_OK;
}


/**
 * @brief Abort the job identified by @a key, if known to the worker.
 *
 * Removes it from the active set; the worker thread finalizes the abort
 * asynchronously via _FinishJob().
 *
 * @param key  Identifier of the job to abort. Silently ignored when no
 *             matching job is registered.
 */
void
Worker::AbortJob(const JobKey& key)
{
	AutoLocker<Worker> locker(this);

	Job* job = fJobs.Lookup(key);
	if (job == NULL)
		return;

	_AbortJob(job, true);
}


/**
 * @brief Look up a job by key.
 *
 * @param key  Identifier of the job to retrieve.
 * @return The matching job pointer, or NULL if no such job exists.
 * @note  The pointer is valid only while the worker remains locked by
 *        the caller.
 */
Job*
Worker::GetJob(const JobKey& key)
{
	AutoLocker<Worker> locker(this);
	return fJobs.Lookup(key);
}


/**
 * @brief Resume a job that previously suspended waiting for user input.
 *
 * Moves the job from the suspended list back onto the unscheduled list so
 * the worker thread will pick it up.
 *
 * @param job  Job to resume.
 * @retval B_OK               Job moved back to the run queue.
 * @retval B_ENTRY_NOT_FOUND  @a job is not currently suspended.
 */
status_t
Worker::ResumeJob(Job* job)
{
	AutoLocker<Worker> locker(this);

	for (JobList::Iterator it = fSuspendedJobs.GetIterator(); it.Next();) {
		if (it.Current() == job) {
			it.Remove();
			job->SetState(JOB_STATE_UNSCHEDULED);
			fUnscheduledJobs.Add(job);
			release_sem(fWorkToDoSem);
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Test whether the worker has at least one job registered.
 *
 * @return true if any jobs are still tracked (running, waiting, or queued).
 */
bool
Worker::HasPendingJobs()
{
	AutoLocker<Worker> locker(this);
	return !fJobs.IsEmpty();
}


/**
 * @brief Add a listener to a job identified by @a key.
 *
 * @param key       Identifier of the target job.
 * @param listener  Listener to attach. Ownership is not transferred.
 * @retval B_OK               Listener attached.
 * @retval B_ENTRY_NOT_FOUND  No such job is registered.
 * @retval B_NO_MEMORY        Could not store the listener.
 */
status_t
Worker::AddListener(const JobKey& key, JobListener* listener)
{
	AutoLocker<Worker> locker(this);

	Job* job = fJobs.Lookup(key);
	if (job == NULL)
		return B_ENTRY_NOT_FOUND;

	return job->AddListener(listener);
}


/**
 * @brief Remove a listener from a job identified by @a key.
 *
 * @param key       Identifier of the target job; ignored if not found.
 * @param listener  Listener to detach.
 */
void
Worker::RemoveListener(const JobKey& key, JobListener* listener)
{
	AutoLocker<Worker> locker(this);

	if (Job* job = fJobs.Lookup(key))
		job->RemoveListener(listener);
}


/**
 * @brief Wire a wait-for-other-job dependency for the currently executing job.
 *
 * Detects the case where @a waitingJob is already waiting on the matching
 * job, validates against self-dependencies and double waits, and otherwise
 * registers @a waitingJob on the dependency's dependents list.
 *
 * @param waitingJob  Job that is about to block.
 * @param key         Identifier of the dependency.
 * @return The wait status describing how the dependency resolved.
 */
job_wait_status
Worker::WaitForJob(Job* waitingJob, const JobKey& key)
{
	AutoLocker<Worker> locker(this);

	// don't wait when the game is over anyway
	if (fTerminating || waitingJob->State() == JOB_STATE_ABORTED)
		return JOB_DEPENDENCY_ABORTED;

	Job* job = fJobs.Lookup(key);
	if (job == NULL)
		return JOB_DEPENDENCY_NOT_FOUND;

	if (waitingJob->Dependency() == job)
		return waitingJob->WaitStatus();
	if (job == waitingJob)
		debugger("Jobs can't depend on themselves");
	if (waitingJob->Dependency() != NULL)
		debugger("Job already has a dependency");

	waitingJob->SetWaitStatus(JOB_DEPENDENCY_ACTIVE);
	waitingJob->SetDependency(job);
	job->DependentJobs().Add(waitingJob);

	return waitingJob->WaitStatus();
}


/**
 * @brief Suspend @a waitingJob until external user input is supplied.
 *
 * Marks the job as JOB_USER_INPUT_WAITING, notifies its listeners, and parks
 * it on the suspended list so it is excluded from the run queue.
 *
 * @param waitingJob  Job that is about to wait.
 * @retval B_OK            Job suspended successfully.
 * @retval B_INTERRUPTED   The worker is shutting down or the job is aborted.
 */
status_t
Worker::WaitForUserInput(Job* waitingJob)
{
	AutoLocker<Worker> locker(this);

	if (fTerminating || waitingJob->State() == JOB_STATE_ABORTED)
		return B_INTERRUPTED;

	waitingJob->SetWaitStatus(JOB_USER_INPUT_WAITING);
	waitingJob->NotifyListeners();
	fSuspendedJobs.Add(waitingJob);

	return B_OK;
}


/**
 * @brief Worker thread entry trampoline that forwards into _WorkerLoop().
 *
 * @param data  Pointer to the owning Worker, cast to void*.
 * @return The result of the worker loop.
 */
/*static*/ status_t
Worker::_WorkerLoopEntry(void* data)
{
	return ((Worker*)data)->_WorkerLoop();
}


/**
 * @brief Main loop body run on the worker thread.
 *
 * Drives _ProcessJobs() until shutdown, then drains any final aborted jobs.
 *
 * @return Always B_OK.
 */
status_t
Worker::_WorkerLoop()
{
	_ProcessJobs();

	// clean up aborted jobs
	AutoLocker<Worker> locker(this);
	while (Job* job = fAbortedJobs.RemoveHead())
		_FinishJob(job);

	return B_OK;
}


/**
 * @brief Pop jobs from the run queue and execute them one at a time.
 *
 * Cleans up aborted jobs first, then dispatches one unscheduled job per
 * iteration. Handles the JOB_STATE_WAITING transition so that jobs blocked
 * on dependencies or user input do not finalize prematurely.
 */
void
Worker::_ProcessJobs()
{
	while (true) {
		AutoLocker<Worker> locker(this);

		// wait for next job
		if (fUnscheduledJobs.IsEmpty() && fAbortedJobs.IsEmpty()) {
			locker.Unlock();

			status_t error = acquire_sem(fWorkToDoSem);
			if (error != B_OK) {
				if (error == B_INTERRUPTED) {
					locker.Lock();
					continue;
				}
				break;
			}

			locker.Lock();
		}

		// clean up aborted jobs
		while (Job* job = fAbortedJobs.RemoveHead())
			_FinishJob(job);

		// process the next job
		if (Job* job = fUnscheduledJobs.RemoveHead()) {
			job->SetState(JOB_STATE_ACTIVE);
			job->NotifyListeners();

			locker.Unlock();
			status_t error = job->Do();
			locker.Lock();

			if (job->State() == JOB_STATE_ACTIVE) {
				job->SetState(
					error == B_OK ? JOB_STATE_SUCCEEDED : JOB_STATE_FAILED);
			} else if (job->State() == JOB_STATE_WAITING)
				continue;

			_FinishJob(job);
		}
	}
}


/**
 * @brief Internal helper that moves a job into JOB_STATE_ABORTED.
 *
 * Removes it from whichever list it currently lives on and, if requested,
 * also removes it from the lookup table.
 *
 * @param job             Job to abort. Must be locked by the caller.
 * @param removeFromTable If true, also remove the job from the lookup table.
 */
void
Worker::_AbortJob(Job* job, bool removeFromTable)
{
	switch (job->State()) {
		case JOB_STATE_ABORTED:
			return;

		case JOB_STATE_UNSCHEDULED:
			fUnscheduledJobs.Remove(job);
			fAbortedJobs.Add(job);
			break;

		case JOB_STATE_WAITING:
		{
			Job* dependency = job->Dependency();
			if (dependency != NULL)
				dependency->DependentJobs().Remove(job);
			job->SetDependency(NULL);
			break;
		}
		case JOB_STATE_ACTIVE:
		case JOB_STATE_FAILED:
		case JOB_STATE_SUCCEEDED:
		default:
			break;
	}

	job->SetState(JOB_STATE_ABORTED);
	if (removeFromTable)
		fJobs.Remove(job);
}


/**
 * @brief Finalize a job that has reached a terminal state.
 *
 * Wakes any jobs that depend on @a job, removes it from the lookup table
 * (unless it was already aborted earlier), notifies listeners, and releases
 * the worker's reference to the job.
 *
 * @param job  Job to finalize.
 */
void
Worker::_FinishJob(Job* job)
{
	// wake up dependent jobs
	if (!job->DependentJobs().IsEmpty()) {
		job_wait_status waitStatus;
		switch (job->State()) {
			case JOB_STATE_ABORTED:
				waitStatus = JOB_DEPENDENCY_ABORTED;
				break;
			case JOB_STATE_FAILED:
				waitStatus = JOB_DEPENDENCY_FAILED;
				break;
			case JOB_STATE_SUCCEEDED:
				waitStatus = JOB_DEPENDENCY_SUCCEEDED;
				break;

			case JOB_STATE_UNSCHEDULED:
			case JOB_STATE_WAITING:
			case JOB_STATE_ACTIVE:
			default:
				// should never happen
				waitStatus = JOB_DEPENDENCY_NOT_FOUND;
				break;
		}

		while (Job* dependentJob = job->DependentJobs().RemoveHead()) {
			dependentJob->SetDependency(NULL);
			dependentJob->SetWaitStatus(waitStatus);
			fUnscheduledJobs.Add(dependentJob);
		}

		release_sem(fWorkToDoSem);
	}

	if (job->State() != JOB_STATE_ABORTED)
		fJobs.Remove(job);
	job->NotifyListeners();
	job->ReleaseReference();
}
