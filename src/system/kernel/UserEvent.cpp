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
 *   Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 *   Copyright 2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file UserEvent.cpp
 * @brief Asynchronous user-space event delivery — signal events and thread events for POSIX timers.
 *
 * Implements UserEvent subclasses used by UserTimer to deliver timer
 * expirations: SignalEvent delivers a POSIX signal to a thread or team;
 * CreateThreadEvent creates a new thread as the notification mechanism.
 *
 * @see UserTimer.cpp, signal.cpp
 */


#include <UserEvent.h>

#include <ksignal.h>
#include <thread_types.h>
#include <util/AutoLock.h>


// #pragma mark - UserEvent


UserEvent::~UserEvent()
{
}


// #pragma mark - SignalEvent


struct SignalEvent::EventSignal : Signal {
	EventSignal(uint32 number, int32 signalCode, int32 errorCode,
		pid_t sendingProcess)
		:
		Signal(number, signalCode, errorCode, sendingProcess),
		fInUse(0)
	{
	}

	bool MarkUsed()
	{
		return atomic_get_and_set(&fInUse, 1) != 0;
	}

	void SetUnused()
	{
		// mark not-in-use
		atomic_set(&fInUse, 0);
	}

	virtual void Handled()
	{
		SetUnused();

		Signal::Handled();
	}

private:
	int32				fInUse;
};


/**
 * @brief Construct a SignalEvent wrapping a pre-allocated EventSignal.
 *
 * Takes ownership of @p signal (via its reference count). The event starts
 * in the idle state with no pending DPC.
 *
 * @param signal Heap-allocated EventSignal whose lifetime is managed by this
 *               SignalEvent; must not be @c NULL.
 */
SignalEvent::SignalEvent(EventSignal* signal)
	:
	fSignal(signal),
	fPendingDPC(0)
{
}


SignalEvent::~SignalEvent()
{
	fSignal->ReleaseReference();
}


void
SignalEvent::SetUserValue(union sigval userValue)
{
	fSignal->SetUserValue(userValue);
}


/**
 * @brief Schedule signal delivery via the default DPC queue.
 *
 * Marks the internal EventSignal as in-use and enqueues a DPC to deliver it.
 * If a DPC is already pending, or the signal is already in use by a prior
 * invocation, returns B_BUSY without queuing another DPC.
 *
 * @retval B_OK    The DPC was successfully queued.
 * @retval B_BUSY  A delivery is already in progress; the caller may retry later.
 */
status_t
SignalEvent::Fire()
{
	bool wasPending = atomic_get_and_set(&fPendingDPC, 1) != 0;
	if (wasPending)
		return B_BUSY;

	if (fSignal->MarkUsed()) {
		atomic_set(&fPendingDPC, 0);
		return B_BUSY;
	}

	AcquireReference();
	DPCQueue::DefaultQueue(B_NORMAL_PRIORITY)->Add(this);

	return B_OK;
}


// #pragma mark - TeamSignalEvent


/**
 * @brief Construct a TeamSignalEvent for the given team and signal.
 *
 * @param team   The team to which the signal will be delivered.
 * @param signal The EventSignal instance that encapsulates signal attributes.
 */
TeamSignalEvent::TeamSignalEvent(Team* team, EventSignal* signal)
	:
	SignalEvent(signal),
	fTeam(team)
{
}


/**
 * @brief Factory method: create a TeamSignalEvent for a team.
 *
 * Allocates both the EventSignal and the TeamSignalEvent on the heap. Both
 * allocations must succeed; if the second fails the signal is freed.
 *
 * @param team          The team to which the signal will be delivered.
 * @param signalNumber  POSIX signal number.
 * @param signalCode    Signal code (e.g. SI_TIMER).
 * @param errorCode     errno value to embed in the siginfo.
 * @return A newly allocated TeamSignalEvent, or @c NULL on allocation failure.
 */
/*static*/ TeamSignalEvent*
TeamSignalEvent::Create(Team* team, uint32 signalNumber, int32 signalCode,
	int32 errorCode)
{
	// create the signal
	EventSignal* signal = new(std::nothrow) EventSignal(signalNumber,
		signalCode, errorCode, team->id);
	if (signal == NULL)
		return NULL;

	// create the event
	TeamSignalEvent* event = new(std::nothrow) TeamSignalEvent(team, signal);
	if (event == NULL) {
		delete signal;
		return NULL;
	}

	return event;
}


/**
 * @brief Schedule signal delivery to the team via the DPC queue.
 *
 * Acquires a reference to the team before calling SignalEvent::Fire() so that
 * the team remains valid when the DPC executes. Releases the reference if
 * Fire() fails.
 *
 * @retval B_OK    The DPC was successfully queued.
 * @retval B_BUSY  A delivery is already in progress.
 */
status_t
TeamSignalEvent::Fire()
{
	// We need a reference to the team to guarantee that it is still there when
	// the DPC actually runs.
	fTeam->AcquireReference();
	status_t result = SignalEvent::Fire();
	if (result != B_OK)
		fTeam->ReleaseReference();

	return result;
}


void
TeamSignalEvent::DoDPC(DPCQueue* queue)
{
	fSignal->AcquireReference();
		// one reference is transferred to send_signal_to_team_locked

	InterruptsSpinLocker locker(fTeam->signal_lock);
	status_t error = send_signal_to_team_locked(fTeam, fSignal->Number(),
		fSignal, B_DO_NOT_RESCHEDULE);
	locker.Unlock();
	fTeam->ReleaseReference();

	// There are situations (for certain signals), in which
	// send_signal_to_team_locked() succeeds without queuing the signal.
	if (error != B_OK || !fSignal->IsPending())
		fSignal->SetUnused();

	// We're no longer queued in the DPC queue, so we can be reused.
	atomic_set(&fPendingDPC, 0);

	ReleaseReference();
}


// #pragma mark - ThreadSignalEvent


/**
 * @brief Construct a ThreadSignalEvent for the given thread and signal.
 *
 * @param thread The thread to which the signal will be delivered.
 * @param signal The EventSignal instance that encapsulates signal attributes.
 */
ThreadSignalEvent::ThreadSignalEvent(Thread* thread, EventSignal* signal)
	:
	SignalEvent(signal),
	fThread(thread)
{
}


/**
 * @brief Factory method: create a ThreadSignalEvent for a thread.
 *
 * Allocates both the EventSignal and the ThreadSignalEvent on the heap.
 *
 * @param thread        The thread to which the signal will be delivered.
 * @param signalNumber  POSIX signal number.
 * @param signalCode    Signal code (e.g. SI_TIMER).
 * @param errorCode     errno value to embed in the siginfo.
 * @param sendingTeam   Team ID of the sender placed in the siginfo.
 * @return A newly allocated ThreadSignalEvent, or @c NULL on allocation failure.
 */
/*static*/ ThreadSignalEvent*
ThreadSignalEvent::Create(Thread* thread, uint32 signalNumber, int32 signalCode,
	int32 errorCode, pid_t sendingTeam)
{
	// create the signal
	EventSignal* signal = new(std::nothrow) EventSignal(signalNumber,
		signalCode, errorCode, sendingTeam);
	if (signal == NULL)
		return NULL;

	// create the event
	ThreadSignalEvent* event = new(std::nothrow) ThreadSignalEvent(thread, signal);
	if (event == NULL) {
		delete signal;
		return NULL;
	}

	return event;
}


/**
 * @brief Schedule signal delivery to the thread via the DPC queue.
 *
 * Acquires a reference to the thread before calling SignalEvent::Fire() so
 * that the thread remains valid when the DPC executes. Releases the reference
 * if Fire() fails.
 *
 * @retval B_OK    The DPC was successfully queued.
 * @retval B_BUSY  A delivery is already in progress.
 */
status_t
ThreadSignalEvent::Fire()
{
	// We need a reference to the thread to guarantee that it is still there
	// when the DPC actually runs.
	fThread->AcquireReference();
	status_t result = SignalEvent::Fire();
	if (result != B_OK)
		fThread->ReleaseReference();

	return result;
}


void
ThreadSignalEvent::DoDPC(DPCQueue* queue)
{
	fSignal->AcquireReference();
		// one reference is transferred to send_signal_to_team_locked
	InterruptsReadSpinLocker teamLocker(fThread->team_lock);
	SpinLocker locker(fThread->team->signal_lock);
	status_t error = send_signal_to_thread_locked(fThread, fSignal->Number(),
		fSignal, B_DO_NOT_RESCHEDULE);
	locker.Unlock();
	teamLocker.Unlock();
	fThread->ReleaseReference();

	// There are situations (for certain signals), in which
	// send_signal_to_team_locked() succeeds without queuing the signal.
	if (error != B_OK || !fSignal->IsPending())
		fSignal->SetUnused();

	// We're no longer queued in the DPC queue, so we can be reused.
	atomic_set(&fPendingDPC, 0);

	ReleaseReference();
}


// #pragma mark - UserEvent


/**
 * @brief Construct a CreateThreadEvent from the given thread creation attributes.
 *
 * Copies the thread name from @p attributes into an internal buffer because
 * the name pointer in the original attributes may point to a temporary.
 * Replaces @c fCreationAttributes.name with the internal buffer pointer.
 *
 * @param attributes Thread creation attributes describing the new thread.
 */
CreateThreadEvent::CreateThreadEvent(const ThreadCreationAttributes& attributes)
	:
	fCreationAttributes(attributes),
	fPendingDPC(0)
{
	// attributes.name is a pointer to a temporary buffer. Copy the name into
	// our own buffer and replace the name pointer.
	strlcpy(fThreadName, attributes.name, sizeof(fThreadName));
	fCreationAttributes.name = fThreadName;
}


/**
 * @brief Factory method: create a CreateThreadEvent from thread creation attributes.
 *
 * @param attributes Thread creation attributes for the new thread.
 * @return A newly allocated CreateThreadEvent, or @c NULL on allocation failure.
 */
/*static*/ CreateThreadEvent*
CreateThreadEvent::Create(const ThreadCreationAttributes& attributes)
{
	return new(std::nothrow) CreateThreadEvent(attributes);
}


/**
 * @brief Schedule new-thread creation via the DPC queue.
 *
 * If a DPC is already pending for this event, returns B_BUSY without
 * enqueuing another one.
 *
 * @retval B_OK    The DPC was successfully queued.
 * @retval B_BUSY  A thread-creation DPC is already in flight.
 */
status_t
CreateThreadEvent::Fire()
{
	bool wasPending = atomic_get_and_set(&fPendingDPC, 1) != 0;
	if (wasPending)
		return B_BUSY;

	AcquireReference();
	DPCQueue::DefaultQueue(B_NORMAL_PRIORITY)->Add(this);

	return B_OK;
}


void
CreateThreadEvent::DoDPC(DPCQueue* queue)
{
	// We're no longer queued in the DPC queue, so we can be reused.
	atomic_set(&fPendingDPC, 0);

	// create the thread
	thread_id threadID = thread_create_thread(fCreationAttributes, false);
	if (threadID >= 0)
		resume_thread(threadID);

	ReleaseReference();
}
