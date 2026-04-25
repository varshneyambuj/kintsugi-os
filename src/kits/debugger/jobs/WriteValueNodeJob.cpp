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
 *   Copyright 2015, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file WriteValueNodeJob.cpp
 * @brief Job that writes a new value back into a debugged value node.
 *
 * WriteValueNodeValueJob takes a ValueNode and a replacement Value, converts
 * the value to its underlying BVariant representation, and uses ValueWriter
 * to push the bytes into the target location described by the node. On
 * success the node's cached location/value is updated under the container
 * lock so observers see the new value.
 */


#include "Jobs.h"

#include <AutoLocker.h>

#include "Architecture.h"
#include "CpuState.h"
#include "DebuggerInterface.h"
#include "TeamTypeInformation.h"
#include "Tracing.h"
#include "Value.h"
#include "ValueLocation.h"
#include "ValueNode.h"
#include "ValueNodeContainer.h"
#include "ValueWriter.h"


/**
 * @brief Construct a WriteValueNodeValueJob targeting the given node.
 *
 * Acquires references to the optional CPU state, the value node, and the new
 * value so they remain valid for the job's lifetime.
 *
 * @param debuggerInterface  Debugger backend used by ValueWriter.
 * @param architecture       Target architecture (used to interpret locations).
 * @param cpuState           CPU state at the relevant frame (may be @c NULL).
 * @param typeInformation    Type information context (currently advisory).
 * @param valueNode          Value node whose backing storage is overwritten.
 * @param newValue           Replacement value to be written.
 */
WriteValueNodeValueJob::WriteValueNodeValueJob(
	DebuggerInterface* debuggerInterface, Architecture* architecture,
	CpuState* cpuState, TeamTypeInformation* typeInformation,
	ValueNode* valueNode, Value* newValue)
	:
	fKey(valueNode, JOB_TYPE_WRITE_VALUE_NODE_VALUE),
	fDebuggerInterface(debuggerInterface),
	fArchitecture(architecture),
	fCpuState(cpuState),
	fTypeInformation(typeInformation),
	fValueNode(valueNode),
	fNewValue(newValue)
{
	if (fCpuState != NULL)
		fCpuState->AcquireReference();
	fValueNode->AcquireReference();
	fNewValue->AcquireReference();
}


/**
 * @brief Releases references held on CPU state, value node, and new value.
 */
WriteValueNodeValueJob::~WriteValueNodeValueJob()
{
	if (fCpuState != NULL)
		fCpuState->ReleaseReference();
	fValueNode->ReleaseReference();
	fNewValue->ReleaseReference();
}


/**
 * @brief Returns the worker-queue key identifying this write job.
 *
 * @return Reference to the job key keyed on the value node.
 */
const JobKey&
WriteValueNodeValueJob::Key() const
{
	return fKey;
}


/**
 * @brief Writes the new value to the node's backing storage.
 *
 * Validates the node is still attached to a container, converts the supplied
 * Value to a BVariant, and asks ValueWriter to write the bytes to the node's
 * location. On success the node's cached location/value is updated under the
 * container lock.
 *
 * @retval B_OK         On a successful write.
 * @retval B_BAD_VALUE  When the value node has no associated container.
 * @return Otherwise the underlying ValueWriter error.
 */
status_t
WriteValueNodeValueJob::Do()
{
	ValueNodeContainer* container = fValueNode->Container();
	if (container == NULL)
		return B_BAD_VALUE;

	ValueWriter writer(fArchitecture, fDebuggerInterface,
		fCpuState, -1);

	BVariant value;
	fNewValue->ToVariant(value);

	status_t error = writer.WriteValue(fValueNode->Location(), value);
	if (error != B_OK)
		return error;

	AutoLocker<ValueNodeContainer> containerLocker(container);
	fValueNode->SetLocationAndValue(fValueNode->Location(), fNewValue, B_OK);

	return B_OK;
}
