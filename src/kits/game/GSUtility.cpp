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
 *   Copyright 2001-2012 Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christopher ML Zumwalt May (zummy@users.sf.net)
 */


/**
 * @file GSUtility.cpp
 * @brief Internal utility functions for the Game Kit sound system
 *
 * Provides helper functions shared across the Game Kit sound classes:
 * smooth value ramping (for gain and pan transitions), per-sample-format size
 * queries, and conversion between MediaKit and Game Kit audio format structs.
 *
 * @see GameSoundBuffer.cpp, GameSound.cpp
 */


#include "GSUtility.h"

#include <GameSoundDefs.h>
#include <MediaDefs.h>

#include <new>


/**
 * @brief Allocates and initialises a linear ramp towards a target value.
 *
 * The ramp increments \a value by a small step on every call to ChangeRamp()
 * until the target \a set is reached. The step size is derived from the total
 * difference, the sample rate, and the desired \a duration.
 *
 * @param value Pointer to the float variable that will be ramped in-place.
 * @param set   The target value to ramp towards.
 * @param frames The playback frame rate (frames per second).
 * @param duration The desired ramp duration in microseconds.
 * @return A newly allocated _gs_ramp, or NULL on allocation failure.
 */
_gs_ramp*
InitRamp(float* value, float set, float frames, bigtime_t duration)
{
	float diff = (set > *value) ? set - *value : *value - set;
	float sec = float(duration) / 1000000.0;
	float inc = diff * 200;

	_gs_ramp* ramp = new (std::nothrow) _gs_ramp;
	if (ramp != NULL) {
		ramp->value = value;

		ramp->frame_total = frames * sec;
		ramp->frame_inc = int(ramp->frame_total / inc);

		ramp->inc = (set - *value) / inc;

		ramp->frame_count = 0;
		ramp->frame_inc_count = 0;

		ramp->duration = duration;
	}
	return ramp;
}


/**
 * @brief Advances a ramp by one frame, updating the target value as needed.
 *
 * Should be called once per audio frame while the ramp is active. When the
 * ramp reaches its total frame count the target value is considered reached
 * and the caller should delete the ramp.
 *
 * @param ramp The active ramp to advance.
 * @return \c true if the ramp has completed and should be deleted,
 *         \c false if it is still in progress.
 */
bool
ChangeRamp(_gs_ramp* ramp)
{
	if (ramp->frame_count > ramp->frame_total)
		return true;

	if (ramp->frame_inc_count >= ramp->frame_inc) {
		ramp->frame_inc_count = 0;
		*ramp->value += ramp->inc;
	} else
		ramp->frame_inc_count++;

	ramp->frame_count++;
	return false;
}


/**
 * @brief Returns the size in bytes of a single audio sample for the given format.
 *
 * Handles both \c media_raw_audio_format and \c gs_audio_format format
 * constants. Returns 0 for unrecognised formats.
 *
 * @param format A sample format constant (e.g. \c gs_audio_format::B_GS_S16).
 * @return The size of one sample in bytes, or 0 if the format is unknown.
 */
size_t
get_sample_size(int32 format)
{
	size_t sample;

	switch(format) {
		case media_raw_audio_format::B_AUDIO_CHAR:
			sample  = sizeof(char);
			break;

		case gs_audio_format::B_GS_U8:
			sample = sizeof(uint8);
			break;

		case gs_audio_format::B_GS_S16:
			sample = sizeof(int16);
			break;

		case gs_audio_format::B_GS_S32:
			sample = sizeof(int32);
			break;

		case gs_audio_format::B_GS_F:
			sample = sizeof(float);
			break;

		default:
			sample = 0;
			break;
	}

	return sample;
}


/**
 * @brief Copies audio format fields from a MediaKit struct into a Game Kit struct.
 *
 * Maps \c media_raw_audio_format fields (format, frame_rate, channel_count,
 * byte_order, buffer_size) to the corresponding \c gs_audio_format fields.
 *
 * @param dest   Destination \c gs_audio_format to populate.
 * @param source Source \c media_raw_audio_format to read from.
 */
void
media_to_gs_format(gs_audio_format* dest, media_raw_audio_format* source)
{
	dest->format = source->format;
	dest->frame_rate = source->frame_rate;
	dest->channel_count = source->channel_count;
	dest->byte_order = source->byte_order;
	dest->buffer_size = source->buffer_size;
}
