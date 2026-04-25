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
 * @file GetStackTraceJob.cpp
 * @brief Job that produces the stack trace for a debugged thread.
 *
 * GetStackTraceJob calls Architecture::CreateStackTrace() with the thread's
 * CPU state and an ImageDebugInfoProvider implementation that lazily loads
 * debug information for any image referenced by a frame. Once a trace has
 * been built it is attached to the thread, unless the underlying CPU state
 * has since changed.
 */


#include "Jobs.h"

#include <AutoLocker.h>

#include "Architecture.h"
#include "CpuState.h"
#include "DebuggerInterface.h"
#include "ImageDebugInfo.h"
#include "StackTrace.h"
#include "Thread.h"
#include "Team.h"


/**
 * @brief Construct a GetStackTraceJob for a thread snapshot.
 *
 * Acquires references on the thread and on its current CPU state and seeds
 * the user-visible job description.
 *
 * @param debuggerInterface  Backend (used by debug-info provider helpers).
 * @param listener           Job listener for any spawned debug-info loads.
 * @param architecture       Architecture that knows how to unwind frames.
 * @param thread             Thread whose stack trace will be built.
 */
GetStackTraceJob::GetStackTraceJob(DebuggerInterface* debuggerInterface,
	JobListener* listener, Architecture* architecture, ::Thread* thread)
	:
	fKey(thread, JOB_TYPE_GET_STACK_TRACE),
	fDebuggerInterface(debuggerInterface),
	fJobListener(listener),
	fArchitecture(architecture),
	fThread(thread)
{
	fThread->AcquireReference();

	fCpuState = fThread->GetCpuState();
	if (fCpuState != NULL)
		fCpuState->AcquireReference();


	SetDescription("Retrieving stack trace for thread %" B_PRId32, fThread->ID());
}


/**
 * @brief Releases references held on CPU state and thread.
 */
GetStackTraceJob::~GetStackTraceJob()
{
	if (fCpuState != NULL)
		fCpuState->ReleaseReference();

	fThread->ReleaseReference();
}


/**
 * @brief Returns the worker-queue key identifying this job.
 *
 * @return Reference to the job key keyed on the thread.
 */
const JobKey&
GetStackTraceJob::Key() const
{
	return fKey;
}


/**
 * @brief Builds the stack trace and attaches it to the thread.
 *
 * Requires that the thread had a captured CPU state at construction time.
 * After unwinding, the resulting StackTrace is set on the thread only when
 * the thread's current CPU state still matches the captured snapshot --
 * otherwise the trace would be stale.
 *
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When no CPU state was available at construction.
 * @return Otherwise the error from Architecture::CreateStackTrace().
 */
status_t
GetStackTraceJob::Do()
{
	if (fCpuState == NULL)
		return B_BAD_VALUE;

	// get the stack trace
	StackTrace* stackTrace;
	status_t error = fArchitecture->CreateStackTrace(fThread->GetTeam(), this,
		fCpuState, stackTrace, fThread->ReturnValueInfos());
	if (error != B_OK)
		return error;
	BReference<StackTrace> stackTraceReference(stackTrace, true);

	// set the stack trace, unless something has changed
	AutoLocker<Team> locker(fThread->GetTeam());

	if (fThread->GetCpuState() == fCpuState)
		fThread->SetStackTrace(stackTrace);

	return B_OK;
}


/**
 * @brief ImageDebugInfoProvider hook used by the unwinder.
 *
 * Returns existing debug info for @a image when available. Otherwise it
 * schedules a LoadImageDebugInfoJob and waits for its completion before
 * returning the freshly-loaded info, transparently handling races between
 * scheduling and waiting.
 *
 * @param image  Image whose debug info the unwinder needs.
 * @param _info  Out: receives a referenced ImageDebugInfo on success.
 * @retval B_OK     On success; @a _info holds an acquired reference.
 * @retval B_ERROR  When the dependency load failed or was aborted.
 */
status_t
GetStackTraceJob::GetImageDebugInfo(Image* image, ImageDebugInfo*& _info)
{
	AutoLocker<Team> teamLocker(fThread->GetTeam());

	while (image->GetImageDebugInfo() == NULL) {
		// schedule a job, if not loaded
		ImageDebugInfo* info;
		status_t error = LoadImageDebugInfoJob::ScheduleIfNecessary(GetWorker(),
			image, fJobListener, &info);
		if (error != B_OK)
			return error;

		if (info != NULL) {
			_info = info;
			return B_OK;
		}

		teamLocker.Unlock();

		// wait for the job to finish
		switch (WaitFor(SimpleJobKey(image, JOB_TYPE_LOAD_IMAGE_DEBUG_INFO))) {
			case JOB_DEPENDENCY_SUCCEEDED:
			case JOB_DEPENDENCY_NOT_FOUND:
				// "Not found" can happen due to a race condition between
				// unlocking the worker and starting to wait.
				break;
			case JOB_DEPENDENCY_FAILED:
			case JOB_DEPENDENCY_ABORTED:
			default:
				return B_ERROR;
		}

		teamLocker.Lock();
	}

	_info = image->GetImageDebugInfo();
	_info->AcquireReference();

	return B_OK;
}
