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

/** @file bluetooth_error.h
 *  @brief Bluetooth error code definitions as specified by Bluetooth V2.1 + EDR. */

#ifndef _BLUETOOTH_ERROR_H
#define _BLUETOOTH_ERROR_H

#include <Errors.h>


/** @brief Operation completed successfully. */
#define BT_OK								B_OK
/** @brief Generic Bluetooth error (alias for BT_UNSPECIFIED_ERROR). */
#define BT_ERROR							BT_UNSPECIFIED_ERROR

/* Official error code for Bluetooth V2.1 + EDR */
/** @brief HCI command is not known. */
#define BT_UNKNOWN_COMMAND					0x01
/** @brief No connection exists for the given handle. */
#define BT_NO_CONNECTION					0x02
/** @brief Hardware failure in the controller. */
#define BT_HARDWARE_FAILURE					0x03
/** @brief Page attempt timed out before a connection could be created. */
#define BT_PAGE_TIMEOUT						0x04
/** @brief Authentication with the remote device failed. */
#define BT_AUTHENTICATION_FAILURE			0x05
/** @brief PIN or link key is missing. */
#define BT_PIN_OR_KEY_MISSING				0x06
/** @brief Controller memory has been exhausted. */
#define BT_MEMORY_FULL						0x07
/** @brief Connection attempt timed out. */
#define BT_CONNECTION_TIMEOUT				0x08
/** @brief Maximum number of ACL connections has been reached. */
#define BT_MAX_NUMBER_OF_CONNECTIONS		0x09
/** @brief Maximum number of SCO connections to a single device has been reached. */
#define BT_MAX_NUMBER_OF_SCO_CONNECTIONS	0x0a
/** @brief An ACL connection already exists to the target device. */
#define BT_ACL_CONNECTION_EXISTS			0x0b
/** @brief The requested command is not permitted in the current state. */
#define BT_COMMAND_DISALLOWED				0x0c
/** @brief Connection rejected due to limited resources. */
#define BT_REJECTED_LIMITED_RESOURCES		0x0d
/** @brief Connection rejected for security reasons. */
#define BT_REJECTED_SECURITY				0x0e
/** @brief Connection rejected because the device is not on the personal list. */
#define BT_REJECTED_PERSONAL				0x0f
/** @brief Host-side connection attempt timed out. */
#define BT_HOST_TIMEOUT						0x10
/** @brief A feature or parameter value is not supported. */
#define BT_UNSUPPORTED_FEATURE				0x11
/** @brief One or more HCI command parameters are invalid. */
#define BT_INVALID_PARAMETERS				0x12
/** @brief Remote user terminated the connection. */
#define BT_REMOTE_USER_ENDED_CONNECTION		0x13
/** @brief Remote device terminated because of low resources. */
#define BT_REMOTE_LOW_RESOURCES				0x14
/** @brief Remote device terminated because it is powering off. */
#define BT_REMOTE_POWER_OFF					0x15
/** @brief Connection terminated by the local host. */
#define BT_CONNECTION_TERMINATED			0x16
/** @brief Too many repeated pairing attempts have been made. */
#define BT_REPEATED_ATTEMPTS				0x17
/** @brief Pairing is not allowed at this time. */
#define BT_PAIRING_NOT_ALLOWED				0x18
/** @brief An unknown LMP PDU was received. */
#define BT_UNKNOWN_LMP_PDU					0x19
/** @brief The remote device does not support the requested feature. */
#define BT_UNSUPPORTED_REMOTE_FEATURE		0x1a
/** @brief SCO offset values were rejected. */
#define BT_SCO_OFFSET_REJECTED				0x1b
/** @brief SCO interval values were rejected. */
#define BT_SCO_INTERVAL_REJECTED			0x1c
/** @brief SCO air mode was rejected. */
#define BT_AIR_MODE_REJECTED				0x1d
/** @brief LMP PDU contains invalid parameters. */
#define BT_INVALID_LMP_PARAMETERS			0x1e
/** @brief An unspecified error occurred. */
#define BT_UNSPECIFIED_ERROR				0x1f
/** @brief An LMP parameter value is not supported. */
#define BT_UNSUPPORTED_LMP_PARAMETER_VALUE	0x20
/** @brief A role change is not allowed at this time. */
#define BT_ROLE_CHANGE_NOT_ALLOWED			0x21
/** @brief LMP response timed out. */
#define BT_LMP_RESPONSE_TIMEOUT				0x22
/** @brief LMP error transaction collision occurred. */
#define BT_LMP_ERROR_TRANSACTION_COLLISION	0x23
/** @brief LMP PDU is not allowed in the current state. */
#define BT_LMP_PDU_NOT_ALLOWED				0x24
/** @brief Encryption mode is not acceptable. */
#define BT_ENCRYPTION_MODE_NOT_ACCEPTED		0x25
/** @brief Unit link key was used instead of a combination key. */
#define BT_UNIT_LINK_KEY_USED				0x26
/** @brief QoS is not supported. */
#define BT_QOS_NOT_SUPPORTED				0x27
/** @brief The LMP instant has already passed. */
#define BT_INSTANT_PASSED					0x28
/** @brief Pairing with unit key is not supported. */
#define BT_PAIRING_NOT_SUPPORTED			0x29
/** @brief LMP transaction collision. */
#define BT_TRANSACTION_COLLISION			0x2a
/** @brief QoS unacceptable parameter. */
#define BT_QOS_UNACCEPTABLE_PARAMETER		0x2c
/** @brief QoS request rejected. */
#define BT_QOS_REJECTED						0x2d
/** @brief Channel classification is not supported. */
#define BT_CLASSIFICATION_NOT_SUPPORTED		0x2e
/** @brief Insufficient security for the requested operation. */
#define BT_INSUFFICIENT_SECURITY			0x2f
/** @brief Parameter value is out of the mandatory range. */
#define BT_PARAMETER_OUT_OF_RANGE			0x30
/** @brief A role switch is currently pending. */
#define BT_ROLE_SWITCH_PENDING				0x32
/** @brief Reserved slot violation. */
#define BT_SLOT_VIOLATION					0x34
/** @brief Role switch failed. */
#define BT_ROLE_SWITCH_FAILED				0x35

/** @brief Extended Inquiry Response data is too large for the PDU. */
#define EXTENDED_INQUIRY_RESPONSE_TOO_LARGE	0x36
/** @brief Simple Pairing is not supported by the host. */
#define SIMPLE_PAIRING_NOT_SUPPORTED_BY_HOST 0x37
/** @brief The host is busy with pairing. */
#define HOST_BUSY_PAIRING					0x38


#endif // _BLUETOOTH_ERROR_H
