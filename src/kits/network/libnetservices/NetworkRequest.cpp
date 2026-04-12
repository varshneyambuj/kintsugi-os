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
 *   Copyright 2010-2014 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christophe Huriaux, c.huriaux@gmail.com
 *       Niels Sascha Reedijk, niels.reedijk@gmail.com
 *       Adrien Destugues, pulkomandy@pulkomandy.tk
 */


/**
 * @file NetworkRequest.cpp
 * @brief Implementation of BNetworkRequest, the socket-based URL request base class.
 *
 * Extends BUrlRequest with a BAbstractSocket member and adds DNS resolution,
 * socket timeout control, SIGUSR1-based blocking-syscall interruption, and a
 * line-oriented read helper used by HTTP and Gopher protocol loops.
 *
 * @see BUrlRequest, BGopherRequest, BHttpRequest
 */


#include <NetworkRequest.h>

#include <AbstractSocket.h>

using namespace BPrivate::Network;


/**
 * @brief Construct a BNetworkRequest with a null socket pointer.
 *
 * All parameters are forwarded to BUrlRequest; fSocket is initialised to NULL
 * and must be allocated by the concrete subclass before _ProtocolLoop() runs.
 *
 * @param url           The URL to retrieve.
 * @param output        BDataIO sink for the response body.
 * @param listener      Lifecycle callback listener, or NULL.
 * @param context       URL context supplying cookies and proxy settings.
 * @param threadName    Name for the background protocol thread.
 * @param protocolName  Human-readable protocol identifier string.
 */
BNetworkRequest::BNetworkRequest(const BUrl& url, BDataIO* output,
	BUrlProtocolListener* listener, BUrlContext* context,
	const char* threadName, const char* protocolName)
	:
	BUrlRequest(url, output, listener, context, threadName, protocolName),
	fSocket(NULL)
{
}


/**
 * @brief Stop the request by signalling the thread and waiting for it to exit.
 *
 * Sends SIGUSR1 to the protocol thread to unblock any blocking socket
 * syscall (connect, read, write), then waits for the thread to terminate.
 *
 * @return B_OK on success, or an error code if the thread cannot be stopped.
 */
status_t
BNetworkRequest::Stop()
{
	status_t threadStatus = BUrlRequest::Stop();

	if (threadStatus != B_OK)
		return threadStatus;

	send_signal(fThreadId, SIGUSR1); // unblock blocking syscalls.
	wait_for_thread(fThreadId, &threadStatus);
	return threadStatus;
}


/**
 * @brief Set the socket read/write timeout.
 *
 * Has no effect if the socket has not yet been created.
 *
 * @param timeout  The timeout in microseconds to pass to the socket.
 */
void
BNetworkRequest::SetTimeout(bigtime_t timeout)
{
	if (fSocket != NULL)
		fSocket->SetTimeout(timeout);
}


/**
 * @brief Resolve a host name and port to a BNetworkAddress for connecting.
 *
 * Calls the HostnameResolved listener hook after a successful resolution.
 *
 * @param host  The hostname or IP address string to resolve.
 * @param port  The TCP port number to associate with the address.
 * @return true if resolution succeeded, false if BNetworkAddress::InitCheck()
 *         is not B_OK.
 */
bool
BNetworkRequest::_ResolveHostName(BString host, uint16_t port)
{
	_EmitDebug(B_URL_PROTOCOL_DEBUG_TEXT, "Resolving %s",
		fUrl.UrlString().String());

	fRemoteAddr = BNetworkAddress(host, port);
	if (fRemoteAddr.InitCheck() != B_OK)
		return false;

	//! ProtocolHook:HostnameResolved
	if (fListener != NULL)
		fListener->HostnameResolved(this, fRemoteAddr.ToString().String());

	_EmitDebug(B_URL_PROTOCOL_DEBUG_TEXT, "Hostname resolved to: %s",
		fRemoteAddr.ToString().String());

	return true;
}


/**
 * @brief Empty signal handler used to make SIGUSR1 unblock blocking syscalls.
 *
 * Installed by _ProtocolSetup() so that SIGUSR1 interrupts connect/read/write
 * without terminating the process.
 *
 * @param  (unused) The signal number.
 */
static void
empty(int)
{
}


/**
 * @brief Install a SIGUSR1 handler to enable signal-based unblocking.
 *
 * Called from the protocol thread entry point before _ProtocolLoop() runs.
 * Without this setup, sending SIGUSR1 from Stop() would kill the process
 * instead of interrupting the blocking socket call.
 */
void
BNetworkRequest::_ProtocolSetup()
{
	// Setup an (empty) signal handler so we can be stopped by a signal,
	// without the whole process being killed.
	// TODO make connect() properly unlock when close() is called on the
	// socket, and remove this.
	struct sigaction action;
	action.sa_handler = empty;
	action.sa_mask = 0;
	action.sa_flags = 0;
	sigaction(SIGUSR1, &action, NULL);
}


/**
 * @brief Extract the next CRLF-terminated line from fInputBuffer.
 *
 * Scans fInputBuffer for a newline character, copies the characters up to
 * (but not including) the newline into \a destString, strips a trailing '\r'
 * if present, and removes the consumed bytes from the buffer.
 *
 * @param destString  Output BString that receives the extracted line content.
 * @return B_OK if a complete line was found, B_ERROR if no newline is present
 *         yet (more data needed), or B_NO_MEMORY on allocation failure.
 */
status_t
BNetworkRequest::_GetLine(BString& destString)
{
	// Find a complete line in inputBuffer
	uint32 characterIndex = 0;

	while ((characterIndex < fInputBuffer.Size())
		&& ((fInputBuffer.Data())[characterIndex] != '\n'))
		characterIndex++;

	if (characterIndex == fInputBuffer.Size())
		return B_ERROR;

	char* temporaryBuffer = new(std::nothrow) char[characterIndex + 1];
	if (temporaryBuffer == NULL)
		return B_NO_MEMORY;

	fInputBuffer.RemoveData(temporaryBuffer, characterIndex + 1);

	// Strip end-of-line character(s)
	if (characterIndex != 0 && temporaryBuffer[characterIndex - 1] == '\r')
		destString.SetTo(temporaryBuffer, characterIndex - 1);
	else
		destString.SetTo(temporaryBuffer, characterIndex);

	delete[] temporaryBuffer;
	return B_OK;
}

