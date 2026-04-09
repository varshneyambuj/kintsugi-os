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
 *   Copyright 2010-2015, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file NetworkAddress.cpp
 *  @brief Modern BNetworkAddress implementation. Wraps a sockaddr_storage
 *         and supports IPv4, IPv6, and link-layer families with hostname
 *         resolution, broadcast/multicast helpers, and comparison operators. */


#include <NetworkAddress.h>

#include <NetworkInterface.h>
#include <NetworkRoster.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/sockio.h>


/** @brief Portable population count used for converting netmasks into
 *         prefix lengths. Prefers the GCC __builtin_popcount intrinsic
 *         (which may map to a hardware POPCNT instruction) when available. */
#if __GNUC__ > 3
#	define addr_bitcount(bitfield) __builtin_popcount(bitfield)
#else
static ssize_t
addr_bitcount(uint32 bitfield)
{
	ssize_t result = 0;
	for (uint8 i = 32; i > 0; i--) {
		if ((bitfield & (1 << (i - 1))) == 0)
			break;
		result++;
	}
	return result;
}
#endif


/** @brief Converts a single hexadecimal character to its numeric value.
 *  @param hex Character in '0'..'9', 'a'..'f', or 'A'..'F'.
 *  @return Corresponding nibble value in the range 0..15. */
static uint8
from_hex(char hex)
{
	if (isdigit(hex))
		return hex - '0';

	return tolower(hex) - 'a' + 10;
}


// #pragma mark -


/** @brief Default constructor. Produces an unspecified (AF_UNSPEC) address. */
BNetworkAddress::BNetworkAddress()
{
	Unset();
}


/** @brief Resolves @a host and stores the resulting address plus port. */
BNetworkAddress::BNetworkAddress(const char* host, uint16 port, uint32 flags)
{
	fStatus = SetTo(host, port, flags);
}


/** @brief Resolves @a host/@a service via getaddrinfo() and stores the result. */
BNetworkAddress::BNetworkAddress(const char* host, const char* service,
	uint32 flags)
{
	fStatus = SetTo(host, service, flags);
}


/** @brief Resolves @a host restricted to the given address @a family. */
BNetworkAddress::BNetworkAddress(int family, const char* host, uint16 port,
	uint32 flags)
{
	fStatus = SetTo(family, host, port, flags);
}


/** @brief Resolves @a host/@a service restricted to the given address family. */
BNetworkAddress::BNetworkAddress(int family, const char* host,
	const char* service, uint32 flags)
{
	fStatus = SetTo(family, host, service, flags);
}


/** @brief Constructs from a raw BSD sockaddr structure. */
BNetworkAddress::BNetworkAddress(const sockaddr& address)
{
	SetTo(address);
}


/** @brief Constructs from a sockaddr_storage container. */
BNetworkAddress::BNetworkAddress(const sockaddr_storage& address)
{
	SetTo(address);
}


/** @brief Constructs from an IPv4 sockaddr_in. */
BNetworkAddress::BNetworkAddress(const sockaddr_in& address)
{
	SetTo(address);
}


/** @brief Constructs from an IPv6 sockaddr_in6. */
BNetworkAddress::BNetworkAddress(const sockaddr_in6& address)
{
	SetTo(address);
}


/** @brief Constructs from a link-layer sockaddr_dl. */
BNetworkAddress::BNetworkAddress(const sockaddr_dl& address)
{
	SetTo(address);
}


/** @brief Constructs an IPv4 address from an in_addr_t and port. */
BNetworkAddress::BNetworkAddress(in_addr_t address, uint16 port)
{
	SetTo(address, port);
}


/** @brief Constructs an IPv6 address from an in6_addr and port. */
BNetworkAddress::BNetworkAddress(const in6_addr& address, uint16 port)
{
	SetTo(address, port);
}


/** @brief Copy constructor. */
BNetworkAddress::BNetworkAddress(const BNetworkAddress& other)
	:
	fAddress(other.fAddress),
	fStatus(other.fStatus),
	fHostName(other.fHostName)
{
}


/** @brief Destructor. */
BNetworkAddress::~BNetworkAddress()
{
}


/** @brief Returns the initialisation status of the address.
 *  @return B_OK if resolved/constructed successfully, or an error code. */
status_t
BNetworkAddress::InitCheck() const
{
	return fStatus;
}


/** @brief Clears the address, resetting it to AF_UNSPEC with no hostname. */
void
BNetworkAddress::Unset()
{
	fAddress.ss_family = AF_UNSPEC;
	fAddress.ss_len = 2;
	fHostName = "";
	fStatus = B_OK;
}


/** @brief Resolves a hostname (or IP literal) and sets the address.
 *         Prefers IPv6 results when available and falls back to IPv4.
 *  @param host  Hostname or IP literal to resolve.
 *  @param port  Port number in host byte order.
 *  @param flags getaddrinfo flags (AI_PASSIVE, AI_CANONNAME, ...).
 *  @return B_OK on success, or an error from the resolver. */
status_t
BNetworkAddress::SetTo(const char* host, uint16 port, uint32 flags)
{
	BReference<const BNetworkAddressResolver> resolver
		= BNetworkAddressResolver::Resolve(host, port, flags);
	if (!resolver.IsSet())
		return B_NO_MEMORY;
	status_t status = resolver->InitCheck();
	if (status != B_OK)
		return status;

	// Prefer IPv6 addresses

	uint32 cookie = 0;
	status = resolver->GetNextAddress(AF_INET6, &cookie, *this);
	if (status != B_OK) {
		cookie = 0;
		status = resolver->GetNextAddress(&cookie, *this);
		if (status != B_OK)
			Unset();
	}
	fHostName = host;
	fStatus = status;
	return status;
}


/** @brief Resolves a hostname plus a service name and sets the address.
 *         Prefers IPv6 results when available and falls back to IPv4.
 *  @param host    Hostname or IP literal.
 *  @param service Named service (e.g. "http") or numeric port string.
 *  @param flags   getaddrinfo flags.
 *  @return B_OK on success, or an error from the resolver. */
status_t
BNetworkAddress::SetTo(const char* host, const char* service, uint32 flags)
{
	BReference<const BNetworkAddressResolver> resolver
		= BNetworkAddressResolver::Resolve(host, service, flags);
	if (!resolver.IsSet())
		return B_NO_MEMORY;
	status_t status = resolver->InitCheck();
	if (status != B_OK)
		return status;

	// Prefer IPv6 addresses

	uint32 cookie = 0;
	status = resolver->GetNextAddress(AF_INET6, &cookie, *this);
	if (status != B_OK) {
		cookie = 0;
		status = resolver->GetNextAddress(&cookie, *this);
		if (status != B_OK)
			Unset();
	}
	fHostName = host;
	fStatus = status;
	return status;
}


/** @brief Resolves @a host for the given address @a family.
 *         AF_LINK is handled specially via _ParseLinkAddress().
 *  @return B_OK on success, B_BAD_VALUE if inconsistent parameters are
 *          passed, or a resolver error. */
status_t
BNetworkAddress::SetTo(int family, const char* host, uint16 port, uint32 flags)
{
	if (family == AF_LINK) {
		if (port != 0)
			return B_BAD_VALUE;
		return _ParseLinkAddress(host);
			// SetToLinkAddress takes care of setting fStatus
	}

	BReference<const BNetworkAddressResolver> resolver
		= BNetworkAddressResolver::Resolve(family, host, port, flags);
	if (!resolver.IsSet())
		return B_NO_MEMORY;
	status_t status = resolver->InitCheck();
	if (status != B_OK)
		return status;

	uint32 cookie = 0;
	status = resolver->GetNextAddress(&cookie, *this);
	if (status != B_OK)
		Unset();
	fHostName = host;
	fStatus = status;
	return status;
}


/** @brief Resolves @a host/@a service for the given address @a family.
 *         AF_LINK is handled specially via _ParseLinkAddress(). */
status_t
BNetworkAddress::SetTo(int family, const char* host, const char* service,
	uint32 flags)
{
	if (family == AF_LINK) {
		if (service != NULL)
			return B_BAD_VALUE;
		return _ParseLinkAddress(host);
			// SetToLinkAddress takes care of setting fStatus
	}

	BReference<const BNetworkAddressResolver> resolver
		= BNetworkAddressResolver::Resolve(family, host, service, flags);
	if (!resolver.IsSet())
		return B_NO_MEMORY;
	status_t status = resolver->InitCheck();
	if (status != B_OK)
		return status;

	uint32 cookie = 0;
	status = resolver->GetNextAddress(&cookie, *this);
	if (status != B_OK)
		Unset();
	fHostName = host;
	fStatus = status;
	return status;
}


/** @brief Sets the address from a raw sockaddr, computing the length from
 *         the family (IPv4/IPv6/link). Unknown families fall back to
 *         sa_len. */
void
BNetworkAddress::SetTo(const sockaddr& address)
{
	if (address.sa_family == AF_UNSPEC) {
		Unset();
		return;
	}

	size_t length = min_c(sizeof(sockaddr_storage), address.sa_len);
	switch (address.sa_family) {
		case AF_INET:
			length = sizeof(sockaddr_in);
			break;
		case AF_INET6:
			length = sizeof(sockaddr_in6);
			break;
		case AF_LINK:
		{
			sockaddr_dl& link = (sockaddr_dl&)address;
			length = sizeof(sockaddr_dl) - sizeof(link.sdl_data) + link.sdl_alen
				+ link.sdl_nlen + link.sdl_slen;
			break;
		}
	}

	SetTo(address, length);
}


/** @brief Sets the address from a sockaddr using an explicit byte length. */
void
BNetworkAddress::SetTo(const sockaddr& address, size_t length)
{
	if (address.sa_family == AF_UNSPEC || length == 0) {
		Unset();
		return;
	}

	memcpy(&fAddress, &address, length);
	fAddress.ss_len = length;
	fStatus = B_OK;
}


/** @brief Sets the address from a sockaddr_storage container. */
void
BNetworkAddress::SetTo(const sockaddr_storage& address)
{
	SetTo((sockaddr&)address);
}


/** @brief Sets the address from an IPv4 sockaddr_in. */
void
BNetworkAddress::SetTo(const sockaddr_in& address)
{
	SetTo((sockaddr&)address);
}


/** @brief Sets the address from an IPv6 sockaddr_in6. */
void
BNetworkAddress::SetTo(const sockaddr_in6& address)
{
	SetTo((sockaddr&)address);
}


/** @brief Sets the address from a link-layer sockaddr_dl. */
void
BNetworkAddress::SetTo(const sockaddr_dl& address)
{
	SetTo((sockaddr&)address);
}


/** @brief Sets an IPv4 address from an in_addr_t and port. */
void
BNetworkAddress::SetTo(in_addr_t inetAddress, uint16 port)
{
	memset(&fAddress, 0, sizeof(sockaddr_storage));

	fAddress.ss_family = AF_INET;
	fAddress.ss_len = sizeof(sockaddr_in);
	SetAddress(inetAddress);
	SetPort(port);

	fStatus = B_OK;
}


void
BNetworkAddress::SetTo(const in6_addr& inet6Address, uint16 port)
{
	memset(&fAddress, 0, sizeof(sockaddr_storage));

	fAddress.ss_family = AF_INET6;
	fAddress.ss_len = sizeof(sockaddr_in6);
	SetAddress(inet6Address);
	SetPort(port);

	fStatus = B_OK;
}


/** @brief Copies another BNetworkAddress into this one. */
void
BNetworkAddress::SetTo(const BNetworkAddress& other)
{
	fAddress = other.fAddress;
	fStatus = other.fStatus;
	fHostName = other.fHostName;
}


/** @brief Sets the address to the IPv4 broadcast address (255.255.255.255).
 *  @return B_OK on success, or B_NOT_SUPPORTED for non-IPv4 families. */
status_t
BNetworkAddress::SetToBroadcast(int family, uint16 port)
{
	if (family != AF_INET)
		return fStatus = B_NOT_SUPPORTED;

	SetTo(INADDR_BROADCAST, port);
	return fStatus;
}


/** @brief Sets the address to a local interface address. Not yet implemented.
 *  @return Currently always B_NOT_SUPPORTED. */
status_t
BNetworkAddress::SetToLocal(int family, uint16 port)
{
	// TODO: choose a local address from the network interfaces
	return fStatus = B_NOT_SUPPORTED;
}


/** @brief Sets the address to the loopback address for the given family
 *         (127.0.0.1 for IPv4, ::1 for IPv6).
 *  @return B_OK on success, or B_NOT_SUPPORTED for an unknown family. */
status_t
BNetworkAddress::SetToLoopback(int family, uint16 port)
{
	switch (family) {
		// TODO: choose family depending on availability of IPv6
		case AF_UNSPEC:
		case AF_INET:
			SetTo(htonl(INADDR_LOOPBACK), port);
			break;

		case AF_INET6:
			SetTo(in6addr_loopback, port);
			break;

		default:
			return fStatus = B_NOT_SUPPORTED;
	}

	return fStatus;
}


/** @brief Builds a netmask address from a CIDR prefix length.
 *  @param family       AF_INET or AF_INET6.
 *  @param prefixLength Number of leading mask bits (<=32 for IPv4, <=128 for IPv6).
 *  @return B_OK on success, B_BAD_VALUE if the prefix is out of range, or
 *          B_NOT_SUPPORTED for an unknown family. */
status_t
BNetworkAddress::SetToMask(int family, uint32 prefixLength)
{
	switch (family) {
		case AF_INET:
		{
			if (prefixLength > 32)
				return B_BAD_VALUE;

			sockaddr_in& mask = (sockaddr_in&)fAddress;
			memset(&fAddress, 0, sizeof(sockaddr_storage));
			mask.sin_family = AF_INET;
			mask.sin_len = sizeof(sockaddr_in);

			uint32 hostMask = 0;
			for (uint8 i = 32; i > 32 - prefixLength; i--)
				hostMask |= 1 << (i - 1);

			mask.sin_addr.s_addr = htonl(hostMask);
			break;
		}

		case AF_INET6:
		{
			if (prefixLength > 128)
				return B_BAD_VALUE;

			sockaddr_in6& mask = (sockaddr_in6&)fAddress;
			memset(&fAddress, 0, sizeof(sockaddr_storage));
			mask.sin6_family = AF_INET6;
			mask.sin6_len = sizeof(sockaddr_in6);

			for (uint8 i = 0; i < sizeof(in6_addr); i++, prefixLength -= 8) {
				if (prefixLength < 8) {
					mask.sin6_addr.s6_addr[i]
						= (uint8)(0xff << (8 - prefixLength));
					break;
				}

				mask.sin6_addr.s6_addr[i] = 0xff;
			}
			break;
		}

		default:
			return B_NOT_SUPPORTED;
	}

	return fStatus = B_OK;
}


/** @brief Sets the address to the "any" wildcard address (0.0.0.0 / ::).
 *  @return B_OK on success, or B_NOT_SUPPORTED for an unknown family. */
status_t
BNetworkAddress::SetToWildcard(int family, uint16 port)
{
	switch (family) {
		case AF_INET:
			SetTo(INADDR_ANY, port);
			break;

		case AF_INET6:
			SetTo(in6addr_any, port);
			break;

		default:
			return B_NOT_SUPPORTED;
	}

	return fStatus;
}


/** @brief Replaces the IPv4 address while keeping the port and family.
 *  @return B_OK on success, or B_BAD_VALUE if the family is not AF_INET. */
status_t
BNetworkAddress::SetAddress(in_addr_t inetAddress)
{
	if (Family() != AF_INET)
		return B_BAD_VALUE;

	sockaddr_in& address = (sockaddr_in&)fAddress;
	address.sin_addr.s_addr = inetAddress;
	return B_OK;
}


/** @brief Replaces the IPv6 address while keeping the port and family.
 *  @return B_OK on success, or B_BAD_VALUE if the family is not AF_INET6. */
status_t
BNetworkAddress::SetAddress(const in6_addr& inet6Address)
{
	if (Family() != AF_INET6)
		return B_BAD_VALUE;

	sockaddr_in6& address = (sockaddr_in6&)fAddress;
	memcpy(address.sin6_addr.s6_addr, &inet6Address,
		sizeof(address.sin6_addr.s6_addr));
	return B_OK;
}


/** @brief Replaces the port number; safe for IPv4 and IPv6 addresses. */
void
BNetworkAddress::SetPort(uint16 port)
{
	switch (fAddress.ss_family) {
		case AF_INET:
			((sockaddr_in&)fAddress).sin_port = htons(port);
			break;

		case AF_INET6:
			((sockaddr_in6&)fAddress).sin6_port = htons(port);
			break;

		default:
			break;
	}
}


/** @brief Sets the address to a link-layer (MAC) address.
 *  @param address Byte array containing the hardware address.
 *  @param length  Length of the hardware address in bytes. */
void
BNetworkAddress::SetToLinkLevel(const uint8* address, size_t length)
{
	sockaddr_dl& link = (sockaddr_dl&)fAddress;
	memset(&link, 0, sizeof(sockaddr_dl));

	link.sdl_family = AF_LINK;
	link.sdl_alen = length;
	memcpy(LLADDR(&link), address, length);

	link.sdl_len = sizeof(sockaddr_dl);
	if (length > sizeof(link.sdl_data))
		link.sdl_len += length - sizeof(link.sdl_data);

	fStatus = B_OK;
}


/** @brief Sets the address to an interface name reference in the link layer. */
void
BNetworkAddress::SetToLinkLevel(const char* name)
{
	sockaddr_dl& link = (sockaddr_dl&)fAddress;
	memset(&link, 0, sizeof(sockaddr_dl));

	size_t length = strlen(name);
	if (length > sizeof(fAddress) - sizeof(sockaddr_dl) + sizeof(link.sdl_data))
		length = sizeof(fAddress) - sizeof(sockaddr_dl) + sizeof(link.sdl_data);

	link.sdl_family = AF_LINK;
	link.sdl_nlen = length;

	memcpy(link.sdl_data, name, link.sdl_nlen);

	link.sdl_len = sizeof(sockaddr_dl);
	if (link.sdl_nlen > sizeof(link.sdl_data))
		link.sdl_len += link.sdl_nlen - sizeof(link.sdl_data);

	fStatus = B_OK;
}


/** @brief Sets a link-layer address that only identifies an interface by index. */
void
BNetworkAddress::SetToLinkLevel(uint32 index)
{
	sockaddr_dl& link = (sockaddr_dl&)fAddress;
	memset(&link, 0, sizeof(sockaddr_dl));

	link.sdl_family = AF_LINK;
	link.sdl_len = sizeof(sockaddr_dl);
	link.sdl_index = index;

	fStatus = B_OK;
}


/** @brief Updates the link-layer interface index. */
void
BNetworkAddress::SetLinkLevelIndex(uint32 index)
{
	sockaddr_dl& link = (sockaddr_dl&)fAddress;
	link.sdl_index = index;
}


/** @brief Updates the link-layer interface hardware type. */
void
BNetworkAddress::SetLinkLevelType(uint8 type)
{
	sockaddr_dl& link = (sockaddr_dl&)fAddress;
	link.sdl_type = type;
}


/** @brief Updates the link-layer Ethernet frame type (stored in network order). */
void
BNetworkAddress::SetLinkLevelFrameType(uint16 frameType)
{
	sockaddr_dl& link = (sockaddr_dl&)fAddress;
	link.sdl_e_type = htons(frameType);
}


/** @brief Returns the stored address family (AF_INET, AF_INET6, AF_LINK, ...). */
int
BNetworkAddress::Family() const
{
	return fAddress.ss_family;
}


/** @brief Returns the port number in host byte order for IP-family addresses.
 *  @return The port, or 0 for non-IP addresses. */
uint16
BNetworkAddress::Port() const
{
	switch (fAddress.ss_family) {
		case AF_INET:
			return ntohs(((sockaddr_in&)fAddress).sin_port);

		case AF_INET6:
			return ntohs(((sockaddr_in6&)fAddress).sin6_port);

		default:
			return 0;
	}
}


/** @brief Returns the length in bytes of the underlying sockaddr. */
size_t
BNetworkAddress::Length() const
{
	return fAddress.ss_len;
}


/** @brief Const read-only view of the underlying sockaddr. */
const sockaddr&
BNetworkAddress::SockAddr() const
{
	return (const sockaddr&)fAddress;
}


/** @brief Mutable view of the underlying sockaddr. */
sockaddr&
BNetworkAddress::SockAddr()
{
	return (sockaddr&)fAddress;
}


/** @brief Reports whether the address carries no usable information
 *         (family AF_UNSPEC, wildcard address with port 0, etc). */
bool
BNetworkAddress::IsEmpty() const
{
	if (fAddress.ss_len == 0)
		return true;

	switch (fAddress.ss_family) {
		case AF_UNSPEC:
			return true;
		case AF_INET:
		{
			sockaddr_in& sin = (sockaddr_in&)fAddress;
			return sin.sin_addr.s_addr == INADDR_ANY && sin.sin_port == 0;
		}
		case AF_INET6:
		{
			sockaddr_in6& sin6 = (sockaddr_in6&)fAddress;
			return IN6_IS_ADDR_UNSPECIFIED(&sin6.sin6_addr)
				&& sin6.sin6_port == 0;
		}

		default:
			return false;
	}
}


/** @brief Reports whether the address is the "any" wildcard (0.0.0.0 or ::). */
bool
BNetworkAddress::IsWildcard() const
{
	switch (fAddress.ss_family) {
		case AF_INET:
			return ((sockaddr_in&)fAddress).sin_addr.s_addr == INADDR_ANY;

		case AF_INET6:
			return !memcmp(&((sockaddr_in6&)fAddress).sin6_addr, &in6addr_any,
				sizeof(in6_addr));

		default:
			return false;
	}
}


/** @brief Reports whether this is an IPv4 broadcast or IPv6 multicast address. */
bool
BNetworkAddress::IsBroadcast() const
{
	switch (fAddress.ss_family) {
		case AF_INET:
			return ((sockaddr_in&)fAddress).sin_addr.s_addr == INADDR_BROADCAST;

		case AF_INET6:
			// There is no broadcast in IPv6, only multicast/anycast
			return IN6_IS_ADDR_MULTICAST(&((sockaddr_in6&)fAddress).sin6_addr);

		default:
			return false;
	}
}


/** @brief Reports whether the address is an IPv4 or IPv6 multicast address. */
bool
BNetworkAddress::IsMulticast() const
{
	switch (fAddress.ss_family) {
		case AF_INET:
			return IN_MULTICAST(((sockaddr_in&)fAddress).sin_addr.s_addr);

		case AF_INET6:
			return IN6_IS_ADDR_MULTICAST(&((sockaddr_in6&)fAddress).sin6_addr);

		default:
			return false;
	}
}


/** @brief Reports whether the address is an IPv6 global-scope multicast address. */
bool
BNetworkAddress::IsMulticastGlobal() const
{
	switch (fAddress.ss_family) {
		case AF_INET6:
			return IN6_IS_ADDR_MC_GLOBAL(&((sockaddr_in6&)fAddress).sin6_addr);

		default:
			return false;
	}
}


/** @brief Reports whether the address is an IPv6 node-local multicast address. */
bool
BNetworkAddress::IsMulticastNodeLocal() const
{
	switch (fAddress.ss_family) {
		case AF_INET6:
			return IN6_IS_ADDR_MC_NODELOCAL(
				&((sockaddr_in6&)fAddress).sin6_addr);

		default:
			return false;
	}
}


/** @brief Reports whether the address is an IPv6 link-local multicast address. */
bool
BNetworkAddress::IsMulticastLinkLocal() const
{
	switch (fAddress.ss_family) {
		case AF_INET6:
			return IN6_IS_ADDR_MC_LINKLOCAL(
				&((sockaddr_in6&)fAddress).sin6_addr);

		default:
			return false;
	}
}


bool
BNetworkAddress::IsMulticastSiteLocal() const
{
	switch (fAddress.ss_family) {
		case AF_INET6:
			return IN6_IS_ADDR_MC_SITELOCAL(
				&((sockaddr_in6&)fAddress).sin6_addr);

		default:
			return false;
	}
}


/** @brief Reports whether the address is an IPv6 organisation-local multicast address. */
bool
BNetworkAddress::IsMulticastOrgLocal() const
{
	switch (fAddress.ss_family) {
		case AF_INET6:
			return IN6_IS_ADDR_MC_ORGLOCAL(
				&((sockaddr_in6&)fAddress).sin6_addr);

		default:
			return false;
	}
}


/** @brief Reports whether the address is IPv6 link-local (fe80::/10).
 *  @note IPv4 link-local detection is not yet implemented. */
bool
BNetworkAddress::IsLinkLocal() const
{
	// TODO: ipv4
	switch (fAddress.ss_family) {
		case AF_INET6:
			return IN6_IS_ADDR_LINKLOCAL(&((sockaddr_in6&)fAddress).sin6_addr);

		default:
			return false;
	}
}


/** @brief Reports whether the address is IPv6 site-local. */
bool
BNetworkAddress::IsSiteLocal() const
{
	switch (fAddress.ss_family) {
		case AF_INET6:
			return IN6_IS_ADDR_SITELOCAL(&((sockaddr_in6&)fAddress).sin6_addr);

		default:
			return false;
	}
}


/** @brief Reports whether the address matches any address configured on a
 *         local interface. Walks the system's interface list. */
bool
BNetworkAddress::IsLocal() const
{
	BNetworkRoster& roster = BNetworkRoster::Default();

	BNetworkInterface interface;
	uint32 cookie = 0;

	while (roster.GetNextInterface(&cookie, interface) == B_OK) {
		int32 count = interface.CountAddresses();
		for (int32 j = 0; j < count; j++) {
			BNetworkInterfaceAddress address;
			if (interface.GetAddressAt(j, address) != B_OK)
				break;

			if (Equals(address.Address(), false))
				return true;
		}
	}

	return false;
}


/** @brief If this address is a netmask, returns the number of leading 1 bits.
 *  @return Prefix length for IPv4/IPv6 netmasks, or B_NOT_SUPPORTED. */
ssize_t
BNetworkAddress::PrefixLength() const
{
	switch (fAddress.ss_family) {
		case AF_INET:
		{
			sockaddr_in& mask = (sockaddr_in&)fAddress;

			uint32 hostMask = ntohl(mask.sin_addr.s_addr);
			return addr_bitcount(hostMask);
		}

		case AF_INET6:
		{
			sockaddr_in6& mask = (sockaddr_in6&)fAddress;

			// TODO : see if we can use the optimized addr_bitcount for this
			ssize_t result = 0;
			for (uint8 i = 0; i < sizeof(in6_addr); i++) {
				for (uint8 j = 0; j < 8; j++) {
					if (!(mask.sin6_addr.s6_addr[i] & (1 << j)))
						return result;
					result++;
				}
			}

			return 128;
		}

		default:
			return B_NOT_SUPPORTED;
	}
}


/** @brief Returns the link-layer interface index stored in the address. */
uint32
BNetworkAddress::LinkLevelIndex() const
{
	return ((sockaddr_dl&)fAddress).sdl_index;
}


/** @brief Returns the embedded interface name from a link-layer address. */
BString
BNetworkAddress::LinkLevelInterface() const
{
	sockaddr_dl& address = (sockaddr_dl&)fAddress;
	if (address.sdl_nlen == 0)
		return "";

	BString name;
	name.SetTo((const char*)address.sdl_data, address.sdl_nlen);

	return name;
}


/** @brief Returns the link-layer hardware type byte. */
uint8
BNetworkAddress::LinkLevelType() const
{
	return ((sockaddr_dl&)fAddress).sdl_type;
}


/** @brief Returns the link-layer Ethernet frame type in host byte order. */
uint16
BNetworkAddress::LinkLevelFrameType() const
{
	return ntohs(((sockaddr_dl&)fAddress).sdl_e_type);
}


/** @brief Returns a pointer to the raw link-layer (MAC) bytes. */
uint8*
BNetworkAddress::LinkLevelAddress() const
{
	return LLADDR(&(sockaddr_dl&)fAddress);
}


/** @brief Returns the length in bytes of the link-layer address. */
size_t
BNetworkAddress::LinkLevelAddressLength() const
{
	return ((sockaddr_dl&)fAddress).sdl_alen;
}


/** @brief Uses the kernel routing table to choose a local source address
 *         appropriate for reaching the given destination. Only effective
 *         when the current address is a wildcard.
 *  @param destination Target address to route to.
 *  @return B_OK on success, B_BAD_VALUE on family mismatch, or errno. */
status_t
BNetworkAddress::ResolveForDestination(const BNetworkAddress& destination)
{
	if (!IsWildcard())
		return B_OK;
	if (destination.fAddress.ss_family != fAddress.ss_family)
		return B_BAD_VALUE;

	char buffer[2048];
	memset(buffer, 0, sizeof(buffer));

	route_entry* route = (route_entry*)buffer;
	route->destination = (sockaddr*)&destination.fAddress;

	int socket = ::socket(fAddress.ss_family, SOCK_DGRAM, 0);
	if (socket < 0)
		return errno;

	if (ioctl(socket, SIOCGETRT, route, sizeof(buffer)) != 0) {
		close(socket);
		return errno;
	}

	uint16 port = Port();
	memcpy(&fAddress, route->source, sizeof(sockaddr_storage));
	SetPort(port);

	close(socket);
	return B_OK;
}


/** @brief Replaces a wildcard address with an explicit address while
 *         preserving the port. No-op if the address is not a wildcard.
 *  @return B_OK on success, B_BAD_VALUE if the families differ. */
status_t
BNetworkAddress::ResolveTo(const BNetworkAddress& address)
{
	if (!IsWildcard())
		return B_OK;
	if (address.fAddress.ss_family != fAddress.ss_family)
		return B_BAD_VALUE;

	uint16 port = Port();
	*this = address;
	SetPort(port);

	return B_OK;
}


/** @brief Returns a human-readable string form of the address.
 *  @param includePort If true, append the port number; IPv6 addresses are
 *                     wrapped in square brackets.
 *  @return Text representation, or an empty string for unsupported families. */
BString
BNetworkAddress::ToString(bool includePort) const
{
	char buffer[512];

	switch (fAddress.ss_family) {
		case AF_INET:
			inet_ntop(AF_INET, &((sockaddr_in&)fAddress).sin_addr, buffer,
				sizeof(buffer));
			break;

		case AF_INET6:
			inet_ntop(AF_INET6, &((sockaddr_in6&)fAddress).sin6_addr,
				buffer, sizeof(buffer));
			break;

		case AF_LINK:
		{
			uint8 *byte = LinkLevelAddress();
			char* target = buffer;
			int bytesLeft = sizeof(buffer);
			target[0] = '\0';

			for (size_t i = 0; i < LinkLevelAddressLength(); i++) {
				if (i != 0 && bytesLeft > 1) {
					target[0] = ':';
					target[1] = '\0';
					target++;
					bytesLeft--;
				}

				int bytesWritten = snprintf(target, bytesLeft, "%02x", byte[i]);
				if (bytesWritten >= bytesLeft)
					break;

				target += bytesWritten;
				bytesLeft -= bytesWritten;
			}
			break;
		}

		default:
			return "";
	}

	BString address = buffer;
	if (includePort && Port() != 0) {
		if (fAddress.ss_family == AF_INET6) {
			address = "[";
			address += buffer;
			address += "]";
		}

		snprintf(buffer, sizeof(buffer), ":%u", Port());
		address += buffer;
	}

	return address;
}


/** @brief Returns the hostname cached from SetTo(), if any.
 *  @note Reverse DNS lookup is not yet implemented. */
BString
BNetworkAddress::HostName() const
{
	// TODO: implement host name lookup
	return fHostName;
}


/** @brief Returns the service name (or numeric port) for the stored port.
 *  @note Service name lookup is not yet implemented — returns the number. */
BString
BNetworkAddress::ServiceName() const
{
	// TODO: implement service lookup
	BString portName;
	portName << Port();
	return portName;
}


/** @brief Tests structural equality with another address.
 *  @param other       Address to compare against.
 *  @param includePort If true, the port numbers must also match.
 *  @return true if the addresses refer to the same host (and port). */
bool
BNetworkAddress::Equals(const BNetworkAddress& other, bool includePort) const
{
	if (IsEmpty() && other.IsEmpty())
		return true;

	if (Family() != other.Family()
			|| (includePort && Port() != other.Port())) {
		return false;
	}

	switch (fAddress.ss_family) {
		case AF_INET:
		{
			sockaddr_in& address = (sockaddr_in&)fAddress;
			sockaddr_in& otherAddress = (sockaddr_in&)other.fAddress;
			return memcmp(&address.sin_addr, &otherAddress.sin_addr,
				sizeof(address.sin_addr)) == 0;
		}

		case AF_INET6:
		{
			sockaddr_in6& address = (sockaddr_in6&)fAddress;
			sockaddr_in6& otherAddress = (sockaddr_in6&)other.fAddress;
			return memcmp(&address.sin6_addr, &otherAddress.sin6_addr,
				sizeof(address.sin6_addr)) == 0;
		}

		default:
			if (fAddress.ss_len != other.fAddress.ss_len)
				return false;

			return memcmp(&fAddress, &other.fAddress, fAddress.ss_len);
	}
}


// #pragma mark - BFlattenable implementation


/** @brief BFlattenable contract: addresses have variable size, so returns false. */
bool
BNetworkAddress::IsFixedSize() const
{
	return false;
}


/** @brief Returns the BFlattenable type code for network addresses. */
type_code
BNetworkAddress::TypeCode() const
{
	return B_NETWORK_ADDRESS_TYPE;
}


/** @brief Returns the number of bytes needed to flatten this address. */
ssize_t
BNetworkAddress::FlattenedSize() const
{
	return Length();
}


/** @brief Flattens the address into a caller-provided buffer.
 *  @param buffer Destination memory.
 *  @param size   Capacity of @a buffer in bytes.
 *  @return B_OK on success, or B_BAD_VALUE if @a size is too small. */
status_t
BNetworkAddress::Flatten(void* buffer, ssize_t size) const
{
	if (buffer == NULL || size < FlattenedSize())
		return B_BAD_VALUE;

	memcpy(buffer, &fAddress, Length());
	return B_OK;
}


/** @brief Restores a flattened address from the given byte buffer.
 *  @param code   Type code of the flattened data.
 *  @param buffer Source bytes previously produced by Flatten().
 *  @param size   Size of @a buffer in bytes.
 *  @return B_OK on success, B_BAD_VALUE on malformed data, or B_BAD_TYPE
 *          if the type code is not accepted. */
status_t
BNetworkAddress::Unflatten(type_code code, const void* buffer, ssize_t size)
{
	// 2 bytes minimum for family, and length
	if (buffer == NULL || size < 2)
		return fStatus = B_BAD_VALUE;
	if (!AllowsTypeCode(code))
		return fStatus = B_BAD_TYPE;

	memcpy(&fAddress, buffer, min_c(size, (ssize_t)sizeof(fAddress)));

	// check if this can contain a valid address
	if (fAddress.ss_family != AF_UNSPEC && size < (ssize_t)sizeof(sockaddr))
		return fStatus = B_BAD_VALUE;

	return fStatus = B_OK;
}


// #pragma mark - operators


/** @brief Assignment operator. Performs a deep copy including hostname. */
BNetworkAddress&
BNetworkAddress::operator=(const BNetworkAddress& other)
{
	memcpy(&fAddress, &other.fAddress, other.fAddress.ss_len);
	fHostName = other.fHostName;
	fStatus = other.fStatus;

	return *this;
}


/** @brief Equality operator; comparison includes the port. */
bool
BNetworkAddress::operator==(const BNetworkAddress& other) const
{
	return Equals(other);
}


/** @brief Inequality operator; comparison includes the port. */
bool
BNetworkAddress::operator!=(const BNetworkAddress& other) const
{
	return !Equals(other);
}


/** @brief Strict-weak ordering for BNetworkAddress, making it usable as a
 *         key in std::map and similar containers. */
bool
BNetworkAddress::operator<(const BNetworkAddress& other) const
{
	if (Family() < other.Family())
		return true;
	if (Family() > other.Family())
		return false;

	int compare;

	switch (fAddress.ss_family) {
		default:
		case AF_INET:
		{
			sockaddr_in& address = (sockaddr_in&)fAddress;
			sockaddr_in& otherAddress = (sockaddr_in&)other.fAddress;
			compare = memcmp(&address.sin_addr, &otherAddress.sin_addr,
				sizeof(address.sin_addr));
			break;
		}

		case AF_INET6:
		{
			sockaddr_in6& address = (sockaddr_in6&)fAddress;
			sockaddr_in6& otherAddress = (sockaddr_in6&)other.fAddress;
			compare = memcmp(&address.sin6_addr, &otherAddress.sin6_addr,
				sizeof(address.sin6_addr));
			break;
		}

		case AF_LINK:
			if (LinkLevelAddressLength() < other.LinkLevelAddressLength())
				return true;
			if (LinkLevelAddressLength() > other.LinkLevelAddressLength())
				return true;

			// TODO: could compare index, and name, too
			compare = memcmp(LinkLevelAddress(), other.LinkLevelAddress(),
				LinkLevelAddressLength());
			break;
	}

	if (compare < 0)
		return true;
	if (compare > 0)
		return false;

	return Port() < other.Port();
}


BNetworkAddress::operator const sockaddr*() const
{
	return (const sockaddr*)&fAddress;
}


BNetworkAddress::operator const sockaddr&() const
{
	return (const sockaddr&)fAddress;
}


BNetworkAddress::operator sockaddr*()
{
	return (sockaddr*)&fAddress;
}


BNetworkAddress::operator const sockaddr*()
{
	return (sockaddr*)&fAddress;
}


BNetworkAddress::operator sockaddr&()
{
	return (sockaddr&)fAddress;
}


BNetworkAddress::operator const sockaddr&()
{
	return (sockaddr&)fAddress;
}


// #pragma mark - private


status_t
BNetworkAddress::_ParseLinkAddress(const char* address)
{
	if (address == NULL)
		return B_BAD_VALUE;

	uint8 linkAddress[128];
	uint32 length = 0;
	while (length < sizeof(linkAddress)) {
		if (!isxdigit(address[0]) || !isxdigit(address[1]))
			return B_BAD_VALUE;

		linkAddress[length++] = (from_hex(address[0]) << 4)
			| from_hex(address[1]);

		if (address[2] == '\0')
			break;
		if (address[2] != ':')
			return B_BAD_VALUE;

		address += 3;
	}

	fHostName = address;

	SetToLinkLevel(linkAddress, length);
	return B_OK;
}
