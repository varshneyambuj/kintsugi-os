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
 *   Copyright 2012-2015, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ResolveValueNodeJob.cpp
 * @brief Job that resolves the location and value of a debugged value node.
 *
 * ResolveValueNodeValueJob walks a node's parent chain on demand, scheduling
 * recursive resolution jobs when the parent's value has not yet been
 * computed. Once parent and child-location dependencies are satisfied, the
 * node is asked to compute its own location and value through a ValueLoader,
 * and the result is committed back into the value node container.
 */


#include "Jobs.h"

#include <AutoLocker.h>

#include "Architecture.h"
#include "CpuState.h"
#include "DebuggerInterface.h"
#include "TeamTypeInformation.h"
#include "Tracing.h"
#include "Value.h"
#include "ValueLoader.h"
#include "ValueLocation.h"
#include "ValueNode.h"
#include "ValueNodeContainer.h"
#include "Variable.h"
#include "VariableValueNodeChild.h"


/**
 * @brief Construct a ResolveValueNodeValueJob for the given value node.
 *
 * Acquires references to the optional CPU state, container, and value node
 * so they remain valid until the job completes.
 *
 * @param debuggerInterface  Debugger backend used by the loader.
 * @param architecture       Target architecture.
 * @param cpuState           CPU state for the active frame, or @c NULL.
 * @param typeInformation    Team type-information service for the loader.
 * @param container          Container the node belongs to.
 * @param valueNode          Node whose location and value should be resolved.
 */
ResolveValueNodeValueJob::ResolveValueNodeValueJob(
	DebuggerInterface* debuggerInterface, Architecture* architecture,
	CpuState* cpuState, TeamTypeInformation* typeInformation,
	ValueNodeContainer* container, ValueNode* valueNode)
	:
	fKey(valueNode, JOB_TYPE_RESOLVE_VALUE_NODE_VALUE),
	fDebuggerInterface(debuggerInterface),
	fArchitecture(architecture),
	fCpuState(cpuState),
	fTypeInformation(typeInformation),
	fContainer(container),
	fValueNode(valueNode)
{
	if (fCpuState != NULL)
		fCpuState->AcquireReference();
	fContainer->AcquireReference();
	fValueNode->AcquireReference();
}


/**
 * @brief Releases references on CPU state, container, and value node.
 */
ResolveValueNodeValueJob::~ResolveValueNodeValueJob()
{
	if (fCpuState != NULL)
		fCpuState->ReleaseReference();
	fContainer->ReleaseReference();
	fValueNode->ReleaseReference();
}


/**
 * @brief Returns the worker-queue key identifying this resolve job.
 *
 * @return Reference to the job key keyed on the value node.
 */
const JobKey&
ResolveValueNodeValueJob::Key() const
{
	return fKey;
}


/**
 * @brief Resolves the node's location and value, scheduling parents as needed.
 *
 * Short-circuits if the node is already resolved or no longer belongs to the
 * container. On failure, the node is marked with the resulting error so
 * observers are not left waiting on a stale node.
 *
 * @retval B_OK          On success or when resolution is already complete.
 * @retval B_BAD_VALUE   When the node no longer belongs to its container.
 * @return Otherwise an error from the underlying loader/parent resolution.
 */
status_t
ResolveValueNodeValueJob::Do()
{
	// check whether the node still belongs to the container
	AutoLocker<ValueNodeContainer> containerLocker(fContainer);
	if (fValueNode->Container() != fContainer)
		return B_BAD_VALUE;

	// if already resolved, we're done
	status_t nodeResolutionState
		= fValueNode->LocationAndValueResolutionState();
	if (nodeResolutionState != VALUE_NODE_UNRESOLVED)
		return nodeResolutionState;

	containerLocker.Unlock();

	// resolve
	status_t error = _ResolveNodeValue();
	if (error != B_OK) {
		nodeResolutionState = fValueNode->LocationAndValueResolutionState();
		if (nodeResolutionState != VALUE_NODE_UNRESOLVED)
			return nodeResolutionState;

		containerLocker.Lock();
		fValueNode->SetLocationAndValue(NULL, NULL, error);
		containerLocker.Unlock();
	}

	return error;
}


/**
 * @brief Drives the resolve sequence: parent value, child location, own value.
 *
 * Recursively schedules resolution of the parent node when needed, then asks
 * the node child to resolve its location, and finally calls
 * ValueNode::ResolvedLocationAndValue() through a ValueLoader. The final
 * location/value are committed under the container lock.
 *
 * @return B_OK on success or the first error encountered along the chain.
 */
status_t
ResolveValueNodeValueJob::_ResolveNodeValue()
{
	// get the node child and parent node
	AutoLocker<ValueNodeContainer> containerLocker(fContainer);
	ValueNodeChild* nodeChild = fValueNode->NodeChild();
	BReference<ValueNodeChild> nodeChildReference(nodeChild);

	ValueNode* parentNode = nodeChild->Parent();
	BReference<ValueNode> parentNodeReference(parentNode);

	// Check whether the node child location has been resolved already
	// (successfully).
	status_t nodeChildResolutionState = nodeChild->LocationResolutionState();
	bool nodeChildDone = nodeChildResolutionState != VALUE_NODE_UNRESOLVED;
	if (nodeChildDone && nodeChildResolutionState != B_OK)
		return nodeChildResolutionState;

	// If the child node location has not been resolved yet, check whether the
	// parent node location and value have been resolved already (successfully).
	bool parentDone = true;
	if (!nodeChildDone && parentNode != NULL) {
		status_t parentResolutionState
			= parentNode->LocationAndValueResolutionState();
		parentDone = parentResolutionState != VALUE_NODE_UNRESOLVED;
		if (parentDone && parentResolutionState != B_OK)
			return parentResolutionState;
	}

	containerLocker.Unlock();

	// resolve the parent node location and value, if necessary
	if (!parentDone) {
		status_t error = _ResolveParentNodeValue(parentNode);
		if (error != B_OK) {
			TRACE_LOCALS("ResolveValueNodeValueJob::_ResolveNodeValue(): value "
				"node: %p (\"%s\"): _ResolveParentNodeValue(%p) failed\n",
				fValueNode, fValueNode->Name().String(), parentNode);
			return error;
		}

		if (State() == JOB_STATE_WAITING)
			return B_OK;
	}

	// resolve the node child location, if necessary
	if (!nodeChildDone) {
		status_t error = _ResolveNodeChildLocation(nodeChild);
		if (error != B_OK) {
			TRACE_LOCALS("ResolveValueNodeValueJob::_ResolveNodeValue(): value "
				"node: %p (\"%s\"): _ResolveNodeChildLocation(%p) failed\n",
				fValueNode, fValueNode->Name().String(), nodeChild);
			return error;
		}
	}

	CpuState* variableCpuState = NULL;
	VariableValueNodeChild* variableChild = dynamic_cast<
		VariableValueNodeChild*>(nodeChild);
	if (variableChild != NULL)
		variableCpuState = variableChild->GetVariable()->GetCpuState();

	// resolve the node location and value
	ValueLoader valueLoader(fArchitecture, fDebuggerInterface,
		variableCpuState != NULL ? variableCpuState : fCpuState);
	ValueLocation* location;
	Value* value;
	status_t error = fValueNode->ResolvedLocationAndValue(&valueLoader,
		location, value);
	if (error != B_OK) {
		TRACE_LOCALS("ResolveValueNodeValueJob::_ResolveNodeValue(): value "
			"node: %p (\"%s\"): fValueNode->ResolvedLocationAndValue() "
			"failed\n", fValueNode, fValueNode->Name().String());
		return error;
	}
	BReference<ValueLocation> locationReference(location, true);
	BReference<Value> valueReference(value, true);

	// set location and value on the node
	containerLocker.Lock();
	status_t nodeResolutionState
		= fValueNode->LocationAndValueResolutionState();
	if (nodeResolutionState != VALUE_NODE_UNRESOLVED)
		return nodeResolutionState;
	fValueNode->SetLocationAndValue(location, value, B_OK);
	containerLocker.Unlock();

	return B_OK;
}


/**
 * @brief Resolves the location of a value node child via ValueLoader.
 *
 * Sets the resolved location on the node child while honouring the case
 * where another resolver already raced ahead and set a final state.
 *
 * @param nodeChild  Child whose location should be resolved.
 * @return B_OK on success or the underlying ResolveLocation() error.
 */
status_t
ResolveValueNodeValueJob::_ResolveNodeChildLocation(ValueNodeChild* nodeChild)
{
	// resolve the location
	ValueLoader valueLoader(fArchitecture, fDebuggerInterface, fCpuState);
	ValueLocation* location = NULL;
	status_t error = nodeChild->ResolveLocation(&valueLoader, location);
	BReference<ValueLocation> locationReference(location, true);

	// set the location on the node child
	AutoLocker<ValueNodeContainer> containerLocker(fContainer);
	status_t nodeChildResolutionState = nodeChild->LocationResolutionState();
	if (nodeChildResolutionState == VALUE_NODE_UNRESOLVED)
		nodeChild->SetLocation(location, error);
	else
		error = nodeChildResolutionState;

	return error;
}


/**
 * @brief Ensures @a parentNode has a resolved value, scheduling a job if not.
 *
 * If a sibling job is already resolving @a parentNode this routine simply
 * waits on it; otherwise a new ResolveValueNodeValueJob is scheduled and the
 * caller blocks via WaitFor() until it completes. Race conditions where the
 * job vanishes between scheduling and waiting are handled gracefully.
 *
 * @param parentNode  Parent node whose value must be resolved.
 * @retval B_OK         On success or when the dependency is already active.
 * @retval B_BAD_VALUE  When the parent no longer belongs to the container.
 * @retval B_ERROR      When the dependency job failed or was aborted.
 */
status_t
ResolveValueNodeValueJob::_ResolveParentNodeValue(ValueNode* parentNode)
{
	AutoLocker<ValueNodeContainer> containerLocker(fContainer);

	if (parentNode->Container() != fContainer)
		return B_BAD_VALUE;

	// if the parent node already has a value, we're done
	status_t nodeResolutionState
		= parentNode->LocationAndValueResolutionState();
	if (nodeResolutionState != VALUE_NODE_UNRESOLVED)
		return nodeResolutionState;

	// check whether a job is already in progress
	AutoLocker<Worker> workerLocker(GetWorker());
	SimpleJobKey jobKey(parentNode, JOB_TYPE_RESOLVE_VALUE_NODE_VALUE);
	if (GetWorker()->GetJob(jobKey) == NULL) {
		workerLocker.Unlock();

		// schedule the job
		status_t error = GetWorker()->ScheduleJob(
			new(std::nothrow) ResolveValueNodeValueJob(fDebuggerInterface,
				fArchitecture, fCpuState, fTypeInformation, fContainer,
				parentNode));
		if (error != B_OK) {
			// scheduling failed -- set the value to invalid
			parentNode->SetLocationAndValue(NULL, NULL, error);
			return error;
		}
	}

	// wait for the job to finish
	workerLocker.Unlock();
	containerLocker.Unlock();

	switch (WaitFor(jobKey)) {
		case JOB_DEPENDENCY_SUCCEEDED:
		case JOB_DEPENDENCY_NOT_FOUND:
			// "Not found" can happen due to a race condition between
			// unlocking the worker and starting to wait.
			break;
		case JOB_DEPENDENCY_ACTIVE:
			return B_OK;
		case JOB_DEPENDENCY_FAILED:
		case JOB_DEPENDENCY_ABORTED:
		default:
			return B_ERROR;
	}

	containerLocker.Lock();

	// now there should be a value for the node
	nodeResolutionState = parentNode->LocationAndValueResolutionState();
	return nodeResolutionState != VALUE_NODE_UNRESOLVED
		? nodeResolutionState : B_ERROR;
}
