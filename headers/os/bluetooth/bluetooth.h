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
 *   Copyright 2008 Mika Lindqvist, monni1995_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file bluetooth.h
 *  @brief Core Bluetooth type definitions, version constants, and protocol numbers. */

#ifndef _BLUETOOTH_H
#define _BLUETOOTH_H

#include <ByteOrder.h>

/** @brief Bluetooth specification version 1.1b (pre-release). */
#define BLUETOOTH_1_1B		0
/** @brief Bluetooth specification version 1.1. */
#define BLUETOOTH_1_1		1
/** @brief Bluetooth specification version 1.2. */
#define BLUETOOTH_1_2		2
/** @brief Bluetooth specification version 2.0. */
#define BLUETOOTH_2_0		3

/** @brief The Bluetooth version targeted by this build. */
#define BLUETOOTH_VERSION	BLUETOOTH_2_0


/* Bluetooth common types */

/** @brief Bluetooth device address (BD_ADDR), packed as 6 bytes in little-endian order. */
typedef struct {
	uint8 b[6];
} __attribute__((packed)) bdaddr_t;


/** @brief Null (zero) Bluetooth device address. */
#define BDADDR_NULL			((bdaddr_t) {{0, 0, 0, 0, 0, 0}})
/** @brief Local loopback Bluetooth device address. */
#define BDADDR_LOCAL		((bdaddr_t) {{0, 0, 0, 0xff, 0xff, 0xff}})
/** @brief Broadcast Bluetooth device address (all bits set). */
#define BDADDR_BROADCAST	((bdaddr_t) {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}})
/** @brief Alias for the broadcast address; matches any remote device. */
#define BDADDR_ANY			BDADDR_BROADCAST


/** @brief Bluetooth link key, 16 bytes. */
typedef struct {
	uint8 l[16];
} __attribute__((packed)) linkkey_t;


/** @brief Protocol number for the HCI (Host Controller Interface) layer. */
#define BLUETOOTH_PROTO_HCI		134	/* HCI protocol number */
/** @brief Protocol number for the L2CAP (Logical Link Control and Adaptation Protocol) layer. */
#define BLUETOOTH_PROTO_L2CAP	135	/* L2CAP protocol number */
/** @brief Protocol number for the RFCOMM (Radio Frequency Communication) layer. */
#define BLUETOOTH_PROTO_RFCOMM	136	/* RFCOMM protocol number */

/** @brief Maximum protocol number value. */
#define BLUETOOTH_PROTO_MAX		256


#endif // _BLUETOOTH_H
