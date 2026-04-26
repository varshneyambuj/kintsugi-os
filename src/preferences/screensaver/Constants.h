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
 */

/** @file Constants.h
    @brief Shared rgb_color constants used by the ScreenSaver preflet drawing code. */

#ifndef CONSTANTS_H
#define CONSTANTS_H


#include <GraphicsDefs.h>


/** @brief Opaque black; used for outlines and stop-glyph fills. */
const rgb_color kBlack      = { 0, 0, 0, 0};
/** @brief Mid grey; used for inactive monitor framing. */
const rgb_color kDarkGrey   = { 150, 150, 150, 0};
/** @brief Light grey; used for monitor inner backgrounds. */
const rgb_color kGrey       = { 200, 200, 200, 0};
/** @brief Pale blue; used for highlighting in the preview. */
const rgb_color kLightBlue  = { 200, 200, 255, 0};
/** @brief Pale green (despite the name, this is a salmon shade). */
const rgb_color kLightGreen = { 255, 200, 200, 0};


#endif	// CONSTANTS_H
