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
 * DrawingMode implementing B_OP_OVER on B_RGBA32.
 */

/** @file DrawingModeOverSolid.h
    @brief Solid-pattern fast path for B_OP_OVER: skips the per-pixel
           IsHighColor() test when the bound pattern is solid. */

#ifndef DRAWING_MODE_OVER_SOLID_H
#define DRAWING_MODE_OVER_SOLID_H

#include "DrawingModeOver.h"


/** @brief Solid-pattern B_OP_OVER blend for a single pixel; bails out
           immediately when the pattern is solid-low. */
void
blend_pixel_over_solid(int x, int y, const color_type& c, uint8 cover,
					   agg_buffer* buffer, const PatternHandler* pattern)
{
	if (pattern->IsSolidLow())
		return;

	uint8* p = buffer->row_ptr(y) + (x << 2);
	if (cover == 255) {
		ASSIGN_OVER(p, c.r, c.g, c.b);
	} else {
		BLEND_OVER(p, c.r, c.g, c.b, cover);
	}
}

/** @brief Solid-pattern B_OP_OVER horizontal-line blend; uses a single
           memset-like inner loop when @a cover is fully opaque. */
void
blend_hline_over_solid(int x, int y, unsigned len,
					   const color_type& c, uint8 cover,
					   agg_buffer* buffer, const PatternHandler* pattern)
{
	if (pattern->IsSolidLow())
		return;

	if (cover == 255) {
		uint32 v;
		uint8* p8 = (uint8*)&v;
		p8[0] = (uint8)c.b;
		p8[1] = (uint8)c.g;
		p8[2] = (uint8)c.r;
		p8[3] = 255;
		uint32* p32 = (uint32*)(buffer->row_ptr(y)) + x;
		do {
			*p32 = v;
			p32++;
			x++;
		} while(--len);
	} else {
		uint8* p = buffer->row_ptr(y) + (x << 2);
		do {
			BLEND_OVER(p, c.r, c.g, c.b, cover);
			x++;
			p += 4;
		} while(--len);
	}
}

/** @brief Solid-pattern B_OP_OVER anti-aliased horizontal solid span. */
void
blend_solid_hspan_over_solid(int x, int y, unsigned len,
							 const color_type& c, const uint8* covers,
							 agg_buffer* buffer, const PatternHandler* pattern)
{
	if (pattern->IsSolidLow())
		return;

	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		if (*covers) {
			if (*covers == 255) {
				ASSIGN_OVER(p, c.r, c.g, c.b);
			} else {
				BLEND_OVER(p, c.r, c.g, c.b, *covers);
			}
		}
		covers++;
		p += 4;
		x++;
	} while(--len);
}

/** @brief Solid-pattern B_OP_OVER anti-aliased vertical solid span. */
void
blend_solid_vspan_over_solid(int x, int y, unsigned len,
							 const color_type& c, const uint8* covers,
							 agg_buffer* buffer, const PatternHandler* pattern)
{
	if (pattern->IsSolidLow())
		return;

	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		if (*covers) {
			if (*covers == 255) {
				ASSIGN_OVER(p, c.r, c.g, c.b);
			} else {
				BLEND_OVER(p, c.r, c.g, c.b, *covers);
			}
		}
		covers++;
		p += buffer->stride();
		y++;
	} while(--len);
}

#endif // DRAWING_MODE_OVER_H

