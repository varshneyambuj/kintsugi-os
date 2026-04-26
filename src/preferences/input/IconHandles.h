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
 * MIT License. Copyright 2020, Haiku, Inc.
 */

/** @file IconHandles.h
    @brief Resource identifiers for the device-class icons used by the Input preflet. */


/**
 * @brief Resource ID enum for the device-class icons.
 *
 * Each enumerator names a B_VECTOR_ICON_TYPE resource embedded in the
 * Input preferences executable; InputIcons looks them up by name to
 * populate its BBitmap members.
 */
enum {
	mouse_icon = 1,
	touchpad_icon = 2,
	keyboard_icon = 3
};
