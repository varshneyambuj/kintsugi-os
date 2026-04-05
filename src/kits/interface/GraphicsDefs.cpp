/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2015, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Caz <turok2@currantbun.com>
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file GraphicsDefs.cpp
 * @brief Global graphics definitions and utility functions for the Interface Kit
 *
 * Provides implementations of global functions declared in GraphicsDefs.h, including
 * color space queries, screen-mode helpers, and color blending utilities.
 *
 * @see InterfaceDefs.cpp, BScreen
 */


#include <GraphicsDefs.h>

#include <AppServerLink.h>
#include <ServerProtocol.h>

#include <math.h>

// patterns
/** @brief Rendering pattern that draws only the foreground (high) color. */
const pattern B_SOLID_HIGH = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
/** @brief Rendering pattern that alternates foreground and background pixels in a checkerboard. */
const pattern B_MIXED_COLORS = {{0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55}};
/** @brief Rendering pattern that draws only the background (low) color. */
const pattern B_SOLID_LOW = {{0, 0, 0, 0, 0, 0, 0, 0}};

// colors
/** @brief The 32-bit RGBA sentinel value that represents a transparent pixel. */
const rgb_color B_TRANSPARENT_COLOR = {0x77, 0x74, 0x77, 0x00};
/** @brief Alias for B_TRANSPARENT_COLOR; kept for source compatibility. */
const rgb_color B_TRANSPARENT_32_BIT = {0x77, 0x74, 0x77, 0x00};
/** @brief The 8-bit color-map index reserved for transparency. */
const uint8 B_TRANSPARENT_8_BIT = 0xff;

/** @brief Magic color-map index that marks a transparent pixel in B_CMAP8 bitmaps. */
const uint8 B_TRANSPARENT_MAGIC_CMAP8 = 0xff;
/** @brief Magic pixel value for transparency in little-endian B_RGBA15 bitmaps. */
const uint16 B_TRANSPARENT_MAGIC_RGBA15 = 0x39ce;
/** @brief Magic pixel value for transparency in big-endian B_RGBA15 bitmaps. */
const uint16 B_TRANSPARENT_MAGIC_RGBA15_BIG = 0xce39;
/** @brief Magic pixel value for transparency in little-endian B_RGBA32 bitmaps. */
const uint32 B_TRANSPARENT_MAGIC_RGBA32 = 0x00777477;
/** @brief Magic pixel value for transparency in big-endian B_RGBA32 bitmaps. */
const uint32 B_TRANSPARENT_MAGIC_RGBA32_BIG = 0x77747700;

// misc.
/** @brief Identifier for the primary (main) screen. */
const struct screen_id B_MAIN_SCREEN_ID = {0};


/**
 * @brief Compute the perceptual brightness of this colour.
 *
 * Uses the HSP colour model formula to weight the red, green, and blue channels
 * according to human visual sensitivity, yielding a value between 0 (black) and
 * 255 (white). Values above 127 are conventionally considered "light".
 *
 * @return An integer in [0, 255] representing the perceptual brightness.
 * @see http://alienryderflex.com/hsp.html
 */
int32
rgb_color::Brightness() const
{
	// From http://alienryderflex.com/hsp.html
	// Useful in particular to decide if the color is "light" or "dark"
	// by checking if the perceptual brightness is > 127.

	return (uint8)roundf(sqrtf(
		0.299f * red * red + 0.587f * green * green + 0.114 * blue * blue));
}


/**
 * @brief Linearly interpolate between two colours, ignoring their alpha values.
 *
 * Each channel of \a color1 is blended towards the corresponding channel of
 * \a color2 by the fraction \a amount / 255. An \a amount of 0 returns
 * \a color1 unchanged; 255 returns \a color2.
 *
 * @param color1 The starting colour (and base for blending).
 * @param color2 The target colour.
 * @param amount Blend weight in [0, 255]; 0 = full color1, 255 = full color2.
 * @return The blended colour.
 * @see blend_color()
 */
// Mix two colors without respect for their alpha values
rgb_color
mix_color(rgb_color color1, rgb_color color2, uint8 amount)
{
	color1.red = (uint8)(((int16(color2.red) - int16(color1.red)) * amount)
		/ 255 + color1.red);
	color1.green = (uint8)(((int16(color2.green) - int16(color1.green))
		* amount) / 255 + color1.green);
	color1.blue = (uint8)(((int16(color2.blue) - int16(color1.blue)) * amount)
		/ 255 + color1.blue );
	color1.alpha = (uint8)(((int16(color2.alpha) - int16(color1.alpha))
		* amount) / 255 + color1.alpha );

	return color1;
}


/**
 * @brief Alpha-aware blend of two colours.
 *
 * Similar to mix_color() but computes an intermediate alpha from both
 * colours' alpha channels before blending the RGB components, producing
 * correct results when compositing partially-transparent colours.
 *
 * @param color1 The base colour.
 * @param color2 The overlay colour.
 * @param amount Blend weight in [0, 255].
 * @return The alpha-composited colour.
 * @see mix_color()
 */
// Mix two colors, respecting their alpha values.
rgb_color
blend_color(rgb_color color1, rgb_color color2, uint8 amount)
{
	const uint8 alphaMix = (uint8)(((int16(color2.alpha) - int16(255
		- color1.alpha)) * amount) / 255 + (255 - color1.alpha));

	color1.red = (uint8)(((int16(color2.red) - int16(color1.red)) * alphaMix)
		/ 255 + color1.red );
	color1.green = (uint8)(((int16(color2.green) - int16(color1.green))
		* alphaMix) / 255 + color1.green);
	color1.blue = (uint8)(((int16(color2.blue) - int16(color1.blue))
		* alphaMix) / 255 + color1.blue);
	color1.alpha = (uint8)(((int16(color2.alpha) - int16(color1.alpha))
		* amount) / 255 + color1.alpha);

	return color1;
}


/**
 * @brief Produce a visually dimmed version of a colour suitable for disabled UI.
 *
 * Blends \a color towards \a background at a fixed 185/255 ratio, approximating
 * the desaturated look used by the OS for inactive controls.
 *
 * @param color      The original active colour.
 * @param background The background colour to blend towards.
 * @return The disabled-state colour.
 * @see mix_color()
 */
rgb_color
disable_color(rgb_color color, rgb_color background)
{
	return mix_color(color, background, 185);
}


/**
 * @brief Query the byte layout of a given colour space.
 *
 * Returns the chunk size (bytes per pixel group), the required row alignment,
 * and the number of pixels encoded per chunk for the specified \a space.
 *
 * @param space          The colour space to query.
 * @param pixelChunk     Output: number of bytes per pixel chunk; may be NULL.
 * @param rowAlignment   Output: required row-start alignment in bytes; may be NULL.
 * @param pixelsPerChunk Output: number of pixels per chunk; may be NULL.
 * @return B_OK on success.
 * @retval B_BAD_VALUE If \a space is unsupported or B_NO_COLOR_SPACE.
 */
status_t
get_pixel_size_for(color_space space, size_t *pixelChunk, size_t *rowAlignment,
	size_t *pixelsPerChunk)
{
	status_t status = B_OK;
	int32 bytesPerPixel = 0;
	int32 pixPerChunk = 0;
	switch (space) {
		// supported
		case B_RGBA64: case B_RGBA64_BIG:
			bytesPerPixel = 8;
			pixPerChunk = 2;
			break;
		case B_RGB48: case B_RGB48_BIG:
			bytesPerPixel = 6;
			pixPerChunk = 2;
			break;
		case B_RGB32: case B_RGBA32:
		case B_RGB32_BIG: case B_RGBA32_BIG:
		case B_UVL32: case B_UVLA32:
		case B_LAB32: case B_LABA32:
		case B_HSI32: case B_HSIA32:
		case B_HSV32: case B_HSVA32:
		case B_HLS32: case B_HLSA32:
		case B_CMY32: case B_CMYA32: case B_CMYK32:
			bytesPerPixel = 4;
			pixPerChunk = 1;
			break;
		case B_RGB24: case B_RGB24_BIG:
		case B_UVL24: case B_LAB24: case B_HSI24:
		case B_HSV24: case B_HLS24: case B_CMY24:
			bytesPerPixel = 3;
			pixPerChunk = 1;
			break;
		case B_RGB16:		case B_RGB15:		case B_RGBA15:
		case B_RGB16_BIG:	case B_RGB15_BIG:	case B_RGBA15_BIG:
			bytesPerPixel = 2;
			pixPerChunk = 1;
			break;
		case B_CMAP8: case B_GRAY8:
			bytesPerPixel = 1;
			pixPerChunk = 1;
			break;
		case B_GRAY1:
			bytesPerPixel = 1;
			pixPerChunk = 8;
			break;
		case B_YCbCr422: case B_YUV422:
			bytesPerPixel = 4;
			pixPerChunk = 2;
			break;
		case B_YCbCr411: case B_YUV411:
			bytesPerPixel = 12;
			pixPerChunk = 8;
			break;
		case B_YCbCr444: case B_YUV444:
			bytesPerPixel = 3;
			pixPerChunk = 1;
			break;
		// TODO: I don't know if it's correct,
		// but beos reports B_YUV420 to be
		// 6 bytes per pixel and 4 pixel per chunk
		case B_YCbCr420: case B_YUV420:
			bytesPerPixel = 3;
			pixPerChunk = 2;
			break;
		case B_YUV9:
			bytesPerPixel = 5;
			pixPerChunk = 4;
			break;
		case B_YUV12:
			bytesPerPixel = 6;
			pixPerChunk = 4;
			break;
		// unsupported
		case B_NO_COLOR_SPACE:
		default:
			status = B_BAD_VALUE;
			break;
	}

	if (pixelChunk != NULL)
		*pixelChunk = bytesPerPixel;

	size_t alignment = 0;
	if (bytesPerPixel != 0) {
		alignment = (sizeof(int) % bytesPerPixel) * sizeof(int);
		if (alignment < sizeof(int))
			alignment = sizeof(int);
	}

	if (rowAlignment!= NULL)
		*rowAlignment = alignment;

	if (pixelsPerChunk!= NULL)
		*pixelsPerChunk = pixPerChunk;

	return status;
}


/**
 * @brief Ask the app_server which overlay flags are supported for a colour space.
 *
 * Sends an AS_GET_BITMAP_SUPPORT_FLAGS request and reads back the bitmask of
 * supported overlay capabilities for the given \a space.
 *
 * @param space The colour space to query.
 * @return A bitmask of overlay support flags, or 0 if the query fails.
 */
static uint32
get_overlay_flags(color_space space)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_BITMAP_SUPPORT_FLAGS);
	link.Attach<uint32>((uint32)space);

	uint32 flags = 0;
	int32 code;
	if (link.FlushWithReply(code) == B_OK && code == B_OK) {
		if (link.Read<uint32>(&flags) < B_OK)
			flags = 0;
	}
	return flags;
}


/**
 * @brief Determine whether BBitmap supports a given colour space and its capabilities.
 *
 * Returns true for all colour spaces that BBitmap can represent. For spaces that
 * also support direct drawing and/or attached BViews, \a supportFlags is updated
 * with the corresponding B_VIEWS_SUPPORT_DRAW_BITMAP and
 * B_BITMAPS_SUPPORT_ATTACHED_VIEWS flags. Overlay support flags from the
 * app_server are ORed in for all supported spaces.
 *
 * @param space        The colour space to test.
 * @param supportFlags Output: capability flags for \a space; may be NULL.
 * @return true if \a space is supported by BBitmap, false otherwise.
 */
bool
bitmaps_support_space(color_space space, uint32 *supportFlags)
{
	bool result = true;
	switch (space) {
		// supported, also for drawing and for attaching BViews
		case B_RGB32:		case B_RGBA32:		case B_RGB24:
		case B_RGB32_BIG:	case B_RGBA32_BIG:	case B_RGB24_BIG:
		case B_RGB16:		case B_RGB15:		case B_RGBA15:
		case B_RGB16_BIG:	case B_RGB15_BIG:	case B_RGBA15_BIG:
		case B_CMAP8:		case B_GRAY8:		case B_GRAY1:
			if (supportFlags != NULL) {
				*supportFlags = B_VIEWS_SUPPORT_DRAW_BITMAP
					| B_BITMAPS_SUPPORT_ATTACHED_VIEWS
					| get_overlay_flags(space);
			}
			break;

		// supported, but cannot draw
		case B_RGBA64: case B_RGBA64_BIG:
		case B_RGB48: case B_RGB48_BIG:
		case B_YCbCr422: case B_YCbCr411: case B_YCbCr444: case B_YCbCr420:
		case B_YUV422: case B_YUV411: case B_YUV444: case B_YUV420:
		case B_UVL24: case B_UVL32: case B_UVLA32:
		case B_LAB24: case B_LAB32: case B_LABA32:
		case B_HSI24: case B_HSI32: case B_HSIA32:
		case B_HSV24: case B_HSV32: case B_HSVA32:
		case B_HLS24: case B_HLS32: case B_HLSA32:
		case B_CMY24: case B_CMY32: case B_CMYA32: case B_CMYK32:
			if (supportFlags != NULL)
				*supportFlags = get_overlay_flags(space);
			break;
		// unsupported
		case B_NO_COLOR_SPACE:
		case B_YUV9: case B_YUV12:
			result = false;
			break;
	}
	return result;
}
