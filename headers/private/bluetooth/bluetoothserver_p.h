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

/** @file bluetoothserver_p.h
 *  @brief Private IPC message codes and application signatures for the
 *         Bluetooth server and kit communication layer. */

#ifndef _BLUETOOTH_SERVER_PRIVATE_H
#define _BLUETOOTH_SERVER_PRIVATE_H


/** @brief MIME signature of the Bluetooth server application. */
#define BLUETOOTH_SIGNATURE "application/x-vnd.Haiku-bluetooth_server"

/** @brief MIME signature of the Bluetooth preferences application. */
#define BLUETOOTH_APP_SIGNATURE "application/x-vnd.Haiku-BluetoothPrefs"

/* Kit Comunication */

/** @brief Message code: query the number of local Bluetooth devices. */
#define BT_MSG_COUNT_LOCAL_DEVICES		'btCd'

/** @brief Message code: acquire a handle to a local Bluetooth device. */
#define BT_MSG_ACQUIRE_LOCAL_DEVICE     'btAd'

/** @brief Message code: send a simple HCI request and wait for its reply. */
#define BT_MSG_HANDLE_SIMPLE_REQUEST    'btsR'

/** @brief Message code: add a device entry to the server's device registry. */
#define BT_MSG_ADD_DEVICE               'btDD'

/** @brief Message code: remove a device entry from the server's device registry. */
#define BT_MSG_REMOVE_DEVICE            'btrD'

/** @brief Message code: retrieve a named property from a local device. */
#define BT_MSG_GET_PROPERTY             'btgP'

// Discovery

/** @brief Notification code: an inquiry (device discovery) procedure has started. */
#define BT_MSG_INQUIRY_STARTED    'IqSt'

/** @brief Notification code: an inquiry procedure completed normally. */
#define BT_MSG_INQUIRY_COMPLETED  'IqCM'

/** @brief Notification code: an inquiry procedure was terminated by the host. */
#define BT_MSG_INQUIRY_TERMINATED 'IqTR'

/** @brief Notification code: an inquiry procedure ended with an error. */
#define BT_MSG_INQUIRY_ERROR      'IqER'

/** @brief Notification code: a remote device was discovered during inquiry. */
#define BT_MSG_INQUIRY_DEVICE     'IqDE'

#endif
