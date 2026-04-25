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
 * MIT License. Copyright 2005, Stephan Aßmus and 2015, Julian Harnath.
 * DrawingMode implementing B_OP_ALPHA in "Pixel Composite" mode on
 * B_RGBA32.
 */

/** @file DrawingModeAlphaPCSolid.h
    @brief Solid-pattern fast path for B_OP_ALPHA Pixel-Composite. */

#ifndef DRAWING_MODE_ALPHA_PC_SOLID_H
#define DRAWING_MODE_ALPHA_PC_SOLID_H

#include "DrawingMode.h"


#define BLEND_ALPHA_PC(d, r, g, b, a) \
{ \
	BLEND_COMPOSITE16(d, r, g, b, a); \
}


#define ASSIGN_ALPHA_PC(d, r, g, b) \
{ \
	d[0] = (b); \
	d[1] = (g); \
	d[2] = (r); \
	d[3] = 255; \
}


/** @brief Solid-pattern B_OP_ALPHA Pixel-Composite blend for one pixel
           using the supplied @a color's alpha and AA cover. */
void
blend_pixel_alpha_pc_solid(int x, int y, const color_type& color, uint8 cover,
					 agg_buffer* buffer, const PatternHandler*)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	uint16 alpha = color.a * cover;
	if (alpha == 255 * 255) {
		ASSIGN_ALPHA_PC(p, color.r, color.g, color.b);
	} else {
		BLEND_ALPHA_PC(p, color.r, color.g, color.b, alpha);
	}
}


/** @brief Solid-pattern B_OP_ALPHA Pixel-Composite horizontal-line
           blend. */
void
blend_hline_alpha_pc_solid(int x, int y, unsigned len,
					 const color_type& color, uint8 cover,
					 agg_buffer* buffer, const PatternHandler*)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	uint16 alpha = color.a * cover;
	if (alpha == 0)
		return;

	if (alpha == 255 * 255) {
		do {
			ASSIGN_ALPHA_PC(p, color.r, color.g, color.b);
			p += 4;
		} while(--len);
		return;
	}

	do {
		BLEND_ALPHA_PC(p, color.r, color.g, color.b, alpha);
		p += 4;
	} while(--len);
}


/** @brief Solid-pattern anti-aliased horizontal solid span for
           B_OP_ALPHA Pixel-Composite. */
void
blend_solid_hspan_alpha_pc_solid(int x, int y, unsigned len,
						   const color_type& color, const uint8* covers,
						   agg_buffer* buffer, const PatternHandler*)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		uint16 alpha = color.a * *covers;
		if (alpha) {
			if(alpha == 255 * 255) {
				ASSIGN_ALPHA_PC(p, color.r, color.g, color.b);
			} else {
				BLEND_ALPHA_PC(p, color.r, color.g, color.b, alpha);
			}
		}
		covers++;
		p += 4;
	} while(--len);
}


/** @brief Solid-pattern anti-aliased vertical solid span for B_OP_ALPHA
           Pixel-Composite. */
void
blend_solid_vspan_alpha_pc_solid(int x, int y, unsigned len,
						   const color_type& color, const uint8* covers,
						   agg_buffer* buffer, const PatternHandler*)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	do {
		uint16 alpha = color.a * *covers;
		if (alpha) {
			if (alpha == 255 * 255) {
				ASSIGN_ALPHA_PC(p, color.r, color.g, color.b);
			} else {
				BLEND_ALPHA_PC(p, color.r, color.g, color.b, alpha);
			}
		}
		covers++;
		p += buffer->stride();
	} while(--len);
}


/** @brief Solid-pattern per-pixel-color horizontal span for B_OP_ALPHA
           Pixel-Composite. */
void
blend_color_hspan_alpha_pc_solid(int x, int y, unsigned len,
						   const color_type* colors,
						   const uint8* covers, uint8 cover,
						   agg_buffer* buffer, const PatternHandler*)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	if (covers) {
		// non-solid opacity
		do {
			uint16 alpha = colors->a * *covers;
			if (alpha) {
				if (alpha == 255 * 255) {
					ASSIGN_ALPHA_PC(p, colors->r, colors->g, colors->b);
				} else {
					BLEND_ALPHA_PC(p, colors->r, colors->g, colors->b, alpha);
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
				ASSIGN_ALPHA_PC(p, colors->r, colors->g, colors->b);
				p += 4;
				++colors;
			} while(--len);
		// solid partial opacity
		} else if (alpha) {
			do {
				BLEND_ALPHA_PC(p, colors->r, colors->g, colors->b, alpha);
				p += 4;
				++colors;
			} while(--len);
		}
	}
}


#endif // DRAWING_MODE_ALPHA_PC_SOLID_H
