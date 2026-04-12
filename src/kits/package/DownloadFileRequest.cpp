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
 *   Copyright 2013, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file DownloadFileRequest.cpp
 * @brief Request that downloads a file and optionally validates its checksum.
 *
 * DownloadFileRequest creates a FetchFileJob to download from a URL to a local
 * BEntry, skipping the download if a previous attempt already completed
 * successfully.  When a checksum is provided a ValidateChecksumJob is also
 * queued to verify the downloaded file's integrity.
 *
 * @see FetchFileJob, ValidateChecksumJob, ChecksumAccessors
 */


#include <package/DownloadFileRequest.h>

#include "FetchFileJob.h"
#include "FetchUtils.h"
#include "ValidateChecksumJob.h"


namespace BPackageKit {


using namespace BPrivate;


/**
 * @brief Construct a download request for a given URL and target file.
 *
 * @param context      Package-kit context for temp files and decisions.
 * @param fileURL      Full URL of the file to download.
 * @param targetEntry  BEntry specifying the destination path on disk.
 * @param checksum     Optional hex-encoded SHA-256 checksum; pass an empty
 *                     string to skip checksum validation.
 */
DownloadFileRequest::DownloadFileRequest(const BContext& context,
	const BString& fileURL, const BEntry& targetEntry, const BString& checksum)
	:
	inherited(context),
	fFileURL(fileURL),
	fTargetEntry(targetEntry),
	fChecksum(checksum)
{
	if (fInitStatus == B_OK) {
		if (fFileURL.IsEmpty())
			fInitStatus = B_BAD_VALUE;
		else
			fInitStatus = targetEntry.InitCheck();
	}
}


/**
 * @brief Destructor.
 */
DownloadFileRequest::~DownloadFileRequest()
{
}


/**
 * @brief Build and queue the download and optional validation jobs.
 *
 * If the target file already has the download-complete attribute set by a
 * previous run, the fetch job is skipped.  A checksum validation job is added
 * only when a non-empty checksum was supplied at construction time.
 *
 * @return B_OK if all required jobs were queued successfully, B_NO_INIT if
 *         InitCheck() fails, B_NO_MEMORY on allocation failure, or another
 *         error code on queuing failure.
 */
status_t
DownloadFileRequest::CreateInitialJobs()
{
	status_t error = InitCheck();
	if (error != B_OK)
		return B_NO_INIT;

	if (!FetchUtils::IsDownloadCompleted(BNode(&fTargetEntry))) {
		// create the download job
		FetchFileJob* fetchJob = new (std::nothrow) FetchFileJob(fContext,
			BString("Downloading ") << fFileURL, fFileURL, fTargetEntry);
		if (fetchJob == NULL)
			return B_NO_MEMORY;

		if ((error = QueueJob(fetchJob)) != B_OK) {
			delete fetchJob;
			return error;
		}
	}

	// create the checksum validation job
	if (fChecksum.IsEmpty())
		return B_OK;

	ValidateChecksumJob* validateJob = new (std::nothrow) ValidateChecksumJob(
		fContext, BString("Validating checksum for ") << fFileURL,
		new (std::nothrow) StringChecksumAccessor(fChecksum),
		new (std::nothrow) GeneralFileChecksumAccessor(fTargetEntry, true));

	if (validateJob == NULL)
		return B_NO_MEMORY;

	if ((error = QueueJob(validateJob)) != B_OK) {
		delete validateJob;
		return error;
	}

	return B_OK;
}


}	// namespace BPackageKit
