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

/** @file LocalDevice.h
 *  @brief Represents the local Bluetooth adapter and provides control over its configuration and discovery. */

#ifndef _LOCAL_DEVICE_H
#define _LOCAL_DEVICE_H

#include <bluetooth/bluetooth.h>
#include <bluetooth/DeviceClass.h>
#include <bluetooth/BluetoothDevice.h>

#include <bluetooth/HCI/btHCI.h>

#include <Messenger.h>
#include <Message.h>

#include <String.h>


namespace Bluetooth {

class DiscoveryAgent;

/** @brief Represents the local Bluetooth hardware adapter; provides factory methods to obtain
 *         an instance, and control methods for name, class, discoverability, and authentication. */
class LocalDevice : public BluetoothDevice {

public:
	/** @brief Returns the default local Bluetooth device.
	 *  @return Pointer to the default LocalDevice, or NULL if none is available. */
	/* Possible throwing */
	static	LocalDevice*	GetLocalDevice();

	/** @brief Returns the number of local Bluetooth adapters present in the system.
	 *  @return Count of available local Bluetooth devices. */
	static	uint32			GetLocalDeviceCount();

	/** @brief Returns the local Bluetooth device with the given HCI identifier.
	 *  @param hid The HCI device identifier.
	 *  @return Pointer to the matching LocalDevice, or NULL if not found. */
	static	LocalDevice*	GetLocalDevice(const hci_id hid);

	/** @brief Returns the local Bluetooth device with the given Bluetooth address.
	 *  @param bdaddr The 48-bit Bluetooth device address to look up.
	 *  @return Pointer to the matching LocalDevice, or NULL if not found. */
	static	LocalDevice*	GetLocalDevice(const bdaddr_t bdaddr);

	/** @brief Returns the DiscoveryAgent associated with this local device.
	 *  @return Pointer to this device's DiscoveryAgent. */
			DiscoveryAgent*	GetDiscoveryAgent();

	/** @brief Returns the user-visible friendly name of this local device.
	 *  @return A BString containing the device name. */
			BString			GetFriendlyName();

	/** @brief Sets the user-visible friendly name of this local device.
	 *  @param name Reference to a BString containing the new name.
	 *  @return B_OK on success, or an error code on failure. */
			status_t		SetFriendlyName(BString& name);

	/** @brief Returns the Class of Device for this local device.
	 *  @return A DeviceClass encoding major class, minor class, and service class. */
			DeviceClass		GetDeviceClass();

	/** @brief Sets the Class of Device for this local device.
	 *  @param deviceClass The new DeviceClass to apply.
	 *  @return B_OK on success, or an error code on failure. */
			status_t		SetDeviceClass(DeviceClass deviceClass);

	/** @brief Sets the discoverability mode of this local device.
	 *  @param mode The discoverability mode (e.g., GIAC, LIAC, or not discoverable).
	 *  @return B_OK on success, or an error code on failure. */
	/* Possible throwing */
			status_t		SetDiscoverable(int mode);

	/** @brief Enables or disables link-level authentication for this local device.
	 *  @param authentication true to require authentication, false to disable it.
	 *  @return B_OK on success, or an error code on failure. */
			status_t		SetAuthentication(bool authentication);

	/** @brief Retrieves a named string property of the local device.
	 *  @param property Null-terminated property name.
	 *  @return A BString containing the property value. */
			BString			GetProperty(const char* property);

	/** @brief Retrieves a named numeric property of the local device.
	 *  @param property Null-terminated property name.
	 *  @param value    Pointer to a uint32 that receives the property value.
	 *  @return B_OK on success, or an error code if the property is unavailable. */
			status_t 		GetProperty(const char* property, uint32* value);

	/** @brief Returns the current discoverability mode of this local device.
	 *  @return The current discoverable mode value. */
			int				GetDiscoverable();

	/** @brief Returns the Bluetooth device address of this local adapter.
	 *  @return The 48-bit bdaddr_t of this device. */
			bdaddr_t		GetBluetoothAddress();
	/*
			ServiceRecord getRecord(Connection notifier);
			void updateRecord(ServiceRecord srvRecord);
	*/

	/** @brief Returns the HCI identifier for this local device.
	 *  @return The hci_id assigned to this adapter. */
			hci_id	ID() const;
private:
			LocalDevice(hci_id hid);
			virtual	~LocalDevice();

			status_t		_ReadLocalVersion();
			status_t		_ReadBufferSize();
			status_t		_ReadLocalFeatures();
			status_t		_ReadTimeouts();
			status_t		_ReadLinkKeys();
			status_t		Reset();

	static	LocalDevice*	RequestLocalDeviceID(BMessage* request);

			BMessenger*		fMessenger;
			hci_id			fHid;

	friend class DiscoveryAgent;
	friend class RemoteDevice;
	friend class PincodeWindow;

};

}


#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::LocalDevice;
#endif

#endif // _LOCAL_DEVICE_H
