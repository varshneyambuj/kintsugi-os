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
 *   Copyright 2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file DPC.cpp
 *  @brief Deferred Procedure Call queues — kernel work pools at three priorities.
 *
 * DPC queues let interrupt handlers and other contexts that cannot block
 * post work to a dedicated worker thread. The kernel maintains three
 * built-in queues (normal, high, real-time priority); subsystems may also
 * spin up their own DPCQueue if they need a private worker. */


#include <DPC.h>

#include <util/AutoLock.h>


#define NORMAL_PRIORITY		B_NORMAL_PRIORITY
#define HIGH_PRIORITY		B_URGENT_DISPLAY_PRIORITY
#define REAL_TIME_PRIORITY	B_FIRST_REAL_TIME_PRIORITY

#define DEFAULT_QUEUE_SLOT_COUNT	64


static DPCQueue sNormalPriorityQueue;
static DPCQueue sHighPriorityQueue;
static DPCQueue sRealTimePriorityQueue;


// #pragma mark - FunctionDPCCallback


/**
 * @brief Construct a recyclable function callback owned by @a owner.
 * @param owner Queue whose free-list will reclaim this object, or NULL.
 */
FunctionDPCCallback::FunctionDPCCallback(DPCQueue* owner)
	:
	fOwner(owner)
{
}


/**
 * @brief Bind a C-style function pointer and argument to this callback.
 * @param function Function to invoke when the callback runs.
 * @param argument Opaque argument passed through to @a function.
 */
void
FunctionDPCCallback::SetTo(void (*function)(void*), void* argument)
{
	fFunction = function;
	fArgument = argument;
}


/**
 * @brief Invoke the bound function and return the callback to its owner.
 *
 * Called from the DPC worker thread. After the hook returns, the callback
 * recycles itself onto the owning queue's free list when present.
 *
 * @param queue Queue that dispatched this callback.
 */
void
FunctionDPCCallback::DoDPC(DPCQueue* queue)
{
	fFunction(fArgument);

	if (fOwner != NULL)
		fOwner->Recycle(this);
}


// #pragma mark - DPCCallback


/**
 * @brief Construct an unqueued DPC callback.
 */
DPCCallback::DPCCallback()
	:
	fInQueue(NULL)
{
}


/**
 * @brief Virtual destructor; derived callbacks clean up their own state.
 */
DPCCallback::~DPCCallback()
{
}


// #pragma mark - DPCQueue


/**
 * @brief Construct an empty, unstarted DPC queue.
 *
 * Initialises the spinlock and the pending-callbacks condition variable.
 * Call Init() to spawn the worker thread and begin accepting work.
 */
DPCQueue::DPCQueue()
	:
	fThreadID(-1),
	fCallbackInProgress(NULL),
	fCallbackDoneCondition(NULL)
{
	B_INITIALIZE_SPINLOCK(&fLock);

	fPendingCallbacksCondition.Init(this, "dpc queue");
}


/**
 * @brief Close the queue if still open and free reserved function callbacks.
 */
DPCQueue::~DPCQueue()
{
	// close, if not closed yet
	{
		InterruptsSpinLocker locker(fLock);
		if (!_IsClosed()) {
			locker.Unlock();
			Close(false);
		}
	}

	// delete function callbacks
	while (DPCCallback* callback = fUnusedFunctionCallbacks.RemoveHead())
		delete callback;
}


/**
 * @brief Return the shared default queue matching @a priority.
 * @param priority Requested thread priority for the target queue.
 * @return Normal, high, or real-time default queue pointer.
 */
/*static*/ DPCQueue*
DPCQueue::DefaultQueue(int priority)
{
	if (priority <= NORMAL_PRIORITY)
		return &sNormalPriorityQueue;

	if (priority <= HIGH_PRIORITY)
		return &sHighPriorityQueue;

	return &sRealTimePriorityQueue;
}


/**
 * @brief Pre-allocate function-callback slots and start the worker thread.
 *
 * @param name          Name used for the kernel worker thread.
 * @param priority      Scheduling priority of the worker thread.
 * @param reservedSlots Number of FunctionDPCCallback instances to pre-create
 *                      for use by the Add(function, argument) overload.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or a thread
 *         spawn error.
 */
status_t
DPCQueue::Init(const char* name, int32 priority, uint32 reservedSlots)
{
	// create function callbacks
	for (uint32 i = 0; i < reservedSlots; i++) {
		FunctionDPCCallback* callback
			= new(std::nothrow) FunctionDPCCallback(this);
		if (callback == NULL)
			return B_NO_MEMORY;

		fUnusedFunctionCallbacks.Add(callback);
	}

	// spawn the thread
	fThreadID = spawn_kernel_thread(&_ThreadEntry, name, priority, this);
	if (fThreadID < 0)
		return fThreadID;

	resume_thread(fThreadID);

	return B_OK;
}


/**
 * @brief Close the queue and wait for its worker thread to exit.
 *
 * Safe to call from thread context only — blocks until the worker finishes.
 *
 * @param cancelPending If true, drop still-queued callbacks instead of
 *                      running them.
 */
void
DPCQueue::Close(bool cancelPending)
{
	InterruptsSpinLocker locker(fLock);

	if (_IsClosed())
		return;

	// If requested, dequeue all pending callbacks
	if (cancelPending)
		fCallbacks.MakeEmpty();

	// mark the queue closed
	thread_id thread = fThreadID;
	fThreadID = -1;

	locker.Unlock();

	// wake up the thread and wait for it
	fPendingCallbacksCondition.NotifyAll();
	wait_for_thread(thread, NULL);
}


/**
 * @brief Enqueue a caller-supplied DPC callback.
 *
 * Safe to call from interrupt context.
 *
 * @param callback Callback object whose DoDPC() will be run on the worker.
 * @return B_OK on success, B_NOT_INITIALIZED if closed, EALREADY if @a
 *         callback is already on a queue.
 */
status_t
DPCQueue::Add(DPCCallback* callback)
{
	// queue the callback, if the queue isn't closed already
	InterruptsSpinLocker locker(fLock);

	if (_IsClosed())
		return B_NOT_INITIALIZED;

	if (callback->fInQueue != NULL)
		return EALREADY;

	bool wasEmpty = fCallbacks.IsEmpty();
	fCallbacks.Add(callback);
	callback->fInQueue = this;

	locker.Unlock();

	// notify the condition variable, if necessary
	if (wasEmpty)
		fPendingCallbacksCondition.NotifyAll();

	return B_OK;
}


/**
 * @brief Enqueue a plain function pointer using a reserved callback slot.
 *
 * Safe to call from interrupt context. Allocates no memory; fails with
 * B_NO_MEMORY when all reserved slots are exhausted.
 *
 * @param function Function to run on the worker thread.
 * @param argument Opaque argument passed to @a function.
 * @return B_OK on success, B_BAD_VALUE if @a function is NULL, B_NO_MEMORY
 *         when no reserved slot is free.
 */
status_t
DPCQueue::Add(void (*function)(void*), void* argument)
{
	if (function == NULL)
		return B_BAD_VALUE;

	// get a free callback
	InterruptsSpinLocker locker(fLock);

	DPCCallback* callback = fUnusedFunctionCallbacks.RemoveHead();
	if (callback == NULL)
		return B_NO_MEMORY;

	locker.Unlock();

	// init the callback
	FunctionDPCCallback* functionCallback
		= static_cast<FunctionDPCCallback*>(callback);
	functionCallback->SetTo(function, argument);

	// add it
	status_t error = Add(functionCallback);
	if (error != B_OK)
		Recycle(functionCallback);

	return error;
}


/**
 * @brief Cancel a previously queued callback, waiting if it is in flight.
 *
 * Must be called from thread context: if the callback is currently running,
 * this blocks until it finishes.
 *
 * @param callback Callback previously passed to Add().
 * @return true if the callback was removed before execution, false if it
 *         either already ran or finished running during the wait.
 */
bool
DPCQueue::Cancel(DPCCallback* callback)
{
	InterruptsSpinLocker locker(fLock);

	// If the callback is queued, remove it.
	if (callback->fInQueue == this) {
		fCallbacks.Remove(callback);
		return true;
	}

	// The callback is not queued. If it isn't in progress, we're done, too.
	if (callback != fCallbackInProgress)
		return false;

	// The callback is currently being executed. We need to wait for it to be
	// done.

	// Set the respective condition, if not set yet. For the unlikely case that
	// there are multiple threads trying to cancel the callback at the same
	// time, the condition variable of the first thread will be used.
	ConditionVariable condition;
	if (fCallbackDoneCondition == NULL)
		fCallbackDoneCondition = &condition;

	// add our wait entry
	ConditionVariableEntry waitEntry;
	fCallbackDoneCondition->Add(&waitEntry);

	// wait
	locker.Unlock();
	waitEntry.Wait();

	return false;
}


/**
 * @brief Return a FunctionDPCCallback to the free list for reuse.
 *
 * Safe to call from interrupt context.
 *
 * @param callback Callback previously handed out by Add(function, argument).
 */
void
DPCQueue::Recycle(FunctionDPCCallback* callback)
{
	InterruptsSpinLocker locker(fLock);
	fUnusedFunctionCallbacks.Insert(callback, false);
}


/**
 * @brief Trampoline from spawn_kernel_thread() into the per-instance loop.
 * @param data DPCQueue pointer disguised as a void*.
 * @return Status returned by _Thread().
 */
/*static*/ status_t
DPCQueue::_ThreadEntry(void* data)
{
	return ((DPCQueue*)data)->_Thread();
}


/**
 * @brief Worker loop: drain queued callbacks and wake cancellation waiters.
 *
 * Blocks on fPendingCallbacksCondition when the queue is empty and exits
 * only when the queue has been closed.
 *
 * @return B_OK when the queue is torn down.
 */
status_t
DPCQueue::_Thread()
{
	while (true) {
		InterruptsSpinLocker locker(fLock);

		// get the next pending callback
		DPCCallback* callback = fCallbacks.RemoveHead();
		if (callback == NULL) {
			// nothing is pending -- wait unless the queue is already closed
			if (_IsClosed())
				break;

			ConditionVariableEntry waitEntry;
			fPendingCallbacksCondition.Add(&waitEntry);

			locker.Unlock();
			waitEntry.Wait();

			continue;
		}

		callback->fInQueue = NULL;
		fCallbackInProgress = callback;

		// call the callback
		locker.Unlock();
		callback->DoDPC(this);
		locker.Lock();

		fCallbackInProgress = NULL;

		// wake up threads waiting for the callback to be done
		ConditionVariable* doneCondition = fCallbackDoneCondition;
		fCallbackDoneCondition = NULL;
		locker.Unlock();
		if (doneCondition != NULL)
			doneCondition->NotifyAll();
	}

	return B_OK;
}


// #pragma mark - kernel private


/**
 * @brief Construct and start the three shared default DPC queues.
 *
 * Panics if any of the worker threads cannot be spawned.
 */
void
dpc_init()
{
	// create the default queues
	new(&sNormalPriorityQueue) DPCQueue;
	new(&sHighPriorityQueue) DPCQueue;
	new(&sRealTimePriorityQueue) DPCQueue;

	if (sNormalPriorityQueue.Init("dpc: normal priority", NORMAL_PRIORITY,
			DEFAULT_QUEUE_SLOT_COUNT) != B_OK
		|| sHighPriorityQueue.Init("dpc: high priority", HIGH_PRIORITY,
			DEFAULT_QUEUE_SLOT_COUNT) != B_OK
		|| sRealTimePriorityQueue.Init("dpc: real-time priority",
			REAL_TIME_PRIORITY, DEFAULT_QUEUE_SLOT_COUNT) != B_OK) {
		panic("Failed to create default DPC queues!");
	}
}
