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
 *   Copyright 2002-2006,2008, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Scott T. Mansfield, thephantom@mac.com
 *       Oliver Tappe, zooey@hirschkaefer.de
 */

/** @file NetAddress.cpp
 *  @brief Legacy BNetAddress implementation. Wraps IPv4 address + port with
 *         hostname resolution and BArchivable support. Non-struct accessors
 *         return values in host byte order; non-struct mutators expect host
 *         byte order input. */

#include <r5_compatibility.h>

#include <ByteOrder.h>
#include <NetAddress.h>
#include <Message.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <new>
#include <string.h>


/** @brief Constructs from a hostname (or dotted-quad) and port number.
 *  @param hostname Hostname to resolve; may be a dotted IPv4 literal or NULL.
 *  @param port     Port number in host byte order. */
BNetAddress::BNetAddress(const char* hostname, unsigned short port)
	:
	fInit(B_NO_INIT)
{
	SetTo(hostname, port);
}


/** @brief Constructs from an existing BSD sockaddr_in structure. */
BNetAddress::BNetAddress(const struct sockaddr_in& addr)
	:
	fInit(B_NO_INIT)
{
	SetTo(addr);
}


/** @brief Constructs from an in_addr and port.
 *  @param addr IPv4 address in network byte order.
 *  @param port Port number in host byte order. */
BNetAddress::BNetAddress(in_addr addr, int port)
	:
	fInit(B_NO_INIT)
{
	SetTo(addr, port);
}


/** @brief Constructs from a raw uint32 address and port.
 *  @param addr IPv4 address in network byte order.
 *  @param port Port number in host byte order. */
BNetAddress::BNetAddress(uint32 addr, int port)
	:
	fInit(B_NO_INIT)
{
	SetTo(addr, port);
}


/** @brief Constructs from a hostname looked up via getservbyname().
 *  @param hostname Hostname (or IPv4 literal) to resolve.
 *  @param protocol Transport protocol name, e.g. "tcp" or "udp".
 *  @param service  Service name listed in /etc/services, e.g. "http". */
BNetAddress::BNetAddress(const char* hostname, const char* protocol,
	const char* service)
	:
	fInit(B_NO_INIT)
{
	SetTo(hostname, protocol, service);
}


/** @brief Copy constructor. */
BNetAddress::BNetAddress(const BNetAddress& other)
{
	*this = other;
}


/** @brief Constructs from an archived BMessage produced by Archive().
 *  @param archive Source BMessage. */
BNetAddress::BNetAddress(BMessage* archive)
{
	int16 int16value;
	if (archive->FindInt16("bnaddr_family", &int16value) != B_OK)
		return;

	fFamily = int16value;

	if (archive->FindInt16("bnaddr_port", &int16value) != B_OK)
		return;

	fPort = int16value;

	if (archive->FindInt32("bnaddr_addr", &fAddress) != B_OK)
		return;

	fInit = B_OK;
}


/** @brief Destructor. */
BNetAddress::~BNetAddress()
{
}


/** @brief Assignment operator. Copies family, port, address, and init state. */
BNetAddress&
BNetAddress::operator=(const BNetAddress& other)
{
	fInit = other.fInit;
	fFamily = other.fFamily;
	fPort = other.fPort;
	fAddress = other.fAddress;

	return *this;
}


/** @brief Retrieves the address as a dotted-quad string and host-order port.
 *  @param hostname On output, the dotted-quad IPv4 representation. May be
 *                  NULL. The caller must provide a buffer of at least
 *                  MAXHOSTNAMELEN + 1 bytes.
 *  @param port     On output, the port number in host byte order. May be NULL.
 *  @return B_OK on success, or B_NO_INIT if the instance was not initialised. */
status_t
BNetAddress::GetAddr(char* hostname, unsigned short* port) const
{
	if (fInit != B_OK)
		return B_NO_INIT;

	if (port != NULL)
		*port = ntohs(fPort);

	if (hostname != NULL) {
		struct in_addr addr;
		addr.s_addr = fAddress;

		char* text = inet_ntoa(addr);
		if (text != NULL)
			strcpy(hostname, text);
	}

	return B_OK;
}


/** @brief Fills a sockaddr_in struct with the address, family, and port.
 *  @param sa Destination sockaddr_in. Only the sin_family, sin_addr, and
 *            sin_port fields are touched; all others are left as the caller
 *            left them. The address and port are stored in network byte order.
 *  @return B_OK on success, or B_NO_INIT if the instance was not initialised. */
status_t BNetAddress::GetAddr(struct sockaddr_in& sa) const
{
	if (fInit != B_OK)
		return B_NO_INIT;

	sa.sin_port = fPort;
	sa.sin_addr.s_addr = fAddress;
	if (check_r5_compatibility()) {
		r5_sockaddr_in* r5Addr = (r5_sockaddr_in*)&sa;
		if (fFamily == AF_INET)
			r5Addr->sin_family = R5_AF_INET;
		else
			r5Addr->sin_family = fFamily;
	} else
		sa.sin_family = fFamily;

	return B_OK;
}


/** @brief Fills an in_addr struct and optional port with the stored values.
 *  @param addr On output, the IPv4 address in network byte order.
 *  @param port On output, the port number in host byte order (may be NULL).
 *  @return B_OK on success, or B_NO_INIT if the instance was not initialised. */
status_t BNetAddress::GetAddr(in_addr& addr, unsigned short* port) const
{
	if (fInit != B_OK)
		return B_NO_INIT;

	addr.s_addr = fAddress;

	if (port != NULL)
		*port = ntohs(fPort);

	return B_OK;
}


/** @brief Returns whether the instance has been successfully initialised.
 *  @return B_OK if initialised, B_ERROR otherwise. */
status_t BNetAddress::InitCheck() const
{
	return fInit == B_OK ? B_OK : B_ERROR;
}


/** @brief Non-const overload of InitCheck() preserved for BeOS/ABI compatibility. */
status_t BNetAddress::InitCheck()
{
	return const_cast<const BNetAddress*>(this)->InitCheck();
}


/** @brief Serialises the address into a BMessage for persistence or IPC.
 *  @param into Destination BMessage to receive the address fields.
 *  @param deep Ignored; present for BArchivable contract compatibility.
 *  @return B_OK on success, B_ERROR on archive failure, or B_NO_INIT
 *          if the instance was not initialised. */
status_t BNetAddress::Archive(BMessage* into, bool deep) const
{
	if (fInit != B_OK)
		return B_NO_INIT;

	if (into->AddInt16("bnaddr_family", fFamily) != B_OK)
		return B_ERROR;

	if (into->AddInt16("bnaddr_port", fPort) != B_OK)
		return B_ERROR;

	if (into->AddInt32("bnaddr_addr", fAddress) != B_OK)
		return B_ERROR;

	return B_OK;
}


/** @brief BArchivable factory that reconstructs a BNetAddress from a BMessage.
 *  @param archive Source BMessage previously produced by Archive().
 *  @return A newly-allocated BNetAddress owned by the caller, or NULL on
 *          allocation failure or invalid archive. */
BArchivable*
BNetAddress::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "BNetAddress"))
		return NULL;

	BNetAddress* address = new (std::nothrow) BNetAddress(archive);
	if (address == NULL)
		return NULL;

	if (address->InitCheck() != B_OK) {
		delete address;
		return NULL;
	}

	return address;
}


/** @brief Sets the address from a hostname or dotted-quad and a port.
 *  @param hostname Dotted-quad IPv4 literal, resolvable hostname, or NULL
 *                  (interpreted as INADDR_ANY).
 *  @param port     Port number in host byte order.
 *  @return B_OK on success, or B_ERROR if the hostname could not be resolved. */
status_t
BNetAddress::SetTo(const char* hostname, unsigned short port)
{
	if (hostname == NULL)
		return B_ERROR;

	in_addr_t addr = INADDR_ANY;

	// Try like all git-out to set the address from the given hostname.

	// See if the string is an ASCII-fied IP address.
	addr = inet_addr(hostname);
	if (addr == INADDR_ANY || addr == (in_addr_t)-1) {
		// See if we can resolve the hostname to an IP address.
		struct hostent* host = gethostbyname(hostname);
		if (host != NULL)
			addr = *(int*)host->h_addr_list[0];
		else
			return B_ERROR;
	}

	fFamily = AF_INET;
	fPort = htons(port);
	fAddress = addr;

	return fInit = B_OK;
}


/** @brief Sets the address from an existing sockaddr_in structure.
 *  @param addr Source sockaddr_in (fields remain in network byte order).
 *  @return B_OK. */
status_t
BNetAddress::SetTo(const struct sockaddr_in& addr)
{
	fPort = addr.sin_port;
	fAddress = addr.sin_addr.s_addr;

	if (check_r5_compatibility()) {
		const r5_sockaddr_in* r5Addr = (const r5_sockaddr_in*)&addr;
		if (r5Addr->sin_family == R5_AF_INET)
			fFamily = AF_INET;
		else
			fFamily = r5Addr->sin_family;
	} else
		fFamily = addr.sin_family;

	return fInit = B_OK;
}


/** @brief Sets the address from an in_addr and an optional port.
 *  @param addr IPv4 address in network byte order.
 *  @param port Port number in host byte order.
 *  @return B_OK. */
status_t
BNetAddress::SetTo(in_addr addr, int port)
{
	fFamily = AF_INET;
	fPort = htons((short)port);
	fAddress = addr.s_addr;

	return fInit = B_OK;
}


/** @brief Sets the address from a raw uint32 and an optional port.
 *  @param addr IPv4 address in network byte order.
 *  @param port Port number in host byte order.
 *  @return B_OK. */
status_t
BNetAddress::SetTo(uint32 addr, int port)
{
	fFamily = AF_INET;
	fPort = htons((short)port);
	fAddress = addr;

	return fInit = B_OK;
}


/** @brief Sets the address from a hostname plus a protocol/service pair.
 *         The port is derived from getservbyname().
 *  @param hostname Dotted-quad IPv4 literal, resolvable hostname, or NULL.
 *  @param protocol Transport protocol name ("tcp", "udp", ...).
 *  @param service  Named service listed in /etc/services (e.g. "http").
 *  @return B_OK on success, or B_ERROR if the service could not be resolved. */
status_t
BNetAddress::SetTo(const char* hostname, const char* protocol,
	const char* service)
{
	struct servent* serviceEntry = getservbyname(service, protocol);
	if (serviceEntry == NULL)
		return B_ERROR;

	return SetTo(hostname, serviceEntry->s_port);
}


//	#pragma mark - FBC


void BNetAddress::_ReservedBNetAddressFBCCruft1() {}
void BNetAddress::_ReservedBNetAddressFBCCruft2() {}
void BNetAddress::_ReservedBNetAddressFBCCruft3() {}
void BNetAddress::_ReservedBNetAddressFBCCruft4() {}
void BNetAddress::_ReservedBNetAddressFBCCruft5() {}
void BNetAddress::_ReservedBNetAddressFBCCruft6() {}
