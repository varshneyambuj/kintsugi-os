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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file StackFrame.cpp
 * @brief Implementation of StackFrame, a single frame in a thread's call stack.
 *
 * StackFrame couples the frame's CpuState, frame and instruction
 * pointers, image and function metadata, and the parameter and local
 * variable lists. It also owns lazy caches of resolved values (StackFrameValues)
 * and value-info entries (StackFrameValueInfos), and dispatches
 * value-retrieval notifications to subscribed Listener instances.
 */

#include "StackFrame.h"

#include <new>

#include "CpuState.h"
#include "FunctionInstance.h"
#include "Image.h"
#include "StackFrameDebugInfo.h"
#include "StackFrameValueInfos.h"
#include "StackFrameValues.h"
#include "Variable.h"


// #pragma mark - StackFrame


/**
 * @brief Constructs a StackFrame and acquires references to its inputs.
 *
 * @param type               Frame type (standard, signal, syscall, ...).
 * @param cpuState           CpuState captured at the frame; reference acquired.
 * @param frameAddress       Frame-pointer-equivalent address.
 * @param instructionPointer Address of the active instruction at this frame.
 * @param debugInfo          StackFrameDebugInfo backing the frame; reference acquired.
 */
StackFrame::StackFrame(stack_frame_type type, CpuState* cpuState,
	target_addr_t frameAddress, target_addr_t instructionPointer,
	StackFrameDebugInfo* debugInfo)
	:
	fType(type),
	fCpuState(cpuState),
	fPreviousCpuState(NULL),
	fFrameAddress(frameAddress),
	fInstructionPointer(instructionPointer),
	fReturnAddress(0),
	fDebugInfo(debugInfo),
	fImage(NULL),
	fFunction(NULL),
	fValues(NULL),
	fValueInfos(NULL)
{
	fCpuState->AcquireReference();
	fDebugInfo->AcquireReference();
}


/**
 * @brief Releases parameter and local-variable references and frame caches.
 */
StackFrame::~StackFrame()
{
	for (int32 i = 0; Variable* variable = fParameters.ItemAt(i); i++)
		variable->ReleaseReference();

	for (int32 i = 0; Variable* variable = fLocalVariables.ItemAt(i); i++)
		variable->ReleaseReference();

	SetImage(NULL);
	SetFunction(NULL);
	SetPreviousCpuState(NULL);

	fDebugInfo->ReleaseReference();
	fCpuState->ReleaseReference();

	if (fValues != NULL)
		fValues->ReleaseReference();

	if (fValueInfos != NULL)
		fValueInfos->ReleaseReference();
}


/**
 * @brief Allocates and initialises the value and value-info caches.
 *
 * @return @c B_OK on success, @c B_NO_MEMORY if either cache cannot be
 *          allocated, or the underlying init error.
 */
status_t
StackFrame::Init()
{
	// create values map
	fValues = new(std::nothrow) StackFrameValues;
	if (fValues == NULL)
		return B_NO_MEMORY;

	status_t error = fValues->Init();
	if (error != B_OK)
		return error;

	// create value infos map
	fValueInfos = new(std::nothrow) StackFrameValueInfos;
	if (fValueInfos == NULL)
		return B_NO_MEMORY;

	error = fValueInfos->Init();
	if (error != B_OK)
		return error;

	return B_OK;
}


/**
 * @brief Replaces the previous-frame CpuState used for unwinding.
 *
 * @param state Replacement state, or NULL to clear; reference acquired/released.
 */
void
StackFrame::SetPreviousCpuState(CpuState* state)
{
	if (fPreviousCpuState != NULL)
		fPreviousCpuState->ReleaseReference();

	fPreviousCpuState = state;

	if (fPreviousCpuState != NULL)
		fPreviousCpuState->AcquireReference();
}

/**
 * @brief Records the unwound return address for this frame.
 *
 * @param address Resolved caller return address.
 */
void
StackFrame::SetReturnAddress(target_addr_t address)
{
	fReturnAddress = address;
}


/**
 * @brief Sets the Image owning the instruction pointer for this frame.
 *
 * @param image New image, or NULL to clear; reference acquired/released.
 */
void
StackFrame::SetImage(Image* image)
{
	if (fImage != NULL)
		fImage->ReleaseReference();

	fImage = image;

	if (fImage != NULL)
		fImage->AcquireReference();
}


/**
 * @brief Sets the FunctionInstance describing the active call.
 *
 * @param function New function instance, or NULL; reference acquired/released.
 */
void
StackFrame::SetFunction(FunctionInstance* function)
{
	if (fFunction != NULL)
		fFunction->ReleaseReference();

	fFunction = function;

	if (fFunction != NULL)
		fFunction->AcquireReference();
}


/**
 * @brief Returns the number of parameters known for this frame.
 *
 * @return Parameter count.
 */
int32
StackFrame::CountParameters() const
{
	return fParameters.CountItems();
}


/**
 * @brief Returns the parameter at @a index, or NULL if out of range.
 *
 * @param index Zero-based parameter index.
 * @return     The Variable describing the parameter, or NULL.
 */
Variable*
StackFrame::ParameterAt(int32 index) const
{
	return fParameters.ItemAt(index);
}


/**
 * @brief Appends a parameter Variable to the frame.
 *
 * @param parameter Parameter to add; reference acquired on success.
 * @return         True on success, false on allocation failure.
 */
bool
StackFrame::AddParameter(Variable* parameter)
{
	if (!fParameters.AddItem(parameter))
		return false;

	parameter->AcquireReference();
	return true;
}


/**
 * @brief Returns the number of local variables known for this frame.
 *
 * @return Local-variable count.
 */
int32
StackFrame::CountLocalVariables() const
{
	return fLocalVariables.CountItems();
}


/**
 * @brief Returns the local variable at @a index, or NULL if out of range.
 *
 * @param index Zero-based local-variable index.
 * @return     The Variable describing the local, or NULL.
 */
Variable*
StackFrame::LocalVariableAt(int32 index) const
{
	return fLocalVariables.ItemAt(index);
}


/**
 * @brief Appends a local Variable to the frame.
 *
 * @param variable Local to add; reference acquired on success.
 * @return        True on success, false on allocation failure.
 */
bool
StackFrame::AddLocalVariable(Variable* variable)
{
	if (!fLocalVariables.AddItem(variable))
		return false;

	variable->AcquireReference();
	return true;
}


/**
 * @brief Subscribes @a listener for value-retrieval notifications.
 *
 * @param listener Listener to register; caller retains ownership.
 */
void
StackFrame::AddListener(Listener* listener)
{
	fListeners.Add(listener);
}


/**
 * @brief Unsubscribes a previously registered listener.
 *
 * @param listener Listener previously passed to @c AddListener().
 */
void
StackFrame::RemoveListener(Listener* listener)
{
	fListeners.Remove(listener);
}


/**
 * @brief Notifies all listeners that a variable's value (or sub-component)
 *        has been retrieved.
 *
 * @param variable Variable whose value was resolved.
 * @param path     Component path inside @a variable that was resolved.
 */
void
StackFrame::NotifyValueRetrieved(Variable* variable, TypeComponentPath* path)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->StackFrameValueRetrieved(this, variable, path);
	}
}


// #pragma mark - StackFrame


/**
 * @brief Virtual destructor anchor for the Listener interface.
 */
StackFrame::Listener::~Listener()
{
}


/**
 * @brief Default no-op implementation of the value-retrieval callback.
 *
 * @param stackFrame Frame whose value was retrieved (unused in default impl).
 * @param variable   Variable whose value was retrieved (unused).
 * @param path       Component path (unused).
 */
void
StackFrame::Listener::StackFrameValueRetrieved(StackFrame* stackFrame,
	Variable* variable, TypeComponentPath* path)
{
}
