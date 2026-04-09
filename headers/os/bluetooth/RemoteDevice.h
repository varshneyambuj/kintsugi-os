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
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file RemoteDevice.h
 *  @brief Represents a remote Bluetooth device discovered or connected via a local adapter. */

#ifndef _REMOTE_DEVICE_H
#define _REMOTE_DEVICE_H

#include <bluetooth/bluetooth.h>
#include <bluetooth/bluetooth_error.h>
#include <bluetooth/BluetoothDevice.h>

#include <String.h>

/** @brief Authentication/connection status: operation is pending. */
#define B_BT_WAIT 0x00
/** @brief Authentication/connection status: operation succeeded. */
#define B_BT_SUCCEEDED 0x01


namespace Bluetooth {

class Connection;
class LocalDevice;

/** @brief Represents a remote Bluetooth peer device, providing methods for name resolution,
 *         authentication, encryption queries, and property access. */
class RemoteDevice : public BluetoothDevice {

public:
	static const int WAIT = B_BT_WAIT;           /**< Status constant: operation pending. */
	static const int SUCCEEDED = B_BT_SUCCEEDED; /**< Status constant: operation succeeded. */

	/** @brief Destructor. */
	virtual ~RemoteDevice();

	/** @brief Returns whether this device is in the local trusted-device list.
	 *  @return true if the device is trusted, false otherwise. */
	bool IsTrustedDevice();

	/** @brief Returns the friendly name of this remote device, optionally querying the device.
	 *  @param alwaysAsk If true, always issue a remote name request; if false, use cache.
	 *  @return A BString containing the friendly name. */
	BString GetFriendlyName(bool alwaysAsk); /* Throwing */

	/** @brief Returns the friendly name of this remote device (uses cache if available).
	 *  @return A BString containing the friendly name. */
	BString GetFriendlyName(void); /* Throwing */

	/** @brief Returns the previously cached friendly name without querying the device.
	 *  @return A BString containing the last-known friendly name. */
	BString GetCachedFriendlyName();

	/** @brief Returns the Bluetooth device address of this remote device.
	 *  @return The 48-bit bdaddr_t of this remote device. */
	bdaddr_t GetBluetoothAddress();

	/** @brief Returns the Class of Device reported by this remote device.
	 *  @return A DeviceClass encoding major class, minor class, and service class. */
	DeviceClass GetDeviceClass();

	/** @brief Checks whether this remote device is the same as another.
	 *  @param obj Pointer to another RemoteDevice to compare against.
	 *  @return true if both objects represent the same physical device. */
	bool Equals(RemoteDevice* obj);

	/*static RemoteDevice* GetRemoteDevice(Connection conn);   Throwing */

	/** @brief Initiates link-level authentication with this remote device.
	 *  @return true if authentication succeeded, false otherwise. */
	bool		Authenticate(); /* Throwing */

	/** @brief Disconnects from this remote device with the specified reason code.
	 *  @param reason HCI disconnect reason code (default: BT_REMOTE_USER_ENDED_CONNECTION).
	 *  @return B_OK on success, or an error code on failure. */
	status_t	Disconnect(int8 reason = BT_REMOTE_USER_ENDED_CONNECTION);
	/* bool Authorize(Connection conn);  Throwing */
	/*bool Encrypt(Connection conn, bool on);  Throwing */

	/** @brief Returns whether the current connection with this device is authenticated.
	 *  @return true if authenticated, false otherwise. */
	bool IsAuthenticated(); /* Throwing */
	/*bool IsAuthorized(Connection conn);  Throwing */

	/** @brief Returns whether the current connection with this device is encrypted.
	 *  @return true if encrypted, false otherwise. */
	bool IsEncrypted(); /* Throwing */

	/** @brief Retrieves a named string property of this remote device.
	 *  @param property Null-terminated property name.
	 *  @return A BString containing the property value. */
	BString GetProperty(const char* property); /* Throwing */

	/** @brief Retrieves a named numeric property of this remote device.
	 *  @param property Null-terminated property name.
	 *  @param value    Pointer to a uint32 that receives the property value.
	 *  @return B_OK on success, or an error code if the property is unavailable. */
	status_t GetProperty(const char* property, uint32* value); /* Throwing */

	/** @brief Returns the LocalDevice through which this remote device was discovered.
	 *  @return Pointer to the owning LocalDevice, or NULL if not yet associated. */
	LocalDevice* GetLocalDeviceOwner();

	/** @brief Constructs a RemoteDevice from a string Bluetooth address.
	 *  @param address BString in "XX:XX:XX:XX:XX:XX" format. */
	RemoteDevice(const BString& address);

	/** @brief Constructs a RemoteDevice from a raw address and a 3-byte CoD record.
	 *  @param address The 48-bit Bluetooth address.
	 *  @param record  3-byte Class of Device array. */
	RemoteDevice(const bdaddr_t address, uint8 record[3]);

protected:
	/* called by Discovery[Listener|Agent] */
	/** @brief Associates this remote device with the local device that discovered it.
	 *  @param ld Pointer to the discovering LocalDevice. */
	void SetLocalDeviceOwner(LocalDevice* ld);
	friend class DiscoveryListener;

private:

	LocalDevice* fDiscovererLocalDevice;
	BMessenger*	 fMessenger;

	uint16		fHandle;
	uint8		fPageRepetitionMode;
	uint8		fScanPeriodMode;
	uint8		fScanMode;
	uint16		fClockOffset;
	int8        fRSSI;
	BString     fFriendlyName;
	bool        fFriendlyNameIsComplete;
};

}

#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::RemoteDevice;
#endif

#endif // _REMOTE_DEVICE_H
