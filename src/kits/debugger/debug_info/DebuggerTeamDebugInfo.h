/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2009-2014, Haiku.
 * Original authors: Ingo Weinhold, Rene Gollent.
 */

/** @file DebuggerTeamDebugInfo.h
    @brief Team-level debug info backend that relies on the live debugger
           interface for symbol data. */

#ifndef DEBUGGER_TEAM_DEBUG_INFO_H
#define DEBUGGER_TEAM_DEBUG_INFO_H

#include "SpecificTeamDebugInfo.h"


class Architecture;
class DebuggerInterface;
class ImageInfo;


/** @brief Fallback SpecificTeamDebugInfo that produces DebuggerImageDebugInfo
           objects driven by symbol data fetched from the live target. */
class DebuggerTeamDebugInfo : public SpecificTeamDebugInfo {
public:
								DebuggerTeamDebugInfo(
									DebuggerInterface* debuggerInterface,
									Architecture* architecture);
	virtual						~DebuggerTeamDebugInfo();

			status_t			Init();

	virtual	status_t			CreateImageDebugInfo(const ImageInfo& imageInfo,
									LocatableFile* imageFile,
									ImageDebugInfoLoadingState& _state,
									SpecificImageDebugInfo*& _imageDebugInfo);

private:
			DebuggerInterface*	fDebuggerInterface;
			Architecture*		fArchitecture;
};


#endif	// DEBUGGER_TEAM_DEBUG_INFO_H
