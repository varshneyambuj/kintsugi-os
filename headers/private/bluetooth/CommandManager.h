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
 *   Copyright 2008 Mika Lindqvist
 *   Copyright 2012 Fredrik Modéen [firstname]@[lastname].se
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file CommandManager.h
 *  @brief HCI command builder utilities and the BluetoothCommand template for
 *         constructing type-safe HCI command packets. */

#ifndef _COMMAND_MANAGER_H
#define _COMMAND_MANAGER_H

#include <malloc.h>
#include <string.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/bluetooth_error.h>
#include <bluetooth/HCI/btHCI_command.h>
#include <bluetooth/HCI/btHCI_event.h>

#include <Message.h>
#include <Messenger.h>

#include <bluetoothserver_p.h>

/** @brief Convenience macro that expands a type name into a (type, sizeof(type))
 *         argument pair suitable for BluetoothCommand template instantiation. */
#define typed_command(type) type, sizeof(type)

/** @brief Type-safe wrapper that serialises an HCI command header followed by
 *         an optional parameter structure into a contiguous byte buffer.
 *
 *  @tparam Type       The C struct type describing the command parameters.
 *                     Use @c void and @p paramSize = 0 for parameter-less commands.
 *  @tparam paramSize  Size of the parameter structure in bytes.
 *  @tparam HeaderSize Size of the HCI command header; defaults to HCI_COMMAND_HDR_SIZE. */
template <typename Type = void, int paramSize = 0,
	int HeaderSize = HCI_COMMAND_HDR_SIZE>
class BluetoothCommand {

public:
	/** @brief Constructs a BluetoothCommand and initialises the HCI header with
	 *         the given OGF/OCF opcode pair.
	 *  @param ogf Opcode Group Field identifying the command category.
	 *  @param ocf Opcode Command Field identifying the specific command. */
	BluetoothCommand(uint8 ogf, uint8 ocf)
	{
		fHeader = (struct hci_command_header*) fBuffer;

		if (paramSize != 0)
			fContent = (Type*)(fHeader + 1);
		else
			// avoid pointing outside in case of not having parameters
			fContent = (Type*)fHeader;

		fHeader->opcode = B_HOST_TO_LENDIAN_INT16(PACK_OPCODE(ogf, ocf));
		fHeader->clen = paramSize;
	}

	/** @brief Returns a pointer to the parameter structure so that individual
	 *         fields can be set with the arrow operator.
	 *  @return Pointer to the typed parameter region within the internal buffer. */
	Type*
	operator->() const
	{
 		return fContent;
	}

	/** @brief Returns a raw pointer to the beginning of the serialised command
	 *         buffer (header + parameters).
	 *  @return Void pointer to the internal byte buffer. */
	void*
	Data() const
	{
		return (void*)fBuffer;
	}

	/** @brief Returns the total byte length of the serialised command
	 *         (header size plus parameter size).
	 *  @return Total size in bytes. */
	size_t Size() const
	{
		return HeaderSize + paramSize;
	}

private:
	char fBuffer[paramSize + HeaderSize];
	Type* fContent;
	struct hci_command_header* fHeader;
};


/** @brief Sends a parameter-less HCI command to the Bluetooth server and
 *         optionally retrieves an integer result from the reply.
 *  @param ofg       Opcode Group Field of the command.
 *  @param ocf       Opcode Command Field of the command.
 *  @param result    Optional pointer to store the integer result field from the
 *                   server reply; may be NULL.
 *  @param hId       HCI device identifier on which to issue the command.
 *  @param messenger Messenger targeting the Bluetooth server.
 *  @return B_OK on success, or a Bluetooth error code on failure. */
status_t
NonParameterCommandRequest(uint8 ofg, uint8 ocf, int32* result, hci_id hId,
	BMessenger* messenger);

/** @brief Sends a single-parameter HCI command to the Bluetooth server and
 *         optionally retrieves an integer result from the reply.
 *  @tparam PARAMETERCONTAINER Struct type that contains the single parameter field.
 *  @tparam PARAMETERTYPE      Type of the parameter value to assign.
 *  @param ofg       Opcode Group Field of the command.
 *  @param ocf       Opcode Command Field of the command.
 *  @param parameter The parameter value to embed in the command.
 *  @param result    Optional pointer to store the integer result field; may be NULL.
 *  @param hId       HCI device identifier on which to issue the command.
 *  @param messenger Messenger targeting the Bluetooth server.
 *  @return B_OK on success, or a Bluetooth error code on failure. */
template<typename PARAMETERCONTAINER, typename PARAMETERTYPE>
status_t
SingleParameterCommandRequest(uint8 ofg, uint8 ocf, PARAMETERTYPE parameter,
	int32* result, hci_id hId, BMessenger* messenger)
{
	int8 bt_status = BT_ERROR;

	BluetoothCommand<typed_command(PARAMETERCONTAINER)>
		simpleCommand(ofg, ocf);

	simpleCommand->param = parameter;

	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	BMessage reply;

	simpleCommand->param = parameter;

	request.AddInt32("hci_id", hId);
	request.AddData("raw command", B_ANY_TYPE, simpleCommand.Data(),
		simpleCommand.Size());
	request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
	request.AddInt16("opcodeExpected", PACK_OPCODE(ofg, ocf));

	if (messenger->SendMessage(&request, &reply) == B_OK) {
		reply.FindInt8("status", &bt_status);
		if (result != NULL)
			reply.FindInt32("result", result);
	}

	return bt_status;
}


/* CONTROL BASEBAND */

/** @brief Builds a raw HCI Reset command buffer.
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildReset(size_t* outsize);

/** @brief Builds a raw HCI Read Local Name command buffer.
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildReadLocalName(size_t* outsize);

/** @brief Builds a raw HCI Read Scan Enable command buffer.
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildReadScan(size_t* outsize);

/** @brief Builds a raw HCI Write Scan Enable command buffer.
 *  @param scanmode Bitmask specifying which scan modes to enable.
 *  @param outsize  Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildWriteScan(uint8 scanmode, size_t* outsize);

/** @brief Builds a raw HCI Read Class of Device command buffer.
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildReadClassOfDevice(size_t* outsize);

/* LINK CONTROL */

/** @brief Builds a raw HCI Remote Name Request command buffer.
 *  @param bdaddr        Bluetooth address of the target remote device.
 *  @param pscan_rep_mode Page scan repetition mode reported during inquiry.
 *  @param clock_offset  Clock offset from the inquiry result, or 0.
 *  @param outsize       Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildRemoteNameRequest(bdaddr_t bdaddr, uint8 pscan_rep_mode,
	uint16 clock_offset, size_t* outsize);

/** @brief Builds a raw HCI Inquiry command buffer.
 *  @param lap     Lower Address Part of the inquiry access code (e.g. 0x9E8B33).
 *  @param length  Inquiry duration in units of 1.28 seconds.
 *  @param num_rsp Maximum number of responses before the inquiry is halted (0 = unlimited).
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildInquiry(uint32 lap, uint8 length, uint8 num_rsp, size_t* outsize);

/** @brief Builds a raw HCI Inquiry Cancel command buffer.
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildInquiryCancel(size_t* outsize);

/** @brief Builds a raw HCI PIN Code Request Reply command buffer.
 *  @param bdaddr  Bluetooth address of the device requesting the PIN.
 *  @param length  Length of the PIN code in bytes (1-16).
 *  @param pincode The PIN code bytes.
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildPinCodeRequestReply(bdaddr_t bdaddr, uint8 length, char pincode[16],
	size_t* outsize);

/** @brief Builds a raw HCI PIN Code Request Negative Reply command buffer.
 *  @param bdaddr  Bluetooth address of the device whose PIN request is rejected.
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildPinCodeRequestNegativeReply(bdaddr_t bdaddr, size_t* outsize);

/** @brief Builds a raw HCI Accept Connection Request command buffer.
 *  @param bdaddr  Bluetooth address of the device whose connection is being accepted.
 *  @param role    Requested role after connection (0 = master, 1 = slave).
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildAcceptConnectionRequest(bdaddr_t bdaddr, uint8 role,
	size_t* outsize);

/** @brief Builds a raw HCI Reject Connection Request command buffer.
 *  @param bdaddr  Bluetooth address of the device whose connection is being rejected.
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildRejectConnectionRequest(bdaddr_t bdaddr, size_t* outsize);

/** @brief Builds a raw HCI IO Capability Request Reply command buffer.
 *  @param bdaddr         Bluetooth address of the remote device.
 *  @param capability     Local IO capability code.
 *  @param oob_data       Whether OOB authentication data is present (0 = no).
 *  @param authentication Authentication requirements bitmask.
 *  @param outsize        Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildIOCapabilityRequestReply(bdaddr_t bdaddr, uint8 capability, uint8 oob_data,
	uint8 authentication, size_t* outsize);

/** @brief Builds a raw HCI User Confirmation Request Reply command buffer.
 *  @param bdaddr  Bluetooth address of the remote device confirming the numeric value.
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildUserConfirmReply(bdaddr_t bdaddr, size_t* outsize);

/** @brief Builds a raw HCI Authentication Requested command buffer.
 *  @param handle  HCI connection handle for which authentication is requested.
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildAuthenticationRequested(uint16 handle, size_t* outsize);

/* OGF_INFORMATIONAL_PARAM */

/** @brief Builds a raw HCI Read Local Version Information command buffer.
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildReadLocalVersionInformation(size_t* outsize);

/** @brief Builds a raw HCI Read Buffer Size command buffer.
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildReadBufferSize(size_t* outsize);

/** @brief Builds a raw HCI Read BD_ADDR command buffer.
 *  @param outsize Receives the byte length of the returned buffer.
 *  @return Heap-allocated buffer containing the serialised command; caller must free. */
void* buildReadBdAddr(size_t* outsize);

#endif
