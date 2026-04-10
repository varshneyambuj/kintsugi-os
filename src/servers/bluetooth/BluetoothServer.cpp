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
 *   Copyright 2007-2009 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   Copyright 2008 Mika Lindqvist, monni1995_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file BluetoothServer.cpp
 *  @brief Bluetooth daemon implementation: device discovery, RPC dispatch, SDP thread, deskbar item. */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>

#include <Entry.h>
#include <Deskbar.h>
#include <Directory.h>
#include <Message.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>
#include <Window.h>

#include <TypeConstants.h>
#include <syslog.h>

#include <bluetoothserver_p.h>
#include <bluetooth/HCI/btHCI_command.h>
#include <bluetooth/L2CAP/btL2CAP.h>
#include <bluetooth/bluetooth.h>

#include "BluetoothServer.h"
#include "DeskbarReplicant.h"
#include "LocalDeviceImpl.h"
#include "Debug.h"


/**
 * @brief Port-listener callback that routes an incoming HCI event to its local device.
 *
 * Extracts the device ID from the port code, locates the corresponding
 * LocalDeviceImpl, and delegates to its HandleEvent() method.  Non-event
 * frame types are silently ignored.
 *
 * @param header Pointer to the raw HCI event header.
 * @param code   Port code encoding the frame type and HCI device ID.
 * @param size   Total size of the event packet in bytes.
 * @return B_OK unconditionally (errors are logged, not propagated).
 */
status_t
DispatchEvent(struct hci_event_header* header, int32 code, size_t size)
{
	// we only handle events
	if (GET_PORTCODE_TYPE(code)!= BT_EVENT) {
		TRACE_BT("BluetoothServer: Wrong type frame code\n");
		return B_OK;
	}

	// fetch the LocalDevice who belongs this event
	LocalDeviceImpl* lDeviceImplementation = ((BluetoothServer*)be_app)->
		LocateLocalDeviceImpl(GET_PORTCODE_HID(code));

	if (lDeviceImplementation == NULL) {
		TRACE_BT("BluetoothServer: LocalDevice could not be fetched\n");
		return B_OK;
	}

	lDeviceImplementation->HandleEvent(header);

	return B_OK;
}


/**
 * @brief Construct the Bluetooth server application.
 *
 * Allocates the DeviceManager, initialises the local-devices list, and
 * creates the BluetoothPortListener that will receive HCI events from the
 * kernel transport layer.
 */
BluetoothServer::BluetoothServer()
	:
	BApplication(BLUETOOTH_SIGNATURE),
	fSDPThreadID(-1),
	fIsShuttingDown(false)
{
	fDeviceManager = new DeviceManager();
	fLocalDevicesList.MakeEmpty();

	fEventListener2 = new BluetoothPortListener(BT_USERLAND_PORT_NAME,
		(BluetoothPortListener::port_listener_func)&DispatchEvent);
}


/**
 * @brief Perform an orderly shutdown of the Bluetooth server.
 *
 * Deletes all registered LocalDeviceImpl instances, removes the Deskbar
 * tray icon, waits for the SDP server thread to exit, and destroys the
 * event listener.
 *
 * @return The result of BApplication::QuitRequested().
 */
bool BluetoothServer::QuitRequested(void)
{
	LocalDeviceImpl* lDeviceImpl = NULL;
	while ((lDeviceImpl = (LocalDeviceImpl*)
		fLocalDevicesList.RemoveItemAt(0)) != NULL)
		delete lDeviceImpl;

	_RemoveDeskbarIcon();

	// stop the SDP server thread
	fIsShuttingDown = true;

	status_t threadReturnStatus;
	wait_for_thread(fSDPThreadID, &threadReturnStatus);
	TRACE_BT("BluetoothServer server thread exited with: %s\n",
		strerror(threadReturnStatus));

	delete fEventListener2;
	TRACE_BT("Shutting down bluetooth_server.\n");

	return BApplication::QuitRequested();
}


/**
 * @brief Handle command-line arguments passed to the server.
 *
 * Recognises "--finish" to initiate a graceful shutdown.
 *
 * @param argc Number of arguments.
 * @param argv Argument vector.
 */
void BluetoothServer::ArgvReceived(int32 argc, char **argv)
{
	if (argc > 1) {
		if (strcmp(argv[1], "--finish") == 0)
			PostMessage(B_QUIT_REQUESTED);
	}
}


/**
 * @brief Complete server initialisation once the message loop is running.
 *
 * Starts monitoring /dev/bluetooth transport directories (h2-h5), launches
 * the HCI event listener, installs the Deskbar tray icon, and spawns the
 * SDP server thread.
 */
void BluetoothServer::ReadyToRun(void)
{
	fDeviceManager->StartMonitoringDevice("bluetooth/h2");
	fDeviceManager->StartMonitoringDevice("bluetooth/h3");
	fDeviceManager->StartMonitoringDevice("bluetooth/h4");
	fDeviceManager->StartMonitoringDevice("bluetooth/h5");

	if (fEventListener2->Launch() != B_OK)
		TRACE_BT("General: Bluetooth event listener failed\n");
	else
		TRACE_BT("General: Bluetooth event listener Ready\n");

	_InstallDeskbarIcon();

	// Spawn the SDP server thread
	fSDPThreadID = spawn_thread(SDPServerThread, "SDP server thread",
		B_NORMAL_PRIORITY, this);

#define _USE_FAKE_SDP_SERVER
#ifdef _USE_FAKE_SDP_SERVER
	if (fSDPThreadID <= 0 || resume_thread(fSDPThreadID) != B_OK) {
		TRACE_BT("BluetoothServer: Failed launching the SDP server thread\n");
	}
#endif
}


/**
 * @brief Hook called when the application gains or loses focus.
 *
 * Currently used only for diagnostic output.
 *
 * @param act true if the application was activated, false if deactivated.
 */
void BluetoothServer::AppActivated(bool act)
{
	printf("Activated %d\n",act);
}


/**
 * @brief Central message dispatcher for the Bluetooth server.
 *
 * Handles device addition/removal, local-device count queries, device
 * acquisition, simple HCI requests, property queries, and application
 * launch notifications.  Synchronous replies are sent when the status
 * is not B_WOULD_BLOCK.
 *
 * @param message The incoming BMessage from clients or internal components.
 */
void BluetoothServer::MessageReceived(BMessage* message)
{
	BMessage reply;
	status_t status = B_WOULD_BLOCK; // mark somehow to do not reply anything

	switch (message->what)
	{
		case BT_MSG_ADD_DEVICE:
		{
			BString str;
			message->FindString("name", &str);

			TRACE_BT("BluetoothServer: Requested LocalDevice %s\n", str.String());

			BPath path(str.String());

			LocalDeviceImpl* lDeviceImpl
				= LocalDeviceImpl::CreateTransportAccessor(&path);

			if (lDeviceImpl->GetID() >= 0) {
				fLocalDevicesList.AddItem(lDeviceImpl);

				TRACE_BT("LocalDevice %s id=%" B_PRId32 " added\n", str.String(),
					lDeviceImpl->GetID());
			} else {
				TRACE_BT("BluetoothServer: Adding LocalDevice hci id invalid\n");
			}

			status = B_WOULD_BLOCK;
			/* TODO: This should be by user request only! */
			lDeviceImpl->Launch();
			break;
		}

		case BT_MSG_REMOVE_DEVICE:
		{
			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			if (lDeviceImpl != NULL) {
				fLocalDevicesList.RemoveItem(lDeviceImpl);
				delete lDeviceImpl;
			}
			break;
		}

		case BT_MSG_COUNT_LOCAL_DEVICES:
			status = HandleLocalDevicesCount(message, &reply);
			break;

		case BT_MSG_ACQUIRE_LOCAL_DEVICE:
			status = HandleAcquireLocalDevice(message, &reply);
			break;

		case BT_MSG_HANDLE_SIMPLE_REQUEST:
			status = HandleSimpleRequest(message, &reply);
			break;

		case BT_MSG_GET_PROPERTY:
			status = HandleGetProperty(message, &reply);
			break;

		// Handle if the bluetooth preferences is running?
		case B_SOME_APP_LAUNCHED:
		{
			const char* signature;

			if (message->FindString("be:signature", &signature) == B_OK) {
				printf("input_server : %s\n", signature);
				if (strcmp(signature, "application/x-vnd.Be-TSKB") == 0) {

				}
			}
			return;
		}

		default:
			BApplication::MessageReceived(message);
			break;
	}

	// Can we reply right now?
	// TOD: review this condition
	if (status != B_WOULD_BLOCK) {
		reply.AddInt32("status", status);
		message->SendReply(&reply);
//		printf("Sending reply message for->\n");
//		message->PrintToStream();
	}
}


#if 0
#pragma mark -
#endif

/**
 * @brief Extract the "hci_id" field from a message and look up the device.
 *
 * @param message The BMessage containing an "hci_id" int32 field.
 * @return The matching LocalDeviceImpl, or NULL if the field is missing or
 *         no device with that ID exists.
 */
LocalDeviceImpl*
BluetoothServer::LocateDelegateFromMessage(BMessage* message)
{
	hci_id hid;

	if (message->FindInt32("hci_id", &hid) != B_OK)
		return NULL;

	return LocateLocalDeviceImpl(hid);
}


/**
 * @brief Find a LocalDeviceImpl by its HCI device ID.
 *
 * Performs a linear search through fLocalDevicesList.
 *
 * @param hid The kernel-assigned HCI device identifier.
 * @return The matching LocalDeviceImpl, or NULL if not found.
 */
LocalDeviceImpl*
BluetoothServer::LocateLocalDeviceImpl(hci_id hid)
{
	// Try to find out when a ID was specified
	int index;

	for (index = 0; index < fLocalDevicesList.CountItems(); index++) {
		LocalDeviceImpl* lDeviceImpl = fLocalDevicesList.ItemAt(index);
		if (lDeviceImpl->GetID() == hid)
			return lDeviceImpl;
	}

	return NULL;
}


#if 0
#pragma - Messages reply
#endif

/**
 * @brief Reply with the number of registered local Bluetooth devices.
 *
 * @param message The incoming request (unused beyond routing).
 * @param reply   The outgoing reply; a "count" int32 field is added.
 * @return The result of BMessage::AddInt32().
 */
status_t
BluetoothServer::HandleLocalDevicesCount(BMessage* message, BMessage* reply)
{
	TRACE_BT("BluetoothServer: count requested\n");

	return reply->AddInt32("count", fLocalDevicesList.CountItems());
}


/**
 * @brief Acquire an available local Bluetooth device for a client.
 *
 * Attempts to match a device by HCI ID, Bluetooth address, or simply the
 * next available one.  On success the device is marked as acquired and its
 * HCI ID is returned in the reply.  Uses a round-robin static index so
 * successive unqualified requests cycle through the device list.
 *
 * @param message The request, optionally containing "hci_id" or "bdaddr".
 * @param reply   The outgoing reply; "hci_id" is added on success.
 * @return B_OK on success, B_ERROR if no device could be acquired.
 */
status_t
BluetoothServer::HandleAcquireLocalDevice(BMessage* message, BMessage* reply)
{
	hci_id hid;
	ssize_t size;
	bdaddr_t bdaddr;
	LocalDeviceImpl* lDeviceImpl = NULL;
	static int32 lastIndex = 0;

	if (message->FindInt32("hci_id", &hid) == B_OK)	{
		TRACE_BT("BluetoothServer: GetLocalDevice requested with id\n");
		lDeviceImpl = LocateDelegateFromMessage(message);

	} else if (message->FindData("bdaddr", B_ANY_TYPE,
		(const void**)&bdaddr, &size) == B_OK) {

		// Try to find out when the user specified the address
		TRACE_BT("BluetoothServer: GetLocalDevice requested with bdaddr\n");
		for (lastIndex = 0; lastIndex < fLocalDevicesList.CountItems();
			lastIndex ++) {
			// TODO: Only possible if the property is available
			// bdaddr_t local;
			// lDeviceImpl = fLocalDevicesList.ItemAt(lastIndex);
			// if ((lDeviceImpl->GetAddress(&local, message) == B_OK)
			// 	&& bacmp(&local, &bdaddr)) {
			// 	break;
			// }
		}

	} else {
		// Careless, any device not performing operations will be fine
		TRACE_BT("BluetoothServer: GetLocalDevice plain request\n");
		// from last assigned till end
		for (int index = lastIndex + 1;
			index < fLocalDevicesList.CountItems();	index++) {
			lDeviceImpl= fLocalDevicesList.ItemAt(index);
			if (lDeviceImpl != NULL && lDeviceImpl->Available()) {
				printf("Requested local device %" B_PRId32 "\n",
					lDeviceImpl->GetID());
				TRACE_BT("BluetoothServer: Device available: %" B_PRId32 "\n", lDeviceImpl->GetID());
				lastIndex = index;
				break;
			}
		}

		// from starting till last assigned if not yet found
		if (lDeviceImpl == NULL) {
			for (int index = 0; index <= lastIndex ; index ++) {
				lDeviceImpl = fLocalDevicesList.ItemAt(index);
				if (lDeviceImpl != NULL && lDeviceImpl->Available()) {
					printf("Requested local device %" B_PRId32 "\n",
						lDeviceImpl->GetID());
					TRACE_BT("BluetoothServer: Device available: %" B_PRId32 "\n", lDeviceImpl->GetID());
					lastIndex = index;
					break;
				}
			}
		}
	}

	if (lastIndex <= fLocalDevicesList.CountItems() && lDeviceImpl != NULL
		&& lDeviceImpl->Available()) {

		hid = lDeviceImpl->GetID();
		lDeviceImpl->Acquire();

		TRACE_BT("BluetoothServer: Device acquired %" B_PRId32 "\n", hid);
		return reply->AddInt32("hci_id", hid);
	}

	return B_ERROR;

}


/**
 * @brief Forward a simple HCI command request to the target local device.
 *
 * If the request includes a "property" field and that property is already
 * cached, the reply is filled immediately.  Otherwise the raw HCI command
 * embedded in the message is issued through the device delegate, and the
 * reply will arrive asynchronously via the event handler.
 *
 * @param message The request, containing "hci_id", optional "property",
 *                and "raw command" data.
 * @param reply   The outgoing reply, populated with cached properties when
 *                available.
 * @return B_OK if the property was cached, B_WOULD_BLOCK if the command
 *         was issued and a reply will come later, or B_ERROR on failure.
 */
status_t
BluetoothServer::HandleSimpleRequest(BMessage* message, BMessage* reply)
{
	LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
	if (lDeviceImpl == NULL) {
		return B_ERROR;
	}

	const char* propertyRequested;

	// Find out if there is a property being requested,
	if (message->FindString("property", &propertyRequested) == B_OK) {
		// Check if the property has been already retrieved
		if (lDeviceImpl->IsPropertyAvailable(propertyRequested)) {
			// Dump everything
			reply->AddMessage("properties", lDeviceImpl->GetPropertiesMessage());
			return B_OK;
		}
	}

	// we are gonna need issue the command ...
	if (lDeviceImpl->ProcessSimpleRequest(DetachCurrentMessage()) == B_OK)
		return B_WOULD_BLOCK;
	else {
		lDeviceImpl->Unregister();
		return B_ERROR;
	}

}


/**
 * @brief Retrieve a cached device property and pack it into the reply.
 *
 * Looks up the requested "property" field name in the target device's
 * cached properties and stores the value in the reply's "result" field.
 * Supports 1-byte, 2-byte, and boolean property types.
 *
 * @param message The request, containing "hci_id" and "property" fields.
 * @param reply   The outgoing reply; "result" is added if the property
 *                is available.
 * @return B_OK in all cases (the client inspects "result" instead).
 */
status_t
BluetoothServer::HandleGetProperty(BMessage* message, BMessage* reply)
{
	// User side will look for the reply in a result field and will
	// not care about status fields, therefore we return OK in all cases

	LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
	if (lDeviceImpl == NULL) {
		return B_ERROR;
	}

	const char* propertyRequested;

	// Find out if there is a property being requested,
	if (message->FindString("property", &propertyRequested) == B_OK) {

		TRACE_BT("BluetoothServer: Searching %s property...\n", propertyRequested);

		// Check if the property has been already retrieved
		if (lDeviceImpl->IsPropertyAvailable(propertyRequested)) {

			// 1 bytes requests
			if (strcmp(propertyRequested, "hci_version") == 0
				|| strcmp(propertyRequested, "lmp_version") == 0
				|| strcmp(propertyRequested, "sco_mtu") == 0) {

				uint8 result = lDeviceImpl->GetPropertiesMessage()->
					FindInt8(propertyRequested);
				reply->AddInt32("result", result);

			// 2 bytes requests
			} else if (strcmp(propertyRequested, "hci_revision") == 0
					|| strcmp(propertyRequested, "lmp_subversion") == 0
					|| strcmp(propertyRequested, "manufacturer") == 0
					|| strcmp(propertyRequested, "acl_mtu") == 0
					|| strcmp(propertyRequested, "acl_max_pkt") == 0
					|| strcmp(propertyRequested, "sco_max_pkt") == 0
					|| strcmp(propertyRequested, "packet_type") == 0 ) {

				uint16 result = lDeviceImpl->GetPropertiesMessage()->
					FindInt16(propertyRequested);
				reply->AddInt32("result", result);

			// 1 bit requests
			} else if (strcmp(propertyRequested, "role_switch_capable") == 0
					|| strcmp(propertyRequested, "encrypt_capable") == 0) {

				bool result = lDeviceImpl->GetPropertiesMessage()->
					FindBool(propertyRequested);

				reply->AddInt32("result", result);



			} else {
				TRACE_BT("BluetoothServer: Property %s could not be satisfied\n", propertyRequested);
			}
		}
	}

	return B_OK;
}


#if 0
#pragma mark -
#endif

/**
 * @brief Thread function that runs a minimal SDP (Service Discovery Protocol) server.
 *
 * Opens an L2CAP socket bound to PSM 1 (SDP), listens for incoming
 * connections, and reads data from each client until the connection drops.
 * Runs in a loop until the server sets fIsShuttingDown.
 *
 * @param data Pointer to the BluetoothServer instance (cast to void*).
 * @return B_NO_ERROR on clean shutdown, or an error status if socket
 *         setup fails.
 */
int32
BluetoothServer::SDPServerThread(void* data)
{
	const BluetoothServer* server = (BluetoothServer*)data;

	// Set up the SDP socket
	struct sockaddr_l2cap loc_addr = { 0 };
	int socketServer;
	int client;
	status_t status;
	char buffer[512] = "";

	TRACE_BT("SDP: SDP server thread up...\n");

	socketServer = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);

	if (socketServer < 0) {
		TRACE_BT("SDP: Could not create server socket ...\n");
		return B_ERROR;
	}

	// bind socket to port 0x1001 of the first available
	// bluetooth adapter
	loc_addr.l2cap_family = AF_BLUETOOTH;
	loc_addr.l2cap_bdaddr = BDADDR_ANY;
	loc_addr.l2cap_psm = B_HOST_TO_LENDIAN_INT16(1);
	loc_addr.l2cap_len = sizeof(struct sockaddr_l2cap);

	status = bind(socketServer, (struct sockaddr*)&loc_addr,
		sizeof(struct sockaddr_l2cap));

	if (status < 0) {
		TRACE_BT("SDP: Could not bind server socket (%s)...\n", strerror(status));
		return status;
	}

	// setsockopt(sock, SOL_L2CAP, SO_L2CAP_OMTU, &omtu, len );
	// getsockopt(sock, SOL_L2CAP, SO_L2CAP_IMTU, &omtu, &len );

	// Listen for up to 10 connections
	status = listen(socketServer, 10);

	if (status != B_OK) {
		TRACE_BT("SDP: Could not listen server socket (%s)...\n", strerror(status));
		return status;
	}

	while (!server->fIsShuttingDown) {

		TRACE_BT("SDP: Waiting connection for socket (%s)...\n", strerror(status));

		uint len = sizeof(struct sockaddr_l2cap);
		client = accept(socketServer, (struct sockaddr*)&loc_addr, &len);

		TRACE_BT("SDP: Incomming connection... %d\n", client);

		ssize_t receivedSize;

		do {
			receivedSize = recv(client, buffer, 29 , 0);
			if (receivedSize < 0)
				TRACE_BT("SDP: Error reading client socket\n");
			else {
				TRACE_BT("SDP: Received from SDP client: %ld:\n", receivedSize);
				for (int i = 0; i < receivedSize ; i++)
					TRACE_BT("SDP: %x:", buffer[i]);

				TRACE_BT("\n");
			}
		} while (receivedSize >= 0);

		snooze(5000000);
		TRACE_BT("SDP: Waiting for next connection...\n");
	}

	// Close the socket
	close(socketServer);

	return B_NO_ERROR;
}


/**
 * @brief Show or activate the given window.
 *
 * If the window is hidden it is shown; otherwise it is brought to the
 * front and activated.
 *
 * @param pWindow The window to display.
 */
void
BluetoothServer::ShowWindow(BWindow* pWindow)
{
	pWindow->Lock();
	if (pWindow->IsHidden())
		pWindow->Show();
	else
		pWindow->Activate();
	pWindow->Unlock();
}


/**
 * @brief Install the Bluetooth tray icon into the Deskbar.
 *
 * Removes any existing instance first, then adds a new replicant
 * referencing this application's executable.
 */
void
BluetoothServer::_InstallDeskbarIcon()
{
	app_info appInfo;
	be_app->GetAppInfo(&appInfo);

	BDeskbar deskbar;

	if (deskbar.HasItem(kDeskbarItemName)) {
		_RemoveDeskbarIcon();
	}

	status_t res = deskbar.AddItem(&appInfo.ref);
	if (res != B_OK)
		TRACE_BT("Failed adding deskbar icon: %" B_PRId32 "\n", res);
}


/** @brief Remove the Bluetooth tray icon from the Deskbar. */
void
BluetoothServer::_RemoveDeskbarIcon()
{
	BDeskbar deskbar;
	status_t res = deskbar.RemoveItem(kDeskbarItemName);
	if (res != B_OK)
		TRACE_BT("Failed removing Deskbar icon: %" B_PRId32 ": \n", res);
}


#if 0
#pragma mark -
#endif

/**
 * @brief Entry point: create and run the Bluetooth server application.
 */
int
main(int /*argc*/, char** /*argv*/)
{
	BluetoothServer* bluetoothServer = new BluetoothServer;

	bluetoothServer->Run();
	delete bluetoothServer;

	return 0;
}

