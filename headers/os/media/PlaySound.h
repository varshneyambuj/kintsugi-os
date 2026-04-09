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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from Haiku, Inc. covered by:
 * Copyright 2009, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file PlaySound.h
 *  @brief Provides simple C-level functions for playing system sounds.
 */

#ifndef _PLAY_SOUND_H
#define _PLAY_SOUND_H

#include <OS.h>
#include <Entry.h>


/** @brief Opaque handle returned by play_sound() and used with stop_sound()/wait_for_sound(). */
typedef sem_id sound_handle;


/** @brief Plays a sound file identified by an entry_ref.
 *  @param soundRef Entry reference to the sound file to play.
 *  @param mix True to mix this sound with other currently playing sounds.
 *  @param queue True to queue this sound to play after the current sound finishes.
 *  @param background True to play in the background (non-blocking).
 *  @return A sound_handle on success, or a negative error code.
 */
sound_handle play_sound(const entry_ref* soundRef, bool mix, bool queue,
	bool background);

/** @brief Stops a playing sound identified by its handle.
 *  @param handle The sound_handle returned by play_sound().
 *  @return B_OK on success, or an error code.
 */
status_t stop_sound(sound_handle handle);

/** @brief Blocks until the sound identified by the handle finishes playing.
 *  @param handle The sound_handle returned by play_sound().
 *  @return B_OK on success, or an error code.
 */
status_t wait_for_sound(sound_handle handle);


#endif // _PLAY_SOUND_H
