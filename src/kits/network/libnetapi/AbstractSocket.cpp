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
 *   Copyright 2011, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */

/** @file AbstractSocket.cpp
 *  @brief Base class for BSD-style socket wrappers, providing common
 *         bind/connect/listen state tracking and timeout handling. */


#include <AbstractSocket.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/time.h>


//#define TRACE_SOCKET
#ifdef TRACE_SOCKET
#	define TRACE(x...) printf(x)
#else
#	define TRACE(x...) ;
#endif


/** @brief Constructs an unconnected, unbound abstract socket.
 *         InitCheck() will return B_NO_INIT until the socket is opened. */
BAbstractSocket::BAbstractSocket()
	:
	fInitStatus(B_NO_INIT),
	fSocket(-1),
	fIsBound(false),
	fIsConnected(false),
	fIsListening(false)
{
}


/** @brief Copy constructor that duplicates the underlying file descriptor.
 *  @param other The source socket to copy state from. The new object owns
 *               an independent dup()'d file descriptor. */
BAbstractSocket::BAbstractSocket(const BAbstractSocket& other)
	:
	fInitStatus(other.fInitStatus),
	fLocal(other.fLocal),
	fPeer(other.fPeer),
	fIsConnected(other.fIsConnected),
	fIsListening(other.fIsListening)
{
	fSocket = dup(other.fSocket);
	if (fSocket < 0)
		fInitStatus = errno;
}


/** @brief Destructor. Ensures the socket is disconnected and closed. */
BAbstractSocket::~BAbstractSocket()
{
	Disconnect();
}


/** @brief Returns the initialisation status of the socket.
 *  @return B_OK if successfully opened/bound/connected, or an error code. */
status_t
BAbstractSocket::InitCheck() const
{
	return fInitStatus;
}


/** @brief Reports whether the socket is bound to a local address.
 *  @return true if Bind() has been successfully called. */
bool
BAbstractSocket::IsBound() const
{
	return fIsBound;
}


/** @brief Reports whether the socket is in the listening state.
 *  @return true if Listen() has been successfully called. */
bool
BAbstractSocket::IsListening() const
{
	return fIsListening;
}


/** @brief Reports whether the socket is connected to a remote peer.
 *  @return true if Connect() has been successfully called. */
bool
BAbstractSocket::IsConnected() const
{
	return fIsConnected;
}


/** @brief Places the socket in the listening state for incoming connections.
 *  @param backlog Maximum length of the pending-connection queue.
 *  @return B_OK on success, B_NO_INIT if not previously bound, or an errno
 *          value on failure. */
status_t
BAbstractSocket::Listen(int backlog)
{
	if (!fIsBound)
		return B_NO_INIT;

	if (listen(Socket(), backlog) != 0)
		return fInitStatus = errno;

	fIsListening = true;
	return B_OK;
}


/** @brief Closes the underlying file descriptor and clears connection state. */
void
BAbstractSocket::Disconnect()
{
	if (fSocket < 0)
		return;

	TRACE("%p: BAbstractSocket::Disconnect()\n", this);

	close(fSocket);
	fSocket = -1;
	fIsConnected = false;
	fIsBound = false;
}


/** @brief Sets the send and receive timeout for blocking socket operations.
 *  @param timeout Timeout in microseconds. Values < 0 are clamped to 0.
 *  @return B_OK on success, or an errno value from setsockopt(). */
status_t
BAbstractSocket::SetTimeout(bigtime_t timeout)
{
	if (timeout < 0)
		timeout = 0;

	struct timeval tv;
	tv.tv_sec = timeout / 1000000LL;
	tv.tv_usec = timeout % 1000000LL;

	if (setsockopt(fSocket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(timeval)) != 0
		|| setsockopt(fSocket, SOL_SOCKET, SO_RCVTIMEO, &tv,
			sizeof(timeval)) != 0) {
		return errno;
	}

	return B_OK;
}


/** @brief Returns the current send timeout in microseconds.
 *  @return The timeout value, or B_INFINITE_TIMEOUT on error. */
bigtime_t
BAbstractSocket::Timeout() const
{
	struct timeval tv;
	socklen_t size = sizeof(tv);
	if (getsockopt(fSocket, SOL_SOCKET, SO_SNDTIMEO, &tv, &size) != 0)
		return B_INFINITE_TIMEOUT;

	return tv.tv_sec * 1000000LL + tv.tv_usec;
}


/** @brief Returns the local address the socket is bound to. */
const BNetworkAddress&
BAbstractSocket::Local() const
{
	return fLocal;
}


/** @brief Returns the remote peer address the socket is connected to. */
const BNetworkAddress&
BAbstractSocket::Peer() const
{
	return fPeer;
}


/** @brief Returns the maximum number of bytes that a single send/recv call
 *         can transfer for this socket type.
 *  @return SSIZE_MAX for the generic base implementation. */
size_t
BAbstractSocket::MaxTransmissionSize() const
{
	return SSIZE_MAX;
}


/** @brief Blocks until the socket has data available for reading.
 *  @param timeout Maximum time to wait in microseconds, or B_INFINITE_TIMEOUT.
 *  @return B_OK if readable, B_TIMED_OUT on timeout, or an errno code. */
status_t
BAbstractSocket::WaitForReadable(bigtime_t timeout) const
{
	return _WaitFor(POLLIN, timeout);
}


/** @brief Blocks until the socket has buffer space available for writing.
 *  @param timeout Maximum time to wait in microseconds, or B_INFINITE_TIMEOUT.
 *  @return B_OK if writable, B_TIMED_OUT on timeout, or an errno code. */
status_t
BAbstractSocket::WaitForWritable(bigtime_t timeout) const
{
	return _WaitFor(POLLOUT, timeout);
}


/** @brief Returns the underlying POSIX socket file descriptor.
 *  @return The file descriptor, or -1 if the socket is not open. */
int
BAbstractSocket::Socket() const
{
	return fSocket;
}


//	#pragma mark - protected


/** @brief Opens (if necessary) and binds the socket to a local address.
 *  @param local     The local address to bind to.
 *  @param reuseAddr If true, sets SO_REUSEADDR before binding.
 *  @param type      Socket type to pass to socket() if a new fd must be opened.
 *  @return B_OK on success, or an errno value on failure. */
status_t
BAbstractSocket::Bind(const BNetworkAddress& local, bool reuseAddr, int type)
{
	fInitStatus = _OpenIfNeeded(local.Family(), type);
	if (fInitStatus != B_OK)
		return fInitStatus;

	if (reuseAddr) {
		int value = 1;
		if (setsockopt(Socket(), SOL_SOCKET, SO_REUSEADDR, &value,
				sizeof(value)) != 0) {
			return fInitStatus = errno;
		}
	}

	if (bind(fSocket, local, local.Length()) != 0)
		return fInitStatus = errno;

	fIsBound = true;
	_UpdateLocalAddress();
	return B_OK;
}


/** @brief Opens the socket if needed and connects it to a remote peer.
 *  @param peer    The remote peer address to connect to.
 *  @param type    Socket type to pass to socket() if opening a new fd.
 *  @param timeout Send/receive timeout to configure before connecting.
 *  @return B_OK on successful connection, or an errno value on failure. */
status_t
BAbstractSocket::Connect(const BNetworkAddress& peer, int type,
	bigtime_t timeout)
{
	Disconnect();

	fInitStatus = _OpenIfNeeded(peer.Family(), type);
	if (fInitStatus == B_OK)
		fInitStatus = SetTimeout(timeout);

	if (fInitStatus == B_OK && !IsBound()) {
		// Bind to ADDR_ANY, if the address family supports it
		BNetworkAddress local;
		if (local.SetToWildcard(peer.Family()) == B_OK)
			fInitStatus = Bind(local, true);
	}
	if (fInitStatus != B_OK)
		return fInitStatus;

	BNetworkAddress normalized = peer;
	if (connect(fSocket, normalized, normalized.Length()) != 0) {
		TRACE("%p: connecting to %s: %s\n", this,
			normalized.ToString().c_str(), strerror(errno));
		return fInitStatus = errno;
	}

	fIsConnected = true;
	fPeer = normalized;
	_UpdateLocalAddress();

	TRACE("%p: connected to %s (local %s)\n", this, peer.ToString().c_str(),
		fLocal.ToString().c_str());

	return fInitStatus = B_OK;
}


/** @brief Accepts the next pending connection on a listening socket.
 *  @param _acceptedSocket On success, set to the new accepted file descriptor.
 *  @param _peer           On success, set to the remote peer address.
 *  @return B_OK on success, or an errno code on failure. */
status_t
BAbstractSocket::AcceptNext(int& _acceptedSocket, BNetworkAddress& _peer)
{
	sockaddr_storage source;
	socklen_t sourceLength = sizeof(sockaddr_storage);

	int fd = accept(fSocket, (sockaddr*)&source, &sourceLength);
	if (fd < 0)
		return fd;

	_peer.SetTo(source);
	_acceptedSocket = fd;
	return B_OK;
}


//	#pragma mark - private


/** @brief Lazily opens the underlying socket() if no file descriptor exists.
 *  @param family Address family (AF_INET, AF_INET6, AF_UNIX, ...).
 *  @param type   Socket type (SOCK_STREAM, SOCK_DGRAM, ...).
 *  @return B_OK if the fd is already open or was successfully created. */
status_t
BAbstractSocket::_OpenIfNeeded(int family, int type)
{
	if (fSocket >= 0)
		return B_OK;

	fSocket = socket(family, type, 0);
	if (fSocket < 0)
		return errno;

	TRACE("%p: socket opened FD %d\n", this, fSocket);
	return B_OK;
}


/** @brief Refreshes fLocal from getsockname() after bind or connect.
 *  @return B_OK on success, or an errno value on failure. */
status_t
BAbstractSocket::_UpdateLocalAddress()
{
	socklen_t localLength = sizeof(sockaddr_storage);
	if (getsockname(fSocket, fLocal, &localLength) != 0)
		return errno;

	return B_OK;
}


/** @brief Waits for a poll event on the socket's file descriptor.
 *  @param flags   Poll events to wait for (e.g. POLLIN, POLLOUT).
 *  @param timeout Maximum wait in microseconds, or B_INFINITE_TIMEOUT.
 *  @return B_OK if the event occurred, B_TIMED_OUT / B_WOULD_BLOCK on no
 *          event, or an errno value on poll() failure. */
status_t
BAbstractSocket::_WaitFor(int flags, bigtime_t timeout) const
{
	if (fInitStatus != B_OK)
		return fInitStatus;

	int millis = 0;
	if (timeout == B_INFINITE_TIMEOUT)
		millis = -1;
	if (timeout > 0)
		millis = timeout / 1000;

	struct pollfd entry;
	entry.fd = Socket();
	entry.events = flags;

	int result;
	do {
		result = poll(&entry, 1, millis);
	} while (result == -1 && errno == EINTR);
	if (result < 0)
		return errno;
	if (result == 0)
		return millis > 0 ? B_TIMED_OUT : B_WOULD_BLOCK;

	return B_OK;
}
