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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2008-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2004-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file IOSchedulerSimple.cpp
 * @brief Simple elevator-style I/O scheduler for block device requests.
 *
 * Implements IOScheduler using an elevator algorithm: requests are sorted by
 * offset and dispatched in order to minimize seek time. Supports read-ahead
 * and handles DMA constraint splitting via DMAResource.
 *
 * @see IOScheduler.cpp, dma_resources.cpp, IORequest.cpp
 */


#include "IOSchedulerSimple.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include <lock.h>
#include <thread_types.h>
#include <thread.h>
#include <slab/Slab.h>
#include <util/AutoLock.h>

#include "IOSchedulerRoster.h"


//#define TRACE_IO_SCHEDULER
#ifdef TRACE_IO_SCHEDULER
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif


// #pragma mark -


static object_cache* sRequestOwnerCache;


struct IOSchedulerSimple::RequestOwner
		: IORequestOwner, DoublyLinkedListLinkImpl<RequestOwner> {
	IORequestList	requests;
	IORequestList	completed_requests;
	IOOperationList	operations;
	RequestOwner* hash_link;

			bool				IsActive() const
									{ return !requests.IsEmpty()
										|| !completed_requests.IsEmpty()
										|| !operations.IsEmpty(); }

			void				Dump() const override;
};


/**
 * @brief Dump the state of this RequestOwner to the kernel debugger.
 *
 * Prints the team ID, thread ID, scheduling priority, and all associated
 * pending, completed, and in-flight operations to the kernel debugger console.
 *
 * @note Must only be called from a kernel debugger context (kprintf is used).
 */
void
IOSchedulerSimple::RequestOwner::Dump() const
{
	kprintf("IOSchedulerSimple::RequestOwner at %p\n", this);
	kprintf("  team:     %" B_PRId32 "\n", team);
	kprintf("  thread:   %" B_PRId32 "\n", thread);
	kprintf("  priority: %" B_PRId32 "\n", priority);

	kprintf("  requests:");
	for (IORequestList::ConstIterator it = requests.GetIterator();
			IORequest* request = it.Next();) {
		kprintf(" %p", request);
	}
	kprintf("\n");

	kprintf("  completed requests:");
	for (IORequestList::ConstIterator it = completed_requests.GetIterator();
			IORequest* request = it.Next();) {
		kprintf(" %p", request);
	}
	kprintf("\n");

	kprintf("  operations:");
	for (IOOperationList::ConstIterator it = operations.GetIterator();
			IOOperation* operation = it.Next();) {
		kprintf(" %p", operation);
	}
	kprintf("\n");
}


// #pragma mark -


struct IOSchedulerSimple::RequestOwnerHashDefinition {
	typedef thread_id KeyType;
	typedef IOSchedulerSimple::RequestOwner ValueType;

	size_t HashKey(thread_id key) const			{ return key; }
	size_t Hash(const ValueType* value) const	{ return value->thread; }
	bool Compare(thread_id key, const ValueType* value) const
		{ return value->thread == key; }
	ValueType*& GetLink(ValueType* value) const
		{ return value->hash_link; }
};

struct IOSchedulerSimple::RequestOwnerHashTable
		: BOpenHashTable<RequestOwnerHashDefinition, false> {
};


/**
 * @brief Construct a new IOSchedulerSimple instance.
 *
 * Initialises all synchronisation primitives (mutex, spinlock, condition
 * variables), and lazily creates the global sRequestOwnerCache object cache
 * on first construction.  No threads are started here; call Init() to
 * complete initialisation.
 *
 * @param resource  Pointer to the DMAResource that provides DMA buffers and
 *                  translates IORequests into hardware-friendly IOOperations.
 *                  May be NULL for devices without DMA constraints.
 *
 * @note The sRequestOwnerCache is created under the IOSchedulerRoster lock to
 *       avoid races when multiple schedulers are constructed concurrently.
 */
IOSchedulerSimple::IOSchedulerSimple(DMAResource* resource)
	:
	IOScheduler(resource),
	fSchedulerThread(-1),
	fRequestNotifierThread(-1),
	fOperationArray(NULL),
	fRequestOwners(NULL),
	fBlockSize(0),
	fPendingOperations(0),
	fTerminating(false)
{
	mutex_init(&fLock, "I/O scheduler");
	B_INITIALIZE_SPINLOCK(&fFinisherLock);

	fNewRequestCondition.Init(this, "I/O new request");
	fFinishedOperationCondition.Init(this, "I/O finished operation");
	fFinishedRequestCondition.Init(this, "I/O finished request");

	if (sRequestOwnerCache == NULL) {
		// Borrow the SchedulerRoster lock to initialize.
		IOSchedulerRoster::Default()->Lock();
		if (sRequestOwnerCache == NULL) {
			sRequestOwnerCache = create_object_cache("IOSchedulerSimpleRequestOwners",
				sizeof(RequestOwner), 0);
			object_cache_set_minimum_reserve(sRequestOwnerCache, smp_get_num_cpus());
		}
		IOSchedulerRoster::Default()->Unlock();
	}
}


/**
 * @brief Destroy the IOSchedulerSimple and release all resources.
 *
 * Signals the scheduler and request-notifier threads to terminate by setting
 * fTerminating and broadcasting all condition variables, then waits for both
 * threads to exit.  Frees the unused-operation pool, the operation array,
 * and all RequestOwner objects from the object cache.
 *
 * @note Acquires fLock and fFinisherLock internally during shutdown sequencing.
 * @note Must not be called from interrupt context.
 */
IOSchedulerSimple::~IOSchedulerSimple()
{
	// shutdown threads
	MutexLocker locker(fLock);
	InterruptsSpinLocker finisherLocker(fFinisherLock);
	fTerminating = true;

	fNewRequestCondition.NotifyAll();
	fFinishedOperationCondition.NotifyAll();
	fFinishedRequestCondition.NotifyAll();

	finisherLocker.Unlock();
	locker.Unlock();

	if (fSchedulerThread >= 0)
		wait_for_thread(fSchedulerThread, NULL);

	if (fRequestNotifierThread >= 0)
		wait_for_thread(fRequestNotifierThread, NULL);

	// destroy our belongings
	mutex_lock(&fLock);
	mutex_destroy(&fLock);

	while (IOOperation* operation = fUnusedOperations.RemoveHead())
		delete operation;

	delete[] fOperationArray;

	RequestOwner* owner = fRequestOwners->Clear(true);
	while (owner != NULL) {
		RequestOwner* next = owner->hash_link;
		object_cache_free(sRequestOwnerCache, owner, 0);
		owner = next;
	}

	delete fRequestOwners;
}


/**
 * @brief Initialise the scheduler: allocate pools, set bandwidth, spawn threads.
 *
 * Calls the base class Init(), then allocates the pool of IOOperation objects
 * sized to the DMAResource's buffer count (or a default of 16), determines the
 * effective block size, initialises the RequestOwner hash table, inserts a
 * fallback owner for out-of-memory conditions, calculates initial bandwidth
 * quotas, and spawns the scheduler and request-notifier kernel threads.
 *
 * @param name  Human-readable name for this scheduler instance; used as a
 *              prefix for the spawned thread names.
 *
 * @retval B_OK           Initialisation succeeded and threads are running.
 * @retval B_NO_MEMORY    Memory allocation failed.
 * @retval negative errno Spawning a kernel thread failed.
 *
 * @note Must be called once after construction and before ScheduleRequest().
 */
status_t
IOSchedulerSimple::Init(const char* name)
{
	status_t error = IOScheduler::Init(name);
	if (error != B_OK)
		return error;

	size_t count = fDMAResource != NULL ? fDMAResource->BufferCount() : 16;
	for (size_t i = 0; i < count; i++) {
		IOOperation* operation = new(std::nothrow) IOOperation;
		if (operation == NULL)
			return B_NO_MEMORY;

		fUnusedOperations.Add(operation);
	}

	fOperationArray = new(std::nothrow) IOOperation*[count];

	if (fDMAResource != NULL)
		fBlockSize = fDMAResource->BlockSize();
	if (fBlockSize == 0)
		fBlockSize = 512;

	fRequestOwners = new(std::nothrow) RequestOwnerHashTable;
	if (fRequestOwners == NULL)
		return B_NO_MEMORY;

	error = fRequestOwners->Init(count);
	if (error != B_OK)
		return error;

	// Allocate a fallback RequestOwner, for use under low-memory conditions.
	RequestOwner* fallbackOwner = _GetRequestOwner(-1, -1, true);
	if (fallbackOwner == NULL)
		return B_NO_MEMORY;
	fallbackOwner->priority = B_LOWEST_ACTIVE_PRIORITY;

	// TODO: Use a device speed dependent bandwidths!
	fIterationBandwidth = fBlockSize * 8192;
	fMinOwnerBandwidth = fBlockSize * 1024;
	fMaxOwnerBandwidth = fBlockSize * 4096;

	// start threads
	char buffer[B_OS_NAME_LENGTH];
	strlcpy(buffer, name, sizeof(buffer));
	strlcat(buffer, " scheduler ", sizeof(buffer));
	size_t nameLength = strlen(buffer);
	snprintf(buffer + nameLength, sizeof(buffer) - nameLength, "%" B_PRId32,
		fID);
	fSchedulerThread = spawn_kernel_thread(&_SchedulerThread, buffer,
		B_NORMAL_PRIORITY + 2, (void *)this);
	if (fSchedulerThread < B_OK)
		return fSchedulerThread;

	strlcpy(buffer, name, sizeof(buffer));
	strlcat(buffer, " notifier ", sizeof(buffer));
	nameLength = strlen(buffer);
	snprintf(buffer + nameLength, sizeof(buffer) - nameLength, "%" B_PRId32,
		fID);
	fRequestNotifierThread = spawn_kernel_thread(&_RequestNotifierThread,
		buffer, B_NORMAL_PRIORITY + 2, (void *)this);
	if (fRequestNotifierThread < B_OK)
		return fRequestNotifierThread;

	resume_thread(fSchedulerThread);
	resume_thread(fRequestNotifierThread);

	return B_OK;
}


/**
 * @brief Submit an IORequest to the scheduler for eventual device dispatch.
 *
 * Locks virtual memory for the request's buffer if it refers to user-space
 * addresses, then acquires fLock and associates the request with a
 * RequestOwner (looked up or created by team/thread ID).  The owner is added
 * to the active owners list if it was previously idle, the I/O priority is
 * refreshed from the thread's current priority, and the scheduler thread is
 * woken via fNewRequestCondition.
 *
 * @param request  The IORequest to be scheduled.  Must not be NULL.
 *
 * @retval B_OK        Request accepted and queued.
 * @retval B_NO_MEMORY No RequestOwner could be allocated (panic is issued).
 * @retval other       Buffer memory locking failed; request is immediately
 *                     completed with the error status.
 *
 * @note Acquires fLock internally; must not be called with fLock already held.
 * @note May block briefly on virtual-memory locking for user-space buffers.
 */
status_t
IOSchedulerSimple::ScheduleRequest(IORequest* request)
{
	TRACE("%p->IOSchedulerSimple::ScheduleRequest(%p)\n", this, request);

	IOBuffer* buffer = request->Buffer();

	// TODO: it would be nice to be able to lock the memory later, but we can't
	// easily do it in the I/O scheduler without being able to asynchronously
	// lock memory (via another thread or a dedicated call).

	if (buffer->IsVirtual()) {
		status_t status = buffer->LockMemory(request->TeamID(),
			request->IsWrite());
		if (status != B_OK) {
			request->SetStatusAndNotify(status);
			return status;
		}
	}

	MutexLocker locker(fLock);

	RequestOwner* owner = _GetRequestOwner(request->TeamID(),
		request->ThreadID(), true);
	if (owner == NULL) {
		panic("IOSchedulerSimple: Out of request owners!\n");
		locker.Unlock();
		if (buffer->IsVirtual())
			buffer->UnlockMemory(request->TeamID(), request->IsWrite());
		request->SetStatusAndNotify(B_NO_MEMORY);
		return B_NO_MEMORY;
	}

	bool wasActive = owner->IsActive();
	request->SetOwner(owner);
	owner->requests.Add(request);

	if (owner->thread != -1) {
		int32 priority = thread_get_io_priority(request->ThreadID());
		if (priority >= 0)
			owner->priority = priority;
	}
//dprintf("  request %p -> owner %p (thread %ld, active %d)\n", request, owner, owner->thread, wasActive);

	if (!wasActive)
		fActiveRequestOwners.Add(owner);

	IOSchedulerRoster::Default()->Notify(IO_SCHEDULER_REQUEST_SCHEDULED, this,
		request);

	fNewRequestCondition.NotifyAll();

	return B_OK;
}


/**
 * @brief Abort a previously scheduled IORequest.
 *
 * Cancels an in-flight or pending request and sets its completion status to
 * the supplied error code.  The request will not be dispatched to the device
 * (or will be cancelled if already dispatched).
 *
 * @param request  The IORequest to abort.  Must have been passed to
 *                 ScheduleRequest() and not yet completed.
 * @param status   The error code to report as the request's completion status
 *                 (e.g. B_CANCELED).
 *
 * @note Currently unimplemented; serves as a placeholder.
 */
void
IOSchedulerSimple::AbortRequest(IORequest* request, status_t status)
{
	// TODO:...
//B_CANCELED
}


/**
 * @brief Called by the device layer to report completion of an IOOperation.
 *
 * Records the transferred byte count and status on the operation, moves it to
 * the completed-operations list, and wakes the finisher via
 * fFinishedOperationCondition so that _Finisher() can process it.
 *
 * If the operation's status is already <= 0 it has been finished before and
 * is silently ignored to prevent double-completion.
 *
 * @param operation        The IOOperation that completed.
 * @param status           B_OK on success, or a negative error code.
 * @param transferredBytes Number of bytes actually transferred by the device.
 *
 * @note Called under interrupt context with fFinisherLock held by the caller
 *       or from a device callback; acquires fFinisherLock internally.
 */
void
IOSchedulerSimple::OperationCompleted(IOOperation* operation, status_t status,
	generic_size_t transferredBytes)
{
	InterruptsSpinLocker _(fFinisherLock);

	// finish operation only once
	if (operation->Status() <= 0)
		return;

	operation->SetStatus(status, transferredBytes);

	fCompletedOperations.Add(operation);
	fFinishedOperationCondition.NotifyAll();
}


/**
 * @brief Dump the scheduler's internal state to the kernel debugger.
 *
 * Prints the scheduler's address, its associated DMAResource pointer, and all
 * currently active RequestOwners to the kernel debugger console.
 *
 * @note Must only be called from a kernel debugger context (kprintf is used).
 */
void
IOSchedulerSimple::Dump() const
{
	kprintf("IOSchedulerSimple at %p\n", this);
	kprintf("  DMA resource:   %p\n", fDMAResource);

	kprintf("  active request owners:");
	for (RequestOwnerHashTable::Iterator it
				= fRequestOwners->GetIterator();
			RequestOwner* owner = it.Next();) {
		kprintf(" %p", owner);
	}
	kprintf("\n");
}


/**
 * @brief Process all completed IOOperations and advance associated requests.
 *
 * Drains fCompletedOperations under fFinisherLock, then for each completed
 * operation: calls IOOperation::Finish(), notifies the scheduler roster,
 * recycles DMA buffers, and decrements fPendingOperations.  If the parent
 * IORequest is fully finished it is either handed to the request-notifier
 * thread (if it has callbacks) or completed inline.
 *
 * @note Must NOT be called with fLock held; it acquires fLock internally for
 *       list and counter manipulation.
 * @note Acquires and releases fFinisherLock (spinlock, disables interrupts)
 *       for each operation dequeued.
 */
void
IOSchedulerSimple::_Finisher()
{
	while (true) {
		InterruptsSpinLocker locker(fFinisherLock);
		IOOperation* operation = fCompletedOperations.RemoveHead();
		if (operation == NULL)
			return;

		locker.Unlock();

		TRACE("IOSchedulerSimple::_Finisher(): operation: %p\n", operation);

		bool operationFinished = operation->Finish();

		IOSchedulerRoster::Default()->Notify(IO_SCHEDULER_OPERATION_FINISHED,
			this, operation->Parent(), operation);
			// Notify for every time the operation is passed to the I/O hook,
			// not only when it is fully finished.

		if (!operationFinished) {
			TRACE("  operation: %p not finished yet\n", operation);
			MutexLocker _(fLock);
			((RequestOwner*)operation->Parent()->Owner())->operations.Add(operation);
			fPendingOperations--;
			continue;
		}

		// notify request and remove operation
		IORequest* request = operation->Parent();

		request->OperationFinished(operation);

		// recycle the operation
		MutexLocker _(fLock);
		if (fDMAResource != NULL)
			fDMAResource->RecycleBuffer(operation->Buffer());

		fPendingOperations--;
		fUnusedOperations.Add(operation);

		// If the request is done, we need to perform its notifications.
		if (request->IsFinished()) {
			if (request->Status() == B_OK && request->RemainingBytes() > 0) {
				// The request has been processed OK so far, but it isn't really
				// finished yet.
				request->SetUnfinished();
			} else {
				// Remove the request from the request owner.
				RequestOwner* owner = (RequestOwner*)request->Owner();
				owner->requests.TakeFrom(&owner->completed_requests);
				owner->requests.Remove(request);
				request->SetOwner(NULL);

				if (!owner->IsActive()) {
					fActiveRequestOwners.Remove(owner);
					if (owner->thread != -1) {
						fRequestOwners->Remove(owner);
						object_cache_free(sRequestOwnerCache, owner, 0);
					}
				}

				if (request->HasCallbacks()) {
					// The request has callbacks that may take some time to
					// perform, so we hand it over to the request notifier.
					fFinishedRequests.Add(request);
					fFinishedRequestCondition.NotifyAll();
				} else {
					// No callbacks -- finish the request right now.
					IOSchedulerRoster::Default()->Notify(
						IO_SCHEDULER_REQUEST_FINISHED, this, request);
					request->NotifyFinished();
				}
			}
		}
	}
}


/**
 * @brief Check whether there is pending finisher work.
 *
 * Returns true if fCompletedOperations is non-empty, indicating that
 * _Finisher() should be invoked to process those operations.
 *
 * @retval true   At least one completed operation is waiting to be processed.
 * @retval false  No completed operations are pending.
 *
 * @note Must be called with fFinisherLock held (spinlock, interrupts disabled).
 */
bool
IOSchedulerSimple::_FinisherWorkPending()
{
	return !fCompletedOperations.IsEmpty();
}


/**
 * @brief Translate a request's remaining I/O into ready-to-dispatch operations.
 *
 * If a DMAResource is associated, calls DMAResource::TranslateNext() in a loop
 * until the quantum is exhausted or the request has no remaining bytes.  Each
 * successful translation adds an IOOperation to @p operations.  Without a
 * DMAResource, a single whole-request IOOperation is prepared instead.
 *
 * @param request            The IORequest to prepare operations for.
 * @param operations         Output list to which prepared IOOperations are appended.
 * @param operationsPrepared Running count incremented for each operation prepared.
 * @param quantum            Bandwidth budget (in bytes) for this scheduling round.
 * @param usedBandwidth      Output: bytes consumed from @p quantum by the
 *                           operations prepared in this call.
 *
 * @retval true  Preparation succeeded (operations added) or completed normally;
 *               continue scheduling.
 * @retval false A resource (DMA buffer or bounce buffer) was temporarily
 *               unavailable (B_BUSY); the caller should retry later.
 *
 * @note Must be called with fLock held.
 * @note On a non-B_BUSY error the request is aborted via AbortRequest().
 */
bool
IOSchedulerSimple::_PrepareRequestOperations(IORequest* request,
	IOOperationList& operations, int32& operationsPrepared, off_t quantum,
	off_t& usedBandwidth)
{
//dprintf("IOSchedulerSimple::_PrepareRequestOperations(%p)\n", request);
	usedBandwidth = 0;

	if (fDMAResource != NULL) {
		while (quantum >= (off_t)fBlockSize && request->RemainingBytes() > 0) {
			IOOperation* operation = fUnusedOperations.RemoveHead();
			if (operation == NULL)
				return false;

			status_t status = fDMAResource->TranslateNext(request, operation,
				quantum);
			if (status != B_OK) {
				operation->SetParent(NULL);
				fUnusedOperations.Add(operation);

				// B_BUSY means some resource (DMABuffers or
				// DMABounceBuffers) was temporarily unavailable. That's OK,
				// we'll retry later.
				if (status == B_BUSY)
					return false;

				AbortRequest(request, status);
				return true;
			}
//dprintf("  prepared operation %p\n", operation);

			off_t bandwidth = operation->Length();
			quantum -= bandwidth;
			usedBandwidth += bandwidth;

			operations.Add(operation);
			operationsPrepared++;
		}
	} else {
		// TODO: If the device has block size restrictions, we might need to use
		// a bounce buffer.
		IOOperation* operation = fUnusedOperations.RemoveHead();
		if (operation == NULL)
			return false;

		status_t status = operation->Prepare(request);
		if (status != B_OK) {
			operation->SetParent(NULL);
			fUnusedOperations.Add(operation);
			AbortRequest(request, status);
			return true;
		}

		operation->SetOriginalRange(request->Offset(), request->Length());
		request->Advance(request->Length());

		off_t bandwidth = operation->Length();
		quantum -= bandwidth;
		usedBandwidth += bandwidth;

		operations.Add(operation);
		operationsPrepared++;
	}

	return true;
}


/**
 * @brief Compute the per-owner bandwidth quantum for one scheduling round.
 *
 * Returns the minimum per-owner bandwidth value.  In the future this should
 * return a priority-dependent quantum so that higher-priority owners receive
 * larger slices.
 *
 * @param priority  The I/O priority of the RequestOwner (currently unused).
 *
 * @return Bandwidth budget in bytes to allocate to this owner per round.
 */
off_t
IOSchedulerSimple::_ComputeRequestOwnerBandwidth(int32 priority) const
{
// TODO: Use a priority dependent quantum!
	return fMinOwnerBandwidth;
}


/**
 * @brief Advance to the next active RequestOwner in round-robin order.
 *
 * Walks the active-owners list starting after @p owner.  If no active owners
 * are present the method drains any pending finisher work and then sleeps on
 * fNewRequestCondition until new requests arrive or termination is requested.
 *
 * @param[in,out] owner   On entry, the last-served owner (NULL to start from
 *                        the list head).  On exit, the next owner to serve.
 * @param[out]    quantum Bandwidth budget assigned to the returned owner.
 *
 * @retval true   @p owner is valid and @p quantum has been set.
 * @retval false  The scheduler has been asked to terminate (fTerminating).
 *
 * @note Must be called with fLock held; temporarily releases it while waiting.
 */
bool
IOSchedulerSimple::_NextActiveRequestOwner(RequestOwner*& owner,
	off_t& quantum)
{
	while (true) {
		if (fTerminating)
			return false;

		if (owner != NULL)
			owner = fActiveRequestOwners.GetNext(owner);
		if (owner == NULL)
			owner = fActiveRequestOwners.Head();

		if (owner != NULL) {
			quantum = _ComputeRequestOwnerBandwidth(owner->priority);
			return true;
		}

		// Wait for new requests owners. First check whether any finisher work
		// has to be done.
		InterruptsSpinLocker finisherLocker(fFinisherLock);
		if (_FinisherWorkPending()) {
			finisherLocker.Unlock();
			mutex_unlock(&fLock);
			_Finisher();
			mutex_lock(&fLock);
			continue;
		}

		// Wait for new requests.
		ConditionVariableEntry entry;
		fNewRequestCondition.Add(&entry);

		finisherLocker.Unlock();
		mutex_unlock(&fLock);

		entry.Wait(B_CAN_INTERRUPT);
		_Finisher();
		mutex_lock(&fLock);
	}
}


struct OperationComparator {
	inline bool operator()(const IOOperation* a, const IOOperation* b)
	{
		off_t offsetA = a->Offset();
		off_t offsetB = b->Offset();
		return offsetA < offsetB
			|| (offsetA == offsetB && a->Length() > b->Length());
	}
};


/**
 * @brief Sort a list of IOOperations using the elevator (SSTF) algorithm.
 *
 * Moves all operations from @p operations into a temporary array, sorts them
 * by ascending disk offset (ties broken by descending length), and then
 * re-inserts them into @p operations in elevator order: operations whose
 * offset is >= @p lastOffset are placed first; when the end of the sorted
 * list is reached without finding any such operation the scan wraps around
 * from offset 0.
 *
 * @param[in,out] operations  List of operations to sort in-place.
 * @param[in,out] lastOffset  The disk offset at which the previous scheduling
 *                            round ended; updated to the end offset of the
 *                            last operation added to the sorted list.
 *
 * @note Must be called with fLock released (uses fOperationArray which is
 *       sized during Init() and must not be concurrently accessed).
 */
void
IOSchedulerSimple::_SortOperations(IOOperationList& operations,
	off_t& lastOffset)
{
// TODO: _Scheduler() could directly add the operations to the array.
	// move operations to an array and sort it
	int32 count = 0;
	while (IOOperation* operation = operations.RemoveHead())
		fOperationArray[count++] = operation;

	std::sort(fOperationArray, fOperationArray + count, OperationComparator());

	// move the sorted operations to a temporary list we can work with
//dprintf("operations after sorting:\n");
	IOOperationList sortedOperations;
	for (int32 i = 0; i < count; i++)
//{
//dprintf("  %3ld: %p: offset: %lld, length: %lu\n", i, fOperationArray[i], fOperationArray[i]->Offset(), fOperationArray[i]->Length());
		sortedOperations.Add(fOperationArray[i]);
//}

	// Sort the operations so that no two adjacent operations overlap. This
	// might result in several elevator runs.
	while (!sortedOperations.IsEmpty()) {
		IOOperation* operation = sortedOperations.Head();
		while (operation != NULL) {
			IOOperation* nextOperation = sortedOperations.GetNext(operation);
			if (operation->Offset() >= lastOffset) {
				sortedOperations.Remove(operation);
//dprintf("  adding operation %p\n", operation);
				operations.Add(operation);
				lastOffset = operation->Offset() + operation->Length();
			}

			operation = nextOperation;
		}

		if (!sortedOperations.IsEmpty())
			lastOffset = 0;
	}
}


/**
 * @brief Main scheduler loop: selects requests, builds operation batches, and
 *        dispatches them to the device callback.
 *
 * Runs as a dedicated kernel thread.  Each iteration:
 *  1. Selects the next active RequestOwner via round-robin / _NextActiveRequestOwner().
 *  2. Collects pending IOOperations from the owner (unfinished carry-overs first,
 *     then new ones prepared via _PrepareRequestOperations()) up to the owner
 *     quantum and per-iteration bandwidth cap.
 *  3. Sorts the batch with _SortOperations() for elevator dispatch.
 *  4. Calls fIOCallback for each operation and then waits for all pending
 *     operations to complete, calling _Finisher() between waits.
 *
 * @retval B_OK  The loop exited cleanly because fTerminating was set.
 *
 * @note Runs at B_NORMAL_PRIORITY + 2 as a kernel thread.
 * @note Acquires and releases fLock repeatedly; must never be called directly.
 */
status_t
IOSchedulerSimple::_Scheduler()
{
	RequestOwner marker;
	marker.thread = -1;
	{
		MutexLocker locker(fLock);
		fActiveRequestOwners.Add(&marker, false);
	}

	off_t lastOffset = 0;

	RequestOwner* owner = NULL;
	off_t quantum = 0;

	while (!fTerminating) {
//dprintf("IOSchedulerSimple::_Scheduler(): next iteration: request owner: %p, quantum: %lld\n", owner, quantum);
		MutexLocker locker(fLock);

		IOOperationList operations;
		int32 operationCount = 0;
		bool resourcesAvailable = true;
		off_t iterationBandwidth = fIterationBandwidth;

		if (owner == NULL) {
			owner = fActiveRequestOwners.GetPrevious(&marker);
			quantum = 0;
			fActiveRequestOwners.Remove(&marker);
		}

		if (owner == NULL || quantum < (off_t)fBlockSize) {
			if (!_NextActiveRequestOwner(owner, quantum)) {
				// we've been asked to terminate
				return B_OK;
			}
		}

		while (resourcesAvailable && iterationBandwidth >= (off_t)fBlockSize) {
//dprintf("IOSchedulerSimple::_Scheduler(): request owner: %p (thread %ld)\n",
//owner, owner->thread);
			// Prepare operations for the owner.

			// There might still be unfinished ones.
			while (IOOperation* operation = owner->operations.RemoveHead()) {
				// TODO: We might actually grant the owner more bandwidth than
				// it deserves.
				// TODO: We should make sure that after the first read operation
				// of a partial write, no other write operation to the same
				// location is scheduled!
				operations.Add(operation);
				operationCount++;
				off_t bandwidth = operation->Length();
				quantum -= bandwidth;
				iterationBandwidth -= bandwidth;

				if (quantum < (off_t)fBlockSize
					|| iterationBandwidth < (off_t)fBlockSize) {
					break;
				}
			}

			while (resourcesAvailable && quantum >= (off_t)fBlockSize
					&& iterationBandwidth >= (off_t)fBlockSize) {
				IORequest* request = owner->requests.Head();
				if (request == NULL) {
					resourcesAvailable = false;
if (operationCount == 0)
panic("no more requests for owner %p (thread %" B_PRId32 ")", owner, owner->thread);
					break;
				}

				off_t bandwidth = 0;
				resourcesAvailable = _PrepareRequestOperations(request,
					operations, operationCount, quantum, bandwidth);
				quantum -= bandwidth;
				iterationBandwidth -= bandwidth;
				if (request->RemainingBytes() == 0 || request->Status() <= 0) {
					// If the request has been completed, move it to the
					// completed list, so we don't pick it up again.
					owner->requests.Remove(request);
					owner->completed_requests.Add(request);
				}
			}

			// Get the next owner.
			if (resourcesAvailable)
				_NextActiveRequestOwner(owner, quantum);
		}

		// If the current owner doesn't have anymore requests, we have to
		// insert our marker, since the owner will be gone in the next
		// iteration.
		if (owner->requests.IsEmpty()) {
			fActiveRequestOwners.InsertBefore(owner, &marker);
			owner = NULL;
		}

		if (operations.IsEmpty())
			continue;

		fPendingOperations = operationCount;

		locker.Unlock();

		// sort the operations
		_SortOperations(operations, lastOffset);

		// execute the operations
#ifdef TRACE_IO_SCHEDULER
		int32 i = 0;
#endif
		while (IOOperation* operation = operations.RemoveHead()) {
			TRACE("IOSchedulerSimple::_Scheduler(): calling callback for "
				"operation %ld: %p\n", i++, operation);

			IOSchedulerRoster::Default()->Notify(IO_SCHEDULER_OPERATION_STARTED,
				this, operation->Parent(), operation);

			fIOCallback(fIOCallbackData, operation);

			_Finisher();
		}

		// wait for all operations to finish
		while (!fTerminating) {
			locker.Lock();

			if (fPendingOperations == 0)
				break;

			// Before waiting first check whether any finisher work has to be
			// done.
			InterruptsSpinLocker finisherLocker(fFinisherLock);
			if (_FinisherWorkPending()) {
				finisherLocker.Unlock();
				locker.Unlock();
				_Finisher();
				continue;
			}

			// wait for finished operations
			ConditionVariableEntry entry;
			fFinishedOperationCondition.Add(&entry);

			finisherLocker.Unlock();
			locker.Unlock();

			entry.Wait(B_CAN_INTERRUPT);
			_Finisher();
		}
	}

	return B_OK;
}


/**
 * @brief Kernel thread entry point for the scheduler loop.
 *
 * Casts the opaque @p _self pointer to IOSchedulerSimple and delegates to
 * _Scheduler().
 *
 * @param _self  Pointer to the IOSchedulerSimple instance (passed via
 *               spawn_kernel_thread).
 *
 * @return Return value of _Scheduler().
 */
/*static*/ status_t
IOSchedulerSimple::_SchedulerThread(void *_self)
{
	IOSchedulerSimple *self = (IOSchedulerSimple *)_self;
	return self->_Scheduler();
}


/**
 * @brief Request-notifier loop: delivers completion callbacks for finished requests.
 *
 * Runs as a dedicated kernel thread.  Waits on fFinishedRequestCondition for
 * requests that have been moved to fFinishedRequests by _Finisher(), then
 * notifies the IOSchedulerRoster and calls IORequest::NotifyFinished() for
 * each one.
 *
 * Requests are placed here (rather than being completed inline) when they have
 * callbacks that could be expensive.
 *
 * @retval B_OK  Loop exited because fTerminating was set while the queue was empty.
 *
 * @note Acquires fLock while dequeuing; releases it before invoking callbacks.
 */
status_t
IOSchedulerSimple::_RequestNotifier()
{
	while (true) {
		MutexLocker locker(fLock);

		// get a request
		IORequest* request = fFinishedRequests.RemoveHead();

		if (request == NULL) {
			if (fTerminating)
				return B_OK;

			ConditionVariableEntry entry;
			fFinishedRequestCondition.Add(&entry);

			locker.Unlock();

			entry.Wait();
			continue;
		}

		locker.Unlock();

		IOSchedulerRoster::Default()->Notify(IO_SCHEDULER_REQUEST_FINISHED,
			this, request);

		// notify the request
		request->NotifyFinished();
	}

	// never can get here
	return B_OK;
}


/**
 * @brief Kernel thread entry point for the request-notifier loop.
 *
 * Casts the opaque @p _self pointer to IOSchedulerSimple and delegates to
 * _RequestNotifier().
 *
 * @param _self  Pointer to the IOSchedulerSimple instance (passed via
 *               spawn_kernel_thread).
 *
 * @return Return value of _RequestNotifier().
 */
/*static*/ status_t
IOSchedulerSimple::_RequestNotifierThread(void *_self)
{
	IOSchedulerSimple *self = (IOSchedulerSimple*)_self;
	return self->_RequestNotifier();
}


/**
 * @brief Look up or allocate a RequestOwner for the given team/thread pair.
 *
 * Searches fRequestOwners for an existing entry keyed by @p thread.  If none
 * is found and @p allocate is true, allocates a new RequestOwner from
 * sRequestOwnerCache (non-blocking).  If the cache is exhausted the fallback
 * owner (thread == -1) is returned instead.
 *
 * @param team      Team ID of the requesting context.
 * @param thread    Thread ID used as the hash key.
 * @param allocate  If true, create a new owner when no existing one is found.
 *
 * @return Pointer to the located or newly created RequestOwner, or NULL if
 *         @p allocate is false and no entry exists.
 *
 * @note Must be called with fLock held.
 * @note Allocation uses CACHE_DONT_WAIT_FOR_MEMORY to avoid blocking under lock.
 */
IOSchedulerSimple::RequestOwner*
IOSchedulerSimple::_GetRequestOwner(team_id team, thread_id thread,
	bool allocate)
{
	// lookup in table
	RequestOwner* owner = fRequestOwners->Lookup(thread);
	if (owner != NULL || !allocate)
		return owner;

	// not in table -- allocate a new one
	owner = new(sRequestOwnerCache, CACHE_DONT_WAIT_FOR_MEMORY) RequestOwner;
	if (owner == NULL) {
		// Use the fallback owner.
		return fRequestOwners->Lookup(-1);
	}

	owner->team = team;
	owner->thread = thread;
	owner->priority = B_IDLE_PRIORITY;
	fRequestOwners->InsertUnchecked(owner);

	return owner;
}
