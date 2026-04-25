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

/** @file DrawingModeAlphaPO.h
    @brief Blend functions for B_OP_ALPHA + B_PIXEL_ALPHA +
           B_ALPHA_OVERLAY: per-pixel-alpha source-over compositing onto an
           opaque destination. */

#ifndef DRAWING_MODE_ALPHA_PO_H
#define DRAWING_MODE_ALPHA_PO_H

#include "DrawingMode.h"

// BLEND_ALPHA_PO
#define BLEND_ALPHA_PO(d, r, g, b, a) \
{ \
	BLEND16(d, r, g, b, a); \
}

// ASSIGN_ALPHA_PO
#define ASSIGN_ALPHA_PO(d, r, g, b) \
{ \
	d[0] = (b); \
	d[1] = (g); \
	d[2] = (r); \
	d[3] = 255; \
}

/** @brief Blends one B_OP_ALPHA / Pixel-Overlay pixel using per-pixel
           source alpha onto an opaque destination. */
void
blend_pixel_alpha_po(int x, int y, const color_type& c, uint8 cover,
					 agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	rgb_color color = pattern->ColorAt(x, y);
	uint16 alpha = color.alpha * cover;
	if (alpha == 255 * 255) {
		ASSIGN_ALPHA_PO(p, color.red, color.green, color.blue);
	} else {
		BLEND_ALPHA_PO(p, color.red, color.green, color.blue, alpha);
	}
}

/** @brief Horizontal-line B_OP_ALPHA / Pixel-Overlay blend. */
void
blend_hline_alpha_po(int x, int y, unsigned len,
					 const color_type& c, uint8 cover,
					 agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		rgb_color color = pattern->ColorAt(x, y);
		uint16 alpha = color.alpha * cover;
		if (alpha) {
			if (alpha == 255) {
				ASSIGN_ALPHA_PO(p, color.red, color.green, color.blue);
			} else {
				BLEND_ALPHA_PO(p, color.red, color.green, color.blue, alpha);
			}
		}
		x++;
		p += 4;
	} while(--len);
}

/** @brief Anti-aliased horizontal solid span for B_OP_ALPHA
           Pixel-Overlay. */
void
blend_solid_hspan_alpha_po(int x, int y, unsigned len,
						   const color_type& c, const uint8* covers,
						   agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		rgb_color color = pattern->ColorAt(x, y);
		uint16 alpha = color.alpha * *covers;
		if (alpha) {
			if(alpha == 255 * 255) {
				ASSIGN_ALPHA_PO(p, color.red, color.green, color.blue);
			} else {
				BLEND_ALPHA_PO(p, color.red, color.green, color.blue, alpha);
			}
		}
		covers++;
		p += 4;
		x++;
	} while(--len);
}



/** @brief Anti-aliased vertical solid span for B_OP_ALPHA Pixel-Overlay. */
void
blend_solid_vspan_alpha_po(int x, int y, unsigned len,
						   const color_type& c, const uint8* covers,
						   agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		rgb_color color = pattern->ColorAt(x, y);
		uint16 alpha = color.alpha * *covers;
		if (alpha) {
			if (alpha == 255 * 255) {
				ASSIGN_ALPHA_PO(p, color.red, color.green, color.blue);
			} else {
				BLEND_ALPHA_PO(p, color.red, color.green, color.blue, alpha);
			}
		}
		covers++;
		p += buffer->stride();
		y++;
	} while(--len);
}


/** @brief Per-pixel-color horizontal span for B_OP_ALPHA Pixel-Overlay. */
void
blend_color_hspan_alpha_po(int x, int y, unsigned len,
						   const color_type* colors,
						   const uint8* covers, uint8 cover,
						   agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	if (covers) {
		// non-solid opacity
		do {
			uint16 alpha = colors->a * *covers;
			if (alpha) {
				if (alpha == 255 * 255) {
					ASSIGN_ALPHA_PO(p, colors->r, colors->g, colors->b);
				} else {
					BLEND_ALPHA_PO(p, colors->r, colors->g, colors->b, alpha);
				}
			}
			covers++;
			p += 4;
			++colors;
		} while(--len);
	} else {
		// solid full opcacity
		uint16 alpha = colors->a * cover;
		if (alpha == 255 * 255) {
			do {
				ASSIGN_ALPHA_PO(p, colors->r, colors->g, colors->b);
				p += 4;
				++colors;
			} while(--len);
		// solid partial opacity
		} else if (alpha) {
			do {
				BLEND_ALPHA_PO(p, colors->r, colors->g, colors->b, alpha);
				p += 4;
				++colors;
			} while(--len);
		}
	}
}

#endif // DRAWING_MODE_ALPHA_PO_H

