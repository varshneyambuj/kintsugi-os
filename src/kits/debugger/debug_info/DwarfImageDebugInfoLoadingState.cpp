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
 *   Copyright 2014, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DwarfImageDebugInfoLoadingState.cpp
 * @brief Implementation of DwarfImageDebugInfoLoadingState, the DWARF
 *        backend's per-image loading state carrier.
 *
 * Holds a DwarfFileLoadingState describing in-progress DWARF loading,
 * including any prompts that the loading state handler must present to
 * the user (e.g. missing external debug-info packages).
 *
 * @see DwarfLoadingStateHandler
 */


#include "DwarfImageDebugInfoLoadingState.h"


/**
 * @brief Default-constructs the loading state with an empty
 *        DwarfFileLoadingState.
 */
DwarfImageDebugInfoLoadingState::DwarfImageDebugInfoLoadingState()
	:
	SpecificImageDebugInfoLoadingState(),
	fState()
{
}


/**
 * @brief Destructor; nothing to release.
 */
DwarfImageDebugInfoLoadingState::~DwarfImageDebugInfoLoadingState()
{
}


/**
 * @brief Reports whether the loading flow is currently blocked on user
 *        input (e.g. choosing an external debug-info file).
 *
 * @return @c true when the underlying state is
 *         @c DWARF_FILE_LOADING_STATE_USER_INPUT_NEEDED.
 */
bool
DwarfImageDebugInfoLoadingState::UserInputRequired() const
{
	return fState.state == DWARF_FILE_LOADING_STATE_USER_INPUT_NEEDED;
}
