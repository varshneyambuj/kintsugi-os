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
 *   Copyright 2012-2014, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file LoadImageDebugInfoJob.cpp
 * @brief Job that loads the debug information for an Image.
 *
 * LoadImageDebugInfoJob asks the team's TeamDebugInfo to parse the image's
 * debug data (DWARF, symbol tables, etc.). The resulting ImageDebugInfo is
 * attached to the Image, or the image is marked unavailable on failure. A
 * static helper is also provided to schedule the job idempotently from
 * other code paths that simply need debug info to be present.
 */


#include "Jobs.h"

#include <AutoLocker.h>

#include "Image.h"
#include "ImageDebugInfo.h"
#include "TeamDebugInfo.h"
#include "Team.h"


// #pragma mark - LoadImageDebugInfoJob


/**
 * @brief Construct a job to load debug info for the given image.
 *
 * Acquires a reference on @a image and seeds the user-visible job
 * description used in progress reporting.
 *
 * @param image  Image whose debug info should be loaded.
 */
LoadImageDebugInfoJob::LoadImageDebugInfoJob(Image* image)
	:
	fKey(image, JOB_TYPE_LOAD_IMAGE_DEBUG_INFO),
	fImage(image),
	fState()
{
	fImage->AcquireReference();

	SetDescription("Loading debugging information for %s",
		fImage->Name().String());
}


/**
 * @brief Releases the reference held on the image.
 */
LoadImageDebugInfoJob::~LoadImageDebugInfoJob()
{
	fImage->ReleaseReference();
}


/**
 * @brief Returns the worker-queue key identifying this job.
 *
 * @return Reference to the job key keyed on the image.
 */
const JobKey&
LoadImageDebugInfoJob::Key() const
{
	return fKey;
}


/**
 * @brief Loads the image's debug info and attaches the result.
 *
 * Captures the image info under the team lock, calls
 * TeamDebugInfo::LoadImageDebugInfo(), and then commits the result on the
 * image. If the loader signals @c UserInputRequired the job suspends itself
 * via WaitForUserInput() until the UI provides additional input.
 *
 * @retval B_OK         On success.
 * @return Otherwise the underlying loader error.
 */
status_t
LoadImageDebugInfoJob::Do()
{
	// get an image info for the image
	AutoLocker<Team> locker(fImage->GetTeam());
	ImageInfo imageInfo(fImage->Info());
	locker.Unlock();

	// create the debug info
	ImageDebugInfo* debugInfo;
	status_t error = fImage->GetTeam()->DebugInfo()->LoadImageDebugInfo(
		imageInfo, fImage->ImageFile(), fState, debugInfo);

	// set the result
	locker.Lock();

	if (fState.UserInputRequired()) {
		return WaitForUserInput();
	} else if (error == B_OK) {
		error = fImage->SetImageDebugInfo(debugInfo, IMAGE_DEBUG_INFO_LOADED);
		debugInfo->ReleaseReference();
	} else
		fImage->SetImageDebugInfo(NULL, IMAGE_DEBUG_INFO_UNAVAILABLE);

	return error;
}


/**
 * @brief Ensures debug info is being loaded for @a image.
 *
 * If the image already has loaded debug info it is returned (with reference
 * acquired) through @a _imageDebugInfo. If a load is already in progress the
 * function returns success without scheduling a new job. Otherwise a new
 * LoadImageDebugInfoJob is scheduled and the image is moved into the
 * @c IMAGE_DEBUG_INFO_LOADING state.
 *
 * @param worker            Worker queue used to schedule the new job.
 * @param image             Image whose debug info should be loaded.
 * @param listener          Job listener to attach to the scheduled job.
 * @param _imageDebugInfo   Optional out: receives the already-loaded info,
 *                          or @c NULL when loading is still pending.
 * @retval B_OK         On success or when loading is already pending.
 * @retval B_NO_MEMORY  When the job allocation fails.
 * @retval B_ERROR      When an earlier load attempt has already failed.
 */
/*static*/ status_t
LoadImageDebugInfoJob::ScheduleIfNecessary(Worker* worker, Image* image,
	JobListener* listener, ImageDebugInfo** _imageDebugInfo)
{
	AutoLocker<Team> teamLocker(image->GetTeam());

	// If already loaded, we're done.
	if (image->GetImageDebugInfo() != NULL) {
		if (_imageDebugInfo != NULL) {
			*_imageDebugInfo = image->GetImageDebugInfo();
			(*_imageDebugInfo)->AcquireReference();
		}
		return B_OK;
	}

	// If already loading, the caller has to wait, if desired.
	if (image->ImageDebugInfoState() == IMAGE_DEBUG_INFO_LOADING) {
		if (_imageDebugInfo != NULL)
			*_imageDebugInfo = NULL;
		return B_OK;
	}

	// If an earlier load attempt failed, bail out.
	if (image->ImageDebugInfoState() != IMAGE_DEBUG_INFO_NOT_LOADED)
		return B_ERROR;

	// schedule a job
	LoadImageDebugInfoJob* job = new(std::nothrow) LoadImageDebugInfoJob(
		image);
	if (job == NULL)
		return B_NO_MEMORY;

	status_t error = worker->ScheduleJob(job, listener);
	if (error != B_OK) {
		image->SetImageDebugInfo(NULL, IMAGE_DEBUG_INFO_UNAVAILABLE);
		return error;
	}

	image->SetImageDebugInfo(NULL, IMAGE_DEBUG_INFO_LOADING);

	if (_imageDebugInfo != NULL)
		*_imageDebugInfo = NULL;
	return B_OK;
}
