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
 * MIT License. Copyright 2005, Stephan Aßmus and 2008, Andrej Spielmann.
 * DrawingMode implementing B_OP_OVER on B_RGBA32.
 */

/** @file DrawingModeOverSUBPIX.h
    @brief Subpixel (LCD-aware) horizontal solid-span variant of B_OP_OVER. */

#ifndef DRAWING_MODE_OVER_SUBPIX_H
#define DRAWING_MODE_OVER_SUBPIX_H

#include "DrawingMode.h"
#include "GlobalSubpixelSettings.h"

// BLEND_OVER_SUBPIX
#define BLEND_OVER_SUBPIX(d, r, g, b, a1, a2, a3) \
{ \
	BLEND_SUBPIX(d, r, g, b, a1, a2, a3) \
}


/** @brief Subpixel B_OP_OVER horizontal solid span; consumes three
           coverage bytes per output pixel (R, G, B subpixel weights),
           swapping order based on @c gSubpixelOrderingRGB. */
void
blend_solid_hspan_over_subpix(int x, int y, unsigned len, const color_type& c,
	const uint8* covers, agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	rgb_color color = pattern->HighColor();
	const int subpixelL = gSubpixelOrderingRGB ? 2 : 0;
	const int subpixelM = 1;
	const int subpixelR = gSubpixelOrderingRGB ? 0 : 2;
	do {
		if (pattern->IsHighColor(x, y)) {
			BLEND_OVER_SUBPIX(p, color.red, color.green, color.blue,
				covers[subpixelL], covers[subpixelM], covers[subpixelR]);
		}
		covers += 3;
		p += 4;
		x++;
		len -= 3;
	} while (len);
}

#endif // DRAWING_MODE_OVER_SUBPIX_H

