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
 *   Copyright 2015 Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 */

/** @file ProxySecureSocket.cpp
 *  @brief TLS socket that tunnels through an HTTP proxy via the CONNECT
 *         verb before handing the underlying connection to OpenSSL. */


#include <ProxySecureSocket.h>

#include <stdio.h>


/** @brief Constructs an unconnected secure socket routed through @a proxy. */
BProxySecureSocket::BProxySecureSocket(const BNetworkAddress& proxy)
	:
	BSecureSocket(),
	fProxyAddress(proxy)
{
}


/** @brief Constructs and immediately connects through @a proxy to @a peer. */
BProxySecureSocket::BProxySecureSocket(const BNetworkAddress& proxy, const BNetworkAddress& peer,
	bigtime_t timeout)
	:
	BSecureSocket(),
	fProxyAddress(proxy)
{
	Connect(peer, timeout);
}


/** @brief Copy constructor preserving the proxy endpoint address. */
BProxySecureSocket::BProxySecureSocket(const BProxySecureSocket& other)
	:
	BSecureSocket(other),
	fProxyAddress(other.fProxyAddress)
{
}


/** @brief Destructor. */
BProxySecureSocket::~BProxySecureSocket()
{
}


/** @brief Connects to @a peer by first establishing an HTTP CONNECT tunnel
 *         through fProxyAddress, then upgrading the socket to TLS via
 *         BSecureSocket::_SetupConnect().
 *  @param peer    Final destination (the CONNECT verb target).
 *  @param timeout Timeout for the underlying TCP connection.
 *  @return B_OK on success, B_BAD_DATA if the proxy reply is malformed,
 *          B_BAD_VALUE if the proxy returns a non-2xx HTTP status, or
 *          an error from the TCP/TLS layers. */
status_t
BProxySecureSocket::Connect(const BNetworkAddress& peer, bigtime_t timeout)
{
	status_t status = InitCheck();
	if (status != B_OK)
		return status;

	status = BSocket::Connect(fProxyAddress, timeout);
	if (status != B_OK)
		return status;

	BString connectRequest;
	connectRequest.SetToFormat("CONNECT %s:%d HTTP/1.0\r\n\r\n",
		peer.HostName().String(), peer.Port());
	BSocket::Write(connectRequest.String(), connectRequest.Length());

	char buffer[256];
	ssize_t length = BSocket::Read(buffer, sizeof(buffer) - 1);
	if (length <= 0)
		return length;

	buffer[length] = '\0';
	int httpStatus = 0;
	int matches = sscanf(buffer, "HTTP/1.0 %d %*[^\r\n]\r\n\r\n", &httpStatus);
	if (matches != 2)
		return B_BAD_DATA;

	if (httpStatus < 200 || httpStatus > 299)
		return B_BAD_VALUE;

	return _SetupConnect();
}


