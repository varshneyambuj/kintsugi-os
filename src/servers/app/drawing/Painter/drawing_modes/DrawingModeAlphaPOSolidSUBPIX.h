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
 * DrawingMode implementing B_OP_ALPHA in "Pixel Overlay" mode on
 * B_RGBA32.
 */

/** @file DrawingModeAlphaPOSolidSUBPIX.h
    @brief Solid-pattern subpixel horizontal solid span for B_OP_ALPHA
           Pixel-Overlay. */

#ifndef DRAWING_MODE_ALPHA_PO_SOLID_SUBPIX_H
#define DRAWING_MODE_ALPHA_PO_SOLID_SUBPIX_H

#include "DrawingModeAlphaPOSUBPIX.h"
#include "GlobalSubpixelSettings.h"

/** @brief Solid-pattern subpixel horizontal solid span for B_OP_ALPHA
           Pixel-Overlay. */
void
blend_solid_hspan_alpha_po_solid_subpix(int x, int y, unsigned len,
	const color_type& c, const uint8* covers, agg_buffer* buffer,
	const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	uint16 alphaRed;
	uint16 alphaGreen;
	uint16 alphaBlue;
	const int subpixelL = gSubpixelOrderingRGB ? 2 : 0;
	const int subpixelM = 1;
	const int subpixelR = gSubpixelOrderingRGB ? 0 : 2;
	do {
		alphaRed = c.a * covers[subpixelL];
		alphaGreen = c.a * covers[subpixelM];
		alphaBlue = c.a * covers[subpixelR];
		BLEND_ALPHA_PO_SUBPIX(p, c.r, c.g, c.b,
			alphaBlue, alphaGreen, alphaRed);
		covers += 3;
		p += 4;
		x++;
		len -= 3;
	} while (len);
}

#endif // DRAWING_MODE_ALPHA_PO_SOLID_SUBPIX_H

