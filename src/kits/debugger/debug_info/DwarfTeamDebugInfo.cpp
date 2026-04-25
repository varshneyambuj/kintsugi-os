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
 *   Copyright 2014-2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DwarfTeamDebugInfo.cpp
 * @brief Implementation of DwarfTeamDebugInfo, the DWARF-backed team-level
 *        debug info factory.
 *
 * Owns a DwarfManager that loads .debug_info / .debug_line sections from
 * each image's on-disk file, then constructs DwarfImageDebugInfo objects
 * for callers. Loading propagates progress and user-input requests via
 * ImageDebugInfoLoadingState.
 *
 * @see DwarfImageDebugInfo, DwarfManager, ImageDebugInfoLoadingState
 */


#include "DwarfTeamDebugInfo.h"

#include <new>

#include <string.h>

#include "arch/Architecture.h"
#include "DebuggerInterface.h"
#include "DwarfFile.h"
#include "DwarfImageDebugInfo.h"
#include "DwarfImageDebugInfoLoadingState.h"
#include "DwarfManager.h"
#include "GlobalTypeLookup.h"
#include "ImageDebugInfoLoadingState.h"
#include "LocatableFile.h"


/**
 * @brief Constructs the DWARF team-level debug info factory.
 *
 * @param architecture  Target architecture, used to size the DwarfManager.
 * @param interface     Debugger interface; reference acquired.
 * @param fileManager   File manager used to resolve image and source
 *                      paths.
 * @param typeLookup    Cross-image type resolver shared with the type
 *                      cache.
 * @param sourceInfo    Shared team-wide source-information cache.
 * @param typeCache     Global type cache; reference acquired.
 */
DwarfTeamDebugInfo::DwarfTeamDebugInfo(Architecture* architecture,
	DebuggerInterface* interface, FileManager* fileManager,
	GlobalTypeLookup* typeLookup, TeamFunctionSourceInformation* sourceInfo,
	GlobalTypeCache* typeCache)
	:
	fArchitecture(architecture),
	fDebuggerInterface(interface),
	fFileManager(fileManager),
	fManager(NULL),
	fTypeLookup(typeLookup),
	fSourceInfo(sourceInfo),
	fTypeCache(typeCache)
{
	fDebuggerInterface->AcquireReference();
	fTypeCache->AcquireReference();
}


/**
 * @brief Destroys the factory, deleting the DwarfManager and releasing
 *        held references.
 */
DwarfTeamDebugInfo::~DwarfTeamDebugInfo()
{
	fDebuggerInterface->ReleaseReference();
	fTypeCache->ReleaseReference();
	delete fManager;
}


/**
 * @brief Creates and initializes the DwarfManager used to parse DWARF
 *        sections.
 *
 * @retval B_OK         The manager was created and initialized.
 * @retval B_NO_MEMORY  Allocation failed.
 * @retval other        Propagated from DwarfManager::Init().
 */
status_t
DwarfTeamDebugInfo::Init()
{
	fManager = new(std::nothrow) DwarfManager(fArchitecture->AddressSize(), fArchitecture->IsBigEndian());
	if (fManager == NULL)
		return B_NO_MEMORY;

	status_t error = fManager->Init();
	if (error != B_OK)
		return error;

	return B_OK;
}


/**
 * @brief Loads DWARF data for an image and produces the corresponding
 *        DwarfImageDebugInfo object.
 *
 * If @a _state already carries a backend-specific state, it is reused
 * (allowing resume after user input); otherwise a new
 * DwarfImageDebugInfoLoadingState is attached. The image file is loaded
 * through the DwarfManager which may stop and request user input via
 * the loading state.
 *
 * @param imageInfo         Image identity and load parameters.
 * @param imageFile         Located file backing the image; must resolve to
 *                          a real on-disk path.
 * @param _state            Loading state used to communicate progress and
 *                          user-input prompts. The DWARF-specific
 *                          substate is created here when missing.
 * @param _imageDebugInfo   Out parameter receiving the new
 *                          DwarfImageDebugInfo on success.
 * @retval B_OK              Image debug info created.
 * @retval B_ENTRY_NOT_FOUND The image file could not be located on disk.
 * @retval B_BAD_VALUE       Existing backend state was not a DWARF state.
 * @retval B_NO_MEMORY       Allocation failed.
 * @retval other             Errors from the DWARF manager or
 *                           DwarfImageDebugInfo::Init().
 */
status_t
DwarfTeamDebugInfo::CreateImageDebugInfo(const ImageInfo& imageInfo,
	LocatableFile* imageFile, ImageDebugInfoLoadingState& _state,
	SpecificImageDebugInfo*& _imageDebugInfo)
{
	// We only like images whose file we can play with.
	BString filePath;
	if (imageFile == NULL || !imageFile->GetLocatedPath(filePath))
		return B_ENTRY_NOT_FOUND;

	// try to load the DWARF file
	DwarfImageDebugInfoLoadingState* dwarfState;
	if (_state.HasSpecificDebugInfoLoadingState()) {
		dwarfState = dynamic_cast<DwarfImageDebugInfoLoadingState*>(
			_state.GetSpecificDebugInfoLoadingState());
		if (dwarfState == NULL)
			return B_BAD_VALUE;
	} else {
	 	dwarfState = new(std::nothrow) DwarfImageDebugInfoLoadingState();
		if (dwarfState == NULL)
			return B_NO_MEMORY;
		_state.SetSpecificDebugInfoLoadingState(dwarfState);
	}

	status_t error = fManager->LoadFile(filePath, dwarfState->GetFileState());
	if (error != B_OK)
		return error;

	error = fManager->FinishLoading();
	if (error != B_OK)
		return error;

	// create the image debug info
	DwarfImageDebugInfo* debugInfo = new(std::nothrow) DwarfImageDebugInfo(
		imageInfo, fDebuggerInterface, fArchitecture, fFileManager,
		fTypeLookup, fTypeCache, fSourceInfo, dwarfState->GetFileState().dwarfFile);
	if (debugInfo == NULL)
		return B_NO_MEMORY;

	error = debugInfo->Init();
	if (error != B_OK) {
		delete debugInfo;
		return error;
	}

	_imageDebugInfo = debugInfo;
	return B_OK;
}
