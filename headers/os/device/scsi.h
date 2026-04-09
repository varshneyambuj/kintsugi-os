/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * This file incorporates work from the Haiku project:
 *   Copyright 2009, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author:
 *   Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file scsi.h
 *  @brief SCSI device type constants, ioctl codes, and data structures for disk/CD/audio control. */

#ifndef _SCSI_H
#define _SCSI_H


#include <Drivers.h>
#include <OS.h>
#include <SupportDefs.h>


/* SCSI device types */
#define B_SCSI_DISK		0x00
#define B_SCSI_TAPE		0x01
#define B_SCSI_PRINTER	0x02
#define B_SCSI_CPU		0x03
#define B_SCSI_WORM		0x04
#define B_SCSI_CD		0x05
#define B_SCSI_SCANNER	0x06
#define B_SCSI_OPTICAL	0x07
#define B_SCSI_JUKEBOX	0x08
#define B_SCSI_NETWORK	0x09


/* SCSI device masks */
#define B_SCSI_ALL_DEVICES_MASK		0xffffffff
#define B_SCSI_DISK_MASK			(1 << (B_SCSI_DISK))
#define B_SCSI_TAPE_MASK			(1 << (B_SCSI_TAPE))
#define B_SCSI_PRINTER_MASK			(1 << (B_SCSI_PRINTER))
#define B_SCSI_CPU_MASK				(1 << (B_SCSI_CPU))
#define B_SCSI_WORM_MASK			(1 << (B_SCSI_WORM))
#define B_SCSI_CD_MASK				(1 << (B_SCSI_CD))
#define B_SCSI_SCANNER_MASK			(1 << (B_SCSI_SCANNER))
#define B_SCSI_OPTICAL_MASK			(1 << (B_SCSI_OPTICAL))
#define B_SCSI_JUKEBOX_MASK			(1 << (B_SCSI_JUKEBOX))
#define B_SCSI_NETWORK_MASK			(1 << (B_SCSI_NETWORK))


/** @brief General SCSI bus management ioctl codes. */
enum {
	B_SCSI_SCAN_FOR_DEVICES = B_DEVICE_OP_CODES_END + 1,
	B_SCSI_ENABLE_PROFILING
};


/** @brief SCSI device ioctl codes for inquiry, eject, and raw command passthrough. */
enum {
	B_SCSI_INQUIRY = B_DEVICE_OP_CODES_END + 100,
	B_SCSI_EJECT,
	B_SCSI_PREVENT_ALLOW,
	B_RAW_DEVICE_COMMAND
};


/** @brief Buffer for SCSI INQUIRY response data. */
typedef struct {
	uchar	inquiry_data[36];
} scsi_inquiry;


/** @brief SCSI CD audio ioctl codes. */
enum {
	B_SCSI_GET_TOC = B_DEVICE_OP_CODES_END + 200,
	B_SCSI_PLAY_TRACK,
	B_SCSI_PLAY_POSITION,
	B_SCSI_STOP_AUDIO,
	B_SCSI_PAUSE_AUDIO,
	B_SCSI_RESUME_AUDIO,
	B_SCSI_GET_POSITION,
	B_SCSI_SET_VOLUME,
	B_SCSI_GET_VOLUME,
	B_SCSI_READ_CD,
	B_SCSI_SCAN,
	B_SCSI_DATA_MODE
};


/** @brief Buffer for the full CD Table of Contents. */
typedef struct {
	uchar	toc_data[804];
} scsi_toc;


/** @brief Specifies a track/index range for CD audio playback. */
typedef struct {
	uchar	start_track;
	uchar	start_index;
	uchar	end_track;
	uchar	end_index;
} scsi_play_track;


/** @brief Specifies an MSF (minute/second/frame) position range for CD audio playback. */
typedef struct {
	uchar	start_m;
	uchar	start_s;
	uchar	start_f;
	uchar	end_m;
	uchar	end_s;
	uchar	end_f;
} scsi_play_position;


/** @brief Current CD playback position data returned by B_SCSI_GET_POSITION. */
typedef struct {
	uchar	position[16];
} scsi_position;


/** @brief CD audio volume and channel routing settings used by B_SCSI_SET/GET_VOLUME. */
typedef struct {
	uchar	flags;
	uchar	port0_channel;
	uchar	port0_volume;
	uchar	port1_channel;
	uchar	port1_volume;
	uchar	port2_channel;
	uchar	port2_volume;
	uchar	port3_channel;
	uchar	port3_volume;
} scsi_volume;


#define B_SCSI_PORT0_CHANNEL	0x01
#define B_SCSI_PORT0_VOLUME		0x02
#define B_SCSI_PORT1_CHANNEL	0x04
#define B_SCSI_PORT1_VOLUME		0x08
#define B_SCSI_PORT2_CHANNEL	0x10
#define B_SCSI_PORT2_VOLUME		0x20
#define B_SCSI_PORT3_CHANNEL	0x40
#define B_SCSI_PORT3_VOLUME		0x80


/** @brief Parameters for reading raw CD sector data via B_SCSI_READ_CD. */
typedef struct {
	uchar	start_m;
	uchar	start_s;
	uchar	start_f;
	uchar	length_m;
	uchar	length_s;
	uchar	length_f;
	long	buffer_length;
	char*	buffer;
	bool	play;
} scsi_read_cd;


/** @brief Parameters for CD scanning (fast-forward / rewind) via B_SCSI_SCAN. */
typedef struct {
	char	speed;
	char	direction;
} scsi_scan;


/** @brief Block address and data mode for B_SCSI_DATA_MODE queries. */
typedef struct {
	off_t	block;
	int32	mode;
} scsi_data_mode;


/** @brief Parameters for a raw SCSI pass-through command (B_RAW_DEVICE_COMMAND). */
typedef struct {
	uint8		command[16];
	uint8		command_length;
	uint8		flags;
	uint8   	scsi_status;
	uint8   	cam_status;
	void*		data;
	size_t		data_length;
	void*		sense_data;
	size_t		sense_data_length;
	bigtime_t	timeout;
} raw_device_command;


/** @brief Flags for the raw_device_command flags field. */
enum {
	B_RAW_DEVICE_DATA_IN			= 0x01,
	B_RAW_DEVICE_REPORT_RESIDUAL	= 0x02,
	B_RAW_DEVICE_SHORT_READ_VALID	= 0x04
};

#endif /* _SCSI_H */
