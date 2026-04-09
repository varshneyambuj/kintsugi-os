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
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file BluetoothDevice.h
 *  @brief Abstract base class representing a generic Bluetooth device. */

#ifndef _BLUETOOTH_DEVICE_H
#define _BLUETOOTH_DEVICE_H

#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/DeviceClass.h>

#include <Message.h>
#include <Messenger.h>
#include <String.h>


namespace Bluetooth {

/** @brief Abstract base class for both local and remote Bluetooth devices,
 *         providing common identity and property access interfaces. */
class BluetoothDevice {

public:
	BluetoothDevice()
		:
		fBdaddr(bdaddrUtils::NullAddress()),
		fDeviceClass()
	{

	}

	/** @brief Returns the human-readable friendly name of the device.
	 *  @return A BString containing the friendly (user-visible) name. */
	virtual BString 	GetFriendlyName()=0;

	/** @brief Returns the Class of Device record for this device.
	 *  @return A DeviceClass object encoding major class, minor class, and service class. */
	virtual DeviceClass GetDeviceClass()=0;

	/** @brief Retrieves a named string property of the device.
	 *  @param property Null-terminated name of the property to read.
	 *  @return A BString containing the property value, or an empty string if not found. */
	virtual BString 	GetProperty(const char* property)=0;

	/** @brief Retrieves a named numeric property of the device.
	 *  @param property Null-terminated name of the property to read.
	 *  @param value    Pointer to a uint32 that receives the property value.
	 *  @return B_OK on success, or an error code if the property is unavailable. */
	virtual status_t 	GetProperty(const char* property, uint32* value)=0;

	/** @brief Returns the Bluetooth device address (BD_ADDR) of this device.
	 *  @return The 48-bit bdaddr_t uniquely identifying this device. */
	virtual bdaddr_t 	GetBluetoothAddress()=0;

protected:
	bdaddr_t 			fBdaddr;      /**< Cached Bluetooth device address. */
	DeviceClass 		fDeviceClass; /**< Cached Class of Device record. */
};

}


#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::BluetoothDevice;
#endif

#endif // _BLUETOOTH_DEVICE_H
