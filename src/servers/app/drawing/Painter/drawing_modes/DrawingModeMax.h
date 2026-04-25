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
 * DrawingMode implementing B_OP_MAX on B_RGBA32.
 */

/** @file DrawingModeMax.h
    @brief Blend functions for B_OP_MAX: writes the source color where it
           is brighter (higher brightness) than the destination. */

#ifndef DRAWING_MODE_MAX_H
#define DRAWING_MODE_MAX_H

#include "DrawingMode.h"


// BLEND_MAX
#define BLEND_MAX(d, r, g, b, a) \
{ \
	pixel32 _p; \
	_p.data32 = *(uint32*)d; \
	if (brightness_for((r), (g), (b)) \
		> brightness_for(_p.data8[2], _p.data8[1], _p.data8[0])) { \
		BLEND(d, (r), (g), (b), a); \
	} \
}

// ASSIGN_MAX
#define ASSIGN_MAX(d, r, g, b) \
{ \
	pixel32 _p; \
	_p.data32 = *(uint32*)d; \
	if (brightness_for((r), (g), (b)) \
		> brightness_for(_p.data8[2], _p.data8[1], _p.data8[0])) { \
		d[0] = (b); \
		d[1] = (g); \
		d[2] = (r); \
		d[3] = 255; \
	} \
}


/** @brief Blends one B_OP_MAX pixel: keeps source iff its brightness is
           higher than the destination. */
void
blend_pixel_max(int x, int y, const color_type& c, uint8 cover,
				agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	rgb_color color = pattern->ColorAt(x, y);
	if (cover == 255) {
		ASSIGN_MAX(p, color.red, color.green, color.blue);
	} else {
		BLEND_MAX(p, color.red, color.green, color.blue, cover);
	}
}

/** @brief Horizontal-line B_OP_MAX blend with a single AA cover. */
void
blend_hline_max(int x, int y, unsigned len, 
				const color_type& c, uint8 cover,
				agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	if (cover == 255) {
		do {
			rgb_color color = pattern->ColorAt(x, y);

			ASSIGN_MAX(p, color.red, color.green, color.blue);

			p += 4;
			x++;
		} while(--len);
	} else {
		do {
			rgb_color color = pattern->ColorAt(x, y);

			BLEND_MAX(p, color.red, color.green, color.blue, cover);

			x++;
			p += 4;
		} while(--len);
	}
}

/** @brief Anti-aliased horizontal solid span using B_OP_MAX. */
void
blend_solid_hspan_max(int x, int y, unsigned len, 
					  const color_type& c, const uint8* covers,
					  agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		rgb_color color = pattern->ColorAt(x, y);
		if (*covers) {
			if (*covers == 255) {
				ASSIGN_MAX(p, color.red, color.green, color.blue);
			} else {
				BLEND_MAX(p, color.red, color.green, color.blue, *covers);
			}
		}
		covers++;
		p += 4;
		x++;
	} while(--len);
}



/** @brief Anti-aliased vertical solid span using B_OP_MAX. */
void
blend_solid_vspan_max(int x, int y, unsigned len, 
					  const color_type& c, const uint8* covers,
					  agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		rgb_color color = pattern->ColorAt(x, y);
		if (*covers) {
			if (*covers == 255) {
				ASSIGN_MAX(p, color.red, color.green, color.blue);
			} else {
				BLEND_MAX(p, color.red, color.green, color.blue, *covers);
			}
		}
		covers++;
		p += buffer->stride();
		y++;
	} while(--len);
}


/** @brief Per-pixel-color horizontal span using B_OP_MAX. */
void
blend_color_hspan_max(int x, int y, unsigned len, 
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
					ASSIGN_MAX(p, colors->r, colors->g, colors->b);
				} else {
					BLEND_MAX(p, colors->r, colors->g, colors->b, *covers);
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
					ASSIGN_MAX(p, colors->r, colors->g, colors->b);
				}
				p += 4;
				++colors;
			} while(--len);
		// solid partial opacity
		} else if (cover) {
			do {
				if (colors->a > 0) {
					BLEND_MAX(p, colors->r, colors->g, colors->b, cover);
				}
				p += 4;
				++colors;
			} while(--len);
		}
	}
}

#endif // DRAWING_MODE_MAX_H

