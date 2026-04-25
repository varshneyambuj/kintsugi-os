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
 *   Copyright 2012-2016, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file GetThreadStateJob.cpp
 * @brief Job that establishes the run state of a debugged thread.
 *
 * GetThreadStateJob attempts to read the thread's CPU state through the
 * debugger backend. When that succeeds the thread is moved to
 * @c THREAD_STATE_STOPPED and the CPU snapshot is attached; when the kernel
 * reports @c B_BAD_THREAD_STATE the thread is moved to running. Threads that
 * already have a known state are left untouched.
 */


#include "Jobs.h"

#include <AutoLocker.h>

#include "CpuState.h"
#include "DebuggerInterface.h"
#include "Team.h"
#include "Thread.h"


/**
 * @brief Construct a GetThreadStateJob targeting @a thread.
 *
 * Acquires a reference on the thread so it survives until completion.
 *
 * @param debuggerInterface  Backend used to read the CPU state.
 * @param thread             Thread whose run state should be determined.
 */
GetThreadStateJob::GetThreadStateJob(DebuggerInterface* debuggerInterface,
	::Thread* thread)
	:
	fKey(thread, JOB_TYPE_GET_THREAD_STATE),
	fDebuggerInterface(debuggerInterface),
	fThread(thread)
{
	fThread->AcquireReference();
}


/**
 * @brief Releases the reference held on the thread.
 */
GetThreadStateJob::~GetThreadStateJob()
{
	fThread->ReleaseReference();
}


/**
 * @brief Returns the worker-queue key identifying this job.
 *
 * @return Reference to the job key keyed on the thread.
 */
const JobKey&
GetThreadStateJob::Key() const
{
	return fKey;
}


/**
 * @brief Determines whether the thread is currently stopped or running.
 *
 * Reads the thread's CPU state. On success the thread is recorded as
 * stopped and the snapshot is attached; on @c B_BAD_THREAD_STATE the
 * thread is recorded as running. Other errors are propagated unchanged.
 * Threads whose state is already known are left untouched.
 *
 * @retval B_OK    On success or when the thread state was already known.
 * @return         Otherwise the underlying GetCpuState() error.
 */
status_t
GetThreadStateJob::Do()
{
	CpuState* state = NULL;
	status_t error = fDebuggerInterface->GetCpuState(fThread->ID(), state);
	BReference<CpuState> reference(state, true);

	AutoLocker<Team> locker(fThread->GetTeam());

	if (fThread->State() != THREAD_STATE_UNKNOWN)
		return B_OK;

	if (error == B_OK) {
		fThread->SetState(THREAD_STATE_STOPPED);
		fThread->SetCpuState(state);
	} else if (error == B_BAD_THREAD_STATE) {
		fThread->SetState(THREAD_STATE_RUNNING);
	} else
		return error;

	return B_OK;
}
