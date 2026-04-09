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
 *   Copyright (c) 2001-2002, Haiku. All rights reserved.
 *   Author: Christopher ML Zumwalt May (zummy@users.sf.net)
 *   Distributed under the terms of the MIT License.
 *
 * Author (Kintsugi OS): Ambuj Varshney <ambuj@kintsugi-os.org>
 */

/** @file GSUtility.h
 *  @brief Internal utility types and functions for the Game Sound Kit.
 */

#ifndef _GAMESOUND_UTILITY_H
#define _GAMESOUND_UTILITY_H


#include <GameSoundDefs.h>
#include <MediaDefs.h>


/** @brief Tracks a linear ramp applied to a floating-point parameter over time. */
struct _gs_ramp
{
	float inc;           /**< Increment applied per step. */
	float orignal;       /**< Original value before the ramp started. */
	float* value;        /**< Pointer to the parameter being ramped. */

	float frame_total;   /**< Total number of frames over which the ramp runs. */
	float frame_inc;     /**< Number of frames between ramp steps. */

	float frame_count;   /**< Frames elapsed so far. */
	float frame_inc_count; /**< Frames counted within the current step. */

	bigtime_t duration;  /**< Total ramp duration in microseconds. */
};


/** @brief Initialises and allocates a new ramp descriptor.
 *  @param value    Pointer to the float parameter to ramp.
 *  @param set      Target value to reach at the end of the ramp.
 *  @param frames   Total number of audio frames over which to ramp.
 *  @param duration Total duration of the ramp in microseconds.
 *  @return Newly allocated _gs_ramp, or NULL on allocation failure.
 */
_gs_ramp* InitRamp(float* value, float set, float frames, bigtime_t duration);

/** @brief Advances a ramp by one step and updates the target parameter.
 *  @param ramp Pointer to the _gs_ramp to advance.
 *  @return True if the ramp is complete, false if still in progress.
 */
bool ChangeRamp(_gs_ramp* ramp);

/** @brief Returns the number of bytes per sample for the given gs_audio_format format code.
 *  @param format A format code from gs_audio_format::format.
 *  @return Sample size in bytes.
 */
size_t get_sample_size(int32 format);

/** @brief Converts a media_raw_audio_format descriptor to a gs_audio_format descriptor.
 *  @param dest   Pointer to the destination gs_audio_format to fill.
 *  @param source Pointer to the source media_raw_audio_format.
 */
void media_to_gs_format(gs_audio_format* dest,
	media_raw_audio_format* source);


/** @brief Clamps a float value to the range [min, max] and casts it to type T.
 *  @tparam T   Target type for the return value.
 *  @tparam min Minimum bound (inclusive).
 *  @tparam max Maximum bound (inclusive).
 *  @param  value The float value to clamp.
 *  @return The clamped value cast to T.
 */
template<typename T, int32 min, int32 max>
static inline T clamp(float value)
{
	if (value <= min)
		return min;
	if (value >= max)
		return max;
	return T(value);
}
#endif
