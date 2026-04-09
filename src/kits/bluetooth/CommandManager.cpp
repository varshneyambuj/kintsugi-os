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
 *   Copyright 2008 Mika Lindqvist
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file CommandManager.cpp
 * @brief HCI command packet builder for the Bluetooth Kit
 *
 * Provides internal helper functions to construct Host Controller Interface
 * (HCI) command packets for controlling local Bluetooth adapters. Commands
 * are built from OGF/OCF opcode pairs and optional parameter payloads,
 * ready for submission to the Bluetooth server.
 *
 * @see LocalDevice, DiscoveryAgent
 */


#include "CommandManager.h"

#include <bluetooth/bluetooth_error.h>
#include <bluetooth/debug.h>

#include "CompanyIdentifiers.h"


/**
 * @brief Allocate and populate a raw HCI command packet header.
 *
 * Allocates a contiguous memory block large enough to hold the
 * hci_command_header followed by \a psize bytes of parameter data.
 * The opcode is packed from \a ogf and \a ocf and stored in
 * little-endian byte order.  If \a param is non-NULL and \a psize is
 * non-zero, \a *param is set to point to the parameter region immediately
 * following the header so the caller can fill it in.
 *
 * @param ogf      Opcode Group Field identifying the HCI command group.
 * @param ocf      Opcode Command Field identifying the specific command.
 * @param param    Output pointer-to-pointer that receives the address of the
 *                 parameter payload region, or NULL if no parameters are needed.
 * @param psize    Size in bytes of the command-specific parameter payload.
 * @param outsize  Output pointer that receives the total allocated size
 *                 (header + payload).
 * @return A pointer to the allocated command buffer on success, or NULL if
 *         the allocation fails.
 * @note The caller is responsible for freeing the returned buffer with free().
 */
inline void*
buildCommand(uint8 ogf, uint8 ocf, void** param, size_t psize,
	size_t* outsize)
{
	CALLED();
	struct hci_command_header* header;

	header = (struct hci_command_header*) malloc(psize
		+ sizeof(struct hci_command_header));
	*outsize = psize + sizeof(struct hci_command_header);

	if (header != NULL) {
		header->opcode = B_HOST_TO_LENDIAN_INT16(PACK_OPCODE(ogf, ocf));
		header->clen = psize;

		if (param != NULL && psize != 0) {
			*param = ((uint8*)header) + sizeof(struct hci_command_header);
		}
	}
	return header;
}


// This is for request that only require a Command complete in reply.

// Propagate to ReadBufferSize => reply stored in server side
// ReadLocalVersion => reply stored in server side
// Reset => no reply

// Request that do not need any input parameter
// Output reply can be fit in 32 bits field without talking status into account
/**
 * @brief Send a parameter-less HCI command and optionally retrieve a 32-bit result.
 *
 * Builds a no-parameter HCI command from \a ofg and \a ocf, sends it to the
 * Bluetooth server via \a messenger, waits for a CommandComplete event, and
 * returns the controller's status code.  If \a result is non-NULL and the
 * reply carries a "result" field, that value is also stored.
 *
 * @param ofg       Opcode Group Field of the command to send.
 * @param ocf       Opcode Command Field of the command to send.
 * @param result    Optional output pointer for a 32-bit result value embedded
 *                  in the reply message, or NULL if not needed.
 * @param hId       The HCI device ID of the target local adapter.
 * @param messenger A BMessenger connected to the Bluetooth server.
 * @return The HCI status byte from the CommandComplete event, or BT_ERROR if
 *         the send fails.
 */
status_t
NonParameterCommandRequest(uint8 ofg, uint8 ocf, int32* result, hci_id hId,
	BMessenger* messenger)
{
	CALLED();
	int8 bt_status = BT_ERROR;

	BluetoothCommand<> simpleCommand(ofg, ocf);

	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage reply;

	request.AddInt32("hci_id", hId);
	request.AddData("raw command", B_ANY_TYPE,
		simpleCommand.Data(), simpleCommand.Size());
	request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
	request.AddInt16("opcodeExpected", PACK_OPCODE(ofg, ocf));

	if (messenger->SendMessage(&request, &reply) == B_OK) {
		reply.FindInt8("status", &bt_status);
		if (result != NULL)
			reply.FindInt32("result", result);
	}

	return bt_status;
}


#if 0
#pragma mark - CONTROL BASEBAND -
#endif


/**
 * @brief Build an HCI Reset command packet.
 *
 * @param outsize Output pointer that receives the total size of the allocated
 *                command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 */
void*
buildReset(size_t* outsize)
{
	CALLED();
	return buildCommand(OGF_CONTROL_BASEBAND, OCF_RESET,
		NULL, 0, outsize);
}


/**
 * @brief Build an HCI ReadLocalName command packet.
 *
 * Requests the human-readable name stored in the local controller.
 *
 * @param outsize Output pointer that receives the total size of the allocated
 *                command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 */
void*
buildReadLocalName(size_t* outsize)
{
	CALLED();
	return buildCommand(OGF_CONTROL_BASEBAND, OCF_READ_LOCAL_NAME,
		NULL, 0, outsize);
}


/**
 * @brief Build an HCI ReadClassOfDevice command packet.
 *
 * Requests the 3-byte Class of Device record stored in the local controller.
 *
 * @param outsize Output pointer that receives the total size of the allocated
 *                command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 */
void*
buildReadClassOfDevice(size_t* outsize)
{
	CALLED();
	return buildCommand(OGF_CONTROL_BASEBAND, OCF_READ_CLASS_OF_DEV,
	NULL, 0, outsize);
}


/**
 * @brief Build an HCI ReadScanEnable command packet.
 *
 * Requests the current inquiry- and page-scan enable state from the controller.
 *
 * @param outsize Output pointer that receives the total size of the allocated
 *                command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 */
void*
buildReadScan(size_t* outsize)
{
	CALLED();
	return buildCommand(OGF_CONTROL_BASEBAND, OCF_READ_SCAN_ENABLE,
	NULL, 0, outsize);
}


/**
 * @brief Build an HCI WriteScanEnable command packet.
 *
 * Sets the inquiry- and page-scan enable bits in the controller according to
 * \a scanmode.
 *
 * @param scanmode The desired scan-enable byte (0 = no scans,
 *                 1 = inquiry scan only, 2 = page scan only,
 *                 3 = inquiry and page scan).
 * @param outsize  Output pointer that receives the total size of the allocated
 *                 command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 * @see buildReadScan()
 */
void*
buildWriteScan(uint8 scanmode, size_t* outsize)
{
	CALLED();
	struct hci_write_scan_enable* param;
	void* command = buildCommand(OGF_CONTROL_BASEBAND, OCF_WRITE_SCAN_ENABLE,
		(void**) &param, sizeof(struct hci_write_scan_enable), outsize);


	if (command != NULL) {
		param->scan = scanmode;
	}

	return command;
}


#if 0
#pragma mark - LINK CONTROL -
#endif


/**
 * @brief Build an HCI RemoteNameRequest command packet.
 *
 * Requests the user-friendly name of a remote Bluetooth device.
 *
 * @param bdaddr         Bluetooth address of the remote device.
 * @param pscan_rep_mode Page-scan repetition mode reported during inquiry.
 * @param clock_offset   Clock offset obtained during the inquiry response,
 *                       used to speed up paging.
 * @param outsize        Output pointer that receives the total size of the
 *                       allocated command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 */
void*
buildRemoteNameRequest(bdaddr_t bdaddr, uint8 pscan_rep_mode,
	uint16 clock_offset, size_t* outsize)
{
	CALLED();
	struct hci_remote_name_request* param;
	void* command = buildCommand(OGF_LINK_CONTROL, OCF_REMOTE_NAME_REQUEST,
		(void**)&param, sizeof(struct hci_remote_name_request), outsize);

	if (command != NULL) {
		param->bdaddr = bdaddr;
		param->pscan_rep_mode = pscan_rep_mode;
		param->clock_offset = clock_offset;
	}

	return command;
}


/**
 * @brief Build an HCI Inquiry command packet.
 *
 * Starts a general or limited inquiry scan to discover nearby Bluetooth devices.
 *
 * @param lap     Lower Address Part of the inquiry access code (e.g.
 *                0x9E8B33 for GIAC or 0x9E8B00 for LIAC).
 * @param length  Maximum duration of the inquiry in units of 1.28 seconds
 *                (range 1–48, i.e. 1.28–61.44 s).
 * @param num_rsp Maximum number of inquiry responses before the inquiry stops;
 *                0 means unlimited.
 * @param outsize Output pointer that receives the total size of the allocated
 *                command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 * @see buildInquiryCancel()
 */
void*
buildInquiry(uint32 lap, uint8 length, uint8 num_rsp, size_t* outsize)
{
	CALLED();
	struct hci_cp_inquiry* param;
	void* command = buildCommand(OGF_LINK_CONTROL, OCF_INQUIRY,
		(void**) &param, sizeof(struct hci_cp_inquiry), outsize);

	if (command != NULL) {

		param->lap[2] = (lap >> 16) & 0xFF;
		param->lap[1] = (lap >>  8) & 0xFF;
		param->lap[0] = (lap >>  0) & 0xFF;
		param->length = length;
		param->num_rsp = num_rsp;
	}

	return command;
}


/**
 * @brief Build an HCI InquiryCancel command packet.
 *
 * Instructs the controller to stop an ongoing inquiry immediately.
 *
 * @param outsize Output pointer that receives the total size of the allocated
 *                command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 * @see buildInquiry()
 */
void*
buildInquiryCancel(size_t* outsize)
{
	CALLED();
	return buildCommand(OGF_LINK_CONTROL, OCF_INQUIRY_CANCEL, NULL, 0, outsize);
}


/**
 * @brief Build an HCI PinCodeRequestReply command packet.
 *
 * Supplies a PIN code to the controller in response to a PIN Code Request event
 * during legacy pairing.
 *
 * @param bdaddr   Bluetooth address of the remote device that requested the PIN.
 * @param length   Length of the PIN code in bytes (1–16).
 * @param pincode  The PIN code bytes; must be exactly \a length bytes long.
 * @param outsize  Output pointer that receives the total size of the allocated
 *                 command buffer.
 * @return A heap-allocated command buffer on success, NULL if \a length exceeds
 *         HCI_PIN_SIZE, or NULL on allocation failure.  The caller must free
 *         the buffer with free().
 * @see buildPinCodeRequestNegativeReply()
 */
void*
buildPinCodeRequestReply(bdaddr_t bdaddr, uint8 length, char pincode[16],
	size_t* outsize)
{
	CALLED();
	struct hci_cp_pin_code_reply* param;

	if (length > HCI_PIN_SIZE)  // PinCode cannot be longer than 16
		return NULL;

	void* command = buildCommand(OGF_LINK_CONTROL, OCF_PIN_CODE_REPLY,
		(void**)&param, sizeof(struct hci_cp_pin_code_reply), outsize);

	if (command != NULL) {
		param->bdaddr = bdaddr;
		param->pin_len = length;
		memcpy(&param->pin_code, pincode, length);
	}

	return command;
}


/**
 * @brief Build an HCI PinCodeRequestNegativeReply command packet.
 *
 * Rejects a PIN Code Request event, causing the pairing attempt to fail.
 *
 * @param bdaddr  Bluetooth address of the remote device whose PIN request
 *                is being rejected.
 * @param outsize Output pointer that receives the total size of the allocated
 *                command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 * @see buildPinCodeRequestReply()
 */
void*
buildPinCodeRequestNegativeReply(bdaddr_t bdaddr, size_t* outsize)
{
	CALLED();
	struct hci_cp_pin_code_neg_reply* param;

	void* command = buildCommand(OGF_LINK_CONTROL, OCF_PIN_CODE_NEG_REPLY,
		(void**) &param, sizeof(struct hci_cp_pin_code_neg_reply), outsize);

	if (command != NULL) {

		param->bdaddr = bdaddr;

	}

	return command;
}


/**
 * @brief Build an HCI AcceptConnectionRequest command packet.
 *
 * Accepts an incoming connection request from a remote device and optionally
 * requests a role switch.
 *
 * @param bdaddr  Bluetooth address of the remote device requesting the connection.
 * @param role    Desired role after connection: 0x00 to become master,
 *                0x01 to remain slave.
 * @param outsize Output pointer that receives the total size of the allocated
 *                command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 * @see buildRejectConnectionRequest()
 */
void*
buildAcceptConnectionRequest(bdaddr_t bdaddr, uint8 role, size_t* outsize)
{
	CALLED();
	struct hci_cp_accept_conn_req* param;

	void* command = buildCommand(OGF_LINK_CONTROL, OCF_ACCEPT_CONN_REQ,
		(void**) &param, sizeof(struct hci_cp_accept_conn_req), outsize);

	if (command != NULL) {
		param->bdaddr = bdaddr;
		param->role = role;
	}

	return command;
}


/**
 * @brief Build an HCI RejectConnectionRequest command packet.
 *
 * Declines an incoming connection request from a remote device.
 *
 * @param bdaddr  Bluetooth address of the remote device whose connection
 *                request is being rejected.
 * @param outsize Output pointer that receives the total size of the allocated
 *                command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 * @see buildAcceptConnectionRequest()
 */
void*
buildRejectConnectionRequest(bdaddr_t bdaddr, size_t* outsize)
{
	CALLED();
	struct hci_cp_reject_conn_req* param;

	void* command = buildCommand(OGF_LINK_CONTROL, OCF_REJECT_CONN_REQ,
		(void**)&param, sizeof(struct hci_cp_reject_conn_req),
		outsize);

	if (command != NULL) {
		param->bdaddr = bdaddr;
	}

	return command;
}


/**
 * @brief Build an HCI IOCapabilityRequestReply command packet.
 *
 * Responds to an IO Capability Request event during Secure Simple Pairing,
 * advertising the local device's IO capabilities, OOB data availability,
 * and authentication requirements.
 *
 * @param bdaddr         Bluetooth address of the remote device that issued
 *                       the IO Capability Request.
 * @param capability     Local IO capability code (e.g. NoInputNoOutput,
 *                       DisplayYesNo).
 * @param oob_data       0x00 if no OOB data is available, 0x01 if present.
 * @param authentication Authentication requirements byte (e.g. MITM
 *                       protection preference).
 * @param outsize        Output pointer that receives the total size of the
 *                       allocated command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 * @see buildUserConfirmReply()
 */
void*
buildIOCapabilityRequestReply(bdaddr_t bdaddr, uint8 capability, uint8 oob_data,
	uint8 authentication, size_t* outsize)
{
	CALLED();
	struct hci_cp_io_capability_request_reply* param;

	void* command = buildCommand(OGF_LINK_CONTROL, OCF_IO_CAPABILITY_REQUEST_REPLY, (void**)&param,
		sizeof(struct hci_cp_io_capability_request_reply), outsize);

	if (command != NULL) {
		param->bdaddr = bdaddr;

		param->capability = capability;
		param->oob_data = oob_data;
		param->authentication = authentication;
	}

	return command;
}


/**
 * @brief Build an HCI UserConfirmationRequestReply command packet.
 *
 * Confirms the numeric comparison value during Secure Simple Pairing,
 * indicating that the user has accepted the displayed passkey.
 *
 * @param bdaddr  Bluetooth address of the remote device involved in pairing.
 * @param outsize Output pointer that receives the total size of the allocated
 *                command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 * @see buildIOCapabilityRequestReply()
 */
void*
buildUserConfirmReply(bdaddr_t bdaddr, size_t* outsize)
{
	CALLED();
	struct hci_cp_user_confirm_reply* param;

	void* command = buildCommand(OGF_LINK_CONTROL, OCF_USER_CONFIRM_REPLY, (void**)&param,
		sizeof(struct hci_cp_user_confirm_reply), outsize);

	if (command != NULL)
		param->bdaddr = bdaddr;

	return command;
}


/**
 * @brief Build an HCI AuthenticationRequested command packet.
 *
 * Initiates authentication for an existing ACL connection identified by
 * \a handle.
 *
 * @param handle  The ACL connection handle returned when the connection was
 *                established.
 * @param outsize Output pointer that receives the total size of the allocated
 *                command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 */
void*
buildAuthenticationRequested(uint16 handle, size_t* outsize)
{
	CALLED();
	struct hci_cp_auth_requested* param;

	void* command = buildCommand(OGF_LINK_CONTROL, OCF_AUTH_REQUESTED, (void**)&param,
		sizeof(struct hci_cp_auth_requested), outsize);

	if (command != NULL)
		param->handle = handle;

	return command;
}


#if 0
#pragma mark - INFORMATIONAL_PARAM -
#endif


/**
 * @brief Build an HCI ReadLocalVersionInformation command packet.
 *
 * Requests the HCI, LMP, and manufacturer version information from the
 * local controller.
 *
 * @param outsize Output pointer that receives the total size of the allocated
 *                command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 */
void*
buildReadLocalVersionInformation(size_t* outsize)
{
	CALLED();
	return buildCommand(OGF_INFORMATIONAL_PARAM, OCF_READ_LOCAL_VERSION,
		NULL, 0, outsize);
}


/**
 * @brief Build an HCI ReadBufferSize command packet.
 *
 * Requests the maximum ACL and SCO data packet lengths and the total number
 * of data packets the controller can buffer.
 *
 * @param outsize Output pointer that receives the total size of the allocated
 *                command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 */
void*
buildReadBufferSize(size_t* outsize)
{
	CALLED();
	return buildCommand(OGF_INFORMATIONAL_PARAM, OCF_READ_BUFFER_SIZE,
		NULL, 0, outsize);
}


/**
 * @brief Build an HCI ReadBdAddr command packet.
 *
 * Requests the 48-bit Bluetooth device address stored in the local controller.
 *
 * @param outsize Output pointer that receives the total size of the allocated
 *                command buffer.
 * @return A heap-allocated command buffer on success, or NULL on allocation
 *         failure.  The caller must free it with free().
 */
void*
buildReadBdAddr(size_t* outsize)
{
	CALLED();
	return buildCommand(OGF_INFORMATIONAL_PARAM, OCF_READ_BD_ADDR,
		NULL, 0, outsize);
}


/** @brief Human-readable names for HCI Link Control commands, indexed by OCF - 1. */
const char* linkControlCommands[] = {
	"Inquiry",
	"Inquiry Cancel",
	"Periodic Inquiry Mode",
	"Exit Periodic Inquiry Mode",
	"Create Connection",
	"Disconnect",
	"Add SCO Connection", // not on 2.1
	"Cancel Create Connection",
	"Accept Connection Request",
	"Reject Connection Request",
	"Link Key Request Reply",
	"Link Key Request Negative Reply",
	"PIN Code Request Reply",
	"PIN Code Request Negative Reply",
	"Change Connection Packet Type",
	"Reserved", // not on 2.1",
	"Authentication Requested",
	"Reserved", // not on 2.1",
	"Set Connection Encryption",
	"Reserved", // not on 2.1",
	"Change Connection Link Key",
	"Reserved", // not on 2.1",
	"Master Link Key",
	"Reserved", // not on 2.1",
	"Remote Name Request",
	"Cancel Remote Name Request",
	"Read Remote Supported Features",
	"Read Remote Extended Features",
	"Read Remote Version Information",
	"Reserved", // not on 2.1",
	"Read Clock Offset",
	"Read LMP Handle",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Setup Synchronous Connection",
	"Accept Synchronous Connection",
	"Reject Synchronous Connection",
	"IO Capability Request Reply",
	"User Confirmation Request Reply",
	"User Confirmation Request Negative Reply",
	"User Passkey Request Reply",
	"User Passkey Request Negative Reply",
	"Remote OOB Data Request Reply",
	"Reserved",
	"Reserved",
	"Remote OOB Data Request Negative Reply",
	"IO Capabilities Response Negative Reply"
};


/** @brief Human-readable names for HCI Link Policy commands, indexed by OCF - 1. */
const char* linkPolicyCommands[] = {
	"Hold Mode",
	"Reserved",
	"Sniff Mode",
	"Exit Sniff Mode",
	"Park State",
	"Exit Park State",
	"QoS Setup",
	"Reserved",
	"Role Discovery",
	"Reserved",
	"Switch Role",
	"Read Link Policy Settings",
	"Write Link Policy Settings",
	"Read Default Link Policy Settings",
	"Write Default Link Policy Settings",
	"Flow Specification",
	"Sniff Subrating"
};


/** @brief Human-readable names for HCI Controller and Baseband commands, indexed by OCF - 1. */
const char* controllerBasebandCommands[] = {
	"Set Event Mask",
	"Reserved",
	"Reset",
	"Reserved",
	"Set Event Filter",
	"Reserved",
	"Reserved",
	"Flush",
	"Read PIN Type",
	"Write PIN Type",
	"Create New Unit Key",
	"Reserved",
	"Read Stored Link Key",
	"Reserved",
	"Reserved",
	"Reserved",
	"Write Stored Link Key",
	"Delete Stored Link Key",
	"Write Local Name",
	"Read Local Name",
	"Read Connection Accept Timeout",
	"Write Connection Accept Timeout",
	"Read Page Timeout",
	"Write Page Timeout",
	"Read Scan Enable",
	"Write Scan Enable",
	"Read Page Scan Activity",
	"Write Page Scan Activity",
	"Read Inquiry Scan Activity",
	"Write Inquiry Scan Activity",
	"Read Authentication Enable",
	"Write Authentication Enable",
	"Read Encryption Mode", // not 2.1
	"Write Encryption Mode",// not 2.1
	"Read Class Of Device",
	"Write Class Of Device",
	"Read Voice Setting",
	"Write Voice Setting",
	"Read Automatic Flush Timeout",
	"Write Automatic Flush Timeout",
	"Read Num Broadcast Retransmissions",
	"Write Num Broadcast Retransmissions",
	"Read Hold Mode Activity",
	"Write Hold Mode Activity",
	"Read Transmit Power Level",
	"Read Synchronous Flow Control Enable",
	"Write Synchronous Flow Control Enable",
	"Reserved",
	"Set Host Controller To Host Flow Control",
	"Reserved",
	"Host Buffer Size",
	"Reserved",
	"Host Number Of Completed Packets",
	"Read Link Supervision Timeout",
	"Write Link Supervision Timeout",
	"Read Number of Supported IAC",
	"Read Current IAC LAP",
	"Write Current IAC LAP",
	"Read Page Scan Period Mode", // not 2.1
	"Write Page Scan Period Mode", // not 2.1
	"Read Page Scan Mode",		// not 2.1
	"Write Page Scan Mode",		// not 2.1
	"Set AFH Channel Classification",
	"Reserved",
	"Reserved",
	"Read Inquiry Scan Type",
	"Write Inquiry Scan Type",
	"Read Inquiry Mode",
	"Write Inquiry Mode",
	"Read Page Scan Type",
	"Write Page Scan Type",
	"Read AFH Channel Assessment Mode",
	"Write AFH Channel Assessment Mode",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Read Extended Inquiry Response",
	"Write Extended Inquiry Response",
	"Refresh Encryption Key",
	"Reserved",
	"Read Simple Pairing Mode",
	"Write Simple Pairing Mode",
	"Read Local OOB Data",
	"Read Inquiry Transmit Power Level",
	"Write Inquiry Transmit Power Level",
	"Read Default Erroneous Data Reporting",
	"Write Default Erroneous Data Reporting",
	"Reserved",
	"Reserved",
	"Reserved",
	"Enhanced Flush",
	"Send Keypress Notification"
};


/** @brief Human-readable names for HCI Informational Parameters commands, indexed by OCF - 1. */
const char* informationalParametersCommands[] = {
	"Read Local Version Information",
	"Read Local Supported Commands",
	"Read Local Supported Features",
	"Read Local Extended Features",
	"Read Buffer Size",
	"Reserved",
	"Read Country Code", // not 2.1
	"Reserved",
	"Read BD ADDR"
};


/** @brief Human-readable names for HCI Status Parameters commands, indexed by OCF - 1. */
const char* statusParametersCommands[] = {
	"Read Failed Contact Counter",
	"Reset Failed Contact Counter",
	"Read Link Quality",
	"Reserved",
	"Read RSSI",
	"Read AFH Channel Map",
	"Read Clock",
};


/** @brief Human-readable names for HCI Testing commands, indexed by OCF - 1. */
const char* testingCommands[] = {
	"Read Loopback Mode",
	"Write Loopback Mode",
	"Enable Device Under Test Mode",
	"Write Simple Pairing Debug Mode",
};


/** @brief Human-readable names for Bluetooth HCI events, indexed by event code - 1. */
const char* bluetoothEvents[] = {
	"Inquiry Complete",
	"Inquiry Result",
	"Conn Complete",
	"Conn Request",
	"Disconnection Complete",
	"Auth Complete",
	"Remote Name Request Complete",
	"Encrypt Change",
	"Change Conn Link Key Complete",
	"Master Link Key Compl",
	"Rmt Features",
	"Rmt Version",
	"Qos Setup Complete",
	"Command Complete",
	"Command Status",
	"Hardware Error",
	"Flush Occur",
	"Role Change",
	"Num Comp Pkts",
	"Mode Change",
	"Return Link Keys",
	"Pin Code Req",
	"Link Key Req",
	"Link Key Notify",
	"Loopback Command",
	"Data Buffer Overflow",
	"Max Slot Change",
	"Read Clock Offset Compl",
	"Con Pkt Type Changed",
	"Qos Violation",
	"Reserved",
	"Page Scan Rep Mode Change",
	"Flow Specification",
	"Inquiry Result With Rssi",
	"Remote Extended Features",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Synchronous Connection Completed",
	"Synchronous Connection Changed",
	"Reserved",
	"Extended Inquiry Result",
	"Encryption Key Refresh Complete",
	"Io Capability Request",
	"Io Capability Response",
	"User Confirmation Request",
	"User Passkey Request",
	"Oob Data Request",
	"Simple Pairing Complete",
	"Reserved",
	"Link Supervision Timeout Changed",
	"Enhanced Flush Complete",
	"Reserved",
	"Reserved",
	"Keypress Notification",
	"Remote Host Supported Features Notification"
};


/** @brief Human-readable Bluetooth HCI error code strings, indexed by error code value. */
const char* bluetoothErrors[] = {
	"No Error",
	"Unknown Command",
	"No Connection",
	"Hardware Failure",
	"Page Timeout",
	"Authentication Failure",
	"Pin Or Key Missing",
	"Memory Full",
	"Connection Timeout",
	"Max Number Of Connections",
	"Max Number Of Sco Connections",
	"Acl Connection Exists",
	"Command Disallowed",
	"Rejected Limited Resources",
	"Rejected Security",
	"Rejected Personal",
	"Host Timeout",
	"Unsupported Feature",
	"Invalid Parameters",
	"Remote User Ended Connection",
	"Remote Low Resources",
	"Remote Power Off",
	"Connection Terminated",
	"Repeated Attempts",
	"Pairing Not Allowed",
	"Unknown Lmp Pdu",
	"Unsupported Remote Feature",
	"Sco Offset Rejected",
	"Sco Interval Rejected",
	"Air Mode Rejected",
	"Invalid Lmp Parameters",
	"Unspecified Error",
	"Unsupported Lmp Parameter Value",
	"Role Change Not Allowed",
	"Lmp Response Timeout",
	"Lmp Error Transaction Collision",
	"Lmp Pdu Not Allowed",
	"Encryption Mode Not Accepted",
	"Unit Link Key Used",
	"Qos Not Supported",
	"Instant Passed",
	"Pairing With Unit Key Not Supported",
	"Different Transaction Collision",
	"Qos Unacceptable Parameter",
	"Qos Rejected",
	"Classification Not Supported",
	"Insufficient Security",
	"Parameter Out Of Range",
	"Reserved",
	"Role Switch Pending",
	"Reserved",
	"Slot Violation",
	"Role Switch Failed",
	"Extended Inquiry Response Too Large",
	"Simple Pairing Not Supported By Host",
	"Host Busy Pairing"
};


/** @brief Printable version strings for each HCI specification revision, indexed by version code. */
const char* hciVersion[] = { "1.0B" , "1.1" , "1.2" , "2.0" , "2.1",
	"3.0", "4.0", "4.1", "4.2", "5.0", "5.1", "5.2"};
/** @brief Printable version strings for each LMP specification revision, indexed by version code. */
const char* lmpVersion[] = { "1.0" , "1.1" , "1.2" , "2.0" , "2.1",
	"3.0", "4.0", "4.1", "4.2", "5.0", "5.1", "5.2"};

#if 0
#pragma mark -
#endif


/**
 * @brief Return the printable HCI specification version string for a version code.
 *
 * @param ver HCI version code as reported by the ReadLocalVersionInformation command.
 * @return A constant string such as "2.1" or "4.2".
 * @note No bounds checking is performed; passing an out-of-range value will
 *       read beyond the hciVersion array.
 * @see BluetoothLmpVersion()
 */
const char*
BluetoothHciVersion(uint16 ver)
{
	CALLED();
	return hciVersion[ver];
}


/**
 * @brief Return the printable LMP specification version string for a version code.
 *
 * @param ver LMP version code as reported by the ReadLocalVersionInformation command.
 * @return A constant string such as "2.1" or "4.2".
 * @note No bounds checking is performed; passing an out-of-range value will
 *       read beyond the lmpVersion array.
 * @see BluetoothHciVersion()
 */
const char*
BluetoothLmpVersion(uint16 ver)
{
	CALLED();
	return lmpVersion[ver];
}


/**
 * @brief Return the human-readable name for a packed HCI command opcode.
 *
 * Decodes the OGF from \a opcode and dispatches into the appropriate
 * per-group command name table to look up the OCF.
 *
 * @param opcode The packed 16-bit HCI opcode (OGF in bits 15:10, OCF in bits 9:0).
 * @return A constant string naming the command, "Vendor specific command" for
 *         OGF_VENDOR_CMD, or "Unknown command" if the OGF is not recognized.
 * @note OCF values are treated as 1-based indices into each group's array.
 *       Passing an OCF beyond the array bounds will cause undefined behaviour.
 */
const char*
BluetoothCommandOpcode(uint16 opcode)
{
	CALLED();
	// NOTE: BT implementations beyond 2.1
	// could specify new commands with OCF numbers
	// beyond the boundaries of the arrays and crash.
	// But only our stack could issue them so its under
	// our control.
	switch (GET_OPCODE_OGF(opcode)) {
		case OGF_LINK_CONTROL:
			return linkControlCommands[GET_OPCODE_OCF(opcode) - 1];
			break;

		case OGF_LINK_POLICY:
			return linkPolicyCommands[GET_OPCODE_OCF(opcode) - 1];
			break;

		case OGF_CONTROL_BASEBAND:
			return controllerBasebandCommands[GET_OPCODE_OCF(opcode) - 1];
			break;

		case OGF_INFORMATIONAL_PARAM:
			return informationalParametersCommands[GET_OPCODE_OCF(opcode) - 1];
			break;

		case OGF_STATUS_PARAM:
			return statusParametersCommands[GET_OPCODE_OCF(opcode) - 1];
			break;

		case OGF_TESTING_CMD:
			return testingCommands[GET_OPCODE_OCF(opcode) - 1];
			break;
		case OGF_VENDOR_CMD:
			return "Vendor specific command";
			break;
		default:
			return "Unknown command";
			break;
	}

}


/**
 * @brief Return the human-readable name for a Bluetooth HCI event code.
 *
 * @param event The 8-bit HCI event code (1-based, matching the HCI specification).
 * @return A constant string naming the event, or "Event out of Range!" if
 *         \a event falls outside the bluetoothEvents table.
 */
const char*
BluetoothEvent(uint8 event)
{
	CALLED();
	if (event < sizeof(bluetoothEvents) / sizeof(const char*))
		return bluetoothEvents[event - 1];
	else
		return "Event out of Range!";
}


/**
 * @brief Return the company name string for a Bluetooth manufacturer identifier.
 *
 * Looks up \a manufacturer in the Bluetooth Assigned Numbers company
 * identifier table.
 *
 * @param manufacturer The 16-bit manufacturer identifier as assigned by the
 *                     Bluetooth SIG.
 * @return The company name string, "internal use" for the reserved value
 *         0xFFFF, or "not assigned" if the identifier is beyond the table.
 * @see BluetoothError()
 */
const char*
BluetoothManufacturer(uint16 manufacturer)
{
	CALLED();
	if (manufacturer < sizeof(bluetoothManufacturers) / sizeof(const char*))
		return bluetoothManufacturers[manufacturer];
	else if (manufacturer == 0xFFFF)
		return "internal use";
	else
		return "not assigned";
}


/**
 * @brief Return the human-readable description for a Bluetooth HCI error code.
 *
 * @param error The 8-bit HCI error code returned by the controller.
 * @return A constant string describing the error, or "not specified" if
 *         \a error is beyond the bluetoothErrors table.
 * @see BluetoothEvent()
 */
const char*
BluetoothError(uint8 error)
{
	CALLED();
	if (error < sizeof(bluetoothErrors) / sizeof(const char*))
		return bluetoothErrors[error];
	else
		return "not specified";
}
