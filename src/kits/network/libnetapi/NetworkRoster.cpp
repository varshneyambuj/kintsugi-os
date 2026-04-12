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


/**
 * @file NetworkRoster.cpp
 * @brief Implementation of BNetworkRoster, the global network interface registry.
 *
 * BNetworkRoster provides a centralised view of all active network interfaces
 * and persistent wireless network configurations on the system. It delegates
 * persistent-network management to the net_server and uses ioctl-based calls
 * for live interface enumeration.
 *
 * @see BNetworkInterface, BNetworkDevice
 */


#include <NetworkRoster.h>

#include <errno.h>
#include <sys/sockio.h>

#include <NetworkDevice.h>
#include <NetworkInterface.h>

#include <net_notifications.h>
#include <AutoDeleter.h>
#include <NetServer.h>


// TODO: using AF_INET for the socket isn't really a smart idea, as one
// could completely remove IPv4 support from the stack easily.
// Since in the stack, device_interfaces are pretty much interfaces now, we
// could get rid of them more or less, and make AF_LINK provide the same
// information as AF_INET for the interface functions mostly.


/** @brief The process-global default BNetworkRoster instance. */
BNetworkRoster BNetworkRoster::sDefault;


/**
 * @brief Return the process-global BNetworkRoster singleton.
 *
 * @return Reference to the single BNetworkRoster instance used by the process.
 */
/*static*/ BNetworkRoster&
BNetworkRoster::Default()
{
	return sDefault;
}


/**
 * @brief Return the number of currently active network interfaces.
 *
 * Opens a temporary AF_INET datagram socket and issues SIOCGIFCOUNT to
 * obtain the live interface count from the networking stack.
 *
 * @return The number of active interfaces, or 0 on error.
 */
size_t
BNetworkRoster::CountInterfaces() const
{
	FileDescriptorCloser socket(::socket(AF_INET, SOCK_DGRAM, 0));
	if (!socket.IsSet())
		return 0;

	ifconf config;
	config.ifc_len = sizeof(config.ifc_value);
	if (ioctl(socket.Get(), SIOCGIFCOUNT, &config, sizeof(struct ifconf)) != 0)
		return 0;

	return (size_t)config.ifc_value;
}


/**
 * @brief Iterate over active network interfaces one at a time.
 *
 * On each call the method fills \a interface with the name of the interface
 * at position \a *cookie and advances the cookie for the next call.
 * The caller must initialise \a cookie to zero before the first call.
 *
 * @param cookie    In/out iteration cursor; must be initialised to 0.
 * @param interface Output parameter set to the interface at the current position.
 * @return B_OK on success, B_BAD_VALUE when the cursor is past the last interface,
 *         or an errno-mapped error code on ioctl failure.
 */
status_t
BNetworkRoster::GetNextInterface(uint32* cookie,
	BNetworkInterface& interface) const
{
	// TODO: think about caching the interfaces!

	if (cookie == NULL)
		return B_BAD_VALUE;

	// get a list of all interfaces

	FileDescriptorCloser socket (::socket(AF_INET, SOCK_DGRAM, 0));
	if (!socket.IsSet())
		return errno;

	ifconf config;
	config.ifc_len = sizeof(config.ifc_value);
	if (ioctl(socket.Get(), SIOCGIFCOUNT, &config, sizeof(struct ifconf)) < 0)
		return errno;

	size_t count = (size_t)config.ifc_value;
	if (count == 0)
		return B_BAD_VALUE;

	char* buffer = (char*)malloc(count * sizeof(struct ifreq));
	if (buffer == NULL)
		return B_NO_MEMORY;

	MemoryDeleter deleter(buffer);

	config.ifc_len = count * sizeof(struct ifreq);
	config.ifc_buf = buffer;
	if (ioctl(socket.Get(), SIOCGIFCONF, &config, sizeof(struct ifconf)) < 0)
		return errno;

	ifreq* interfaces = (ifreq*)buffer;
	ifreq* end = (ifreq*)(buffer + config.ifc_len);

	for (uint32 i = 0; interfaces < end; i++) {
		interface.SetTo(interfaces[0].ifr_name);
		if (i == *cookie) {
			(*cookie)++;
			return B_OK;
		}

		interfaces = (ifreq*)((uint8*)interfaces
			+ _SIZEOF_ADDR_IFREQ(interfaces[0]));
	}

	return B_BAD_VALUE;
}


/**
 * @brief Create a new network interface with the given name.
 *
 * Issues SIOCAIFADDR on a temporary socket to request that the networking
 * stack register a new interface named \a name.
 *
 * @param name  Null-terminated interface name (e.g. "en0").
 * @return B_OK on success, or an errno-mapped error code on failure.
 */
status_t
BNetworkRoster::AddInterface(const char* name)
{
	FileDescriptorCloser socket (::socket(AF_INET, SOCK_DGRAM, 0));
	if (!socket.IsSet())
		return errno;

	ifaliasreq request;
	memset(&request, 0, sizeof(ifaliasreq));
	strlcpy(request.ifra_name, name, IF_NAMESIZE);

	if (ioctl(socket.Get(), SIOCAIFADDR, &request, sizeof(request)) != 0)
		return errno;

	return B_OK;
}


/**
 * @brief Create a new network interface described by a BNetworkInterface object.
 *
 * Convenience overload that extracts the interface name and delegates to
 * AddInterface(const char*).
 *
 * @param interface The interface whose name is used for creation.
 * @return B_OK on success, or an errno-mapped error code on failure.
 */
status_t
BNetworkRoster::AddInterface(const BNetworkInterface& interface)
{
	return AddInterface(interface.Name());
}


/**
 * @brief Remove the network interface with the given name.
 *
 * Issues SIOCDIFADDR on a temporary socket to request that the networking
 * stack unregister the interface named \a name.
 *
 * @param name  Null-terminated interface name to remove.
 * @return B_OK on success, or an errno-mapped error code on failure.
 */
status_t
BNetworkRoster::RemoveInterface(const char* name)
{
	FileDescriptorCloser socket(::socket(AF_INET, SOCK_DGRAM, 0));
	if (!socket.IsSet())
		return errno;

	ifreq request;
	strlcpy(request.ifr_name, name, IF_NAMESIZE);

	request.ifr_addr.sa_family = AF_UNSPEC;

	if (ioctl(socket.Get(), SIOCDIFADDR, &request, sizeof(request)) != 0)
		return errno;

	return B_OK;
}


/**
 * @brief Remove the network interface described by a BNetworkInterface object.
 *
 * Convenience overload that extracts the interface name and delegates to
 * RemoveInterface(const char*).
 *
 * @param interface The interface to remove.
 * @return B_OK on success, or an errno-mapped error code on failure.
 */
status_t
BNetworkRoster::RemoveInterface(const BNetworkInterface& interface)
{
	return RemoveInterface(interface.Name());
}


/**
 * @brief Return the number of persistently stored wireless networks.
 *
 * Sends kMsgCountPersistentNetworks to the net_server and extracts the
 * "count" field from the reply.
 *
 * @return The number of stored networks, or 0 if the server cannot be reached.
 */
int32
BNetworkRoster::CountPersistentNetworks() const
{
	BMessenger networkServer(kNetServerSignature);
	BMessage message(kMsgCountPersistentNetworks);
	BMessage reply;
	if (networkServer.SendMessage(&message, &reply) != B_OK)
		return 0;

	int32 count = 0;
	if (reply.FindInt32("count", &count) != B_OK)
		return 0;

	return count;
}


/**
 * @brief Iterate over persistently stored wireless networks.
 *
 * Fetches the wireless_network entry at position \a *cookie from the
 * net_server and advances the cookie.  Callers must initialise \a cookie
 * to zero before the first call.
 *
 * @param cookie   In/out iteration cursor; must be initialised to 0.
 * @param network  Output structure populated with the network details.
 * @return B_OK on success, or an error code if the entry cannot be retrieved.
 */
status_t
BNetworkRoster::GetNextPersistentNetwork(uint32* cookie,
	wireless_network& network) const
{
	BMessenger networkServer(kNetServerSignature);
	BMessage message(kMsgGetPersistentNetwork);
	message.AddInt32("index", (int32)*cookie);

	BMessage reply;
	status_t result = networkServer.SendMessage(&message, &reply);
	if (result != B_OK)
		return result;

	status_t status;
	if (reply.FindInt32("status", &status) != B_OK)
		return B_ERROR;
	if (status != B_OK)
		return status;

	BMessage networkMessage;
	if (reply.FindMessage("network", &networkMessage) != B_OK)
		return B_ERROR;

	BString networkName;
	if (networkMessage.FindString("name", &networkName) != B_OK)
		return B_ERROR;

	memset(network.name, 0, sizeof(network.name));
	strlcpy(network.name, networkName.String(), sizeof(network.name));

	BNetworkAddress address;
	if (networkMessage.FindFlat("address", &network.address) != B_OK)
		network.address.Unset();

	if (networkMessage.FindUInt32("flags", &network.flags) != B_OK)
		network.flags = 0;

	if (networkMessage.FindUInt32("authentication_mode",
			&network.authentication_mode) != B_OK) {
		network.authentication_mode = B_NETWORK_AUTHENTICATION_NONE;
	}

	if (networkMessage.FindUInt32("cipher", &network.cipher) != B_OK)
		network.cipher = B_NETWORK_CIPHER_NONE;

	if (networkMessage.FindUInt32("group_cipher", &network.group_cipher)
			!= B_OK) {
		network.group_cipher = B_NETWORK_CIPHER_NONE;
	}

	if (networkMessage.FindUInt32("key_mode", &network.key_mode) != B_OK)
		network.key_mode = B_KEY_MODE_NONE;

	return B_OK;
}


/**
 * @brief Persistently store a wireless network configuration.
 *
 * Serialises \a network into a BMessage and forwards it to the net_server
 * via kMsgAddPersistentNetwork.
 *
 * @param network  The wireless network descriptor to store.
 * @return B_OK on success, or an error code if serialisation or IPC fails.
 */
status_t
BNetworkRoster::AddPersistentNetwork(const wireless_network& network)
{
	BMessage message(kMsgAddPersistentNetwork);
	BString networkName;
	networkName.SetTo(network.name, sizeof(network.name));
	status_t status = message.AddString("name", networkName);
	if (status == B_OK) {
		BNetworkAddress address = network.address;
		status = message.AddFlat("address", &address);
	}

	if (status == B_OK)
		status = message.AddUInt32("flags", network.flags);
	if (status == B_OK) {
		status = message.AddUInt32("authentication_mode",
			network.authentication_mode);
	}
	if (status == B_OK)
		status = message.AddUInt32("cipher", network.cipher);
	if (status == B_OK)
		status = message.AddUInt32("group_cipher", network.group_cipher);
	if (status == B_OK)
		status = message.AddUInt32("key_mode", network.key_mode);

	if (status != B_OK)
		return status;

	BMessenger networkServer(kNetServerSignature);
	BMessage reply;
	status = networkServer.SendMessage(&message, &reply);
	if (status == B_OK)
		reply.FindInt32("status", &status);

	return status;
}


/**
 * @brief Remove a persistently stored wireless network by name.
 *
 * Sends kMsgRemovePersistentNetwork to the net_server with the network \a name
 * and returns the status from the reply.
 *
 * @param name  Null-terminated SSID of the network to remove.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNetworkRoster::RemovePersistentNetwork(const char* name)
{
	BMessage message(kMsgRemovePersistentNetwork);
	status_t status = message.AddString("name", name);
	if (status != B_OK)
		return status;

	BMessenger networkServer(kNetServerSignature);
	BMessage reply;
	status = networkServer.SendMessage(&message, &reply);
	if (status == B_OK)
		reply.FindInt32("status", &status);

	return status;
}


/**
 * @brief Begin receiving network-event notifications for a messenger.
 *
 * Registers \a target to receive messages for events matching \a eventMask
 * via the net_notifications mechanism.
 *
 * @param target     The BMessenger to which notifications will be sent.
 * @param eventMask  Bitmask of events to subscribe to.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNetworkRoster::StartWatching(const BMessenger& target, uint32 eventMask)
{
	return start_watching_network(eventMask, target);
}


/**
 * @brief Stop receiving network-event notifications for a messenger.
 *
 * Unregisters \a target from the net_notifications mechanism so it will
 * no longer receive network-change events.
 *
 * @param target  The BMessenger to unregister.
 */
void
BNetworkRoster::StopWatching(const BMessenger& target)
{
	stop_watching_network(target);
}


// #pragma mark - private


/**
 * @brief Default constructor — initialises the singleton instance.
 */
BNetworkRoster::BNetworkRoster()
{
}


/**
 * @brief Destructor.
 */
BNetworkRoster::~BNetworkRoster()
{
}
