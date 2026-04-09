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
 *   Copyright 2007-2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file DiscoveryAgent.h
 *  @brief Agent class that controls Bluetooth device discovery (inquiry) operations. */

#ifndef _DISCOVERY_AGENT_H
#define _DISCOVERY_AGENT_H

#include <bluetooth/bluetooth.h>
#include <bluetooth/DiscoveryListener.h>
#include <bluetooth/RemoteDevice.h>


/** @brief Retrieve only cached (previously discovered) devices. */
#define BT_CACHED 0x00
/** @brief Retrieve only pre-known (bonded/paired) devices. */
#define BT_PREKNOWN 0x01
/** @brief Device is not in discoverable mode. */
#define BT_NOT_DISCOVERABLE 0x01

/** @brief General Inquiry Access Code (GIAC) — discovers all discoverable devices. */
#define BT_GIAC 0x9E8B33
/** @brief Limited Inquiry Access Code (LIAC) — discovers only devices in limited discovery mode. */
#define BT_LIAC 0x9E8B00

/** @brief Maximum number of inquiry responses allowed per inquiry operation. */
#define BT_MAX_RESPONSES		(32)

/** @brief Base time unit for inquiry duration in seconds (1.28 s per unit). */
#define BT_BASE_INQUIRY_TIME	(1.28)
/** @brief Default inquiry duration in units of 1.28 s (0x0A = ~12.8 s). */
#define BT_DEFAULT_INQUIRY_TIME	(0x0A)
/** @brief Minimum inquiry duration value (0x01 = ~1.28 s). */
#define BT_MIN_INQUIRY_TIME	(0x01) //  1.18 secs
/** @brief Maximum inquiry duration value (0x30 = ~61.44 s). */
#define BT_MAX_INQUIRY_TIME	(0x30) // 61.44 secs

namespace Bluetooth {

class LocalDevice;

/** @brief Controls Bluetooth device discovery on behalf of a LocalDevice, issuing HCI inquiry
 *         commands and delivering results to a DiscoveryListener. */
class DiscoveryAgent {

public:

	static const int GIAC = BT_GIAC;             /**< General Inquiry Access Code constant. */
	static const int LIAC = BT_LIAC;             /**< Limited Inquiry Access Code constant. */

	static const int PREKNOWN = BT_PREKNOWN;           /**< Option: return pre-known devices only. */
	static const int CACHED = BT_CACHED;               /**< Option: return cached devices only. */
	static const int NOT_DISCOVERABLE = BT_NOT_DISCOVERABLE; /**< Option: filter non-discoverable devices. */

	/** @brief Retrieves a list of known remote devices according to the specified option.
	 *  @param option One of CACHED, PREKNOWN, or NOT_DISCOVERABLE.
	 *  @return A RemoteDevicesList containing the matching devices. */
	RemoteDevicesList RetrieveDevices(int option); /* TODO */

	/** @brief Starts a Bluetooth inquiry using the given access code, notifying the listener.
	 *  @param accessCode  The inquiry access code (GIAC or LIAC).
	 *  @param listener    The DiscoveryListener to receive discovery callbacks.
	 *  @return B_OK on success, or an error code if the inquiry could not be started. */
	status_t StartInquiry(int accessCode, DiscoveryListener* listener); /* Throwing */

	/** @brief Starts a Bluetooth inquiry with an explicit timeout duration.
	 *  @param accessCode  The inquiry access code (GIAC or LIAC).
	 *  @param listener    The DiscoveryListener to receive discovery callbacks.
	 *  @param secs        Duration of the inquiry in microseconds (bigtime_t).
	 *  @return B_OK on success, or an error code if the inquiry could not be started. */
	status_t StartInquiry(uint32 accessCode, DiscoveryListener* listener, bigtime_t secs);

	/** @brief Cancels an ongoing inquiry associated with the given listener.
	 *  @param listener The DiscoveryListener whose inquiry should be cancelled.
	 *  @return B_OK on success, or an error code if cancellation failed. */
	status_t CancelInquiry(DiscoveryListener* listener);

	/*
	int searchServices(int[] attrSet, UUID[] uuidSet, RemoteDevice btDev,
					DiscoveryListener discListener);

	bool cancelServiceSearch(int transID);
	BString selectService(UUID uuid, int security, boolean master);
	*/

private:
	DiscoveryAgent(LocalDevice* ld);
	~DiscoveryAgent();
	void SetLocalDeviceOwner(LocalDevice* ld);

	DiscoveryListener* fLastUsedListener;
	LocalDevice*       fLocalDevice;
	BMessenger*        fMessenger;

	friend class LocalDevice;
};

}

#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::DiscoveryAgent;
#endif

#endif // _DISCOVERY_AGENT_H
