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
 * MIT License. Copyright 2008, Andrej Spielmann.
 */

/** @file GlobalSubpixelSettings.h
    @brief Process-global flags steering LCD subpixel anti-aliasing and font
           hinting in the Painter subsystem. */

#ifndef GLOBAL_SUBPIXEL_SETTINGS_H
#define GLOBAL_SUBPIXEL_SETTINGS_H

#include <SupportDefs.h>

// TODO: these global settings need to be removed - once we have more than one
//	user, we also must support more than one setting. That's why there is a
//	DesktopSettings class in the first place...

/** @brief Hinting strategies advertised to the font subsystem. */
enum {
	HINTING_MODE_OFF = 0,				/**< Hinting disabled for all faces. */
	HINTING_MODE_ON,					/**< Hinting enabled for all faces. */
	HINTING_MODE_MONOSPACED_ONLY		/**< Hint only monospaced faces. */
};

//#define AVERAGE_BASED_SUBPIXEL_FILTERING

/** @brief Master switch for LCD subpixel anti-aliasing. */
extern bool gSubpixelAntialiasing;

/** @brief Default font-hinting mode (one of the HINTING_MODE_* enum values). */
extern uint8 gDefaultHintingMode;

// The weight with which the average of the subpixels is applied to counter
// color fringes (0 = full sharpness ... 255 = grayscale anti-aliasing)
/** @brief Mix between full subpixel sharpness (0) and grayscale AA (255).
 *         Higher values reduce color fringes at the cost of sharpness. */
extern uint8 gSubpixelAverageWeight;

// There are two types of LCD displays in general - the more common have
// sub - pixels physically ordered as RGB within a pixel, but some are BGR.
// Sub - pixel antialiasing optimised for one ordering obviously doesn't work
// on the other.
/** @brief Physical sub-pixel ordering: true for RGB, false for BGR. */
extern bool gSubpixelOrderingRGB;

#endif // GLOBAL_SUBPIXEL_SETTINGS_H
