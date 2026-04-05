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
 *   Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file DiscoveryListener.cpp
 * @brief Implementation of DiscoveryListener, the Bluetooth discovery callback interface
 *
 * DiscoveryListener receives notifications during a Bluetooth device inquiry
 * scan: device found events (with address, class, and RSSI), inquiry
 * completion, and error conditions. Applications subclass DiscoveryListener
 * and register it with a DiscoveryAgent.
 *
 * @see DiscoveryAgent, RemoteDevice
 */


#include <bluetooth/DiscoveryAgent.h>
#include <bluetooth/DiscoveryListener.h>
#include <bluetooth/RemoteDevice.h>
#include <bluetooth/DeviceClass.h>
#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/debug.h>

#include <bluetooth/HCI/btHCI_event.h>

#include <bluetoothserver_p.h>

#include <Message.h>


namespace Bluetooth {


/* hooks */

/**
 * @brief Called when a new remote device is discovered during an inquiry scan.
 *
 * This is a default no-op hook. Subclasses override this method to act on
 * each newly found device. It is invoked by MessageReceived() the first time
 * a device's Bluetooth address is seen; duplicate reports for the same address
 * update the cached device record but do not trigger this callback again.
 *
 * @param btDevice Pointer to the newly created RemoteDevice object representing
 *                 the discovered device; owned by the internal devices list.
 * @param cod      The Class of Device reported by the remote device, encoding
 *                 its service class, major device class, and minor device class.
 * @see DiscoveryAgent::StartInquiry(), InquiryCompleted()
 */
void
DiscoveryListener::DeviceDiscovered(RemoteDevice* btDevice, DeviceClass cod)
{
	CALLED();
}


/**
 * @brief Called when the Bluetooth inquiry scan has started.
 *
 * This is a default no-op hook. Subclasses override this method to receive
 * notification that the HCI inquiry command has been accepted by the
 * controller. The internal device list is cleared immediately before this
 * callback is fired so that each inquiry starts with a fresh set of results.
 *
 * @param status HCI status code returned with the inquiry-started event;
 *               B_OK indicates the controller accepted the inquiry command.
 * @see InquiryCompleted(), DeviceDiscovered()
 */
void
DiscoveryListener::InquiryStarted(status_t status)
{
	CALLED();
}


/**
 * @brief Called when an inquiry scan ends, whether normally or abnormally.
 *
 * This is a default no-op hook. Subclasses override this method to act on
 * inquiry completion. The @p discType parameter distinguishes the three
 * possible termination paths.
 *
 * @param discType One of the BT_INQUIRY_* constants:
 *                 - @c BT_INQUIRY_COMPLETED  – the inquiry ran to completion.
 *                 - @c BT_INQUIRY_TERMINATED – the inquiry was cancelled via
 *                   DiscoveryAgent::CancelInquiry().
 *                 - @c BT_INQUIRY_ERROR      – the inquiry ended due to an error.
 * @see InquiryStarted(), DiscoveryAgent::CancelInquiry()
 */
void
DiscoveryListener::InquiryCompleted(int discType)
{
	CALLED();
}


/* private */

/**
 * @brief Associates this listener with the LocalDevice that owns the inquiry.
 *
 * A LocalDevice reference is required for any request issued back to the
 * Bluetooth server (e.g. fetching remote device attributes). This method is
 * called by DiscoveryAgent::StartInquiry() before the inquiry command is sent.
 *
 * @param ld Pointer to the LocalDevice that initiated the inquiry owning
 *           this listener.
 */
void
DiscoveryListener::SetLocalDeviceOwner(LocalDevice* ld)
{
	CALLED();
	fLocalDevice = ld;
}


/**
 * @brief Returns the list of remote devices accumulated during the current inquiry.
 *
 * The returned list contains one RemoteDevice entry per unique Bluetooth
 * address seen since the most recent BT_MSG_INQUIRY_STARTED message.
 * Duplicate inquiry results update the existing entry rather than adding a
 * new one.
 *
 * @return A RemoteDevicesList of all devices discovered so far.
 * @see DeviceDiscovered()
 */
RemoteDevicesList
DiscoveryListener::GetRemoteDevicesList(void)
{
	CALLED();
	return fRemoteDevicesList;
}


/**
 * @brief Dispatches Bluetooth server inquiry messages to the appropriate hooks.
 *
 * Handles the following message codes sent by the Bluetooth server:
 * - @c BT_MSG_INQUIRY_DEVICE    – one or more devices reported in a single
 *   HCI event; each new address is added to the device list and triggers
 *   DeviceDiscovered(). Duplicate addresses update the cached record silently.
 * - @c BT_MSG_INQUIRY_STARTED   – clears the device list and calls
 *   InquiryStarted() with the HCI status byte.
 * - @c BT_MSG_INQUIRY_COMPLETED – calls InquiryCompleted(BT_INQUIRY_COMPLETED).
 * - @c BT_MSG_INQUIRY_TERMINATED – calls InquiryCompleted(BT_INQUIRY_TERMINATED).
 * - @c BT_MSG_INQUIRY_ERROR     – calls InquiryCompleted(BT_INQUIRY_ERROR).
 *
 * All unrecognised messages are forwarded to BLooper::MessageReceived().
 *
 * @param message The incoming BMessage delivered by the BLooper message loop.
 * @see DeviceDiscovered(), InquiryStarted(), InquiryCompleted()
 */
void
DiscoveryListener::MessageReceived(BMessage* message)
{
	CALLED();
	int8 status;

	switch (message->what) {
		case BT_MSG_INQUIRY_DEVICE:
		{
			uint8 count = 0;
			if (message->FindUInt8("count", &count) != B_OK || count == 0)
				break;

			for (uint8 i = 0; i < count; i++) {
				ssize_t size;
				const bdaddr_t* bdaddr;
				const uint8* devClass;
				uint8 pageRepetitionMode = 0;
				uint8 scanPeriodMode = 0;
				// default value is 0 only, in newer specs this has been removed in such case it
				// should be set to zero
				uint8 scanMode = 0;
				uint16 clockOffset = 0;
				int8 rssi = HCI_RSSI_INVALID;
				BString friendlyName;
				bool friendlyNameIsComplete = false;


				if (message->FindData("bdaddr", B_ANY_TYPE, i, (const void**)&bdaddr, &size) != B_OK
					|| message->FindData("dev_class", B_ANY_TYPE, i, (const void**)&devClass, &size)
						!= B_OK) {
					continue;
				}

				message->FindUInt8("page_repetition_mode", i, &pageRepetitionMode);
				message->FindUInt8("scan_period_mode", i, &scanPeriodMode);

				// if not present, the default value of these fields will be used
				message->FindUInt8("scan_mode", i, &scanMode);
				message->FindUInt16("clock_offset", i, &clockOffset);
				message->FindInt8("rssi", i, &rssi);
				message->FindBool("friendly_name_is_complete", &friendlyNameIsComplete);

				message->FindString("friendly_name", i, &friendlyName);

				// Skip duplicated replies
				bool duplicatedFound = false;
				for (int32 index = 0; index < fRemoteDevicesList.CountItems(); index++) {
					RemoteDevice* existingDevice = fRemoteDevicesList.ItemAt(index);
					bdaddr_t b1 = existingDevice->GetBluetoothAddress();
					if (bdaddrUtils::Compare(*bdaddr, b1)) {
						// update these values
						existingDevice->fPageRepetitionMode = pageRepetitionMode;
						existingDevice->fScanPeriodMode = scanPeriodMode;
						existingDevice->fScanMode = scanMode;
						existingDevice->fClockOffset = clockOffset;
						existingDevice->fRSSI = rssi;
						if (!existingDevice->fFriendlyNameIsComplete && !friendlyName.IsEmpty()) {
							existingDevice->fFriendlyNameIsComplete = friendlyNameIsComplete;
							existingDevice->fFriendlyName = friendlyName;
						}
						duplicatedFound = true;
						break;
					}
				}

				if (!duplicatedFound) {
					RemoteDevice* rd = new RemoteDevice(*bdaddr, (uint8*)devClass);
					fRemoteDevicesList.AddItem(rd);
					// keep all inquiry reported data
					rd->SetLocalDeviceOwner(fLocalDevice);
					rd->fPageRepetitionMode = pageRepetitionMode;
					rd->fScanPeriodMode = scanPeriodMode;
					rd->fScanMode = scanMode;
					rd->fClockOffset = clockOffset;
					rd->fRSSI = rssi;
					rd->fFriendlyNameIsComplete = friendlyNameIsComplete;
					rd->fFriendlyName = friendlyName;
					DeviceDiscovered(rd, rd->GetDeviceClass());
				}
			}
			break;
		}

		case BT_MSG_INQUIRY_STARTED:
			if (message->FindInt8("status", &status) == B_OK) {
				fRemoteDevicesList.MakeEmpty();
				InquiryStarted(status);
			}
			break;

		case BT_MSG_INQUIRY_COMPLETED:
			InquiryCompleted(BT_INQUIRY_COMPLETED);
			break;

		case BT_MSG_INQUIRY_TERMINATED: /* inquiry was cancelled */
			InquiryCompleted(BT_INQUIRY_TERMINATED);
			break;

		case BT_MSG_INQUIRY_ERROR:
			InquiryCompleted(BT_INQUIRY_ERROR);
			break;

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


/**
 * @brief Constructs a DiscoveryListener and starts its message loop.
 *
 * Initialises the internal BLooper base and pre-allocates the remote-device
 * list with a capacity of BT_MAX_RESPONSES entries. The looper thread is
 * started immediately so the listener is ready to receive server messages
 * before StartInquiry() returns.
 *
 * @note The looper is started unconditionally; a more refined approach would
 *       start and stop it in tandem with active inquiries.
 * @see DiscoveryAgent::StartInquiry()
 */
DiscoveryListener::DiscoveryListener()
	:
	BLooper(),
	fRemoteDevicesList(BT_MAX_RESPONSES)
{
	CALLED();
	// TODO: Make a better handling of the running not running state
	Run();
}

} /* end namespace Bluetooth */
