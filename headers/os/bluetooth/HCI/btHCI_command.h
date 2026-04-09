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

/** @file btHCI_command.h
 *  @brief HCI command header, opcode macros, and all command/reply parameter structures. */

#ifndef _BTHCI_COMMAND_H_
#define _BTHCI_COMMAND_H_

#include <bluetooth/bluetooth.h>
#include <bluetooth/HCI/btHCI.h>

/** @brief Size of the HCI command packet header in bytes (opcode + parameter total length). */
#define HCI_COMMAND_HDR_SIZE 3

/** @brief HCI command packet header layout (packed). */
struct hci_command_header {
	uint16	opcode;		/* OCF & OGF */
	uint8	clen;
} __attribute__ ((packed));


/* Command opcode pack/unpack */
/** @brief Packs an OGF and OCF into a 16-bit HCI command opcode.
 *  @param ogf Opcode Group Field (6 bits).
 *  @param ocf Opcode Command Field (10 bits).
 *  @return The packed 16-bit opcode. */
#define PACK_OPCODE(ogf, ocf)	(uint16)((ocf & 0x03ff)|(ogf << 10))

/** @brief Extracts the OGF from a packed 16-bit HCI command opcode.
 *  @param op The packed 16-bit opcode.
 *  @return The 6-bit OGF value. */
#define GET_OPCODE_OGF(op)		(op >> 10)

/** @brief Extracts the OCF from a packed 16-bit HCI command opcode.
 *  @param op The packed 16-bit opcode.
 *  @return The 10-bit OCF value. */
#define GET_OPCODE_OCF(op)		(op & 0x03ff)


/* - Informational Parameters Command definition - */
/** @brief OGF: Informational Parameters command group. */
#define OGF_INFORMATIONAL_PARAM	0x04

	/** @brief OCF: Read Local Version Information. */
	#define OCF_READ_LOCAL_VERSION		0x0001
	/** @brief Reply parameters for HCI Read Local Version Information command. */
	struct hci_rp_read_loc_version {
		uint8		status;
		uint8		hci_version;
		uint16		hci_revision;
		uint8		lmp_version;
		uint16		manufacturer;
		uint16		lmp_subversion;
	} __attribute__ ((packed));

	/** @brief OCF: Read Local Supported Features. */
	#define OCF_READ_LOCAL_FEATURES		0x0003
	/** @brief Reply parameters for HCI Read Local Supported Features command. */
	struct hci_rp_read_loc_features {
		uint8		status;
		uint8		features[8];
	} __attribute__ ((packed));

	/** @brief OCF: Read Buffer Size. */
	#define OCF_READ_BUFFER_SIZE		0x0005
	/** @brief Reply parameters for HCI Read Buffer Size command. */
	struct hci_rp_read_buffer_size {
		uint8		status;
		uint16		acl_mtu;
		uint8		sco_mtu;
		uint16		acl_max_pkt;
		uint16		sco_max_pkt;
	} __attribute__ ((packed));

	/** @brief OCF: Read BD_ADDR. */
	#define OCF_READ_BD_ADDR			0x0009
	/** @brief Reply parameters for HCI Read BD_ADDR command. */
	struct hci_rp_read_bd_addr {
		uint8		status;
		bdaddr_t	bdaddr;
	} __attribute__ ((packed));

/* - Host Controller and Baseband Command definition - */
/** @brief OGF: Host Controller and Baseband command group. */
#define OGF_CONTROL_BASEBAND			0x03

	/** @brief OCF: Reset the HCI controller. */
	#define OCF_RESET					0x0003
  /*struct hci_reset {
		void no_fields;
	} __attribute__ ((packed));*/

	/** @brief OCF: Set Event Filter. */
	#define OCF_SET_EVENT_FLT			0x0005
	/** @brief Command parameters for HCI Set Event Filter command. */
	struct hci_cp_set_event_flt {
		uint8		flt_type;
		uint8		cond_type;
		uint8		condition[0];
	} __attribute__ ((packed));

	/** @brief OCF: Read Stored Link Key. */
	#define OCF_READ_STORED_LINK_KEY	0x000D
	/** @brief Command parameters for HCI Read Stored Link Key command. */
	struct hci_read_stored_link_key {
		bdaddr_t	bdaddr;
		uint8		all_keys_flag;
	} __attribute__ ((packed));
	/** @brief Reply parameters for HCI Read Stored Link Key command. */
	struct hci_read_stored_link_key_reply {
		uint8		status;
		uint16		max_num_keys;
		uint16		num_keys_read;
	} __attribute__ ((packed));

	/** @brief OCF: Write Stored Link Key. */
	#define OCF_WRITE_STORED_LINK_KEY	0x0011
	/** @brief Command parameters for HCI Write Stored Link Key command. */
	struct hci_write_stored_link_key {
		uint8		num_keys_to_write;
		// these are repeated "num_keys_write" times
		bdaddr_t	bdaddr;
		uint8		key[HCI_LINK_KEY_SIZE];
	} __attribute__ ((packed));
	/** @brief Reply parameters for HCI Write Stored Link Key command. */
	struct hci_write_stored_link_key_reply {
		uint8		status;
		uint8		num_keys_written;
	} __attribute__ ((packed));


	/** @brief OCF: Write Local Name. */
	#define OCF_WRITE_LOCAL_NAME		0x0013
	/** @brief Command parameters for HCI Write Local Name command. */
	struct hci_write_local_name {
		char		local_name[HCI_DEVICE_NAME_SIZE];
	} __attribute__ ((packed));

	/** @brief OCF: Read Local Name. */
	#define OCF_READ_LOCAL_NAME			0x0014
	/** @brief Reply parameters for HCI Read Local Name command. */
	struct hci_rp_read_local_name {
		uint8		status;
		char		local_name[HCI_DEVICE_NAME_SIZE];
	} __attribute__ ((packed));

	/** @brief OCF: Read Connection Accept Timeout. */
	#define OCF_READ_CA_TIMEOUT			0x0015
	/** @brief OCF: Write Connection Accept Timeout. */
	#define OCF_WRITE_CA_TIMEOUT		0x0016
	/** @brief OCF: Read Page Timeout. */
	#define OCF_READ_PG_TIMEOUT			0x0017
	/** @brief Reply parameters for HCI Read Page Timeout command. */
	struct hci_rp_read_page_timeout {
		uint8		status;
		uint16		page_timeout;
	} __attribute__ ((packed));
	/** @brief OCF: Write Page Timeout. */
	#define OCF_WRITE_PG_TIMEOUT		0x0018

	/** @brief OCF: Read Scan Enable. */
	#define OCF_READ_SCAN_ENABLE		0x0019
	/** @brief Reply parameters for HCI Read Scan Enable command. */
	struct hci_read_scan_enable {
		uint8		status;
		uint8		enable;
	} __attribute__ ((packed));

	/** @brief OCF: Write Scan Enable. */
	#define OCF_WRITE_SCAN_ENABLE		0x001A
		/** @brief Scan mode: both inquiry and page scan disabled. */
		#define HCI_SCAN_DISABLED		0x00
		/** @brief Scan mode: inquiry scan enabled, page scan disabled. */
		#define HCI_SCAN_INQUIRY		0x01 //Page Scan disabled
		/** @brief Scan mode: page scan enabled, inquiry scan disabled. */
		#define HCI_SCAN_PAGE			0x02 //Inquiry Scan disabled
		/** @brief Scan mode: both inquiry and page scan enabled. */
		#define HCI_SCAN_INQUIRY_PAGE	0x03 //All enabled
	/** @brief Command parameters for HCI Write Scan Enable command. */
	struct hci_write_scan_enable {
		uint8		scan;
	} __attribute__ ((packed));

	/** @brief OCF: Read Authentication Enable. */
	#define OCF_READ_AUTH_ENABLE		0x001F
	/** @brief OCF: Write Authentication Enable. */
	#define OCF_WRITE_AUTH_ENABLE		0x0020
		/** @brief Authentication mode: authentication disabled. */
		#define HCI_AUTH_DISABLED		0x00
		/** @brief Authentication mode: authentication enabled for all connections. */
		#define HCI_AUTH_ENABLED		0x01
	/** @brief Command parameters for HCI Write Authentication Enable command. */
	struct hci_write_authentication_enable {
		uint8		authentication;
	} __attribute__ ((packed));

	/** @brief OCF: Read Encryption Mode. */
	#define OCF_READ_ENCRYPT_MODE		0x0021
	/** @brief OCF: Write Encryption Mode. */
	#define OCF_WRITE_ENCRYPT_MODE		0x0022
		/** @brief Encryption mode: encryption disabled. */
		#define HCI_ENCRYPT_DISABLED	0x00
		/** @brief Encryption mode: encryption enabled for point-to-point traffic only. */
		#define HCI_ENCRYPT_P2P			0x01
		/** @brief Encryption mode: encryption enabled for both P2P and broadcast. */
		#define HCI_ENCRYPT_BOTH		0x02
	/** @brief Command parameters for HCI Write Encryption Mode command. */
	struct hci_write_encryption_mode_enable {
		uint8		encryption;
	} __attribute__ ((packed));

	/* Filter types */
	/** @brief Event filter type: clear all filters. */
	#define HCI_FLT_CLEAR_ALL			0x00
	/** @brief Event filter type: inquiry result filter. */
	#define HCI_FLT_INQ_RESULT			0x01
	/** @brief Event filter type: connection setup filter. */
	#define HCI_FLT_CONN_SETUP			0x02

	/* CONN_SETUP Condition types */
	/** @brief Connection filter condition: allow connections from all devices. */
	#define HCI_CONN_SETUP_ALLOW_ALL	0x00
	/** @brief Connection filter condition: allow connections from a specific device class. */
	#define HCI_CONN_SETUP_ALLOW_CLASS	0x01
	/** @brief Connection filter condition: allow connections from a specific BD_ADDR. */
	#define HCI_CONN_SETUP_ALLOW_BDADDR	0x02

	/* CONN_SETUP Conditions */
	/** @brief Connection filter auto-accept: do not auto-accept. */
	#define HCI_CONN_SETUP_AUTO_OFF		0x01
	/** @brief Connection filter auto-accept: auto-accept connections. */
	#define HCI_CONN_SETUP_AUTO_ON		0x02

	/** @brief OCF: Read Class of Device. */
	#define OCF_READ_CLASS_OF_DEV		0x0023

	/** @brief Reply parameters for HCI Read Class of Device command. */
	struct hci_read_dev_class_reply {
		uint8		status;
		uint8		dev_class[3];
	} __attribute__ ((packed));

	/** @brief OCF: Write Class of Device. */
	#define OCF_WRITE_CLASS_OF_DEV		0x0024
	/** @brief Command parameters for HCI Write Class of Device command. */
	struct hci_write_dev_class {
		uint8		dev_class[3];
	} __attribute__ ((packed));

	/** @brief OCF: Read Voice Setting. */
	#define OCF_READ_VOICE_SETTING		0x0025
	/** @brief Reply parameters for HCI Read Voice Setting command. */
	struct hci_rp_read_voice_setting {
		uint8		status;
		uint16		voice_setting;
	} __attribute__ ((packed));

	/** @brief OCF: Write Voice Setting. */
	#define OCF_WRITE_VOICE_SETTING		0x0026
	/** @brief Command parameters for HCI Write Voice Setting command. */
	struct hci_cp_write_voice_setting {
		uint16		voice_setting;
	} __attribute__ ((packed));

/** @brief OCF: IO Capability Request Reply. */
#define OCF_IO_CAPABILITY_REQUEST_REPLY 0x002B
	/** @brief Command parameters for HCI IO Capability Request Reply command. */
	struct hci_cp_io_capability_request_reply {
		bdaddr_t    bdaddr;
		uint8       capability;
		uint8       oob_data;
		uint8       authentication;
	} __attribute__((packed));

/** @brief OCF: User Confirmation Request Reply (confirm numeric comparison). */
#define OCF_USER_CONFIRM_REPLY 0x002C
	/** @brief Command parameters for HCI User Confirmation Request Reply command. */
	struct hci_cp_user_confirm_reply {
		bdaddr_t    bdaddr;
	} __attribute__((packed));

/** @brief OCF: User Confirmation Request Negative Reply (reject numeric comparison). */
#define OCF_USER_CONFIRM_NEG_REPLY 0x002D
	/** @brief Command parameters for HCI User Confirmation Request Negative Reply command. */
	struct hci_cp_user_confirm_neg_reply {
		bdaddr_t    bdaddr;
	} __attribute__((packed));

/** @brief OCF: Host Buffer Size. */
#define OCF_HOST_BUFFER_SIZE 0x0033
	/** @brief Command parameters for HCI Host Buffer Size command. */
	struct hci_cp_host_buffer_size {
		uint16		acl_mtu;
		uint8		sco_mtu;
		uint16		acl_max_pkt;
		uint16		sco_max_pkt;
	} __attribute__ ((packed));

	/* Link Control Command definition */
	/** @brief OGF: Link Control command group. */
	#define OGF_LINK_CONTROL			0x01

	/** @brief OCF: Inquiry — begin device discovery. */
	#define OCF_INQUIRY					0x0001
	/** @brief Command parameters for HCI Inquiry command. */
	struct hci_cp_inquiry {
		uint8		lap[3];
		uint8		length;
		uint8		num_rsp;
	} __attribute__ ((packed));

	/** @brief OCF: Inquiry Cancel — abort an ongoing inquiry. */
	#define OCF_INQUIRY_CANCEL			0x0002

	/** @brief OCF: Create Connection. */
	#define OCF_CREATE_CONN				0x0005
	/** @brief Command parameters for HCI Create Connection command. */
	struct hci_cp_create_conn {
		bdaddr_t bdaddr;
		uint16		pkt_type;
		uint8		pscan_rep_mode;
		uint8		pscan_mode;
		uint16		clock_offset;
		uint8		role_switch;
	} __attribute__ ((packed));

	/** @brief OCF: Disconnect. */
	#define OCF_DISCONNECT				0x0006
	/** @brief Command parameters for HCI Disconnect command. */
	struct hci_disconnect {
		uint16		handle;
		uint8		reason;
	} __attribute__ ((packed));

	/** @brief OCF: Add SCO Connection. */
	#define OCF_ADD_SCO					0x0007
	/** @brief Command parameters for HCI Add SCO Connection command. */
	struct hci_cp_add_sco {
		uint16		handle;
		uint16		pkt_type;
	} __attribute__ ((packed));

	/** @brief OCF: Accept Connection Request. */
	#define OCF_ACCEPT_CONN_REQ			0x0009
	/** @brief Command parameters for HCI Accept Connection Request command. */
	struct hci_cp_accept_conn_req {
		bdaddr_t	bdaddr;
		uint8		role;
	} __attribute__ ((packed));

	/** @brief OCF: Reject Connection Request. */
	#define OCF_REJECT_CONN_REQ			0x000a
	/** @brief Command parameters for HCI Reject Connection Request command. */
	struct hci_cp_reject_conn_req {
		bdaddr_t	bdaddr;
		uint8		reason;
	} __attribute__ ((packed));

	/** @brief OCF: Link Key Request Reply. */
	#define OCF_LINK_KEY_REPLY			0x000B
	/** @brief Command parameters for HCI Link Key Request Reply command. */
	struct hci_cp_link_key_reply {
		bdaddr_t	bdaddr;
		uint8		link_key[16];
	} __attribute__ ((packed));

	/** @brief OCF: Link Key Request Negative Reply. */
	#define OCF_LINK_KEY_NEG_REPLY		0x000C
	/** @brief Command parameters for HCI Link Key Request Negative Reply command. */
	struct hci_cp_link_key_neg_reply {
		bdaddr_t	bdaddr;
	} __attribute__ ((packed));

	/** @brief OCF: PIN Code Request Reply. */
	#define OCF_PIN_CODE_REPLY			0x000D
	/** @brief Command parameters for HCI PIN Code Request Reply command. */
	struct hci_cp_pin_code_reply {
		bdaddr_t bdaddr;
		uint8		pin_len;
		uint8		pin_code[HCI_PIN_SIZE];
	} __attribute__ ((packed));

	/** @brief Reply parameters returned after a Link Key Request Reply command. */
	struct hci_cp_link_key_reply_reply {
		uint8	status;
		bdaddr_t bdaddr;
	} __attribute__ ((packed));

	/** @brief OCF: PIN Code Request Negative Reply. */
	#define OCF_PIN_CODE_NEG_REPLY		0x000E
	/** @brief Command parameters for HCI PIN Code Request Negative Reply command. */
	struct hci_cp_pin_code_neg_reply {
		bdaddr_t	bdaddr;
	} __attribute__ ((packed));

	/** @brief OCF: Change Connection Packet Type. */
	#define OCF_CHANGE_CONN_PTYPE		0x000F
	/** @brief Command parameters for HCI Change Connection Packet Type command. */
	struct hci_cp_change_conn_ptype {
		uint16		handle;
		uint16		pkt_type;
	} __attribute__ ((packed));

	/** @brief OCF: Authentication Requested. */
	#define OCF_AUTH_REQUESTED			0x0011
	/** @brief Command parameters for HCI Authentication Requested command. */
	struct hci_cp_auth_requested {
		uint16		handle;
	} __attribute__ ((packed));

	/** @brief OCF: Set Connection Encryption. */
	#define OCF_SET_CONN_ENCRYPT		0x0013
	/** @brief Command parameters for HCI Set Connection Encryption command. */
	struct hci_cp_set_conn_encrypt {
		uint16		handle;
		uint8		encrypt;
	} __attribute__ ((packed));

	/** @brief OCF: Change Connection Link Key. */
	#define OCF_CHANGE_CONN_LINK_KEY	0x0015
	/** @brief Command parameters for HCI Change Connection Link Key command. */
	struct hci_cp_change_conn_link_key {
		uint16		handle;
	} __attribute__ ((packed));

	/** @brief OCF: Remote Name Request. */
	#define OCF_REMOTE_NAME_REQUEST		0x0019
	/** @brief Command parameters for HCI Remote Name Request command. */
	struct hci_remote_name_request {
		bdaddr_t bdaddr;
		uint8		pscan_rep_mode;
		uint8		reserved;
		uint16		clock_offset;
	} __attribute__ ((packed));

	/** @brief OCF: Read Remote Supported Features. */
	#define OCF_READ_REMOTE_FEATURES	0x001B
	/** @brief Command parameters for HCI Read Remote Supported Features command. */
	struct hci_cp_read_rmt_features {
		uint16		handle;
	} __attribute__ ((packed));

	/** @brief OCF: Read Remote Version Information. */
	#define OCF_READ_REMOTE_VERSION		0x001D
	/** @brief Command parameters for HCI Read Remote Version Information command. */
	struct hci_cp_read_rmt_version {
		uint16		handle;
	} __attribute__ ((packed));


/* Link Policy Command definition */
/** @brief OGF: Link Policy command group. */
#define OGF_LINK_POLICY					0x02

	/** @brief OCF: Role Discovery. */
	#define OCF_ROLE_DISCOVERY			0x0009
	/** @brief Command parameters for HCI Role Discovery command. */
	struct hci_cp_role_discovery {
		uint16		handle;
	} __attribute__ ((packed));
	/** @brief Reply parameters for HCI Role Discovery command. */
	struct hci_rp_role_discovery {
		uint8		status;
		uint16		handle;
		uint8		role;
	} __attribute__ ((packed));

	/** @brief OCF: Flow Specification. */
	#define OCF_FLOW_SPECIFICATION
	/** @brief Command parameters for HCI Flow Specification command. */
	struct hci_cp_flow_specification {
		uint16		handle;
		uint8		flags;
		uint8		flow_direction;
		uint8		service_type;
		uint32		token_rate;
		uint32		token_bucket;
		uint32		peak;
		uint32		latency;
	} __attribute__ ((packed));
	/* Quality of service types */
	/** @brief QoS service type: no traffic. */
	#define HCI_SERVICE_TYPE_NO_TRAFFIC		0x00
	/** @brief QoS service type: best effort. */
	#define HCI_SERVICE_TYPE_BEST_EFFORT		0x01
	/** @brief QoS service type: guaranteed delivery. */
	#define HCI_SERVICE_TYPE_GUARANTEED		0x02
	/* 0x03 - 0xFF - reserved for future use */

	/** @brief OCF: Read Link Policy Settings. */
	#define OCF_READ_LINK_POLICY		0x000C
	/** @brief Command parameters for HCI Read Link Policy Settings command. */
	struct hci_cp_read_link_policy {
		uint16		handle;
	} __attribute__ ((packed));
	/** @brief Reply parameters for HCI Read Link Policy Settings command. */
	struct hci_rp_read_link_policy {
		uint8		status;
		uint16		handle;
		uint16		policy;
	} __attribute__ ((packed));

	/** @brief OCF: Switch Role. */
	#define OCF_SWITCH_ROLE				0x000B
	/** @brief Command parameters for HCI Switch Role command. */
	struct hci_cp_switch_role {
		bdaddr_t	bdaddr;
		uint8		role;
	} __attribute__ ((packed));

	/** @brief OCF: Write Link Policy Settings. */
	#define OCF_WRITE_LINK_POLICY		0x000D
	/** @brief Command parameters for HCI Write Link Policy Settings command. */
	struct hci_cp_write_link_policy {
		uint16		handle;
		uint16		policy;
	} __attribute__ ((packed));
	/** @brief Reply parameters for HCI Write Link Policy Settings command. */
	struct hci_rp_write_link_policy {
		uint8		status;
		uint16		handle;
	} __attribute__ ((packed));

/* Status params */
/** @brief OGF: Status Parameters command group. */
#define OGF_STATUS_PARAM				0x05

/* Testing commands */
/** @brief OGF: Testing command group. */
#define OGF_TESTING_CMD					0x06

/* Vendor specific commands */
/** @brief OGF: Vendor-specific command group. */
#define OGF_VENDOR_CMD					0x3F

/** @brief OCF: Write BCM2035 BD_ADDR (vendor-specific). */
#define OCF_WRITE_BCM2035_BDADDR		0x01
	/** @brief Command parameters for BCM2035 Write BD_ADDR vendor command. */
	struct hci_write_bcm2035_bdaddr {
		bdaddr_t bdaddr;
	} _PACKED;

#endif // _BTHCI_COMMAND_H_
