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
 * DrawingMode implementing B_OP_INVERT on B_RGBA32.
 */

/** @file DrawingModeInvert.h
    @brief Blend functions for B_OP_INVERT: inverts destination colors
           where the pattern is high. */

#ifndef DRAWING_MODE_INVERT_H
#define DRAWING_MODE_INVERT_H

#include "DrawingMode.h"

// BLEND_INVERT
#define BLEND_INVERT(d, a) \
{ \
	pixel32 _p; \
	_p.data32 = *(uint32*)d; \
	BLEND(d, 255 - _p.data8[2], 255 - _p.data8[1], 255 - _p.data8[0], a); \
	(void)_p; \
}

// ASSIGN_INVERT
#define ASSIGN_INVERT(d) \
{ \
	pixel32 _p; \
	_p.data32 = *(uint32*)d; \
	d[0] = 255 - _p.data8[0]; \
	d[1] = 255 - _p.data8[1]; \
	d[2] = 255 - _p.data8[2]; \
	d[3] = 255; \
}

/** @brief Blends one B_OP_INVERT pixel: inverts the destination where
           the pattern is high. */
void
blend_pixel_invert(int x, int y, const color_type& c, uint8 cover,
				   agg_buffer* buffer, const PatternHandler* pattern)
{
	if (pattern->IsHighColor(x, y)) {
		uint8* p = buffer->row_ptr(y) + (x << 2);
		if (cover == 255) {
			ASSIGN_INVERT(p);
		} else {
			BLEND_INVERT(p, cover);
		}
	}
}

/** @brief Horizontal-line B_OP_INVERT blend with a single AA cover. */
void
blend_hline_invert(int x, int y, unsigned len, 
				   const color_type& c, uint8 cover,
				   agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	if(cover == 255) {
		do {
			if (pattern->IsHighColor(x, y)) {
				ASSIGN_INVERT(p);
			}
			x++;
			p += 4;
		} while(--len);
	} else {
		do {
			if (pattern->IsHighColor(x, y)) {
				BLEND_INVERT(p, cover);
			}
			x++;
			p += 4;
		} while(--len);
	}
}

/** @brief Anti-aliased horizontal solid span using B_OP_INVERT. */
void
blend_solid_hspan_invert(int x, int y, unsigned len, 
						 const color_type& c, const uint8* covers,
						 agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		if (pattern->IsHighColor(x, y)) {
			if (*covers) {
				if (*covers == 255) {
					ASSIGN_INVERT(p);
				} else {
					BLEND_INVERT(p, *covers);
				}
			}
		}
		covers++;
		p += 4;
		x++;
	} while(--len);
}



/** @brief Anti-aliased vertical solid span using B_OP_INVERT. */
void
blend_solid_vspan_invert(int x, int y, unsigned len, 
						 const color_type& c, const uint8* covers,
						 agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		if (pattern->IsHighColor(x, y)) {
			if (*covers) {
				if (*covers == 255) {
					ASSIGN_INVERT(p);
				} else {
					BLEND_INVERT(p, *covers);
				}
			}
		}
		covers++;
		p += buffer->stride();
		y++;
	} while(--len);
}


/** @brief Per-pixel-color horizontal span using B_OP_INVERT. */
void
blend_color_hspan_invert(int x, int y, unsigned len, 
						 const color_type* colors, 
						 const uint8* covers, uint8 cover,
						 agg_buffer* buffer, const PatternHandler* pattern)
{
	// TODO: does the R5 app_server check for the
	// appearance of the high color in the source bitmap?
	uint8* p = buffer->row_ptr(y) + (x << 2);
	if (covers) {
		// non-solid opacity
		do {
			if (*covers && colors->a > 0) {
				if (*covers == 255) {
					ASSIGN_INVERT(p);
				} else {
					BLEND_INVERT(p, *covers);
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
					ASSIGN_INVERT(p);
				}
				p += 4;
				++colors;
			} while(--len);
		// solid partial opacity
		} else if (cover) {
			do {
				if (colors->a > 0) {
					BLEND_INVERT(p, cover);
				}
				p += 4;
				++colors;
			} while(--len);
		}
	}
}

#endif // DRAWING_MODE_INVERT_H

