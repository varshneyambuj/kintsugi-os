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
 *   Copyright 2004-2005 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file AppDefs.cpp
 *  @brief Application definition constants including built-in cursor bitmaps.
 *
 *  Defines the default cursor image data used by the application kit,
 *  including the hand cursor and I-beam (text selection) cursor. Each cursor
 *  is stored as a 16x16 monochrome bitmap with accompanying transparency mask.
 */

#include <AppDefs.h>
#include <SupportDefs.h>


/** @brief Hand cursor bitmap data.
 *
 *  A 16x16 monochrome cursor image representing a pointing hand,
 *  typically used for indicating clickable elements. The data includes
 *  the cursor dimensions, color depth, hot-spot coordinates, image mask,
 *  and transparency mask.
 */
const uint8 B_HAND_CURSOR[] = {
	16,		// size (width/height)
	1,		// depth
	2, 2,	// hot-spot coordinates

	// image mask
	0x0, 0x0, 0x0, 0x0, 0x38, 0x0, 0x24, 0x0, 0x24, 0x0, 0x13, 0xe0, 0x12, 0x5c, 0x9, 0x2a, 
	0x8, 0x1, 0x3c, 0x1, 0x4c, 0x1, 0x42, 0x1, 0x30, 0x1, 0xc, 0x1, 0x2, 0x0, 0x1, 0x0, 

	// transparency mask
	0x0, 0x0, 0x0, 0x0, 0x38, 0x0, 0x3c, 0x0, 0x3c, 0x0, 0x1f, 0xe0, 0x1f, 0xfc, 0xf, 0xfe, 
	0xf, 0xff, 0x3f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x3f, 0xff, 0xf, 0xff, 0x3, 0xfe, 0x1, 0xf8, 
};

/** @brief I-beam cursor bitmap data.
 *
 *  A 16x16 monochrome cursor image representing an I-beam (text cursor),
 *  typically used for indicating text selection areas. The data includes
 *  the cursor dimensions, color depth, hot-spot coordinates, image mask,
 *  and transparency mask.
 */
const uint8 B_I_BEAM_CURSOR[] = {
	16,		// size (width/height)
	1,		// depth
	5, 8,	// hot-spot coordinates

	// image mask
	0x6, 0xc0, 0x3, 0x80, 0x1, 0x0, 0x1, 0x0, 0x1, 0x0, 0x1, 0x0, 0x1, 0x0, 0x1, 0x0, 
	0x1, 0x0, 0x1, 0x0, 0x1, 0x0, 0x1, 0x0, 0x1, 0x0, 0x1, 0x0, 0x3, 0x80, 0x6, 0xc0, 

	// transparency mask
	0xf, 0xc0, 0x7, 0x80, 0x3, 0x0, 0x3, 0x0, 0x3, 0x0, 0x3, 0x0, 0x3, 0x0, 0x3, 0x0, 
	0x3, 0x0, 0x3, 0x0, 0x3, 0x0, 0x3, 0x0, 0x3, 0x0, 0x3, 0x0, 0x7, 0x80, 0xf, 0xc0, 
};
