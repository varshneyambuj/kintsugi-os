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
 * DrawingMode implementing B_OP_ADD on B_RGBA32.
 */

/** @file DrawingModeAdd.h
    @brief Blend functions for B_OP_ADD: saturating channel-wise addition of
           the pattern source onto the destination. */

#ifndef DRAWING_MODE_ADD_H
#define DRAWING_MODE_ADD_H

#include "DrawingMode.h"


// BLEND_ADD
#define BLEND_ADD(d, r, g, b, a) \
{ \
	pixel32 _p; \
	_p.data32 = *(uint32*)d; \
	uint8 rt = min_c(255, _p.data8[2] + (r)); \
	uint8 gt = min_c(255, _p.data8[1] + (g)); \
	uint8 bt = min_c(255, _p.data8[0] + (b)); \
	BLEND(d, rt, gt, bt, a); \
}

//ASSIGN_ADD
#define ASSIGN_ADD(d, r, g, b) \
{ \
	pixel32 _p; \
	_p.data32 = *(uint32*)d; \
	d[0] = min_c(255, _p.data8[0] + (b)); \
	d[1] = min_c(255, _p.data8[1] + (g)); \
	d[2] = min_c(255, _p.data8[2] + (r)); \
	d[3] = 255; \
}


/** @brief Blends one B_OP_ADD pixel at (@a x, @a y) using saturating
           per-channel addition of the pattern color. */
void
blend_pixel_add(int x, int y, const color_type& c, uint8 cover,
				agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	rgb_color color = pattern->ColorAt(x, y);
	if (cover == 255) {
		ASSIGN_ADD(p, color.red, color.green, color.blue);
	} else {
		BLEND_ADD(p, color.red, color.green, color.blue, cover);
	}
}

/** @brief Blends a horizontal run of @a len B_OP_ADD pixels with a single
           AA cover. */
void
blend_hline_add(int x, int y, unsigned len,
				const color_type& c, uint8 cover,
				agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	if (cover == 255) {
		do {
			rgb_color color = pattern->ColorAt(x, y);

			ASSIGN_ADD(p, color.red, color.green, color.blue);

			p += 4;
			x++;
		} while(--len);
	} else {
		do {
			rgb_color color = pattern->ColorAt(x, y);

			BLEND_ADD(p, color.red, color.green, color.blue, cover);

			x++;
			p += 4;
		} while(--len);
	}
}

/** @brief Blends a horizontal anti-aliased solid span using B_OP_ADD with
           per-pixel coverage values from @a covers. */
void
blend_solid_hspan_add(int x, int y, unsigned len,
					  const color_type& c, const uint8* covers,
					  agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		rgb_color color = pattern->ColorAt(x, y);
		if (*covers) {
			if (*covers == 255) {
				ASSIGN_ADD(p, color.red, color.green, color.blue);
			} else {
				BLEND_ADD(p, color.red, color.green, color.blue, *covers);
			}
		}
		covers++;
		p += 4;
		x++;
	} while(--len);
}



/** @brief Blends a vertical anti-aliased solid span using B_OP_ADD with
           per-pixel coverage values from @a covers. */
void
blend_solid_vspan_add(int x, int y, unsigned len,
					  const color_type& c, const uint8* covers,
					  agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		rgb_color color = pattern->ColorAt(x, y);
		if (*covers) {
			if (*covers == 255) {
				ASSIGN_ADD(p, color.red, color.green, color.blue);
			} else {
				BLEND_ADD(p, color.red, color.green, color.blue, *covers);
			}
		}
		covers++;
		p += buffer->stride();
		y++;
	} while(--len);
}


/** @brief Blends a horizontal span of per-pixel colors using B_OP_ADD,
           honouring per-pixel @a covers when supplied or the constant
           @a cover otherwise. */
void
blend_color_hspan_add(int x, int y, unsigned len,
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
					ASSIGN_ADD(p, colors->r, colors->g, colors->b);
				} else {
					BLEND_ADD(p, colors->r, colors->g, colors->b, *covers);
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
					ASSIGN_ADD(p, colors->r, colors->g, colors->b);
				}
				p += 4;
				++colors;
			} while(--len);
		// solid partial opacity
		} else if (cover) {
			do {
				if (colors->a > 0) {
					BLEND_ADD(p, colors->r, colors->g, colors->b, cover);
				}
				p += 4;
				++colors;
			} while(--len);
		}
	}
}

#endif // DRAWING_MODE_ADD_H

