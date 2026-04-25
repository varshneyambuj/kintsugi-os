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
 * DrawingMode implementing B_OP_SELECT on B_RGBA32.
 */

/** @file DrawingModeSelect.h
    @brief Blend functions for B_OP_SELECT: swaps destination pixels that
           match the high color with the low color and vice versa. */

#ifndef DRAWING_MODE_SELECT_H
#define DRAWING_MODE_SELECT_H

#include "DrawingMode.h"

// BLEND_SELECT
#define BLEND_SELECT(d, r, g, b, a) \
{ \
	BLEND(d, r, g, b, a); \
}

// ASSIGN_SELECT
#define ASSIGN_SELECT(d, r, g, b) \
{ \
	d[0] = (b); \
	d[1] = (g); \
	d[2] = (r); \
	d[3] = 255; \
}

/** @brief Helper for B_OP_SELECT: compares destination @a p with the
           pattern's high and low colors and writes the swap target into
           @a result, leaving it untouched when neither matches. */
inline bool
compare(uint8* p, const rgb_color& high, const rgb_color& low, rgb_color* result)
{
	pixel32 _p;
	_p.data32 = *(uint32*)p;
	if (_p.data8[2] == high.red &&
		_p.data8[1] == high.green &&
		_p.data8[0] == high.blue) {
		result->red = low.red;
		result->green = low.green;
		result->blue = low.blue;
		return true;
	} else if (_p.data8[2] == low.red &&
			   _p.data8[1] == low.green &&
			   _p.data8[0] == low.blue) {
		result->red = high.red;
		result->green = high.green;
		result->blue = high.blue;
		return true;
	}
	return false;
}

/** @brief Blends one B_OP_SELECT pixel: swaps the destination between
           the pattern's high and low colors when it matches one. */
void
blend_pixel_select(int x, int y, const color_type& c, uint8 cover,
				   agg_buffer* buffer, const PatternHandler* pattern)
{
	if (pattern->IsHighColor(x, y)) {
		uint8* p = buffer->row_ptr(y) + (x << 2);
		rgb_color high = pattern->HighColor();
		rgb_color low = pattern->LowColor();
		rgb_color color; 
		if (compare(p, high, low, &color)) {
			if (cover == 255) {
				ASSIGN_SELECT(p, color.red, color.green, color.blue);
			} else {
				BLEND_SELECT(p, color.red, color.green, color.blue, cover);
			}
		}
	}
}

/** @brief Horizontal-line B_OP_SELECT blend with a single AA cover. */
void
blend_hline_select(int x, int y, unsigned len, 
				   const color_type& c, uint8 cover,
				   agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	rgb_color high = pattern->HighColor();
	rgb_color low = pattern->LowColor();
	rgb_color color; 
	if (cover == 255) {
		do {
			if (pattern->IsHighColor(x, y)
				&& compare(p, high, low, &color))
				ASSIGN_SELECT(p, color.red, color.green, color.blue);
			x++;
			p += 4;
		} while(--len);
	} else {
		do {
			if (pattern->IsHighColor(x, y)
				&& compare(p, high, low, &color)) {
				BLEND_SELECT(p, color.red, color.green, color.blue, cover);
			}
			x++;
			p += 4;
		} while(--len);
	}
}

/** @brief Anti-aliased horizontal solid span using B_OP_SELECT. */
void
blend_solid_hspan_select(int x, int y, unsigned len, 
						 const color_type& c, const uint8* covers,
						 agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	rgb_color high = pattern->HighColor();
	rgb_color low = pattern->LowColor();
	rgb_color color; 
	do {
		if (pattern->IsHighColor(x, y)) {
			if (*covers && compare(p, high, low, &color)) {
				if (*covers == 255) {
					ASSIGN_SELECT(p, color.red, color.green, color.blue);
				} else {
					BLEND_SELECT(p, color.red, color.green, color.blue, *covers);
				}
			}
		}
		covers++;
		p += 4;
		x++;
	} while(--len);
}



/** @brief Anti-aliased vertical solid span using B_OP_SELECT. */
void
blend_solid_vspan_select(int x, int y, unsigned len, 
						 const color_type& c, const uint8* covers,
						 agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	rgb_color high = pattern->HighColor();
	rgb_color low = pattern->LowColor();
	rgb_color color; 
	do {
		if (pattern->IsHighColor(x, y)) {
			if (*covers && compare(p, high, low, &color)) {
				if (*covers == 255) {
					ASSIGN_SELECT(p, color.red, color.green, color.blue);
				} else {
					BLEND_SELECT(p, color.red, color.green, color.blue, *covers);
				}
			}
		}
		covers++;
		p += buffer->stride();
		y++;
	} while(--len);
}


/** @brief Per-pixel-color horizontal span using B_OP_SELECT. */
void
blend_color_hspan_select(int x, int y, unsigned len, 
						 const color_type* colors, 
						 const uint8* covers, uint8 cover,
						 agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	rgb_color high = pattern->HighColor();
	rgb_color low = pattern->LowColor();
	rgb_color color; 
	if (covers) {
		// non-solid opacity
		do {
			if (*covers && colors->a > 0 && compare(p, high, low, &color)) {
				if (*covers == 255) {
					ASSIGN_SELECT(p, color.red, color.green, color.blue);
				} else {
					BLEND_SELECT(p, color.red, color.green, color.blue, *covers);
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
				if (colors->a > 0 && compare(p, high, low, &color)) {
					ASSIGN_SELECT(p, color.red, color.green, color.blue);
				}
				p += 4;
				++colors;
			} while(--len);
		// solid partial opacity
		} else if (cover) {
			do {
				if (colors->a > 0 && compare(p, high, low, &color)) {
					BLEND_SELECT(p, color.red, color.green, color.blue, cover);
				}
				p += 4;
				++colors;
			} while(--len);
		}
	}
}

#endif // DRAWING_MODE_SELECT_H

