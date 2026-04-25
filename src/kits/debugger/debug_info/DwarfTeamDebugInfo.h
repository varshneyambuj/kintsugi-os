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
 * MIT License. Copyright 2009-2016, Haiku.
 * Original authors: Ingo Weinhold, Rene Gollent.
 */

/** @file DwarfTeamDebugInfo.h
    @brief Team-level debug info factory backed by DWARF parsing of each
           image's on-disk file. */

#ifndef DWARF_TEAM_DEBUG_INFO_H
#define DWARF_TEAM_DEBUG_INFO_H

#include "SpecificTeamDebugInfo.h"


class Architecture;
class DebuggerInterface;
class DwarfManager;
class FileManager;
class ImageInfo;
class GlobalTypeCache;
class GlobalTypeLookup;
class TeamFunctionSourceInformation;
class TeamMemory;


/** @brief SpecificTeamDebugInfo that owns a DwarfManager and produces
           DwarfImageDebugInfo objects from DWARF sections. */
class DwarfTeamDebugInfo : public SpecificTeamDebugInfo {
public:
								DwarfTeamDebugInfo(Architecture* architecture,
									DebuggerInterface* interface,
									FileManager* fileManager,
									GlobalTypeLookup* typeLookup,
									TeamFunctionSourceInformation* sourceInfo,
									GlobalTypeCache* typeCache);
	virtual						~DwarfTeamDebugInfo();

			status_t			Init();

	virtual	status_t			CreateImageDebugInfo(const ImageInfo& imageInfo,
									LocatableFile* imageFile,
									ImageDebugInfoLoadingState& _state,
									SpecificImageDebugInfo*& _imageDebugInfo);

private:
			Architecture*		fArchitecture;
			DebuggerInterface*	fDebuggerInterface;
			FileManager*		fFileManager;
			DwarfManager*		fManager;
			GlobalTypeLookup*	fTypeLookup;
			TeamFunctionSourceInformation* fSourceInfo;
			GlobalTypeCache*	fTypeCache;
};


#endif	// DWARF_TEAM_DEBUG_INFO_H
