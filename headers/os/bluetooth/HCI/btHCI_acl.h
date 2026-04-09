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

/** @file btHCI_acl.h
 *  @brief HCI ACL data packet header structure and handle/flag pack/unpack macros. */

#ifndef _BTHCI_ACL_H_
#define _BTHCI_ACL_H_

#include <bluetooth/bluetooth.h>
#include <bluetooth/HCI/btHCI.h>

/** @brief Size of the HCI ACL data packet header in bytes. */
#define HCI_ACL_HDR_SIZE	4

/** @brief HCI ACL data packet header layout (packed). */
struct hci_acl_header {
	uint16 	handle;		/* Handle & Flags(PB, BC) */
	uint16 	alen;
} __attribute__ ((packed)) ;

/* ACL handle and flags pack/unpack */
/** @brief Packs a connection handle and PB/BC flags into the 16-bit ACL handle field.
 *  @param h  12-bit connection handle.
 *  @param pb 2-bit Packet Boundary flag.
 *  @param bc 2-bit Broadcast flag.
 *  @return Packed 16-bit value. */
#define pack_acl_handle_flags(h, pb, bc)	(((h) & 0x0fff) | (((pb) & 3) << 12) | (((bc) & 3) << 14))

/** @brief Extracts the 12-bit connection handle from a packed ACL handle field.
 *  @param h Packed 16-bit ACL handle field.
 *  @return 12-bit connection handle. */
#define get_acl_handle(h)					((h) & 0x0fff)

/** @brief Extracts the 2-bit Packet Boundary (PB) flag from a packed ACL handle field.
 *  @param h Packed 16-bit ACL handle field.
 *  @return 2-bit PB flag value. */
#define get_acl_pb_flag(h)					(((h) & 0x3000) >> 12)

/** @brief Extracts the 2-bit Broadcast (BC) flag from a packed ACL handle field.
 *  @param h Packed 16-bit ACL handle field.
 *  @return 2-bit BC flag value. */
#define get_acl_bc_flag(h)					(((h) & 0xc000) >> 14)

/* PB flag values */
/* 00 - reserved for future use */
/** @brief PB flag: continuing fragment of a higher-layer PDU. */
#define	HCI_ACL_PACKET_FRAGMENT		0x1
/** @brief PB flag: first non-automatically-flushable packet of a higher-layer PDU. */
#define	HCI_ACL_PACKET_START		0x2
/* 11 - reserved for future use */

/* BC flag values */
/** @brief BC flag: point-to-point packet (Host Controller to Host only). */
#define HCI_ACL_POINT2POINT			0x0 /* only Host controller to Host */
/** @brief BC flag: active broadcast to all active slaves. */
#define HCI_ACL_BROADCAST_ACTIVE	0x1 /* both directions */
/** @brief BC flag: piconet broadcast to all devices including parked. */
#define HCI_ACL_BROADCAST_PICONET	0x2 /* both directions */
										/* 11 - reserved for future use */

#endif // _BTHCI_ACL_H_
