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

/** @file btHCI_event.h
 *  @brief HCI event codes and corresponding parameter structures for all standard Bluetooth events. */

#ifndef _BTHCI_EVENT_H_
#define _BTHCI_EVENT_H_

#include <bluetooth/bluetooth.h>
#include <bluetooth/HCI/btHCI.h>

/** @brief Size of the HCI event packet header in bytes (event code + parameter total length). */
#define HCI_EVENT_HDR_SIZE	2

/** @brief HCI event packet header layout (packed). */
struct hci_event_header {
	uint8		ecode; /**< HCI event code. */
	uint8		elen;  /**< Total length of the event parameters in bytes. */
} __attribute__ ((packed));


/* ---- HCI Events ---- */
/** @brief HCI event: Inquiry Complete — a device inquiry has finished. */
#define HCI_EVENT_INQUIRY_COMPLETE					0x01

/** @brief HCI event: Inquiry Result — one or more devices were found during inquiry. */
#define HCI_EVENT_INQUIRY_RESULT					0x02

/** @brief HCI event: Connection Complete — an ACL or SCO connection attempt has completed. */
#define HCI_EVENT_CONN_COMPLETE						0x03
/** @brief Parameters for the Connection Complete event. */
struct hci_ev_conn_complete {
	uint8		status;
	uint16		handle;
	bdaddr_t	bdaddr;
	uint8		link_type;
	uint8		encrypt_mode;
} __attribute__ ((packed));

/** @brief HCI event: Connection Request — a remote device wishes to connect. */
#define HCI_EVENT_CONN_REQUEST						0x04
/** @brief Parameters for the Connection Request event. */
struct hci_ev_conn_request {
	bdaddr_t	bdaddr;
	uint8		dev_class[3];
	uint8		link_type;
} __attribute__ ((packed));

/** @brief HCI event: Disconnection Complete — an existing connection has been torn down. */
#define HCI_EVENT_DISCONNECTION_COMPLETE			0x05
/** @brief Parameters for the Disconnection Complete event. */
struct hci_ev_disconnection_complete_reply {
	uint8		status;
	uint16		handle;
	uint8		reason;
} __attribute__ ((packed));

/** @brief HCI event: Authentication Complete — link-level authentication has completed. */
#define HCI_EVENT_AUTH_COMPLETE						0x06
/** @brief Parameters for the Authentication Complete event. */
struct hci_ev_auth_complete {
	uint8		status;
	uint16		handle;
} __attribute__ ((packed));

/** @brief HCI event: Remote Name Request Complete — a remote name request has finished. */
#define HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE		0x07
/** @brief Parameters for the Remote Name Request Complete event. */
struct hci_ev_remote_name_request_complete_reply {
	uint8		status;
	bdaddr_t	bdaddr;
	char		remote_name[248];
} __attribute__ ((packed));

/** @brief HCI event: Encryption Change — the encryption state of a connection has changed. */
#define HCI_EVENT_ENCRYPT_CHANGE					0x08
/** @brief Parameters for the Encryption Change event. */
struct hci_ev_encrypt_change {
	uint8		status;
	uint16		handle;
	uint8		encrypt;
} __attribute__ ((packed));

/** @brief HCI event: Change Connection Link Key Complete — link key change finished. */
#define HCI_EVENT_CHANGE_CONN_LINK_KEY_COMPLETE		0x09
/** @brief Parameters for the Change Connection Link Key Complete event. */
struct hci_ev_change_conn_link_key_complete {
	uint8		status;
	uint16		handle;
} __attribute__ ((packed));

/** @brief HCI event: Master Link Key Complete — temporary master key activation finished. */
#define HCI_EVENT_MASTER_LINK_KEY_COMPL				0x0a
/** @brief Parameters for the Master Link Key Complete event. */
struct hci_ev_master_link_key_complete {
	uint8		status;	 /* 0x00 - success */
	uint16		handle;	 /* Connection handle */
	uint8		key_flag;   /* Key flag */
} __attribute__ ((packed));

/** @brief HCI event: Read Remote Supported Features Complete. */
#define HCI_EVENT_RMT_FEATURES						0x0B
/** @brief Parameters for the Read Remote Supported Features Complete event. */
struct hci_ev_rmt_features {
	uint8		status;
	uint16		handle;
	uint8		features[8];
} __attribute__ ((packed));

/** @brief HCI event: Read Remote Version Information Complete. */
#define HCI_EVENT_RMT_VERSION						0x0C
/** @brief Parameters for the Read Remote Version Information Complete event. */
struct hci_ev_rmt_version {
	uint8		status;
	uint16		handle;
	uint8		lmp_ver;
	uint16		manufacturer;
	uint16		lmp_subver;
} __attribute__ ((packed));

/** @brief HCI event: QoS Setup Complete — QoS negotiation has finished. */
#define HCI_EVENT_QOS_SETUP_COMPLETE				0x0D
/** @brief QoS flow specification parameters embedded in QoS events. */
struct hci_qos {
	uint8		service_type;
	uint32		token_rate;
	uint32		peak_bandwidth;
	uint32		latency;
	uint32		delay_variation;
} __attribute__ ((packed));
/** @brief Parameters for the QoS Setup Complete event. */
struct hci_ev_qos_setup_complete {
	uint8		status;
	uint16		handle;
	struct		hci_qos qos;
} __attribute__ ((packed));

/** @brief HCI event: Command Complete — an HCI command has been processed by the controller. */
#define HCI_EVENT_CMD_COMPLETE 						0x0E
/** @brief Parameters for the Command Complete event (return parameters follow this struct). */
struct hci_ev_cmd_complete {
	uint8		ncmd;
	uint16		opcode;
} __attribute__ ((packed));

/** @brief HCI event: Command Status — the controller has begun processing an HCI command. */
#define HCI_EVENT_CMD_STATUS 						0x0F
/** @brief Parameters for the Command Status event. */
struct hci_ev_cmd_status {
	uint8		status;
	uint8		ncmd;
	uint16		opcode;
} __attribute__ ((packed));

/** @brief HCI event: Hardware Error — the controller has detected a hardware fault. */
#define HCI_EVENT_HARDWARE_ERROR					0x10
/** @brief Parameters for the Hardware Error event. */
struct hci_ev_hardware_error {
	uint8		hardware_code; /* hardware error code */
} __attribute__ ((packed)) ;

/** @brief HCI event: Flush Occurred — packets for a connection have been flushed. */
#define HCI_EVENT_FLUSH_OCCUR						0x11
/** @brief Parameters for the Flush Occurred event. */
struct hci_ev_flush_occur {
	uint16		handle; /* connection handle */
} __attribute__ ((packed)) ;

/** @brief HCI event: Role Change — the master/slave role for a connection has changed. */
#define HCI_EVENT_ROLE_CHANGE						0x12
/** @brief Parameters for the Role Change event. */
struct hci_ev_role_change {
	uint8		status;
	bdaddr_t	bdaddr;
	uint8		role;
} __attribute__ ((packed));

/** @brief HCI event: Number of Completed Packets — controller has transmitted queued packets. */
#define HCI_EVENT_NUM_COMP_PKTS						0x13
/** @brief Per-handle count entry used inside the Number of Completed Packets event. */
struct handle_and_number {
	uint16		handle;
	uint16		num_completed;
} __attribute__ ((packed));

/** @brief Parameters for the Number of Completed Packets event header (handle/count pairs follow). */
struct hci_ev_num_comp_pkts {
	uint8		num_hndl;
	// struct	handle_and_number; hardcoded...
} __attribute__ ((packed));

/** @brief HCI event: Mode Change — a connection has entered a new power mode. */
#define HCI_EVENT_MODE_CHANGE						0x14
/** @brief Parameters for the Mode Change event. */
struct hci_ev_mode_change {
	uint8		status;
	uint16		handle;
	uint8		mode;
	uint16		interval;
} __attribute__ ((packed));

/** @brief HCI event: Return Link Keys — link keys stored in the controller are returned. */
#define HCI_EVENT_RETURN_LINK_KEYS					0x15
/** @brief Single BD_ADDR / link-key pair used within the Return Link Keys event. */
struct link_key_info {
	bdaddr_t	bdaddr;
	linkkey_t	link_key;
} __attribute__ ((packed)) ;
/** @brief Parameters for the Return Link Keys event. */
struct hci_ev_return_link_keys {
	uint8					num_keys;	/* # of keys */
	struct link_key_info	link_keys;   /* As much as num_keys param */
} __attribute__ ((packed)) ;

/** @brief HCI event: PIN Code Request — the controller needs a PIN from the host. */
#define HCI_EVENT_PIN_CODE_REQ						0x16
/** @brief Parameters for the PIN Code Request event. */
struct hci_ev_pin_code_req {
	bdaddr_t bdaddr;
} __attribute__ ((packed));

/** @brief HCI event: Link Key Request — the controller needs a link key from the host. */
#define HCI_EVENT_LINK_KEY_REQ						0x17
/** @brief Parameters for the Link Key Request event. */
struct hci_ev_link_key_req {
	bdaddr_t bdaddr;
} __attribute__ ((packed));

/** @brief HCI event: Link Key Notification — a new link key has been created. */
#define HCI_EVENT_LINK_KEY_NOTIFY					0x18
/** @brief Parameters for the Link Key Notification event. */
struct hci_ev_link_key_notify {
	bdaddr_t bdaddr;
	linkkey_t	link_key;
	uint8	 key_type;
} __attribute__ ((packed));

/** @brief HCI event: Loopback Command — a loopback echo of a command is returned. */
#define HCI_EVENT_LOOPBACK_COMMAND					0x19
/** @brief Parameters for the Loopback Command event (variable length command data). */
struct hci_ev_loopback_command {
	uint8		command[0]; /* depends of command */
} __attribute__ ((packed)) ;

/** @brief HCI event: Data Buffer Overflow — the host-to-controller data buffer overflowed. */
#define HCI_EVENT_DATA_BUFFER_OVERFLOW				0x1a
/** @brief Parameters for the Data Buffer Overflow event. */
struct hci_ev_data_buffer_overflow {
	uint8		link_type; /* Link type */
} __attribute__ ((packed)) ;

/** @brief HCI event: Max Slots Change — the maximum number of slots for a connection changed. */
#define HCI_EVENT_MAX_SLOT_CHANGE					0x1b
/** @brief Parameters for the Max Slots Change event. */
struct hci_ev_max_slot_change {
	uint16	handle;	/* connection handle */
	uint8	lmp_max_slots; /* Max. # of slots allowed */
} __attribute__ ((packed)) ;

/** @brief HCI event: Read Clock Offset Complete — clock offset read operation finished. */
#define HCI_EVENT_READ_CLOCK_OFFSET_COMPL			0x1c
/** @brief Parameters for the Read Clock Offset Complete event. */
struct hci_ev_read_clock_offset_compl {
	uint8		status;	   /* 0x00 - success */
	uint16		handle;	   /* Connection handle */
	uint16		clock_offset; /* Clock offset */
} __attribute__ ((packed)) ;

/** @brief HCI event: Connection Packet Type Changed — the packet types in use have changed. */
#define HCI_EVENT_CON_PKT_TYPE_CHANGED				0x1d
/** @brief Parameters for the Connection Packet Type Changed event. */
struct hci_ev_con_pkt_type_changed {
	uint8		status;	 /* 0x00 - success */
	uint16		handle; /* connection handle */
	uint16		pkt_type;   /* packet type */
} __attribute__ ((packed));

/** @brief HCI event: QoS Violation — a QoS-guaranteed connection has violated its parameters. */
#define HCI_EVENT_QOS_VIOLATION						0x1e
/** @brief Parameters for the QoS Violation event. */
struct hci_ev_qos_violation {
	uint16		handle; /* connection handle */
} __attribute__ ((packed)) ;

/** @brief HCI event: Page Scan Repetition Mode Change — remote device changed its page scan mode. */
#define HCI_EVENT_PAGE_SCAN_REP_MODE_CHANGE			0x20
/** @brief Parameters for the Page Scan Repetition Mode Change event. */
struct hci_ev_page_scan_rep_mode_change {
	bdaddr_t	bdaddr;			 /* destination address */
	uint8		page_scan_rep_mode; /* page scan repetition mode */
} __attribute__ ((packed));

/* Events Beyond Bluetooth 1.1 */
/** @brief HCI event: Flow Specification Complete — flow specification negotiation finished. */
#define HCI_EVENT_FLOW_SPECIFICATION				0x21
/** @brief Parameters for the Flow Specification Complete event. */
struct hci_ev_flow_specification {
	uint8		status;
	uint16		handle;
	uint8		flags;
	uint8		flow_direction;
	uint8		service_type;
	uint32		token_rate;
	uint32 		token_bucket_size;
	uint32		peak_bandwidth;
	uint32		access_latency;
} __attribute__ ((packed));

/** @brief HCI event: Inquiry Result with RSSI — inquiry result including signal strength. */
#define HCI_EVENT_INQUIRY_RESULT_WITH_RSSI			0x22
/** @brief Sentinel RSSI value indicating that the RSSI measurement is invalid. */
#define HCI_RSSI_INVALID 127

/** @brief HCI event: Read Remote Extended Features Complete. */
#define HCI_EVENT_REMOTE_EXTENDED_FEATURES			0x23
/** @brief Parameters for the Read Remote Extended Features Complete event. */
struct hci_ev_remote_extended_features {
	uint8		status;
	uint16		handle;
	uint8		page_number;
	uint8		maximun_page_number;
	uint64		extended_lmp_features;
} __attribute__ ((packed));

/** @brief HCI event: Synchronous Connection Completed — eSCO/SCO connection attempt finished. */
#define HCI_EVENT_SYNCHRONOUS_CONNECTION_COMPLETED	0x2C
/** @brief Parameters for the Synchronous Connection Completed event. */
struct hci_ev_sychronous_connection_completed {
	uint8		status;
	uint16		handle;
	bdaddr_t	bdaddr;
	uint8		link_type;
	uint8		transmission_interval;
	uint8		retransmission_window;
	uint16		rx_packet_length;
	uint16		tx_packet_length;
	uint8		air_mode;
} __attribute__ ((packed));

/** @brief HCI event: Synchronous Connection Changed — parameters of an eSCO/SCO link changed. */
#define HCI_EVENT_SYNCHRONOUS_CONNECTION_CHANGED	0x2D
/** @brief Parameters for the Synchronous Connection Changed event. */
struct hci_ev_sychronous_connection_changed {
	uint8		status;
	uint16		handle;
	uint8		transmission_interval;
	uint8		retransmission_window;
	uint16		rx_packet_length;
	uint16		tx_packet_length;
} __attribute__ ((packed));

// TODO: Define remaining Bluetooth 2.1 events structures
/** @brief HCI event: Extended Inquiry Result — inquiry result with extended inquiry response data. */
#define HCI_EVENT_EXTENDED_INQUIRY_RESULT			0x2F
/** @brief Maximum length of an Extended Inquiry Response (EIR) payload in bytes. */
#define HCI_MAX_EIR_LENGTH 240
/** @brief Parameters for the Extended Inquiry Result event. */
struct hci_ev_extended_inquiry_info {
    bdaddr_t bdaddr;
    uint8    page_repetition_mode;
    uint8    scan_period_mode;
    uint8    dev_class[3];
    uint16   clock_offset;
    int8     rssi;
    uint8    eir[HCI_MAX_EIR_LENGTH];
} __attribute__((packed));
/** @brief EIR data type: shortened local name. */
#define EIR_NAME_SHORT 0x08
/** @brief EIR data type: complete local name. */
#define EIR_NAME_COMPLETE 0x09

/** @brief HCI event: Encryption Key Refresh Complete — encryption keys have been refreshed. */
#define HCI_EVENT_ENCRYPTION_KEY_REFRESH_COMPLETE	0x30

/** @brief HCI event: IO Capability Request — controller requests IO capability information. */
#define HCI_EVENT_IO_CAPABILITY_REQUEST				0x31
/** @brief Parameters for the IO Capability Request event. */
struct hci_ev_io_capability_request {
    bdaddr_t    bdaddr;
} __attribute__((packed));

/** @brief HCI event: IO Capability Response — remote device's IO capabilities have been received. */
#define HCI_EVENT_IO_CAPABILITY_RESPONSE			0x32
/** @brief Parameters for the IO Capability Response event. */
struct hci_ev_io_capability_response {
    bdaddr_t    bdaddr;
    uint8       capability;
    uint8       oob_data;
    uint8       authentication;
} __attribute__((packed));

/** @brief IO capability: display only (no input). */
#define HCI_IO_CAP_DISPLAY_ONLY 0x00
/** @brief IO capability: display with yes/no confirmation. */
#define HCI_IO_CAP_DISPLAY_YES_NO 0x01
/** @brief IO capability: keyboard only (no display). */
#define HCI_IO_CAP_KEYBOARD_ONLY 0x02
/** @brief IO capability: no input and no output. */
#define HCI_IO_CAP_NO_INPUT_NO_OUTPUT 0x03

/** @brief OOB data flag: no OOB authentication data available. */
#define HCI_OOB_DATA_NOT_PRESENT 0x00
/** @brief OOB data flag: OOB authentication data is present. */
#define HCI_OOB_DATA_PRESENT 0x01

/** @brief Authentication requirement: no MITM protection, no bonding. */
#define HCI_AUTH_REQ_NO_MITM_NO_BOND 0x00
/** @brief Authentication requirement: MITM protection required, no bonding. */
#define HCI_AUTH_REQ_MITM_NO_BOND 0x01
/** @brief Authentication requirement: no MITM protection, dedicated bonding. */
#define HCI_AUTH_REQ_NO_MITM_DEDICATED_BOND 0x02
/** @brief Authentication requirement: MITM protection required, dedicated bonding. */
#define HCI_AUTH_REQ_MITM_DEDICATED_BOND 0x03
/** @brief Authentication requirement: no MITM protection, general bonding. */
#define HCI_AUTH_REQ_NO_MITM_GENERAL_BOND 0x04
/** @brief Authentication requirement: MITM protection required, general bonding. */
#define HCI_AUTH_REQ_MITM_GENERAL_BOND 0x05

/** @brief HCI event: User Confirmation Request — host must confirm a numeric passkey. */
#define HCI_EVENT_USER_CONFIRMATION_REQUEST 0x33
/** @brief Parameters for the User Confirmation Request event. */
struct hci_ev_user_confirmation_request {
    bdaddr_t    bdaddr;
    uint32      passkey;
} __attribute__((packed));

/** @brief HCI event: User Passkey Request — host must provide a passkey for SSP. */
#define HCI_EVENT_USER_PASSKEY_REQUEST				0x34

/** @brief HCI event: Remote OOB Data Request — controller requests OOB pairing data. */
#define HCI_EVENT_OOB_DATA_REQUEST					0x35

/** @brief HCI event: Simple Pairing Complete — Secure Simple Pairing procedure has finished. */
#define HCI_EVENT_SIMPLE_PAIRING_COMPLETE			0x36
/** @brief Parameters for the Simple Pairing Complete event. */
struct hci_ev_simple_pairing_complete {
    uint8       status;
    bdaddr_t    bdaddr;
} __attribute__((packed));

/** @brief HCI event: Link Supervision Timeout Changed — LST value for a connection has changed. */
#define HCI_EVENT_LINK_SUPERVISION_TIMEOUT_CHANGED	0x38

/** @brief HCI event: Enhanced Flush Complete — an enhanced flush has completed. */
#define HCI_EVENT_ENHANCED_FLUSH_COMPLETE			0x39

/** @brief HCI event: Keypress Notification — SSP keypress progress notification. */
#define HCI_EVENT_KEYPRESS_NOTIFICATION				0x3C

/** @brief HCI event: Remote Host Supported Features Notification — remote host features received. */
#define HCI_EVENT_REMOTE_HOST_SUPPORTED_FEATURES_NOTIFICATION	0x3D


/* HAIKU Internal Events, not produced by the transport devices but
 * by some entity of the Haiku Bluetooth Stack.
 * The MSB 0xE is chosen for this purpose
 */

/** @brief Internal stack event: the Bluetooth server is shutting down. */
#define HCI_HAIKU_EVENT_SERVER_QUITTING				0xE0
/** @brief Internal stack event: a Bluetooth transport device has been removed. */
#define HCI_HAIKU_EVENT_DEVICE_REMOVED				0xE1


#endif // _BTHCI_EVENT_H_
