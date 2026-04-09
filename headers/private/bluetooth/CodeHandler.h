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
 *   (No explicit copyright header in the original file; part of the
 *   Haiku Bluetooth Kit private API.)
 */

/** @file CodeHandler.h
 *  @brief Static helper class for encoding and decoding 32-bit Bluetooth
 *         inter-layer message codes that carry device ID, handler, and
 *         packet-type fields. */

#ifndef _CODEHANDLER_H_
#define _CODEHANDLER_H_

#include <bluetooth/HCI/btHCI.h>

namespace Bluetooth
{

/** @brief Provides static accessors to pack and unpack the three sub-fields
 *         (device ID, handler index, and protocol type) embedded in a single
 *         32-bit message code used across the HCI, L2CAP, and transport layers.
 *
 *  The 32-bit code layout is:
 *  - bits 31..24  device (hci_id)
 *  - bits 23..16  protocol (bt_packet_t)
 *  - bits 15..0   handler (uint16)
 */
class CodeHandler {

public:
	/*
	 * TODO: Handler and protocol could be fit in 16 bits 12
	 * for handler and 4 for the protocol
	 */

	/*
	 * Used:
	 * - From HCI layer to send events to bluetooth server.
	 * - L2cap lower to dispatch TX packets to HCI Layer
	 * - informing about connection handle
	 * - Transport drivers dispatch its data to HCI layer
	 *
	 */

	/** @brief Extracts the HCI device identifier from a message code.
	 *  @param code The 32-bit encoded message code.
	 *  @return The hci_id stored in bits 31..24 of @p code. */
	static hci_id Device(uint32 code)
	{
		return ((code & 0xFF000000) >> 24);
	}


	/** @brief Embeds an HCI device identifier into a message code.
	 *  @param code   Pointer to the 32-bit message code to modify.
	 *  @param device The hci_id value to store in bits 31..24. */
	static void SetDevice(uint32* code, hci_id device)
	{
		*code = *code | ((device & 0xFF) << 24);
	}


	/** @brief Extracts the handler index from a message code.
	 *  @param code The 32-bit encoded message code.
	 *  @return The uint16 handler value stored in bits 15..0 of @p code. */
	static uint16 Handler(uint32 code)
	{
		return ((code & 0xFFFF) >> 0);
	}


	/** @brief Embeds a handler index into a message code.
	 *  @param code    Pointer to the 32-bit message code to modify.
	 *  @param handler The uint16 handler value to store in bits 15..0. */
	static void SetHandler(uint32* code, uint16 handler)
	{
		*code = *code | ((handler & 0xFFFF) << 0);
	}


	/** @brief Extracts the Bluetooth packet-type (protocol) field from a message code.
	 *  @param code The 32-bit encoded message code.
	 *  @return The bt_packet_t value stored in bits 23..16 of @p code. */
	static bt_packet_t Protocol(uint32 code)
	{
		return (bt_packet_t)((code & 0xFF0000) >> 16);
	}


	/** @brief Embeds a Bluetooth packet-type (protocol) field into a message code.
	 *  @param code     Pointer to the 32-bit message code to modify.
	 *  @param protocol The bt_packet_t value to store in bits 23..16. */
	static void SetProtocol(uint32* code, bt_packet_t protocol)
	{
		*code = *code | ((protocol & 0xFF) << 16);
	}


};


} // namespace


#endif
