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
 *   Copyright 2013-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file NetworkRoute.cpp
 * @brief Implementation of BNetworkRoute, a kernel routing-table entry wrapper.
 *
 * BNetworkRoute wraps a route_entry and manages the heap-allocated sockaddr
 * objects for destination, mask, gateway, and source addresses.  It also
 * provides static helpers for querying the kernel routing table via ioctls.
 *
 * @see BNetworkInterface, BNetworkAddress
 */


#include <NetworkRoute.h>

#include <errno.h>
#include <net/if.h>
#include <string.h>
#include <sys/sockio.h>

#include <AutoDeleter.h>


/**
 * @brief Construct a default BNetworkRoute with all fields zeroed.
 */
BNetworkRoute::BNetworkRoute()
{
	memset(&fRouteEntry, 0, sizeof(route_entry));
}


/**
 * @brief Destructor — frees all dynamically allocated address fields.
 */
BNetworkRoute::~BNetworkRoute()
{
	UnsetDestination();
	UnsetMask();
	UnsetGateway();
	UnsetSource();
}


/**
 * @brief Copy the route from another BNetworkRoute object.
 *
 * @param other  The source route to copy from.
 * @return B_OK on success, or an error code if address allocation fails.
 */
status_t
BNetworkRoute::SetTo(const BNetworkRoute& other)
{
	return SetTo(other.RouteEntry());
}


/**
 * @brief Populate this route from a raw route_entry structure.
 *
 * Deep-copies each non-NULL address field from \a routeEntry, sets the
 * flags and MTU, and leaves source unset (the kernel does not provide it
 * in routing-table dumps).
 *
 * @param routeEntry  The route_entry to copy data from.
 * @return B_OK on success, or an error code if address allocation fails.
 */
status_t
BNetworkRoute::SetTo(const route_entry& routeEntry)
{
	#define SET_ADDRESS(address, setFunction) \
		if (routeEntry.address != NULL) { \
			result = setFunction(*routeEntry.address); \
			if (result != B_OK) \
				return result; \
		}

	status_t result;
	SET_ADDRESS(destination, SetDestination)
	SET_ADDRESS(mask, SetMask)
	SET_ADDRESS(gateway, SetGateway)
	SET_ADDRESS(source, SetSource)

	SetFlags(routeEntry.flags);
	SetMTU(routeEntry.mtu);
	return B_OK;
}


/**
 * @brief Transfer ownership of all address fields from another route.
 *
 * Performs a shallow copy of \a other's internal route_entry and then
 * zeroes \a other so it no longer owns any addresses.
 *
 * @param other  The route to adopt; its fields are cleared on return.
 */
void
BNetworkRoute::Adopt(BNetworkRoute& other)
{
	memcpy(&fRouteEntry, &other.fRouteEntry, sizeof(route_entry));
	memset(&other.fRouteEntry, 0, sizeof(route_entry));
}


/**
 * @brief Return a const reference to the underlying route_entry structure.
 *
 * @return Const reference to the internal route_entry.
 */
const route_entry&
BNetworkRoute::RouteEntry() const
{
	return fRouteEntry;
}


/**
 * @brief Return the destination address of the route, or NULL if unset.
 *
 * @return Pointer to the destination sockaddr, or NULL.
 */
const sockaddr*
BNetworkRoute::Destination() const
{
	return fRouteEntry.destination;
}


/**
 * @brief Set the destination address of the route.
 *
 * @param destination  The destination address to copy and store.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
BNetworkRoute::SetDestination(const sockaddr& destination)
{
	return _AllocateAndSetAddress(destination, fRouteEntry.destination);
}


/**
 * @brief Clear the destination address and release its memory.
 */
void
BNetworkRoute::UnsetDestination()
{
	_FreeAndUnsetAddress(fRouteEntry.destination);
}


/**
 * @brief Return the route's subnet mask, or NULL if unset.
 *
 * @return Pointer to the mask sockaddr, or NULL.
 */
const sockaddr*
BNetworkRoute::Mask() const
{
	return fRouteEntry.mask;
}


/**
 * @brief Set the subnet mask of the route.
 *
 * @param mask  The mask address to copy and store.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
BNetworkRoute::SetMask(const sockaddr& mask)
{
	return _AllocateAndSetAddress(mask, fRouteEntry.mask);
}


/**
 * @brief Clear the subnet mask and release its memory.
 */
void
BNetworkRoute::UnsetMask()
{
	_FreeAndUnsetAddress(fRouteEntry.mask);
}


/**
 * @brief Return the gateway address of the route, or NULL if unset.
 *
 * @return Pointer to the gateway sockaddr, or NULL.
 */
const sockaddr*
BNetworkRoute::Gateway() const
{
	return fRouteEntry.gateway;
}


/**
 * @brief Set the gateway address of the route.
 *
 * @param gateway  The gateway address to copy and store.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
BNetworkRoute::SetGateway(const sockaddr& gateway)
{
	return _AllocateAndSetAddress(gateway, fRouteEntry.gateway);
}


/**
 * @brief Clear the gateway address and release its memory.
 */
void
BNetworkRoute::UnsetGateway()
{
	_FreeAndUnsetAddress(fRouteEntry.gateway);
}


/**
 * @brief Return the source address of the route, or NULL if unset.
 *
 * @return Pointer to the source sockaddr, or NULL.
 */
const sockaddr*
BNetworkRoute::Source() const
{
	return fRouteEntry.source;
}


/**
 * @brief Set the source address of the route.
 *
 * @param source  The source address to copy and store.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
BNetworkRoute::SetSource(const sockaddr& source)
{
	return _AllocateAndSetAddress(source, fRouteEntry.source);
}


/**
 * @brief Clear the source address and release its memory.
 */
void
BNetworkRoute::UnsetSource()
{
	_FreeAndUnsetAddress(fRouteEntry.source);
}


/**
 * @brief Return the route flags bitmask.
 *
 * @return Current flags value (e.g. RTF_DEFAULT, RTF_GATEWAY).
 */
uint32
BNetworkRoute::Flags() const
{
	return fRouteEntry.flags;
}


/**
 * @brief Set the route flags bitmask.
 *
 * @param flags  New flags value to assign.
 */
void
BNetworkRoute::SetFlags(uint32 flags)
{
	fRouteEntry.flags = flags;
}


/**
 * @brief Return the Maximum Transmission Unit for this route.
 *
 * @return Current MTU value in bytes.
 */
uint32
BNetworkRoute::MTU() const
{
	return fRouteEntry.mtu;
}


/**
 * @brief Set the Maximum Transmission Unit for this route.
 *
 * @param mtu  New MTU value in bytes.
 */
void
BNetworkRoute::SetMTU(uint32 mtu)
{
	fRouteEntry.mtu = mtu;
}


/**
 * @brief Determine the address family of this route from its address fields.
 *
 * Checks destination, mask, gateway, and source in order and returns the
 * family of the first non-AF_UNSPEC address found.
 *
 * @return Address family constant (e.g. AF_INET, AF_INET6), or AF_UNSPEC if
 *         no address is set.
 */
int
BNetworkRoute::AddressFamily() const
{
	#define RETURN_FAMILY_IF_SET(address) \
		if (fRouteEntry.address != NULL \
			&& fRouteEntry.address->sa_family != AF_UNSPEC) { \
			return fRouteEntry.address->sa_family; \
		}

	RETURN_FAMILY_IF_SET(destination)
	RETURN_FAMILY_IF_SET(mask)
	RETURN_FAMILY_IF_SET(gateway)
	RETURN_FAMILY_IF_SET(source)

	return AF_UNSPEC;
}


/**
 * @brief Retrieve the default route for a given address family and interface.
 *
 * Calls GetRoutes() with RTF_DEFAULT and adopts the first matching entry
 * into \a route.
 *
 * @param family         Address family (AF_INET or AF_INET6).
 * @param interfaceName  Optional interface name to filter routes; may be NULL.
 * @param route          Output route populated with the default route.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no default route exists.
 */
status_t
BNetworkRoute::GetDefaultRoute(int family, const char* interfaceName,
	BNetworkRoute& route)
{
	BObjectList<BNetworkRoute, true> routes(1);
	status_t result = GetRoutes(family, interfaceName, RTF_DEFAULT, routes);
	if (result != B_OK)
		return result;

	if (routes.CountItems() == 0)
		return B_ENTRY_NOT_FOUND;

	route.Adopt(*routes.ItemAt(0));
	return B_OK;
}


/**
 * @brief Retrieve the default gateway address for an address family and interface.
 *
 * Finds the default route and copies its gateway address into \a gateway.
 *
 * @param family         Address family (AF_INET or AF_INET6).
 * @param interfaceName  Optional interface name to filter routes; may be NULL.
 * @param gateway        Output sockaddr populated with the gateway address.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no default route or gateway exists.
 */
status_t
BNetworkRoute::GetDefaultGateway(int family, const char* interfaceName,
	sockaddr& gateway)
{
	BNetworkRoute route;
	status_t result = GetDefaultRoute(family, interfaceName, route);
	if (result != B_OK)
		return result;

	const sockaddr* defaultGateway = route.Gateway();
	if (defaultGateway == NULL)
		return B_ENTRY_NOT_FOUND;

	memcpy(&gateway, defaultGateway, defaultGateway->sa_len);
	return B_OK;
}


/**
 * @brief Retrieve all routes for the given address family.
 *
 * Overload that includes all interfaces and applies no flag filter.
 *
 * @param family   Address family to query.
 * @param routes   Output list populated with newly allocated BNetworkRoute objects.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNetworkRoute::GetRoutes(int family, BObjectList<BNetworkRoute, true>& routes)
{
	return GetRoutes(family, NULL, 0, routes);
}


/**
 * @brief Retrieve all routes for the given address family and interface.
 *
 * Overload that applies no flag filter but limits results to \a interfaceName.
 *
 * @param family         Address family to query.
 * @param interfaceName  Interface name to filter by, or NULL for all.
 * @param routes         Output list populated with newly allocated BNetworkRoute objects.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNetworkRoute::GetRoutes(int family, const char* interfaceName,
	BObjectList<BNetworkRoute, true>& routes)
{
	return GetRoutes(family, interfaceName, 0, routes);
}


/**
 * @brief Retrieve routes from the kernel routing table with optional filters.
 *
 * Issues SIOCGRTSIZE and SIOCGRTTABLE ioctls to fetch the full routing table,
 * then filters entries by \a interfaceName and \a filterFlags before appending
 * them to \a routes.
 *
 * @param family         Address family of the socket used to query the kernel.
 * @param interfaceName  If non-NULL, only entries matching this name are included.
 * @param filterFlags    If non-zero, only entries whose flags contain all of these
 *                       bits are included.
 * @param routes         Output list; ownership of each appended BNetworkRoute is
 *                       transferred to the list.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or errno on ioctl error.
 */
status_t
BNetworkRoute::GetRoutes(int family, const char* interfaceName,
	uint32 filterFlags, BObjectList<BNetworkRoute, true>& routes)
{
	FileDescriptorCloser socket(::socket(family, SOCK_DGRAM, 0));
	if (!socket.IsSet())
		return errno;

	ifconf config;
	config.ifc_len = sizeof(config.ifc_value);
	if (ioctl(socket.Get(), SIOCGRTSIZE, &config, sizeof(struct ifconf)) < 0)
		return errno;

	uint32 size = (uint32)config.ifc_value;
	if (size == 0)
		return B_OK;

	void* buffer = malloc(size);
	if (buffer == NULL)
		return B_NO_MEMORY;

	MemoryDeleter bufferDeleter(buffer);
	config.ifc_len = size;
	config.ifc_buf = buffer;

	if (ioctl(socket.Get(), SIOCGRTTABLE, &config, sizeof(struct ifconf)) < 0)
		return errno;

	ifreq* interface = (ifreq*)buffer;
	ifreq* end = (ifreq*)((uint8*)buffer + size);

	while (interface < end) {
		route_entry& routeEntry = interface->ifr_route;

		if ((interfaceName == NULL
				|| strcmp(interface->ifr_name, interfaceName) == 0)
			&& (filterFlags == 0 || (routeEntry.flags & filterFlags) != 0)) {

			BNetworkRoute* route = new(std::nothrow) BNetworkRoute;
			if (route == NULL)
				return B_NO_MEMORY;

			// Note that source is not provided in the buffer.
			routeEntry.source = NULL;

			status_t result = route->SetTo(routeEntry);
			if (result != B_OK) {
				delete route;
				return result;
			}

			if (!routes.AddItem(route)) {
				delete route;
				return B_NO_MEMORY;
			}
		}

		size_t addressSize = 0;
		if (routeEntry.destination != NULL)
			addressSize += routeEntry.destination->sa_len;
		if (routeEntry.mask != NULL)
			addressSize += routeEntry.mask->sa_len;
		if (routeEntry.gateway != NULL)
			addressSize += routeEntry.gateway->sa_len;

		interface = (ifreq *)((addr_t)interface + IF_NAMESIZE
			+ sizeof(route_entry) + addressSize);
	}

	return B_OK;
}


/**
 * @brief Allocate storage for a sockaddr and copy \a from into it.
 *
 * If \a to is NULL, a new sockaddr_storage-sized buffer is allocated.
 * The address is then memcpy'd from \a from.
 *
 * @param from  Source address to copy.
 * @param to    Reference to pointer that receives the (possibly new) buffer.
 * @return B_OK on success, B_BAD_VALUE if sa_len exceeds sockaddr_storage,
 *         or B_NO_MEMORY on allocation failure.
 */
status_t
BNetworkRoute::_AllocateAndSetAddress(const sockaddr& from,
	sockaddr*& to)
{
	if (from.sa_len > sizeof(sockaddr_storage))
		return B_BAD_VALUE;

	if (to == NULL) {
		to = (sockaddr*)malloc(sizeof(sockaddr_storage));
		if (to == NULL)
			return B_NO_MEMORY;
	}

	memcpy(to, &from, from.sa_len);
	return B_OK;
}


/**
 * @brief Free a heap-allocated sockaddr and set the pointer to NULL.
 *
 * @param address  Reference to the pointer to free and nullify.
 */
void
BNetworkRoute::_FreeAndUnsetAddress(sockaddr*& address)
{
	free(address);
	address = NULL;
}
