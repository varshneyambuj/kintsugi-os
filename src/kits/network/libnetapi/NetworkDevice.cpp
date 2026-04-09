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

/** @file NetworkDevice.cpp
 *  @brief BNetworkDevice implementation wrapping IEEE 802.11 ioctls for
 *         scanning, joining, and introspecting wireless interfaces. */


#include <NetworkDevice.h>

#include <errno.h>
#include <net/if.h>
#include <net/if_media.h>
#include <stdio.h>
#include <sys/sockio.h>

#include <Looper.h>
#include <Messenger.h>

#include <AutoDeleter.h>
#include <NetServer.h>
#include <NetworkNotifications.h>

extern "C" {
#	include <compat/sys/cdefs.h>
#	include <compat/sys/ioccom.h>
#	include <net80211/ieee80211_ioctl.h>
}


//#define TRACE_DEVICE
#ifdef TRACE_DEVICE
#	define TRACE(x, ...) printf(x, __VA_ARGS__);
#else
#	define TRACE(x, ...) ;
#endif


namespace {


struct ie_data {
	uint8	type;
	uint8	length;
	uint8	data[1];
};


// #pragma mark - private functions (code shared with net_server)


/** @brief Issues an SIOCG80211 ioctl to read an 802.11 parameter.
 *  @param name   Interface name (e.g. "wlan0").
 *  @param type   ieee80211req i_type to query.
 *  @param data   Destination buffer.
 *  @param length In/out: buffer capacity / bytes actually returned.
 *  @return B_OK on success, or an errno value. */
static status_t
get_80211(const char* name, int32 type, void* data, int32& length)
{
	FileDescriptorCloser socket(::socket(AF_INET, SOCK_DGRAM, 0));
	if (!socket.IsSet())
		return errno;

	struct ieee80211req ireq;
	strlcpy(ireq.i_name, name, IF_NAMESIZE);
	ireq.i_type = type;
	ireq.i_val = 0;
	ireq.i_len = length;
	ireq.i_data = data;

	if (ioctl(socket.Get(), SIOCG80211, &ireq, sizeof(struct ieee80211req))
		< 0)
		return errno;

	length = ireq.i_len;
	return B_OK;
}


/** @brief Issues an SIOCS80211 ioctl to set an 802.11 parameter. */
static status_t
set_80211(const char* name, int32 type, void* data,
	int32 length = 0, int32 value = 0)
{
	FileDescriptorCloser socket(::socket(AF_INET, SOCK_DGRAM, 0));
	if (!socket.IsSet())
		return errno;

	struct ieee80211req ireq;
	strlcpy(ireq.i_name, name, IF_NAMESIZE);
	ireq.i_type = type;
	ireq.i_val = value;
	ireq.i_len = length;
	ireq.i_data = data;

	if (ioctl(socket.Get(), SIOCS80211, &ireq, sizeof(struct ieee80211req))
		< 0)
		return errno;

	return B_OK;
}


/** @brief Generic ioctl helper used by interface requests.
 *         Opens an AF_LINK socket, copies @a name into the request, and
 *         performs the given ioctl operation.
 *  @tparam T     An ifreq-compatible structure.
 *  @param  name   Interface name to target.
 *  @param  option Ioctl number (e.g. SIOCGIFMEDIA). */
template<typename T> status_t
do_request(T& request, const char* name, int option)
{
	FileDescriptorCloser socket(::socket(AF_LINK, SOCK_DGRAM, 0));
	if (!socket.IsSet())
		return errno;

	strlcpy(((struct ifreq&)request).ifr_name, name, IF_NAMESIZE);

	if (ioctl(socket.Get(), option, &request, sizeof(T)) < 0)
		return errno;

	return B_OK;
}


/** @brief Specialisation of do_request() for 802.11 requests, which use
 *         an AF_INET socket and the ieee80211req name field. */
template<> status_t
do_request<ieee80211req>(ieee80211req& request, const char* name, int option)
{
	FileDescriptorCloser socket(::socket(AF_INET, SOCK_DGRAM, 0));
	if (!socket.IsSet())
		return errno;

	strlcpy(((struct ieee80211req&)request).i_name, name, IFNAMSIZ);

	if (ioctl(socket.Get(), option, &request, sizeof(request)) < 0)
		return errno;

	return B_OK;
}


/** @brief Reads a 16-bit little-endian value from a byte stream and advances
 *         the read cursor. */
static uint16
read_le16(uint8*& data, int32& length)
{
	uint16 value = B_LENDIAN_TO_HOST_INT16(*(uint16*)data);
	data += 2;
	length -= 2;
	return value;
}


/** @brief Reads a 32-bit little-endian value from a byte stream and advances
 *         the read cursor. */
static uint32
read_le32(uint8*& data, int32& length)
{
	uint32 value = B_LENDIAN_TO_HOST_INT32(*(uint32*)data);
	data += 4;
	length -= 4;
	return value;
}


/** @brief Translates an RSN (Robust Security Network) cipher suite selector
 *         into the corresponding B_NETWORK_CIPHER_* constant. */
static uint32
from_rsn_cipher(uint32 cipher)
{
	if ((cipher & 0xffffff) != RSN_OUI)
		return B_NETWORK_CIPHER_CCMP;

	switch (cipher >> 24) {
		case RSN_CSE_NULL:
			return B_NETWORK_CIPHER_NONE;
		case RSN_CSE_WEP40:
			return B_NETWORK_CIPHER_WEP_40;
		case RSN_CSE_WEP104:
			return B_NETWORK_CIPHER_WEP_104;
		case RSN_CSE_TKIP:
			return B_NETWORK_CIPHER_TKIP;
		default:
		case RSN_CSE_CCMP:
			return B_NETWORK_CIPHER_CCMP;
		case RSN_CSE_WRAP:
			return B_NETWORK_CIPHER_AES_128_CMAC;
	}
}


/** @brief Translates an RSN authentication/key-management suite selector
 *         into the corresponding B_KEY_MODE_* constant. */
static uint32
from_rsn_key_mode(uint32 mode)
{
	if ((mode & 0xffffff) != RSN_OUI)
		return B_KEY_MODE_IEEE802_1X;

	switch (mode >> 24) {
		default:
		case RSN_ASE_8021X_UNSPEC:
			return B_KEY_MODE_IEEE802_1X;
		case RSN_ASE_8021X_PSK:
			return B_KEY_MODE_PSK;
		// the following are currently not defined in net80211
		case 3:
			return B_KEY_MODE_FT_IEEE802_1X;
		case 4:
			return B_KEY_MODE_FT_PSK;
		case 5:
			return B_KEY_MODE_IEEE802_1X_SHA256;
		case 6:
			return B_KEY_MODE_PSK_SHA256;
	}
}


/** @brief Parses the cipher/key-management fields common to both RSN (WPA2)
 *         and WPA (TKIP) information elements and populates @a network. */
static void
parse_ie_rsn_wpa(wireless_network& network, uint8*& data, int32& length)
{
	if (length >= 4) {
		// parse group cipher
		network.group_cipher = from_rsn_cipher(read_le32(data, length));
	} else if (length > 0)
		return;

	if (length >= 2) {
		// parse unicast cipher
		uint16 count = read_le16(data, length);
		network.cipher = 0;

		for (uint16 i = 0; i < count; i++) {
			if (length < 4)
				return;
			network.cipher |= from_rsn_cipher(read_le32(data, length));
		}
	} else if (length > 0)
		return;

	if (length >= 2) {
		// parse key management mode
		uint16 count = read_le16(data, length);
		network.key_mode = 0;

		for (uint16 i = 0; i < count; i++) {
			if (length < 4)
				return;
			network.key_mode |= from_rsn_key_mode(read_le32(data, length));
		}
	} else if (length > 0)
		return;

	// TODO: capabilities, and PMKID following in case of RSN
}


/** @brief Parses a Robust Security Network (RSN/WPA2) information element
 *         from a beacon/probe-response frame. */
static void
parse_ie_rsn(wireless_network& network, ie_data* ie)
{
	network.authentication_mode = B_NETWORK_AUTHENTICATION_WPA2;
	network.cipher = B_NETWORK_CIPHER_CCMP;
	network.group_cipher = B_NETWORK_CIPHER_CCMP;
	network.key_mode = B_KEY_MODE_IEEE802_1X;

	int32 length = ie->length;
	if (length < 2)
		return;

	uint8* data = ie->data;

	uint16 version = read_le16(data, length);
	if (version != RSN_VERSION)
		return;

	parse_ie_rsn_wpa(network, data, length);
}


/** @brief Parses a legacy WPA (TKIP) vendor-specific information element.
 *  @return true if the element looked like a WPA IE, false otherwise. */
static bool
parse_ie_wpa(wireless_network& network, ie_data* ie)
{
	int32 length = ie->length;
	if (length < 6)
		return false;

	uint8* data = ie->data;

	uint32 oui = read_le32(data, length);
	TRACE("  oui: %" B_PRIx32 "\n", oui);
	if (oui != ((WPA_OUI_TYPE << 24) | WPA_OUI))
		return false;

	uint16 version = read_le16(data, length);
	if (version != WPA_VERSION)
		return false;

	network.authentication_mode = B_NETWORK_AUTHENTICATION_WPA;
	network.cipher = B_NETWORK_CIPHER_TKIP;
	network.group_cipher = B_NETWORK_CIPHER_TKIP;
	network.key_mode = B_KEY_MODE_IEEE802_1X;

	parse_ie_rsn_wpa(network, data, length);
	return true;
}


/** @brief Walks an information-element blob, extracting SSID, RSN, and WPA
 *         entries into @a network, and deriving the final authentication
 *         mode from the combination of security fields found. */
static void
parse_ie(wireless_network& network, uint8* _ie, int32 ieLength)
{
	struct ie_data* ie = (ie_data*)_ie;
	bool hadRSN = false;
	bool hadWPA = false;

	while (ieLength > 1) {
		TRACE("ie type %u\n", ie->type);
		switch (ie->type) {
			case IEEE80211_ELEMID_SSID:
				strlcpy(network.name, (char*)ie->data,
					min_c(ie->length + 1, (int)sizeof(network.name)));
				break;
			case IEEE80211_ELEMID_RSN:
				parse_ie_rsn(network, ie);
				hadRSN = true;
				break;
			case IEEE80211_ELEMID_VENDOR:
				if (!hadRSN && parse_ie_wpa(network, ie))
					hadWPA = true;
				break;
		}

		ieLength -= 2 + ie->length;
		ie = (ie_data*)((uint8*)ie + 2 + ie->length);
	}

	if (hadRSN || hadWPA) {
		// Determine authentication mode

		if ((network.key_mode & (B_KEY_MODE_IEEE802_1X_SHA256
				| B_KEY_MODE_PSK_SHA256)) != 0) {
			network.authentication_mode = B_NETWORK_AUTHENTICATION_WPA2;
		} else if ((network.key_mode & (B_KEY_MODE_IEEE802_1X
				| B_KEY_MODE_PSK | B_KEY_MODE_FT_IEEE802_1X
				| B_KEY_MODE_FT_PSK)) != 0) {
			if (!hadRSN)
				network.authentication_mode = B_NETWORK_AUTHENTICATION_WPA;
		} else if ((network.key_mode & B_KEY_MODE_NONE) != 0) {
			if ((network.cipher & (B_NETWORK_CIPHER_WEP_40
					| B_NETWORK_CIPHER_WEP_104)) != 0)
				network.authentication_mode = B_NETWORK_AUTHENTICATION_WEP;
			else
				network.authentication_mode = B_NETWORK_AUTHENTICATION_NONE;
		}
	}
}


/** @brief Parses the information elements attached to a station info record. */
static void
parse_ie(wireless_network& network, struct ieee80211req_sta_info& info)
{
	parse_ie(network, (uint8*)&info + info.isi_ie_off, info.isi_ie_len);
}


/** @brief Parses the information elements attached to a scan result record. */
static void
parse_ie(wireless_network& network, struct ieee80211req_scan_result& result)
{
	parse_ie(network, (uint8*)&result + result.isr_ie_off + result.isr_ssid_len
			+ result.isr_meshid_len, result.isr_ie_len);
}


/** @brief Extracts just the SSID string from an IE blob.
 *  @return true if an SSID element was found, false otherwise. */
static bool
get_ssid_from_ie(char* name, uint8* _ie, int32 ieLength)
{
	struct ie_data* ie = (ie_data*)_ie;

	while (ieLength > 1) {
		switch (ie->type) {
			case IEEE80211_ELEMID_SSID:
				strlcpy(name, (char*)ie->data, min_c(ie->length + 1, 32));
				return true;
		}

		ieLength -= 2 + ie->length;
		ie = (ie_data*)((uint8*)ie + 2 + ie->length);
	}
	return false;
}


/** @brief Convenience overload extracting the SSID from a station info. */
static bool
get_ssid_from_ie(char* name, struct ieee80211req_sta_info& info)
{
	return get_ssid_from_ie(name, (uint8*)&info + info.isi_ie_off,
		info.isi_ie_len);
}


/** @brief Populates a wireless_network from a kernel station info entry. */
static void
fill_wireless_network(wireless_network& network,
	struct ieee80211req_sta_info& info)
{
	network.name[0] = '\0';
	network.address.SetToLinkLevel(info.isi_macaddr,
		IEEE80211_ADDR_LEN);
	network.signal_strength = info.isi_rssi;
	network.noise_level = info.isi_noise;
	network.flags |= (info.isi_capinfo & IEEE80211_CAPINFO_PRIVACY) != 0
		? B_NETWORK_IS_ENCRYPTED : 0;

	network.authentication_mode = 0;
	network.cipher = 0;
	network.group_cipher = 0;
	network.key_mode = 0;

	parse_ie(network, info);
}


/** @brief Populates a wireless_network from a scan result entry. */
static void
fill_wireless_network(wireless_network& network, const char* networkName,
	struct ieee80211req_scan_result& result)
{
	strlcpy(network.name, networkName, sizeof(network.name));
	network.address.SetToLinkLevel(result.isr_bssid,
		IEEE80211_ADDR_LEN);
	network.signal_strength = result.isr_rssi;
	network.noise_level = result.isr_noise;
	network.flags = (result.isr_capinfo & IEEE80211_CAPINFO_PRIVACY)
		!= 0 ? B_NETWORK_IS_ENCRYPTED : 0;

	network.authentication_mode = 0;
	network.cipher = 0;
	network.group_cipher = 0;
	network.key_mode = 0;

	parse_ie(network, result);
}


/** @brief Retrieves all scan results from @a device and allocates a
 *         contiguous array of wireless_network structs.
 *  @param device    Interface name to query.
 *  @param networks  On success, set to a new[] allocated array owned by the
 *                   caller. Must be NULL on entry.
 *  @param count     On success, the number of entries in @a networks.
 *  @return B_OK on success, or an error code. */
static status_t
get_scan_results(const char* device, wireless_network*& networks, uint32& count)
{
	if (networks != NULL)
		return B_BAD_VALUE;

	// TODO: Find some way to reduce code duplication with the following function!
	const size_t kBufferSize = 65535;
	uint8* buffer = (uint8*)malloc(kBufferSize);
	if (buffer == NULL)
		return B_NO_MEMORY;
	MemoryDeleter deleter(buffer);

	int32 length = kBufferSize;
	status_t status = get_80211(device, IEEE80211_IOC_SCAN_RESULTS, buffer,
		length);
	if (status != B_OK)
		return status;

	BObjectList<wireless_network> networksList(true);

	int32 bytesLeft = length;
	uint8* entry = buffer;

	while (bytesLeft > (int32)sizeof(struct ieee80211req_scan_result)) {
		ieee80211req_scan_result* result
			= (ieee80211req_scan_result*)entry;

		char networkName[32];
		strlcpy(networkName, (char*)(result + 1),
			min_c((int)sizeof(networkName), result->isr_ssid_len + 1));

		wireless_network* network = new wireless_network;
		fill_wireless_network(*network, networkName, *result);
		networksList.AddItem(network);

		entry += result->isr_len;
		bytesLeft -= result->isr_len;
	}

	count = 0;
	if (!networksList.IsEmpty()) {
		networks = new wireless_network[networksList.CountItems()];
		for (int32 i = 0; i < networksList.CountItems(); i++) {
			networks[i] = *networksList.ItemAt(i);
			count++;
		}
	}

	return B_OK;
}


/** @brief Retrieves a single scan result matching @a index, BSSID @a address,
 *         or SSID @a name. At least one selector should be provided.
 *  @return B_OK on match, or B_ENTRY_NOT_FOUND if no matching AP was seen. */
static status_t
get_scan_result(const char* device, wireless_network& network, uint32 index,
	const BNetworkAddress* address, const char* name)
{
	if (address != NULL && address->Family() != AF_LINK)
		return B_BAD_VALUE;

	// TODO: Find some way to reduce code duplication with the preceding function!
	const size_t kBufferSize = 65535;
	uint8* buffer = (uint8*)malloc(kBufferSize);
	if (buffer == NULL)
		return B_NO_MEMORY;

	MemoryDeleter deleter(buffer);

	int32 length = kBufferSize;
	status_t status = get_80211(device, IEEE80211_IOC_SCAN_RESULTS, buffer,
		length);
	if (status != B_OK)
		return status;

	int32 bytesLeft = length;
	uint8* entry = buffer;
	uint32 count = 0;

	while (bytesLeft > (int32)sizeof(struct ieee80211req_scan_result)) {
		ieee80211req_scan_result* result
			= (ieee80211req_scan_result*)entry;

		char networkName[32];
		strlcpy(networkName, (char*)(result + 1),
			min_c((int)sizeof(networkName), result->isr_ssid_len + 1));

		if (index == count || (address != NULL && !memcmp(
				address->LinkLevelAddress(), result->isr_bssid,
				IEEE80211_ADDR_LEN))
			|| (name != NULL && !strcmp(networkName, name))) {
			// Fill wireless_network with scan result data
			fill_wireless_network(network, networkName, *result);
			return B_OK;
		}

		entry += result->isr_len;
		bytesLeft -= result->isr_len;
		count++;
	}

	return B_ENTRY_NOT_FOUND;
}


/** @brief Retrieves information about an already-associated station
 *         (as opposed to a scan result) from the driver. */
static status_t
get_station(const char* device, wireless_network& network, uint32 index,
	const BNetworkAddress* address, const char* name)
{
	if (address != NULL && address->Family() != AF_LINK)
		return B_BAD_VALUE;

	const size_t kBufferSize = 65535;
	uint8* buffer = (uint8*)malloc(kBufferSize);
	if (buffer == NULL)
		return B_NO_MEMORY;

	MemoryDeleter deleter(buffer);

	struct ieee80211req_sta_req& request = *(ieee80211req_sta_req*)buffer;
	if (address != NULL) {
		memcpy(request.is_u.macaddr, address->LinkLevelAddress(),
			IEEE80211_ADDR_LEN);
	} else
		memset(request.is_u.macaddr, 0xff, IEEE80211_ADDR_LEN);

	int32 length = kBufferSize;
	status_t status = get_80211(device, IEEE80211_IOC_STA_INFO, &request,
		length);
	if (status != B_OK)
		return status;

	int32 bytesLeft = length;
	uint8* entry = (uint8*)&request.info[0];
	uint32 count = 0;

	while (bytesLeft > (int32)sizeof(struct ieee80211req_sta_info)) {
		ieee80211req_sta_info* info = (ieee80211req_sta_info*)entry;

		char networkName[32];
		get_ssid_from_ie(networkName, *info);
		if (index == count || address != NULL
			|| (name != NULL && !strcmp(networkName, name))) {
			fill_wireless_network(network, *info);
			return B_OK;
		}

		entry += info->isi_len;
		bytesLeft -= info->isi_len;
		count++;
	}

	return B_ENTRY_NOT_FOUND;
}


/** @brief Tries get_station() first, then falls back to get_scan_result()
 *         if the station is not currently associated. */
static status_t
get_network(const char* device, wireless_network& network, uint32 index,
	const BNetworkAddress* address, const char* name)
{
	status_t status = get_station(device, network, index, address, name);
	if (status != B_OK)
		return get_scan_result(device, network, index, address, name);

	return B_OK;
}


}	// namespace


// #pragma mark -


/** @brief Default constructor. Builds an unbound device handle. */
BNetworkDevice::BNetworkDevice()
{
	Unset();
}


/** @brief Constructs a device handle bound to the interface named @a name. */
BNetworkDevice::BNetworkDevice(const char* name)
{
	SetTo(name);
}


/** @brief Destructor. */
BNetworkDevice::~BNetworkDevice()
{
}


/** @brief Clears the bound interface name. */
void
BNetworkDevice::Unset()
{
	fName[0] = '\0';
}


/** @brief Binds this device handle to a specific interface name. */
void
BNetworkDevice::SetTo(const char* name)
{
	strlcpy(fName, name, IF_NAMESIZE);
}


/** @brief Returns the interface name bound to this device. */
const char*
BNetworkDevice::Name() const
{
	return fName;
}


/** @brief Reports whether the interface exists (SIOCGIFINDEX succeeds). */
bool
BNetworkDevice::Exists() const
{
	ifreq request;
	return do_request(request, Name(), SIOCGIFINDEX) == B_OK;
}


/** @brief Returns the numeric interface index, or 0 if the interface is gone. */
uint32
BNetworkDevice::Index() const
{
	ifreq request;
	if (do_request(request, Name(), SIOCGIFINDEX) != B_OK)
		return 0;

	return request.ifr_index;
}


/** @brief Returns the interface flag bitmask (IFF_UP, IFF_BROADCAST, ...). */
uint32
BNetworkDevice::Flags() const
{
	ifreq request;
	if (do_request(request, Name(), SIOCGIFFLAGS) != B_OK)
		return 0;

	return request.ifr_flags;
}


/** @brief Reports whether the link is physically up (IFF_LINK is set). */
bool
BNetworkDevice::HasLink() const
{
	return (Flags() & IFF_LINK) != 0;
}


/** @brief Returns the current IFM_* media type/subtype combination. */
int32
BNetworkDevice::Media() const
{
	ifreq request;
	if (do_request(request, Name(), SIOCGIFMEDIA) != B_OK)
		return -1;

	return request.ifr_media;
}


/** @brief Forces the interface to a specific media/link mode via SIOCSIFMEDIA. */
status_t
BNetworkDevice::SetMedia(int32 media)
{
	ifreq request;
	request.ifr_media = media;
	return do_request(request, Name(), SIOCSIFMEDIA);
}


/** @brief Retrieves the device's hardware (MAC) address. */
status_t
BNetworkDevice::GetHardwareAddress(BNetworkAddress& address)
{
	ifreq request;
	status_t status = do_request(request, Name(), SIOCGIFADDR);
	if (status != B_OK)
		return status;

	address.SetTo(request.ifr_addr);
	return B_OK;
}


/** @brief Reports whether the underlying media is Ethernet. */
bool
BNetworkDevice::IsEthernet()
{
	return IFM_TYPE(Media()) == IFM_ETHER;
}


/** @brief Reports whether the underlying media is IEEE 802.11 (Wi-Fi). */
bool
BNetworkDevice::IsWireless()
{
	return IFM_TYPE(Media()) == IFM_IEEE80211;
}


/** @brief Dispatches a driver-specific ioctl to either the Ethernet or
 *         802.11 ioctl family based on the interface media type. */
status_t
BNetworkDevice::Control(int option, void* request)
{
	switch (IFM_TYPE(Media())) {
		case IFM_ETHER:
			return do_request(*reinterpret_cast<ifreq*>(request),
				&fName[0], option);

		case IFM_IEEE80211:
			return do_request(*reinterpret_cast<ieee80211req*>(request),
				&fName[0], option);

		default:
			return B_ERROR;
	}
}


/** @brief Initiates a Wi-Fi scan on this device.
 *         Optionally waits for the scan to complete by subscribing to
 *         B_WATCH_NETWORK_WLAN_CHANGES notifications.
 *  @param wait        If true, block until the scan completes.
 *  @param forceRescan If true, flush any cached scan results first.
 *  @return B_OK on success, or an errno/status code. */
status_t
BNetworkDevice::Scan(bool wait, bool forceRescan)
{
	// Network status listener for change notifications
	class ScanListener : public BLooper {
	public:
		ScanListener(BString iface)
			:
			fInterface(iface)
		{
			start_watching_network(B_WATCH_NETWORK_WLAN_CHANGES, this);
		}
		virtual ~ScanListener()
		{
			stop_watching_network(this);
		}

	protected:
		virtual void MessageReceived(BMessage *message)
		{
			if (message->what != B_NETWORK_MONITOR) {
				BLooper::MessageReceived(message);
				return;
			}

			BString interfaceName;
			if (message->FindString("interface", &interfaceName) != B_OK)
				return;
			// See comment in AutoconfigLooper::_NetworkMonitorNotification
			// for the reason as to why we use FindFirst instead of ==.
			if (fInterface.FindFirst(interfaceName) < 0)
				return;
			if (message->FindInt32("opcode") != B_NETWORK_WLAN_SCANNED)
				return;

			Lock();
			Quit();
		}

	private:
		BString fInterface;
	};

	ScanListener* listener = NULL;
	if (wait)
		listener = new ScanListener(Name());

	// Trigger the scan
	struct ieee80211_scan_req request;
	memset(&request, 0, sizeof(request));
	request.sr_flags = IEEE80211_IOC_SCAN_ACTIVE
		| IEEE80211_IOC_SCAN_BGSCAN
		| IEEE80211_IOC_SCAN_NOPICK
		| IEEE80211_IOC_SCAN_ONCE
		| (forceRescan ? IEEE80211_IOC_SCAN_FLUSH : 0);
	request.sr_duration = IEEE80211_IOC_SCAN_FOREVER;
	request.sr_nssid = 0;

	status_t status = set_80211(Name(), IEEE80211_IOC_SCAN_REQ, &request,
		sizeof(request));

	// If there are no VAPs running, the net80211 layer will return ENXIO.
	// Try to bring up the interface (which should start a VAP) and try again.
	if (status == ENXIO) {
		struct ieee80211req dummy;
		status = set_80211(Name(), IEEE80211_IOC_HAIKU_COMPAT_WLAN_UP, &dummy,
			sizeof(dummy));
		if (status != B_OK)
			return status;

		status = set_80211(Name(), IEEE80211_IOC_SCAN_REQ, &request,
			sizeof(request));
	}

	// If there is already a scan currently running, it's probably an "infinite"
	// one, which we of course don't want to wait for. So just return immediately
	// if that's the case.
	if (status == EINPROGRESS) {
		delete listener;
		return B_OK;
	}

	if (!wait || status != B_OK) {
		delete listener;
		return status;
	}

	while (wait_for_thread(listener->Run(), NULL) == B_INTERRUPTED)
		;
	return B_OK;
}


/** @brief Returns the list of Wi-Fi networks currently visible to this device.
 *  @param networks On success, set to a caller-owned new[] array.
 *  @param count    On success, the number of entries in @a networks.
 *  @return B_OK on success, or an error code. */
status_t
BNetworkDevice::GetNetworks(wireless_network*& networks, uint32& count)
{
	return get_scan_results(Name(), networks, count);
}


/** @brief Looks up a specific Wi-Fi network by SSID.
 *  @return B_OK on success, or B_ENTRY_NOT_FOUND if no such SSID was seen. */
status_t
BNetworkDevice::GetNetwork(const char* name, wireless_network& network)
{
	if (name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	return get_network(Name(), network, UINT32_MAX, NULL, name);
}


/** @brief Looks up a specific Wi-Fi network by BSSID (link-level address). */
status_t
BNetworkDevice::GetNetwork(const BNetworkAddress& address,
	wireless_network& network)
{
	if (address.Family() != AF_LINK)
		return B_BAD_VALUE;

	return get_network(Name(), network, UINT32_MAX, &address, NULL);
}


/** @brief Asks net_server to associate this device with @a name.
 *  @param name     SSID of the target network.
 *  @param password Passphrase for WPA/WPA2 networks (may be NULL). */
status_t
BNetworkDevice::JoinNetwork(const char* name, const char* password)
{
	if (name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	BMessage message(kMsgJoinNetwork);
	status_t status = message.AddString("device", Name());

	if (status == B_OK)
		status = message.AddString("name", name);
	if (status == B_OK && password != NULL)
		status = message.AddString("password", password);
	if (status != B_OK)
		return status;

	// Send message to the net_server

	BMessenger networkServer(kNetServerSignature);
	BMessage reply;
	status = networkServer.SendMessage(&message, &reply);
	if (status == B_OK)
		reply.FindInt32("status", &status);

	return status;
}


/** @brief Convenience overload joining a network described by a wireless_network. */
status_t
BNetworkDevice::JoinNetwork(const wireless_network& network,
	const char* password)
{
	return JoinNetwork(network.address, password);
}


/** @brief Joins the access point identified by its BSSID. */
status_t
BNetworkDevice::JoinNetwork(const BNetworkAddress& address,
	const char* password)
{
	if (address.InitCheck() != B_OK)
		return B_BAD_VALUE;

	BMessage message(kMsgJoinNetwork);
	status_t status = message.AddString("device", Name());

	if (status == B_OK) {
		status = message.AddFlat("address",
			const_cast<BNetworkAddress*>(&address));
	}
	if (status == B_OK && password != NULL)
		status = message.AddString("password", password);
	if (status != B_OK)
		return status;

	// Send message to the net_server

	BMessenger networkServer(kNetServerSignature);
	BMessage reply;
	status = networkServer.SendMessage(&message, &reply);
	if (status == B_OK)
		reply.FindInt32("status", &status);

	return status;
}


/** @brief Asks net_server to disassociate this device from SSID @a name. */
status_t
BNetworkDevice::LeaveNetwork(const char* name)
{
	BMessage message(kMsgLeaveNetwork);
	status_t status = message.AddString("device", Name());
	if (status == B_OK)
		status = message.AddString("name", name);
	if (status == B_OK)
		status = message.AddInt32("reason", IEEE80211_REASON_UNSPECIFIED);
	if (status != B_OK)
		return status;

	BMessenger networkServer(kNetServerSignature);
	BMessage reply;
	status = networkServer.SendMessage(&message, &reply);
	if (status == B_OK)
		reply.FindInt32("status", &status);

	return status;
}


/** @brief Convenience overload leaving a wireless_network by BSSID. */
status_t
BNetworkDevice::LeaveNetwork(const wireless_network& network)
{
	return LeaveNetwork(network.address);
}


/** @brief Asks net_server to disassociate this device from the given BSSID. */
status_t
BNetworkDevice::LeaveNetwork(const BNetworkAddress& address)
{
	BMessage message(kMsgLeaveNetwork);
	status_t status = message.AddString("device", Name());
	if (status == B_OK) {
		status = message.AddFlat("address",
			const_cast<BNetworkAddress*>(&address));
	}
	if (status == B_OK)
		status = message.AddInt32("reason", IEEE80211_REASON_UNSPECIFIED);
	if (status != B_OK)
		return status;

	BMessenger networkServer(kNetServerSignature);
	BMessage reply;
	status = networkServer.SendMessage(&message, &reply);
	if (status == B_OK)
		reply.FindInt32("status", &status);

	return status;
}


/** @brief Iterates over the set of Wi-Fi networks this device is currently
 *         associated with and returns detailed network info for each.
 *  @param cookie  In/out iterator cursor; start at 0.
 *  @param network On success, populated with the current network details.
 *  @return B_OK on success, B_ENTRY_NOT_FOUND when iteration is done. */
status_t
BNetworkDevice::GetNextAssociatedNetwork(uint32& cookie,
	wireless_network& network)
{
	BNetworkAddress address;
	status_t status = GetNextAssociatedNetwork(cookie, address);
	if (status != B_OK)
		return status;

	return GetNetwork(address, network);
}


/** @brief Iterates over associated-network BSSIDs.
 *  @note Currently only a single associated network is supported. */
status_t
BNetworkDevice::GetNextAssociatedNetwork(uint32& cookie,
	BNetworkAddress& address)
{
	// We currently support only a single associated network
	if (cookie != 0)
		return B_ENTRY_NOT_FOUND;

	uint8 mac[IEEE80211_ADDR_LEN];
	int32 length = IEEE80211_ADDR_LEN;
	status_t status = get_80211(Name(), IEEE80211_IOC_BSSID, mac, length);
	if (status != B_OK)
		return status;

	if (mac[0] == 0 && mac[1] == 0 && mac[2] == 0 && mac[3] == 0 && mac[4] == 0
			&& mac[5] == 0) {
		return B_ENTRY_NOT_FOUND;
	}

	address.SetToLinkLevel(mac, IEEE80211_ADDR_LEN);
	cookie++;
	return B_OK;
}
