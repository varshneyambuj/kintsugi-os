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


/**
 * @file DiscoveryAgent.cpp
 * @brief Implementation of DiscoveryAgent, the Bluetooth device discovery controller
 *
 * DiscoveryAgent initiates and manages Bluetooth inquiry scans to discover
 * nearby remote devices. It communicates with the Bluetooth server via HCI
 * commands and dispatches discovered device notifications to a registered
 * DiscoveryListener.
 *
 * @see DiscoveryListener, LocalDevice, RemoteDevice
 */


#include <bluetooth/bluetooth_error.h>
#include <bluetooth/DiscoveryAgent.h>
#include <bluetooth/DiscoveryListener.h>
#include <bluetooth/LocalDevice.h>
#include <bluetooth/RemoteDevice.h>
#include <bluetooth/debug.h>

#include <bluetooth/HCI/btHCI_command.h>
#include <bluetooth/HCI/btHCI_event.h>

#include <bluetoothserver_p.h>
#include <CommandManager.h>

#include "KitSupport.h"


namespace Bluetooth {


/**
 * @brief Returns the list of remote devices discovered during the last inquiry.
 *
 * Retrieves the devices collected by the most recently registered
 * DiscoveryListener. If no inquiry has been initiated (i.e. no listener has
 * ever been set), an empty list is returned.
 *
 * @param option Reserved for future use; currently ignored.
 * @return A RemoteDevicesList containing all devices found during the last
 *         inquiry, or an empty list if no inquiry has been run.
 * @see DiscoveryListener::GetRemoteDevicesList()
 */
RemoteDevicesList
DiscoveryAgent::RetrieveDevices(int option)
{
	CALLED();
    // No inquiry process initiated
    if (fLastUsedListener == NULL)
        return RemoteDevicesList();

    return fLastUsedListener->GetRemoteDevicesList();
}


/**
 * @brief Starts a Bluetooth inquiry scan using the default inquiry time.
 *
 * Convenience overload that calls the three-argument StartInquiry() with the
 * value returned by GetInquiryTime().
 *
 * @param accessCode The inquiry access code (e.g. GIAC or LIAC) that
 *                   determines which devices respond to the scan.
 * @param listener   The DiscoveryListener that will receive device-found and
 *                   inquiry-completed callbacks.
 * @return B_OK if the inquiry command was submitted successfully, or an error
 *         code otherwise.
 * @see StartInquiry(uint32, DiscoveryListener*, bigtime_t)
 */
status_t
DiscoveryAgent::StartInquiry(int accessCode, DiscoveryListener* listener)
{
	CALLED();
    return StartInquiry(accessCode, listener, GetInquiryTime());
}


/**
 * @brief Starts a Bluetooth inquiry scan with an explicit duration.
 *
 * Builds and sends an HCI Inquiry command to the Bluetooth server. The
 * supplied DiscoveryListener is registered as the message target for all
 * inquiry result and completion events. The method validates that a server
 * messenger is available and that the duration falls within the allowable HCI
 * range (1–61 seconds).
 *
 * @param accessCode The inquiry access code (e.g. GIAC or LIAC).
 * @param listener   The DiscoveryListener to receive discovery callbacks; its
 *                   local-device owner is set before the command is issued.
 * @param secs       Inquiry duration in seconds; must be in the range [1, 61].
 * @retval B_OK      The inquiry command was successfully sent to the server.
 * @retval B_ERROR   No Bluetooth server messenger is available.
 * @retval B_TIMED_OUT The requested duration is outside the valid range.
 * @see CancelInquiry(), DiscoveryListener
 */
status_t
DiscoveryAgent::StartInquiry(uint32 accessCode, DiscoveryListener* listener,
	bigtime_t secs)
{
	CALLED();
    size_t size;

	if (fMessenger == NULL)
		return B_ERROR;

	if (secs < 1 || secs > 61 )
		return B_TIMED_OUT;

    void*  startInquiryCommand = NULL;

    // keep the listener whats the current listener for our inquiry state
    fLastUsedListener = listener;

    // Inform the listener who is gonna be its owner LocalDevice
    // and its discovered devices
    listener->SetLocalDeviceOwner(fLocalDevice);

    /* Issue inquiry command */
    BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
    BMessage reply;

    request.AddInt32("hci_id", fLocalDevice->ID());

    startInquiryCommand = buildInquiry(accessCode, secs, BT_MAX_RESPONSES,
		&size);

    // For stating the inquiry
    request.AddData("raw command", B_ANY_TYPE, startInquiryCommand, size);
    request.AddInt16("eventExpected", HCI_EVENT_CMD_STATUS);
    request.AddInt16("opcodeExpected",
		PACK_OPCODE(OGF_LINK_CONTROL, OCF_INQUIRY));

	// For getting each discovered message
    request.AddInt16("eventExpected",  HCI_EVENT_INQUIRY_RESULT);
	request.AddInt16("eventExpected", HCI_EVENT_INQUIRY_RESULT_WITH_RSSI);
	request.AddInt16("eventExpected", HCI_EVENT_EXTENDED_INQUIRY_RESULT);

	// For finishing each discovered message
    request.AddInt16("eventExpected",  HCI_EVENT_INQUIRY_COMPLETE);


    if (fMessenger->SendMessage(&request, listener) == B_OK)
    {
    	return B_OK;
    }

	return B_ERROR;

}


/**
 * @brief Cancels an in-progress Bluetooth inquiry scan.
 *
 * Sends an HCI Inquiry Cancel command to the Bluetooth server and waits
 * synchronously for the server's reply. The Bluetooth status code embedded
 * in the reply is returned to the caller.
 *
 * @param listener The DiscoveryListener that was passed to StartInquiry();
 *                 currently unused in the cancel path but retained for API
 *                 symmetry.
 * @retval B_ERROR  No Bluetooth server messenger is available, or the server
 *                  did not respond with a recognisable status field.
 * @return The HCI status byte from the server reply on success.
 * @see StartInquiry()
 */
status_t
DiscoveryAgent::CancelInquiry(DiscoveryListener* listener)
{
	CALLED();
    size_t size;

    if (fMessenger == NULL)
    	return B_ERROR;

    void* cancelInquiryCommand = NULL;
    int8  bt_status = BT_ERROR;

    /* Issue inquiry command */
    BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
    BMessage reply;

    request.AddInt32("hci_id", fLocalDevice->ID());

    cancelInquiryCommand = buildInquiryCancel(&size);
    request.AddData("raw command", B_ANY_TYPE, cancelInquiryCommand, size);
    request.AddInt16("eventExpected",  HCI_EVENT_CMD_STATUS);
    request.AddInt16("opcodeExpected",
		PACK_OPCODE(OGF_LINK_CONTROL, OCF_INQUIRY_CANCEL));

    if (fMessenger->SendMessage(&request, &reply) == B_OK) {
        if (reply.FindInt8("status", &bt_status ) == B_OK ) {
			return bt_status;
		}
    }

    return B_ERROR;
}


/**
 * @brief Sets the LocalDevice that owns this DiscoveryAgent.
 *
 * Associates the agent with a specific local Bluetooth adapter. The stored
 * pointer is used to supply the HCI device identifier in all subsequent
 * inquiry requests.
 *
 * @param ld Pointer to the LocalDevice that will own this agent.
 */
void
DiscoveryAgent::SetLocalDeviceOwner(LocalDevice* ld)
{
	CALLED();
    fLocalDevice = ld;
}


/**
 * @brief Constructs a DiscoveryAgent bound to the specified local device.
 *
 * Initialises the agent with the provided LocalDevice and opens a messenger
 * to the Bluetooth server via _RetrieveBluetoothMessenger().
 *
 * @param ld Pointer to the LocalDevice that will own and drive this agent.
 */
DiscoveryAgent::DiscoveryAgent(LocalDevice* ld)
{
	CALLED();
	fLocalDevice = ld;
	fMessenger = _RetrieveBluetoothMessenger();
}


/**
 * @brief Destroys the DiscoveryAgent and releases the server messenger.
 *
 * Deletes the BMessenger allocated by _RetrieveBluetoothMessenger() during
 * construction. Any in-flight inquiry should be cancelled before the agent
 * is destroyed to avoid dangling listener references.
 */
DiscoveryAgent::~DiscoveryAgent()
{
	CALLED();
	delete fMessenger;
}


} /* End namespace Bluetooth */
