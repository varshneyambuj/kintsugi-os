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

/** @file DiscoveryListener.h
 *  @brief Abstract listener interface for receiving Bluetooth device discovery events. */

#ifndef _DISCOVERY_LISTENER_H
#define _DISCOVERY_LISTENER_H

#include <Looper.h>
#include <ObjectList.h>

#include <bluetooth/DeviceClass.h>
#include <bluetooth/RemoteDevice.h>


/** @brief Discovery completion type: inquiry finished normally. */
#define BT_INQUIRY_COMPLETED	0x01 // HCI_EVENT_X check specs
/** @brief Discovery completion type: inquiry was cancelled by the host. */
#define BT_INQUIRY_TERMINATED	0x02 // HCI_EVENT_X
/** @brief Discovery completion type: inquiry ended with an error. */
#define BT_INQUIRY_ERROR		0x03 // HCI_EVENT_X


namespace Bluetooth {

/** @brief Ordered list of discovered remote Bluetooth devices. */
typedef BObjectList<RemoteDevice> RemoteDevicesList;

class LocalDevice;

/** @brief Abstract BLooper subclass that receives asynchronous callbacks during Bluetooth
 *         device discovery; subclass and override the virtual methods to handle events. */
class DiscoveryListener : public BLooper {

public:

	static const int INQUIRY_COMPLETED = BT_INQUIRY_COMPLETED;   /**< Inquiry finished normally. */
	static const int INQUIRY_TERMINATED = BT_INQUIRY_TERMINATED; /**< Inquiry was cancelled. */
	static const int INQUIRY_ERROR = BT_INQUIRY_ERROR;           /**< Inquiry ended with an error. */

	static const int SERVICE_SEARCH_COMPLETED = 0x01;             /**< Service search completed normally. */
	static const int SERVICE_SEARCH_TERMINATED = 0x02;            /**< Service search was cancelled. */
	static const int SERVICE_SEARCH_ERROR = 0x03;                 /**< Service search ended with an error. */
	static const int SERVICE_SEARCH_NO_RECORDS = 0x04;            /**< No service records were found. */
	static const int SERVICE_SEARCH_DEVICE_NOT_REACHABLE = 0x06; /**< Target device was unreachable. */

	/** @brief Called when a new remote device is discovered during an inquiry.
	 *  @param btDevice Pointer to the discovered RemoteDevice (owned by the listener).
	 *  @param cod      The Class of Device reported by the remote device. */
	virtual void DeviceDiscovered(RemoteDevice* btDevice, DeviceClass cod);
	/*
	virtual void servicesDiscovered(int transID, ServiceRecord[] servRecord);
	virtual void serviceSearchCompleted(int transID, int respCode);
	*/

	/** @brief Called when a device inquiry finishes.
	 *  @param discType One of INQUIRY_COMPLETED, INQUIRY_TERMINATED, or INQUIRY_ERROR. */
	virtual void InquiryCompleted(int discType);

	/** @brief Called when an inquiry has been successfully started (non-JSR82 extension).
	 *  @param status B_OK if the inquiry started, or an error code if it failed to start. */
	/* JSR82 non-defined methods */
	virtual void InquiryStarted(status_t status);

private:

	/** @brief Returns the list of remote devices discovered so far during the current inquiry.
	 *  @return A RemoteDevicesList containing all discovered devices. */
	RemoteDevicesList GetRemoteDevicesList(void);

	/** @brief Handles incoming BMessages from the Bluetooth stack (internal use).
	 *  @param msg The incoming BMessage to process. */
	void MessageReceived(BMessage* msg);

	LocalDevice* fLocalDevice;
	RemoteDevicesList fRemoteDevicesList;

	friend class DiscoveryAgent;

protected:
	/** @brief Constructs a DiscoveryListener; call from subclass constructors. */
	DiscoveryListener();

	/** @brief Associates this listener with a specific local Bluetooth device.
	 *  @param ld Pointer to the owning LocalDevice. */
	void SetLocalDeviceOwner(LocalDevice* ld);
};

}

#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::DiscoveryListener;
#endif

#endif // _DISCOVERY_LISTENER_H
