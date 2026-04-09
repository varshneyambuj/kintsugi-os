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
 *   Distributed under the terms of the MIT License.
 */

/** @file DatagramSocket.cpp
 *  @brief Connection-less (SOCK_DGRAM) socket wrapper providing
 *         sendto/recvfrom, broadcast, and BDataIO compatibility. */


#include <DatagramSocket.h>

#include <errno.h>


//#define TRACE_SOCKET
#ifdef TRACE_SOCKET
#	define TRACE(x...) printf(x)
#else
#	define TRACE(x...) ;
#endif


/** @brief Constructs an unconnected datagram socket. */
BDatagramSocket::BDatagramSocket()
{
}


/** @brief Constructs a datagram socket and immediately connects it to a peer.
 *  @param peer    Remote address to associate with the socket.
 *  @param timeout Send/receive timeout in microseconds. */
BDatagramSocket::BDatagramSocket(const BNetworkAddress& peer, bigtime_t timeout)
{
	Connect(peer, timeout);
}


/** @brief Copy constructor forwarding to BAbstractSocket. */
BDatagramSocket::BDatagramSocket(const BDatagramSocket& other)
	:
	BAbstractSocket(other)
{
}


/** @brief Destructor. */
BDatagramSocket::~BDatagramSocket()
{
}


/** @brief Binds the datagram socket to a local address.
 *  @param local     Local address to bind to.
 *  @param reuseAddr If true, enables SO_REUSEADDR.
 *  @return B_OK on success, or an error code. */
status_t
BDatagramSocket::Bind(const BNetworkAddress& local, bool reuseAddr)
{
	return BAbstractSocket::Bind(local, reuseAddr, SOCK_DGRAM);
}


/** @brief Accept is not supported for datagram sockets.
 *  @return Always B_NOT_SUPPORTED. */
status_t
BDatagramSocket::Accept(BAbstractSocket*& _socket)
{
	return B_NOT_SUPPORTED;
}


/** @brief Associates the socket with a default peer address.
 *  @param peer    Remote peer to use for subsequent Write()/send() calls.
 *  @param timeout Send/receive timeout in microseconds.
 *  @return B_OK on success, or an error code. */
status_t
BDatagramSocket::Connect(const BNetworkAddress& peer, bigtime_t timeout)
{
	return BAbstractSocket::Connect(peer, SOCK_DGRAM, timeout);
}


/** @brief Enables or disables sending of broadcast datagrams on the socket.
 *  @param broadcast true to allow broadcasts, false to disallow.
 *  @return B_OK on success, or an errno code from setsockopt(). */
status_t
BDatagramSocket::SetBroadcast(bool broadcast)
{
	int value = broadcast ? 1 : 0;
	if (setsockopt(fSocket, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value))
			!= 0)
		return errno;

	return B_OK;
}


/** @brief Sets the default peer address without performing a connect().
 *  @param peer Remote peer address to associate with the socket. */
void
BDatagramSocket::SetPeer(const BNetworkAddress& peer)
{
	fPeer = peer;
}


/** @brief Returns the largest datagram size the socket can transmit in one
 *         send() call.
 *  @return 32768 bytes; the actual limit may vary with the address family. */
size_t
BDatagramSocket::MaxTransmissionSize() const
{
	// TODO: might vary on family!
	return 32768;
}


/** @brief Sends a single datagram to a specific address.
 *  @param address Destination address for the datagram.
 *  @param buffer  Pointer to the bytes to send.
 *  @param size    Number of bytes to send.
 *  @return Number of bytes sent on success, or a negative errno value. */
ssize_t
BDatagramSocket::SendTo(const BNetworkAddress& address, const void* buffer,
	size_t size)
{
	ssize_t bytesSent = sendto(fSocket, buffer, size, 0, address,
		address.Length());
	if (bytesSent < 0)
		return errno;

	return bytesSent;
}


/** @brief Receives a single datagram and reports the sender address.
 *  @param buffer     Destination buffer for the received bytes.
 *  @param bufferSize Size of the destination buffer.
 *  @param from       On return, set to the address of the datagram sender.
 *  @return Number of bytes received, or a negative errno value. */
ssize_t
BDatagramSocket::ReceiveFrom(void* buffer, size_t bufferSize,
	BNetworkAddress& from)
{
	socklen_t fromLength = sizeof(sockaddr_storage);
	ssize_t bytesReceived = recvfrom(fSocket, buffer, bufferSize, 0,
		from, &fromLength);
	if (bytesReceived < 0)
		return errno;

	return bytesReceived;
}


//	#pragma mark - BDataIO implementation


/** @brief BDataIO read implementation that receives a datagram with recv().
 *  @param buffer Destination buffer.
 *  @param size   Size of the buffer.
 *  @return Number of bytes received, or a negative errno value. */
ssize_t
BDatagramSocket::Read(void* buffer, size_t size)
{
	ssize_t bytesReceived = recv(Socket(), buffer, size, 0);
	if (bytesReceived < 0) {
		TRACE("%p: BSocket::Read() error: %s\n", this, strerror(errno));
		return errno;
	}

	return bytesReceived;
}


/** @brief BDataIO write implementation. Sends via send() if connected, or
 *         sendto() to the stored peer otherwise.
 *  @param buffer Source buffer.
 *  @param size   Number of bytes to send.
 *  @return Number of bytes sent, or a negative errno value. */
ssize_t
BDatagramSocket::Write(const void* buffer, size_t size)
{
	ssize_t bytesSent;

	if (!fIsConnected)
		bytesSent = sendto(Socket(), buffer, size, 0, fPeer, fPeer.Length());
	else
		bytesSent = send(Socket(), buffer, size, 0);

	if (bytesSent < 0) {
		TRACE("%p: BDatagramSocket::Write() error: %s\n", this,
			strerror(errno));
		return errno;
	}

	return bytesSent;
}
