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

/** @file DrawingModeOver.h
    @brief Blend functions for B_OP_OVER: writes the high pattern color
           opaquely on cells the pattern marks high, leaves others
           untouched. */

#ifndef DRAWING_MODE_OVER_H
#define DRAWING_MODE_OVER_H

#include "DrawingMode.h"

// BLEND_OVER
#define BLEND_OVER(d, r, g, b, a) \
{ \
	BLEND(d, r, g, b, a) \
}

// ASSIGN_OVER
#define ASSIGN_OVER(d, r, g, b) \
{ \
	d[0] = (b); \
	d[1] = (g); \
	d[2] = (r); \
	d[3] = 255; \
}

/** @brief Blends one B_OP_OVER pixel: writes the high color when the
           pattern is high at (@a x, @a y), otherwise leaves the
           destination unchanged. */
void
blend_pixel_over(int x, int y, const color_type& c, uint8 cover,
				 agg_buffer* buffer, const PatternHandler* pattern)
{
	if (pattern->IsHighColor(x, y)) {
		uint8* p = buffer->row_ptr(y) + (x << 2);
		rgb_color color = pattern->HighColor();
		if (cover == 255) {
			ASSIGN_OVER(p, color.red, color.green, color.blue);
		} else {
			BLEND_OVER(p, color.red, color.green, color.blue, cover);
		}
	}
}

/** @brief Blends a horizontal run of @a len B_OP_OVER pixels with a
           single AA cover. */
void
blend_hline_over(int x, int y, unsigned len,
				 const color_type& c, uint8 cover,
				 agg_buffer* buffer, const PatternHandler* pattern)
{
	if (cover == 255) {
		rgb_color color = pattern->HighColor();
		uint32 v;
		uint8* p8 = (uint8*)&v;
		p8[0] = (uint8)color.blue;
		p8[1] = (uint8)color.green;
		p8[2] = (uint8)color.red;
		p8[3] = 255;
		uint32* p32 = (uint32*)(buffer->row_ptr(y)) + x;
		do {
			if (pattern->IsHighColor(x, y))
				*p32 = v;
			p32++;
			x++;
		} while(--len);
	} else {
		uint8* p = buffer->row_ptr(y) + (x << 2);
		rgb_color color = pattern->HighColor();
		do {
			if (pattern->IsHighColor(x, y)) {
				BLEND_OVER(p, color.red, color.green, color.blue, cover);
			}
			x++;
			p += 4;
		} while(--len);
	}
}

/** @brief Blends a horizontal anti-aliased solid span using B_OP_OVER
           with per-pixel coverage values from @a covers. */
void
blend_solid_hspan_over(int x, int y, unsigned len,
					   const color_type& c, const uint8* covers,
					   agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	rgb_color color = pattern->HighColor();
	do {
		if (pattern->IsHighColor(x, y)) {
			if (*covers) {
				if (*covers == 255) {
					ASSIGN_OVER(p, color.red, color.green, color.blue);
				} else {
					BLEND_OVER(p, color.red, color.green, color.blue, *covers);
				}
			}
		}
		covers++;
		p += 4;
		x++;
	} while(--len);
}



/** @brief Blends a vertical anti-aliased solid span using B_OP_OVER with
           per-pixel coverage values from @a covers. */
void
blend_solid_vspan_over(int x, int y, unsigned len,
					   const color_type& c, const uint8* covers,
					   agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	rgb_color color = pattern->HighColor();
	do {
		if (pattern->IsHighColor(x, y)) {
			if (*covers) {
				if (*covers == 255) {
					ASSIGN_OVER(p, color.red, color.green, color.blue);
				} else {
					BLEND_OVER(p, color.red, color.green, color.blue, *covers);
				}
			}
		}
		covers++;
		p += buffer->stride();
		y++;
	} while(--len);
}


/** @brief Blends a horizontal span of per-pixel colors using B_OP_OVER,
           honouring per-pixel @a covers when supplied or the constant
           @a cover otherwise. */
void
blend_color_hspan_over(int x, int y, unsigned len,
					   const color_type* colors, 
					   const uint8* covers, uint8 cover,
					   agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	if (covers) {
		// non-solid opacity
		do {
			if (*covers && colors->a > 0) {
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
				if (colors->a > 0) {
					ASSIGN_OVER(p, colors->r, colors->g, colors->b);
				}
				p += 4;
				++colors;
			} while(--len);
		// solid partial opacity
		} else if (cover) {
			do {
				if (colors->a > 0) {
					BLEND_OVER(p, colors->r, colors->g, colors->b, cover);
				}
				p += 4;
				++colors;
			} while(--len);
		}
	}
}

#endif // DRAWING_MODE_OVER_H

