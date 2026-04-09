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
 *   Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   Copyright 2008 Mika Lindqvist, monni1995_at_gmail.com
 *   Copyright 2012 Fredrik Modéen
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file LocalDevice.cpp
 * @brief Implementation of LocalDevice, the local Bluetooth adapter interface
 *
 * LocalDevice represents the host system's Bluetooth controller. It provides
 * methods to query and set adapter properties (name, address, class, scan
 * modes), retrieve a DiscoveryAgent for device scanning, and enumerate known
 * remote devices. It communicates with the Bluetooth server via the Kit
 * support layer.
 *
 * @see RemoteDevice, DiscoveryAgent, DeviceClass
 */


#include <bluetooth/bluetooth_error.h>

#include <bluetooth/HCI/btHCI_command.h>
#include <bluetooth/HCI/btHCI_event.h>

#include <bluetooth/DeviceClass.h>
#include <bluetooth/DiscoveryAgent.h>
#include <bluetooth/LocalDevice.h>
#include <bluetooth/RemoteDevice.h>

#include <bluetooth/bdaddrUtils.h>
#include <bluetoothserver_p.h>
#include <CommandManager.h>

#include <new>

#include "KitSupport.h"


namespace Bluetooth {


/**
 * @brief Request a LocalDevice handle from the Bluetooth server using a pre-built message.
 *
 * Sends \a request to the Bluetooth server and, on success, constructs a new
 * LocalDevice with the HCI device ID returned in the reply.  This is the
 * shared implementation used by all public GetLocalDevice() overloads.
 *
 * @param request A fully populated BMessage addressed to the Bluetooth server.
 * @return A heap-allocated LocalDevice on success, or NULL if the messenger
 *         cannot be obtained, the send fails, or the reply contains no valid
 *         HCI device ID.
 * @see GetLocalDevice()
 */
LocalDevice*
LocalDevice::RequestLocalDeviceID(BMessage* request)
{
	BMessage reply;
	hci_id hid;
	LocalDevice* lDevice = NULL;

	BMessenger* messenger = _RetrieveBluetoothMessenger();

	if (messenger == NULL)
		return NULL;

	if (messenger->SendMessage(request, &reply) == B_OK
		&& reply.FindInt32("hci_id", &hid) == B_OK ) {

		if (hid >= 0)
			lDevice = new (std::nothrow)LocalDevice(hid);
	}

	delete messenger;
	return lDevice;
}


#if 0
#pragma -
#endif


/**
 * @brief Obtain a LocalDevice representing any available local Bluetooth adapter.
 *
 * Asks the Bluetooth server to allocate a handle to any available adapter.
 *
 * @return A heap-allocated LocalDevice on success, or NULL if no adapter is
 *         available or the server cannot be reached.
 * @see GetLocalDevice(const hci_id), GetLocalDevice(const bdaddr_t)
 */
LocalDevice*
LocalDevice::GetLocalDevice()
{
	BMessage request(BT_MSG_ACQUIRE_LOCAL_DEVICE);

	return RequestLocalDeviceID(&request);
}


/**
 * @brief Obtain a LocalDevice for the adapter identified by a specific HCI device ID.
 *
 * @param hid The numeric HCI device identifier assigned by the Bluetooth server.
 * @return A heap-allocated LocalDevice on success, or NULL on failure.
 * @see GetLocalDevice(), GetLocalDevice(const bdaddr_t)
 */
LocalDevice*
LocalDevice::GetLocalDevice(const hci_id hid)
{
	BMessage request(BT_MSG_ACQUIRE_LOCAL_DEVICE);
	request.AddInt32("hci_id", hid);

	return RequestLocalDeviceID(&request);
}


/**
 * @brief Obtain a LocalDevice for the adapter whose Bluetooth address matches \a bdaddr.
 *
 * @param bdaddr The 48-bit Bluetooth device address of the desired local adapter.
 * @return A heap-allocated LocalDevice on success, or NULL on failure.
 * @see GetLocalDevice(), GetLocalDevice(const hci_id)
 */
LocalDevice*
LocalDevice::GetLocalDevice(const bdaddr_t bdaddr)
{
	BMessage request(BT_MSG_ACQUIRE_LOCAL_DEVICE);
	request.AddData("bdaddr", B_ANY_TYPE, &bdaddr, sizeof(bdaddr_t));

	return RequestLocalDeviceID(&request);
}


/**
 * @brief Return the number of local Bluetooth adapters visible to the server.
 *
 * @return The count of locally attached Bluetooth controllers, or 0 if the
 *         Bluetooth server cannot be contacted.
 */
uint32
LocalDevice::GetLocalDeviceCount()
{
	BMessenger* messenger = _RetrieveBluetoothMessenger();
	uint32 count = 0;

	if (messenger != NULL) {

		BMessage request(BT_MSG_COUNT_LOCAL_DEVICES);
		BMessage reply;

		if (messenger->SendMessage(&request, &reply) == B_OK)
			count = reply.FindInt32("count");

		delete messenger;
	}

	return count;

}


/**
 * @brief Create a DiscoveryAgent bound to this local adapter.
 *
 * Each call allocates a new DiscoveryAgent instance.  The caller is
 * responsible for deleting the returned object.
 *
 * @return A heap-allocated DiscoveryAgent, or NULL on allocation failure.
 * @see DiscoveryAgent
 */
DiscoveryAgent*
LocalDevice::GetDiscoveryAgent()
{
	// TODO: Study a singleton here
	return new (std::nothrow)DiscoveryAgent(this);
}


/**
 * @brief Retrieve a named string property from the local adapter.
 *
 * @param property The property key to query.
 * @return The property value as a BString, or an empty/null string if the
 *         property is not found.
 * @note This overload is currently a stub and always returns NULL.
 */
BString
LocalDevice::GetProperty(const char* property)
{
	return NULL;

}


/**
 * @brief Retrieve a named integer property from the local adapter.
 *
 * Sends a BT_MSG_GET_PROPERTY request to the Bluetooth server and stores the
 * result in \a value on success.
 *
 * @param property The property key to query (e.g. "manufacturer").
 * @param value    Output pointer that receives the 32-bit property value.
 * @return B_OK if the property was retrieved successfully, B_ERROR otherwise.
 */
status_t
LocalDevice::GetProperty(const char* property, uint32* value)
{
	if (fMessenger == NULL)
		return B_ERROR;

	BMessage request(BT_MSG_GET_PROPERTY);
	BMessage reply;

	request.AddInt32("hci_id", fHid);
	request.AddString("property", property);

	if (fMessenger->SendMessage(&request, &reply) == B_OK) {
		if (reply.FindInt32("result", (int32*)value ) == B_OK ) {
			return B_OK;

		}
	}

	return B_ERROR;
}


/**
 * @brief Query the current scan-enable state of the local adapter.
 *
 * Sends a ReadScanEnable HCI command to the controller and returns the raw
 * scan-enable byte from the controller's reply.
 *
 * @return The current scan-enable mode byte (see HCI spec), or -1 if the
 *         messenger is unavailable, memory cannot be allocated for the
 *         command, or the server does not reply successfully.
 * @see SetDiscoverable()
 */
int
LocalDevice::GetDiscoverable()
{
	if (fMessenger == NULL)
		return -1;

	size_t	size;
	void* command = buildReadScan(&size);
	if (command == NULL)
		return -1;

	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	request.AddInt32("hci_id", fHid);
	request.AddData("raw command", B_ANY_TYPE, command, size);
	request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
	request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_CONTROL_BASEBAND,
		OCF_READ_SCAN_ENABLE));

	int8 discoverable;
	BMessage reply;
	if (fMessenger->SendMessage(&request, &reply) == B_OK
		&& reply.FindInt8("scan_enable", &discoverable) == B_OK)
		return discoverable;

	return -1;
}


/**
 * @brief Set the scan-enable mode of the local adapter.
 *
 * Builds and sends a WriteScaEnable HCI command with the requested mode.
 * The scan-enable byte controls whether the adapter responds to inquiry
 * scans (discoverability) and page scans (connectability).
 *
 * @param mode The desired scan-enable byte value (e.g. BT_INQUIRY_SCAN_ENABLE
 *             or BT_INQUIRY_AND_PAGE_SCAN_ENABLE).
 * @return B_OK on success, B_NO_MEMORY if the command buffer cannot be
 *         allocated, or B_ERROR if the messenger is unavailable or the
 *         server returns an error.
 * @see GetDiscoverable()
 */
status_t
LocalDevice::SetDiscoverable(int mode)
{
	if (fMessenger == NULL)
		return B_ERROR;

	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage reply;

	size_t size;
	int8 bt_status = BT_ERROR;

	request.AddInt32("hci_id", fHid);


	void* command = buildWriteScan(mode, &size);

	if (command == NULL) {
		return B_NO_MEMORY;
	}

	request.AddData("raw command", B_ANY_TYPE, command, size);
	request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
	request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_CONTROL_BASEBAND,
		OCF_WRITE_SCAN_ENABLE));

	if (fMessenger->SendMessage(&request, &reply) == B_OK) {
		if (reply.FindInt8("status", &bt_status ) == B_OK ) {
			return bt_status;

		}
	}

	return B_ERROR;
}


/** @brief Internal parameter struct used to hold the authentication-enable byte for HCI. */
struct authentication_t {
	uint8 param;
};


/**
 * @brief Enable or disable link-level authentication on the local adapter.
 *
 * Sends a WriteAuthenticationEnable HCI command to the controller.
 *
 * @param authentication True to require authentication for new connections,
 *                       false to disable it.
 * @return B_OK on success, or an error code on failure.
 * @see SetDiscoverable()
 */
status_t
LocalDevice::SetAuthentication(bool authentication)
{
	return SingleParameterCommandRequest<struct authentication_t, uint8>
		(OGF_CONTROL_BASEBAND, OCF_WRITE_AUTH_ENABLE, authentication,
		NULL, fHid, fMessenger);
}


/**
 * @brief Read the Bluetooth device address of the local adapter from the controller.
 *
 * Sends a ReadBdAddr HCI informational-parameters command and returns the
 * 48-bit address from the controller's reply.
 *
 * @return The adapter's bdaddr_t on success, or the loopback/local address
 *         returned by bdaddrUtils::LocalAddress() if the messenger is
 *         unavailable, the command cannot be allocated, or the server fails.
 * @see bdaddrUtils::LocalAddress()
 */
bdaddr_t
LocalDevice::GetBluetoothAddress()
{
	if (fMessenger == NULL)
		return bdaddrUtils::LocalAddress();

	size_t	size;
	void* command = buildReadBdAddr(&size);

	if (command == NULL)
		return bdaddrUtils::LocalAddress();

	const bdaddr_t* bdaddr;
	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage reply;
	ssize_t	ssize;

	request.AddInt32("hci_id", fHid);
	request.AddData("raw command", B_ANY_TYPE, command, size);
	request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
	request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_INFORMATIONAL_PARAM,
		OCF_READ_BD_ADDR));

	if (fMessenger->SendMessage(&request, &reply) == B_OK
		&& reply.FindData("bdaddr", B_ANY_TYPE, 0,
			(const void**)&bdaddr, &ssize) == B_OK)
			return *bdaddr;

	return bdaddrUtils::LocalAddress();
}


/**
 * @brief Return the HCI device ID associated with this LocalDevice.
 *
 * @return The hci_id value assigned by the Bluetooth server at construction.
 */
hci_id
LocalDevice::ID(void) const
{
	return fHid;
}


/**
 * @brief Read the human-readable name of the local adapter from the controller.
 *
 * Sends a ReadLocalName HCI control-baseband command and returns the
 * UTF-8 name string from the controller's reply.
 *
 * @return The adapter's friendly name on success.  Returns a descriptive
 *         error string ("Unknown|Messenger", "Unknown|NoMemory", or
 *         "Unknown|ServerFailed") when the operation cannot complete.
 * @see SetFriendlyName()
 */
BString
LocalDevice::GetFriendlyName()
{
	if (fMessenger == NULL)
		return BString("Unknown|Messenger");

	size_t	size;
	void* command = buildReadLocalName(&size);
	if (command == NULL)
		return BString("Unknown|NoMemory");

	BString friendlyname;
	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage reply;


	request.AddInt32("hci_id", fHid);
	request.AddData("raw command", B_ANY_TYPE, command, size);
	request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
	request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_CONTROL_BASEBAND,
		OCF_READ_LOCAL_NAME));

	if (fMessenger->SendMessage(&request, &reply) == B_OK
		&& reply.FindString("friendlyname", &friendlyname) == B_OK)
		return friendlyname;

	return BString("Unknown|ServerFailed");
}


/**
 * @brief Write a new human-readable name to the local adapter.
 *
 * Builds a WriteLocalName HCI command with \a name and sends it to the
 * controller via the Bluetooth server.
 *
 * @param name The desired friendly name (up to 248 bytes per the HCI spec).
 * @return B_OK on success, BT_ERROR if the messenger is unavailable or the
 *         server returns an error.
 * @see GetFriendlyName()
 */
status_t
LocalDevice::SetFriendlyName(BString& name)
{
	int8 btStatus = BT_ERROR;

	if (fMessenger == NULL)
		return btStatus;

	BluetoothCommand<typed_command(hci_write_local_name)>
		writeName(OGF_CONTROL_BASEBAND, OCF_WRITE_LOCAL_NAME);

	strcpy(writeName->local_name, name.String());

	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage reply;

	request.AddInt32("hci_id", fHid);
	request.AddData("raw command", B_ANY_TYPE,
		writeName.Data(), writeName.Size());
	request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
	request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_CONTROL_BASEBAND,
		OCF_WRITE_LOCAL_NAME));

	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		reply.FindInt8("status", &btStatus);

	return btStatus;
}


/**
 * @brief Read the device class of the local adapter from the controller.
 *
 * Sends a ReadClassOfDevice HCI command and updates the cached fDeviceClass
 * member before returning it.
 *
 * @return The current DeviceClass of the adapter.  Returns the cached
 *         (possibly uninitialized) value if the messenger is unavailable or
 *         the command cannot be built.
 * @see SetDeviceClass(), DeviceClass
 */
DeviceClass
LocalDevice::GetDeviceClass()
{

//	if (fDeviceClass.IsUnknownDeviceClass()) {

		if (fMessenger == NULL)
			return fDeviceClass;

		size_t	size;
		void* command = buildReadClassOfDevice(&size);
		if (command == NULL)
			return fDeviceClass;

		BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
		BMessage reply;
		const uint8* bufferRecord;
		ssize_t	ssize;

		request.AddInt32("hci_id", fHid);
		request.AddData("raw command", B_ANY_TYPE, command, size);
		request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
		request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_CONTROL_BASEBAND,
			OCF_READ_CLASS_OF_DEV));

		if (fMessenger->SendMessage(&request, &reply) == B_OK
			&& reply.FindData("devclass", B_ANY_TYPE, 0, (const void**)&bufferRecord,
			&ssize) == B_OK) {
			uint8 record[3] = { bufferRecord[0], bufferRecord[1], bufferRecord[2] };
			fDeviceClass.SetRecord(record);
		}
//	}

	return fDeviceClass;

}


/**
 * @brief Write the device class of the local adapter to the controller.
 *
 * Builds a WriteClassOfDevice HCI command from \a deviceClass and sends it
 * to the controller via the Bluetooth server.
 *
 * @param deviceClass The DeviceClass value to assign to the local adapter.
 * @return B_OK on success, BT_ERROR if the messenger is unavailable or the
 *         server returns an error.
 * @see GetDeviceClass(), DeviceClass
 */
status_t
LocalDevice::SetDeviceClass(DeviceClass deviceClass)
{
	int8 bt_status = BT_ERROR;

	if (fMessenger == NULL)
		return bt_status;

	BluetoothCommand<typed_command(hci_write_dev_class)>
		setDeviceClass(OGF_CONTROL_BASEBAND, OCF_WRITE_CLASS_OF_DEV);

	setDeviceClass->dev_class[0] = deviceClass.Record() & 0xFF;
	setDeviceClass->dev_class[1] = (deviceClass.Record() & 0xFF00) >> 8;
	setDeviceClass->dev_class[2] = (deviceClass.Record() & 0xFF0000) >> 16;

	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage reply;

	request.AddInt32("hci_id", fHid);
	request.AddData("raw command", B_ANY_TYPE,
		setDeviceClass.Data(), setDeviceClass.Size());
	request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
	request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_CONTROL_BASEBAND,
		OCF_WRITE_CLASS_OF_DEV));

	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		reply.FindInt8("status", &bt_status);

	return bt_status;

}


/**
 * @brief Read the local HCI and LMP version information from the controller.
 *
 * Sends a ReadLocalVersionInformation HCI informational-parameters command.
 * The reply is stored server-side and is not returned directly to the caller.
 *
 * @return B_OK if the command was acknowledged, or BT_ERROR on failure.
 * @note Called automatically during LocalDevice construction.
 */
status_t
LocalDevice::_ReadLocalVersion()
{
	int8 bt_status = BT_ERROR;

	BluetoothCommand<> localVersion(OGF_INFORMATIONAL_PARAM,
		OCF_READ_LOCAL_VERSION);

	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage reply;

	request.AddInt32("hci_id", fHid);
	request.AddData("raw command", B_ANY_TYPE,
		localVersion.Data(), localVersion.Size());
	request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
	request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_INFORMATIONAL_PARAM,
		OCF_READ_LOCAL_VERSION));

	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		reply.FindInt8("status", &bt_status);

	return bt_status;
}


/**
 * @brief Read ACL and SCO buffer size information from the controller.
 *
 * Sends a ReadBufferSize HCI informational-parameters command. The reply
 * is stored server-side and is not returned directly to the caller.
 *
 * @return B_OK if the command was acknowledged, or BT_ERROR on failure.
 * @note Called automatically during LocalDevice construction.
 */
status_t
LocalDevice::_ReadBufferSize()
{
	int8 bt_status = BT_ERROR;

	BluetoothCommand<> BufferSize(OGF_INFORMATIONAL_PARAM,
		OCF_READ_BUFFER_SIZE);


	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage reply;

	request.AddInt32("hci_id", fHid);
	request.AddData("raw command", B_ANY_TYPE,
		BufferSize.Data(), BufferSize.Size());
	request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
	request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_INFORMATIONAL_PARAM,
		OCF_READ_BUFFER_SIZE));

	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		reply.FindInt8("status", &bt_status);

	return bt_status;
}


/**
 * @brief Read the set of LMP features supported by the local controller.
 *
 * Sends a ReadLocalSupportedFeatures HCI informational-parameters command.
 * The reply is stored server-side and is not returned directly to the caller.
 *
 * @return B_OK if the command was acknowledged, or BT_ERROR on failure.
 * @note Called automatically during LocalDevice construction.
 */
status_t
LocalDevice::_ReadLocalFeatures()
{
	int8 bt_status = BT_ERROR;

	BluetoothCommand<> LocalFeatures(OGF_INFORMATIONAL_PARAM,
		OCF_READ_LOCAL_FEATURES);

	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage reply;

	request.AddInt32("hci_id", fHid);
	request.AddData("raw command", B_ANY_TYPE,
		LocalFeatures.Data(), LocalFeatures.Size());
	request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
	request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_INFORMATIONAL_PARAM,
		OCF_READ_LOCAL_FEATURES));

	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		reply.FindInt8("status", &bt_status);

	return bt_status;
}


/**
 * @brief Read the stored link keys from the controller's non-volatile memory.
 *
 * Sends a ReadStoredLinkKey HCI control-baseband command and waits for both
 * the CommandComplete and ReturnLinkKeys events from the controller.
 *
 * @return B_OK if the command completed successfully, or BT_ERROR on failure.
 * @note Called automatically during LocalDevice construction.
 */
status_t
LocalDevice::_ReadLinkKeys()
{
	int8 bt_status = BT_ERROR;

	BluetoothCommand<> LocalFeatures(OGF_CONTROL_BASEBAND,
		OCF_READ_STORED_LINK_KEY);

	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage reply;

	request.AddInt32("hci_id", fHid);
	request.AddData("raw command", B_ANY_TYPE,
		LocalFeatures.Data(), LocalFeatures.Size());
	request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
	request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_CONTROL_BASEBAND,
		OCF_READ_STORED_LINK_KEY));

	request.AddInt16("eventExpected",  HCI_EVENT_RETURN_LINK_KEYS);


	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		reply.FindInt8("status", &bt_status);

	return bt_status;
}


/** @brief Internal parameter struct used to hold the 16-bit page-timeout value for HCI. */
struct pageTimeout_t {
	uint16 param;
};


/**
 * @brief Read and configure the page and inquiry timeouts on the local controller.
 *
 * Reads the current page timeout, then writes the fixed page timeout
 * (0x8000 slots) and connection-accept timeout (0x7d00 slots) to the
 * controller.
 *
 * @return The status code from the final WriteConnectionAcceptTimeout command,
 *         or an error code if that command fails.
 * @note Called automatically during LocalDevice construction.
 */
status_t
LocalDevice::_ReadTimeouts()
{

	// Read PageTimeout
	NonParameterCommandRequest(OGF_CONTROL_BASEBAND,
		OCF_READ_PG_TIMEOUT, NULL, fHid, fMessenger);

	// Write PageTimeout
	SingleParameterCommandRequest<struct pageTimeout_t, uint16>
		(OGF_CONTROL_BASEBAND, OCF_WRITE_PG_TIMEOUT, 0x8000, NULL,
		fHid, fMessenger);

	// Write PageTimeout
	return SingleParameterCommandRequest<struct pageTimeout_t, uint16>
		(OGF_CONTROL_BASEBAND, OCF_WRITE_CA_TIMEOUT, 0x7d00, NULL,
		fHid, fMessenger);
}


/**
 * @brief Send a soft Reset HCI command to the local controller.
 *
 * Resets the Bluetooth controller to its default state.  All active
 * connections are dropped and all controller state is cleared.
 *
 * @return B_OK on success, or BT_ERROR if the messenger is unavailable or
 *         the controller returns an error.
 * @note This is a destructive operation — all active connections will be
 *       terminated immediately.
 */
status_t
LocalDevice::Reset()
{
	int8 bt_status = BT_ERROR;

	BluetoothCommand<> Reset(OGF_CONTROL_BASEBAND, OCF_RESET);

	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage reply;

	request.AddInt32("hci_id", fHid);
	request.AddData("raw command", B_ANY_TYPE, Reset.Data(), Reset.Size());
	request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
	request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_CONTROL_BASEBAND,
		OCF_RESET));

	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		reply.FindInt8("status", &bt_status);

	return bt_status;

}


/*
ServiceRecord
LocalDevice::getRecord(Connection notifier) {

}

void
LocalDevice::updateRecord(ServiceRecord srvRecord) {

}
*/


/**
 * @brief Construct a LocalDevice for the adapter identified by \a hid.
 *
 * Establishes a messenger to the Bluetooth server, then initialises the
 * controller by reading its buffer sizes, supported features, version,
 * link-layer timeouts, and stored link keys.  If the adapter's manufacturer
 * ID indicates a Broadcom chipset (manufacturer 15), optional vendor-specific
 * workarounds (reset, bdaddr override) can be enabled at compile time via the
 * BT_WRITE_BDADDR_FOR_BCM2035 define.
 *
 * @param hid The numeric HCI device identifier returned by the Bluetooth server.
 * @note Instances should only be created via the static GetLocalDevice()
 *       factory methods, not directly.
 * @see GetLocalDevice(), ~LocalDevice()
 */
LocalDevice::LocalDevice(hci_id hid)
	:
	BluetoothDevice(),
	fHid(hid)
{
	fMessenger = _RetrieveBluetoothMessenger();

	_ReadBufferSize();
	_ReadLocalFeatures();
	_ReadLocalVersion();
	_ReadTimeouts();
	_ReadLinkKeys();

	// Uncomment this if you want your device to have a nicer default name
	// BString name("HaikuBluetooth");
	// SetFriendlyName(name);


	uint32 value;

	// HARDCODE -> move this to addons
	if (GetProperty("manufacturer", &value) == B_OK
		&& value == 15) {

		// Uncomment this out if your Broadcom dongle is not working properly
		// Reset();	// Perform a reset to Broadcom buggyland

// Uncomment this out if your Broadcom dongle has a null bdaddr
//#define BT_WRITE_BDADDR_FOR_BCM2035
#ifdef BT_WRITE_BDADDR_FOR_BCM2035
#warning Writting broadcom bdaddr @ init.
		// try write bdaddr to a bcm2035 -> will be moved to an addon
		int8 bt_status = BT_ERROR;

		BluetoothCommand<typed_command(hci_write_bcm2035_bdaddr)>
			writeAddress(OGF_VENDOR_CMD, OCF_WRITE_BCM2035_BDADDR);

		BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
		BMessage reply;
		writeAddress->bdaddr.b[0] = 0x3C;
		writeAddress->bdaddr.b[1] = 0x19;
		writeAddress->bdaddr.b[2] = 0x30;
		writeAddress->bdaddr.b[3] = 0xC9;
		writeAddress->bdaddr.b[4] = 0x03;
		writeAddress->bdaddr.b[5] = 0x00;

		request.AddInt32("hci_id", fHid);
		request.AddData("raw command", B_ANY_TYPE,
			writeAddress.Data(), writeAddress.Size());
		request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
		request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_VENDOR_CMD,
			OCF_WRITE_BCM2035_BDADDR));

		if (fMessenger->SendMessage(&request, &reply) == B_OK)
			reply.FindInt8("status", &bt_status);
#endif
	}
}


/**
 * @brief Destroy the LocalDevice and release its server messenger.
 *
 * Deletes the BMessenger used to communicate with the Bluetooth server.
 * Any subsequent method calls on this object will have undefined behaviour.
 *
 * @see LocalDevice(hci_id)
 */
LocalDevice::~LocalDevice()
{
	delete fMessenger;
}


} /* end namespace Bluetooth */
