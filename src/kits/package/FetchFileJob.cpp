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
 *   Copyright 2011-2021, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler <axeld@pinc-software.de>
 *       Rene Gollent <rene@gollent.com>
 *       Oliver Tappe <zooey@hirschkaefer.de>
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file FetchFileJob.cpp
 * @brief Job that downloads a file from a URL to a local BEntry using the Network Kit.
 *
 * On Haiku targets FetchFileJob uses BUrlProtocolRoster to create an HTTP(S)
 * request, supports resuming partial downloads via the Range header, and retries
 * on transient I/O errors and timeouts.  On non-Haiku targets the job returns
 * B_UNSUPPORTED.  The download-complete BFS attribute is written on success.
 *
 * @see FetchUtils, DownloadFileRequest
 */


#include "FetchFileJob.h"

#include <stdio.h>
#include <sys/wait.h>

#include <Path.h>

#ifdef HAIKU_TARGET_PLATFORM_HAIKU
#	include <HttpRequest.h>
#	include <UrlRequest.h>
#	include <UrlProtocolRoster.h>
using namespace BPrivate::Network;
#endif

#include "FetchUtils.h"


namespace BPackageKit {

namespace BPrivate {


#ifdef HAIKU_TARGET_PLATFORM_HAIKU

/**
 * @brief Construct the fetch job for the Haiku platform (Network Kit available).
 *
 * @param context      Package-kit context.
 * @param title        Human-readable job title for progress display.
 * @param fileURL      URL of the file to download.
 * @param targetEntry  BEntry specifying the destination file path.
 */
FetchFileJob::FetchFileJob(const BContext& context, const BString& title,
	const BString& fileURL, const BEntry& targetEntry)
	:
	inherited(context, title),
	fFileURL(fileURL),
	fTargetEntry(targetEntry),
	fTargetFile(&targetEntry, B_CREATE_FILE | B_WRITE_ONLY),
	fError(B_ERROR),
	fDownloadProgress(0.0)
{
}


/**
 * @brief Destructor.
 */
FetchFileJob::~FetchFileJob()
{
}


/**
 * @brief Return the current download progress as a fraction in [0, 1].
 *
 * @return Fraction of the file downloaded so far, or 0.0 if unknown.
 */
float
FetchFileJob::DownloadProgress() const
{
	return fDownloadProgress;
}


/**
 * @brief Return the URL being downloaded.
 *
 * @return C-string containing the download URL.
 */
const char*
FetchFileJob::DownloadURL() const
{
	return fFileURL.String();
}


/**
 * @brief Return the local file name of the download target.
 *
 * @return C-string containing the target entry's leaf name.
 */
const char*
FetchFileJob::DownloadFileName() const
{
	return fTargetEntry.Name();
}


/**
 * @brief Return the number of bytes received so far.
 *
 * @return Byte count received in the current download session.
 */
off_t
FetchFileJob::DownloadBytes() const
{
	return fBytes;
}


/**
 * @brief Return the total expected size of the download.
 *
 * @return Total byte count as reported by the server, or 0 if unknown.
 */
off_t
FetchFileJob::DownloadTotalBytes() const
{
	return fTotalBytes;
}


/**
 * @brief Execute the download, retrying on transient errors.
 *
 * Sets the MIME type attribute on the target file, attempts a resumable HTTP
 * download using BUrlProtocolRoster, and loops on B_IO_ERROR or B_DEV_TIMEOUT.
 * On success, writes the download-complete attribute via FetchUtils.
 *
 * @return B_OK on success, or the last error code from the network request.
 */
status_t
FetchFileJob::Execute()
{
	status_t result = fTargetFile.InitCheck();
	if (result != B_OK)
		return result;

	result = FetchUtils::SetFileType(fTargetFile,
		"application/x-vnd.haiku-package");
	if (result != B_OK) {
		fprintf(stderr, "failed to set file type for '%s': %s\n",
			DownloadFileName(), strerror(result));
	}

	do {
		BUrlRequest* request = BUrlProtocolRoster::MakeRequest(fFileURL.String(),
			&fTargetFile, this);
		if (request == NULL)
			return B_BAD_VALUE;

		// Try to resume the download where we left off
		off_t currentPosition;
		BHttpRequest* http = dynamic_cast<BHttpRequest*>(request);
		if (http != NULL && fTargetFile.GetSize(&currentPosition) == B_OK
			&& currentPosition > 0) {
			http->SetRangeStart(currentPosition);
			fTargetFile.Seek(0, SEEK_END);
		}

		thread_id thread = request->Run();
		wait_for_thread(thread, NULL);

		if (fError != B_IO_ERROR && fError != B_DEV_TIMEOUT && fError != B_OK) {
			// Something went wrong with the download and it's not just a
			// timeout. Remove whatever we wrote to the file, since the content
			// returned by the server was probably not part of the file.
			fTargetFile.SetSize(currentPosition);
		}
	} while (fError == B_IO_ERROR || fError == B_DEV_TIMEOUT);

	if (fError == B_OK) {
		result = FetchUtils::MarkDownloadComplete(fTargetFile);
		if (result != B_OK) {
			fprintf(stderr, "failed to mark download '%s' as complete: %s\n",
				DownloadFileName(), strerror(result));
		}
	}

	return fError;
}


/**
 * @brief BUrlRequest progress callback; updates the download-progress fraction.
 *
 * Called by the network stack whenever bytes arrive.  Updates fBytes,
 * fTotalBytes, fDownloadProgress, and notifies state listeners.
 *
 * @param request        The active BUrlRequest.
 * @param bytesReceived  Cumulative bytes received so far.
 * @param bytesTotal     Total expected bytes (0 if unknown).
 */
void
FetchFileJob::DownloadProgress(BUrlRequest*, off_t bytesReceived,
	off_t bytesTotal)
{
	if (bytesTotal != 0) {
		fBytes = bytesReceived;
		fTotalBytes = bytesTotal;
		fDownloadProgress = (float)bytesReceived/bytesTotal;
		NotifyStateListeners();
	}
}


/**
 * @brief BUrlRequest completion callback; translates HTTP status codes to system errors.
 *
 * Maps HTTP client-error, server-error, timeout, and other status codes to
 * appropriate Haiku status_t values and stores the result in fError.
 *
 * @param request  The completed BUrlRequest.
 * @param success  true if the request completed without a network-layer error.
 */
void
FetchFileJob::RequestCompleted(BUrlRequest* request, bool success)
{
	fError = request->Status();

	if (success) {
		const BHttpResult* httpResult = dynamic_cast<const BHttpResult*>
			(&request->Result());
		if (httpResult != NULL) {
			uint16 code = httpResult->StatusCode();
			uint16 codeClass = BHttpRequest::StatusCodeClass(code);

			switch (codeClass) {
				case B_HTTP_STATUS_CLASS_CLIENT_ERROR:
				case B_HTTP_STATUS_CLASS_SERVER_ERROR:
					fError = B_IO_ERROR;
					break;
				default:
					fError = B_OK;
					break;
			}
			switch (code) {
				case B_HTTP_STATUS_OK:
				case B_HTTP_STATUS_PARTIAL_CONTENT:
					fError = B_OK;
					break;
				case B_HTTP_STATUS_REQUEST_TIMEOUT:
				case B_HTTP_STATUS_GATEWAY_TIMEOUT:
					fError = B_DEV_TIMEOUT;
					break;
				case B_HTTP_STATUS_NOT_IMPLEMENTED:
					fError = B_NOT_SUPPORTED;
					break;
				case B_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE:
					fError = B_UNKNOWN_MIME_TYPE;
					break;
				case B_HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE:
					fError = B_RESULT_NOT_REPRESENTABLE; // alias for ERANGE
					break;
				case B_HTTP_STATUS_UNAUTHORIZED:
					fError = B_PERMISSION_DENIED;
					break;
				case B_HTTP_STATUS_FORBIDDEN:
				case B_HTTP_STATUS_METHOD_NOT_ALLOWED:
				case B_HTTP_STATUS_NOT_ACCEPTABLE:
					fError = B_NOT_ALLOWED;
					break;
				case B_HTTP_STATUS_NOT_FOUND:
					fError = B_NAME_NOT_FOUND;
					break;
				case B_HTTP_STATUS_BAD_GATEWAY:
					fError = B_BAD_DATA;
					break;
				default:
					break;
			}
		}
	}
}


/**
 * @brief Remove the target file if the download did not complete successfully.
 *
 * @param jobResult  The status code returned by Execute().
 */
void
FetchFileJob::Cleanup(status_t jobResult)
{
	if (jobResult != B_OK)
		fTargetEntry.Remove();
}


#else // HAIKU_TARGET_PLATFORM_HAIKU


/**
 * @brief Construct the fetch job for non-Haiku platforms (stub implementation).
 *
 * @param context      Package-kit context.
 * @param title        Human-readable job title.
 * @param fileURL      URL of the file to download (unused on non-Haiku).
 * @param targetEntry  Destination BEntry (unused on non-Haiku).
 */
FetchFileJob::FetchFileJob(const BContext& context, const BString& title,
	const BString& fileURL, const BEntry& targetEntry)
	:
	inherited(context, title),
	fFileURL(fileURL),
	fTargetEntry(targetEntry),
	fTargetFile(&targetEntry, B_CREATE_FILE | B_WRITE_ONLY),
	fDownloadProgress(0.0)
{
}


/**
 * @brief Destructor.
 */
FetchFileJob::~FetchFileJob()
{
}


/**
 * @brief Return the current download progress (always 0 on non-Haiku).
 *
 * @return 0.0 — downloads are not supported on non-Haiku targets.
 */
float
FetchFileJob::DownloadProgress() const
{
	return fDownloadProgress;
}


/**
 * @brief Return the download URL string.
 *
 * @return C-string of the URL passed at construction.
 */
const char*
FetchFileJob::DownloadURL() const
{
	return fFileURL.String();
}


/**
 * @brief Return the local destination file name.
 *
 * @return C-string of the target entry's leaf name.
 */
const char*
FetchFileJob::DownloadFileName() const
{
	return fTargetEntry.Name();
}


/**
 * @brief Return bytes downloaded (always 0 on non-Haiku).
 *
 * @return 0 — not applicable on non-Haiku targets.
 */
off_t
FetchFileJob::DownloadBytes() const
{
	return fBytes;
}


/**
 * @brief Return total expected download size (always 0 on non-Haiku).
 *
 * @return 0 — not applicable on non-Haiku targets.
 */
off_t
FetchFileJob::DownloadTotalBytes() const
{
	return fTotalBytes;
}


/**
 * @brief Stub Execute(); always returns B_UNSUPPORTED on non-Haiku platforms.
 *
 * @return B_UNSUPPORTED.
 */
status_t
FetchFileJob::Execute()
{
	return B_UNSUPPORTED;
}


/**
 * @brief Remove the target file if the job did not complete successfully.
 *
 * @param jobResult  The status code returned by Execute().
 */
void
FetchFileJob::Cleanup(status_t jobResult)
{
	if (jobResult != B_OK)
		fTargetEntry.Remove();
}


#endif // HAIKU_TARGET_PLATFORM_HAIKU

}	// namespace BPrivate

}	// namespace BPackageKit
