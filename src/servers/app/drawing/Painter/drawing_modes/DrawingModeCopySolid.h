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
 * DrawingMode implementing B_OP_COPY ignoring the pattern (solid) on
 * B_RGBA32.
 */

/** @file DrawingModeCopySolid.h
    @brief Solid-pattern fast path for B_OP_COPY: omits the per-pixel
           pattern lookup. */

#ifndef DRAWING_MODE_COPY_SOLID_H
#define DRAWING_MODE_COPY_SOLID_H

#include "DrawingModeOver.h"

/** @brief Solid-pattern B_OP_COPY blend for one pixel. */
void
blend_pixel_copy_solid(int x, int y, const color_type& c, uint8 cover,
					   agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	if (cover == 255) {
		ASSIGN_OVER(p, c.r, c.g, c.b);
	} else {
		BLEND_OVER(p, c.r, c.g, c.b, cover);
	}
}

/** @brief Solid-pattern B_OP_COPY horizontal-line blend. */
void
blend_hline_copy_solid(int x, int y, unsigned len, 
					   const color_type& c, uint8 cover,
					   agg_buffer* buffer, const PatternHandler* pattern)
{
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

/** @brief Solid-pattern anti-aliased horizontal solid span using
           B_OP_COPY. */
void
blend_solid_hspan_copy_solid(int x, int y, unsigned len, 
							 const color_type& c, const uint8* covers,
							 agg_buffer* buffer,
							 const PatternHandler* pattern)
{
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



/** @brief Solid-pattern anti-aliased vertical solid span using
           B_OP_COPY. */
void
blend_solid_vspan_copy_solid(int x, int y, unsigned len, 
							 const color_type& c, const uint8* covers,
							 agg_buffer* buffer,
							 const PatternHandler* pattern)
{
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


/** @brief Solid-pattern per-pixel-color horizontal span using
           B_OP_COPY. */
void
blend_color_hspan_copy_solid(int x, int y, unsigned len, 
							 const color_type* colors, const uint8* covers,
							 uint8 cover,
							 agg_buffer* buffer,
							 const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	if (covers) {
		// non-solid opacity
		do {
				if (*covers) {
				if (*covers == 255) {
					ASSIGN_OVER(p, colors->r, colors->g, colors->b);
				} else {
					BLEND_OVER(p, colors->r, colors->g, colors->b, *covers);
				}
			}
			covers++;
			p += 4;
			++colors;
		} while(--len);
	} else {
		// solid full opcacity
		if (cover == 255) {
			do {
				ASSIGN_OVER(p, colors->r, colors->g, colors->b);
				p += 4;
				++colors;
			} while(--len);
		// solid partial opacity
		} else if (cover) {
			do {
				BLEND_OVER(p, colors->r, colors->g, colors->b, cover);
				p += 4;
				++colors;
			} while(--len);
		}
	}
}

#endif // DRAWING_MODE_COPY_SOLID_H

