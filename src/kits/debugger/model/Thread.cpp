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
 *   Copyright 2013-2016, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Thread.cpp
 * @brief Implementation of Thread, the per-thread state object held by the
 *        debugger model.
 *
 * Thread records run/stop state, the captured CpuState and StackTrace at
 * the most recent stop, an optional list of return-value snapshots, and
 * the reason a thread was halted. State changes route through the owning
 * Team so that listeners (UI, scripting) receive notifications.
 */

#include "model/Thread.h"

#include <stdio.h>

#include "CpuState.h"
#include "StackTrace.h"
#include "Team.h"


/**
 * @brief Constructs a Thread bound to @a team with kernel id @a threadID.
 *
 * @param team     Owning Team; not reference-counted by Thread.
 * @param threadID Kernel thread identifier.
 */
Thread::Thread(Team* team, thread_id threadID)
	:
	fTeam(team),
	fID(threadID),
	fState(THREAD_STATE_UNKNOWN),
	fReturnValueInfos(NULL),
	fStopRequestPending(false),
	fStoppedReason(THREAD_STOPPED_UNKNOWN),
	fCpuState(NULL),
	fStackTrace(NULL)
{
}


/**
 * @brief Releases CpuState and StackTrace references and clears
 *        return-value snapshots.
 */
Thread::~Thread()
{
	if (fCpuState != NULL)
		fCpuState->ReleaseReference();
	if (fStackTrace != NULL)
		fStackTrace->ReleaseReference();

	ClearReturnValueInfos();
	delete fReturnValueInfos;
}


/**
 * @brief Performs late initialisation that may fail.
 *
 * Allocates the return-value-info container.
 *
 * @return @c B_OK on success, @c B_NO_MEMORY on allocation failure.
 */
status_t
Thread::Init()
{
	fReturnValueInfos = new(std::nothrow) ReturnValueInfoList;
	if (fReturnValueInfos == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


/**
 * @brief Returns true when this Thread is the team's main thread.
 *
 * @return True if the thread id matches the owning team's id.
 */
bool
Thread::IsMainThread() const
{
	return fID == fTeam->ID();
}


/**
 * @brief Replaces the thread's display name.
 *
 * @param name New thread name.
 */
void
Thread::SetName(const BString& name)
{
	fName = name;
}


/**
 * @brief Updates run state, stop reason, and dependent caches.
 *
 * If the new state is not @c THREAD_STATE_STOPPED the cached CpuState,
 * StackTrace, and return-value snapshots are cleared, and any pending
 * stop request is reset. Listeners are notified through the Team.
 *
 * @param state  New run state (running, stopped, unknown, etc.).
 * @param reason Reason code for the most recent stop.
 * @param info   Free-form description of the stop reason.
 */
void
Thread::SetState(uint32 state, uint32 reason, const BString& info)
{
	if (state == fState && reason == fStoppedReason)
		return;

	fState = state;
	fStoppedReason = reason;
	fStoppedReasonInfo = info;

	// unset CPU state and stack trace, if the thread isn't stopped
	if (fState != THREAD_STATE_STOPPED) {
		SetCpuState(NULL);
		SetStackTrace(NULL);
		ClearReturnValueInfos();
		fStopRequestPending = false;
	}

	fTeam->NotifyThreadStateChanged(this);
}


/**
 * @brief Replaces the cached CpuState and notifies listeners.
 *
 * @param state New CpuState, or NULL to clear; reference acquired/released.
 */
void
Thread::SetCpuState(CpuState* state)
{
	if (state == fCpuState)
		return;

	if (fCpuState != NULL)
		fCpuState->ReleaseReference();

	fCpuState = state;

	if (fCpuState != NULL)
		fCpuState->AcquireReference();

	fTeam->NotifyThreadCpuStateChanged(this);
}


/**
 * @brief Replaces the cached StackTrace and notifies listeners.
 *
 * @param trace New StackTrace, or NULL to clear; reference acquired/released.
 */
void
Thread::SetStackTrace(StackTrace* trace)
{
	if (trace == fStackTrace)
		return;

	if (fStackTrace != NULL)
		fStackTrace->ReleaseReference();

	fStackTrace = trace;

	if (fStackTrace != NULL)
		fStackTrace->AcquireReference();

	fTeam->NotifyThreadStackTraceChanged(this);
}

/**
 * @brief Marks that a stop request is pending for this thread.
 *
 * The flag is cleared automatically when the thread next leaves the
 * stopped state.
 */
void
Thread::SetStopRequestPending()
{
	fStopRequestPending = true;
}


/**
 * @brief Appends a captured return-value snapshot to the thread's history.
 *
 * @param info Snapshot to append; reference acquired on success.
 * @return    @c B_OK on success, @c B_NO_MEMORY on allocation failure.
 */
status_t
Thread::AddReturnValueInfo(ReturnValueInfo* info)
{
	if (!fReturnValueInfos->AddItem(info))
		return B_NO_MEMORY;

	info->AcquireReference();
	return B_OK;
}


/**
 * @brief Releases all stored return-value snapshots and empties the list.
 */
void
Thread::ClearReturnValueInfos()
{
	for (int32 i = 0; i < fReturnValueInfos->CountItems(); i++)
		fReturnValueInfos->ItemAt(i)->ReleaseReference();

	fReturnValueInfos->MakeEmpty();
}
