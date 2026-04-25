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
 * MIT License. Copyright 2014, Rene Gollent.
 */

/** @file DwarfFileLoadingState.h
    @brief State carrier describing the progress of locating and loading DWARF debug info. */

#ifndef DWARF_FILE_LOADING_STATE_H
#define DWARF_FILE_LOADING_STATE_H


#include <Referenceable.h>
#include <String.h>


class DwarfFile;


/**
 * @brief Phases of the DWARF file loading state machine.
 *
 * The loader may need user assistance to locate an external debug info
 * file (build-id / .gnu_debuglink); these states drive that handshake.
 */
enum dwarf_file_loading_state {
	DWARF_FILE_LOADING_STATE_INITIAL = 0,
	DWARF_FILE_LOADING_STATE_USER_INPUT_NEEDED,
	DWARF_FILE_LOADING_STATE_USER_INPUT_PROVIDED,
	DWARF_FILE_LOADING_STATE_FAILED,
	DWARF_FILE_LOADING_STATE_SUCCEEDED
};


/**
 * @brief Snapshot of an in-flight DWARF load attempt.
 *
 * Aggregates the partially-constructed DwarfFile, any external info file
 * name discovered in the executable, the user-resolved path on disk, and
 * the current phase of the load.
 */
struct DwarfFileLoadingState {
			BReference<DwarfFile>
								dwarfFile;
			BString				externalInfoFileName;
			BString				locatedExternalInfoPath;
			dwarf_file_loading_state
								state;

								DwarfFileLoadingState();
								~DwarfFileLoadingState();
};


#endif	// DWARF_FILE_LOADING_STATE_H
