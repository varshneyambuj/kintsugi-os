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
 * Incorporates work from the Haiku project, originally copyrighted:
 *   Copyright 2020, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author (Kintsugi OS): Ambuj Varshney <ambuj@kintsugi-os.org>
 */

/** @file GameSoundDefs.h
 *  @brief Common type definitions, constants, and structures for the Game Sound Kit.
 */

#ifndef _GAME_SOUND_DEFS_H
#define _GAME_SOUND_DEFS_H


#include <SupportDefs.h>


/** @brief Opaque integer handle identifying a game sound instance. */
typedef int32 gs_id;

/** @brief Current API version of the Game Sound Kit. */
#define B_GS_CUR_API_VERSION B_BEOS_VERSION
/** @brief Minimum API version supported by the Game Sound Kit. */
#define B_GS_MIN_API_VERSION 0x100
/** @brief Sentinel gs_id value representing an invalid or unallocated sound. */
#define B_GS_INVALID_SOUND ((gs_id)-1)
/** @brief Sentinel gs_id value representing the main/default sound output. */
#define B_GS_MAIN_SOUND ((gs_id)-2)


/** @brief Error codes specific to the Game Sound Kit. */
enum {
	B_GS_BAD_HANDLE = -99999,
	B_GS_NO_SOUNDS,
	B_GS_NO_HARDWARE,
	B_GS_ALREADY_COMMITTED,
	B_GS_READ_ONLY_VALUE
};


/** @brief Describes the audio format used by a game sound buffer. */
struct gs_audio_format {
	/** @brief Sample format constants. */
	enum format {
		B_GS_U8  = 0x11, /**< Unsigned 8-bit PCM. */
		B_GS_S16 = 0x2,  /**< Signed 16-bit PCM. */
		B_GS_F   = 0x24, /**< 32-bit floating-point PCM. */
		B_GS_S32 = 0x4   /**< Signed 32-bit PCM. */
	};
	float	frame_rate;     /**< Frames (samples) per second. */
	uint32	channel_count;  /**< Number of audio channels. */
	uint32	format;         /**< Sample format; one of the format enum values. */
	uint32	byte_order;     /**< Byte order of multi-byte samples. */
	size_t	buffer_size;    /**< Preferred buffer size in bytes. */
};


/** @brief Identifiers for adjustable sound attributes. */
enum gs_attributes {
	B_GS_NO_ATTRIBUTE = 0,
	B_GS_MAIN_GAIN = 1,
	B_GS_CD_THROUGH_GAIN,
	B_GS_GAIN = 128,
	B_GS_PAN,
	B_GS_SAMPLING_RATE,
	B_GS_LOOPING,
	B_GS_FIRST_PRIVATE_ATTRIBUTE = 90000,
	B_GS_FIRST_USER_ATTRIBUTE = 100000
};


/** @brief Represents a single attribute value applied over a duration to a sound. */
struct gs_attribute {
	int32		attribute; /**< Attribute identifier from gs_attributes. */
	bigtime_t	duration;  /**< Time in microseconds over which the change is applied. */
	float		value;     /**< Target value for the attribute. */
	uint32		flags;     /**< Modifier flags for the attribute change. */
};


/** @brief Describes the valid range and granularity for a specific sound attribute. */
struct gs_attribute_info {
	int32	attribute;   /**< Attribute identifier from gs_attributes. */
	float	granularity; /**< Smallest meaningful step for this attribute. */
	float	minimum;     /**< Minimum allowed value. */
	float	maximum;     /**< Maximum allowed value. */
};


#endif
