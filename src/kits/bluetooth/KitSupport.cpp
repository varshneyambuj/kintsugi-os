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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file KitSupport.cpp
 * @brief Internal support utilities for the Bluetooth Kit
 *
 * Provides shared helper functions used internally by the Bluetooth Kit,
 * including _RetrieveBluetoothMessenger() which establishes a BMessenger
 * connection to the running Bluetooth server for sending HCI requests.
 *
 * @see LocalDevice, DiscoveryAgent
 */


#include <bluetooth/bluetooth.h>
#include <bluetooth/DiscoveryAgent.h>
#include <bluetooth/debug.h>

#include <bluetoothserver_p.h>

#include "KitSupport.h"


/**
 * @brief Retrieve a valid BMessenger connected to the Bluetooth server.
 *
 * Allocates a new BMessenger targeting the Bluetooth server signature
 * (@c BLUETOOTH_SIGNATURE). If the server is not running or the messenger
 * cannot be validated, the allocation is freed and @c NULL is returned.
 *
 * @return A heap-allocated BMessenger targeting the Bluetooth server, or
 *         @c NULL if the server is unreachable or the messenger is invalid.
 *
 * @note The caller is responsible for deleting the returned BMessenger.
 *       A known memory leak exists in error paths — marked for review.
 * @see LocalDevice, DiscoveryAgent
 */
BMessenger*
_RetrieveBluetoothMessenger(void)
{
	CALLED();
	// Fix/review: leaking memory here
	BMessenger* fMessenger = new BMessenger(BLUETOOTH_SIGNATURE);

	if (fMessenger == NULL || !fMessenger->IsValid()) {
		delete fMessenger;
		return NULL;
	} else
		return fMessenger;
}


/**
 * @brief Return the default Bluetooth inquiry duration.
 *
 * Returns the compile-time constant @c BT_DEFAULT_INQUIRY_TIME, which
 * specifies how long (in units of 1.28 seconds) a device discovery scan
 * should run when no explicit duration is requested.
 *
 * @return The default inquiry time value as a @c uint8.
 * @see SetInquiryTime(), DiscoveryAgent
 */
uint8
GetInquiryTime()
{
	CALLED();
	return BT_DEFAULT_INQUIRY_TIME;
}


/**
 * @brief Set the Bluetooth inquiry duration (currently a no-op).
 *
 * Intended to override the inquiry time used by device discovery scans.
 * The implementation is currently a placeholder and does not persist
 * the supplied value.
 *
 * @param time The desired inquiry time, in units of 1.28 seconds.
 * @see GetInquiryTime()
 */
void
SetInquiryTime(uint8 time)
{
	CALLED();
	((void)(time));
}
