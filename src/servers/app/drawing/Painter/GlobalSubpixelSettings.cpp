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
 *   Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file GlobalSubpixelSettings.cpp
 * @brief Storage for app_server-wide LCD subpixel anti-aliasing preferences.
 *
 * Defines the process-global flags used by the Painter subsystem to decide
 * whether subpixel (LCD/ClearType-style) anti-aliasing is active, the default
 * font hinting mode, the average-vs-sharpness mix used by subpixel filtering,
 * and the physical sub-pixel ordering of the display (RGB vs BGR). The values
 * themselves are populated by DesktopSettings during start-up.
 *
 * @see DesktopSettings, GlobalSubpixelSettings.h
 */


#include "GlobalSubpixelSettings.h"


// NOTE: all these are initialized in DesktopSettings.cpp

/** @brief Master switch for LCD subpixel anti-aliasing (true = enabled). */
bool gSubpixelAntialiasing;

/** @brief Default font-hinting mode; one of HINTING_MODE_OFF, HINTING_MODE_ON,
 *         or HINTING_MODE_MONOSPACED_ONLY. */
uint8 gDefaultHintingMode;

/** @brief Sharpness/average mix for subpixel filtering. 0 keeps full subpixel
 *         sharpness, 255 collapses to plain grayscale anti-aliasing. */
uint8 gSubpixelAverageWeight;

/** @brief Physical sub-pixel ordering of the display: true for RGB, false
 *         for BGR. Subpixel rendering must match the panel's actual layout. */
bool gSubpixelOrderingRGB;
