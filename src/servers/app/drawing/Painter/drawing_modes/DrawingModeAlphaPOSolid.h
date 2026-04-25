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
 * MIT License. Copyright 2005, Stephan Aßmus.
 * DrawingMode implementing B_OP_ALPHA in "Pixel Overlay" mode on
 * B_RGBA32.
 */

/** @file DrawingModeAlphaPOSolid.h
    @brief Solid-pattern fast path for B_OP_ALPHA Pixel-Overlay. */

#ifndef DRAWING_MODE_ALPHA_PO_SOLID_H
#define DRAWING_MODE_ALPHA_PO_SOLID_H

#include "DrawingModeAlphaPO.h"

/** @brief Solid-pattern B_OP_ALPHA Pixel-Overlay blend for one pixel. */
void
blend_pixel_alpha_po_solid(int x, int y, const color_type& c, uint8 cover,
						   agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	uint16 alpha = c.a * cover;
	if (alpha == 255 * 255) {
		ASSIGN_ALPHA_PO(p, c.r, c.g, c.b);
	} else {
		BLEND_ALPHA_PO(p, c.r, c.g, c.b, alpha);
	}
}

/** @brief Solid-pattern B_OP_ALPHA Pixel-Overlay horizontal-line blend. */
void
blend_hline_alpha_po_solid(int x, int y, unsigned len,
						   const color_type& c, uint8 cover,
						   agg_buffer* buffer, const PatternHandler* pattern)
{
	uint16 alpha = c.a * cover;
	if (alpha == 255 * 255) {
		// cache the color as 32bit values
		uint32 v;
		uint8* p8 = (uint8*)&v;
		p8[0] = c.b;
		p8[1] = c.g;
		p8[2] = c.r;
		p8[3] = 255;
		// row offset as 32bit pointer
		uint32* p32 = (uint32*)(buffer->row_ptr(y)) + x;
		do {
			*p32 = v;
			p32++;
			x++;
		} while(--len);
	} else {
		uint8* p = buffer->row_ptr(y) + (x << 2);
		if (len < 4) {
			do {
				BLEND_ALPHA_CO(p, c.r, c.g, c.b, alpha);
				x++;
				p += 4;
			} while(--len);
		} else {
			alpha = alpha >> 8;
			blend_line32(p, len, c.r, c.g, c.b, alpha);
		}
	}
}

/** @brief Solid-pattern anti-aliased horizontal solid span for B_OP_ALPHA
           Pixel-Overlay. */
void
blend_solid_hspan_alpha_po_solid(int x, int y, unsigned len,
								 const color_type& c, const uint8* covers,
						 		 agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		uint16 alpha = c.a * *covers;
		if (alpha) {
			if(alpha == 255 * 255) {
				ASSIGN_ALPHA_PO(p, c.r, c.g, c.b);
			} else {
				BLEND_ALPHA_PO(p, c.r, c.g, c.b, alpha);
			}
		}
		covers++;
		p += 4;
		x++;
	} while(--len);
}



/** @brief Solid-pattern anti-aliased vertical solid span for B_OP_ALPHA
           Pixel-Overlay. */
void
blend_solid_vspan_alpha_po_solid(int x, int y, unsigned len,
								 const color_type& c, const uint8* covers,
								 agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		uint16 alpha = c.a * *covers;
		if (alpha) {
			if (alpha == 255 * 255) {
				ASSIGN_ALPHA_PO(p, c.r, c.g, c.b);
			} else {
				BLEND_ALPHA_PO(p, c.r, c.g, c.b, alpha);
			}
		}
		covers++;
		p += buffer->stride();
		y++;
	} while(--len);
}

#endif // DRAWING_MODE_ALPHA_PO_SOLID_H

