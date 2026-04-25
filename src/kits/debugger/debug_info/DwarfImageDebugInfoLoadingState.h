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
 * MIT License. Copyright 2014, Haiku.
 * Original authors: Rene Gollent.
 */

/** @file DwarfImageDebugInfoLoadingState.h
    @brief DWARF backend's per-image loading state carrier; wraps a
           DwarfFileLoadingState and signals when user input is required. */

#ifndef DWARF_IMAGE_DEBUG_INFO_LOADING_STATE_H
#define DWARF_IMAGE_DEBUG_INFO_LOADING_STATE_H


#include "DwarfFileLoadingState.h"
#include "SpecificImageDebugInfoLoadingState.h"


/** @brief SpecificImageDebugInfoLoadingState specialization that holds
           DWARF-specific loading progress and user-input requests. */
class DwarfImageDebugInfoLoadingState
	: public SpecificImageDebugInfoLoadingState {
public:
								DwarfImageDebugInfoLoadingState();
	virtual						~DwarfImageDebugInfoLoadingState();

	virtual	bool				UserInputRequired() const;

			/** @brief Returns the underlying DwarfFileLoadingState by
			           reference for direct mutation by the loader. */
			DwarfFileLoadingState& GetFileState()
									{ return fState; }

private:
			DwarfFileLoadingState fState;
};


#endif // DWARF_IMAGE_DEBUG_INFO_LOADING_STATE_H

