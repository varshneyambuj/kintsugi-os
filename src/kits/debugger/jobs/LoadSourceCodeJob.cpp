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
 * @file LoadSourceCodeJob.cpp
 * @brief Job that loads or disassembles source code for a function instance.
 *
 * LoadSourceCodeJob first attempts to load the high-level source file backing
 * the function (when @c fLoadForFunction is set). If source is unavailable,
 * or only disassembly is desired for this instance, it falls back to
 * disassembling the function via the team's debug info. The resulting source
 * or disassembled code is attached to the function/function instance so the
 * UI can display it.
 */


#include "Jobs.h"

#include <AutoLocker.h>

#include "Architecture.h"
#include "DebuggerInterface.h"
#include "DisassembledCode.h"
#include "Function.h"
#include "FunctionInstance.h"
#include "FileSourceCode.h"
#include "Team.h"
#include "TeamDebugInfo.h"


/**
 * @brief Construct a LoadSourceCodeJob for the given function instance.
 *
 * Acquires a reference to @a functionInstance and seeds the user-visible job
 * description with its pretty-printed name.
 *
 * @param debuggerInterface  Debugger backend used for disassembly.
 * @param architecture       Target architecture (used by the disassembler).
 * @param team               Owning team that holds debug info.
 * @param functionInstance   Function instance whose code should be loaded.
 * @param loadForFunction    @c true to load high-level source for the parent
 *                           Function as well as disassembly; @c false to
 *                           disassemble only this instance.
 */
LoadSourceCodeJob::LoadSourceCodeJob(
	DebuggerInterface* debuggerInterface, Architecture* architecture,
	Team* team, FunctionInstance* functionInstance, bool loadForFunction)
	:
	fKey(functionInstance, JOB_TYPE_LOAD_SOURCE_CODE),
	fDebuggerInterface(debuggerInterface),
	fArchitecture(architecture),
	fTeam(team),
	fFunctionInstance(functionInstance),
	fLoadForFunction(loadForFunction)
{
	fFunctionInstance->AcquireReference();

	SetDescription("Loading source code for function %s",
		fFunctionInstance->PrettyName().String());
}


/**
 * @brief Releases the reference held on the function instance.
 */
LoadSourceCodeJob::~LoadSourceCodeJob()
{
	fFunctionInstance->ReleaseReference();
}


/**
 * @brief Returns the worker-queue key for this job.
 *
 * @return Reference to the job key keyed on the function instance.
 */
const JobKey&
LoadSourceCodeJob::Key() const
{
	return fKey;
}


/**
 * @brief Loads source code and/or disassembly for the function instance.
 *
 * Drives the two-stage load: high-level source through TeamDebugInfo then,
 * unless overridden, disassembly through Architecture/TeamDebugInfo. Updates
 * the Function and FunctionInstance source-code states under the team lock,
 * and ensures only one of source or disassembly is presented as active.
 *
 * @return B_OK on successful load or the underlying error from
 *         TeamDebugInfo::DisassembleFunction().
 */
status_t
LoadSourceCodeJob::Do()
{
	// if requested, try loading the source code for the function
	Function* function = fFunctionInstance->GetFunction();
	if (fLoadForFunction) {
		FileSourceCode* sourceCode;
		status_t error = fTeam->DebugInfo()->LoadSourceCode(
			function->SourceFile(), sourceCode);

		AutoLocker<Team> locker(fTeam);

		if (error == B_OK) {
			function->SetSourceCode(sourceCode, FUNCTION_SOURCE_LOADED);
			sourceCode->ReleaseReference();
			return B_OK;
		}

		function->SetSourceCode(NULL, FUNCTION_SOURCE_UNAVAILABLE);
	}

	// Only try to load the function instance code, if it's not overridden yet.
	AutoLocker<Team> locker(fTeam);
	if (fFunctionInstance->SourceCodeState() != FUNCTION_SOURCE_LOADING)
		return B_OK;
	locker.Unlock();

	// disassemble the function
	DisassembledCode* sourceCode = NULL;
	status_t error = fTeam->DebugInfo()->DisassembleFunction(fFunctionInstance,
		sourceCode);

	// set the result
	locker.Lock();
	if (error == B_OK) {
		if (fFunctionInstance->SourceCodeState() == FUNCTION_SOURCE_LOADING) {
			// various parts of the debugger expect functions to have only
			// one of source or disassembly available. As such, if the current
			// function had source code previously active, unset it when
			// explicitly asked for disassembly. This needs to be done first
			// since Function will clear the disassembled code states of all
			// its child instances.
			function_source_state state
				= fLoadForFunction ? FUNCTION_SOURCE_LOADED
					: FUNCTION_SOURCE_SUPPRESSED;
			if (function->SourceCodeState() == FUNCTION_SOURCE_LOADED) {
				FileSourceCode* functionSourceCode = function->GetSourceCode();
				function->SetSourceCode(functionSourceCode, state);
			}

			fFunctionInstance->SetSourceCode(sourceCode, state);
			sourceCode->ReleaseReference();
		}
	} else
		fFunctionInstance->SetSourceCode(NULL, FUNCTION_SOURCE_UNAVAILABLE);

	return error;
}
