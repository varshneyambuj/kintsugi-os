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
 *   Copyright 2002-2008, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file NetEndpoint.cpp
 *  @brief Legacy BNetEndpoint implementation providing a BSD-socket style
 *         bind/connect/listen/accept/send/receive API over IPv4. */

#include <Message.h>
#include <NetEndpoint.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <new>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>


/** @brief Constructs an IPv4 endpoint with a default family of AF_INET.
 *  @param type Socket type such as SOCK_STREAM or SOCK_DGRAM. */
BNetEndpoint::BNetEndpoint(int type)
	:
	fStatus(B_NO_INIT),
	fFamily(AF_INET),
	fType(type),
	fProtocol(0),
	fSocket(-1),
	fTimeout(B_INFINITE_TIMEOUT)
{
	_SetupSocket();
}


/** @brief Constructs an endpoint with an explicit address family, type, and protocol.
 *  @param family   Address family (AF_INET, etc).
 *  @param type     Socket type (SOCK_STREAM, SOCK_DGRAM, ...).
 *  @param protocol Transport protocol number (0 for the default). */
BNetEndpoint::BNetEndpoint(int family, int type, int protocol)
	:
	fStatus(B_NO_INIT),
	fFamily(family),
	fType(type),
	fProtocol(protocol),
	fSocket(-1),
	fTimeout(B_INFINITE_TIMEOUT)
{
	_SetupSocket();
}


/** @brief Unarchiving constructor. Restores local/peer address, protocol,
 *         and timeout from fields previously written by Archive(). */
BNetEndpoint::BNetEndpoint(BMessage* archive)
	:
	fStatus(B_NO_INIT),
	fFamily(AF_INET),
	fProtocol(0),
	fSocket(-1),
	fTimeout(B_INFINITE_TIMEOUT)
{
	if (!archive)
		return;

	in_addr addr, peer;
	unsigned short addrPort = 0, peerPort = 0;

	fStatus = archive->FindInt32("_BNetEndpoint_addr_addr",
		(int32 *)&addr.s_addr);
	if (fStatus == B_OK) {
		fStatus = archive->FindInt16("_BNetEndpoint_addr_port",
			(int16 *)&addrPort);
		if (fStatus == B_OK)
			fStatus = fAddr.SetTo(addr, addrPort);
	}

	fStatus = archive->FindInt32("_BNetEndpoint_peer_addr",
		(int32 *)&peer.s_addr);
	if (fStatus == B_OK) {
		fStatus = archive->FindInt16("_BNetEndpoint_peer_port",
			(int16 *)&peerPort);
		if (fStatus == B_OK)
			fStatus = fPeer.SetTo(peer, peerPort);
	}

	fStatus = archive->FindInt64("_BNetEndpoint_timeout", (int64 *)&fTimeout);
	if (fStatus == B_OK)
		fStatus = archive->FindInt32("_BNetEndpoint_proto", (int32 *)&fType);

	if (fStatus == B_OK)
		_SetupSocket();
}


/** @brief Copy constructor. dup()'s the underlying file descriptor so both
 *         endpoints own an independent reference to the same kernel socket. */
BNetEndpoint::BNetEndpoint(const BNetEndpoint& endpoint)
	:
	fStatus(endpoint.fStatus),
	fFamily(endpoint.fFamily),
	fType(endpoint.fType),
	fProtocol(endpoint.fProtocol),
	fSocket(-1),
	fTimeout(endpoint.fTimeout),
	fAddr(endpoint.fAddr),
	fPeer(endpoint.fPeer)

{
	if (endpoint.fSocket >= 0) {
		fSocket = dup(endpoint.fSocket);
		if (fSocket < 0)
			fStatus = errno;
	}
}


/** @brief Private accept-helper constructor used internally by Accept().
 *         Adopts an already-accepted file descriptor along with its local
 *         and peer addresses, without re-opening a new socket. */
BNetEndpoint::BNetEndpoint(const BNetEndpoint& endpoint, int socket,
	const struct sockaddr_in& localAddress,
	const struct sockaddr_in& peerAddress)
	:
	fStatus(endpoint.fStatus),
	fFamily(endpoint.fFamily),
	fType(endpoint.fType),
	fProtocol(endpoint.fProtocol),
	fSocket(socket),
	fTimeout(endpoint.fTimeout),
	fAddr(localAddress),
	fPeer(peerAddress)
{
}


/** @brief Assignment operator. Closes any existing socket and duplicates
 *         the source endpoint's file descriptor and addressing state. */
BNetEndpoint&
BNetEndpoint::operator=(const BNetEndpoint& endpoint)
{
	if (this == &endpoint)
		return *this;

	Close();

	fStatus = endpoint.fStatus;
	fFamily = endpoint.fFamily;
	fType = endpoint.fType;
	fProtocol = endpoint.fProtocol;
	fTimeout = endpoint.fTimeout;
	fAddr = endpoint.fAddr;
	fPeer = endpoint.fPeer;

	fSocket = -1;
	if (endpoint.fSocket >= 0) {
		fSocket = dup(endpoint.fSocket);
		if (fSocket < 0)
			fStatus = errno;
	}

    return *this;
}


/** @brief Destructor. Closes the socket if still open. */
BNetEndpoint::~BNetEndpoint()
{
	if (fSocket >= 0)
		Close();
}


// #pragma mark -


/** @brief Serialises the endpoint's addresses, timeout, and protocol type
 *         into a BMessage.
 *  @param into Destination BMessage.
 *  @param deep Forwarded to BArchivable::Archive().
 *  @return B_OK on success, or an error code on failure. */
status_t
BNetEndpoint::Archive(BMessage* into, bool deep) const
{
	if (!into)
		return B_ERROR;

	status_t status = BArchivable::Archive(into, deep);
	if (status != B_OK)
		return status;

	in_addr addr, peer;
	unsigned short addrPort, peerPort;

	status = fAddr.GetAddr(addr, &addrPort);
	if (status == B_OK) {
		status = into->AddInt32("_BNetEndpoint_addr_addr", addr.s_addr);
		if (status == B_OK)
			status = into->AddInt16("_BNetEndpoint_addr_port", addrPort);
		if (status != B_OK)
			return status;
	}
	status = fPeer.GetAddr(peer, &peerPort);
	if (status == B_OK) {
		status = into->AddInt32("_BNetEndpoint_peer_addr", peer.s_addr);
		if (status == B_OK)
			status = into->AddInt16("_BNetEndpoint_peer_port", peerPort);
		if (status != B_OK)
			return status;
	}

	status = into->AddInt64("_BNetEndpoint_timeout", fTimeout);
	if (status == B_OK)
		status = into->AddInt32("_BNetEndpoint_proto", fType);

	return status;
}


/** @brief BArchivable factory that reconstructs a BNetEndpoint from a BMessage.
 *  @param archive Source BMessage produced by Archive().
 *  @return A new BNetEndpoint on success (owned by the caller), or NULL. */
BArchivable*
BNetEndpoint::Instantiate(BMessage* archive)
{
	if (!archive)
		return NULL;

	if (!validate_instantiation(archive, "BNetEndpoint"))
		return NULL;

	BNetEndpoint* endpoint = new BNetEndpoint(archive);
	if (endpoint && endpoint->InitCheck() == B_OK)
		return endpoint;

	delete endpoint;
	return NULL;
}


// #pragma mark -


/** @brief Returns the endpoint's initialisation status.
 *  @return B_OK if a socket has been successfully opened, B_NO_INIT otherwise. */
status_t
BNetEndpoint::InitCheck() const
{
	return fSocket == -1 ? B_NO_INIT : B_OK;
}


/** @brief Returns the underlying socket file descriptor (or -1 if closed). */
int
BNetEndpoint::Socket() const
{
	return fSocket;
}


/** @brief Returns the local address the endpoint is bound to. */
const BNetAddress&
BNetEndpoint::LocalAddr() const
{
	return fAddr;
}


/** @brief Returns the remote address the endpoint is connected to. */
const BNetAddress&
BNetEndpoint::RemoteAddr() const
{
	return fPeer;
}


/** @brief Replaces the socket type, discarding any existing file descriptor.
 *  @param protocol SOCK_DGRAM or SOCK_STREAM; stored in fType.
 *  @return B_OK on success, or an error code if the new socket cannot be opened. */
status_t
BNetEndpoint::SetProtocol(int protocol)
{
	Close();
	fType = protocol;	// sic (protocol is SOCK_DGRAM or SOCK_STREAM)
	return _SetupSocket();
}


/** @brief Sets a low-level socket option via setsockopt().
 *  @param option Option name (e.g. SO_BROADCAST).
 *  @param level  Protocol level (e.g. SOL_SOCKET).
 *  @param data   Pointer to the option value.
 *  @param length Length of the option value in bytes.
 *  @return B_OK on success, or B_ERROR with Error() updated to errno. */
int
BNetEndpoint::SetOption(int32 option, int32 level,
	const void* data, unsigned int length)
{
	if (fSocket < 0 && _SetupSocket() != B_OK)
		return fStatus;

	if (setsockopt(fSocket, level, option, data, length) < 0) {
		fStatus = errno;
		return B_ERROR;
	}

	return B_OK;
}


/** @brief Toggles the O_NONBLOCK flag on the underlying file descriptor.
 *  @param enable true to switch to non-blocking mode, false for blocking.
 *  @return B_OK on success, or B_ERROR with Error() updated to errno. */
int
BNetEndpoint::SetNonBlocking(bool enable)
{
	if (fSocket < 0 && _SetupSocket() != B_OK)
		return fStatus;

	int flags = fcntl(fSocket, F_GETFL);
	if (flags < 0) {
		fStatus = errno;
		return B_ERROR;
	}

	if (enable)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;

	if (fcntl(fSocket, F_SETFL, flags) < 0) {
		fStatus = errno;
		return B_ERROR;
	}

	return B_OK;
}


/** @brief Toggles the SO_REUSEADDR socket option on the endpoint.
 *  @param enable true to allow reusing a bind address, false to disallow. */
int
BNetEndpoint::SetReuseAddr(bool enable)
{
	if (fSocket < 0 && _SetupSocket() != B_OK)
		return fStatus;

	int onoff = (int) enable;
	return SetOption(SO_REUSEADDR, SOL_SOCKET, &onoff, sizeof(onoff));
}


/** @brief Sets the maximum time to wait for data on Receive() calls.
 *  @param timeout Timeout in microseconds. Negative means infinite. */
void
BNetEndpoint::SetTimeout(bigtime_t timeout)
{
	fTimeout = timeout < 0 ? B_INFINITE_TIMEOUT : timeout;
}


/** @brief Returns the last stored error code (a status_t or errno value). */
int
BNetEndpoint::Error() const
{
	return (int)fStatus;
}


/** @brief Returns a human-readable description of the last error. */
char*
BNetEndpoint::ErrorStr() const
{
	return strerror(fStatus);
}


// #pragma mark -


/** @brief Closes the underlying socket and resets the init status to
 *         B_NO_INIT. Safe to call repeatedly. */
void
BNetEndpoint::Close()
{
	if (fSocket >= 0)
		close(fSocket);

	fSocket = -1;
	fStatus = B_NO_INIT;
}


/** @brief Binds the socket to a local BNetAddress.
 *  @param address Local address to bind to.
 *  @return B_OK on success, or B_ERROR (the errno is stored in fStatus). */
status_t
BNetEndpoint::Bind(const BNetAddress& address)
{
	if (fSocket < 0 && _SetupSocket() != B_OK)
		return fStatus;

	struct sockaddr_in addr;
	status_t status = address.GetAddr(addr);
	if (status != B_OK)
		return status;

	if (bind(fSocket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fStatus = errno;
		Close();
		return B_ERROR;
	}

	socklen_t addrSize = sizeof(addr);
	if (getsockname(fSocket, (struct sockaddr *)&addr, &addrSize) < 0) {
		fStatus = errno;
		Close();
		return B_ERROR;
	}

	fAddr.SetTo(addr);
	return B_OK;
}


/** @brief Convenience overload that binds to INADDR_ANY on the given port.
 *  @param port Port number in host byte order. */
status_t
BNetEndpoint::Bind(int port)
{
	BNetAddress addr(INADDR_ANY, port);
	return Bind(addr);
}


/** @brief Connects the socket to the given remote address.
 *  @param address Remote peer address.
 *  @return B_OK on success, or B_ERROR (the errno is stored in fStatus). */
status_t
BNetEndpoint::Connect(const BNetAddress& address)
{
	if (fSocket < 0 && _SetupSocket() != B_OK)
		return fStatus;

	sockaddr_in addr;
	if (address.GetAddr(addr) != B_OK)
		return B_ERROR;

	if (connect(fSocket, (sockaddr *) &addr, sizeof(addr)) < 0) {
		Close();
		fStatus = errno;
		return B_ERROR;
	}

	socklen_t addrSize = sizeof(addr);
	if (getpeername(fSocket, (sockaddr *) &addr, &addrSize) < 0) {
		Close();
		fStatus = errno;
		return B_ERROR;
	}
	fPeer.SetTo(addr);
	return B_OK;
}


/** @brief Convenience overload that resolves @a hostname and connects.
 *  @param hostname Hostname or dotted-quad string.
 *  @param port     Port number in host byte order. */
status_t
BNetEndpoint::Connect(const char *hostname, int port)
{
	BNetAddress addr(hostname, port);
	return Connect(addr);
}


/** @brief Places the socket in the listening state.
 *  @param backlog Maximum pending-connection queue depth.
 *  @return B_OK on success, B_ERROR on failure. */
status_t
BNetEndpoint::Listen(int backlog)
{
	if (fSocket < 0 && _SetupSocket() != B_OK)
		return fStatus;

	if (listen(fSocket, backlog) < 0) {
		Close();
		fStatus = errno;
		return B_ERROR;
	}
	return B_OK;
}


/** @brief Accepts the next pending connection on a listening endpoint.
 *  @param timeout Maximum time to wait, in milliseconds. Negative means
 *                 wait indefinitely.
 *  @return A new BNetEndpoint owned by the caller, or NULL on timeout or
 *          error. */
BNetEndpoint*
BNetEndpoint::Accept(int32 timeout)
{
	if (!IsDataPending(timeout < 0 ? B_INFINITE_TIMEOUT : 1000LL * timeout))
		return NULL;

	struct sockaddr_in peerAddress;
	socklen_t peerAddressSize = sizeof(peerAddress);

	int socket
		= accept(fSocket, (struct sockaddr *)&peerAddress, &peerAddressSize);
	if (socket < 0) {
		Close();
		fStatus = errno;
		return NULL;
	}

	struct sockaddr_in localAddress;
	socklen_t localAddressSize = sizeof(localAddress);
	if (getsockname(socket, (struct sockaddr *)&localAddress,
			&localAddressSize) < 0) {
		close(socket);
		fStatus = errno;
		return NULL;
	}

	BNetEndpoint* endpoint = new (std::nothrow) BNetEndpoint(*this, socket,
		localAddress, peerAddress);
	if (endpoint == NULL) {
		close(socket);
		fStatus = B_NO_MEMORY;
		return NULL;
	}

	return endpoint;
}


// #pragma mark -


/** @brief Checks whether the socket has incoming data available.
 *  @param timeout Maximum time to wait in microseconds. Negative values
 *                 (and values overflowing INT32_MAX seconds) mean wait
 *                 indefinitely.
 *  @return true if data is available before the deadline, false otherwise. */
bool
BNetEndpoint::IsDataPending(bigtime_t timeout)
{
	struct timeval tv;
	fd_set fds;

	FD_ZERO(&fds);
	FD_SET(fSocket, &fds);

	// Make sure the timeout does not overflow. If it does, have an infinite
	// timeout instead. Note that this conveniently includes B_INFINITE_TIMEOUT.
	if (timeout > INT32_MAX * 1000000ll)
		timeout = -1;

	if (timeout >= 0) {
		tv.tv_sec = timeout / 1000000;
		tv.tv_usec = (timeout % 1000000);
	}

	int status;
	do {
		status = select(fSocket + 1, &fds, NULL, NULL,
			timeout >= 0 ? &tv : NULL);
	} while (status == -1 && errno == EINTR);

	if (status < 0) {
		fStatus = errno;
		return false;
	}

	return FD_ISSET(fSocket, &fds);
}


/** @brief Reads bytes from a connected socket into a raw buffer.
 *  @param buffer Destination memory.
 *  @param length Maximum number of bytes to read.
 *  @param flags  Additional flags forwarded to recv().
 *  @return Bytes received, 0 on timeout, or a negative error value. */
int32
BNetEndpoint::Receive(void* buffer, size_t length, int flags)
{
	if (fSocket < 0 && _SetupSocket() != B_OK)
		return fStatus;

	if (fTimeout >= 0 && IsDataPending(fTimeout) == false)
		return 0;

	ssize_t bytesReceived = recv(fSocket, buffer, length, flags);
	if (bytesReceived < 0)
		fStatus = errno;

	return bytesReceived;
}


/** @brief Reads bytes from a connected socket into a BNetBuffer.
 *  @param buffer Destination BNetBuffer; received bytes are appended.
 *  @param length Maximum number of bytes to read.
 *  @param flags  Additional flags forwarded to recv(). */
int32
BNetEndpoint::Receive(BNetBuffer& buffer, size_t length, int flags)
{
	BNetBuffer chunk(length);
	ssize_t bytesReceived = Receive(chunk.Data(), length, flags);
	if (bytesReceived > 0)
		buffer.AppendData(chunk.Data(), bytesReceived);
	return bytesReceived;
}


/** @brief Reads a datagram into a raw buffer and reports the sender address.
 *  @param buffer  Destination memory.
 *  @param length  Maximum number of bytes to read.
 *  @param address On success, set to the sender address.
 *  @param flags   Additional flags forwarded to recvfrom().
 *  @return Bytes received, 0 on timeout, or a negative error value. */
int32
BNetEndpoint::ReceiveFrom(void* buffer, size_t length,
	BNetAddress& address, int flags)
{
	if (fSocket < 0 && _SetupSocket() != B_OK)
		return fStatus;

	if (fTimeout >= 0 && IsDataPending(fTimeout) == false)
		return 0;

	struct sockaddr_in addr;
	socklen_t addrSize = sizeof(addr);

	ssize_t bytesReceived = recvfrom(fSocket, buffer, length, flags,
		(struct sockaddr *)&addr, &addrSize);
	if (bytesReceived < 0)
		fStatus = errno;
	else
		address.SetTo(addr);

	return bytesReceived;
}


/** @brief Reads a datagram into a BNetBuffer and reports the sender address. */
int32
BNetEndpoint::ReceiveFrom(BNetBuffer& buffer, size_t length,
	BNetAddress& address, int flags)
{
	BNetBuffer chunk(length);
	ssize_t bytesReceived = ReceiveFrom(chunk.Data(), length, address, flags);
	if (bytesReceived > 0)
		buffer.AppendData(chunk.Data(), bytesReceived);
	return bytesReceived;
}


/** @brief Sends bytes over a connected socket.
 *  @param buffer Source memory.
 *  @param length Number of bytes to send.
 *  @param flags  Additional flags forwarded to send().
 *  @return Bytes sent, or a negative error value. */
int32
BNetEndpoint::Send(const void* buffer, size_t length, int flags)
{
	if (fSocket < 0 && _SetupSocket() != B_OK)
		return fStatus;

	ssize_t bytesSent = send(fSocket, (const char *) buffer, length, flags);
	if (bytesSent < 0)
		fStatus = errno;

	return bytesSent;
}


/** @brief Sends the full contents of a BNetBuffer over a connected socket. */
int32
BNetEndpoint::Send(BNetBuffer& buffer, int flags)
{
	return Send(buffer.Data(), buffer.Size(), flags);
}


/** @brief Sends a datagram to a specific address.
 *  @param buffer  Source memory.
 *  @param length  Number of bytes to send.
 *  @param address Destination address.
 *  @param flags   Additional flags forwarded to sendto().
 *  @return Bytes sent, or a negative error value. */
int32
BNetEndpoint::SendTo(const void* buffer, size_t length,
	const BNetAddress& address, int flags)
{
	if (fSocket < 0 && _SetupSocket() != B_OK)
		return fStatus;

	struct sockaddr_in addr;
	if (address.GetAddr(addr) != B_OK)
		return B_ERROR;

	ssize_t	bytesSent = sendto(fSocket, buffer, length, flags,
		(struct sockaddr *) &addr, sizeof(addr));
	if (bytesSent < 0)
		fStatus = errno;

	return bytesSent;
}


/** @brief Sends the full contents of a BNetBuffer to a specific address. */
int32
BNetEndpoint::SendTo(BNetBuffer& buffer,
	const BNetAddress& address, int flags)
{
	return SendTo(buffer.Data(), buffer.Size(), address, flags);
}


// #pragma mark -


/** @brief Lazily opens the underlying socket() with the stored
 *         family/type/protocol. Updates fStatus to B_OK or errno. */
status_t
BNetEndpoint::_SetupSocket()
{
	if ((fSocket = socket(fFamily, fType, fProtocol)) < 0)
		fStatus = errno;
	else
		fStatus = B_OK;
	return fStatus;
}


// #pragma mark -

/** @brief Non-const overload of InitCheck() kept for ABI compatibility. */
status_t BNetEndpoint::InitCheck()
{
	return const_cast<const BNetEndpoint*>(this)->InitCheck();
}


/** @brief Non-const overload of LocalAddr() kept for ABI compatibility. */
const BNetAddress& BNetEndpoint::LocalAddr()
{
	return const_cast<const BNetEndpoint*>(this)->LocalAddr();
}


/** @brief Non-const overload of RemoteAddr() kept for ABI compatibility. */
const BNetAddress& BNetEndpoint::RemoteAddr()
{
	return const_cast<const BNetEndpoint*>(this)->RemoteAddr();
}


// #pragma mark -


// These are virtuals, implemented for binary compatibility purpose
void BNetEndpoint::_ReservedBNetEndpointFBCCruft1() {}
void BNetEndpoint::_ReservedBNetEndpointFBCCruft2() {}
void BNetEndpoint::_ReservedBNetEndpointFBCCruft3() {}
void BNetEndpoint::_ReservedBNetEndpointFBCCruft4() {}
void BNetEndpoint::_ReservedBNetEndpointFBCCruft5() {}
void BNetEndpoint::_ReservedBNetEndpointFBCCruft6() {}

