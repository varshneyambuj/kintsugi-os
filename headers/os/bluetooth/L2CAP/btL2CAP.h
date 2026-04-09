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

/** @file btL2CAP.h
 *  @brief L2CAP socket address structure for Bluetooth connection endpoints. */

#ifndef _BTL2CAP_H_
#define _BTL2CAP_H_

#include <bluetooth/bluetooth.h>

/** @brief Socket address structure for L2CAP (Logical Link Control and Adaptation Protocol) endpoints. */
struct sockaddr_l2cap {
	uint8		l2cap_len;		/**< Total length of this address structure in bytes. */
	uint8		l2cap_family;	/**< Address family identifier (AF_BLUETOOTH). */
	uint16		l2cap_psm;		/**< PSM (Protocol/Service Multiplexor) identifying the upper-layer protocol. */
	bdaddr_t	l2cap_bdaddr;	/**< Remote Bluetooth device address. */
};


#endif // _BTL2CAP_H_
