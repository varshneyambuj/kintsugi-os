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
 *   Copyright 2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   Copyright 2008 Mika Lindqvist, monni1995_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file RemoteDevice.cpp
 * @brief Implementation of RemoteDevice, a discovered Bluetooth peer device
 *
 * RemoteDevice represents a remote Bluetooth device found during inquiry or
 * retrieved from the known-devices cache. It stores the device's Bluetooth
 * address, name, and class, and provides methods to authenticate, encrypt,
 * and retrieve service records for the device.
 *
 * @see LocalDevice, DiscoveryAgent, DeviceClass
 */


#include <bluetooth/DeviceClass.h>
#include <bluetooth/DiscoveryAgent.h>
#include <bluetooth/DiscoveryListener.h>
#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/LocalDevice.h>
#include <bluetooth/RemoteDevice.h>

#include <bluetooth/HCI/btHCI_command.h>
#include <bluetooth/HCI/btHCI_event.h>

#include <bluetooth/debug.h>
#include <bluetooth/bluetooth_error.h>

#include <Catalog.h>
#include <CommandManager.h>
#include <Locale.h>
#include <bluetoothserver_p.h>

#include "KitSupport.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "RemoteDevice"


namespace Bluetooth {


// TODO: Check headers for valid/reserved ranges
/** @brief Sentinel connection handle value used to indicate no active ACL connection. */
static const uint16 invalidConnectionHandle = 0xF000;


/**
 * @brief Return whether this device is marked as trusted.
 *
 * @return Always returns true in the current stub implementation.
 * @note This method is not yet fully implemented and unconditionally returns
 *       true regardless of any stored trust state.
 */
bool
RemoteDevice::IsTrustedDevice(void)
{
	CALLED();
	return true;
}


/**
 * @brief Retrieve the human-readable friendly name of the remote device.
 *
 * Sends an HCI Remote Name Request command to the Bluetooth server and waits
 * for the Remote Name Request Complete event. If @a alwaysAsk is false and a
 * complete cached name is already available, the cached name is returned
 * immediately without issuing a new HCI command.
 *
 * @param alwaysAsk If true, always issue a fresh HCI name request even if a
 *                  cached name is available. If false, return the cached name
 *                  when it is already complete.
 * @return The friendly name string on success, or a localised error sentinel
 *         string (prefixed with '#') if the owner device, server messenger,
 *         or HCI command is unavailable or fails.
 * @see GetCachedFriendlyName()
 */
BString
RemoteDevice::GetFriendlyName(bool alwaysAsk)
{
	CALLED();
	if (!alwaysAsk) {
		if (!fFriendlyName.IsEmpty() && fFriendlyNameIsComplete)
			return fFriendlyName;
	}

	if (fDiscovererLocalDevice == NULL)
		return BString(B_TRANSLATE("#NoOwnerError#Not Valid name"));

	if (fMessenger == NULL)
		return BString(B_TRANSLATE("#ServerNotReady#Not Valid name"));

	void* remoteNameCommand = NULL;
	size_t size;

	// Issue inquiry command
	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage reply;

	request.AddInt32("hci_id", fDiscovererLocalDevice->ID());

	// Fill the request
	remoteNameCommand = buildRemoteNameRequest(fBdaddr, fPageRepetitionMode,
		fClockOffset, &size);

	request.AddData("raw command", B_ANY_TYPE, remoteNameCommand, size);

	request.AddInt16("eventExpected",  HCI_EVENT_CMD_STATUS);
	request.AddInt16("opcodeExpected",
		PACK_OPCODE(OGF_LINK_CONTROL, OCF_REMOTE_NAME_REQUEST));

	request.AddInt16("eventExpected",  HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE);


	if (fMessenger->SendMessage(&request, &reply) == B_OK) {
		BString name;
		int8 status;

		if ((reply.FindInt8("status", &status) == B_OK) && (status == BT_OK)) {

			if ((reply.FindString("friendlyname", &name) == B_OK )) {
				fFriendlyName = name;
				fFriendlyNameIsComplete = true;
				return name;
			} else {
				return BString(""); // should not happen
			}

		} else {
			// seems we got a negative event
			if (!fFriendlyName.IsEmpty())
				return fFriendlyName;

			return BString(B_TRANSLATE("#CommandFailed#Not Valid name"));
		}
	}

	return BString(B_TRANSLATE("#NotCompletedRequest#Not Valid name"));
}


/**
 * @brief Retrieve the friendly name, using the cache when available.
 *
 * Convenience overload that calls GetFriendlyName(false), returning the
 * locally cached name if it is already complete, or issuing an HCI name
 * request otherwise.
 *
 * @return The friendly name string, or a localised error sentinel on failure.
 * @see GetFriendlyName(bool)
 */
BString
RemoteDevice::GetFriendlyName()
{
	CALLED();
	return GetFriendlyName(false);
}


/**
 * @brief Return the locally cached friendly name without issuing any HCI request.
 *
 * @return The cached friendly name string, which may be empty if the name has
 *         not yet been retrieved.
 * @see GetFriendlyName()
 */
BString
RemoteDevice::GetCachedFriendlyName()
{
	CALLED();
	return fFriendlyName;
}


/**
 * @brief Return the Bluetooth device address of this remote device.
 *
 * @return The 48-bit Bluetooth address (bdaddr_t) stored at construction time.
 */
bdaddr_t
RemoteDevice::GetBluetoothAddress()
{
	CALLED();
	return fBdaddr;
}


/**
 * @brief Compare this device's address to another RemoteDevice instance.
 *
 * @param obj Pointer to the other RemoteDevice to compare against.
 * @return true if both devices share the same Bluetooth address, false otherwise.
 * @see bdaddrUtils::Compare()
 */
bool
RemoteDevice::Equals(RemoteDevice* obj)
{
	CALLED();
	return bdaddrUtils::Compare(fBdaddr, obj->GetBluetoothAddress());
}


//  static RemoteDevice* GetRemoteDevice(Connection conn);


/**
 * @brief Establish an ACL connection and authenticate with the remote device.
 *
 * Sends an HCI Create Connection command followed by an HCI Authentication
 * Requested command via the Bluetooth server. On success the ACL connection
 * handle is stored in fHandle for subsequent operations.
 *
 * @return true if both the ACL connection and authentication complete with
 *         BT_OK status, false if the local device or server messenger is
 *         unavailable, or if either HCI command returns a non-OK status.
 * @note The role-switch capability and preferred packet type are read from the
 *       owning LocalDevice's properties before building the connection command.
 * @see Disconnect()
 * @see IsAuthenticated()
 */
bool
RemoteDevice::Authenticate()
{
	CALLED();
	int8 btStatus = BT_ERROR;

	if (fMessenger == NULL || fDiscovererLocalDevice == NULL)
		return false;

	BluetoothCommand<typed_command(hci_cp_create_conn)>
		createConnection(OGF_LINK_CONTROL, OCF_CREATE_CONN);

	bdaddrUtils::Copy(createConnection->bdaddr, fBdaddr);
	createConnection->pscan_rep_mode = fPageRepetitionMode;
	createConnection->pscan_mode = fScanMode; // Reserved in spec 2.1
	createConnection->clock_offset = fClockOffset | 0x8000; // substract!

	uint32 roleSwitch;
	fDiscovererLocalDevice->GetProperty("role_switch_capable", &roleSwitch);
	createConnection->role_switch = (uint8)roleSwitch;

	uint32 packetType;
	fDiscovererLocalDevice->GetProperty("packet_type", &packetType);
	createConnection->pkt_type = (uint16)packetType;

	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage reply;

	request.AddInt32("hci_id", fDiscovererLocalDevice->ID());
	request.AddData("raw command", B_ANY_TYPE,
		createConnection.Data(), createConnection.Size());

	// First we get the status about the starting of the connection
	request.AddInt16("eventExpected",  HCI_EVENT_CMD_STATUS);
	request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_LINK_CONTROL,
		OCF_CREATE_CONN));

	request.AddInt16("eventExpected", HCI_EVENT_LINK_KEY_REQ);
	request.AddInt16("eventExpected", HCI_EVENT_ROLE_CHANGE);
	request.AddInt16("eventExpected", HCI_EVENT_CONN_COMPLETE);

	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		reply.FindInt8("status", &btStatus);

	if (btStatus != BT_OK)
		return false;

	reply.FindInt16("handle", (int16*)&fHandle);

	BluetoothCommand<typed_command(hci_cp_auth_requested)> authRequest(OGF_LINK_CONTROL,
		OCF_AUTH_REQUESTED);

	authRequest->handle = fHandle;

	BMessage authRequestMsg(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage authReply;

	authRequestMsg.AddInt32("hci_id", fDiscovererLocalDevice->ID());
	authRequestMsg.AddData("raw command", B_ANY_TYPE, authRequest.Data(), authRequest.Size());

	authRequestMsg.AddInt16("eventExpected", HCI_EVENT_CMD_STATUS);
	authRequestMsg.AddInt16("opcodeExpected", PACK_OPCODE(OGF_LINK_CONTROL, OCF_AUTH_REQUESTED));

	authRequestMsg.AddInt16("eventExpected", HCI_EVENT_AUTH_COMPLETE);

	int8 authStatus = BT_ERROR;
	if (fMessenger->SendMessage(&authRequestMsg, &authReply) == B_OK)
		authReply.FindInt8("status", &authStatus);

	if (authStatus == BT_OK)
		return true;
	else
		return false;
}


/**
 * @brief Disconnect the active ACL link to the remote device.
 *
 * Sends an HCI Disconnect command with the supplied reason code. On a
 * successful Disconnection Complete event the stored connection handle is
 * reset to invalidConnectionHandle.
 *
 * @param reason HCI disconnect reason code to send to the controller.
 * @return The HCI status byte returned by the Disconnection Complete event on
 *         success, or B_ERROR if no active connection handle is held.
 * @see Authenticate()
 */
status_t
RemoteDevice::Disconnect(int8 reason)
{
	CALLED();
	if (fHandle != invalidConnectionHandle) {

		int8 btStatus = BT_ERROR;

		if (fMessenger == NULL || fDiscovererLocalDevice == NULL)
			return false;

		BluetoothCommand<typed_command(struct hci_disconnect)>
			disconnect(OGF_LINK_CONTROL, OCF_DISCONNECT);

		disconnect->reason = reason;
		disconnect->handle = fHandle;

		BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
		BMessage reply;


		request.AddInt32("hci_id", fDiscovererLocalDevice->ID());
		request.AddData("raw command", B_ANY_TYPE,
			disconnect.Data(), disconnect.Size());

		request.AddInt16("eventExpected",  HCI_EVENT_CMD_STATUS);
		request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_LINK_CONTROL,
			OCF_DISCONNECT));

		request.AddInt16("eventExpected",  HCI_EVENT_DISCONNECTION_COMPLETE);

		if (fMessenger->SendMessage(&request, &reply) == B_OK)
			reply.FindInt8("status", &btStatus);

		if (btStatus == BT_OK)
			fHandle = invalidConnectionHandle;

		return btStatus;

	}

	return B_ERROR;
}


//  bool Authorize(Connection conn);
//  bool Encrypt(Connection conn, bool on);


/**
 * @brief Return whether the current ACL link to the remote device is authenticated.
 *
 * @return Always returns true in the current stub implementation.
 * @note This method is not yet fully implemented.
 */
bool
RemoteDevice::IsAuthenticated()
{
	CALLED();
	return true;
}


//  bool IsAuthorized(Connection conn);


/**
 * @brief Return whether the current ACL link to the remote device is encrypted.
 *
 * @return Always returns true in the current stub implementation.
 * @note This method is not yet fully implemented.
 */
bool
RemoteDevice::IsEncrypted()
{
	CALLED();
	return true;
}


/**
 * @brief Return the LocalDevice that discovered or owns this RemoteDevice.
 *
 * @return Pointer to the owning LocalDevice, or NULL if none has been assigned.
 * @see SetLocalDeviceOwner()
 */
LocalDevice*
RemoteDevice::GetLocalDeviceOwner()
{
	CALLED();
	return fDiscovererLocalDevice;
}


/* Private */
/**
 * @brief Set the LocalDevice that owns or manages this RemoteDevice.
 *
 * Called internally by the discovery subsystem to bind a remote device to the
 * local adapter that discovered it.
 *
 * @param ld Pointer to the LocalDevice to associate with this remote device.
 * @see GetLocalDeviceOwner()
 */
void
RemoteDevice::SetLocalDeviceOwner(LocalDevice* ld)
{
	CALLED();
	fDiscovererLocalDevice = ld;
}


/* Constructor */
/**
 * @brief Construct a RemoteDevice from a raw Bluetooth address and class record.
 *
 * Initialises the device with the given 48-bit address and decodes the
 * three-byte Class of Device record. Connects to the Bluetooth server by
 * retrieving a messenger via _RetrieveBluetoothMessenger().
 *
 * @param address The 48-bit Bluetooth device address.
 * @param record  Three-byte Class of Device (CoD) payload as received in the
 *                inquiry result event.
 * @see RemoteDevice(const BString&)
 * @see DeviceClass::SetRecord()
 */
RemoteDevice::RemoteDevice(const bdaddr_t address, uint8 record[3])
	:
	BluetoothDevice(),
	fDiscovererLocalDevice(NULL),
	fHandle(invalidConnectionHandle)
{
	CALLED();
	fBdaddr = address;
	fDeviceClass.SetRecord(record);
	fMessenger = _RetrieveBluetoothMessenger();
}


/**
 * @brief Construct a RemoteDevice from a human-readable address string.
 *
 * Parses the colon-separated Bluetooth address string and initialises the
 * device. No Class of Device record is available via this constructor.
 *
 * @param address A string in the standard "XX:XX:XX:XX:XX:XX" Bluetooth
 *                address notation.
 * @see RemoteDevice(const bdaddr_t, uint8[3])
 * @see bdaddrUtils::FromString()
 */
RemoteDevice::RemoteDevice(const BString& address)
	:
	BluetoothDevice(),
	fDiscovererLocalDevice(NULL),
	fHandle(invalidConnectionHandle)
{
	CALLED();
	fBdaddr = bdaddrUtils::FromString((const char*)address.String());
	fMessenger = _RetrieveBluetoothMessenger();
}


/**
 * @brief Destroy the RemoteDevice and release all held resources.
 *
 * Deletes the server messenger obtained during construction. Any active ACL
 * connection must be closed via Disconnect() before the object is destroyed.
 */
RemoteDevice::~RemoteDevice()
{
	CALLED();
	delete fMessenger;
}


/**
 * @brief Retrieve a named string property from the remote device.
 *
 * @param property The name of the property to retrieve.
 * @return A BString containing the property value, or NULL if not implemented.
 * @note This method is not yet implemented and always returns NULL.
 */
BString
RemoteDevice::GetProperty(const char* property) /* Throwing */
{
	return NULL;
}


/**
 * @brief Retrieve a named integer property from the remote device.
 *
 * @param property The name of the property to retrieve.
 * @param value    Pointer to a uint32 that will receive the property value.
 * @return B_ERROR unconditionally; this method is not yet implemented.
 * @note This method is a stub and always returns B_ERROR.
 */
status_t
RemoteDevice::GetProperty(const char* property, uint32* value) /* Throwing */
{
	CALLED();
	return B_ERROR;
}


/**
 * @brief Return the Bluetooth Class of Device descriptor for this remote device.
 *
 * @return A DeviceClass value object encoding the device's major/minor class
 *         and supported service classes.
 * @see DeviceClass
 */
DeviceClass
RemoteDevice::GetDeviceClass()
{
	CALLED();
	return fDeviceClass;
}

} /* end namespace Bluetooth */
