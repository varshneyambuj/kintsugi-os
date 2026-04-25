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
 *   Copyright 2014-2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ExpressionEvaluationJob.cpp
 * @brief Job that evaluates a debugger expression in a frame's context.
 *
 * ExpressionEvaluationJob hands off the user-entered expression text to the
 * appropriate SourceLanguage's evaluator. When evaluation requests the value
 * of a still-unresolved value node, the job schedules a child
 * ResolveValueNodeValueJob and waits for completion before reporting the
 * final result through ExpressionInfo::NotifyExpressionEvaluated().
 */


#include "Jobs.h"

#include <String.h>

#include <AutoLocker.h>

#include "DebuggerInterface.h"
#include "ExpressionInfo.h"
#include "model/Thread.h"
#include "SourceLanguage.h"
#include "StackFrame.h"
#include "Team.h"
#include "Type.h"
#include "Value.h"
#include "ValueNode.h"
#include "ValueNodeManager.h"
#include "Variable.h"


/**
 * @brief Construct a job that evaluates @a info in the given context.
 *
 * Captures references to language, expression info, and the optional frame
 * and thread so the job survives any teardown of the calling context.
 *
 * @param team               Owning team (provides type information).
 * @param debuggerInterface  Backend for low-level memory/CPU access.
 * @param language           Source language whose evaluator will run.
 * @param info               Expression details and result-notification target.
 * @param frame              Stack frame providing local context, or @c NULL.
 * @param thread             Thread whose CPU state seeds value resolution.
 */
ExpressionEvaluationJob::ExpressionEvaluationJob(Team* team,
	DebuggerInterface* debuggerInterface, SourceLanguage* language,
	ExpressionInfo* info, StackFrame* frame,
	::Thread* thread)
	:
	fKey(info->Expression(), JOB_TYPE_EVALUATE_EXPRESSION),
	fTeam(team),
	fDebuggerInterface(debuggerInterface),
	fArchitecture(debuggerInterface->GetArchitecture()),
	fTypeInformation(team->GetTeamTypeInformation()),
	fLanguage(language),
	fExpressionInfo(info),
	fFrame(frame),
	fThread(thread),
	fManager(NULL),
	fResultValue(NULL)
{
	fLanguage->AcquireReference();
	fExpressionInfo->AcquireReference();

	if (fFrame != NULL)
		fFrame->AcquireReference();
	if (fThread != NULL)
		fThread->AcquireReference();
}


/**
 * @brief Releases all references acquired in the constructor.
 */
ExpressionEvaluationJob::~ExpressionEvaluationJob()
{
	fLanguage->ReleaseReference();
	fExpressionInfo->ReleaseReference();
	if (fFrame != NULL)
		fFrame->ReleaseReference();
	if (fThread != NULL)
		fThread->ReleaseReference();
	if (fManager != NULL)
		fManager->ReleaseReference();
	if (fResultValue != NULL)
		fResultValue->ReleaseReference();
}


/**
 * @brief Returns the worker-queue key identifying this evaluation job.
 *
 * @return Reference to the job key keyed on the expression text.
 */
const JobKey&
ExpressionEvaluationJob::Key() const
{
	return fKey;
}


/**
 * @brief Runs the evaluator and notifies observers of the result.
 *
 * Lazily creates the ValueNodeManager bound to the current stack frame, runs
 * the language evaluator, and if a value node value is still required,
 * schedules a child resolve job and waits. The final outcome (success or
 * error) is forwarded through ExpressionInfo::NotifyExpressionEvaluated().
 *
 * @return B_OK on success or the error returned by the evaluator chain.
 */
status_t
ExpressionEvaluationJob::Do()
{
	BReference<Value> reference;
	status_t result = B_OK;
	if (fFrame != NULL && fManager == NULL) {
		fManager = new(std::nothrow) ValueNodeManager();
		if (fManager == NULL)
			result = B_NO_MEMORY;
		else
			result = fManager->SetStackFrame(fThread, fFrame);
	}

	if (result != B_OK) {
		fExpressionInfo->NotifyExpressionEvaluated(result, NULL);
		return result;
	}

	ValueNode* neededNode = NULL;
	result = fLanguage->EvaluateExpression(fExpressionInfo->Expression(),
		fManager, fTeam->GetTeamTypeInformation(), fResultValue, neededNode);
	if (neededNode != NULL) {
		result = ResolveNodeValue(neededNode);
		if (State() == JOB_STATE_WAITING)
			return B_OK;
		// if result != B_OK, fall through
	}

	fExpressionInfo->NotifyExpressionEvaluated(result, fResultValue);

	return B_OK;
}


/**
 * @brief Schedules and waits for resolution of @a node's value.
 *
 * If a sibling job is already resolving @a node it is reused; otherwise a
 * new ResolveValueNodeValueJob is scheduled. The function then waits via
 * WaitFor() and translates dependency outcomes into a status code.
 *
 * @param node  Value node whose value the evaluator needs.
 * @retval B_OK     On success or when the dependency is still active/missing.
 * @retval B_ERROR  When the dependency failed or was aborted.
 * @return Otherwise the underlying scheduling error.
 */
status_t
ExpressionEvaluationJob::ResolveNodeValue(ValueNode* node)
{
	AutoLocker<Worker> workerLocker(GetWorker());
	SimpleJobKey jobKey(node, JOB_TYPE_RESOLVE_VALUE_NODE_VALUE);

	status_t error = B_OK;
	if (GetWorker()->GetJob(jobKey) == NULL) {
		workerLocker.Unlock();

		// schedule the job
		error = GetWorker()->ScheduleJob(
			new(std::nothrow) ResolveValueNodeValueJob(fDebuggerInterface,
				fArchitecture, fThread->GetCpuState(), fTypeInformation,
				fManager->GetContainer(), node));
		if (error != B_OK) {
			// scheduling failed -- set the value to invalid
			node->SetLocationAndValue(NULL, NULL, error);
			return error;
		}
	}

	// wait for the job to finish
	workerLocker.Unlock();


	switch (WaitFor(jobKey)) {
		case JOB_DEPENDENCY_SUCCEEDED:
		case JOB_DEPENDENCY_NOT_FOUND:
		case JOB_DEPENDENCY_ACTIVE:
			error = B_OK;
			break;
		case JOB_DEPENDENCY_FAILED:
		case JOB_DEPENDENCY_ABORTED:
		default:
			error = B_ERROR;
			break;
	}

	return error;
}
