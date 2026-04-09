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

/** @file Socket.cpp
 *  @brief Connection-oriented (SOCK_STREAM) socket wrapper providing
 *         bind/connect/accept and BDataIO-compatible read/write. */


#include <Socket.h>

#include <errno.h>


//#define TRACE_SOCKET
#ifdef TRACE_SOCKET
#	define TRACE(x...) printf(x)
#else
#	define TRACE(x...) ;
#endif


/** @brief Constructs an unconnected stream socket. */
BSocket::BSocket()
{
}


/** @brief Constructs a stream socket and immediately connects it to @a peer. */
BSocket::BSocket(const BNetworkAddress& peer, bigtime_t timeout)
{
	Connect(peer, timeout);
}


/** @brief Copy constructor forwarding to BAbstractSocket. */
BSocket::BSocket(const BSocket& other)
	:
	BAbstractSocket(other)
{
}


/** @brief Destructor. */
BSocket::~BSocket()
{
}


/** @brief Binds the stream socket to a local address.
 *  @param local     Local address to bind to.
 *  @param reuseAddr If true, enables SO_REUSEADDR.
 *  @return B_OK on success, or an error code from the underlying bind(). */
status_t
BSocket::Bind(const BNetworkAddress& local, bool reuseAddr)
{
	return BAbstractSocket::Bind(local, reuseAddr, SOCK_STREAM);
}


/** @brief Establishes a TCP connection to the given remote peer.
 *  @param peer    Remote peer address.
 *  @param timeout Maximum time to wait for the connection. */
status_t
BSocket::Connect(const BNetworkAddress& peer, bigtime_t timeout)
{
	return BAbstractSocket::Connect(peer, SOCK_STREAM, timeout);
}


/** @brief Accepts the next pending connection and returns it as a BSocket.
 *  @param _socket On success, set to a newly-allocated BSocket owned by the
 *                 caller. The caller is responsible for deleting it.
 *  @return B_OK on success, B_NO_MEMORY on allocation failure, or an
 *          error code from AcceptNext(). */
status_t
BSocket::Accept(BAbstractSocket*& _socket)
{
	int fd = -1;
	BNetworkAddress peer;
	status_t error = AcceptNext(fd, peer);
	if (error != B_OK)
		return error;
	BSocket* socket = new(std::nothrow) BSocket();
	if (socket == NULL) {
		close(fd);
		return B_NO_MEMORY;
	}

	socket->_SetTo(fd, fLocal, peer);
	_socket = socket;
	return B_OK;
}


//	#pragma mark - BDataIO implementation


/** @brief BDataIO read implementation that calls recv() on the stream.
 *  @return Number of bytes read, or a negative errno value. */
ssize_t
BSocket::Read(void* buffer, size_t size)
{
	ssize_t bytesReceived = recv(Socket(), buffer, size, 0);
	if (bytesReceived < 0) {
		TRACE("%p: BSocket::Read() error: %s\n", this, strerror(errno));
		return errno;
	}

	return bytesReceived;
}


/** @brief BDataIO write implementation that calls send() on the stream.
 *  @return Number of bytes written, or a negative errno value. */
ssize_t
BSocket::Write(const void* buffer, size_t size)
{
	ssize_t bytesSent = send(Socket(), buffer, size, 0);
	if (bytesSent < 0) {
		TRACE("%p: BSocket::Write() error: %s\n", this, strerror(errno));
		return errno;
	}

	return bytesSent;
}


//	#pragma mark - private


/** @brief Internal helper used by Accept() to adopt an already-open
 *         descriptor and populate the local/peer addresses. */
void
BSocket::_SetTo(int fd, const BNetworkAddress& local,
	const BNetworkAddress& peer)
{
	Disconnect();

	fInitStatus = B_OK;
	fSocket = fd;
	fLocal = local;
	fPeer = peer;
	fIsConnected = true;

	TRACE("%p: accepted from %s to %s\n", this, local.ToString().c_str(),
		peer.ToString().c_str());
}
