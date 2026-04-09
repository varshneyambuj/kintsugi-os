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
 *   AUTHOR: Marcus Overhagen
 *     FILE: PlaySound.cpp
 */

/** @file PlaySound.cpp
 *  @brief Implements the legacy C-level sound playback API (play_sound,
 *         stop_sound, wait_for_sound). These functions are currently stubs.
 */

#include <BeBuild.h>
#include <OS.h>
#include <Entry.h>

#include <PlaySound.h>
#include "MediaDebug.h"

/**
 * @brief Begin asynchronous playback of a sound file (not yet implemented).
 *
 * @param soundRef   Entry ref pointing to the sound file to play.
 * @param mix        If true, mix with other currently playing sounds.
 * @param queue      If true, queue the sound after any currently playing sound.
 * @param background If true, play in the background without blocking.
 * @return A sound_handle identifying this playback instance, or an error code.
 */
sound_handle play_sound(const entry_ref *soundRef,
						bool mix,
						bool queue,
						bool background
						)
{
	UNIMPLEMENTED();
	return (sound_handle)1;
}

/**
 * @brief Stop a previously started sound (not yet implemented).
 *
 * @param handle  The sound_handle returned by play_sound().
 * @return B_OK always (unimplemented).
 */
status_t stop_sound(sound_handle handle)
{
	UNIMPLEMENTED();
	return B_OK;
}

/**
 * @brief Block until the specified sound has finished playing (not yet implemented).
 *
 * @param handle  The sound_handle returned by play_sound().
 * @return B_OK always (unimplemented).
 */
status_t wait_for_sound(sound_handle handle)
{
	UNIMPLEMENTED();
	return B_OK;
}
