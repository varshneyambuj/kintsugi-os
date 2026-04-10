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

/** @file BluetoothServer.h
 *  @brief Top-level Bluetooth daemon: owns local devices, the device manager, and the SDP thread. */

#ifndef _BLUETOOTH_SERVER_APP_H
#define _BLUETOOTH_SERVER_APP_H

#include <stdlib.h>

#include <Application.h>
#include <ObjectList.h>
#include <OS.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/HCI/btHCI.h>
#include <bluetooth/HCI/btHCI_transport.h>
#include <bluetooth/HCI/btHCI_command.h>

#include "HCIDelegate.h"
#include "DeviceManager.h"
#include "LocalDeviceImpl.h"

#include <PortListener.h>

/** @brief Trace prefix used by the Bluetooth server's debug output. */
#define BT "bluetooth_server: "

/** @brief Indices into the daemon's blackboard ports for inter-component messaging. */
typedef enum {
	BLACKBOARD_GENERAL = 0,        /**< Daemon-wide control messages. */
	BLACKBOARD_DEVICEMANAGER,      /**< Device manager events. */
	BLACKBOARD_KIT,                /**< Messages addressed to the user-space Bluetooth kit. */
	BLACKBOARD_SDP,                /**< Service discovery protocol messages. */
	// more blackboards
	BLACKBOARD_END
} BluetoothServerBlackBoardIndex;

/** @brief Maps an HCI device index @c X to its dedicated per-controller blackboard slot. */
#define BLACKBOARD_LD(X) (BLACKBOARD_END+X-HCI_DEVICE_INDEX_OFFSET)

/** @brief Owning list of LocalDeviceImpl instances managed by the daemon. */
typedef BObjectList<LocalDeviceImpl> LocalDevicesList;
/** @brief PortListener specialised for HCI event packets. */
typedef PortListener<struct hci_event_header,
	HCI_MAX_EVENT_SIZE, // Event Body can hold max 255 + 2 header
	24					// Some devices have sent chunks of 24 events(inquiry result)
	> BluetoothPortListener;

/** @brief BApplication subclass implementing the Bluetooth daemon.
 *
 * Discovers local controllers via the DeviceManager, instantiates a
 * LocalDeviceImpl for each, dispatches incoming HCI events through a
 * PortListener thread, runs the SDP server in a background thread, and
 * exposes a BMessage RPC surface to the userspace Bluetooth kit. */
class BluetoothServer : public BApplication
{
public:

	BluetoothServer();

	/** @brief Returns true once shutdown has been requested. */
	virtual bool QuitRequested(void);
	/** @brief Parses daemon command-line arguments. */
	virtual void ArgvReceived(int32 argc, char **argv);
	/** @brief Starts the device manager, port listener, and SDP thread. */
	virtual void ReadyToRun(void);


	/** @brief Reacts to BApplication activation events. */
	virtual void AppActivated(bool act);
	/** @brief Dispatches incoming kit RPC messages to the per-handler routines. */
	virtual void MessageReceived(BMessage *message);

	/** @brief Background thread entry point for the SDP server. */
	static int32 SDPServerThread(void* data);

	/* Messages reply */
	/** @brief Handles "how many local devices?" RPCs. */
	status_t	HandleLocalDevicesCount(BMessage* message, BMessage* reply);
	/** @brief Handles client requests to acquire a specific local device. */
	status_t    HandleAcquireLocalDevice(BMessage* message, BMessage* reply);

	/** @brief Handles read-property requests from clients. */
	status_t    HandleGetProperty(BMessage* message, BMessage* reply);
	/** @brief Handles simple HCI passthrough requests from clients. */
	status_t    HandleSimpleRequest(BMessage* message, BMessage* reply);


	/** @brief Looks up a LocalDeviceImpl by its HCI device id. */
    LocalDeviceImpl*    LocateLocalDeviceImpl(hci_id hid);

private:

	LocalDeviceImpl*	LocateDelegateFromMessage(BMessage* message);

	void 				ShowWindow(BWindow* pWindow);

	void				_InstallDeskbarIcon();
	void				_RemoveDeskbarIcon();

	LocalDevicesList   	fLocalDevicesList;     /**< Live LocalDeviceImpl instances, one per controller. */


	// Notification system
	BluetoothPortListener*	fEventListener2;   /**< Background thread that drains the HCI event port. */

	DeviceManager*			fDeviceManager;    /**< /dev watcher discovering controllers at runtime. */

	BPoint 					fCenter;           /**< Cached centre point used for centred dialogs. */

	thread_id				fSDPThreadID;      /**< Thread id of the SDP server thread. */

	bool					fIsShuttingDown;   /**< Set true once QuitRequested() has fired. */
};

#endif
