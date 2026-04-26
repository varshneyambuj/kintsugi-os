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
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2010, Haiku.
 */

/** @file IconHandles.h
    @brief Resource id handles used by MediaIcons to fetch bitmaps from the executable. */


/**
 * @brief Resource ids for the bitmaps embedded in the Media preflet.
 *
 * Each value matches an entry in the application resource file and is
 * resolved at runtime via BResources::LoadResource().
 */
enum {
	devices_icon = 1, /**< @brief Generic devices badge used by DeviceListItem. */
	mixer_icon = 2,   /**< @brief Audio mixer badge used by AudioMixerListItem. */
	tv_icon = 3,      /**< @brief Video output (TV) badge. */
	cam_icon = 4,     /**< @brief Video input (camera) badge. */
	mic_icon = 5,     /**< @brief Audio input (microphone) badge. */
	speaker_icon = 6  /**< @brief Audio output (speaker) badge. */
};

