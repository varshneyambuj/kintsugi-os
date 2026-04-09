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
 *   Copyright 2024, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file l2cap.h
 *  @brief L2CAP protocol constants, channel IDs, PSM values, and packed
 *         PDU structures used by the Bluetooth stack. */

#ifndef _L2CAP_H_
#define _L2CAP_H_

#include <bluetooth/bluetooth.h>


/* Channel IDs */
/*! These are unique for a unit. Thus the total number of channels that a unit
 * can have open simultaneously is (L2CAP_LAST_CID - L2CAP_FIRST_CID) = 65471.
 * (This does not depend on the number of connections.) */

/** @brief Null / invalid channel identifier. */
#define L2CAP_NULL_CID		0x0000

/** @brief Reserved CID for L2CAP signalling commands. */
#define L2CAP_SIGNALING_CID	0x0001

/** @brief Reserved CID for connectionless data traffic. */
#define L2CAP_CONNECTIONLESS_CID 0x0002
	/* 0x0003-0x003f: reserved */

/** @brief First dynamically assignable channel identifier. */
#define L2CAP_FIRST_CID		0x0040

/** @brief Last valid channel identifier. */
#define L2CAP_LAST_CID		0xffff


/* Idents */
/*! Command idents are unique within a connection, since there is only one
 * L2CAP_SIGNALING_CID. Thus only (L2CAP_LAST_IDENT - L2CAP_FIRST_IDENT),
 * i.e. 254, commands can be pending simultaneously for a connection. */

/** @brief Null / invalid command identifier. */
#define L2CAP_NULL_IDENT		0x00

/** @brief First valid command identifier value. */
#define L2CAP_FIRST_IDENT		0x01

/** @brief Last valid command identifier value. */
#define L2CAP_LAST_IDENT		0xff


/* MTU */

/** @brief Minimum acceptable L2CAP MTU (48 bytes). */
#define L2CAP_MTU_MINIMUM		48

/** @brief Default L2CAP MTU (672 bytes). */
#define L2CAP_MTU_DEFAULT		672

/** @brief Maximum possible L2CAP MTU (65535 bytes). */
#define L2CAP_MTU_MAXIMUM		0xffff


/* Timeouts */

/** @brief Default flush timeout value indicating "always retransmit". */
#define L2CAP_FLUSH_TIMEOUT_DEFAULT	0xffff /* always retransmit */

/** @brief Default link supervision timeout. */
#define L2CAP_LINK_TIMEOUT_DEFAULT	0xffff


/* Protocol/Service Multiplexer (PSM) values */

/** @brief Any/Invalid PSM value. */
#define L2CAP_PSM_ANY		0x0000	/* Any/Invalid PSM */

/** @brief PSM for Service Discovery Protocol. */
#define L2CAP_PSM_SDP		0x0001	/* Service Discovery Protocol */

/** @brief PSM for RFCOMM serial emulation protocol. */
#define L2CAP_PSM_RFCOMM	0x0003	/* RFCOMM protocol */

/** @brief PSM for Telephony Control Protocol (TCS-Binary). */
#define L2CAP_PSM_TCS_BIN	0x0005	/* Telephony Control Protocol */

/** @brief PSM for TCS-Binary cordless telephony. */
#define L2CAP_PSM_TCS_BIN_CORDLESS 0x0007 /* TCS cordless */

/** @brief PSM for Bluetooth Network Encapsulation Protocol (BNEP). */
#define L2CAP_PSM_BNEP		0x000F	/* BNEP */

/** @brief PSM for HID control channel. */
#define L2CAP_PSM_HID_CTRL	0x0011	/* HID control */

/** @brief PSM for HID interrupt channel. */
#define L2CAP_PSM_HID_INT	0x0013	/* HID interrupt */

/** @brief PSM for UPnP / Extended Service Discovery Profile (ESDP). */
#define L2CAP_PSM_UPnP		0x0015	/* UPnP (ESDP) */

/** @brief PSM for Audio/Video Control Transport Protocol (AVCTP). */
#define L2CAP_PSM_AVCTP		0x0017	/* AVCTP */

/** @brief PSM for Audio/Video Distribution Transport Protocol (AVDTP). */
#define L2CAP_PSM_AVDTP		0x0019	/* AVDTP */
	/* < 0x1000: reserved */
	/* >= 0x1000: dynamically assigned */


/** @brief Basic L2CAP PDU header present on every ACL data packet. */
typedef struct {
	uint16	length;	/* payload size */
	uint16	dcid;	/* destination channel ID */
} _PACKED l2cap_basic_header;


/* Connectionless traffic ("CLT") */

/** @brief Header for connectionless L2CAP traffic, following the basic header
 *         when dcid == L2CAP_CONNECTIONLESS_CID. */
typedef struct {
	/* dcid == L2CAP_CONNECTIONLESS_CID (0x2) */
	uint16	psm;
} _PACKED l2cap_connectionless_header;

/** @brief Maximum payload size for connectionless L2CAP traffic. */
#define L2CAP_CONNECTIONLESS_MTU_MAXIMUM (L2CAP_MTU_MAXIMUM - sizeof(l2cap_connectionless_header))


/** @brief Header for an L2CAP signalling command carried on L2CAP_SIGNALING_CID. */
typedef struct {
	uint8	code;   /* command opcode */
/** @brief Returns non-zero if @p code identifies a signalling request. */
#define L2CAP_IS_SIGNAL_REQ(code) (((code) & 1) == 0)
/** @brief Returns non-zero if @p code identifies a signalling response. */
#define L2CAP_IS_SIGNAL_RSP(code) (((code) & 1) == 1)
	uint8	ident;  /* identifier to match request and response */
	uint16	length; /* command parameters length */
} _PACKED l2cap_command_header;

/** @brief Opcode for a Command Reject response. */
#define L2CAP_COMMAND_REJECT_RSP	0x01

/** @brief Payload of an L2CAP Command Reject response, carrying the rejection
 *         reason and optional additional data. */
typedef struct {
	enum : uint16 {
		REJECTED_NOT_UNDERSTOOD	= 0x0000, /**< Command not understood. */
		REJECTED_MTU_EXCEEDED	= 0x0001, /**< Signalling MTU exceeded. */
		REJECTED_INVALID_CID	= 0x0002, /**< Invalid channel ID. */
		/* 0x0003-0xffff: reserved */
	}; uint16 reason;
	/* data may follow */
} _PACKED l2cap_command_reject;

/** @brief Optional data appended to an l2cap_command_reject for specific
 *         rejection reasons. */
typedef union {
	struct {
		uint16	mtu; /* actual signaling MTU */
	} _PACKED mtu_exceeded;
	struct {
		uint16	scid; /* source (local) CID */
		uint16	dcid; /* destination (remote) CID */
	} _PACKED invalid_cid;
} l2cap_command_reject_data;


/** @brief Opcode for a Connection Request command. */
#define L2CAP_CONNECTION_REQ	0x02

/** @brief Parameters of an L2CAP Connection Request command. */
typedef struct {
	uint16	psm;
	uint16	scid; /* source channel ID */
} _PACKED l2cap_connection_req;

/** @brief Opcode for a Connection Response command. */
#define L2CAP_CONNECTION_RSP	0x03

/** @brief Parameters of an L2CAP Connection Response command. */
typedef struct {
	uint16	dcid;   /* destination channel ID */
	uint16	scid;   /* source channel ID */
	enum : uint16 {
		RESULT_SUCCESS					= 0x0000, /**< Connection accepted. */
		RESULT_PENDING					= 0x0001, /**< Connection pending. */
		RESULT_PSM_NOT_SUPPORTED		= 0x0002, /**< PSM not supported. */
		RESULT_SECURITY_BLOCK			= 0x0003, /**< Security requirements not met. */
		RESULT_NO_RESOURCES				= 0x0004, /**< No resources available. */
		RESULT_INVALID_SCID				= 0x0005, /**< Invalid source CID. */
		RESULT_SCID_ALREADY_ALLOCATED	= 0x0006, /**< Source CID already in use. */
		/* 0x0007-0xffff: reserved */
	}; uint16 result;
	enum : uint16 {
		NO_STATUS_INFO					= 0x0000, /**< No further status info. */
		STATUS_AUTHENTICATION_PENDING	= 0x0001, /**< Authentication in progress. */
		STATUS_AUTHORIZATION_PENDING	= 0x0002, /**< Authorization in progress. */
		/* 0x0003-0xffff: reserved */
	}; uint16 status; /* only defined if result = pending */
} _PACKED l2cap_connection_rsp;


/** @brief Opcode for a Configuration Request command. */
#define L2CAP_CONFIGURATION_REQ	0x04

/** @brief Parameters of an L2CAP Configuration Request command. */
typedef struct {
	uint16	dcid;  /* destination channel ID */
	uint16	flags;
	/* options may follow */
} _PACKED l2cap_configuration_req;

/** @brief Opcode for a Configuration Response command. */
#define L2CAP_CONFIGURATION_RSP	0x05

/** @brief Parameters of an L2CAP Configuration Response command. */
typedef struct {
	uint16	scid;   /* source channel ID */
	uint16	flags;
/** @brief Flag bit indicating that further configuration fragments follow. */
#define L2CAP_CFG_FLAG_CONTINUATION		0x0001
	enum : uint16 {
		RESULT_SUCCESS				= 0x0000, /**< Configuration accepted. */
		RESULT_UNACCEPTABLE_PARAMS	= 0x0001, /**< Parameters not acceptable. */
		RESULT_REJECTED				= 0x0002, /**< Configuration rejected. */
		RESULT_UNKNOWN_OPTION		= 0x0003, /**< Unknown option received. */
		RESULT_PENDING				= 0x0004, /**< Configuration pending. */
		RESULT_FLOW_SPEC_REJECTED	= 0x0005, /**< Flow specification rejected. */
		/* 0x0006-0xffff: reserved */
	}; uint16 result;
	/* options may follow */
} _PACKED l2cap_configuration_rsp;

/** @brief TLV header for a single L2CAP configuration option. */
typedef struct {
	enum : uint8 {
		OPTION_MTU				= 0x01, /**< MTU option type. */
		OPTION_FLUSH_TIMEOUT	= 0x02, /**< Flush timeout option type. */
		OPTION_QOS				= 0x03, /**< Quality of Service option type. */

		OPTION_HINT_BIT			= 0x80, /**< Hint bit: option may be ignored if unrecognised. */
	}; uint8 type;
	uint8 length;
	/* value follows */
} _PACKED l2cap_configuration_option;

/** @brief Quality of Service flow specification used in L2CAP configuration. */
typedef struct {
	uint8 flags;				/* reserved for future use */
	uint8 service_type;			/* 1 = best effort */
	uint32 token_rate;			/* average bytes per second */
	uint32 token_bucket_size;	/* max burst bytes */
	uint32 peak_bandwidth;		/* bytes per second */
	uint32 access_latency;		/* microseconds */
	uint32 delay_variation;		/* microseconds */
} _PACKED l2cap_qos;

/** @brief Union of possible configuration option values indexed by option type. */
typedef union {
	uint16		mtu;           /**< MTU value for OPTION_MTU. */
	uint16		flush_timeout; /**< Flush timeout for OPTION_FLUSH_TIMEOUT. */
	l2cap_qos	qos;           /**< QoS flow spec for OPTION_QOS. */
} l2cap_configuration_option_value;


/** @brief Opcode for a Disconnection Request command. */
#define L2CAP_DISCONNECTION_REQ	0x06

/** @brief Parameters of an L2CAP Disconnection Request command. */
typedef struct {
	uint16	dcid; /* destination channel ID */
	uint16	scid; /* source channel ID */
} _PACKED l2cap_disconnection_req;

/** @brief Opcode for a Disconnection Response command. */
#define L2CAP_DISCONNECTION_RSP	0x07

/** @brief Parameters of an L2CAP Disconnection Response (mirrors the request). */
typedef l2cap_disconnection_req l2cap_disconnection_rsp;


/** @brief Opcode for an Echo Request command. */
#define L2CAP_ECHO_REQ	0x08

/** @brief Opcode for an Echo Response command. */
#define L2CAP_ECHO_RSP	0x09

/** @brief Maximum data payload for an echo command. */
#define L2CAP_MAX_ECHO_SIZE \
	(L2CAP_MTU_MAXIMUM - sizeof(l2cap_command_header))


/** @brief Opcode for an Information Request command. */
#define L2CAP_INFORMATION_REQ	0x0a

/** @brief Parameters of an L2CAP Information Request command. */
typedef struct {
	enum : uint16 {
		TYPE_CONNECTIONLESS_MTU	= 0x0001, /**< Request the connectionless MTU. */
		TYPE_EXTENDED_FEATURES	= 0x0002, /**< Request extended feature mask. */
		TYPE_FIXED_CHANNELS		= 0x0003, /**< Request supported fixed channels. */
			/* 0x0004-0xffff: reserved */
	}; uint16 type;
} _PACKED l2cap_information_req;

/** @brief Opcode for an Information Response command. */
#define L2CAP_INFORMATION_RSP	0x0b

/** @brief Parameters of an L2CAP Information Response command. */
typedef struct {
	uint16	type;
	enum : uint16 {
		RESULT_SUCCESS			= 0x0000, /**< Information request succeeded. */
		RESULT_NOT_SUPPORTED	= 0x0001, /**< Requested info type not supported. */
	}; uint16 result;
	/* data may follow */
} _PACKED l2cap_information_rsp;

/** @brief Union of possible data payloads appended to an information response. */
typedef union {
	uint16 mtu;                /**< Connectionless MTU value. */
	uint32 extended_features;  /**< Extended features bitmask. */
} _PACKED l2cap_information_rsp_data;


#endif /* _L2CAP_H_ */
