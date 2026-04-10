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

/** @file LocalDeviceImpl.h
 *  @brief Server-side controller object: dispatches HCI events into the daemon's request handlers. */

#ifndef _LOCALDEVICE_IMPL_H_
#define _LOCALDEVICE_IMPL_H_

#include <String.h>

#include <bluetooth/bluetooth.h>

#include "LocalDeviceHandler.h"

#include "HCIDelegate.h"
#include "HCIControllerAccessor.h"
#include "HCITransportAccessor.h"

/** @brief Concrete server-side LocalDeviceHandler with full HCI event dispatch.
 *
 * Wraps an HCIDelegate (either controller or transport flavour), parses
 * incoming HCI event packets, and either matches them against an in-flight
 * petition (replying to the originating client) or runs the spontaneous-event
 * handlers below for connection, pairing, and inquiry events. */
class LocalDeviceImpl : public LocalDeviceHandler {

private:
	LocalDeviceImpl(HCIDelegate* hd);

public:

	// Factory methods
	/** @brief Creates a LocalDeviceImpl backed by an HCIControllerAccessor on @p path. */
	static LocalDeviceImpl* CreateControllerAccessor(BPath* path);
	/** @brief Creates a LocalDeviceImpl backed by an HCITransportAccessor on @p path. */
	static LocalDeviceImpl* CreateTransportAccessor(BPath* path);
	~LocalDeviceImpl();
	/** @brief Removes this device from the daemon's local-devices list. */
	void Unregister();

	/** @brief Entry point: dispatches an incoming HCI event packet. */
	void HandleEvent(struct hci_event_header* event);

	// Request handling
	/** @brief Handles a synchronous client request that does not need event tracking. */
	status_t ProcessSimpleRequest(BMessage* request);

private:
	void HandleUnexpectedEvent(struct hci_event_header* event);
	void HandleExpectedRequest(struct hci_event_header* event,
		BMessage* request);

	// Events handling
	void CommandComplete(struct hci_ev_cmd_complete* event, BMessage* request,
		int32 index);
	void CommandStatus(struct hci_ev_cmd_status* event, BMessage* request,
		int32 index);

	void NumberOfCompletedPackets(struct hci_ev_num_comp_pkts* event);

	// Inquiry
	void InquiryResult(uint8* numberOfResponses, BMessage* request);
	void InquiryResultWithRSSI(uint8* numberOfResponses, BMessage* request);
	void ExtendedInquiryResult(uint8* numberOfResponses, BMessage* request);
	void ParseEIR(const uint8* eir, BMessage& reply);
	void InquiryComplete(uint8* status, BMessage* request);
	void RemoteNameRequestComplete(struct hci_ev_remote_name_request_complete_reply*
		remotename, BMessage* request);

	// Connection
	void ConnectionComplete(struct hci_ev_conn_complete* event, BMessage* request);
	void ConnectionRequest(struct hci_ev_conn_request* event, BMessage* request);
	void DisconnectionComplete(struct hci_ev_disconnection_complete_reply* event,
		BMessage* request);

	// Pairing
	void PinCodeRequest(struct hci_ev_pin_code_req* event, BMessage* request);
	void RoleChange(struct hci_ev_role_change* event, BMessage* request);
	void LinkKeyNotify(struct hci_ev_link_key_notify* event, BMessage* request);
	void ReturnLinkKeys(struct hci_ev_return_link_keys* returnedKeys);

	void LinkKeyRequested(struct hci_ev_link_key_req* keyReqyested,
		BMessage* request);

	void PageScanRepetitionModeChange(struct hci_ev_page_scan_rep_mode_change* event,
		BMessage* request);
	void MaxSlotChange(struct hci_ev_max_slot_change* event, BMessage* request);

	void HardwareError(struct hci_ev_hardware_error* event);

	// Simple Secure Pairing
	void IOCapabilityRequest(struct hci_ev_io_capability_request* event,
		BMessage* request);
	void IOCapabilityResponse(struct hci_ev_io_capability_response* event,
		BMessage* request);
	void UserConfirmationRequest(struct hci_ev_user_confirmation_request* event, BMessage* request);
	void SimplePairingComplete(struct hci_ev_simple_pairing_complete* event,
		BMessage* request);
	void AuthComplete(struct hci_ev_auth_complete* eventData, BMessage* request);
};

#endif
