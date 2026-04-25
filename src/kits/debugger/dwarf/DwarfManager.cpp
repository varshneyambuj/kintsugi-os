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
 *   Copyright 2014-2017, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DwarfManager.cpp
 * @brief Owner and lifecycle manager for a collection of DwarfFile loaders.
 *
 * DwarfManager is the entry point used by the debugger backend to load
 * DWARF debug information from one or more executables / shared objects.
 * Each load may need a two-phase handshake when the executable references
 * an external debug-info file (build-id or .gnu_debuglink); the supplied
 * @ref DwarfFileLoadingState carries that handshake.
 */

#include "DwarfManager.h"

#include <new>

#include <AutoDeleter.h>
#include <AutoLocker.h>

#include "DwarfFile.h"
#include "DwarfFileLoadingState.h"


/**
 * @brief Constructs a manager bound to the target ABI's address layout.
 *
 * @param addressSize Width of a target address in bytes (4 or 8).
 * @param isBigEndian @c true if the target uses big-endian byte order.
 */
DwarfManager::DwarfManager(uint8 addressSize, bool isBigEndian)
	:
	fAddressSize(addressSize), fIsBigEndian(isBigEndian),
	fLock("dwarf manager")
{
}


/**
 * @brief Destroys the manager and releases its references on every loaded file.
 */
DwarfManager::~DwarfManager()
{
	while (DwarfFile* file = fFiles.RemoveHead())
		file->ReleaseReference();
}


/**
 * @brief Verifies that the manager's lock initialised correctly.
 *
 * @retval B_OK Lock is usable.
 * @retval other Underlying BLocker initialisation failure.
 */
status_t
DwarfManager::Init()
{
	return fLock.InitCheck();
}


/**
 * @brief Begins (or resumes) loading DWARF debug info for an executable.
 *
 * Loading runs in two phases: @c StartLoading parses the section
 * directory and may discover an external debug-info reference; if so,
 * the caller resolves it via OS UI and invokes this routine again with
 * @c locatedExternalInfoPath set.  @c Load then performs the heavy parse.
 *
 * @param fileName Path to the executable / shared object on disk.
 * @param _state   In/out load state.  Carries the partially-built
 *                 DwarfFile across calls and reports the current phase.
 * @retval B_OK         Load completed; @c _state.state is @c SUCCEEDED.
 * @retval B_NO_MEMORY  Allocation failure.
 * @retval other        Filesystem or DWARF-format error; the state
 *                      records whether user input is needed.
 */
status_t
DwarfManager::LoadFile(const char* fileName, DwarfFileLoadingState& _state)
{
	AutoLocker<DwarfManager> locker(this);

	DwarfFile* file = _state.dwarfFile;
	BReference<DwarfFile> fileReference;
	if (file == NULL) {
		file = new(std::nothrow) DwarfFile;
		if (file == NULL)
			return B_NO_MEMORY;
		fileReference.SetTo(file, true);
		_state.dwarfFile = file;
	} else
		fileReference.SetTo(file);

	status_t error;
	if (_state.externalInfoFileName.IsEmpty()) {
		error = file->StartLoading(fileName, _state.externalInfoFileName);
		if (error != B_OK) {
			// only preserve state in the failure case if an external
			// debug information reference was found, but the corresponding
			// file could not be located on disk.
			_state.state = _state.externalInfoFileName.IsEmpty()
				? DWARF_FILE_LOADING_STATE_FAILED
				: DWARF_FILE_LOADING_STATE_USER_INPUT_NEEDED;

			return error;
		}
	}

	error = file->Load(fAddressSize, fIsBigEndian, _state.locatedExternalInfoPath);
	if (error != B_OK) {
		_state.state = DWARF_FILE_LOADING_STATE_FAILED;
		return error;
	}

	fFiles.Add(file);

	fileReference.Detach();
		// keep a reference for ourselves in the list.

	_state.state = DWARF_FILE_LOADING_STATE_SUCCEEDED;

	return B_OK;
}


/**
 * @brief Completes deferred post-load resolution for every loaded file.
 *
 * Some inter-DIE references (especially across compilation units or to
 * supplementary objects) cannot be resolved until every CU is parsed.
 * This method walks the loaded DwarfFiles to finish that work.
 *
 * @retval B_OK   All files finalised successfully.
 * @retval other  First failure reported by a child DwarfFile.
 */
status_t
DwarfManager::FinishLoading()
{
	AutoLocker<DwarfManager> locker(this);

	for (FileList::Iterator it = fFiles.GetIterator();
			DwarfFile* file = it.Next();) {
		status_t error = file->FinishLoading(fAddressSize, fIsBigEndian);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}
