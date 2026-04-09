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
 *   Copyright 2010, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file NetworkInterface.cpp
 *  @brief BNetworkInterface / BNetworkInterfaceAddress implementation,
 *         wrapping the SIOCGIF* / SIOCSIF* ioctls used to query and
 *         configure network interfaces. */

#include <NetworkInterface.h>

#include <errno.h>
#include <net/if.h>
#include <sys/sockio.h>

#include <AutoDeleter.h>
#include <Messenger.h>
#include <NetServer.h>
#include <NetworkRoute.h>


/** @brief Deduces the protocol family implied by an interface address
 *         record. Prefers the address field, then the netmask, then the
 *         destination, falling back to AF_INET. */
static int
family_from_interface_address(const BNetworkInterfaceAddress& address)
{
	if (address.Address().Family() != AF_UNSPEC)
		return address.Address().Family();
	if (address.Mask().Family() != AF_UNSPEC)
		return address.Mask().Family();
	if (address.Destination().Family() != AF_UNSPEC)
		return address.Destination().Family();

	return AF_INET;
}


/** @brief Executes an ifaliasreq ioctl (SIOCAIFADDR/SIOCDIFADDR/SIOCGIFALIAS).
 *  @param name     Interface name to target.
 *  @param option   Ioctl number to perform.
 *  @param address  In/out interface address record.
 *  @param readBack If true, copy the result back into @a address. */
static status_t
do_ifaliasreq(const char* name, int32 option, BNetworkInterfaceAddress& address,
	bool readBack = false)
{
	int family = AF_INET;
	if (!readBack)
		family = family_from_interface_address(address);

	FileDescriptorCloser socket(::socket(family, SOCK_DGRAM, 0));
	if (!socket.IsSet())
		return errno;

	ifaliasreq request;
	strlcpy(request.ifra_name, name, IF_NAMESIZE);
	request.ifra_index = address.Index();
	request.ifra_flags = address.Flags();

	memcpy(&request.ifra_addr, &address.Address().SockAddr(),
		address.Address().Length());
	memcpy(&request.ifra_mask, &address.Mask().SockAddr(),
		address.Mask().Length());
	memcpy(&request.ifra_broadaddr, &address.Broadcast().SockAddr(),
		address.Broadcast().Length());

	if (ioctl(socket.Get(), option, &request, sizeof(struct ifaliasreq)) < 0)
		return errno;

	if (readBack) {
		address.SetFlags(request.ifra_flags);
		address.Address().SetTo(request.ifra_addr);
		address.Mask().SetTo(request.ifra_mask);
		address.Broadcast().SetTo(request.ifra_broadaddr);
	}

	return B_OK;
}


/** @brief Const overload of do_ifaliasreq() used by write-only ioctls. */
static status_t
do_ifaliasreq(const char* name, int32 option,
	const BNetworkInterfaceAddress& address)
{
	return do_ifaliasreq(name, option,
		const_cast<BNetworkInterfaceAddress&>(address));
}


/** @brief Generic ioctl helper that opens a socket of the given @a family
 *         and performs the request on @a name. */
template<typename T> status_t
do_request(int family, T& request, const char* name, int option)
{
	FileDescriptorCloser socket(::socket(family, SOCK_DGRAM, 0));
	if (!socket.IsSet())
		return errno;

	strlcpy(((struct ifreq&)request).ifr_name, name, IF_NAMESIZE);

	if (ioctl(socket.Get(), option, &request, sizeof(T)) < 0)
		return errno;

	return B_OK;
}


// #pragma mark -


/** @brief Default constructor. Represents an empty, unbound alias record. */
BNetworkInterfaceAddress::BNetworkInterfaceAddress()
	:
	fIndex(-1),
	fFlags(0)
{
}


/** @brief Destructor. */
BNetworkInterfaceAddress::~BNetworkInterfaceAddress()
{
}


/** @brief Populates this record from the @a index'th alias on @a interface. */
status_t
BNetworkInterfaceAddress::SetTo(const BNetworkInterface& interface, int32 index)
{
	fIndex = index;
	return do_ifaliasreq(interface.Name(), B_SOCKET_GET_ALIAS, *this, true);
}


/** @brief Sets the primary address field. */
void
BNetworkInterfaceAddress::SetAddress(const BNetworkAddress& address)
{
	fAddress = address;
}


/** @brief Sets the netmask associated with the address. */
void
BNetworkInterfaceAddress::SetMask(const BNetworkAddress& mask)
{
	fMask = mask;
}


/** @brief Sets the broadcast address for the alias. */
void
BNetworkInterfaceAddress::SetBroadcast(const BNetworkAddress& broadcast)
{
	fBroadcast = broadcast;
}


/** @brief Sets the point-to-point destination address. Shares storage with
 *         the broadcast field because BSD interfaces use one or the other. */
void
BNetworkInterfaceAddress::SetDestination(const BNetworkAddress& destination)
{
	fBroadcast = destination;
}


/** @brief Sets the per-alias flag bitmask (IFAF_AUTOCONFIGURED, etc). */
void
BNetworkInterfaceAddress::SetFlags(uint32 flags)
{
	fFlags = flags;
}


// #pragma mark -


/** @brief Default constructor. Builds an unbound interface handle. */
BNetworkInterface::BNetworkInterface()
{
	Unset();
}


/** @brief Constructs a handle bound to the interface named @a name. */
BNetworkInterface::BNetworkInterface(const char* name)
{
	SetTo(name);
}


/** @brief Constructs a handle bound to the interface with @a index. */
BNetworkInterface::BNetworkInterface(uint32 index)
{
	SetTo(index);
}


/** @brief Destructor. */
BNetworkInterface::~BNetworkInterface()
{
}


/** @brief Clears the bound interface name. */
void
BNetworkInterface::Unset()
{
	fName[0] = '\0';
}


/** @brief Binds this handle to @a name. */
void
BNetworkInterface::SetTo(const char* name)
{
	strlcpy(fName, name, IF_NAMESIZE);
}


/** @brief Binds this handle to the interface whose index is @a index.
 *         Resolves the index to a name via SIOCGIFNAME. */
status_t
BNetworkInterface::SetTo(uint32 index)
{
	ifreq request;
	request.ifr_index = index;

	status_t status = do_request(AF_INET, request, "", SIOCGIFNAME);
	if (status != B_OK)
		return status;

	strlcpy(fName, request.ifr_name, IF_NAMESIZE);
	return B_OK;
}


/** @brief Reports whether the bound interface still exists in the kernel. */
bool
BNetworkInterface::Exists() const
{
	ifreq request;
	return do_request(AF_INET, request, Name(), SIOCGIFINDEX) == B_OK;
}


/** @brief Returns the bound interface name. */
const char*
BNetworkInterface::Name() const
{
	return fName;
}


/** @brief Returns the kernel index assigned to the bound interface. */
uint32
BNetworkInterface::Index() const
{
	ifreq request;
	if (do_request(AF_INET, request, Name(), SIOCGIFINDEX) != B_OK)
		return 0;

	return request.ifr_index;
}


/** @brief Returns the interface flags (IFF_UP, IFF_BROADCAST, ...). */
uint32
BNetworkInterface::Flags() const
{
	ifreq request;
	if (do_request(AF_INET, request, Name(), SIOCGIFFLAGS) != B_OK)
		return 0;

	return request.ifr_flags;
}


/** @brief Returns the interface MTU in bytes. */
uint32
BNetworkInterface::MTU() const
{
	ifreq request;
	if (do_request(AF_INET, request, Name(), SIOCGIFMTU) != B_OK)
		return 0;

	return request.ifr_mtu;
}


/** @brief Returns the IFM_* media type/subtype of the interface. */
int32
BNetworkInterface::Media() const
{
	ifreq request;
	if (do_request(AF_INET, request, Name(), SIOCGIFMEDIA) != B_OK)
		return -1;

	return request.ifr_media;
}


uint32
BNetworkInterface::Metric() const
{
	ifreq request;
	if (do_request(AF_INET, request, Name(), SIOCGIFMETRIC) != B_OK)
		return 0;

	return request.ifr_metric;
}


uint32
BNetworkInterface::Type() const
{
	ifreq request;
	if (do_request(AF_INET, request, Name(), SIOCGIFTYPE) != B_OK)
		return 0;

	return request.ifr_type;
}


/** @brief Retrieves interface packet/byte/error counters via SIOCGIFSTATS. */
status_t
BNetworkInterface::GetStats(ifreq_stats& stats)
{
	ifreq request;
	status_t status = do_request(AF_INET, request, Name(), SIOCGIFSTATS);
	if (status != B_OK)
		return status;

	memcpy(&stats, &request.ifr_stats, sizeof(ifreq_stats));
	return B_OK;
}


/** @brief Reports whether the interface is physically up (IFF_LINK is set). */
bool
BNetworkInterface::HasLink() const
{
	return (Flags() & IFF_LINK) != 0;
}


/** @brief Replaces the interface flag bitmask. */
status_t
BNetworkInterface::SetFlags(uint32 flags)
{
	ifreq request;
	request.ifr_flags = flags;
	return do_request(AF_INET, request, Name(), SIOCSIFFLAGS);
}


/** @brief Sets the interface MTU in bytes. */
status_t
BNetworkInterface::SetMTU(uint32 mtu)
{
	ifreq request;
	request.ifr_mtu = mtu;
	return do_request(AF_INET, request, Name(), SIOCSIFMTU);
}


/** @brief Forces the interface into a specific media mode. */
status_t
BNetworkInterface::SetMedia(int32 media)
{
	ifreq request;
	request.ifr_media = media;
	return do_request(AF_INET, request, Name(), SIOCSIFMEDIA);
}


/** @brief Sets the interface routing metric. */
status_t
BNetworkInterface::SetMetric(uint32 metric)
{
	ifreq request;
	request.ifr_metric = metric;
	return do_request(AF_INET, request, Name(), SIOCSIFMETRIC);
}


/** @brief Returns the number of configured address aliases on the interface. */
int32
BNetworkInterface::CountAddresses() const
{
	ifreq request;
	if (do_request(AF_INET, request, Name(), B_SOCKET_COUNT_ALIASES) != B_OK)
		return 0;

	return request.ifr_count;
}


/** @brief Fetches the @a index'th address alias of the interface.
 *  @param index   Zero-based alias index.
 *  @param address On success, populated with the alias details. */
status_t
BNetworkInterface::GetAddressAt(int32 index, BNetworkInterfaceAddress& address)
{
	return address.SetTo(*this, index);
}


/** @brief Locates the alias index whose primary address equals @a address.
 *  @return The alias index on success, or -1 if no match was found. */
int32
BNetworkInterface::FindAddress(const BNetworkAddress& address)
{
	FileDescriptorCloser socket(::socket(address.Family(), SOCK_DGRAM, 0));
	if (!socket.IsSet())
		return -1;

	ifaliasreq request;
	memset(&request, 0, sizeof(ifaliasreq));

	strlcpy(request.ifra_name, Name(), IF_NAMESIZE);
	request.ifra_index = -1;
	memcpy(&request.ifra_addr, &address.SockAddr(), address.Length());

	if (ioctl(socket.Get(), B_SOCKET_GET_ALIAS, &request,
		sizeof(struct ifaliasreq)) < 0) {
		return -1;
	}

	return request.ifra_index;
}


/** @brief Locates the first alias with the given address family.
 *  @return The alias index on success, or -1 if no match was found. */
int32
BNetworkInterface::FindFirstAddress(int family)
{
	FileDescriptorCloser socket(::socket(family, SOCK_DGRAM, 0));
	if (!socket.IsSet())
		return -1;

	ifaliasreq request;
	memset(&request, 0, sizeof(ifaliasreq));

	strlcpy(request.ifra_name, Name(), IF_NAMESIZE);
	request.ifra_index = -1;
	request.ifra_addr.ss_family = AF_UNSPEC;

	if (ioctl(socket.Get(), B_SOCKET_GET_ALIAS, &request,
		sizeof(struct ifaliasreq)) < 0) {
		return -1;
	}

	return request.ifra_index;
}


/** @brief Adds a new alias described by a full BNetworkInterfaceAddress record. */
status_t
BNetworkInterface::AddAddress(const BNetworkInterfaceAddress& address)
{
	return do_ifaliasreq(Name(), B_SOCKET_ADD_ALIAS, address);
}


/** @brief Convenience overload that adds an alias with only a primary address. */
status_t
BNetworkInterface::AddAddress(const BNetworkAddress& local)
{
	BNetworkInterfaceAddress address;
	address.SetAddress(local);

	return do_ifaliasreq(Name(), B_SOCKET_ADD_ALIAS, address);
}


/** @brief Replaces an existing alias with the fields from @a address. */
status_t
BNetworkInterface::SetAddress(const BNetworkInterfaceAddress& address)
{
	return do_ifaliasreq(Name(), B_SOCKET_SET_ALIAS, address);
}


/** @brief Removes the alias matching the record in @a address. */
status_t
BNetworkInterface::RemoveAddress(const BNetworkInterfaceAddress& address)
{
	ifreq request;
	memcpy(&request.ifr_addr, &address.Address().SockAddr(),
		address.Address().Length());

	return do_request(family_from_interface_address(address), request, Name(),
		B_SOCKET_REMOVE_ALIAS);
}


/** @brief Removes the alias whose primary address equals @a address. */
status_t
BNetworkInterface::RemoveAddress(const BNetworkAddress& address)
{
	ifreq request;
	memcpy(&request.ifr_addr, &address.SockAddr(), address.Length());

	return do_request(address.Family(), request, Name(), B_SOCKET_REMOVE_ALIAS);
}


/** @brief Removes the @a index'th alias, looking it up first. */
status_t
BNetworkInterface::RemoveAddressAt(int32 index)
{
	BNetworkInterfaceAddress address;
	status_t status = GetAddressAt(index, address);
	if (status != B_OK)
		return status;

	return RemoveAddress(address);
}


/** @brief Retrieves the link-level (MAC) address of the interface. */
status_t
BNetworkInterface::GetHardwareAddress(BNetworkAddress& address)
{
	FileDescriptorCloser socket(::socket(AF_LINK, SOCK_DGRAM, 0));
	if (!socket.IsSet())
		return errno;

	ifreq request;
	strlcpy(request.ifr_name, Name(), IF_NAMESIZE);

	if (ioctl(socket.Get(), SIOCGIFADDR, &request, sizeof(struct ifreq)) < 0)
		return errno;

	address.SetTo(request.ifr_addr);
	return B_OK;
}


/** @brief Installs a new route attached to this interface.
 *  @return B_OK on success, B_BAD_VALUE if the route has no address family. */
status_t
BNetworkInterface::AddRoute(const BNetworkRoute& route)
{
	int family = route.AddressFamily();
	if (family == AF_UNSPEC)
		return B_BAD_VALUE;

	ifreq request;
	request.ifr_route = route.RouteEntry();
	return do_request(family, request, Name(), SIOCADDRT);
}


/** @brief Adds a default route through @a gateway on this interface. */
status_t
BNetworkInterface::AddDefaultRoute(const BNetworkAddress& gateway)
{
	BNetworkRoute route;
	status_t result = route.SetGateway(gateway);
	if (result != B_OK)
		return result;

	route.SetFlags(RTF_STATIC | RTF_DEFAULT | RTF_GATEWAY);
	return AddRoute(route);
}


/** @brief Removes the route previously installed via AddRoute(). */
status_t
BNetworkInterface::RemoveRoute(const BNetworkRoute& route)
{
	int family = route.AddressFamily();
	if (family == AF_UNSPEC)
		return B_BAD_VALUE;

	return RemoveRoute(family, route);
}


/** @brief Removes a route using an explicitly specified address @a family. */
status_t
BNetworkInterface::RemoveRoute(int family, const BNetworkRoute& route)
{
	ifreq request;
	request.ifr_route = route.RouteEntry();
	return do_request(family, request, Name(), SIOCDELRT);
}


/** @brief Removes the default route associated with this interface. */
status_t
BNetworkInterface::RemoveDefaultRoute(int family)
{
	BNetworkRoute route;
	route.SetFlags(RTF_STATIC | RTF_DEFAULT);
	return RemoveRoute(family, route);
}


/** @brief Retrieves all routes attached to this interface for @a family. */
status_t
BNetworkInterface::GetRoutes(int family,
	BObjectList<BNetworkRoute, true>& routes) const
{
	return BNetworkRoute::GetRoutes(family, Name(), routes);
}


/** @brief Retrieves the default route for @a family on this interface. */
status_t
BNetworkInterface::GetDefaultRoute(int family, BNetworkRoute& route) const
{
	return BNetworkRoute::GetDefaultRoute(family, Name(), route);
}


/** @brief Retrieves the gateway address of the default route for @a family. */
status_t
BNetworkInterface::GetDefaultGateway(int family, BNetworkAddress& gateway) const
{
	return BNetworkRoute::GetDefaultGateway(family, Name(), gateway);
}


/** @brief Asks net_server to auto-configure this interface (e.g. DHCP for
 *         AF_INET or SLAAC for AF_INET6). */
status_t
BNetworkInterface::AutoConfigure(int family)
{
	BMessage message(kMsgConfigureInterface);
	message.AddString("device", Name());

	BMessage address;
	address.AddInt32("family", family);
	address.AddBool("auto_config", true);
	message.AddMessage("address", &address);

	BMessenger networkServer(kNetServerSignature);
	BMessage reply;
	status_t status = networkServer.SendMessage(&message, &reply);
	if (status == B_OK)
		reply.FindInt32("status", &status);

	return status;
}
