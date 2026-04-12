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
 * @file UrlRequest.cpp
 * @brief Implementation of BUrlRequest, the abstract URL request base class.
 *
 * Manages a background protocol thread that runs _ProtocolLoop(), exposes
 * setters for URL, context, listener, and output, and notifies the listener
 * when the request completes. A process-wide default BUrlContext is used
 * when none is provided by the caller.
 *
 * @see BNetworkRequest, BUrlProtocolListener, BUrlContext
 */


#include <UrlRequest.h>
#include <Debug.h>
#include <stdio.h>

using namespace BPrivate::Network;


static BReference<BUrlContext> gDefaultContext = new(std::nothrow) BUrlContext();


/**
 * @brief Construct a BUrlRequest for the given URL.
 *
 * If \a context is NULL, the process-wide default BUrlContext is used.
 *
 * @param url           The URL to retrieve.
 * @param output        BDataIO sink that receives the response body.
 * @param listener      Lifecycle callback listener, or NULL.
 * @param context       URL context supplying cookies and proxy settings, or NULL.
 * @param threadName    Name for the background protocol thread.
 * @param protocolName  Human-readable protocol identifier string.
 */
BUrlRequest::BUrlRequest(const BUrl& url, BDataIO* output,
	BUrlProtocolListener* listener, BUrlContext* context,
	const char* threadName, const char* protocolName)
	:
	fUrl(url),
	fContext(context),
	fListener(listener),
	fOutput(output),
	fQuit(false),
	fRunning(false),
	fThreadStatus(B_NO_INIT),
	fThreadId(0),
	fThreadName(threadName),
	fProtocol(protocolName)
{
	if (fContext == NULL)
		fContext = gDefaultContext;
}


/**
 * @brief Destructor — stops the request thread if it is still running.
 */
BUrlRequest::~BUrlRequest()
{
	Stop();
}


// #pragma mark URL protocol thread management


/**
 * @brief Spawn and resume the background protocol thread.
 *
 * If the thread is already running, the existing thread ID is returned
 * immediately. Otherwise a new thread is spawned at B_NORMAL_PRIORITY and
 * resumed.
 *
 * @return The thread_id of the protocol thread, or a negative error code if
 *         spawning or resuming the thread fails.
 */
thread_id
BUrlRequest::Run()
{
	// Thread already running
	if (fRunning) {
		PRINT(("BUrlRequest::Run() : Oops, already running ! "
			"[urlProtocol=%p]!\n", this));
		return fThreadId;
	}

	fThreadId = spawn_thread(BUrlRequest::_ThreadEntry, fThreadName,
		B_NORMAL_PRIORITY, this);

	if (fThreadId < B_OK)
		return fThreadId;

	fRunning = true;

	status_t launchErr = resume_thread(fThreadId);
	if (launchErr < B_OK) {
		PRINT(("BUrlRequest::Run() : Failed to resume thread %" B_PRId32 "\n",
			fThreadId));
		return launchErr;
	}

	return fThreadId;
}


/**
 * @brief Signal the protocol thread to stop at the next opportunity.
 *
 * Sets fQuit to true so that the protocol loop exits cleanly. Does not wait
 * for the thread to actually finish — call the subclass Stop() for that.
 *
 * @return B_OK if the thread was running and the quit flag was set, or
 *         B_ERROR if the thread was not running.
 */
status_t
BUrlRequest::Stop()
{
	if (!fRunning)
		return B_ERROR;

	fQuit = true;
	return B_OK;
}


// #pragma mark URL protocol parameters modification


/**
 * @brief Change the URL for this request.
 *
 * Only allowed when the request is not currently running.
 *
 * @param url  The new URL to use.
 * @return B_OK on success, or B_ERROR if the request is already running.
 */
status_t
BUrlRequest::SetUrl(const BUrl& url)
{
	// We should avoid to change URL while the thread is running ...
	if (IsRunning())
		return B_ERROR;

	fUrl = url;
	return B_OK;
}


/**
 * @brief Set the URL context for this request.
 *
 * Only allowed when the request is not running. Passing NULL restores the
 * process-wide default context.
 *
 * @param context  The new BUrlContext to use, or NULL for the default.
 * @return B_OK on success, or B_ERROR if the request is already running.
 */
status_t
BUrlRequest::SetContext(BUrlContext* context)
{
	if (IsRunning())
		return B_ERROR;

	if (context == NULL)
		fContext = gDefaultContext;
	else
		fContext = context;

	return B_OK;
}


/**
 * @brief Replace the protocol listener.
 *
 * Only allowed when the request is not currently running.
 *
 * @param listener  The new BUrlProtocolListener, or NULL to disable callbacks.
 * @return B_OK on success, or B_ERROR if the request is already running.
 */
status_t
BUrlRequest::SetListener(BUrlProtocolListener* listener)
{
	if (IsRunning())
		return B_ERROR;

	fListener = listener;
	return B_OK;
}


/**
 * @brief Replace the output BDataIO sink.
 *
 * Only allowed when the request is not currently running.
 *
 * @param output  The new BDataIO that receives the response body.
 * @return B_OK on success, or B_ERROR if the request is already running.
 */
status_t
BUrlRequest::SetOutput(BDataIO* output)
{
	if (IsRunning())
		return B_ERROR;

	fOutput = output;
	return B_OK;
}


// #pragma mark URL protocol parameters access


/**
 * @brief Return the URL associated with this request.
 *
 * @return A const reference to the request BUrl.
 */
const BUrl&
BUrlRequest::Url() const
{
	return fUrl;
}


/**
 * @brief Return the URL context associated with this request.
 *
 * @return A pointer to the active BUrlContext (never NULL).
 */
BUrlContext*
BUrlRequest::Context() const
{
	return fContext;
}


/**
 * @brief Return the registered protocol listener.
 *
 * @return A pointer to the BUrlProtocolListener, or NULL if none is set.
 */
BUrlProtocolListener*
BUrlRequest::Listener() const
{
	return fListener;
}


/**
 * @brief Return the protocol identifier string (e.g. "HTTP", "HTTPS").
 *
 * @return A const reference to the protocol BString.
 */
const BString&
BUrlRequest::Protocol() const
{
	return fProtocol;
}


#ifndef LIBNETAPI_DEPRECATED
/**
 * @brief Return the output BDataIO sink.
 *
 * @return A pointer to the output BDataIO, or NULL if none was set.
 */
BDataIO*
BUrlRequest::Output() const
{
	return fOutput;
}
#endif

// #pragma mark URL protocol informations


/**
 * @brief Return whether the protocol thread is currently running.
 *
 * @return true if the thread is alive and has not yet set fRunning to false.
 */
bool
BUrlRequest::IsRunning() const
{
	return fRunning;
}


/**
 * @brief Return the last status code set by the protocol thread.
 *
 * @return B_BUSY while running, B_OK after successful completion, or an
 *         error code after a failed request.
 */
status_t
BUrlRequest::Status() const
{
	return fThreadStatus;
}


// #pragma mark Thread management


/**
 * @brief Static thread entry point for the protocol thread.
 *
 * Casts \a arg to BUrlRequest, sets the thread status to B_BUSY, calls
 * _ProtocolSetup() and _ProtocolLoop(), then updates fRunning and
 * fThreadStatus and notifies the listener of completion.
 *
 * @param arg  Pointer to the BUrlRequest owning this thread.
 * @return B_OK always (the actual exit status is stored in fThreadStatus).
 */
/*static*/ int32
BUrlRequest::_ThreadEntry(void* arg)
{
	BUrlRequest* request = reinterpret_cast<BUrlRequest*>(arg);
	request->fThreadStatus = B_BUSY;
	request->_ProtocolSetup();

	status_t protocolLoopExitStatus = request->_ProtocolLoop();

	request->fRunning = false;
	request->fThreadStatus = protocolLoopExitStatus;

	if (request->fListener != NULL) {
		request->fListener->RequestCompleted(request,
			protocolLoopExitStatus == B_OK);
	}

	return B_OK;
}


/**
 * @brief Format and emit a debug message through the registered listener.
 *
 * Formats \a format and the variadic arguments into a fixed-size buffer and
 * passes the result to BUrlProtocolListener::DebugMessage(). Does nothing if
 * fListener is NULL.
 *
 * @param type    The BUrlProtocolDebugMessage category.
 * @param format  printf-style format string for the message.
 * @param ...     Additional arguments for the format string.
 */
void
BUrlRequest::_EmitDebug(BUrlProtocolDebugMessage type,
	const char* format, ...)
{
	if (fListener == NULL)
		return;

	va_list arguments;
	va_start(arguments, format);

	char debugMsg[1024];
	vsnprintf(debugMsg, sizeof(debugMsg), format, arguments);
	fListener->DebugMessage(this, type, debugMsg);
	va_end(arguments);
}
