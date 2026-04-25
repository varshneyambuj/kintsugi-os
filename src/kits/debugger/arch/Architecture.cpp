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
 *   Copyright 2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Architecture.cpp
 * @brief Architecture-neutral base class implementation.
 *
 * Holds shared logic that does not depend on the specific CPU. Includes
 * the default DWARF register-rule initialization (used by every backend)
 * and CreateStackTrace(), which walks frames using whichever backend is
 * active (x86, x86_64, etc.) via the architecture's overrides.
 *
 * @see ArchitectureX86, ArchitectureX8664
 */

#include "Architecture.h"

#include <new>

#include <AutoDeleter.h>
#include <AutoLocker.h>

#include "CfaContext.h"
#include "CpuState.h"
#include "FunctionInstance.h"
#include "Image.h"
#include "ImageDebugInfo.h"
#include "ImageDebugInfoProvider.h"
#include "Register.h"
#include "RegisterMap.h"
#include "SpecificImageDebugInfo.h"
#include "StackTrace.h"
#include "Team.h"


/**
 * @brief Construct the base architecture wrapper.
 *
 * @param teamMemory          Memory accessor used to read target memory; not owned.
 * @param addressSize         Pointer width in bytes (e.g. 4 for x86, 8 for x86_64).
 * @param debugCpuStateSize   Size of the kernel debugger's debug_cpu_state struct
 *                            for this arch.
 * @param bigEndian           true for big-endian architectures.
 */
Architecture::Architecture(TeamMemory* teamMemory, uint8 addressSize,
	size_t debugCpuStateSize, bool bigEndian)
	:
	fTeamMemory(teamMemory),
	fAddressSize(addressSize),
	fDebugCpuStateSize(debugCpuStateSize),
	fBigEndian(bigEndian)
{
}


/** @brief Virtual destructor anchor. */
Architecture::~Architecture()
{
}


/**
 * @brief Default initialization hook; subclasses may override.
 *
 * @return Always B_OK in the base class.
 */
status_t
Architecture::Init()
{
	return B_OK;
}


/**
 * @brief Populate the DWARF call-frame-address register rules with sensible defaults.
 *
 * Implements the GCC convention of "same value" for callee-preserved registers
 * and a CFA value-offset of 0 for the stack pointer. Subclasses typically call
 * this and then override the rule for the instruction pointer.
 *
 * @param context  CFA context whose register rules are mutated in place.
 * @return Status code from GetDwarfRegisterMaps().
 * @retval B_OK  Default rules installed.
 */
status_t
Architecture::InitRegisterRules(CfaContext& context) const
{
	// Init the initial register rules. The DWARF 3 specs on the
	// matter: "The default rule for all columns before
	// interpretation of the initial instructions is the undefined
	// rule. However, an ABI authoring body or a compilation system
	// authoring body may specify an alternate default value for any
	// or all columns."
	// GCC's assumes the "same value" rule for all callee preserved
	// registers. We set them respectively.
	// the stack pointer is initialized to
	// CFA offset 0 by default.
	const Register* registers = Registers();
	RegisterMap* toDwarf = NULL;
	status_t result = GetDwarfRegisterMaps(&toDwarf, NULL);
	if (result != B_OK)
		return result;

	BReference<RegisterMap> toDwarfMapReference(toDwarf, true);
	for (int32 i = 0; i < CountRegisters(); i++) {
		int32 dwarfReg = toDwarf->MapRegisterIndex(i);
		if (dwarfReg < 0 || dwarfReg > CountRegisters() - 1)
			continue;

		// TODO: on CPUs that have a return address register
		// a default rule should be set up to use that to
		// extract the instruction pointer
		switch (registers[i].Type()) {
			case REGISTER_TYPE_STACK_POINTER:
			{
				context.RegisterRule(dwarfReg)->SetToValueOffset(0);
				break;
			}
			default:
			{
				context.RegisterRule(dwarfReg)->SetToSameValue();
				break;
			}
		}
	}

	return result;
}


/**
 * @brief Walk the target's call stack starting from @a cpuState.
 *
 * Iteratively builds StackFrame entries by consulting the
 * SpecificImageDebugInfo for each instruction pointer, falling back to
 * CreateStackFrame() when DWARF information is not available. Optionally
 * extends an existing partial trace and gathers return-value information
 * for the topmost frame.
 *
 * @param team               Owning team; used to look up images by address.
 * @param imageInfoProvider  Source of ImageDebugInfo for individual images.
 * @param cpuState           Initial CPU state. Reference is taken; subsequent
 *                           previous-frame states are produced as the walk progresses.
 * @param _stackTrace        On entry, may point to an existing StackTrace to
 *                           extend (when @a useExistingTrace is true). On
 *                           successful return, points to the populated trace.
 * @param returnValueInfos   Optional list collecting return-value info for the
 *                           topmost frame.
 * @param maxStackDepth      Maximum number of frames to add; -1 walks to completion.
 * @param useExistingTrace   When true, append to @a _stackTrace instead of allocating.
 * @param getFullFrameInfo   When true, request full frame info from the image
 *                           debug providers (parameters, locals, etc.).
 * @retval B_OK         Trace built (or extended) successfully.
 * @retval B_ERROR      The trace ended up with zero frames.
 * @retval B_NO_MEMORY  Allocation failed while creating frames.
 */
status_t
Architecture::CreateStackTrace(Team* team,
	ImageDebugInfoProvider* imageInfoProvider, CpuState* cpuState,
	StackTrace*& _stackTrace, ReturnValueInfoList* returnValueInfos,
	int32 maxStackDepth, bool useExistingTrace, bool getFullFrameInfo)
{
	BReference<CpuState> cpuStateReference(cpuState);

	StackTrace* stackTrace = NULL;
	ObjectDeleter<StackTrace> stackTraceDeleter;
	StackFrame* nextFrame = NULL;

	if (useExistingTrace)
		stackTrace = _stackTrace;
	else {
		// create the object
		stackTrace = new(std::nothrow) StackTrace;
		if (stackTrace == NULL)
			return B_NO_MEMORY;
		stackTraceDeleter.SetTo(stackTrace);
	}

	// if we're passed an already existing partial stack trace,
	// attempt to continue building it from where it left off.
	if (stackTrace->CountFrames() > 0) {
		nextFrame = stackTrace->FrameAt(stackTrace->CountFrames() - 1);
		cpuState = nextFrame->PreviousCpuState();
	}

	while (cpuState != NULL) {
		// get the instruction pointer
		target_addr_t instructionPointer = cpuState->InstructionPointer();

		// get the image for the instruction pointer
		AutoLocker<Team> teamLocker(team);
		Image* image = team->ImageByAddress(instructionPointer);
		BReference<Image> imageReference(image);
		teamLocker.Unlock();

		// get the image debug info
		ImageDebugInfo* imageDebugInfo = NULL;
		if (image != NULL)
			imageInfoProvider->GetImageDebugInfo(image, imageDebugInfo);
		BReference<ImageDebugInfo> imageDebugInfoReference(imageDebugInfo,
			true);

		// get the function
		teamLocker.Lock();
		FunctionInstance* function = NULL;
		FunctionDebugInfo* functionDebugInfo = NULL;
		if (imageDebugInfo != NULL) {
			function = imageDebugInfo->FunctionAtAddress(instructionPointer);
			if (function != NULL)
				functionDebugInfo = function->GetFunctionDebugInfo();
		}
		BReference<FunctionInstance> functionReference(function);
		teamLocker.Unlock();

		// If the CPU state's instruction pointer is actually the return address
		// of the next frame, we let the architecture fix that.
		if (nextFrame != NULL
			&& nextFrame->ReturnAddress() == cpuState->InstructionPointer()) {
			UpdateStackFrameCpuState(nextFrame, image,
				functionDebugInfo, cpuState);
		}

		// create the frame using the debug info
		StackFrame* frame = NULL;
		CpuState* previousCpuState = NULL;
		if (function != NULL) {
			status_t error = functionDebugInfo->GetSpecificImageDebugInfo()
				->CreateFrame(image, function, cpuState, getFullFrameInfo,
					nextFrame == NULL
						? returnValueInfos : NULL, frame,
					previousCpuState);
			if (error != B_OK && error != B_UNSUPPORTED)
				break;
		}

		// If we have no frame yet, let the architecture create it.
		if (frame == NULL) {
			status_t error = CreateStackFrame(image, functionDebugInfo,
				cpuState, nextFrame == NULL, frame, previousCpuState);
			if (error != B_OK)
				break;
		}

		cpuStateReference.SetTo(previousCpuState, true);

		frame->SetImage(image);
		frame->SetFunction(function);

		if (!stackTrace->AddFrame(frame)) {
			delete frame;
			return B_NO_MEMORY;
		}

		nextFrame = frame;
		cpuState = previousCpuState;
		if (--maxStackDepth == 0)
			break;
	}

	if (stackTrace->CountFrames() == 0)
		return B_ERROR;

	stackTraceDeleter.Detach();
	_stackTrace = stackTrace;
	return B_OK;
}
