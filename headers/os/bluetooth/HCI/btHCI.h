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

/** @file btHCI.h
 *  @brief Core HCI type definitions, packet type enumerations, size constants, LMP feature flags, and diagnostic helpers. */

#ifndef _BTHCI_H_
#define _BTHCI_H_

/* typedefs */
/** @brief Opaque integer identifier for a local HCI device instance. */
typedef int32 hci_id;
/** @brief Offset added to the raw device index to produce an hci_id. */
#define HCI_DEVICE_INDEX_OFFSET 0x7c

/** @brief Bluetooth HCI transport layer variant identifiers. */
typedef enum { H2 = 2, H3, H4, H5 } transport_type;

/** @brief HCI packet type discriminator, used to route packets between layers. */
typedef enum {
				BT_COMMAND = 0, /**< HCI command packet. */
				BT_EVENT,       /**< HCI event packet. */
				BT_ACL,         /**< ACL data packet. */
				BT_SCO,         /**< SCO data packet. */
				BT_ESCO,        /**< eSCO data packet. */
				// more packets here
				HCI_NUM_PACKET_TYPES /**< Sentinel: total number of packet type entries. */
} bt_packet_t;

/** @brief Returns a human-readable string for the given HCI command opcode.
 *  @param opcode 16-bit packed opcode (OGF + OCF).
 *  @return Null-terminated string describing the command, or an unknown-command label. */
const char* BluetoothCommandOpcode(uint16 opcode);

/** @brief Returns a human-readable string for the given HCI event code.
 *  @param event 8-bit HCI event code.
 *  @return Null-terminated string describing the event. */
const char* BluetoothEvent(uint8 event);

/** @brief Returns the name of the Bluetooth chip manufacturer identified by the given code.
 *  @param manufacturer 16-bit manufacturer identifier from the local version info.
 *  @return Null-terminated string containing the manufacturer name. */
const char* BluetoothManufacturer(uint16 manufacturer);

/** @brief Returns a human-readable label for the given HCI version number.
 *  @param ver 8-bit HCI version value.
 *  @return Null-terminated string such as "Bluetooth 2.0". */
const char* BluetoothHciVersion(uint16 ver);

/** @brief Returns a human-readable label for the given LMP version number.
 *  @param ver 8-bit LMP version value.
 *  @return Null-terminated string such as "Bluetooth 2.0". */
const char* BluetoothLmpVersion(uint16 ver);

/** @brief Returns a human-readable description for the given Bluetooth error code.
 *  @param error 8-bit HCI error/status code.
 *  @return Null-terminated string describing the error condition. */
const char* BluetoothError(uint8 error);

/* packets sizes */
/** @brief Maximum payload size of an ACL packet in bytes. */
#define HCI_MAX_ACL_SIZE		1024
/** @brief Maximum payload size of an SCO packet in bytes. */
#define HCI_MAX_SCO_SIZE		255
/** @brief Maximum total size of an HCI event packet in bytes. */
#define HCI_MAX_EVENT_SIZE		260
/** @brief Maximum size of a complete HCI frame (ACL payload + 4-byte header). */
#define HCI_MAX_FRAME_SIZE		(HCI_MAX_ACL_SIZE + 4)

/* fields sizes */
/** @brief Size of a Lower Address Part (LAP) field in bytes. */
#define HCI_LAP_SIZE			3	/* LAP */
/** @brief Size of a link key in bytes. */
#define HCI_LINK_KEY_SIZE		16	/* link key */
/** @brief Size of a PIN code in bytes. */
#define HCI_PIN_SIZE			16	/* PIN */
/** @brief Size of an event mask field in bytes. */
#define HCI_EVENT_MASK_SIZE		8	/* event mask */
/** @brief Size of a Class of Device field in bytes. */
#define HCI_CLASS_SIZE			3	/* class */
/** @brief Size of the LMP features field in bytes. */
#define HCI_FEATURES_SIZE		8	/* LMP features */
/** @brief Maximum size of a Bluetooth unit name string in bytes (including NUL). */
#define HCI_DEVICE_NAME_SIZE	248	/* unit name size */

// HCI Packet types
/** @brief ACL packet type: 2-DH1 (EDR, 2 Mbps, 1 slot). */
#define HCI_2DH1        0x0002
/** @brief ACL packet type: 3-DH1 (EDR, 3 Mbps, 1 slot). */
#define HCI_3DH1        0x0004
/** @brief ACL packet type: DM1 (1 slot, mandatory). */
#define HCI_DM1         0x0008
/** @brief ACL packet type: DH1 (1 slot). */
#define HCI_DH1         0x0010
/** @brief ACL packet type: 2-DH3 (EDR, 2 Mbps, 3 slots). */
#define HCI_2DH3        0x0100
/** @brief ACL packet type: 3-DH3 (EDR, 3 Mbps, 3 slots). */
#define HCI_3DH3        0x0200
/** @brief ACL packet type: DM3 (3 slots). */
#define HCI_DM3         0x0400
/** @brief ACL packet type: DH3 (3 slots). */
#define HCI_DH3         0x0800
/** @brief ACL packet type: 2-DH5 (EDR, 2 Mbps, 5 slots). */
#define HCI_2DH5        0x1000
/** @brief ACL packet type: 3-DH5 (EDR, 3 Mbps, 5 slots). */
#define HCI_3DH5        0x2000
/** @brief ACL packet type: DM5 (5 slots). */
#define HCI_DM5         0x4000
/** @brief ACL packet type: DH5 (5 slots). */
#define HCI_DH5         0x8000

/** @brief SCO packet type: HV1 (voice, 1 slot, no FEC). */
#define HCI_HV1         0x0020
/** @brief SCO packet type: HV2 (voice, 1 slot, 2/3 FEC). */
#define HCI_HV2         0x0040
/** @brief SCO packet type: HV3 (voice, 1 slot, 1/3 FEC). */
#define HCI_HV3         0x0080

/** @brief eSCO packet type: EV3. */
#define HCI_EV3         0x0008
/** @brief eSCO packet type: EV4. */
#define HCI_EV4         0x0010
/** @brief eSCO packet type: EV5. */
#define HCI_EV5         0x0020
/** @brief eSCO packet type: 2-EV3 (EDR). */
#define HCI_2EV3        0x0040
/** @brief eSCO packet type: 3-EV3 (EDR). */
#define HCI_3EV3        0x0080
/** @brief eSCO packet type: 2-EV5 (EDR). */
#define HCI_2EV5        0x0100
/** @brief eSCO packet type: 3-EV5 (EDR). */
#define HCI_3EV5        0x0200

/** @brief Bitmask of all SCO packet types. */
#define SCO_PTYPE_MASK  (HCI_HV1 | HCI_HV2 | HCI_HV3)
/** @brief Bitmask of all ACL packet types. */
#define ACL_PTYPE_MASK  (HCI_DM1 | HCI_DH1 | HCI_DM3 | HCI_DH3 | HCI_DM5 | HCI_DH5)


// LMP features
/** @brief LMP feature: 3-slot packets supported. */
#define LMP_3SLOT       0x01
/** @brief LMP feature: 5-slot packets supported. */
#define LMP_5SLOT       0x02
/** @brief LMP feature: encryption supported. */
#define LMP_ENCRYPT     0x04
/** @brief LMP feature: slot offset supported. */
#define LMP_SOFFSET     0x08
/** @brief LMP feature: timing accuracy supported. */
#define LMP_TACCURACY   0x10
/** @brief LMP feature: role switch supported. */
#define LMP_RSWITCH     0x20
/** @brief LMP feature: hold mode supported. */
#define LMP_HOLD        0x40
/** @brief LMP feature: sniff mode supported. */
#define LMP_SNIFF       0x80

/** @brief LMP feature: park state supported. */
#define LMP_PARK        0x01
/** @brief LMP feature: RSSI measurement supported. */
#define LMP_RSSI        0x02
/** @brief LMP feature: data rate quality measurement supported. */
#define LMP_QUALITY     0x04
/** @brief LMP feature: SCO link supported. */
#define LMP_SCO         0x08
/** @brief LMP feature: HV2 packets supported. */
#define LMP_HV2         0x10
/** @brief LMP feature: HV3 packets supported. */
#define LMP_HV3         0x20
/** @brief LMP feature: u-law log synchronous data supported. */
#define LMP_ULAW        0x40
/** @brief LMP feature: A-law log synchronous data supported. */
#define LMP_ALAW        0x80

/** @brief LMP feature: CVSD synchronous data supported. */
#define LMP_CVSD        0x01
/** @brief LMP feature: paging scheme supported. */
#define LMP_PSCHEME     0x02
/** @brief LMP feature: power control supported. */
#define LMP_PCONTROL    0x04
/** @brief LMP feature: transparent synchronous data supported. */
#define LMP_TRSP_SCO    0x08
/** @brief LMP feature: broadcast encryption supported. */
#define LMP_BCAST_ENC   0x80

/** @brief LMP feature: EDR ACL 2 Mbps mode supported. */
#define LMP_EDR_ACL_2M  0x02
/** @brief LMP feature: EDR ACL 3 Mbps mode supported. */
#define LMP_EDR_ACL_3M  0x04
/** @brief LMP feature: enhanced inquiry scan supported. */
#define LMP_ENH_ISCAN   0x08
/** @brief LMP feature: interlaced inquiry scan supported. */
#define LMP_ILACE_ISCAN 0x10
/** @brief LMP feature: interlaced page scan supported. */
#define LMP_ILACE_PSCAN 0x20
/** @brief LMP feature: RSSI with inquiry results supported. */
#define LMP_RSSI_INQ    0x40
/** @brief LMP feature: eSCO link supported. */
#define LMP_ESCO        0x80

/** @brief LMP feature: EV4 eSCO packets supported. */
#define LMP_EV4         0x01
/** @brief LMP feature: EV5 eSCO packets supported. */
#define LMP_EV5         0x02
/** @brief LMP feature: AFH capable slave supported. */
#define LMP_AFH_CAP_SLV 0x08
/** @brief LMP feature: AFH classification slave supported. */
#define LMP_AFH_CLS_SLV 0x10
/** @brief LMP feature: EDR 3-slot eSCO packets supported. */
#define LMP_EDR_3SLOT   0x80

/** @brief LMP feature: EDR 5-slot eSCO packets supported. */
#define LMP_EDR_5SLOT   0x01
/** @brief LMP feature: sniff subrating supported. */
#define LMP_SNIFF_SUBR  0x02
/** @brief LMP feature: pause encryption supported. */
#define LMP_PAUSE_ENC   0x04
/** @brief LMP feature: AFH capable master supported. */
#define LMP_AFH_CAP_MST 0x08
/** @brief LMP feature: AFH classification master supported. */
#define LMP_AFH_CLS_MST 0x10
/** @brief LMP feature: EDR eSCO 2 Mbps mode supported. */
#define LMP_EDR_ESCO_2M 0x20
/** @brief LMP feature: EDR eSCO 3 Mbps mode supported. */
#define LMP_EDR_ESCO_3M 0x40
/** @brief LMP feature: EDR 3-slot eSCO packets supported (alternate bit). */
#define LMP_EDR_3S_ESCO 0x80

/** @brief LMP feature: extended inquiry response supported. */
#define LMP_EXT_INQ     0x01
/** @brief LMP feature: Secure Simple Pairing supported. */
#define LMP_SIMPLE_PAIR 0x08
/** @brief LMP feature: encapsulated PDU supported. */
#define LMP_ENCAPS_PDU  0x10
/** @brief LMP feature: error data reporting supported. */
#define LMP_ERR_DAT_REP 0x20
/** @brief LMP feature: non-flushable packet boundary flag supported. */
#define LMP_NFLUSH_PKTS 0x40

/** @brief LMP feature: link supervision timeout changed event supported. */
#define LMP_LSTO        0x01
/** @brief LMP feature: inquiry TX power level supported. */
#define LMP_INQ_TX_PWR  0x02
/** @brief LMP feature: extended features pages available. */
#define LMP_EXT_FEAT    0x80

// Link policies
/** @brief Link policy: role switch allowed. */
#define HCI_LP_RSWITCH  0x0001
/** @brief Link policy: hold mode allowed. */
#define HCI_LP_HOLD     0x0002
/** @brief Link policy: sniff mode allowed. */
#define HCI_LP_SNIFF    0x0004
/** @brief Link policy: park state allowed. */
#define HCI_LP_PARK     0x0008

// Link mode
/** @brief Link mode flag: accept connections automatically. */
#define HCI_LM_ACCEPT   0x8000
/** @brief Link mode flag: this device is the master. */
#define HCI_LM_MASTER   0x0001
/** @brief Link mode flag: authentication required. */
#define HCI_LM_AUTH     0x0002
/** @brief Link mode flag: encryption required. */
#define HCI_LM_ENCRYPT  0x0004
/** @brief Link mode flag: device is trusted. */
#define HCI_LM_TRUSTED  0x0008
/** @brief Link mode flag: reliable (retransmission) mode enabled. */
#define HCI_LM_RELIABLE 0x0010
/** @brief Link mode flag: secure connection required. */
#define HCI_LM_SECURE   0x0020


#endif // _BTHCI_H_

