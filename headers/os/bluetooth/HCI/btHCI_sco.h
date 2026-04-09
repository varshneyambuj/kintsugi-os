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
 *   Copyright 2008 Mika Lindqvist, monni1995_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file btHCI_sco.h
 *  @brief HCI SCO (Synchronous Connection Oriented) data packet header structure. */

#ifndef _BTHCI_SCO_H_
#define _BTHCI_SCO_H_

#include <bluetooth/HCI/btHCI.h>

/** @brief Size of the HCI SCO data packet header in bytes. */
#define HCI_SCO_HDR_SIZE	3

/** @brief HCI SCO data packet header layout (packed). */
struct hci_sco_header {
	uint16	handle; /**< SCO connection handle (12 bits) and reserved flags (4 bits). */
	uint8	slen;   /**< Length of the SCO data payload in bytes. */
} __attribute__((packed));

#endif // _BTHCI_SCO_H_
