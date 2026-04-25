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
 * @file Image.cpp
 * @brief Implementation of Image, a loaded executable or shared library
 *        attached to a Team in the debugger model.
 *
 * Image owns the descriptive ImageInfo, an optional LocatableFile pointing
 * at the on-disk binary, and the asynchronously loaded ImageDebugInfo. It
 * mediates ownership between the Team's debug-info aggregator and the
 * notification path that informs listeners when debug info finishes
 * loading or transitions to an error state.
 */

#include "Image.h"

#include "ImageDebugInfo.h"
#include "LocatableFile.h"
#include "Team.h"
#include "TeamDebugInfo.h"


/**
 * @brief Constructs an Image for @a team described by @a imageInfo.
 *
 * @param team      Owning Team. Not referenced (Team owns its Images).
 * @param imageInfo Snapshot describing the loaded image.
 * @param imageFile Optional LocatableFile for the on-disk binary; reference
 *                  is acquired when non-NULL.
 */
Image::Image(Team* team,const ImageInfo& imageInfo, LocatableFile* imageFile)
	:
	fTeam(team),
	fInfo(imageInfo),
	fImageFile(imageFile),
	fDebugInfo(NULL),
	fDebugInfoState(IMAGE_DEBUG_INFO_NOT_LOADED)
{
	if (fImageFile != NULL)
		fImageFile->AcquireReference();
}


/**
 * @brief Detaches debug info from the Team and releases held references.
 */
Image::~Image()
{
	if (fDebugInfo != NULL) {
		if (fTeam != NULL)
			fTeam->DebugInfo()->RemoveImageDebugInfo(fDebugInfo);
		fDebugInfo->ReleaseReference();
	}
	if (fImageFile != NULL)
		fImageFile->ReleaseReference();
}


/**
 * @brief Performs deferred initialisation of the Image.
 *
 * @return @c B_OK; the base implementation has no further work.
 */
status_t
Image::Init()
{
	return B_OK;
}


/**
 * @brief Tests whether @a address falls within the image's text or data range.
 *
 * @param address Target-space address to test.
 * @return       True if @a address is inside the text or data segment.
 */
bool
Image::ContainsAddress(target_addr_t address) const
{
	return (address >= fInfo.TextBase()
			&& address < fInfo.TextBase() + fInfo.TextSize())
		|| (address >= fInfo.DataBase()
			&& address < fInfo.DataBase() + fInfo.DataSize());
}


/**
 * @brief Installs the per-image debug info and announces the change.
 *
 * Detaches and releases any previously held ImageDebugInfo, then registers
 * the new one with the Team's debug-info aggregator. On registration
 * failure the state is recorded as @c IMAGE_DEBUG_INFO_UNAVAILABLE so the
 * UI surfaces the failure. Listeners are notified after the swap.
 *
 * @param debugInfo Replacement ImageDebugInfo, or NULL to detach.
 * @param state     Debug-info loading state to record.
 * @return         @c B_OK on success, otherwise the error from the
 *                  aggregator's add path.
 */
status_t
Image::SetImageDebugInfo(ImageDebugInfo* debugInfo,
	image_debug_info_state state)
{
	if (debugInfo == fDebugInfo && state == fDebugInfoState)
		return B_OK;

	if (fDebugInfo != NULL) {
		fTeam->DebugInfo()->RemoveImageDebugInfo(fDebugInfo);
		fDebugInfo->ReleaseReference();
	}

	fDebugInfo = debugInfo;
	fDebugInfoState = state;

	status_t error = B_OK;
	if (fDebugInfo != NULL) {
		error = fTeam->DebugInfo()->AddImageDebugInfo(fDebugInfo);
		if (error == B_OK) {
			fDebugInfo->AcquireReference();
		} else {
			fDebugInfo = NULL;
			fDebugInfoState = IMAGE_DEBUG_INFO_UNAVAILABLE;
		}
	}

	// notify listeners
	fTeam->NotifyImageDebugInfoChanged(this);

	return error;
}
