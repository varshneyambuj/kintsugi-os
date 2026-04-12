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
 *   Copyright 2010 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christophe Huriaux, c.huriaux@gmail.com
 */


/**
 * @file UrlSynchronousRequest.cpp
 * @brief Implementation of BUrlSynchronousRequest, a blocking URL request wrapper.
 *
 * Wraps an existing BUrlRequest and acts as its listener, blocking in
 * WaitUntilCompletion() via a snooze loop until RequestCompleted() is called.
 * This allows callers to perform URL requests synchronously without managing
 * a separate thread.
 *
 * @see BUrlRequest, BUrlProtocolListener
 */

#include <cstdio>

#include <UrlSynchronousRequest.h>

using namespace BPrivate::Network;


#define PRINT(x) printf x;


/**
 * @brief Construct a BUrlSynchronousRequest wrapping \a request.
 *
 * Borrows the URL, output, and context from \a request but sets the listener
 * to NULL initially; Perform() installs this object as the listener before
 * starting the thread.
 *
 * @param request  The BUrlRequest whose Run() will be called by Perform().
 */
BUrlSynchronousRequest::BUrlSynchronousRequest(BUrlRequest& request)
	:
	BUrlRequest(request.Url(), request.Output(), NULL, request.Context(),
		"BUrlSynchronousRequest", request.Protocol()),
	fRequestComplete(false),
	fWrappedRequest(request)
{
}


/**
 * @brief Install this object as the listener and start the wrapped request.
 *
 * Resets fRequestComplete, registers this as the listener on the wrapped
 * request, and calls Run() to spawn the protocol thread.
 *
 * @return B_OK if the thread was successfully started, or the negative
 *         thread_id error code from BUrlRequest::Run().
 */
status_t
BUrlSynchronousRequest::Perform()
{
	fWrappedRequest.SetListener(this);
	fRequestComplete = false;

	thread_id worker = fWrappedRequest.Run();
		// TODO something to do with the thread_id maybe ?

	if (worker < B_OK)
		return worker;
	else
		return B_OK;
}


/**
 * @brief Block until the wrapped request's protocol thread finishes.
 *
 * Polls fRequestComplete in a snooze loop with a 10 ms interval. Callers
 * should call Perform() first and may inspect the wrapped request's result
 * after this method returns.
 *
 * @return B_OK always.
 */
status_t
BUrlSynchronousRequest::WaitUntilCompletion()
{
	while (!fRequestComplete)
		snooze(10000);

	return B_OK;
}


/**
 * @brief Log that the connection was opened (debug only).
 *
 * @param  (unused) The BUrlRequest that opened the connection.
 */
void
BUrlSynchronousRequest::ConnectionOpened(BUrlRequest*)
{
	PRINT(("SynchronousRequest::ConnectionOpened()\n"));
}


/**
 * @brief Log the resolved IP address (debug only).
 *
 * @param  (unused) The BUrlRequest that resolved the hostname.
 * @param ip         The resolved IP address string.
 */
void
BUrlSynchronousRequest::HostnameResolved(BUrlRequest*, const char* ip)
{
	PRINT(("SynchronousRequest::HostnameResolved(%s)\n", ip));
}


/**
 * @brief Log that the response started (debug only).
 *
 * @param  (unused) The BUrlRequest that started receiving the response.
 */
void
BUrlSynchronousRequest::ResponseStarted(BUrlRequest*)
{
	PRINT(("SynchronousRequest::ResponseStarted()\n"));
}


/**
 * @brief Log that headers were received (debug only).
 *
 * @param  (unused) The BUrlRequest that received all response headers.
 */
void
BUrlSynchronousRequest::HeadersReceived(BUrlRequest*)
{
	PRINT(("SynchronousRequest::HeadersReceived()\n"));
}


/**
 * @brief Log the number of bytes written to output (debug only).
 *
 * @param caller        The BUrlRequest that wrote data.
 * @param bytesWritten  The number of bytes written in this chunk.
 */
void
BUrlSynchronousRequest::BytesWritten(BUrlRequest* caller, size_t bytesWritten)
{
	PRINT(("SynchronousRequest::BytesWritten(%" B_PRIdSSIZE ")\n",
		bytesWritten));
}


/**
 * @brief Log download progress (debug only).
 *
 * @param  (unused)      The BUrlRequest making download progress.
 * @param bytesReceived  Total bytes received so far.
 * @param bytesTotal     Total expected bytes, or 0 if unknown.
 */
void
BUrlSynchronousRequest::DownloadProgress(BUrlRequest*,
	off_t bytesReceived, off_t bytesTotal)
{
	PRINT(("SynchronousRequest::DownloadProgress(%" B_PRIdOFF ", %" B_PRIdOFF
		")\n", bytesReceived, bytesTotal));
}


/**
 * @brief Log upload progress (debug only).
 *
 * @param  (unused)  The BUrlRequest making upload progress.
 * @param bytesSent  Total bytes sent so far.
 * @param bytesTotal Total expected bytes to send.
 */
void
BUrlSynchronousRequest::UploadProgress(BUrlRequest*, off_t bytesSent,
	off_t bytesTotal)
{
	PRINT(("SynchronousRequest::UploadProgress(%" B_PRIdOFF ", %" B_PRIdOFF
		")\n", bytesSent, bytesTotal));
}


/**
 * @brief Mark the request as complete and log the result (debug only).
 *
 * Sets fRequestComplete to true, which unblocks WaitUntilCompletion().
 *
 * @param caller   The BUrlRequest that completed.
 * @param success  true if the request completed without error.
 */
void
BUrlSynchronousRequest::RequestCompleted(BUrlRequest* caller, bool success)
{
	PRINT(("SynchronousRequest::RequestCompleted(%s) : %s\n", (success?"true":"false"),
		strerror(caller->Status())));
	fRequestComplete = true;
}
