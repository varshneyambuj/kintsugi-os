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

/** @file btHCI_transport.h
 *  @brief HCI transport driver interface: IOCTLs, statistics, device info, hooks, and bus manager. */

#ifndef _BTHCI_TRANSPORT_H_
#define _BTHCI_TRANSPORT_H_


#include <bluetooth/HCI/btHCI.h>

#include <Drivers.h>
#include <sys/socket.h>
#include <util/DoublyLinkedList.h>

#ifdef _KERNEL_MODE
#include <net_buffer.h>
#endif


/* Here the transport driver have some flags that
 * can be used to inform the upper layer about some
 * special behaouvior to perform */

/** @brief Transport flag: the HCI stack should ignore this device entirely. */
#define BT_IGNORE_THIS_DEVICE	(1 << 0)
/** @brief Transport flag: SCO transport is known to be broken on this device. */
#define BT_SCO_NOT_WORKING		(1 << 1)
/** @brief Transport flag: the controller requires a reset before use. */
#define BT_WILL_NEED_A_RESET	(1 << 2)
/** @brief Transport flag: device is a Digianswer adapter requiring special handling. */
#define BT_DIGIANSWER			(1 << 4)

// Mandatory IOCTLS
/** @brief Base offset for Bluetooth IOCTL codes (added to B_DEVICE_OP_CODES_END). */
#define BT_IOCTLS_OFFSET 3000

/** @brief Bluetooth transport driver IOCTL commands. */
enum {
	ISSUE_BT_COMMAND = B_DEVICE_OP_CODES_END + BT_IOCTLS_OFFSET, /**< Send an HCI command to the controller. */
	GET_STATS,            /**< Retrieve traffic statistics (bt_hci_statistics). */
	GET_NOTIFICATION_PORT, /**< Retrieve the kernel notification port handle. */
	GET_HCI_ID,           /**< Retrieve the hci_id assigned to this device. */
	BT_UP                 /**< Signal that the transport is ready for operation. */
};

// To deprecate ...
/** @brief Packs a packet type, hci_id, and data value into a single 32-bit port code.
 *  @param type 8-bit packet type.
 *  @param hid  8-bit HCI device identifier.
 *  @param data 16-bit data payload.
 *  @return Packed 32-bit port code. */
#define PACK_PORTCODE(type,hid,data) ((type & 0xFF) << 24 | (hid & 0xFF) << 16 | (data & 0xFFFF))

/** @brief Extracts the packet type from a packed port code.
 *  @param code 32-bit packed port code.
 *  @return 8-bit packet type. */
#define GET_PORTCODE_TYPE(code) ((code & 0xFF000000) >> 24)

/** @brief Extracts the HCI device identifier from a packed port code.
 *  @param code 32-bit packed port code.
 *  @return 8-bit hci_id. */
#define GET_PORTCODE_HID(code) ((code & 0x00FF0000) >> 16)

/** @brief Extracts the data payload from a packed port code.
 *  @param code 32-bit packed port code.
 *  @return 16-bit data value. */
#define GET_PORTCODE_DATA(code) ((code & 0x0000FFFF))

/** @brief Well-known port name used by the Bluetooth stack for kernel-to-userland event delivery. */
#define BT_USERLAND_PORT_NAME "BT Kernel-User Event"
/** @brief Well-known port name used by the Bluetooth stack for kernel RX packet assembly. */
#define BT_RX_PORT_NAME "BT Kernel RX assembly"
/** @brief Well-known port name for the Bluetooth connection manager. */
#define BLUETOOTH_CONNECTION_PORT "bluetooth connection port"


/** @brief Bitmask flags describing the current operational state of a transport driver. */
typedef enum {
	ANCILLYANT  = (1<<0), /**< Transport exists but is not yet in active service. */
	RUNNING     = (1<<1), /**< Transport is actively processing packets. */
	LEAVING     = (1<<2), /**< Transport is shutting down. */
	SENDING     = (1<<3), /**< Transport is currently transmitting a packet. */
	PROCESSING  = (1<<4)  /**< Transport is processing a received packet. */
} bt_transport_status_t;


/** @brief Scalar statistics counter type for packet and byte counts. */
typedef uint8 bt_stat_t;

/** @brief Aggregated HCI traffic statistics for a single transport device. */
typedef struct bt_hci_statistics {
	bt_stat_t acceptedTX;   /**< Packets accepted for transmission. */
	bt_stat_t rejectedTX;   /**< Packets rejected before transmission. */
	bt_stat_t successfulTX; /**< Packets successfully transmitted. */
	bt_stat_t errorTX;      /**< Packets that encountered a TX error. */

	bt_stat_t acceptedRX;   /**< Packets accepted on receive. */
	bt_stat_t rejectedRX;   /**< Packets discarded on receive. */
	bt_stat_t successfulRX; /**< Packets successfully received. */
	bt_stat_t errorRX;      /**< Packets that encountered an RX error. */

	bt_stat_t commandTX;    /**< HCI command packets transmitted. */
	bt_stat_t eventRX;      /**< HCI event packets received. */
	bt_stat_t aclTX;        /**< ACL data packets transmitted. */
	bt_stat_t aclRX;        /**< ACL data packets received. */
	bt_stat_t scoTX;        /**< SCO data packets transmitted. */
	bt_stat_t scoRX;        /**< SCO data packets received. */
	bt_stat_t escoTX;       /**< eSCO data packets transmitted. */
	bt_stat_t escoRX;       /**< eSCO data packets received. */

	bt_stat_t bytesRX;      /**< Total bytes received. */
	bt_stat_t bytesTX;      /**< Total bytes transmitted. */
} bt_hci_statistics;


/** @brief Basic identity information for a local HCI device. */
typedef struct bt_hci_device {
	transport_type	kind;                    /**< Physical transport variant (H2/H3/H4/H5). */
	char			realName[B_OS_NAME_LENGTH]; /**< Kernel device node name. */
} bt_hci_device;


#if defined(_KERNEL_MODE)
/* Hooks which drivers will have to provide.
 * The structure is meant to be allocated in driver side and
 * provided to the HCI where it will fill the remaining fields
 */
/** @brief Function pointer table that each HCI transport driver must populate
 *         to allow the HCI layer to send packets through the driver. */
typedef struct bt_hci_transport_hooks {
	// to be filled by driver
	/** @brief Send an HCI command packet to the controller.
	 *  @param hciId   HCI device identifier.
	 *  @param command Pointer to the raw command buffer.
	 *  @return B_OK on success. */
	status_t	(*SendCommand)(hci_id hciId, void* command);

	/** @brief Send an ACL data packet to the controller.
	 *  @param hciId HCI device identifier.
	 *  @param nbuf  Network buffer containing the ACL payload.
	 *  @return B_OK on success. */
	status_t	(*SendACL)(hci_id hciId, net_buffer* nbuf);

	/** @brief Send an SCO data packet to the controller.
	 *  @param hciId HCI device identifier.
	 *  @param nbuf  Network buffer containing the SCO payload.
	 *  @return B_OK on success. */
	status_t	(*SendSCO)(hci_id hciId, net_buffer* nbuf);

	/** @brief Send an eSCO data packet to the controller.
	 *  @param hciId HCI device identifier.
	 *  @param nbuf  Network buffer containing the eSCO payload.
	 *  @return B_OK on success. */
	status_t	(*SendESCO)(hci_id hciId, net_buffer* nbuf);

	/** @brief Deliver current traffic statistics to the HCI layer.
	 *  @param hciId      HCI device identifier.
	 *  @param statistics Pointer to the statistics structure to fill.
	 *  @return B_OK on success. */
	status_t	(*DeliverStatistics)(hci_id hciId, bt_hci_statistics* statistics);

	transport_type kind; /**< Physical transport variant of this driver. */
} bt_hci_transport_hooks;

/** @brief Static identification information provided by a transport driver at registration time. */
typedef struct bt_hci_device_information {
	uint32	flags;                  /**< Transport capability flags (BT_IGNORE_THIS_DEVICE etc.). */
	uint16	vendorId;               /**< USB or PCI vendor identifier. */
	uint16	deviceId;               /**< USB or PCI device identifier. */
	char	name[B_OS_NAME_LENGTH]; /**< Human-readable device name string. */
} bt_hci_device_information;


#if defined(__cplusplus)
/** @brief Kernel-mode representation of a registered Bluetooth transport device,
 *         linked into the HCI layer's device list. */
struct bluetooth_device : DoublyLinkedListLinkImpl<bluetooth_device> {

	net_buffer*	fBuffersRx[HCI_NUM_PACKET_TYPES];       /**< Partial receive buffers, one per packet type. */
	size_t		fExpectedPacketSize[HCI_NUM_PACKET_TYPES]; /**< Expected total size for each in-progress packet. */
	hci_id		index;                                  /**< HCI device index assigned by the stack. */

	uint16		supportedPacketTypes; /**< Bitmask of ACL/SCO packet types supported by this device. */
	uint16		linkMode;             /**< Current link mode flags (HCI_LM_*). */
	int			fd;                   /**< File descriptor for the underlying driver node. */

	bt_hci_device_information*	info;  /**< Static device information provided at registration. */
	bt_hci_transport_hooks*		hooks; /**< Driver function pointer table. */
	uint16						mtu;   /**< Maximum transmission unit for ACL payloads. */

};
#else
struct bluetooth_device;
#endif


/** @brief Module name string used to locate the HCI bus manager via the kernel module API. */
#define BT_HCI_MODULE_NAME "bluetooth/hci/v1"

// Possible definition of a bus manager
/** @brief HCI bus manager module interface exported to transport drivers and upper layers. */
typedef struct bt_hci_module_info {
	module_info info;
	// Registration in Stack
	/** @brief Registers a new transport driver with the HCI stack and obtains a device handle.
	 *  @param hooks   Pointer to the driver's hook table (allocated by the driver).
	 *  @param device  Receives a pointer to the allocated bluetooth_device on success.
	 *  @return B_OK on success, or an error code. */
	status_t			(*RegisterDriver)(bt_hci_transport_hooks* hooks,
							bluetooth_device** device);

	/** @brief Unregisters and removes a previously registered transport driver.
	 *  @param id The hci_id assigned when the driver was registered.
	 *  @return B_OK on success, or an error code. */
	status_t			(*UnregisterDriver)(hci_id id);

	/** @brief Looks up a registered bluetooth_device by its HCI identifier.
	 *  @param id The hci_id to search for.
	 *  @return Pointer to the bluetooth_device, or NULL if not found. */
	bluetooth_device*	(*FindDeviceByID)(hci_id id);

	// to be called from transport driver
	/** @brief Delivers a raw received packet from the transport driver to the HCI stack.
	 *  @param hid   HCI device identifier of the receiving device.
	 *  @param type  Packet type discriminator (bt_packet_t).
	 *  @param data  Pointer to the raw packet data.
	 *  @param count Length of the data in bytes.
	 *  @return B_OK on success, or an error code. */
	status_t			(*PostTransportPacket)(hci_id hid, bt_packet_t type,
							void* data, size_t count);

	// To be called from upper layers
	/** @brief Sends an ACL data packet from an upper-layer protocol to a transport device.
	 *  @param hciId  Target HCI device identifier.
	 *  @param buffer Network buffer containing the ACL packet.
	 *  @return B_OK on success. */
	status_t		(*PostACL)(hci_id hciId, net_buffer* buffer);

	/** @brief Sends an SCO data packet from an upper-layer protocol to a transport device.
	 *  @param hciId  Target HCI device identifier.
	 *  @param buffer Network buffer containing the SCO packet.
	 *  @return B_OK on success. */
	status_t		(*PostSCO)(hci_id hciId, net_buffer* buffer);

	/** @brief Sends an eSCO data packet from an upper-layer protocol to a transport device.
	 *  @param hciId  Target HCI device identifier.
	 *  @param buffer Network buffer containing the eSCO packet.
	 *  @return B_OK on success. */
	status_t		(*PostESCO)(hci_id hciId, net_buffer* buffer);

} bt_hci_module_info ;

#endif


#endif // _BTHCI_TRANSPORT_H_
