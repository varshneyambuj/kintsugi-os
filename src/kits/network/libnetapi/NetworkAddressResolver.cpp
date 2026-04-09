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
 *   Copyright 2010-2011, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2015-2017, Adrien Destugues, pulkomandy@pulkomandy.tk.
 *   Distributed under the terms of the MIT License.
 */

/** @file NetworkAddressResolver.cpp
 *  @brief Wrapper around getaddrinfo() with an LRU result cache. Produces
 *         BNetworkAddress objects from hostnames and service names. */


#include <NetworkAddressResolver.h>

#include <errno.h>
#include <netdb.h>

#include <Autolock.h>
#include <NetworkAddress.h>


/** @brief Splits a "host:port" string into separate host and port components.
 *         IPv6 literals must be enclosed in brackets (e.g. "[::1]:80") to
 *         avoid being misinterpreted.
 *  @param host In/out: input "host[:port]" string; on return contains only the host.
 *  @param port On output: the extracted port substring (may remain empty).
 *  @return true if a port was extracted, false otherwise. */
static bool
strip_port(BString& host, BString& port)
{
	int32 first = host.FindFirst(':');
	int32 separator = host.FindLast(':');
	if (separator != first
			&& (separator == 0 || host.ByteAt(separator - 1) != ']')) {
		return false;
	}

	if (separator != -1) {
		// looks like there is a port
		host.CopyInto(port, separator + 1, -1);
		host.Truncate(separator);

		return true;
	}

	return false;
}


// #pragma mark -


/** @brief Default constructor. Builds an empty resolver in B_NO_INIT state. */
BNetworkAddressResolver::BNetworkAddressResolver()
	:
	BReferenceable(),
	fInfo(NULL),
	fStatus(B_NO_INIT)
{
}


/** @brief Constructs and immediately resolves @a address on @a port. */
BNetworkAddressResolver::BNetworkAddressResolver(const char* address,
	uint16 port, uint32 flags)
	:
	BReferenceable(),
	fInfo(NULL),
	fStatus(B_NO_INIT)
{
	SetTo(address, port, flags);
}

/** @brief Constructs and immediately resolves @a address for @a service. */
BNetworkAddressResolver::BNetworkAddressResolver(const char* address,
	const char* service, uint32 flags)
	:
	BReferenceable(),
	fInfo(NULL),
	fStatus(B_NO_INIT)
{
	SetTo(address, service, flags);
}


/** @brief Constructs and resolves @a address on @a port restricted to @a family. */
BNetworkAddressResolver::BNetworkAddressResolver(int family,
	const char* address, uint16 port, uint32 flags)
	:
	BReferenceable(),
	fInfo(NULL),
	fStatus(B_NO_INIT)
{
	SetTo(family, address, port, flags);
}


/** @brief Constructs and resolves @a address for @a service restricted to @a family. */
BNetworkAddressResolver::BNetworkAddressResolver(int family,
	const char* address, const char* service, uint32 flags)
	:
	BReferenceable(),
	fInfo(NULL),
	fStatus(B_NO_INIT)
{
	SetTo(family, address, service, flags);
}


/** @brief Destructor. Frees any cached getaddrinfo result. */
BNetworkAddressResolver::~BNetworkAddressResolver()
{
	Unset();
}


/** @brief Returns the status of the last resolution attempt. */
status_t
BNetworkAddressResolver::InitCheck() const
{
	return fStatus;
}


/** @brief Clears any cached resolution results and resets state. */
void
BNetworkAddressResolver::Unset()
{
	if (fInfo != NULL) {
		freeaddrinfo(fInfo);
		fInfo = NULL;
	}
	fStatus = B_NO_INIT;
}


/** @brief Resolves @a address on @a port with AF_UNSPEC. */
status_t
BNetworkAddressResolver::SetTo(const char* address, uint16 port, uint32 flags)
{
	return SetTo(AF_UNSPEC, address, port, flags);
}


/** @brief Resolves @a address for @a service with AF_UNSPEC. */
status_t
BNetworkAddressResolver::SetTo(const char* address, const char* service,
	uint32 flags)
{
	return SetTo(AF_UNSPEC, address, service, flags);
}


/** @brief Resolves @a address on @a port for the given family. */
status_t
BNetworkAddressResolver::SetTo(int family, const char* address, uint16 port,
	uint32 flags)
{
	BString service;
	service << port;

	return SetTo(family, address, port != 0 ? service.String() : NULL, flags);
}


/** @brief Core resolution primitive: calls getaddrinfo() and caches the result.
 *         Splits an embedded "host:port" out of @a host and respects
 *         B_NO_ADDRESS_RESOLUTION, B_UNCONFIGURED_ADDRESS_FAMILIES, and
 *         AI_PASSIVE semantics via the @a flags parameter.
 *  @param family  Preferred address family or AF_UNSPEC.
 *  @param host    Hostname, IP literal, or NULL for passive binding.
 *  @param service Named service or numeric port string; may be NULL.
 *  @param flags   B_NO_ADDRESS_RESOLUTION / B_UNCONFIGURED_ADDRESS_FAMILIES.
 *  @return B_OK on success, or a status_t mapped from the getaddrinfo
 *          error code. */
status_t
BNetworkAddressResolver::SetTo(int family, const char* host,
	const char* service, uint32 flags)
{
	Unset();

	// Check if the address contains a port

	BString hostString(host);

	BString portString;
	if (!strip_port(hostString, portString) && service != NULL)
		portString = service;

	// Resolve address

	addrinfo hint = {0};
	hint.ai_family = family;
	if ((flags & B_NO_ADDRESS_RESOLUTION) != 0)
		hint.ai_flags |= AI_NUMERICHOST;
	else if ((flags & B_UNCONFIGURED_ADDRESS_FAMILIES) == 0)
		hint.ai_flags |= AI_ADDRCONFIG;

	if (host == NULL && portString.Length() == 0) {
		portString = "0";
		hint.ai_flags |= AI_PASSIVE;
	}

	int status = getaddrinfo(host != NULL ? hostString.String() : NULL,
		portString.Length() != 0 ? portString.String() : NULL, &hint, &fInfo);
	if (status == 0)
		return fStatus = B_OK;

	// Map errors
	// TODO: improve error reporting, maybe add specific error codes?

	switch (status) {
		case EAI_ADDRFAMILY:
		case EAI_BADFLAGS:
		case EAI_PROTOCOL:
		case EAI_BADHINTS:
		case EAI_SOCKTYPE:
		case EAI_SERVICE:
		case EAI_NONAME:
		case EAI_FAMILY:
			fStatus = B_BAD_VALUE;
			break;

		case EAI_SYSTEM:
			fStatus = errno;
			break;

		case EAI_OVERFLOW:
		case EAI_MEMORY:
			fStatus = B_NO_MEMORY;
			break;

		case EAI_AGAIN:
			// TODO: better error code to denote temporary failure?
			fStatus = B_TIMED_OUT;
			break;

		default:
			fStatus = B_ERROR;
			break;
	}

	return fStatus;
}


/** @brief Iterates over the resolved addrinfo list without family filtering.
 *  @param cookie  In/out cursor; caller should start at 0.
 *  @param address On success, set to the next resolved address.
 *  @return B_OK on success, or B_BAD_VALUE when the iterator is exhausted. */
status_t
BNetworkAddressResolver::GetNextAddress(uint32* cookie,
	BNetworkAddress& address) const
{
	if (fStatus != B_OK)
		return fStatus;

	// Skip previous info entries

	addrinfo* info = fInfo;
	int32 first = *cookie;
	for (int32 index = 0; index < first && info != NULL; index++) {
		info = info->ai_next;
	}

	if (info == NULL)
		return B_BAD_VALUE;

	// Return current

	address.SetTo(*info->ai_addr, info->ai_addrlen);
	(*cookie)++;

	return B_OK;
}


/** @brief Iterates over the resolved addrinfo list, skipping entries whose
 *         family does not match @a family.
 *  @param family  Address family to include (AF_INET, AF_INET6, ...).
 *  @param cookie  In/out cursor; caller should start at 0.
 *  @param address On success, set to the next matching address.
 *  @return B_OK on success, or B_BAD_VALUE when the iterator is exhausted. */
status_t
BNetworkAddressResolver::GetNextAddress(int family, uint32* cookie,
	BNetworkAddress& address) const
{
	if (fStatus != B_OK)
		return fStatus;

	// Skip previous info entries, and those that have a non-matching family

	addrinfo* info = fInfo;
	int32 first = *cookie;
	for (int32 index = 0; index < first && info != NULL; index++)
		info = info->ai_next;

	while (info != NULL && info->ai_family != family)
		info = info->ai_next;

	if (info == NULL)
		return B_BAD_VALUE;

	// Return current

	address.SetTo(*info->ai_addr, info->ai_addrlen);
	(*cookie)++;

	return B_OK;
}


/** @brief Cached resolver factory. AF_UNSPEC host/service variant. */
/*static*/ BReference<const BNetworkAddressResolver>
BNetworkAddressResolver::Resolve(const char* address, const char* service,
	uint32 flags)
{
	return Resolve(AF_UNSPEC, address, service, flags);
}


/** @brief Cached resolver factory. AF_UNSPEC host/port variant. */
/*static*/ BReference<const BNetworkAddressResolver>
BNetworkAddressResolver::Resolve(const char* address, uint16 port, uint32 flags)
{
	return Resolve(AF_UNSPEC, address, port, flags);
}


/** @brief Cached resolver factory. Family-specific host/port variant. */
/*static*/ BReference<const BNetworkAddressResolver>
BNetworkAddressResolver::Resolve(int family, const char* address,
	uint16 port, uint32 flags)
{
	BString service;
	service << port;

	return Resolve(family, address, port == 0 ? NULL : service.String(), flags);
}


/** @brief Core cached resolver factory. Looks up an MRU cache keyed on
 *         (family, host, service, flags); on a miss, resolves synchronously
 *         and inserts the result, evicting the oldest entry if the cache
 *         is full (currently ~256 entries).
 *  @return A reference-counted resolver that can be shared across callers. */
/*static*/ BReference<const BNetworkAddressResolver>
BNetworkAddressResolver::Resolve(int family, const char* address,
	const char* service, uint32 flags)
{
	BAutolock locker(&sCacheLock);

	// TODO it may be faster to use an hash map to have faster lookup of the
	// cache. However, we also need to access the cache by LRU, and for that
	// a doubly-linked list is better. We should have these two share the same
	// items, so it's easy to remove the LRU from the map, or insert a new
	// item in both structures.
	for (int i = 0; i < sCacheMap.CountItems(); i++) {
		CacheEntry* entry = sCacheMap.ItemAt(i);
		if (entry->Matches(family, address, service, flags)) {
			// This entry is now the MRU, move to end of list.
			// TODO if the item is old (more than 1 minute), it should be
			// dropped and a new request made.
			sCacheMap.MoveItem(i, sCacheMap.CountItems());
			return entry->fResolver;
		}
	}

	// Cache miss! Unlock the cache while we perform the costly address
	// resolution
	locker.Unlock();

	BNetworkAddressResolver* resolver = new(std::nothrow)
		BNetworkAddressResolver(family, address, service, flags);

	if (resolver != NULL && resolver->InitCheck() == B_OK) {
		CacheEntry* entry = new(std::nothrow) CacheEntry(family, address,
			service, flags, resolver);

		locker.Lock();
		// TODO adjust capacity. Chrome uses 256 entries with a timeout of
		// 1 minute, IE uses 1000 entries with a timeout of 30 seconds.
		if (sCacheMap.CountItems() > 255)
			delete sCacheMap.RemoveItemAt(0);

		if (entry)
			sCacheMap.AddItem(entry, sCacheMap.CountItems());
	}

	return BReference<const BNetworkAddressResolver>(resolver, true);
}

BLocker BNetworkAddressResolver::sCacheLock("DNS cache");
BObjectList<BNetworkAddressResolver::CacheEntry>
	BNetworkAddressResolver::sCacheMap;
