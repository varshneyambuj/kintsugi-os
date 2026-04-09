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
 *   Copyright 2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file btCoreData.h
 *  @brief Core Bluetooth data structures and the btCoreData kernel module
 *         interface for managing HCI connections. */

#ifndef _BTCOREDATA_H
#define _BTCOREDATA_H


#include <module.h>
#include <lock.h>
#include <util/DoublyLinkedList.h>
#include <util/VectorMap.h>
#include <net_datalink.h>
#include <net/if_dl.h>


#include <net_buffer.h>
#include <net_device.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/HCI/btHCI.h>
#include <bluetooth/HCI/btHCI_transport.h>
#include <l2cap.h>


/** @brief Module name string used to locate the btCoreData kernel module. */
#define BT_CORE_DATA_MODULE_NAME "bluetooth/btCoreData/v1"


/** @brief Lifecycle state of an HCI ACL connection. */
typedef enum _connection_status {
	HCI_CONN_CLOSED, /**< Connection is not established. */
	HCI_CONN_OPEN,   /**< Connection is active and ready for data transfer. */
} connection_status;


#ifdef __cplusplus

/** @brief Represents a single HCI ACL connection to a remote Bluetooth device,
 *         including all associated network interface addresses, buffering state,
 *         and L2CAP signalling context. */
struct HciConnection : DoublyLinkedListLinkImpl<HciConnection> {
	/** @brief Constructs an HciConnection associated with the given HCI device ID.
	 *  @param hid The HCI device identifier for the local controller. */
	HciConnection(hci_id hid);

	/** @brief Destructs the HciConnection, releasing any associated resources. */
	virtual ~HciConnection();

	hci_id				Hid;                  /**< Local HCI device identifier. */
	bluetooth_device*	ndevice;              /**< Pointer to the underlying Bluetooth device. */
	bdaddr_t			destination;          /**< Bluetooth address of the remote device. */
	uint16				handle;               /**< HCI connection handle assigned by the controller. */
	int					type;                 /**< Link type (e.g. ACL or SCO). */
	uint16				mtu;                  /**< Maximum transmission unit for this connection. */
	connection_status	status;               /**< Current lifecycle state of the connection. */

	net_buffer*			currentRxPacket;           /**< Partially reassembled inbound packet, or NULL. */
	ssize_t				currentRxExpectedLength;   /**< Total expected byte length of the current inbound packet. */

	struct net_interface_address interface_address; /**< Network interface address record for this connection. */
	struct sockaddr_dl address_dl;                  /**< Datalink-layer socket address. */
	struct sockaddr_storage address_dest;           /**< Remote destination socket address storage. */

	/** @brief Optional hook called when the connection is disconnected.
	 *  @param conn Pointer to the HciConnection being disconnected. */
	void (*disconnect_hook)(HciConnection*);

public:
	mutex			fLock;          /**< Mutex protecting concurrent access to this connection. */
	uint8			fNextIdent;     /**< Next L2CAP command identifier to be allocated. */
	VectorMap<uint8, void*> fInUseIdents; /**< Map of in-use L2CAP command identifiers to associated data. */
};

#else

struct HciConnection;

#endif


/** @brief Module interface exported by the btCoreData kernel module, providing
 *         functions to create, look up, route, and destroy HCI connections, as
 *         well as to manage per-connection L2CAP command identifiers. */
struct bluetooth_core_data_module_info {
	module_info info;

	/** @brief Posts a raw HCI event buffer from a device up to the Bluetooth stack.
	 *  @param ndev   The Bluetooth device that generated the event.
	 *  @param event  Pointer to the raw event data.
	 *  @param size   Size of the event data in bytes.
	 *  @return B_OK on success, or a negative error code. */
	status_t				(*PostEvent)(bluetooth_device* ndev, void* event,
								size_t size);

	// FIXME: We really shouldn't be passing out connection pointers at all...
	/** @brief Creates and registers a new HCI connection record.
	 *  @param handle HCI connection handle.
	 *  @param type   Link type (ACL or SCO).
	 *  @param dst    Bluetooth address of the remote device.
	 *  @param hid    Local HCI device identifier.
	 *  @return Pointer to the newly created HciConnection, or NULL on failure. */
	struct HciConnection*	(*AddConnection)(uint16 handle, int type,
								const bdaddr_t& dst, hci_id hid);

	// status_t				(*RemoveConnection)(bdaddr_t destination, hci_id hid);
	/** @brief Removes an existing HCI connection record by handle.
	 *  @param handle HCI connection handle to remove.
	 *  @param hid    Local HCI device identifier.
	 *  @return B_OK on success, or a negative error code. */
	status_t				(*RemoveConnection)(uint16 handle, hci_id hid);

	/** @brief Determines which local HCI device should route traffic to a
	 *         given remote Bluetooth address.
	 *  @param destination Bluetooth address of the remote peer.
	 *  @return The hci_id of the local device that has an open connection
	 *          to @p destination, or a negative value if none exists. */
	hci_id					(*RouteConnection)(const bdaddr_t& destination);

	/** @brief Looks up an HCI connection by its connection handle and device ID.
	 *  @param handle HCI connection handle.
	 *  @param hid    Local HCI device identifier.
	 *  @return Pointer to the matching HciConnection, or NULL if not found. */
	struct HciConnection*	(*ConnectionByHandle)(uint16 handle, hci_id hid);

	/** @brief Looks up an HCI connection by the remote device address and device ID.
	 *  @param destination Bluetooth address of the remote peer.
	 *  @param hid         Local HCI device identifier.
	 *  @return Pointer to the matching HciConnection, or NULL if not found. */
	struct HciConnection*	(*ConnectionByDestination)(
								const bdaddr_t& destination, hci_id hid);

	/** @brief Allocates a unique L2CAP command identifier for a connection.
	 *  @param conn       The HCI connection context.
	 *  @param associated Opaque pointer to associate with this identifier.
	 *  @return An unused 8-bit command identifier. */
	uint8					(*allocate_command_ident)(struct HciConnection* conn, void* associated);

	/** @brief Retrieves the data associated with an allocated command identifier.
	 *  @param conn  The HCI connection context.
	 *  @param ident The command identifier previously returned by allocate_command_ident.
	 *  @return The associated data pointer, or NULL if the identifier is not in use. */
	void*					(*lookup_command_ident)(struct HciConnection* conn, uint8 ident);

	/** @brief Releases a previously allocated command identifier, making it available
	 *         for reuse.
	 *  @param conn  The HCI connection context.
	 *  @param ident The command identifier to free. */
	void					(*free_command_ident)(struct HciConnection* conn, uint8 ident);
};


/** @brief Returns true if an open connection to @p destination exists on device @p hid.
 *  @param destination Bluetooth address to search for.
 *  @param hid         Local HCI device identifier.
 *  @return true if a matching connection exists, false otherwise. */
inline bool ExistConnectionByDestination(const bdaddr_t& destination,
				hci_id hid);

/** @brief Returns true if an open connection with @p handle exists on device @p hid.
 *  @param handle HCI connection handle to search for.
 *  @param hid    Local HCI device identifier.
 *  @return true if a matching connection exists, false otherwise. */
inline bool ExistConnectionByHandle(uint16 handle, hci_id hid);


#endif // _BTCOREDATA_H
