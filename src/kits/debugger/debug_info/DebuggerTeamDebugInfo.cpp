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
 * @file DebuggerTeamDebugInfo.cpp
 * @brief Implementation of DebuggerTeamDebugInfo, the team-level debug info
 *        backend that relies solely on the live debugger interface.
 *
 * This backend is used as a last resort when richer per-image debug data
 * (such as DWARF) is unavailable. It produces DebuggerImageDebugInfo
 * instances that draw symbol information directly from the running target.
 *
 * @see SpecificTeamDebugInfo, DebuggerImageDebugInfo
 */


#include "DebuggerTeamDebugInfo.h"

#include <new>

#include "DebuggerImageDebugInfo.h"


/**
 * @brief Constructs the team-wide backend bound to a debugger interface.
 *
 * @param debuggerInterface  Interface used to query the live target.
 * @param architecture       Architecture description of the target.
 */
DebuggerTeamDebugInfo::DebuggerTeamDebugInfo(
	DebuggerInterface* debuggerInterface, Architecture* architecture)
	:
	fDebuggerInterface(debuggerInterface),
	fArchitecture(architecture)
{
}


/**
 * @brief Destroys the team debug info.
 */
DebuggerTeamDebugInfo::~DebuggerTeamDebugInfo()
{
}


/**
 * @brief Performs deferred initialization for the backend.
 *
 * @retval B_OK Always; this backend has no resources to bring up.
 */
status_t
DebuggerTeamDebugInfo::Init()
{
	return B_OK;
}


/**
 * @brief Creates a per-image debug info object backed by the debugger
 *        interface.
 *
 * @param imageInfo          Description of the image to be inspected.
 * @param imageFile          Located file for the image; ignored by this
 *                           backend.
 * @param _state             Loading state object; unused here but kept for
 *                           interface symmetry.
 * @param _imageDebugInfo    Out parameter receiving the new
 *                           DebuggerImageDebugInfo on success.
 * @retval B_OK         The image debug info was created and initialized.
 * @retval B_NO_MEMORY  Allocation failed.
 * @retval other        Propagated from DebuggerImageDebugInfo::Init().
 */
status_t
DebuggerTeamDebugInfo::CreateImageDebugInfo(const ImageInfo& imageInfo,
	LocatableFile* imageFile, ImageDebugInfoLoadingState& _state,
	SpecificImageDebugInfo*& _imageDebugInfo)
{
	DebuggerImageDebugInfo* debuggerInfo
		= new(std::nothrow) DebuggerImageDebugInfo(imageInfo,
			fDebuggerInterface, fArchitecture);
	if (debuggerInfo == NULL)
		return B_NO_MEMORY;

	status_t error = debuggerInfo->Init();
	if (error != B_OK) {
		delete debuggerInfo;
		return error;
	}

	_imageDebugInfo = debuggerInfo;
	return B_OK;
}
