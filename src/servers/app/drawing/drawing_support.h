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
 * MIT License. Copyright 2005-2007, Haiku.
 * Original authors: Stephan Aßmus.
 */

/** @file drawing_support.h
    @brief Inline helpers used by the software drawing back-end (fast fills, blends, snapping). */

#ifndef DRAWING_SUPPORT_H
#define DRAWING_SUPPORT_H


#include <SupportDefs.h>
#include <string.h>

class BRect;


/** @brief Fills @a numBytes bytes at @a dst with the 32-bit @a color value.
 *
 * Uses 64-bit stores when possible to amortise the per-pixel cost.
 *
 * @param dst       Destination byte pointer; must be 4-byte aligned.
 * @param color     Packed 32-bit pixel value to broadcast.
 * @param numBytes  Number of bytes to write; expected to be a multiple of 4.
 */
static inline void
gfxset32(uint8* dst, uint32 color, int32 numBytes)
{
	uint64 s64 = ((uint64)color << 32) | color;
	while (numBytes >= 8) {
		*(uint64*)dst = s64;
		numBytes -= 8;
		dst += 8;
	}
	if (numBytes == 4) {
		*(uint32*)dst = color;
	}
}

/** @brief Aliased view of a 32-bit pixel as four 8-bit channels. */
union pixel32 {
	uint32	data32;
	uint8	data8[4];
};

/** @brief Blends a horizontal run of @a pixels in B_RGB32/B_RGBA32 with a constant RGBA color.
 *
 * Performs a non-premultiplied source-over blend in place, writing through a small
 * temporary on the stack to avoid repeated read-modify-write of the destination.
 *
 * @param buffer  Pointer to the first pixel of the run (4 bytes per pixel).
 * @param pixels  Number of pixels to blend.
 * @param r       Source red component.
 * @param g       Source green component.
 * @param b       Source blue component.
 * @param a       Source alpha; 0 leaves the destination untouched, 255 replaces it.
 */
static inline void
blend_line32(uint8* buffer, int32 pixels, uint8 r, uint8 g, uint8 b, uint8 a)
{
	pixel32 p;

	r = (r * a) >> 8;
	g = (g * a) >> 8;
	b = (b * a) >> 8;
	a = 255 - a;

	uint8 tempBuffer[pixels * 4];

	uint8* t = tempBuffer;
	uint8* s = buffer;

	for (int32 i = 0; i < pixels; i++) {
		p.data32 = *(uint32*)s;

		t[0] = ((p.data8[0] * a) >> 8) + b;
		t[1] = ((p.data8[1] * a) >> 8) + g;
		t[2] = ((p.data8[2] * a) >> 8) + r;

		t += 4;
		s += 4;
	}

	memcpy(buffer, tempBuffer, pixels * 4);
}

/** @brief Snaps the corners of @a rect to the nearest integer pixel boundaries. */
void align_rect_to_pixels(BRect* rect);

#endif	// DRAWING_SUPPORT_H
