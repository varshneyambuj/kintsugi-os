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
 *   Copyright 2006-2015, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file NetworkSettings.cpp
 * @brief Implementation of BNetworkSettings and related settings classes.
 *
 * Provides persistent read/write access to network configuration stored in
 * the system settings directory.  The main BNetworkSettings class manages
 * three subsystems (interfaces, wireless networks, and services) via the
 * DriverSettings file format.  Supporting classes BNetworkInterfaceSettings,
 * BNetworkInterfaceAddressSettings, BNetworkServiceSettings, and
 * BNetworkServiceAddressSettings represent individual configuration records.
 *
 * @see BNetworkInterface, BNetworkDevice
 */


#include <NetworkSettings.h>

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <fs_interface.h>
#include <NetworkDevice.h>
#include <NetworkInterface.h>
#include <Path.h>
#include <PathMonitor.h>
#include <String.h>

#include <DriverSettingsMessageAdapter.h>
#include <NetServer.h>


using namespace BNetworkKit;


/** @brief Settings file name for interface configuration. */
static const char* kInterfaceSettingsName = "interfaces";

/** @brief Settings file name for network service configuration. */
static const char* kServicesSettingsName = "services";

/** @brief Settings file name for wireless network configuration. */
static const char* kNetworksSettingsName = "wireless_networks";


// Interface templates

namespace BPrivate {


class InterfaceAddressFamilyConverter : public DriverSettingsConverter {
public:
	virtual	status_t			ConvertFromDriverSettings(
									const driver_parameter& parameter,
									const char* name, int32 index, uint32 type,
									BMessage& target);
	virtual	status_t			ConvertToDriverSettings(const BMessage& source,
									const char* name, int32 index,
									uint32 type, BString& value);
};


}	// namespace BPrivate

using BPrivate::InterfaceAddressFamilyConverter;


const static settings_template kInterfaceAddressTemplate[] = {
	{B_STRING_TYPE, "family", NULL, true, new InterfaceAddressFamilyConverter},
	{B_STRING_TYPE, "address", NULL},
	{B_STRING_TYPE, "mask", NULL},
	{B_STRING_TYPE, "peer", NULL},
	{B_STRING_TYPE, "broadcast", NULL},
	{B_STRING_TYPE, "gateway", NULL},
	{B_BOOL_TYPE, "auto_config", NULL},
	{0, NULL, NULL}
};

const static settings_template kInterfaceNetworkTemplate[] = {
	{B_STRING_TYPE, "name", NULL, true},
	{B_STRING_TYPE, "mac", NULL},
};

const static settings_template kInterfaceTemplate[] = {
	{B_STRING_TYPE, "device", NULL, true},
	{B_BOOL_TYPE, "disabled", NULL},
	{B_MESSAGE_TYPE, "address", kInterfaceAddressTemplate},
	{B_MESSAGE_TYPE, "network", kInterfaceNetworkTemplate},
	{B_INT32_TYPE, "flags", NULL},
	{B_INT32_TYPE, "metric", NULL},
	{B_INT32_TYPE, "mtu", NULL},
	{0, NULL, NULL}
};

const static settings_template kInterfacesTemplate[] = {
	{B_MESSAGE_TYPE, "interface", kInterfaceTemplate},
	{0, NULL, NULL}
};

// Network templates

const static settings_template kNetworkTemplate[] = {
	{B_STRING_TYPE, "name", NULL, true},
	{B_STRING_TYPE, "mac", NULL},
	{B_STRING_TYPE, "password", NULL},
	{B_STRING_TYPE, "authentication", NULL},
	{B_STRING_TYPE, "cipher", NULL},
	{B_STRING_TYPE, "group_cipher", NULL},
	{B_STRING_TYPE, "key", NULL},
	{0, NULL, NULL}
};

const static settings_template kNetworksTemplate[] = {
	{B_MESSAGE_TYPE, "network", kNetworkTemplate},
	{0, NULL, NULL}
};

// Service templates

const static settings_template kServiceAddressTemplate[] = {
	{B_STRING_TYPE, "family", NULL, true},
	{B_STRING_TYPE, "type", NULL},
	{B_STRING_TYPE, "protocol", NULL},
	{B_STRING_TYPE, "address", NULL},
	{B_INT32_TYPE, "port", NULL},
	{0, NULL, NULL}
};

const static settings_template kServiceTemplate[] = {
	{B_STRING_TYPE, "name", NULL, true},
	{B_BOOL_TYPE, "disabled", NULL},
	{B_MESSAGE_TYPE, "address", kServiceAddressTemplate},
	{B_STRING_TYPE, "user", NULL},
	{B_STRING_TYPE, "group", NULL},
	{B_STRING_TYPE, "launch", NULL},
	{B_STRING_TYPE, "family", NULL},
	{B_STRING_TYPE, "type", NULL},
	{B_STRING_TYPE, "protocol", NULL},
	{B_INT32_TYPE, "port", NULL},
	{B_BOOL_TYPE, "stand_alone", NULL},
	{0, NULL, NULL}
};

const static settings_template kServicesTemplate[] = {
	{B_MESSAGE_TYPE, "service", kServiceTemplate},
	{0, NULL, NULL}
};


struct address_family {
	int			family;
	const char*	name;
	const char*	identifiers[4];
};


static const address_family kFamilies[] = {
	{
		AF_INET,
		"inet",
		{"AF_INET", "inet", "ipv4", NULL},
	},
	{
		AF_INET6,
		"inet6",
		{"AF_INET6", "inet6", "ipv6", NULL},
	},
	{ -1, NULL, {NULL} }
};


/**
 * @brief Return the canonical name string for an address family integer.
 *
 * @param family  Address family constant (AF_INET, AF_INET6, …).
 * @return Pointer to the canonical name string, or NULL if not found.
 */
static const char*
get_family_name(int family)
{
	for (int32 i = 0; kFamilies[i].family >= 0; i++) {
		if (kFamilies[i].family == family)
			return kFamilies[i].name;
	}
	return NULL;
}


/**
 * @brief Resolve an address-family identifier string to its integer constant.
 *
 * Checks both canonical names and common aliases defined in kFamilies.
 *
 * @param argument  Case-sensitive identifier string (e.g. "inet", "AF_INET").
 * @return Address family constant, or AF_UNSPEC if no match is found.
 */
static int
get_address_family(const char* argument)
{
	for (int32 i = 0; kFamilies[i].family >= 0; i++) {
		for (int32 j = 0; kFamilies[i].identifiers[j]; j++) {
			if (!strcmp(argument, kFamilies[i].identifiers[j])) {
				// found a match
				return kFamilies[i].family;
			}
		}
	}

	return AF_UNSPEC;
}


/*!	Parses the \a argument as network \a address for the specified \a family.
	If \a family is \c AF_UNSPEC, \a family will be overwritten with the family
	of the successfully parsed address.
*/
/**
 * @brief Parse \a argument as a network address for the specified \a family.
 *
 * If \a family is AF_UNSPEC, it is updated to match the parsed address family.
 * A NULL \a argument with a known family sets a wildcard address.
 *
 * @param family    In/out family constant; updated when AF_UNSPEC.
 * @param argument  Address string to parse, or NULL for wildcard.
 * @param address   Output parameter populated with the parsed address.
 * @return true if an address was successfully parsed, false otherwise.
 */
static bool
parse_address(int32& family, const char* argument, BNetworkAddress& address)
{
	if (argument == NULL) {
		if (family != AF_UNSPEC)
			address.SetToWildcard(family);
		return false;
	}

	status_t status = address.SetTo(family, argument, (uint16)0,
		B_NO_ADDRESS_RESOLUTION);
	if (status != B_OK)
		return false;

	if (family == AF_UNSPEC) {
		// Test if we support the resulting address family
		bool supported = false;

		for (int32 i = 0; kFamilies[i].family >= 0; i++) {
			if (kFamilies[i].family == address.Family()) {
				supported = true;
				break;
			}
		}
		if (!supported)
			return false;

		// Take over family from address
		family = address.Family();
	}

	return true;
}


/**
 * @brief Parse a socket-type string into its SOCK_* constant.
 *
 * @param string  Type string ("stream" or anything else for SOCK_DGRAM).
 * @return SOCK_STREAM or SOCK_DGRAM.
 */
static int
parse_type(const char* string)
{
	if (!strcasecmp(string, "stream"))
		return SOCK_STREAM;

	return SOCK_DGRAM;
}


/**
 * @brief Resolve a protocol name string to its IPPROTO_* constant.
 *
 * @param string  Protocol name (e.g. "tcp", "udp").
 * @return IPPROTO_* constant, or IPPROTO_TCP if the name is not found.
 */
static int
parse_protocol(const char* string)
{
	struct protoent* proto = getprotobyname(string);
	if (proto == NULL)
		return IPPROTO_TCP;

	return proto->p_proto;
}


/**
 * @brief Return the default socket type for a given protocol.
 *
 * @param protocol  IPPROTO_* constant.
 * @return SOCK_STREAM for TCP, SOCK_DGRAM for all others.
 */
static int
type_for_protocol(int protocol)
{
	// default determined by protocol
	switch (protocol) {
		case IPPROTO_TCP:
			return SOCK_STREAM;

		case IPPROTO_UDP:
		default:
			return SOCK_DGRAM;
	}
}


// #pragma mark -


/**
 * @brief Converter stub — reading address family from driver settings is unsupported.
 *
 * @return B_NOT_SUPPORTED always.
 */
status_t
InterfaceAddressFamilyConverter::ConvertFromDriverSettings(
	const driver_parameter& parameter, const char* name, int32 index,
	uint32 type, BMessage& target)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Serialise an integer address family to its canonical name string.
 *
 * Reads the "family" int32 from \a source and writes the matching name
 * string into \a value for storage in driver settings files.
 *
 * @param source  BMessage containing a "family" int32 field.
 * @param name    Field name (unused).
 * @param index   Field index (unused).
 * @param type    Field type (unused).
 * @param value   Output string populated with the family name.
 * @return B_OK on success, B_NOT_SUPPORTED if no "family" field exists.
 */
status_t
InterfaceAddressFamilyConverter::ConvertToDriverSettings(const BMessage& source,
	const char* name, int32 index, uint32 type, BString& value)
{
	int32 family;
	if (source.FindInt32("family", &family) == B_OK) {
		const char* familyName = get_family_name(family);
		if (familyName != NULL)
			value << familyName;
		else
			value << family;

		return B_OK;
	}

	return B_NOT_SUPPORTED;
}


// #pragma mark -


/**
 * @brief Construct BNetworkSettings and load all configuration from disk.
 */
BNetworkSettings::BNetworkSettings()
{
	_Load();
}


/**
 * @brief Destructor.
 */
BNetworkSettings::~BNetworkSettings()
{
}


/**
 * @brief Iterate over configured network interfaces one at a time.
 *
 * @param cookie     In/out iteration cursor; must be initialised to 0.
 * @param interface  Output BMessage populated with the interface configuration.
 * @return B_OK on success, or an error code when no more entries exist.
 */
status_t
BNetworkSettings::GetNextInterface(uint32& cookie, BMessage& interface)
{
	status_t status = fInterfaces.FindMessage("interface", cookie, &interface);
	if (status != B_OK)
		return status;

	cookie++;
	return B_OK;
}


/**
 * @brief Retrieve the configuration message for a named interface.
 *
 * @param name       Null-terminated interface device name to look up.
 * @param interface  Output BMessage populated on success.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no such interface is configured.
 */
status_t
BNetworkSettings::GetInterface(const char* name, BMessage& interface) const
{
	int32 index;
	return _GetItem(fInterfaces, "interface", "device", name, index, interface);
}


/**
 * @brief Add or replace a network interface configuration entry.
 *
 * If an entry with the same device name already exists it is removed first.
 * The updated configuration is persisted to the interfaces settings file.
 *
 * @param interface  BMessage describing the interface (must have "device" field).
 * @return B_OK on success, B_BAD_VALUE if "device" is missing, or a save error.
 */
status_t
BNetworkSettings::AddInterface(const BMessage& interface)
{
	const char* name = NULL;
	if (interface.FindString("device", &name) != B_OK)
		return B_BAD_VALUE;

	_RemoveItem(fInterfaces, "interface", "device", name);

	status_t result = fInterfaces.AddMessage("interface", &interface);
	if (result != B_OK)
		return result;

	return _Save(kInterfaceSettingsName);
}


/**
 * @brief Remove a network interface configuration entry by name.
 *
 * @param name  Device name of the interface to remove.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNetworkSettings::RemoveInterface(const char* name)
{
	return _RemoveItem(fInterfaces, "interface", "device", name,
		kInterfaceSettingsName);
}


/**
 * @brief Return a BNetworkInterfaceSettings object for a named interface.
 *
 * @param name  Device name of the interface.
 * @return BNetworkInterfaceSettings populated from the stored configuration.
 */
BNetworkInterfaceSettings
BNetworkSettings::Interface(const char* name)
{
	BMessage interface;
	GetInterface(name, interface);
	return BNetworkInterfaceSettings(interface);
}


/**
 * @brief Return a const BNetworkInterfaceSettings for a named interface.
 *
 * @param name  Device name of the interface.
 * @return Const BNetworkInterfaceSettings populated from the stored configuration.
 */
const BNetworkInterfaceSettings
BNetworkSettings::Interface(const char* name) const
{
	BMessage interface;
	GetInterface(name, interface);
	return BNetworkInterfaceSettings(interface);
}


/**
 * @brief Return the number of configured wireless networks.
 *
 * @return Number of "network" entries in the wireless network settings.
 */
int32
BNetworkSettings::CountNetworks() const
{
	int32 count = 0;
	if (fNetworks.GetInfo("network", NULL, &count) != B_OK)
		return 0;

	return count;
}


/**
 * @brief Iterate over configured wireless networks one at a time.
 *
 * @param cookie   In/out iteration cursor; must be initialised to 0.
 * @param network  Output BMessage populated with the network configuration.
 * @return B_OK on success, or an error code when no more entries exist.
 */
status_t
BNetworkSettings::GetNextNetwork(uint32& cookie, BMessage& network) const
{
	status_t status = fNetworks.FindMessage("network", cookie, &network);
	if (status != B_OK)
		return status;

	cookie++;
	return B_OK;
}


/**
 * @brief Retrieve the configuration message for a named wireless network.
 *
 * @param name     SSID of the network to look up.
 * @param network  Output BMessage populated on success.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no such network is configured.
 */
status_t
BNetworkSettings::GetNetwork(const char* name, BMessage& network) const
{
	int32 index;
	return _GetItem(fNetworks, "network", "name", name, index, network);
}


/**
 * @brief Add or replace a wireless network configuration entry.
 *
 * @param network  BMessage describing the network (must have "name" field).
 * @return B_OK on success, B_BAD_VALUE if "name" is missing, or a save error.
 */
status_t
BNetworkSettings::AddNetwork(const BMessage& network)
{
	const char* name = NULL;
	if (network.FindString("name", &name) != B_OK)
		return B_BAD_VALUE;

	_RemoveItem(fNetworks, "network", "name", name);

	status_t result = fNetworks.AddMessage("network", &network);
	if (result != B_OK)
		return result;

	return _Save(kNetworksSettingsName);
}


/**
 * @brief Remove a wireless network configuration entry by SSID.
 *
 * @param name  SSID of the network to remove.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNetworkSettings::RemoveNetwork(const char* name)
{
	return _RemoveItem(fNetworks, "network", "name", name,
		kNetworksSettingsName);
}


/**
 * @brief Return a const reference to the complete services configuration message.
 *
 * @return Const reference to the internal fServices BMessage.
 */
const BMessage&
BNetworkSettings::Services() const
{
	return fServices;
}


/**
 * @brief Iterate over configured network services one at a time.
 *
 * @param cookie   In/out iteration cursor; must be initialised to 0.
 * @param service  Output BMessage populated with the service configuration.
 * @return B_OK on success, or an error code when no more entries exist.
 */
status_t
BNetworkSettings::GetNextService(uint32& cookie, BMessage& service)
{
	status_t status = fServices.FindMessage("service", cookie, &service);
	if (status != B_OK)
		return status;

	cookie++;
	return B_OK;
}


/**
 * @brief Retrieve the configuration message for a named network service.
 *
 * @param name     Service name to look up.
 * @param service  Output BMessage populated on success.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no such service is configured.
 */
status_t
BNetworkSettings::GetService(const char* name, BMessage& service) const
{
	int32 index;
	return _GetItem(fServices, "service", "name", name, index, service);
}


/**
 * @brief Add or replace a network service configuration entry.
 *
 * @param service  BMessage describing the service (must have "name" field).
 * @return B_OK on success, B_BAD_VALUE if "name" is missing, or a save error.
 */
status_t
BNetworkSettings::AddService(const BMessage& service)
{
	const char* name = service.GetString("name");
	if (name == NULL)
		return B_BAD_VALUE;

	_RemoveItem(fServices, "service", "name", name);

	status_t result = fServices.AddMessage("service", &service);
	if (result != B_OK)
		return result;

	return _Save(kServicesSettingsName);
}


/**
 * @brief Remove a network service configuration entry by name.
 *
 * @param name  Service name to remove.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNetworkSettings::RemoveService(const char* name)
{
	return _RemoveItem(fServices, "service", "name", name,
		kServicesSettingsName);
}


/**
 * @brief Return a BNetworkServiceSettings object for a named service.
 *
 * @param name  Service name to look up.
 * @return BNetworkServiceSettings populated from the stored configuration.
 */
BNetworkServiceSettings
BNetworkSettings::Service(const char* name)
{
	BMessage service;
	GetService(name, service);
	return BNetworkServiceSettings(service);
}


/**
 * @brief Return a const BNetworkServiceSettings for a named service.
 *
 * @param name  Service name to look up.
 * @return Const BNetworkServiceSettings populated from the stored configuration.
 */
const BNetworkServiceSettings
BNetworkSettings::Service(const char* name) const
{
	BMessage service;
	GetService(name, service);
	return BNetworkServiceSettings(service);
}


/**
 * @brief Begin monitoring settings files for changes and delivering notifications.
 *
 * Registers \a target to receive a BMessage whenever interfaces, networks,
 * or services settings files are modified on disk.  Only one listener is
 * supported at a time; calling this while already watching will stop the
 * previous watcher first.
 *
 * @param target  BMessenger to receive change notifications.
 * @return B_OK on success, or an error code if path monitoring fails.
 */
status_t
BNetworkSettings::StartMonitoring(const BMessenger& target)
{
	if (_IsWatching(target))
		return B_OK;
	if (_IsWatching())
		StopMonitoring(fListener);

	fListener = target;

	status_t status = _StartWatching(kInterfaceSettingsName, target);
	if (status == B_OK)
		status = _StartWatching(kNetworksSettingsName, target);
	if (status == B_OK)
		status = _StartWatching(kServicesSettingsName, target);

	return status;
}


/**
 * @brief Stop monitoring settings files for a given messenger.
 *
 * @param target  The BMessenger to unregister from path monitoring.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNetworkSettings::StopMonitoring(const BMessenger& target)
{
	// TODO: this needs to be changed in case the server will watch
	//	anything else but settings
	return BPrivate::BPathMonitor::StopWatching(target);
}


/**
 * @brief Process a BPathMonitor change notification and reload the affected file.
 *
 * Determines which settings file changed from the "path" field of \a message,
 * reloads it, and sends an appropriate update notification to the registered listener.
 *
 * @param message  The B_PATH_MONITOR notification message to process.
 * @return B_OK on success, or an error code if the message is malformed.
 */
status_t
BNetworkSettings::Update(BMessage* message)
{
	const char* pathName;
	int32 opcode;
	if (message->FindInt32("opcode", &opcode) != B_OK
		|| message->FindString("path", &pathName) != B_OK)
		return B_BAD_VALUE;

	BPath settingsFolderPath;
	_GetPath(NULL, settingsFolderPath);
	if (strncmp(pathName, settingsFolderPath.Path(),
			strlen(settingsFolderPath.Path()))) {
		return B_NAME_NOT_FOUND;
	}

	if (message->FindBool("removed")) {
		// for now, we only consider existing settings files
		// (ie. deleting "services" won't stop any)
		return B_OK;
	}

	int32 fields;
	if (opcode == B_STAT_CHANGED
		&& message->FindInt32("fields", &fields) == B_OK
		&& (fields & (B_STAT_MODIFICATION_TIME | B_STAT_SIZE)) == 0) {
		// only update when the modified time or size has changed
		return B_OK;
	}

	BPath path(pathName);
	uint32 type;
	if (_Load(path.Leaf(), &type) == B_OK) {
		BMessage update(type);
		fListener.SendMessage(&update);
	}

	return B_OK;
}


// #pragma mark - private


/**
 * @brief Load one or all settings files from the network settings directory.
 *
 * When \a name is NULL, all three subsystem files are loaded.  When \a name
 * is specified, only that file is loaded.  On success \a _type receives the
 * appropriate kMsg*SettingsUpdated constant.
 *
 * @param name   Leaf name of the specific file to load, or NULL for all.
 * @param _type  Optional output parameter set to the update message type.
 * @return B_OK on success, or an error code on parse or I/O failure.
 */
status_t
BNetworkSettings::_Load(const char* name, uint32* _type)
{
	BPath path;
	status_t status = _GetPath(NULL, path);
	if (status != B_OK)
		return status;

	DriverSettingsMessageAdapter adapter;
	status = B_ENTRY_NOT_FOUND;

	if (name == NULL || strcmp(name, kInterfaceSettingsName) == 0) {
		status = adapter.ConvertFromDriverSettings(
			_Path(path, kInterfaceSettingsName).Path(), kInterfacesTemplate,
			fInterfaces);
		if (status == B_OK && _type != NULL)
			*_type = kMsgInterfaceSettingsUpdated;
	}
	if (name == NULL || strcmp(name, kNetworksSettingsName) == 0) {
		status = adapter.ConvertFromDriverSettings(
			_Path(path, kNetworksSettingsName).Path(),
			kNetworksTemplate, fNetworks);
		if (status == B_OK) {
			// Convert settings for simpler consumption
			BMessage network;
			for (int32 index = 0; fNetworks.FindMessage("network", index,
					&network); index++) {
				if (_ConvertNetworkFromSettings(network) == B_OK)
					fNetworks.ReplaceMessage("network", index, &network);
			}

			if (_type != NULL)
				*_type = kMsgNetworkSettingsUpdated;
		}
	}
	if (name == NULL || strcmp(name, kServicesSettingsName) == 0) {
		status = adapter.ConvertFromDriverSettings(
			_Path(path, kServicesSettingsName).Path(), kServicesTemplate,
			fServices);
		if (status == B_OK && _type != NULL)
			*_type = kMsgServiceSettingsUpdated;
	}

	return status;
}


/**
 * @brief Save one or all settings subsystems to the network settings directory.
 *
 * When \a name is NULL, all three subsystem files are saved.
 *
 * @param name  Leaf name of the specific file to save, or NULL for all.
 * @return B_OK on success, or an error code on serialisation or I/O failure.
 */
status_t
BNetworkSettings::_Save(const char* name)
{
	BPath path;
	status_t status = _GetPath(NULL, path);
	if (status != B_OK)
		return status;

	DriverSettingsMessageAdapter adapter;
	status = B_ENTRY_NOT_FOUND;

	if (name == NULL || strcmp(name, kInterfaceSettingsName) == 0) {
		status = adapter.ConvertToDriverSettings(
			_Path(path, kInterfaceSettingsName).Path(),
			kInterfacesTemplate, fInterfaces);
	}
	if (name == NULL || strcmp(name, kNetworksSettingsName) == 0) {
		// Convert settings to storage format
		BMessage networks = fNetworks;
		BMessage network;
		for (int32 index = 0; networks.FindMessage("network", index,
				&network); index++) {
			if (_ConvertNetworkToSettings(network) == B_OK)
				networks.ReplaceMessage("network", index, &network);
		}

		status = adapter.ConvertToDriverSettings(
			_Path(path, kNetworksSettingsName).Path(),
			kNetworksTemplate, networks);
	}
	if (name == NULL || strcmp(name, kServicesSettingsName) == 0) {
		status = adapter.ConvertToDriverSettings(
			_Path(path, kServicesSettingsName).Path(),
			kServicesTemplate, fServices);
	}

	return status;
}


/**
 * @brief Build a child path by appending a leaf name to a parent BPath.
 *
 * @param parent  Parent directory path.
 * @param leaf    Leaf file name to append.
 * @return BPath combining parent and leaf.
 */
BPath
BNetworkSettings::_Path(BPath& parent, const char* leaf)
{
	return BPath(parent.Path(), leaf);
}


/**
 * @brief Resolve the network settings directory path, creating it if absent.
 *
 * @param name  Optional leaf name to append; NULL for the directory itself.
 * @param path  Output BPath set to the resolved path.
 * @return B_OK on success, B_ERROR if the system settings directory is unavailable.
 */
status_t
BNetworkSettings::_GetPath(const char* name, BPath& path)
{
	if (find_directory(B_SYSTEM_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return B_ERROR;

	path.Append("network");
	create_directory(path.Path(), 0755);

	if (name != NULL)
		path.Append(name);
	return B_OK;
}


/**
 * @brief Start watching a specific settings file for stat changes.
 *
 * @param name    Leaf name of the settings file to watch.
 * @param target  BMessenger to receive B_PATH_MONITOR notifications.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNetworkSettings::_StartWatching(const char* name, const BMessenger& target)
{
	BPath path;
	status_t status = _GetPath(name, path);
	if (status != B_OK)
		return status;

	return BPrivate::BPathMonitor::StartWatching(path.Path(), B_WATCH_STAT,
		target);
}


/**
 * @brief Convert an in-memory wireless network BMessage to storage format.
 *
 * Transforms binary fields (address, authentication_mode, cipher,
 * group_cipher) into human-readable strings suitable for driver settings files.
 *
 * @param message  In/out BMessage representing the network; modified in place.
 * @return B_OK on success.
 */
status_t
BNetworkSettings::_ConvertNetworkToSettings(BMessage& message)
{
	BNetworkAddress address;
	status_t result = message.FindFlat("address", &address);
	if (result == B_OK)
		message.RemoveName("address");

	if (result == B_OK && address.Family() == AF_LINK) {
		size_t addressLength = address.LinkLevelAddressLength();
		uint8* macAddress = address.LinkLevelAddress();
		bool usable = false;
		BString formatted;

		for (size_t index = 0; index < addressLength; index++) {
			if (index > 0)
				formatted.Append(":");
			char buffer[3];
			snprintf(buffer, sizeof(buffer), "%2x", macAddress[index]);
			formatted.Append(buffer, sizeof(buffer));

			if (macAddress[index] != 0)
				usable = true;
		}

		if (usable)
			message.AddString("mac", formatted);
	}

	uint32 authentication = 0;
	result = message.FindUInt32("authentication_mode", &authentication);
	if (result == B_OK) {
		message.RemoveName("authentication_mode");

		const char* authenticationString = NULL;
		switch (authentication) {
			case B_NETWORK_AUTHENTICATION_NONE:
				authenticationString = "none";
				break;
			case B_NETWORK_AUTHENTICATION_WEP:
				authenticationString = "wep";
				break;
			case B_NETWORK_AUTHENTICATION_WPA:
				authenticationString = "wpa";
				break;
			case B_NETWORK_AUTHENTICATION_WPA2:
				authenticationString = "wpa2";
				break;
		}

		if (result == B_OK && authenticationString != NULL)
			message.AddString("authentication", authenticationString);
	}

	uint32 cipher = 0;
	result = message.FindUInt32("cipher", &cipher);
	if (result == B_OK) {
		message.RemoveName("cipher");

		if ((cipher & B_NETWORK_CIPHER_NONE) != 0)
			message.AddString("cipher", "none");
		if ((cipher & B_NETWORK_CIPHER_TKIP) != 0)
			message.AddString("cipher", "tkip");
		if ((cipher & B_NETWORK_CIPHER_CCMP) != 0)
			message.AddString("cipher", "ccmp");
	}

	uint32 groupCipher = 0;
	result = message.FindUInt32("group_cipher", &groupCipher);
	if (result == B_OK) {
		message.RemoveName("group_cipher");

		if ((groupCipher & B_NETWORK_CIPHER_NONE) != 0)
			message.AddString("group_cipher", "none");
		if ((groupCipher & B_NETWORK_CIPHER_WEP_40) != 0)
			message.AddString("group_cipher", "wep40");
		if ((groupCipher & B_NETWORK_CIPHER_WEP_104) != 0)
			message.AddString("group_cipher", "wep104");
		if ((groupCipher & B_NETWORK_CIPHER_TKIP) != 0)
			message.AddString("group_cipher", "tkip");
		if ((groupCipher & B_NETWORK_CIPHER_CCMP) != 0)
			message.AddString("group_cipher", "ccmp");
	}

	// TODO: the other fields aren't currently used, add them when they are
	// and when it's clear how they will be stored
	message.RemoveName("noise_level");
	message.RemoveName("signal_strength");
	message.RemoveName("flags");
	message.RemoveName("key_mode");

	return B_OK;
}


/**
 * @brief Convert a stored wireless network BMessage from settings to in-memory format.
 *
 * Transforms human-readable string fields back into their binary integer/enum
 * equivalents for use by higher-level APIs.
 *
 * @param message  In/out BMessage loaded from disk; modified in place.
 * @return B_OK on success.
 */
status_t
BNetworkSettings::_ConvertNetworkFromSettings(BMessage& message)
{
	message.RemoveName("mac");
		// TODO: convert into a flat BNetworkAddress "address"

	const char* authentication = NULL;
	if (message.FindString("authentication", &authentication) == B_OK) {
		message.RemoveName("authentication");

		if (strcasecmp(authentication, "none") == 0) {
			message.AddUInt32("authentication_mode",
				B_NETWORK_AUTHENTICATION_NONE);
		} else if (strcasecmp(authentication, "wep") == 0) {
			message.AddUInt32("authentication_mode",
				B_NETWORK_AUTHENTICATION_WEP);
		} else if (strcasecmp(authentication, "wpa") == 0) {
			message.AddUInt32("authentication_mode",
				B_NETWORK_AUTHENTICATION_WPA);
		} else if (strcasecmp(authentication, "wpa2") == 0) {
			message.AddUInt32("authentication_mode",
				B_NETWORK_AUTHENTICATION_WPA2);
		}
	}

	int32 index = 0;
	uint32 cipher = 0;
	const char* cipherString = NULL;
	while (message.FindString("cipher", index++, &cipherString) == B_OK) {
		if (strcasecmp(cipherString, "none") == 0)
			cipher |= B_NETWORK_CIPHER_NONE;
		else if (strcasecmp(cipherString, "tkip") == 0)
			cipher |= B_NETWORK_CIPHER_TKIP;
		else if (strcasecmp(cipherString, "ccmp") == 0)
			cipher |= B_NETWORK_CIPHER_CCMP;
	}

	message.RemoveName("cipher");
	if (cipher != 0)
		message.AddUInt32("cipher", cipher);

	index = 0;
	cipher = 0;
	while (message.FindString("group_cipher", index++, &cipherString) == B_OK) {
		if (strcasecmp(cipherString, "none") == 0)
			cipher |= B_NETWORK_CIPHER_NONE;
		else if (strcasecmp(cipherString, "wep40") == 0)
			cipher |= B_NETWORK_CIPHER_WEP_40;
		else if (strcasecmp(cipherString, "wep104") == 0)
			cipher |= B_NETWORK_CIPHER_WEP_104;
		else if (strcasecmp(cipherString, "tkip") == 0)
			cipher |= B_NETWORK_CIPHER_TKIP;
		else if (strcasecmp(cipherString, "ccmp") == 0)
			cipher |= B_NETWORK_CIPHER_CCMP;
	}

	message.RemoveName("group_cipher");
	if (cipher != 0)
		message.AddUInt32("group_cipher", cipher);

	message.AddUInt32("flags", B_NETWORK_IS_PERSISTENT);

	// TODO: add the other fields
	message.RemoveName("key");
	return B_OK;
}


/**
 * @brief Search a container message for an item matching a name field.
 *
 * Iterates over \a itemField messages in \a container looking for one
 * whose \a nameField string equals \a name.
 *
 * @param container  The BMessage holding the item array.
 * @param itemField  Field name of the array (e.g. "interface", "service").
 * @param nameField  Field within each item used as the unique identifier.
 * @param name       Name value to search for.
 * @param _index     Output parameter set to the matched item's array index.
 * @param item       Output parameter populated with the matched item message.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no match exists.
 */
status_t
BNetworkSettings::_GetItem(const BMessage& container, const char* itemField,
	const char* nameField, const char* name, int32& _index,
	BMessage& item) const
{
	int32 index = 0;
	while (container.FindMessage(itemField, index, &item) == B_OK) {
		const char* itemName = NULL;
		if (item.FindString(nameField, &itemName) == B_OK
			&& strcmp(itemName, name) == 0) {
			_index = index;
			return B_OK;
		}

		index++;
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Remove an item from a container message and optionally save the store.
 *
 * Finds the item by name and removes it; if \a store is non-NULL, saves
 * that settings file afterward.
 *
 * @param container  The BMessage holding the item array.
 * @param itemField  Field name of the array.
 * @param nameField  Field within each item used as the unique identifier.
 * @param name       Name of the item to remove.
 * @param store      Leaf name of the file to save after removal, or NULL.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no matching item exists.
 */
status_t
BNetworkSettings::_RemoveItem(BMessage& container, const char* itemField,
	const char* nameField, const char* name, const char* store)
{
	BMessage item;
	int32 index;
	if (_GetItem(container, itemField, nameField, name, index, item) == B_OK) {
		container.RemoveData(itemField, index);
		if (store != NULL)
			return _Save(store);
		return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


// #pragma mark - BNetworkInterfaceAddressSettings


/**
 * @brief Construct a default BNetworkInterfaceAddressSettings with AF_UNSPEC family.
 */
BNetworkInterfaceAddressSettings::BNetworkInterfaceAddressSettings()
	:
	fFamily(AF_UNSPEC),
	fAutoConfigure(true)
{
}


/**
 * @brief Construct BNetworkInterfaceAddressSettings from a BMessage.
 *
 * Parses the "family", "auto_config", "address", "mask", "peer",
 * "broadcast", and "gateway" fields from \a data.
 *
 * @param data  BMessage holding the serialised address settings.
 */
BNetworkInterfaceAddressSettings::BNetworkInterfaceAddressSettings(
	const BMessage& data)
{
	if (data.FindInt32("family", &fFamily) != B_OK) {
		const char* familyString;
		if (data.FindString("family", &familyString) == B_OK) {
			fFamily = get_address_family(familyString);
			if (fFamily == AF_UNSPEC) {
				// we don't support this family
				fprintf(stderr, "Ignore unknown family: %s\n",
					familyString);
				return;
			}
		} else
			fFamily = AF_UNSPEC;
	}

	fAutoConfigure = data.GetBool("auto_config", false);

	if (!fAutoConfigure) {
		if (parse_address(fFamily, data.GetString("address", NULL), fAddress))
			parse_address(fFamily, data.GetString("mask", NULL), fMask);

		parse_address(fFamily, data.GetString("peer", NULL), fPeer);
		parse_address(fFamily, data.GetString("broadcast", NULL), fBroadcast);
		parse_address(fFamily, data.GetString("gateway", NULL), fGateway);
	}
}


/**
 * @brief Copy constructor.
 *
 * @param other  The source object to copy from.
 */
BNetworkInterfaceAddressSettings::BNetworkInterfaceAddressSettings(
	const BNetworkInterfaceAddressSettings& other)
	:
	fFamily(other.fFamily),
	fAutoConfigure(other.fAutoConfigure),
	fAddress(other.fAddress),
	fMask(other.fMask),
	fPeer(other.fPeer),
	fBroadcast(other.fBroadcast),
	fGateway(other.fGateway)
{
}


/**
 * @brief Destructor.
 */
BNetworkInterfaceAddressSettings::~BNetworkInterfaceAddressSettings()
{
}


/**
 * @brief Return the address family for this address entry.
 *
 * @return AF_INET, AF_INET6, or AF_UNSPEC.
 */
int
BNetworkInterfaceAddressSettings::Family() const
{
	return fFamily;
}


/**
 * @brief Set the address family for this address entry.
 *
 * @param family  New address family constant.
 */
void
BNetworkInterfaceAddressSettings::SetFamily(int family)
{
	fFamily = family;
}


/**
 * @brief Return whether this address uses automatic configuration (DHCP/SLAAC).
 *
 * @return true if automatically configured.
 */
bool
BNetworkInterfaceAddressSettings::IsAutoConfigure() const
{
	return fAutoConfigure;
}


/**
 * @brief Set whether this address uses automatic configuration.
 *
 * @param configure  true to enable auto-configuration.
 */
void
BNetworkInterfaceAddressSettings::SetAutoConfigure(bool configure)
{
	fAutoConfigure = configure;
}


/**
 * @brief Return a const reference to the configured IP address.
 *
 * @return Const reference to the BNetworkAddress.
 */
const BNetworkAddress&
BNetworkInterfaceAddressSettings::Address() const
{
	return fAddress;
}


/**
 * @brief Return a mutable reference to the configured IP address.
 *
 * @return Mutable reference to the BNetworkAddress.
 */
BNetworkAddress&
BNetworkInterfaceAddressSettings::Address()
{
	return fAddress;
}


/**
 * @brief Return a const reference to the subnet mask.
 *
 * @return Const reference to the mask BNetworkAddress.
 */
const BNetworkAddress&
BNetworkInterfaceAddressSettings::Mask() const
{
	return fMask;
}


/**
 * @brief Return a mutable reference to the subnet mask.
 *
 * @return Mutable reference to the mask BNetworkAddress.
 */
BNetworkAddress&
BNetworkInterfaceAddressSettings::Mask()
{
	return fMask;
}


/**
 * @brief Return a const reference to the peer address (PPP/point-to-point).
 *
 * @return Const reference to the peer BNetworkAddress.
 */
const BNetworkAddress&
BNetworkInterfaceAddressSettings::Peer() const
{
	return fPeer;
}


/**
 * @brief Return a mutable reference to the peer address.
 *
 * @return Mutable reference to the peer BNetworkAddress.
 */
BNetworkAddress&
BNetworkInterfaceAddressSettings::Peer()
{
	return fPeer;
}


/**
 * @brief Return a const reference to the broadcast address.
 *
 * @return Const reference to the broadcast BNetworkAddress.
 */
const BNetworkAddress&
BNetworkInterfaceAddressSettings::Broadcast() const
{
	return fBroadcast;
}


/**
 * @brief Return a mutable reference to the broadcast address.
 *
 * @return Mutable reference to the broadcast BNetworkAddress.
 */
BNetworkAddress&
BNetworkInterfaceAddressSettings::Broadcast()
{
	return fBroadcast;
}


/**
 * @brief Return a const reference to the default gateway address.
 *
 * @return Const reference to the gateway BNetworkAddress.
 */
const BNetworkAddress&
BNetworkInterfaceAddressSettings::Gateway() const
{
	return fGateway;
}


/**
 * @brief Return a mutable reference to the default gateway address.
 *
 * @return Mutable reference to the gateway BNetworkAddress.
 */
BNetworkAddress&
BNetworkInterfaceAddressSettings::Gateway()
{
	return fGateway;
}


/**
 * @brief Serialise this address settings object into a BMessage.
 *
 * @param data  Output BMessage populated with all configured address fields.
 * @return B_OK on success, or an error code if any field cannot be set.
 */
status_t
BNetworkInterfaceAddressSettings::GetMessage(BMessage& data) const
{
	status_t status = B_OK;
	if (fFamily != AF_UNSPEC)
		status = data.SetInt32("family", fFamily);
	if (status == B_OK && fAutoConfigure)
		return data.SetBool("auto_config", fAutoConfigure);

	if (status == B_OK && !fAddress.IsEmpty()) {
		status = data.SetString("address", fAddress.ToString());
		if (status == B_OK && !fMask.IsEmpty())
			status = data.SetString("mask", fMask.ToString());
	}
	if (status == B_OK && !fPeer.IsEmpty())
		status = data.SetString("peer", fPeer.ToString());
	if (status == B_OK && !fBroadcast.IsEmpty())
		status = data.SetString("broadcast", fBroadcast.ToString());
	if (status == B_OK && !fGateway.IsEmpty())
		status = data.SetString("gateway", fGateway.ToString());

	return status;
}


/**
 * @brief Assignment operator — copies all address fields from \a other.
 *
 * @param other  The source object.
 * @return Reference to this object.
 */
BNetworkInterfaceAddressSettings&
BNetworkInterfaceAddressSettings::operator=(
	const BNetworkInterfaceAddressSettings& other)
{
	fFamily = other.fFamily;
	fAutoConfigure = other.fAutoConfigure;
	fAddress = other.fAddress;
	fMask = other.fMask;
	fPeer = other.fPeer;
	fBroadcast = other.fBroadcast;
	fGateway = other.fGateway;

	return *this;
}


// #pragma mark - BNetworkInterfaceSettings


/**
 * @brief Construct a default BNetworkInterfaceSettings with zeroed numeric fields.
 */
BNetworkInterfaceSettings::BNetworkInterfaceSettings()
	:
	fFlags(0),
	fMTU(0),
	fMetric(0)
{
}


/**
 * @brief Construct BNetworkInterfaceSettings from a BMessage.
 *
 * Parses device name, flags, MTU, metric, and all "address" sub-messages.
 *
 * @param message  BMessage holding the serialised interface settings.
 */
BNetworkInterfaceSettings::BNetworkInterfaceSettings(const BMessage& message)
{
	fName = message.GetString("device");
	fFlags = message.GetInt32("flags", 0);
	fMTU = message.GetInt32("mtu", 0);
	fMetric = message.GetInt32("metric", 0);

	BMessage addressData;
	for (int32 index = 0; message.FindMessage("address", index,
			&addressData) == B_OK; index++) {
		BNetworkInterfaceAddressSettings address(addressData);
		fAddresses.push_back(address);
	}
}


/**
 * @brief Destructor.
 */
BNetworkInterfaceSettings::~BNetworkInterfaceSettings()
{
}


/**
 * @brief Return the interface device name.
 *
 * @return Null-terminated device name string (e.g. "en0").
 */
const char*
BNetworkInterfaceSettings::Name() const
{
	return fName;
}


/**
 * @brief Set the interface device name.
 *
 * @param name  New device name string.
 */
void
BNetworkInterfaceSettings::SetName(const char* name)
{
	fName = name;
}


/**
 * @brief Return the interface configuration flags.
 *
 * @return Bitmask of interface flags.
 */
int32
BNetworkInterfaceSettings::Flags() const
{
	return fFlags;
}


/**
 * @brief Set the interface configuration flags.
 *
 * @param flags  New flags bitmask.
 */
void
BNetworkInterfaceSettings::SetFlags(int32 flags)
{
	fFlags = flags;
}


/**
 * @brief Return the configured Maximum Transmission Unit.
 *
 * @return MTU in bytes, or 0 if not configured.
 */
int32
BNetworkInterfaceSettings::MTU() const
{
	return fMTU;
}


/**
 * @brief Set the Maximum Transmission Unit.
 *
 * @param mtu  MTU value in bytes.
 */
void
BNetworkInterfaceSettings::SetMTU(int32 mtu)
{
	fMTU = mtu;
}


/**
 * @brief Return the routing metric for this interface.
 *
 * @return Metric value, or 0 if not configured.
 */
int32
BNetworkInterfaceSettings::Metric() const
{
	return fMetric;
}


/**
 * @brief Set the routing metric for this interface.
 *
 * @param metric  New metric value.
 */
void
BNetworkInterfaceSettings::SetMetric(int32 metric)
{
	fMetric = metric;
}


/**
 * @brief Return the number of address entries for this interface.
 *
 * @return Count of BNetworkInterfaceAddressSettings entries.
 */
int32
BNetworkInterfaceSettings::CountAddresses() const
{
	return fAddresses.size();
}


/**
 * @brief Return a const reference to the address entry at \a index.
 *
 * @param index  Zero-based index into the address list.
 * @return Const reference to the BNetworkInterfaceAddressSettings at \a index.
 */
const BNetworkInterfaceAddressSettings&
BNetworkInterfaceSettings::AddressAt(int32 index) const
{
	return fAddresses[index];
}


/**
 * @brief Return a mutable reference to the address entry at \a index.
 *
 * @param index  Zero-based index into the address list.
 * @return Mutable reference to the BNetworkInterfaceAddressSettings at \a index.
 */
BNetworkInterfaceAddressSettings&
BNetworkInterfaceSettings::AddressAt(int32 index)
{
	return fAddresses[index];
}


/**
 * @brief Return the index of the first address entry matching a given family.
 *
 * @param family  Address family to search for.
 * @return Zero-based index of the first match, or -1 if not found.
 */
int32
BNetworkInterfaceSettings::FindFirstAddress(int family) const
{
	for (int32 index = 0; index < CountAddresses(); index++) {
		const BNetworkInterfaceAddressSettings address = AddressAt(index);
		if (address.Family() == family)
			return index;
	}
	return -1;
}


/**
 * @brief Append an address entry to the interface's address list.
 *
 * @param address  The BNetworkInterfaceAddressSettings to add.
 */
void
BNetworkInterfaceSettings::AddAddress(
	const BNetworkInterfaceAddressSettings& address)
{
	fAddresses.push_back(address);
}


/**
 * @brief Remove the address entry at \a index from the list.
 *
 * @param index  Zero-based index of the entry to remove.
 */
void
BNetworkInterfaceSettings::RemoveAddress(int32 index)
{
	fAddresses.erase(fAddresses.begin() + index);
}


/**
 * @brief Check whether the interface is currently auto-configured for a family.
 *
 * Queries the live kernel interface state rather than the stored settings to
 * determine whether DHCP or SLAAC is active.  Falls back to the persistent
 * settings if no address has been assigned yet.
 *
 * @param family  Address family to check (AF_INET or AF_INET6).
 * @return true if auto-configuration is currently active or configured.
 */
bool
BNetworkInterfaceSettings::IsAutoConfigure(int family) const
{
	BNetworkInterface interface(fName);
	// TODO: this needs to happen at protocol level
	if ((interface.Flags() & (IFF_AUTO_CONFIGURED | IFF_CONFIGURING)) != 0)
		return true;

	BNetworkInterfaceAddress address;
	status_t status = B_ERROR;

	int32 index = interface.FindFirstAddress(family);
	if (index >= 0)
		status = interface.GetAddressAt(index, address);
	if (index < 0 || status != B_OK || address.Address().IsEmpty()) {
		if (status == B_OK) {
			// Check persistent settings for the mode -- the address
			// can also be empty if the automatic configuration hasn't
			// started yet (for example, because there is no link).
			int32 index = FindFirstAddress(family);
			if (index < 0)
				index = FindFirstAddress(AF_UNSPEC);
			if (index >= 0) {
				const BNetworkInterfaceAddressSettings& address
					= AddressAt(index);
				return address.IsAutoConfigure();
			}
		}
	}

	return false;
}


/**
 * @brief Serialise this interface settings object into a BMessage.
 *
 * @param data  Output BMessage populated with device name, flags, MTU,
 *              metric, and all address sub-messages.
 * @return B_OK on success, or an error code if any field cannot be set.
 */
status_t
BNetworkInterfaceSettings::GetMessage(BMessage& data) const
{
	status_t status = data.SetString("device", fName);
	if (status == B_OK && fFlags != 0)
		status = data.SetInt32("flags", fFlags);
	if (status == B_OK && fMTU != 0)
		status = data.SetInt32("mtu", fMTU);
	if (status == B_OK && fMetric != 0)
		status = data.SetInt32("metric", fMetric);

	for (int32 i = 0; i < CountAddresses(); i++) {
		BMessage address;
		status = AddressAt(i).GetMessage(address);
		if (status == B_OK)
			status = data.AddMessage("address", &address);
		if (status != B_OK)
			break;
	}
	return status;
}


// #pragma mark - BNetworkServiceAddressSettings


/**
 * @brief Construct a default BNetworkServiceAddressSettings.
 */
BNetworkServiceAddressSettings::BNetworkServiceAddressSettings()
{
}


/**
 * @brief Construct BNetworkServiceAddressSettings from a BMessage and service defaults.
 *
 * Parses family, address, protocol, type, and port from \a data, falling
 * back to the provided service-level defaults when individual fields are absent.
 *
 * @param data             BMessage holding the serialised address settings.
 * @param serviceFamily    Default address family from the parent service.
 * @param serviceType      Default socket type from the parent service.
 * @param serviceProtocol  Default protocol from the parent service.
 * @param servicePort      Default port from the parent service.
 */
BNetworkServiceAddressSettings::BNetworkServiceAddressSettings(
	const BMessage& data, int serviceFamily, int serviceType,
	int serviceProtocol, int servicePort)
{
	// TODO: dump problems in the settings to syslog
	if (data.FindInt32("family", &fFamily) != B_OK) {
		const char* familyString;
		if (data.FindString("family", &familyString) == B_OK) {
			fFamily = get_address_family(familyString);
			if (fFamily == AF_UNSPEC) {
				// we don't support this family
				fprintf(stderr, "Ignore unknown family: %s\n",
					familyString);
				return;
			}
		} else
			fFamily = serviceFamily;
	}

	if (!parse_address(fFamily, data.GetString("address"), fAddress))
		fAddress.SetToWildcard(fFamily);

	const char* string;
	if (data.FindString("protocol", &string) == B_OK)
		fProtocol = parse_protocol(string);
	else
		fProtocol = serviceProtocol;

	if (data.FindString("type", &string) == B_OK)
		fType = parse_type(string);
	else if (fProtocol != serviceProtocol)
		fType = type_for_protocol(fProtocol);
	else
		fType = serviceType;

	fAddress.SetPort(data.GetInt32("port", servicePort));
}


/**
 * @brief Destructor.
 */
BNetworkServiceAddressSettings::~BNetworkServiceAddressSettings()
{
}


/**
 * @brief Return the address family for this service address.
 *
 * @return AF_INET, AF_INET6, or AF_UNSPEC.
 */
int
BNetworkServiceAddressSettings::Family() const
{
	return fFamily;
}


/**
 * @brief Set the address family for this service address.
 *
 * @param family  New address family constant.
 */
void
BNetworkServiceAddressSettings::SetFamily(int family)
{
	fFamily = family;
}


/**
 * @brief Return the IP protocol number for this service address.
 *
 * @return IPPROTO_* constant.
 */
int
BNetworkServiceAddressSettings::Protocol() const
{
	return fProtocol;
}


/**
 * @brief Set the IP protocol number for this service address.
 *
 * @param protocol  IPPROTO_* constant to assign.
 */
void
BNetworkServiceAddressSettings::SetProtocol(int protocol)
{
	fProtocol = protocol;
}


/**
 * @brief Return the socket type for this service address.
 *
 * @return SOCK_STREAM or SOCK_DGRAM.
 */
int
BNetworkServiceAddressSettings::Type() const
{
	return fType;
}


/**
 * @brief Set the socket type for this service address.
 *
 * @param type  SOCK_STREAM or SOCK_DGRAM.
 */
void
BNetworkServiceAddressSettings::SetType(int type)
{
	fType = type;
}


/**
 * @brief Return a const reference to the bound network address.
 *
 * @return Const reference to the BNetworkAddress.
 */
const BNetworkAddress&
BNetworkServiceAddressSettings::Address() const
{
	return fAddress;
}


/**
 * @brief Return a mutable reference to the bound network address.
 *
 * @return Mutable reference to the BNetworkAddress.
 */
BNetworkAddress&
BNetworkServiceAddressSettings::Address()
{
	return fAddress;
}


/**
 * @brief Serialise this service address settings object into a BMessage.
 *
 * @param data  Output BMessage to populate.
 * @return B_NOT_SUPPORTED (not yet implemented).
 */
status_t
BNetworkServiceAddressSettings::GetMessage(BMessage& data) const
{
	// TODO!
	return B_NOT_SUPPORTED;
}


/**
 * @brief Compare two service address settings for equality.
 *
 * @param other  The address settings to compare against.
 * @return true if family, type, protocol, and address all match.
 */
bool
BNetworkServiceAddressSettings::operator==(
	const BNetworkServiceAddressSettings& other) const
{
	return Family() == other.Family()
		&& Type() == other.Type()
		&& Protocol() == other.Protocol()
		&& Address() == other.Address();
}


// #pragma mark - BNetworkServiceSettings


/**
 * @brief Construct a default BNetworkServiceSettings with no name and defaults.
 */
BNetworkServiceSettings::BNetworkServiceSettings()
	:
	fFamily(AF_UNSPEC),
	fType(-1),
	fProtocol(-1),
	fPort(-1),
	fEnabled(true),
	fStandAlone(false)
{
}


/**
 * @brief Construct BNetworkServiceSettings from a BMessage.
 *
 * Parses name, family, protocol, type, port, stand_alone, launch arguments,
 * and per-address configuration from \a message.
 *
 * @param message  BMessage holding the serialised service settings.
 */
BNetworkServiceSettings::BNetworkServiceSettings(const BMessage& message)
	:
	fType(-1),
	fProtocol(-1),
	fPort(-1),
	fEnabled(true),
	fStandAlone(false)
{
	// TODO: user/group is currently ignored!

	fName = message.GetString("name");

	// Default family/port/protocol/type for all addresses

	// we default to inet/tcp/port-from-service-name if nothing is specified
	const char* string;
	if (message.FindString("family", &string) != B_OK)
		string = "inet";

	fFamily = get_address_family(string);
	if (fFamily == AF_UNSPEC)
		fFamily = AF_INET;

	if (message.FindString("protocol", &string) == B_OK)
		fProtocol = parse_protocol(string);
	else {
		string = "tcp";
			// we set 'string' here for an eventual call to getservbyname()
			// below
		fProtocol = IPPROTO_TCP;
	}

	if (message.FindInt32("port", &fPort) != B_OK) {
		struct servent* servent = getservbyname(Name(), string);
		if (servent != NULL)
			fPort = ntohs(servent->s_port);
		else
			fPort = -1;
	}

	if (message.FindString("type", &string) == B_OK)
		fType = parse_type(string);
	else
		fType = type_for_protocol(fProtocol);

	fStandAlone = message.GetBool("stand_alone");

	const char* argument;
	for (int i = 0; message.FindString("launch", i, &argument) == B_OK; i++) {
		fArguments.Add(argument);
	}

	BMessage addressData;
	int32 i = 0;
	for (; message.FindMessage("address", i, &addressData) == B_OK; i++) {
		BNetworkServiceAddressSettings address(addressData, fFamily,
			fType, fProtocol, fPort);
		fAddresses.push_back(address);
	}

	if (i == 0 && (fFamily < 0 || fPort < 0)) {
		// no address specified
		printf("service %s has no address specified\n", Name());
		return;
	}

	if (i == 0) {
		// no address specified, but family/port were given; add empty address
		BNetworkServiceAddressSettings address;
		address.SetFamily(fFamily);
		address.SetType(fType);
		address.SetProtocol(fProtocol);
		address.Address().SetToWildcard(fFamily, fPort);

		fAddresses.push_back(address);
	}
}


/**
 * @brief Destructor.
 */
BNetworkServiceSettings::~BNetworkServiceSettings()
{
}


/**
 * @brief Check whether this service is fully and validly configured.
 *
 * @return B_OK if name, launch arguments, and at least one address are present;
 *         B_BAD_VALUE otherwise.
 */
status_t
BNetworkServiceSettings::InitCheck() const
{
	if (!fName.IsEmpty() && !fArguments.IsEmpty() && CountAddresses() > 0)
		return B_OK;

	return B_BAD_VALUE;
}


/**
 * @brief Return the service name.
 *
 * @return Null-terminated service name string.
 */
const char*
BNetworkServiceSettings::Name() const
{
	return fName.String();
}


/**
 * @brief Set the service name.
 *
 * @param name  New service name string.
 */
void
BNetworkServiceSettings::SetName(const char* name)
{
	fName = name;
}


/**
 * @brief Return whether this service runs as a stand-alone daemon.
 *
 * @return true if the service manages its own socket rather than using inetd.
 */
bool
BNetworkServiceSettings::IsStandAlone() const
{
	return fStandAlone;
}


/**
 * @brief Set whether this service runs as a stand-alone daemon.
 *
 * @param alone  true for stand-alone, false for inetd-style.
 */
void
BNetworkServiceSettings::SetStandAlone(bool alone)
{
	fStandAlone = alone;
}


/**
 * @brief Return whether this service is enabled and fully configured.
 *
 * @return true if InitCheck() passes and fEnabled is true.
 */
bool
BNetworkServiceSettings::IsEnabled() const
{
	return InitCheck() == B_OK && fEnabled;
}


/**
 * @brief Enable or disable this service.
 *
 * @param enable  true to enable, false to disable.
 */
void
BNetworkServiceSettings::SetEnabled(bool enable)
{
	fEnabled = enable;
}


/**
 * @brief Return the default address family for this service.
 *
 * @return AF_INET, AF_INET6, or AF_UNSPEC.
 */
int
BNetworkServiceSettings::Family() const
{
	return fFamily;
}


/**
 * @brief Set the default address family for this service.
 *
 * @param family  New address family constant.
 */
void
BNetworkServiceSettings::SetFamily(int family)
{
	fFamily = family;
}


/**
 * @brief Return the default IP protocol number for this service.
 *
 * @return IPPROTO_* constant.
 */
int
BNetworkServiceSettings::Protocol() const
{
	return fProtocol;
}


/**
 * @brief Set the default IP protocol number for this service.
 *
 * @param protocol  IPPROTO_* constant to assign.
 */
void
BNetworkServiceSettings::SetProtocol(int protocol)
{
	fProtocol = protocol;
}


/**
 * @brief Return the default socket type for this service.
 *
 * @return SOCK_STREAM or SOCK_DGRAM.
 */
int
BNetworkServiceSettings::Type() const
{
	return fType;
}


/**
 * @brief Set the default socket type for this service.
 *
 * @param type  SOCK_STREAM or SOCK_DGRAM.
 */
void
BNetworkServiceSettings::SetType(int type)
{
	fType = type;
}


/**
 * @brief Return the default port number for this service.
 *
 * @return Port number, or -1 if not configured.
 */
int
BNetworkServiceSettings::Port() const
{
	return fPort;
}


/**
 * @brief Set the default port number for this service.
 *
 * @param port  Port number to assign.
 */
void
BNetworkServiceSettings::SetPort(int port)
{
	fPort = port;
}


/**
 * @brief Return the number of launch arguments for this service.
 *
 * @return Number of strings in the launch argument list.
 */
int32
BNetworkServiceSettings::CountArguments() const
{
	return fArguments.CountStrings();
}


/**
 * @brief Return the launch argument at \a index.
 *
 * @param index  Zero-based argument index.
 * @return Null-terminated argument string.
 */
const char*
BNetworkServiceSettings::ArgumentAt(int32 index) const
{
	return fArguments.StringAt(index);
}


/**
 * @brief Append a launch argument to the service's argument list.
 *
 * @param argument  Null-terminated argument string to add.
 */
void
BNetworkServiceSettings::AddArgument(const char* argument)
{
	fArguments.Add(argument);
}


/**
 * @brief Remove the launch argument at \a index.
 *
 * @param index  Zero-based index of the argument to remove.
 */
void
BNetworkServiceSettings::RemoveArgument(int32 index)
{
	fArguments.Remove(index);
}


/**
 * @brief Return the number of address entries for this service.
 *
 * @return Count of BNetworkServiceAddressSettings entries.
 */
int32
BNetworkServiceSettings::CountAddresses() const
{
	return fAddresses.size();
}


/**
 * @brief Return a const reference to the service address entry at \a index.
 *
 * @param index  Zero-based index into the address list.
 * @return Const reference to the BNetworkServiceAddressSettings.
 */
const BNetworkServiceAddressSettings&
BNetworkServiceSettings::AddressAt(int32 index) const
{
	return fAddresses[index];
}


/**
 * @brief Append an address entry to the service's address list.
 *
 * @param address  The BNetworkServiceAddressSettings to add.
 */
void
BNetworkServiceSettings::AddAddress(
	const BNetworkServiceAddressSettings& address)
{
	fAddresses.push_back(address);
}


/**
 * @brief Remove the address entry at \a index from the service's list.
 *
 * @param index  Zero-based index of the entry to remove.
 */
void
BNetworkServiceSettings::RemoveAddress(int32 index)
{
	fAddresses.erase(fAddresses.begin() + index);
}


/**
 * @brief Query the net_server to determine whether this service is currently running.
 *
 * Sends kMsgIsServiceRunning to the net_server and interprets the "running"
 * field of the reply.
 *
 * @return true if the server reports the service is active.
 */
bool
BNetworkServiceSettings::IsRunning() const
{
	BMessage request(kMsgIsServiceRunning);
	request.AddString("name", fName);

	BMessenger networkServer(kNetServerSignature);
	BMessage reply;
	status_t status = networkServer.SendMessage(&request, &reply);
	if (status == B_OK)
		return reply.GetBool("running");

	return false;
}


/**
 * @brief Serialise this service settings object into a BMessage.
 *
 * Writes name, enabled state, stand_alone flag, default family/type/protocol/
 * port, launch arguments, and all non-default address entries.
 *
 * @param data  Output BMessage to populate.
 * @return B_OK on success, or an error code if any field cannot be set.
 */
status_t
BNetworkServiceSettings::GetMessage(BMessage& data) const
{
	status_t status = data.SetString("name", fName);
	if (status == B_OK && !fEnabled)
		status = data.SetBool("disabled", true);
	if (status == B_OK && fStandAlone)
		status = data.SetBool("stand_alone", true);

	if (fFamily != AF_UNSPEC)
		status = data.SetInt32("family", fFamily);
	if (fType != -1)
		status = data.SetInt32("type", fType);
	if (fProtocol != -1)
		status = data.SetInt32("protocol", fProtocol);
	if (fPort != -1)
		status = data.SetInt32("port", fPort);

	for (int32 i = 0; i < fArguments.CountStrings(); i++) {
		if (status == B_OK)
			status = data.AddString("launch", fArguments.StringAt(i));
		if (status != B_OK)
			break;
	}

	for (int32 i = 0; i < CountAddresses(); i++) {
		BNetworkServiceAddressSettings address = AddressAt(i);
		if (address.Family() == Family()
			&& address.Type() == Type()
			&& address.Protocol() == Protocol()
			&& address.Address().IsWildcard()
			&& address.Address().Port() == Port()) {
			// This address will be created automatically, no need to store it
			continue;
		}

		BMessage addressMessage;
		status = AddressAt(i).GetMessage(addressMessage);
		if (status == B_OK)
			status = data.AddMessage("address", &addressMessage);
		if (status != B_OK)
			break;
	}
	return status;
}
