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
 * @file GetCPUStateJob.cpp
 * @brief Job that fetches the CPU register state of a debugged thread.
 *
 * GetCpuStateJob queries the debugger backend for a snapshot of the target
 * thread's CPU state and, if the thread is currently stopped, attaches the
 * snapshot to the Thread model so the UI and other consumers see consistent
 * register values.
 */


#include "Jobs.h"

#include <AutoLocker.h>

#include "CpuState.h"
#include "DebuggerInterface.h"
#include "Team.h"
#include "Thread.h"


/**
 * @brief Construct a GetCpuStateJob targeting @a thread.
 *
 * Acquires a reference on the thread so it remains valid until completion.
 *
 * @param debuggerInterface  Backend used to read CPU state.
 * @param thread             Thread whose CPU state should be sampled.
 */
GetCpuStateJob::GetCpuStateJob(DebuggerInterface* debuggerInterface,
	::Thread* thread)
	:
	fKey(thread, JOB_TYPE_GET_CPU_STATE),
	fDebuggerInterface(debuggerInterface),
	fThread(thread)
{
	fThread->AcquireReference();
}


/**
 * @brief Releases the reference held on the thread.
 */
GetCpuStateJob::~GetCpuStateJob()
{
	fThread->ReleaseReference();
}


/**
 * @brief Returns the worker-queue key identifying this job.
 *
 * @return Reference to the job key keyed on the thread.
 */
const JobKey&
GetCpuStateJob::Key() const
{
	return fKey;
}


/**
 * @brief Reads the thread's CPU state and attaches it when stopped.
 *
 * Calls DebuggerInterface::GetCpuState() and, if successful and the thread
 * is in @c THREAD_STATE_STOPPED, stores the snapshot on the thread under
 * the team lock.
 *
 * @return B_OK on success or the underlying GetCpuState() error.
 */
status_t
GetCpuStateJob::Do()
{
	CpuState* state;
	status_t error = fDebuggerInterface->GetCpuState(fThread->ID(), state);
	if (error != B_OK)
		return error;
	BReference<CpuState> reference(state, true);

	AutoLocker<Team> locker(fThread->GetTeam());

	if (fThread->State() == THREAD_STATE_STOPPED)
		fThread->SetCpuState(state);

	return B_OK;
}
