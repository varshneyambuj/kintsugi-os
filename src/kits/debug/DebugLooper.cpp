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
 *   Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DebugLooper.cpp
 * @brief Event loop that multiplexes debug messages from multiple BTeamDebuggers.
 *
 * BDebugLooper runs an event loop (either on a dedicated spawned thread or on
 * the caller's thread) that monitors the debugger ports of all registered
 * BTeamDebugger instances. Incoming debug messages are dispatched to the
 * associated BDebugMessageHandler. Debuggers can be added or removed safely
 * from any thread via an internal job queue.
 *
 * @see BTeamDebugger, BDebugMessageHandler
 */


#include <DebugLooper.h>

#include <new>

#include <AutoLocker.h>
#include <DebugMessageHandler.h>
#include <TeamDebugger.h>
#include <util/DoublyLinkedList.h>


/**
 * @brief Internal pairing of a BTeamDebugger with its message handler.
 */
struct BDebugLooper::Debugger {
	BTeamDebugger*			debugger;
	BDebugMessageHandler*	handler;

	/**
	 * @brief Construct a Debugger pairing.
	 *
	 * @param debugger  The BTeamDebugger whose port will be monitored.
	 * @param handler   The handler to dispatch messages to.
	 */
	Debugger(BTeamDebugger* debugger, BDebugMessageHandler* handler)
		:
		debugger(debugger),
		handler(handler)
	{
	}
};


/**
 * @brief Base class for work items posted to the looper thread's job queue.
 *
 * Callers waiting for a job to complete block on a semaphore created inside
 * Wait(); the looper thread calls Done() to unblock the waiter after Do() runs.
 */
struct BDebugLooper::Job : DoublyLinkedListLinkImpl<Job> {
	/**
	 * @brief Construct an empty Job with no semaphore yet allocated.
	 */
	Job()
		:
		fDoneSemaphore(-1)
	{
	}

	/**
	 * @brief Destructor — no resources to release at this level.
	 */
	virtual ~Job()
	{
	}

	/**
	 * @brief Block the calling thread until the looper executes this job.
	 *
	 * Creates a semaphore, releases @a lock, waits for Done() to post, then
	 * re-acquires @a lock before returning.
	 *
	 * @param lock  The looper's BLocker, which must be held on entry and will
	 *              be re-acquired before return.
	 * @return The status_t value passed to Done() by the looper.
	 */
	status_t Wait(BLocker& lock)
	{
		fDoneSemaphore = create_sem(0, "debug looper job");

		lock.Unlock();

		while (acquire_sem(fDoneSemaphore) == B_INTERRUPTED) {
		}

		lock.Lock();

		delete_sem(fDoneSemaphore);
		fDoneSemaphore = -1;

		return fResult;
	}

	/**
	 * @brief Signal completion to a thread blocked in Wait().
	 *
	 * @param result  The status_t to return from Wait().
	 */
	void Done(status_t result)
	{
		fResult = result;
		release_sem(fDoneSemaphore);
	}

	/**
	 * @brief Execute the job on the looper thread.
	 *
	 * @param looper  The owning BDebugLooper.
	 * @return B_OK on success, or an error code on failure.
	 */
	virtual status_t Do(BDebugLooper* looper) = 0;

protected:
	sem_id			fDoneSemaphore;  /**< Semaphore used for caller synchronisation. */
	status_t		fResult;         /**< Result code stored by Done() for Wait(). */
};


/** @brief Intrusive doubly-linked list type used for the pending job queue. */
struct BDebugLooper::JobList : DoublyLinkedList<Job> {
};


/**
 * @brief Job that registers a new BTeamDebugger with the looper.
 */
struct BDebugLooper::AddDebuggerJob : Job {
	/**
	 * @brief Construct an AddDebuggerJob.
	 *
	 * @param debugger  The debugger to register.
	 * @param handler   The message handler for the debugger.
	 */
	AddDebuggerJob(BTeamDebugger* debugger,
		BDebugMessageHandler* handler)
		:
		fDebugger(debugger),
		fHandler(handler)
	{
	}

	/**
	 * @brief Execute the registration on the looper thread.
	 *
	 * @param looper  The owning BDebugLooper.
	 * @return B_OK on success, B_NO_MEMORY if allocation fails.
	 */
	virtual status_t Do(BDebugLooper* looper)
	{
		Debugger* debugger = new(std::nothrow) Debugger(fDebugger, fHandler);
		if (debugger == NULL || !looper->fDebuggers.AddItem(debugger)) {
			delete debugger;
			return B_NO_MEMORY;
		}

		return B_OK;
	}

private:
	BTeamDebugger*			fDebugger;
	BDebugMessageHandler*	fHandler;
};


/**
 * @brief Job that unregisters a BTeamDebugger identified by team ID.
 */
struct BDebugLooper::RemoveDebuggerJob : Job {
	/**
	 * @brief Construct a RemoveDebuggerJob.
	 *
	 * @param team  Team ID of the debugger to remove.
	 */
	RemoveDebuggerJob(team_id team)
		:
		fTeam(team)
	{
	}

	/**
	 * @brief Execute the removal on the looper thread.
	 *
	 * @param looper  The owning BDebugLooper.
	 * @return B_OK if found and removed, B_ENTRY_NOT_FOUND otherwise.
	 */
	virtual status_t Do(BDebugLooper* looper)
	{
		for (int32 i = 0; Debugger* debugger = looper->fDebuggers.ItemAt(i);
				i++) {
			if (debugger->debugger->Team() == fTeam) {
				delete looper->fDebuggers.RemoveItemAt(i);
				return B_OK;
			}
		}

		return B_ENTRY_NOT_FOUND;
	}

private:
	team_id	fTeam;
};


// #pragma mark -


/**
 * @brief Default constructor — allocates all fields but does not start running.
 *
 * Call Init() and then Run() to prepare the looper for operation.
 */
BDebugLooper::BDebugLooper()
	:
	fLock("debug looper"),
	fThread(-1),
	fOwnsThread(false),
	fTerminating(false),
	fNotified(false),
	fJobs(NULL),
	fEventSemaphore(-1)
{
}


/**
 * @brief Destructor — does not stop a running looper thread.
 *
 * Callers should call Quit() and wait for the thread to exit before destroying
 * the object.
 */
BDebugLooper::~BDebugLooper()
{
}


/**
 * @brief Prepare the looper's internal data structures for use.
 *
 * Creates the job list and the event semaphore used to wake the looper when
 * new jobs arrive or Quit() is called. Must be called before Run().
 *
 * @return B_OK on success; B_BAD_VALUE if a thread is already running;
 *         B_NO_MEMORY if the job list allocation fails; or a semaphore-
 *         creation error.
 */
status_t
BDebugLooper::Init()
{
	status_t error = fLock.InitCheck();
	if (error != B_OK)
		return error;

	AutoLocker<BLocker> locker(fLock);

	if (fThread >= 0)
		return B_BAD_VALUE;

	if (fJobs == NULL) {
		fJobs = new(std::nothrow) JobList;
		if (fJobs == NULL)
			return B_NO_MEMORY;
	}

	if (fEventSemaphore < 0) {
		fEventSemaphore = create_sem(0, "debug looper event");
		if (fEventSemaphore < 0)
			return fEventSemaphore;
	}

	return B_OK;
}


/**
 * @brief Start the looper event loop, either on a new thread or inline.
 *
 * If @a spawnThread is true, spawns a new B_NORMAL_PRIORITY thread and returns
 * immediately. If false, the caller's thread enters _MessageLoop() and blocks
 * until Quit() is called.
 *
 * @param spawnThread  True to run on a dedicated thread; false to run inline.
 * @return B_OK (or a spawned thread ID) on success; B_BAD_VALUE if already
 *         running; a negative spawn_thread error if thread creation fails.
 */
thread_id
BDebugLooper::Run(bool spawnThread)
{
	AutoLocker<BLocker> locker(fLock);

	if (fThread >= 0)
		return B_BAD_VALUE;

	fNotified = false;

	if (spawnThread) {
		fThread = spawn_thread(&_MessageLoopEntry, "debug looper",
			B_NORMAL_PRIORITY, this);
		if (fThread < 0)
			return fThread;

		fOwnsThread = true;

		resume_thread(fThread);
		return B_OK;
	}

	fThread = find_thread(NULL);
	fOwnsThread = false;

	_MessageLoop();
	return B_OK;
}


/**
 * @brief Request that the looper exit its event loop.
 *
 * Sets the termination flag and wakes the looper thread via the event
 * semaphore. Returns immediately; the caller must wait for the thread if
 * synchronisation is required.
 */
void
BDebugLooper::Quit()
{
	AutoLocker<BLocker> locker(fLock);

	fTerminating = true;
	_Notify();
}


/**
 * @brief Register a BTeamDebugger and its handler with the looper.
 *
 * If called from a thread other than the looper thread, the registration is
 * posted as a job and the caller blocks until it completes.
 *
 * @param debugger  The BTeamDebugger to monitor.
 * @param handler   The handler that will receive dispatched debug messages.
 * @return B_OK on success, B_BAD_VALUE if either pointer is NULL, or
 *         B_NO_MEMORY if the internal Debugger record cannot be allocated.
 */
status_t
BDebugLooper::AddTeamDebugger(BTeamDebugger* debugger,
	BDebugMessageHandler* handler)
{
	if (debugger == NULL || handler == NULL)
		return B_BAD_VALUE;

	AddDebuggerJob job(debugger, handler);
	return _DoJob(&job);
}


/**
 * @brief Unregister a BTeamDebugger by pointer.
 *
 * @param debugger  The debugger to remove; must not be NULL.
 * @return true if the debugger was found and removed, false otherwise.
 */
bool
BDebugLooper::RemoveTeamDebugger(BTeamDebugger* debugger)
{
	if (debugger == NULL)
		return false;

	RemoveDebuggerJob job(debugger->Team());
	return _DoJob(&job) == B_OK;
}


/**
 * @brief Unregister the BTeamDebugger associated with the given team ID.
 *
 * @param team  The team ID whose debugger should be removed.
 * @return true if the debugger was found and removed, false otherwise.
 */
bool
BDebugLooper::RemoveTeamDebugger(team_id team)
{
	if (team < 0)
		return false;

	RemoveDebuggerJob job(team);
	return _DoJob(&job) == B_OK;
}


/**
 * @brief Thread entry-point for the dedicated looper thread.
 *
 * @param data  Pointer to the BDebugLooper instance.
 * @return The status_t returned by _MessageLoop().
 */
/*static*/ status_t
BDebugLooper::_MessageLoopEntry(void* data)
{
	return ((BDebugLooper*)data)->_MessageLoop();
}


/**
 * @brief Core event loop — waits for port messages or job notifications.
 *
 * Uses wait_for_objects() to simultaneously watch all registered debugger ports
 * and the fEventSemaphore. When a job arrives it is executed; when a port
 * message arrives it is dispatched to the associated BDebugMessageHandler.
 * Exits when fTerminating is true.
 *
 * @return B_OK when the loop terminates cleanly.
 */
status_t
BDebugLooper::_MessageLoop()
{
	while (true) {
		// prepare the wait info array
		int32 debuggerCount = fDebuggers.CountItems();
		object_wait_info waitInfos[debuggerCount + 1];

		for (int32 i = 0; i < debuggerCount; i++) {
			waitInfos[i].object
				= fDebuggers.ItemAt(i)->debugger->DebuggerPort();
			waitInfos[i].type = B_OBJECT_TYPE_PORT;
			waitInfos[i].events = B_EVENT_READ;
		}

		waitInfos[debuggerCount].object = fEventSemaphore;
		waitInfos[debuggerCount].type = B_OBJECT_TYPE_SEMAPHORE;
		waitInfos[debuggerCount].events = B_EVENT_ACQUIRE_SEMAPHORE;

		// wait for the next event
		wait_for_objects(waitInfos, debuggerCount + 1);

		AutoLocker<BLocker> locker(fLock);

		// handle all pending jobs
		bool handledJobs = fJobs->Head() != NULL;
		while (Job* job = fJobs->RemoveHead())
			job->Done(job->Do(this));

		// acquire notification semaphore and mark unnotified
		if ((waitInfos[debuggerCount].events & B_EVENT_ACQUIRE_SEMAPHORE) != 0)
			acquire_sem(fEventSemaphore);
		fNotified = false;

		if (fTerminating)
			return B_OK;

		// Always loop when jobs were executed, since that might add/remove
		// debuggers.
		if (handledJobs)
			continue;

		// read a pending port message
		for (int32 i = 0; i < debuggerCount; i++) {
			if ((waitInfos[i].events & B_EVENT_READ) != 0) {
				Debugger* debugger = fDebuggers.ItemAt(i);

				// read the message
				debug_debugger_message_data message;
				int32 code;
				ssize_t messageSize = read_port(
					debugger->debugger->DebuggerPort(), &code, &message,
					sizeof(message));
				if (messageSize < 0)
					continue;

				// handle the message
				bool continueThread = debugger->handler->HandleDebugMessage(
					code, message);

				// If requested, tell the thread to continue (only when there
				// is a thread and the message was synchronous).
				if (continueThread && message.origin.thread >= 0
						&& message.origin.nub_port >= 0) {
					debugger->debugger->ContinueThread(message.origin.thread);
				}

				// Handle only one message -- the hook might have added/removed
				// debuggers which makes further iteration problematic.
				break;
			}
		}
	}
}


/**
 * @brief Execute a job, either inline (if on the looper thread) or via the queue.
 *
 * If called from the looper thread (or before the looper is running), executes
 * @a job directly. Otherwise, appends it to fJobs, wakes the looper, and blocks
 * until the looper completes the job.
 *
 * @param job  The job to execute.
 * @return The status_t returned by job->Do().
 */
status_t
BDebugLooper::_DoJob(Job* job)
{
	AutoLocker<BLocker> locker(fLock);

	// execute directly, if in looper thread or not running yet
	if (fThread < 0 || fThread == find_thread(NULL))
		return job->Do(this);

	// execute in the looper thread
	fJobs->Add(job);
	_Notify();

	return job->Wait(fLock);
}


/**
 * @brief Post a single notification to the event semaphore.
 *
 * Idempotent: if fNotified is already true the semaphore is not posted again,
 * preventing spurious wake-ups from accumulating.
 */
void
BDebugLooper::_Notify()
{
	if (fNotified)
		return;

	fNotified = true;
	release_sem(fEventSemaphore);
}
