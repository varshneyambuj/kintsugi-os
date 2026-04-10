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
 * Authors:
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2006 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold (bonefish@users.sf.net)
 *       Michael Lotz <mmlr@mlotz.ch>
 */


/**
 * @file ColorConversion.cpp
 * @brief Color space conversion utilities for the Interface Kit
 *
 * Provides functions for converting pixel data between different color spaces
 * (e.g., RGB32, CMAP8, GRAY8) and a palette-to-true-color conversion helper
 * class used internally by BBitmap and BColorControl.
 *
 * @see BBitmap, BColorControl
 */


#include "ColorConversion.h"

#include <InterfaceDefs.h>
#include <Locker.h>
#include <Point.h>

#include <Palette.h>

#include <new>
#include <string.h>
#include <pthread.h>


using std::nothrow;


namespace BPrivate {

/**
 * @brief Compute the perceptual brightness of an RGB24 color.
 *
 * Uses integer-weighted luminance coefficients that approximate the
 * ITU-R BT.601 standard: brightness = (308*R + 600*G + 116*B) / 1024.
 *
 * @param red   Red component (0–255).
 * @param green Green component (0–255).
 * @param blue  Blue component (0–255).
 * @return Luminance value in the range [0, 255].
 */
static inline
uint8
brightness_for(uint8 red, uint8 green, uint8 blue)
{
	// brightness = 0.301 * red + 0.586 * green + 0.113 * blue
	// we use for performance reasons:
	// brightness = (308 * red + 600 * green + 116 * blue) / 1024
	return uint8((308 * red + 600 * green + 116 * blue) / 1024);
}


/**
 * @brief Compute a psycho-visual distance metric between two RGB24 colors.
 *
 * Implements a perceptually weighted Euclidean distance in RGB space. The
 * weighting varies the contribution of the red and blue channels based on
 * their mean value, following common low-cost approximations to the CIE
 * color difference formula.
 *
 * @param red1   Red component of the first color.
 * @param green1 Green component of the first color.
 * @param blue1  Blue component of the first color.
 * @param red2   Red component of the second color.
 * @param green2 Green component of the second color.
 * @param blue2  Blue component of the second color.
 * @return Non-negative distance value; 0 if and only if the two colors are equal.
 */
static inline
unsigned
color_distance(uint8 red1, uint8 green1, uint8 blue1,
			   uint8 red2, uint8 green2, uint8 blue2)
{
	// euklidian distance (its square actually)
	int rd = (int)red1 - (int)red2;
	int gd = (int)green1 - (int)green2;
	int bd = (int)blue1 - (int)blue2;
	//return rd * rd + gd * gd + bd * bd;

	// distance according to psycho-visual tests
	int rmean = ((int)red1 + (int)red2) / 2;
	return (((512 + rmean) * rd * rd) >> 8)
		   + 4 * gd * gd
		   + (((767 - rmean) * bd * bd) >> 8);
}


/**
 * @brief Construct an uninitialized PaletteConverter.
 *
 * The converter is not usable until SetTo() or InitializeDefault() is
 * called. InitCheck() will return B_NO_INIT until then.
 *
 * @see SetTo()
 * @see InitCheck()
 */
PaletteConverter::PaletteConverter()
	: fColorMap(NULL),
	  fOwnColorMap(NULL),
	  fCStatus(B_NO_INIT)
{
}


/**
 * @brief Construct a PaletteConverter and initialize it from a raw palette.
 *
 * Builds the internal color_map by finding the closest palette entry for
 * every 15-bit RGB value. This can be slow on the first call.
 *
 * @param palette Pointer to an array of exactly 256 rgb_color entries that
 *                defines the palette. Must not be NULL.
 * @see SetTo(const rgb_color*)
 */
PaletteConverter::PaletteConverter(const rgb_color *palette)
	: fColorMap(NULL),
	  fOwnColorMap(NULL),
	  fCStatus(B_NO_INIT)
{
	SetTo(palette);
}


/**
 * @brief Construct a PaletteConverter and initialize it from an existing color_map.
 *
 * The converter borrows the supplied color_map without taking ownership; the
 * caller must keep it alive for the lifetime of this object.
 *
 * @param colorMap Pointer to a fully initialized color_map. Must not be NULL.
 * @see SetTo(const color_map*)
 */
PaletteConverter::PaletteConverter(const color_map *colorMap)
	: fColorMap(NULL),
	  fOwnColorMap(NULL),
	  fCStatus(B_NO_INIT)
{
	SetTo(colorMap);
}


/**
 * @brief Destroy the PaletteConverter and release any owned color_map.
 *
 * Only the internally allocated color_map (created by SetTo(const rgb_color*))
 * is deleted. A color_map supplied via SetTo(const color_map*) is not freed.
 */
PaletteConverter::~PaletteConverter()
{
	delete fOwnColorMap;
}


/**
 * @brief Initialize the converter from a raw 256-entry palette.
 *
 * Allocates a new color_map and populates its index_map by finding the
 * nearest palette color for every possible 15-bit RGB value using
 * color_distance(). This is the slow path; prefer SetTo(const color_map*)
 * when a pre-built color_map is available.
 *
 * @param palette Pointer to an array of exactly 256 rgb_color entries.
 * @return B_OK on success.
 * @retval B_BAD_VALUE  \a palette is NULL.
 * @retval B_NO_MEMORY  Could not allocate the internal color_map.
 * @see SetTo(const color_map*)
 * @see InitCheck()
 */
status_t
PaletteConverter::SetTo(const rgb_color *palette)
{
	// cleanup
	SetTo((const color_map*)NULL);
	status_t error = (palette ? B_OK : B_BAD_VALUE);
	// alloc color map
	if (error == B_OK) {
		fOwnColorMap = new(nothrow) color_map;
		if (fOwnColorMap == NULL)
			error = B_NO_MEMORY;
	}
	// init color map
	if (error == B_OK) {
		fColorMap = fOwnColorMap;
		// init color list
		memcpy((void*)fOwnColorMap->color_list, palette, sizeof(rgb_color) * 256);
		// init index map
// TODO: build this list takes about 2 seconds in qemu on my system
//		(because of color_distance())
		for (int32 color = 0; color < 32768; color++) {
			// get components
			uint8 red = (color & 0x7c00) >> 7;
			uint8 green = (color & 0x3e0) >> 2;
			uint8 blue = (color & 0x1f) << 3;
			red |= red >> 5;
			green |= green >> 5;
			blue |= blue >> 5;
			// find closest color
			uint8 closestIndex = 0;
			unsigned closestDistance = UINT_MAX;
			for (int32 i = 0; i < 256; i++) {
				const rgb_color &c = fOwnColorMap->color_list[i];
				unsigned distance = color_distance(red, green, blue,
												   c.red, c.green, c.blue);
				if (distance < closestDistance) {
					closestIndex = i;
					closestDistance = distance;
				}
			}
			fOwnColorMap->index_map[color] = closestIndex;
		}
		// no need to init inversion map
	}
	fCStatus = error;
	return error;
}


/**
 * @brief Initialize the converter from a pre-built color_map.
 *
 * The converter borrows the pointer without taking ownership; the caller is
 * responsible for keeping the color_map alive. Any previously owned color_map
 * is deleted before the new pointer is stored.
 *
 * @param colorMap Pointer to a fully initialized color_map, or NULL to reset
 *                 the converter to an uninitialized state.
 * @return B_OK if \a colorMap is non-NULL.
 * @retval B_BAD_VALUE \a colorMap is NULL.
 * @see SetTo(const rgb_color*)
 * @see InitCheck()
 */
status_t
PaletteConverter::SetTo(const color_map *colorMap)
{
	// cleanup
	if (fOwnColorMap) {
		delete fOwnColorMap;
		fOwnColorMap = NULL;
	}
	// set
	fColorMap = colorMap;
	fCStatus = (fColorMap ? B_OK : B_BAD_VALUE);
	return fCStatus;
}


/**
 * @brief Return the initialization status of the converter.
 *
 * @return B_OK if the converter is ready for use.
 * @retval B_NO_INIT  No SetTo() call has succeeded yet.
 * @retval B_BAD_VALUE The last SetTo() was given a NULL argument.
 * @retval B_NO_MEMORY The last SetTo(const rgb_color*) ran out of memory.
 */
status_t
PaletteConverter::InitCheck() const
{
	return fCStatus;
}


/**
 * @brief Return the palette index closest to a packed RGB15 color.
 *
 * @param rgb Packed 15-bit color value with layout R[14:10] G[9:5] B[4:0].
 * @return Best-match index into the 256-entry palette.
 * @note The converter must be initialized before calling this method.
 */
inline
uint8
PaletteConverter::IndexForRGB15(uint16 rgb) const
{
	return fColorMap->index_map[rgb];
}


/**
 * @brief Return the palette index closest to an RGB15 color given as separate components.
 *
 * Only the five least-significant bits of each component are used.
 *
 * @param red   Red component, bits [4:0].
 * @param green Green component, bits [4:0].
 * @param blue  Blue component, bits [4:0].
 * @return Best-match index into the 256-entry palette.
 * @note The converter must be initialized before calling this method.
 */
inline
uint8
PaletteConverter::IndexForRGB15(uint8 red, uint8 green, uint8 blue) const
{
	// the 5 least significant bits are used
	return fColorMap->index_map[(red << 10) | (green << 5) | blue];
}


/**
 * @brief Return the palette index closest to a packed RGB16 color.
 *
 * The 6-bit green channel is collapsed to 5 bits before the index_map lookup.
 *
 * @param rgb Packed 16-bit color value with layout R[15:11] G[10:5] B[4:0].
 * @return Best-match index into the 256-entry palette.
 * @note The converter must be initialized before calling this method.
 */
inline
uint8
PaletteConverter::IndexForRGB16(uint16 rgb) const
{
	return fColorMap->index_map[((rgb >> 1) & 0x7fe0) | (rgb & 0x1f)];
}


/**
 * @brief Return the palette index closest to an RGB16 color given as separate components.
 *
 * Red and blue use their five least-significant bits; green uses six bits.
 *
 * @param red   Red component, bits [4:0].
 * @param green Green component, bits [5:0].
 * @param blue  Blue component, bits [4:0].
 * @return Best-match index into the 256-entry palette.
 * @note The converter must be initialized before calling this method.
 */
inline
uint8
PaletteConverter::IndexForRGB16(uint8 red, uint8 green, uint8 blue) const
{
	// the 5 (for red, blue) / 6 (for green) least significant bits are used
	return fColorMap->index_map[(red << 10) | ((green & 0x3e) << 4) | blue];
}


/**
 * @brief Return the palette index closest to a packed RGB24-in-32 color.
 *
 * Extracts the top 5 bits of each channel and looks up the nearest palette
 * entry. The lowest 8 bits of \a rgb (the alpha/unused byte) are ignored.
 *
 * @param rgb Packed 32-bit value with layout R[31:24] G[23:16] B[15:8].
 * @return Best-match index into the 256-entry palette.
 * @note The converter must be initialized before calling this method.
 */
inline
uint8
PaletteConverter::IndexForRGB24(uint32 rgb) const
{
	return fColorMap->index_map[((rgb & 0xf8000000) >> 17)
								| ((rgb & 0xf80000) >> 14)
								| ((rgb & 0xf800) >> 11)];
}


/**
 * @brief Return the palette index closest to an RGB24 color given as separate components.
 *
 * @param red   Red component (0–255).
 * @param green Green component (0–255).
 * @param blue  Blue component (0–255).
 * @return Best-match index into the 256-entry palette.
 * @note The converter must be initialized before calling this method.
 */
inline
uint8
PaletteConverter::IndexForRGB24(uint8 red, uint8 green, uint8 blue) const
{
	return fColorMap->index_map[((red & 0xf8) << 7)
								| ((green & 0xf8) << 2)
								| (blue >> 3)];
}


/**
 * @brief Return the palette index closest to a packed RGBA32 color, with transparency support.
 *
 * If the alpha component (bits [7:0]) is less than 128 the transparent
 * palette sentinel B_TRANSPARENT_MAGIC_CMAP8 is returned instead of the
 * nearest opaque color.
 *
 * @param rgba Packed 32-bit value with layout R[31:24] G[23:16] B[15:8] A[7:0].
 * @return Best-match palette index, or B_TRANSPARENT_MAGIC_CMAP8 for
 *         semi-transparent pixels.
 * @note The converter must be initialized before calling this method.
 */
inline
uint8
PaletteConverter::IndexForRGBA32(uint32 rgba) const
{
	if ((rgba & 0x000000ff) < 128)
		return B_TRANSPARENT_MAGIC_CMAP8;
	return IndexForRGB24(rgba);
}


/**
 * @brief Return the palette index closest to an 8-bit grayscale value.
 *
 * Delegates to IndexForRGB24() with equal R, G, B components.
 *
 * @param gray Grayscale intensity (0–255).
 * @return Best-match index into the 256-entry palette.
 * @note The converter must be initialized before calling this method.
 */
inline
uint8
PaletteConverter::IndexForGray(uint8 gray) const
{
	return IndexForRGB24(gray, gray, gray);
}


/**
 * @brief Return the rgb_color entry for a given palette index.
 *
 * @param index Palette index in [0, 255].
 * @return Const reference to the corresponding rgb_color in the color list.
 * @note The converter must be initialized before calling this method.
 */
inline
const rgb_color &
PaletteConverter::RGBColorForIndex(uint8 index) const
{
	return fColorMap->color_list[index];
}


/**
 * @brief Return the RGB15 representation of a palette entry.
 *
 * @param index Palette index in [0, 255].
 * @return Packed 16-bit value with layout R[14:10] G[9:5] B[4:0].
 * @note The converter must be initialized before calling this method.
 */
inline
uint16
PaletteConverter::RGB15ColorForIndex(uint8 index) const
{
	const rgb_color &color = fColorMap->color_list[index];
	return ((color.red & 0xf8) << 7)
		   | ((color.green & 0xf8) << 2)
		   | (color.blue >> 3);
}


/**
 * @brief Return the RGB16 representation of a palette entry.
 *
 * @param index Palette index in [0, 255].
 * @return Packed 16-bit value with layout R[15:11] G[10:5] B[4:0].
 * @note The converter must be initialized before calling this method.
 */
inline
uint16
PaletteConverter::RGB16ColorForIndex(uint8 index) const
{
	const rgb_color &color = fColorMap->color_list[index];
	return ((color.red & 0xf8) << 8)
		   | ((color.green & 0xfc) << 3)
		   | (color.blue >> 3);
}


/**
 * @brief Return the RGBA32 representation of a palette entry as a packed uint32.
 *
 * @param index Palette index in [0, 255].
 * @return Packed 32-bit value with layout A[31:24] B[23:16] G[15:8] R[7:0].
 * @note The converter must be initialized before calling this method.
 */
inline
uint32
PaletteConverter::RGBA32ColorForIndex(uint8 index) const
{
	const rgb_color &color = fColorMap->color_list[index];
	return (color.red << 16) | (color.green << 8) | color.blue
		| (color.alpha << 24);
}


/**
 * @brief Decompose a palette entry into separate RGBA components.
 *
 * @param index  Palette index in [0, 255].
 * @param red    Receives the red component.
 * @param green  Receives the green component.
 * @param blue   Receives the blue component.
 * @param alpha  Receives the alpha component.
 * @note The converter must be initialized before calling this method.
 */
inline
void
PaletteConverter::RGBA32ColorForIndex(uint8 index, uint8 &red, uint8 &green,
									 uint8 &blue, uint8 &alpha) const
{
	const rgb_color &color = fColorMap->color_list[index];
	red = color.red;
	green = color.green;
	blue = color.blue;
	alpha = color.alpha;
}


/**
 * @brief Return the grayscale brightness of a palette entry.
 *
 * @param index Palette index in [0, 255].
 * @return Grayscale value in [0, 255] computed via brightness_for().
 * @note The converter must be initialized before calling this method.
 */
inline
uint8
PaletteConverter::GrayColorForIndex(uint8 index) const
{
	const rgb_color &color = fColorMap->color_list[index];
	return brightness_for(color.red, color.green, color.blue);
}


/** @brief pthread_once guard ensuring the global PaletteConverter is initialized exactly once. */
static pthread_once_t sPaletteConverterInitOnce = PTHREAD_ONCE_INIT;
/** @brief Process-wide shared PaletteConverter backed by the system color map. */
static PaletteConverter	sPaletteConverter;


/**
 * @brief Initialize the process-wide PaletteConverter exactly once.
 *
 * Uses pthread_once to guarantee thread-safe single initialization.
 * When \a useServer is true the live system_colors() palette (fetched from
 * the app server) is used; otherwise the compile-time kSystemPalette
 * constant is used, which is safe before the app server connection is open.
 *
 * @param useServer Pass true to obtain the palette from the running app server,
 *                  false to use the built-in default palette.
 * @return B_OK on success, or the error code from SetTo() if initialization failed.
 * @see InitCheck()
 */
/*static*/ status_t
PaletteConverter::InitializeDefault(bool useServer)
{
	if (sPaletteConverter.InitCheck() != B_OK) {
		pthread_once(&sPaletteConverterInitOnce,
			useServer
				? &_InitializeDefaultAppServer
				: &_InitializeDefaultNoAppServer);
	}

	return sPaletteConverter.InitCheck();
}


/**
 * @brief pthread_once callback that initializes sPaletteConverter from the app server.
 *
 * Retrieves the live system palette via system_colors() and passes it to
 * sPaletteConverter.SetTo(). Called at most once per process.
 */
/*static*/ void
PaletteConverter::_InitializeDefaultAppServer()
{
	sPaletteConverter.SetTo(system_colors());
}


/**
 * @brief pthread_once callback that initializes sPaletteConverter from the built-in palette.
 *
 * Uses the compile-time kSystemPalette constant so that color conversions can
 * work before an app server connection is established.
 */
/*static*/ void
PaletteConverter::_InitializeDefaultNoAppServer()
{
	sPaletteConverter.SetTo(kSystemPalette);
}


/** @brief Signature for a pixel-read callback that advances a uint8 source pointer. */
typedef uint32 (readFunc)(const uint8 **source, int32 index);
/** @brief Signature for a 64-bit pixel-read callback that advances a uint16 source pointer. */
typedef uint64 (read64Func)(const uint16 **source, int32 index);
/** @brief Signature for a pixel-write callback that advances a uint8 destination pointer. */
typedef void (writeFunc)(uint8 **dest, uint8 *data, int32 index);


/**
 * @brief Read one RGB48 pixel (three consecutive uint16 channels) and advance the source pointer.
 *
 * Packs the three 16-bit channel words into a uint64 as R[47:32] G[31:16] B[15:0].
 *
 * @param source Pointer to the current read position; advanced by 3 uint16 words on return.
 * @param index  Current pixel index within the row (unused by this implementation).
 * @return Packed 64-bit pixel value.
 */
uint64
ReadRGB48(const uint16 **source, int32 index)
{
	uint64 result = (*source)[0] | ((uint64)((*source)[1]) << 16)
		| ((uint64)((*source)[2]) << 32);
	*source += 3;
	return result;
}


/**
 * @brief Write one RGB24 pixel (three bytes) and advance the destination pointer.
 *
 * @param dest  Pointer to the current write position; advanced by 3 bytes on return.
 * @param data  Source pixel data in BGRA byte order (little-endian internal format).
 * @param index Current pixel index within the row (unused by this implementation).
 */
void
WriteRGB24(uint8 **dest, uint8 *data, int32 index)
{
	(*dest)[0] = data[0];
	(*dest)[1] = data[1];
	(*dest)[2] = data[2];
	*dest += 3;
}


/**
 * @brief Read one RGB24 pixel (three bytes) and advance the source pointer.
 *
 * Packs the three bytes into a uint32 as 0x00RRGGBB (little-endian byte order).
 *
 * @param source Pointer to the current read position; advanced by 3 bytes on return.
 * @param index  Current pixel index within the row (unused by this implementation).
 * @return Packed 32-bit pixel value.
 */
uint32
ReadRGB24(const uint8 **source, int32 index)
{
	uint32 result = (*source)[0] | ((*source)[1] << 8) | ((*source)[2] << 16);
	*source += 3;
	return result;
}


/**
 * @brief Convert one pixel to 8-bit grayscale and write it to the destination.
 *
 * Uses integer luminance weights: (R*308 + G*600 + B*116) >> 10.
 *
 * @param dest  Pointer to the current write position; advanced by 1 byte on return.
 * @param data  Source pixel in BGRA byte order (data[0]=B, data[1]=G, data[2]=R).
 * @param index Current pixel index within the row (unused by this implementation).
 */
void
WriteGray8(uint8 **dest, uint8 *data, int32 index)
{
	**dest = (data[2] * 308 + data[1] * 600 + data[0] * 116) >> 10;
	// this would boost the speed but is less accurate:
	//*dest = (data[2] << 8) + (data[1] << 9) + (data[0] << 8) >> 10;
	(*dest)++;
}


/**
 * @brief Read one 8-bit grayscale pixel and advance the source pointer.
 *
 * The single gray byte is returned in the low 8 bits of the result;
 * higher bits are zero.
 *
 * @param source Pointer to the current read position; advanced by 1 byte on return.
 * @param index  Current pixel index within the row (unused by this implementation).
 * @return Grayscale value in [0, 255].
 */
uint32
ReadGray8(const uint8 **source, int32 index)
{
	uint32 result = **source;
	(*source)++;
	return result;
}


/**
 * @brief Write one pixel to a B_GRAY1 (1-bit monochrome) destination buffer.
 *
 * Converts the source pixel to a luminance value and sets or clears the
 * appropriate bit within the current destination byte.  The destination pointer
 * is advanced only when the last bit of a byte has been written.
 *
 * @param dest  Pointer to the current byte in the destination buffer; advanced
 *              by 1 byte only after every 8th pixel.
 * @param data  Source pixel in BGRA byte order (data[0]=B, data[1]=G, data[2]=R).
 * @param index Zero-based pixel index within the current row; determines the bit position.
 */
void
WriteGray1(uint8 **dest, uint8 *data, int32 index)
{
	int32 shift = 7 - (index % 8);
	**dest &= ~(0x01 << shift);
	**dest |= (data[2] * 308 + data[1] * 600 + data[0] * 116) >> (17 - shift);
	if (shift == 0)
		(*dest)++;
}


/**
 * @brief Read one pixel from a B_GRAY1 (1-bit monochrome) source buffer.
 *
 * In B_GRAY1 a set bit represents black (0x00) and a clear bit represents
 * white (0xFF).  The source pointer is advanced only after every 8th pixel.
 *
 * @param source Pointer to the current byte in the source buffer; advanced
 *               by 1 byte only after every 8th pixel.
 * @param index  Zero-based pixel index within the current row; selects the bit position.
 * @return 0x00 for a set bit (black) or 0xFF for a clear bit (white).
 */
uint32
ReadGray1(const uint8 **source, int32 index)
{
	int32 shift = 7 - (index % 8);
	// In B_GRAY1, a set bit means black (highcolor), a clear bit means white
	// (low/view color). So we map them to 00 and 0xFF, respectively.
	uint32 result = ((**source >> shift) & 0x01) ? 0x00 : 0xFF;
	if (shift == 0)
		(*source)++;
	return result;
}


/**
 * @brief Convert one RGBA32 pixel to its nearest CMAP8 palette index and write it.
 *
 * Delegates to sPaletteConverter.IndexForRGBA32(), which handles the
 * transparency sentinel B_TRANSPARENT_MAGIC_CMAP8 for low-alpha pixels.
 *
 * @param dest  Pointer to the current write position; advanced by 1 byte on return.
 * @param data  Source pixel in RGBA32 byte layout (interpreted as uint32).
 * @param index Current pixel index within the row (unused by this implementation).
 */
void
WriteCMAP8(uint8 **dest, uint8 *data, int32 index)
{
	**dest = sPaletteConverter.IndexForRGBA32(*(uint32 *)data);
	(*dest)++;
}


/**
 * @brief Read one CMAP8 pixel index and expand it to RGBA32.
 *
 * Looks up the palette entry via sPaletteConverter.RGBA32ColorForIndex().
 *
 * @param source Pointer to the current read position; advanced by 1 byte on return.
 * @param index  Current pixel index within the row (unused by this implementation).
 * @return Packed RGBA32 color with layout A[31:24] B[23:16] G[15:8] R[7:0].
 */
uint32
ReadCMAP8(const uint8 **source, int32 index)
{
	uint32 result = sPaletteConverter.RGBA32ColorForIndex(**source);
	(*source)++;
	return result;
}


/**
 * @brief Low-level pixel converter from a 64-bit source format to a 32-bit destination format.
 *
 * Iterates over a rectangular region of pixels, reading each source pixel via
 * \a srcFunc (or directly as srcByte), applying per-channel bit-shift and mask
 * operations to remap the color components, then writing each destination pixel
 * via \a dstFunc (or directly as dstByte).  Boundary checks against
 * \a srcBitsLength and \a dstBitsLength prevent buffer overruns.
 *
 * @param srcBits          Pointer to the source pixel buffer.
 * @param dstBits          Pointer to the destination pixel buffer.
 * @param srcBitsLength    Byte length of the source buffer.
 * @param dstBitsLength    Byte length of the destination buffer.
 * @param redShift         Bit shift to apply to the red channel (positive = right).
 * @param greenShift       Bit shift to apply to the green channel.
 * @param blueShift        Bit shift to apply to the blue channel.
 * @param alphaShift       Bit shift to apply to the alpha channel.
 * @param alphaBits        Number of alpha bits in the source (0 if no alpha).
 * @param redMask          Bitmask isolating the red channel in the destination word.
 * @param greenMask        Bitmask isolating the green channel in the destination word.
 * @param blueMask         Bitmask isolating the blue channel in the destination word.
 * @param alphaMask        Bitmask isolating the alpha channel; used as fill when
 *                         \a alphaBits is 0.
 * @param srcBytesPerRow   Row stride of the source buffer in bytes.
 * @param dstBytesPerRow   Row stride of the destination buffer in bytes.
 * @param srcBitsPerPixel  Bits per source pixel.
 * @param dstBitsPerPixel  Bits per destination pixel.
 * @param srcColorSpace    Source color space identifier.
 * @param dstColorSpace    Destination color space identifier.
 * @param srcOffset        Top-left pixel offset in the source buffer.
 * @param dstOffset        Top-left pixel offset in the destination buffer.
 * @param width            Number of pixels to convert per row.
 * @param height           Number of rows to convert.
 * @param srcSwap          True if source channel bytes are swapped (big-endian 16-bit).
 * @param dstSwap          True if destination channel bytes should be swapped.
 * @param srcFunc          Optional read callback; NULL to read srcByte directly.
 * @param dstFunc          Optional write callback; NULL to write dstByte directly.
 * @return B_OK always (early exit on buffer exhaustion without error).
 */
template<typename srcByte, typename dstByte>
status_t
ConvertBits64To32(const srcByte *srcBits, dstByte *dstBits, int32 srcBitsLength,
	int32 dstBitsLength, int32 redShift, int32 greenShift, int32 blueShift,
	int32 alphaShift, int32 alphaBits, uint32 redMask, uint32 greenMask,
	uint32 blueMask, uint32 alphaMask, int32 srcBytesPerRow,
	int32 dstBytesPerRow, int32 srcBitsPerPixel, int32 dstBitsPerPixel,
	color_space srcColorSpace, color_space dstColorSpace, BPoint srcOffset,
	BPoint dstOffset, int32 width, int32 height, bool srcSwap, bool dstSwap,
	read64Func *srcFunc, writeFunc *dstFunc)
{
	uint8* srcBitsEnd = (uint8*)srcBits + srcBitsLength;
	uint8* dstBitsEnd = (uint8*)dstBits + dstBitsLength;

	int32 srcBitsPerRow = srcBytesPerRow << 3;
	int32 dstBitsPerRow = dstBytesPerRow << 3;

	// Advance the buffers to reach their offsets
	int32 srcOffsetX = (int32)srcOffset.x;
	int32 dstOffsetX = (int32)dstOffset.x;
	int32 srcOffsetY = (int32)srcOffset.y;
	int32 dstOffsetY = (int32)dstOffset.y;
	if (srcOffsetX < 0) {
		dstOffsetX -= srcOffsetX;
		srcOffsetX = 0;
	}
	if (srcOffsetY < 0) {
		dstOffsetY -= srcOffsetY;
		height += srcOffsetY;
		srcOffsetY = 0;
	}
	if (dstOffsetX < 0) {
		srcOffsetX -= dstOffsetX;
		dstOffsetX = 0;
	}
	if (dstOffsetY < 0) {
		srcOffsetY -= dstOffsetY;
		height += dstOffsetY;
		dstOffsetY = 0;
	}

	srcBits = (srcByte*)((uint8*)srcBits + ((srcOffsetY * srcBitsPerRow
		+ srcOffsetX * srcBitsPerPixel) >> 3));
	dstBits = (dstByte*)((uint8*)dstBits + ((dstOffsetY * dstBitsPerRow
		+ dstOffsetX * dstBitsPerPixel) >> 3));

	// Ensure that the width fits
	int32 srcWidth = (srcBitsPerRow - srcOffsetX * srcBitsPerPixel)
		/ srcBitsPerPixel;
	if (srcWidth < width)
		width = srcWidth;

	int32 dstWidth = (dstBitsPerRow - dstOffsetX * dstBitsPerPixel)
		/ dstBitsPerPixel;
	if (dstWidth < width)
		width = dstWidth;

	if (width < 0)
		return B_OK;

	int32 srcLinePad = (srcBitsPerRow - width * srcBitsPerPixel + 7) >> 3;
	int32 dstLinePad = (dstBitsPerRow - width * dstBitsPerPixel + 7) >> 3;
	uint64 result;
	uint64 source;

	// srcSwap, means the lower bits come first
	if (srcSwap) {
		redShift -= 8;
		greenShift -= 8;
		blueShift -= 8;
		alphaShift -= 8;
	}

	for (int32 i = 0; i < height; i++) {
		for (int32 j = 0; j < width; j++) {
			if ((uint8 *)srcBits + sizeof(srcByte) > srcBitsEnd
				|| (uint8 *)dstBits + sizeof(dstByte) > dstBitsEnd)
				return B_OK;

			if (srcFunc)
				source = srcFunc((const uint16 **)&srcBits, srcOffsetX++);
			else {
				source = *srcBits;
				srcBits++;
			}

			if (redShift > 0)
				result = ((source >> redShift) & redMask);
			else if (redShift < 0)
				result = ((source << -redShift) & redMask);
			else
				result = source & redMask;

			if (greenShift > 0)
				result |= ((source >> greenShift) & greenMask);
			else if (greenShift < 0)
				result |= ((source << -greenShift) & greenMask);
			else
				result |= source & greenMask;

			if (blueShift > 0)
				result |= ((source >> blueShift) & blueMask);
			else if (blueShift < 0)
				result |= ((source << -blueShift) & blueMask);
			else
				result |= source & blueMask;

			if (alphaBits > 0) {
				if (alphaShift > 0)
					result |= ((source >> alphaShift) & alphaMask);
				else if (alphaShift < 0)
					result |= ((source << -alphaShift) & alphaMask);
				else
					result |= source & alphaMask;

				// if we only had one alpha bit we want it to be 0/255
				if (alphaBits == 1 && result & alphaMask)
					result |= alphaMask;
			} else
				result |= alphaMask;

			if (dstFunc)
				dstFunc((uint8 **)&dstBits, (uint8 *)&result, dstOffsetX++);
			else {
				*dstBits = result;
				dstBits++;
			}
		}

		srcBits = (srcByte*)((uint8*)srcBits + srcLinePad);
		dstBits = (dstByte*)((uint8*)dstBits + dstLinePad);
		dstOffsetX -= width;
		srcOffsetX -= width;
	}

	return B_OK;
}


/**
 * @brief Low-level pixel converter between arbitrary color spaces using 32-bit intermediates.
 *
 * The general-purpose inner loop: reads each source pixel, shifts and masks each
 * color channel into a 32-bit intermediate, then writes it to the destination.
 * When source and destination color spaces are identical and pixel-aligned the
 * function fast-paths to memcpy().  Boundary checks against \a srcBitsLength and
 * \a dstBitsLength prevent buffer overruns.
 *
 * @param srcBits          Pointer to the source pixel buffer.
 * @param dstBits          Pointer to the destination pixel buffer.
 * @param srcBitsLength    Byte length of the source buffer.
 * @param dstBitsLength    Byte length of the destination buffer.
 * @param redShift         Bit shift for the red channel (positive = right-shift source).
 * @param greenShift       Bit shift for the green channel.
 * @param blueShift        Bit shift for the blue channel.
 * @param alphaShift       Bit shift for the alpha channel.
 * @param alphaBits        Number of alpha bits in the source (0 = no alpha, fill with mask).
 * @param redMask          Destination bitmask for the red channel.
 * @param greenMask        Destination bitmask for the green channel.
 * @param blueMask         Destination bitmask for the blue channel.
 * @param alphaMask        Destination bitmask for the alpha channel.
 * @param srcBytesPerRow   Row stride of the source buffer in bytes.
 * @param dstBytesPerRow   Row stride of the destination buffer in bytes.
 * @param srcBitsPerPixel  Bits per source pixel.
 * @param dstBitsPerPixel  Bits per destination pixel.
 * @param srcColorSpace    Source color space identifier (used for same-space fast path).
 * @param dstColorSpace    Destination color space identifier.
 * @param srcOffset        Top-left pixel offset in the source buffer.
 * @param dstOffset        Top-left pixel offset in the destination buffer.
 * @param width            Number of pixels to convert per row.
 * @param height           Number of rows to convert.
 * @param srcSwap          True if source 16-bit words are byte-swapped.
 * @param dstSwap          True if destination 16-bit words should be byte-swapped.
 * @param srcFunc          Optional read callback; NULL to read srcByte directly.
 * @param dstFunc          Optional write callback; NULL to write dstByte directly.
 * @return B_OK always (early exit on buffer exhaustion without error).
 */
template<typename srcByte, typename dstByte>
status_t
ConvertBits(const srcByte *srcBits, dstByte *dstBits, int32 srcBitsLength,
	int32 dstBitsLength, int32 redShift, int32 greenShift, int32 blueShift,
	int32 alphaShift, int32 alphaBits, uint32 redMask, uint32 greenMask,
	uint32 blueMask, uint32 alphaMask, int32 srcBytesPerRow,
	int32 dstBytesPerRow, int32 srcBitsPerPixel, int32 dstBitsPerPixel,
	color_space srcColorSpace, color_space dstColorSpace, BPoint srcOffset,
	BPoint dstOffset, int32 width, int32 height, bool srcSwap, bool dstSwap,
	readFunc *srcFunc, writeFunc *dstFunc)
{
	uint8* srcBitsEnd = (uint8*)srcBits + srcBitsLength;
	uint8* dstBitsEnd = (uint8*)dstBits + dstBitsLength;

	int32 srcBitsPerRow = srcBytesPerRow << 3;
	int32 dstBitsPerRow = dstBytesPerRow << 3;

	// Advance the buffers to reach their offsets
	int32 srcOffsetX = (int32)srcOffset.x;
	int32 dstOffsetX = (int32)dstOffset.x;
	int32 srcOffsetY = (int32)srcOffset.y;
	int32 dstOffsetY = (int32)dstOffset.y;
	if (srcOffsetX < 0) {
		dstOffsetX -= srcOffsetX;
		srcOffsetX = 0;
	}
	if (srcOffsetY < 0) {
		dstOffsetY -= srcOffsetY;
		height += srcOffsetY;
		srcOffsetY = 0;
	}
	if (dstOffsetX < 0) {
		srcOffsetX -= dstOffsetX;
		dstOffsetX = 0;
	}
	if (dstOffsetY < 0) {
		srcOffsetY -= dstOffsetY;
		height += dstOffsetY;
		dstOffsetY = 0;
	}

	srcBits = (srcByte*)((uint8*)srcBits + ((srcOffsetY * srcBitsPerRow
		+ srcOffsetX * srcBitsPerPixel) >> 3));
	dstBits = (dstByte*)((uint8*)dstBits + ((dstOffsetY * dstBitsPerRow
		+ dstOffsetX * dstBitsPerPixel) >> 3));

	// Ensure that the width fits
	int32 srcWidth = (srcBitsPerRow - srcOffsetX * srcBitsPerPixel)
		/ srcBitsPerPixel;
	if (srcWidth < width)
		width = srcWidth;

	int32 dstWidth = (dstBitsPerRow - dstOffsetX * dstBitsPerPixel)
		/ dstBitsPerPixel;
	if (dstWidth < width)
		width = dstWidth;

	if (width < 0)
		return B_OK;

	// Catch the copy case
	if (srcColorSpace == dstColorSpace && srcBitsPerPixel % 8 == 0) {
		int32 copyCount = (width * srcBitsPerPixel) >> 3;
		for (int32 i = 0; i < height; i++) {
			// make sure we don't write beyond the bits size
			if (copyCount > srcBitsLength)
				copyCount = srcBitsLength;
			if (copyCount > dstBitsLength)
				copyCount = dstBitsLength;
			if (copyCount == 0)
				break;

			memcpy(dstBits, srcBits, copyCount);

			srcBitsLength -= copyCount;
			dstBitsLength -= copyCount;
			srcBits = (srcByte*)((uint8*)srcBits + srcBytesPerRow);
			dstBits = (dstByte*)((uint8*)dstBits + dstBytesPerRow);

			if ((uint8 *)srcBits > srcBitsEnd || (uint8 *)dstBits > dstBitsEnd)
				return B_OK;
		}

		return B_OK;
	}

	int32 srcLinePad = (srcBitsPerRow - width * srcBitsPerPixel + 7) >> 3;
	int32 dstLinePad = (dstBitsPerRow - width * dstBitsPerPixel + 7) >> 3;
	uint32 result;
	uint32 source;

	for (int32 i = 0; i < height; i++) {
		for (int32 j = 0; j < width; j++) {
			if ((uint8 *)srcBits + sizeof(srcByte) > srcBitsEnd
				|| (uint8 *)dstBits + sizeof(dstByte) > dstBitsEnd)
				return B_OK;

			if (srcFunc)
				source = srcFunc((const uint8 **)&srcBits, srcOffsetX++);
			else {
				source = *srcBits;
				srcBits++;
			}

			// This is valid, as only 16 bit modes will need to swap
			if (srcSwap)
				source = (source << 8) | (source >> 8);

			if (redShift > 0)
				result = ((source >> redShift) & redMask);
			else if (redShift < 0)
				result = ((source << -redShift) & redMask);
			else
				result = source & redMask;

			if (greenShift > 0)
				result |= ((source >> greenShift) & greenMask);
			else if (greenShift < 0)
				result |= ((source << -greenShift) & greenMask);
			else
				result |= source & greenMask;

			if (blueShift > 0)
				result |= ((source >> blueShift) & blueMask);
			else if (blueShift < 0)
				result |= ((source << -blueShift) & blueMask);
			else
				result |= source & blueMask;

			if (alphaBits > 0) {
				if (alphaShift > 0)
					result |= ((source >> alphaShift) & alphaMask);
				else if (alphaShift < 0)
					result |= ((source << -alphaShift) & alphaMask);
				else
					result |= source & alphaMask;

				// if we only had one alpha bit we want it to be 0/255
				if (alphaBits == 1 && result & alphaMask)
					result |= alphaMask;
			} else
				result |= alphaMask;

			// This is valid, as only 16 bit modes will need to swap
			if (dstSwap)
				result = (result << 8) | (result >> 8);

			if (dstFunc)
				dstFunc((uint8 **)&dstBits, (uint8 *)&result, dstOffsetX++);
			else {
				*dstBits = result;
				dstBits++;
			}
		}

		srcBits = (srcByte*)((uint8*)srcBits + srcLinePad);
		dstBits = (dstByte*)((uint8*)dstBits + dstLinePad);
		dstOffsetX -= width;
		srcOffsetX -= width;
	}

	return B_OK;
}


/**
 * @brief Dispatch a 64-bit source conversion to the appropriate 32-bit destination handler.
 *
 * Selects the correct ConvertBits64To32() instantiation based on \a dstColorSpace,
 * adjusting per-channel shifts and masks to match the target layout.
 *
 * @param srcBits         Pointer to the source pixel buffer.
 * @param dstBits         Pointer to the destination pixel buffer (untyped).
 * @param srcBitsLength   Byte length of the source buffer.
 * @param dstBitsLength   Byte length of the destination buffer.
 * @param redShift        Source bit position of the red channel MSB.
 * @param greenShift      Source bit position of the green channel MSB.
 * @param blueShift       Source bit position of the blue channel MSB.
 * @param alphaShift      Source bit position of the alpha channel MSB.
 * @param alphaBits       Number of alpha bits in the source format.
 * @param srcBytesPerRow  Row stride of the source buffer in bytes.
 * @param dstBytesPerRow  Row stride of the destination buffer in bytes.
 * @param srcBitsPerPixel Bits per source pixel.
 * @param srcColorSpace   Source color space identifier.
 * @param dstColorSpace   Target color space identifier.
 * @param srcOffset       Top-left pixel offset in the source buffer.
 * @param dstOffset       Top-left pixel offset in the destination buffer.
 * @param width           Number of pixels per row to convert.
 * @param height          Number of rows to convert.
 * @param srcSwap         True if source channel bytes are swapped.
 * @param srcFunc         Optional 64-bit read callback; NULL to read srcByte directly.
 * @return B_OK on success.
 * @retval B_BAD_VALUE \a dstColorSpace is not a supported 32-bit destination space.
 */
template<typename srcByte>
status_t
ConvertBits64(const srcByte *srcBits, void *dstBits, int32 srcBitsLength,
	int32 dstBitsLength, int32 redShift, int32 greenShift, int32 blueShift,
	int32 alphaShift, int32 alphaBits, int32 srcBytesPerRow,
	int32 dstBytesPerRow, int32 srcBitsPerPixel, color_space srcColorSpace,
	color_space dstColorSpace, BPoint srcOffset, BPoint dstOffset, int32 width,
	int32 height, bool srcSwap,	read64Func *srcFunc)
{
	switch (dstColorSpace) {
		case B_RGBA32:
			ConvertBits64To32(srcBits, (uint32 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 24, greenShift - 16, blueShift - 8,
				alphaShift - 32, alphaBits, 0x00ff0000, 0x0000ff00, 0x000000ff,
				0xff000000, srcBytesPerRow, dstBytesPerRow, srcBitsPerPixel,
				32, srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, srcSwap, false, srcFunc, NULL);
			break;

		case B_RGBA32_BIG:
			ConvertBits64To32(srcBits, (uint32 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 16, greenShift - 24, blueShift - 32,
				alphaShift - 8, alphaBits, 0x0000ff00, 0x00ff0000, 0xff000000,
				0x00000ff, srcBytesPerRow, dstBytesPerRow, srcBitsPerPixel, 32,
				srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, srcSwap, false, srcFunc, NULL);
			break;

		/* Note:	we set the unused alpha to 255 here. This is because BeOS
					uses the unused alpha for B_OP_ALPHA even though it should
					not care about it. */
		case B_RGB32:
			ConvertBits64To32(srcBits, (uint32 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 24, greenShift - 32, blueShift - 16,
				0, 0, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000,
				srcBytesPerRow, dstBytesPerRow, srcBitsPerPixel, 32,
				srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, srcSwap, false, srcFunc, NULL);
			break;

		case B_RGB32_BIG:
			ConvertBits64To32(srcBits, (uint32 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 16, greenShift - 24, blueShift - 32,
				0, 0, 0x0000ff00, 0x00ff0000, 0xff000000, 0x000000ff,
				srcBytesPerRow, dstBytesPerRow, srcBitsPerPixel, 32,
				srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, srcSwap, false, srcFunc, NULL);
			break;

		default:
			return B_BAD_VALUE;
			break;
	}

	return B_OK;
}


/**
 * @brief Dispatch a source-typed conversion to the correct typed destination handler.
 *
 * Selects the appropriate ConvertBits() overload based on \a dstColorSpace,
 * adjusting per-channel shifts and masks for the target layout and invoking the
 * relevant write callback (e.g. WriteRGB24, WriteGray8, WriteCMAP8).
 *
 * @param srcBits         Pointer to the source pixel buffer.
 * @param dstBits         Pointer to the destination pixel buffer (untyped).
 * @param srcBitsLength   Byte length of the source buffer.
 * @param dstBitsLength   Byte length of the destination buffer.
 * @param redShift        Source bit position of the red channel MSB.
 * @param greenShift      Source bit position of the green channel MSB.
 * @param blueShift       Source bit position of the blue channel MSB.
 * @param alphaShift      Source bit position of the alpha channel MSB.
 * @param alphaBits       Number of alpha bits in the source format.
 * @param srcBytesPerRow  Row stride of the source buffer in bytes.
 * @param dstBytesPerRow  Row stride of the destination buffer in bytes.
 * @param srcBitsPerPixel Bits per source pixel.
 * @param srcColorSpace   Source color space identifier.
 * @param dstColorSpace   Target color space identifier.
 * @param srcOffset       Top-left pixel offset in the source buffer.
 * @param dstOffset       Top-left pixel offset in the destination buffer.
 * @param width           Number of pixels per row to convert.
 * @param height          Number of rows to convert.
 * @param srcSwap         True if source 16-bit words are byte-swapped.
 * @param srcFunc         Optional 32-bit read callback; NULL to read srcByte directly.
 * @return B_OK on success.
 * @retval B_BAD_VALUE \a dstColorSpace is not a supported destination color space.
 */
template<typename srcByte>
status_t
ConvertBits(const srcByte *srcBits, void *dstBits, int32 srcBitsLength,
	int32 dstBitsLength, int32 redShift, int32 greenShift, int32 blueShift,
	int32 alphaShift, int32 alphaBits, int32 srcBytesPerRow,
	int32 dstBytesPerRow, int32 srcBitsPerPixel, color_space srcColorSpace,
	color_space dstColorSpace, BPoint srcOffset, BPoint dstOffset, int32 width,
	int32 height, bool srcSwap,	readFunc *srcFunc)
{
	switch (dstColorSpace) {
		case B_RGBA32:
			ConvertBits(srcBits, (uint32 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 24, greenShift - 16, blueShift - 8,
				alphaShift - 32, alphaBits, 0x00ff0000, 0x0000ff00, 0x000000ff,
				0xff000000, srcBytesPerRow, dstBytesPerRow, srcBitsPerPixel,
				32, srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, srcSwap, false, srcFunc, NULL);
			break;

		case B_RGBA32_BIG:
			ConvertBits(srcBits, (uint32 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 16, greenShift - 24, blueShift - 32,
				alphaShift - 8, alphaBits, 0x0000ff00, 0x00ff0000, 0xff000000,
				0x00000ff, srcBytesPerRow, dstBytesPerRow, srcBitsPerPixel, 32,
				srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, srcSwap, false, srcFunc, NULL);
			break;

		/* Note:	we set the unused alpha to 255 here. This is because BeOS
					uses the unused alpha for B_OP_ALPHA even though it should
					not care about it. */
		case B_RGB32:
			ConvertBits(srcBits, (uint32 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 24, greenShift - 16, blueShift - 8,
				0, 0, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000,
				srcBytesPerRow, dstBytesPerRow, srcBitsPerPixel, 32,
				srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, srcSwap, false, srcFunc, NULL);
			break;

		case B_RGB32_BIG:
			ConvertBits(srcBits, (uint32 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 16, greenShift - 24, blueShift - 32,
				0, 0, 0x0000ff00, 0x00ff0000, 0xff000000, 0x000000ff,
				srcBytesPerRow, dstBytesPerRow, srcBitsPerPixel, 32,
				srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, srcSwap, false, srcFunc, NULL);
			break;

		case B_RGB24:
			ConvertBits(srcBits, (uint8 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 24, greenShift - 16, blueShift - 8,
				0, 0, 0xff0000, 0x00ff00, 0x0000ff, 0x000000, srcBytesPerRow,
				dstBytesPerRow, srcBitsPerPixel, 24, srcColorSpace,
				dstColorSpace, srcOffset, dstOffset, width, height, srcSwap,
				false, srcFunc, WriteRGB24);
			break;

		case B_RGB24_BIG:
			ConvertBits(srcBits, (uint8 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 8, greenShift - 16, blueShift - 24,
				0, 0, 0x0000ff, 0x00ff00, 0xff0000, 0x000000, srcBytesPerRow,
				dstBytesPerRow, srcBitsPerPixel, 24, srcColorSpace,
				dstColorSpace, srcOffset, dstOffset, width, height, srcSwap,
				false, srcFunc, WriteRGB24);
			break;

		case B_RGB16:
		case B_RGB16_BIG:
			ConvertBits(srcBits, (uint16 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 16, greenShift - 11, blueShift - 5,
				0, 0, 0xf800, 0x07e0, 0x001f, 0x0000, srcBytesPerRow,
				dstBytesPerRow, srcBitsPerPixel, 16, srcColorSpace,
				dstColorSpace, srcOffset, dstOffset, width, height, srcSwap,
				dstColorSpace == B_RGB16_BIG, srcFunc, NULL);
			break;

		case B_RGBA15:
		case B_RGBA15_BIG:
			ConvertBits(srcBits, (uint16 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 15, greenShift - 10, blueShift - 5,
				alphaShift - 16, alphaBits, 0x7c00, 0x03e0, 0x001f, 0x8000,
				srcBytesPerRow, dstBytesPerRow, srcBitsPerPixel, 16,
				srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, srcSwap, dstColorSpace == B_RGBA15_BIG, srcFunc, NULL);
			break;

		case B_RGB15:
		case B_RGB15_BIG:
			ConvertBits(srcBits, (uint16 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 15, greenShift - 10, blueShift - 5,
				0, 0, 0x7c00, 0x03e0, 0x001f, 0x0000, srcBytesPerRow,
				dstBytesPerRow, srcBitsPerPixel, 16, srcColorSpace,
				dstColorSpace, srcOffset, dstOffset, width, height, srcSwap,
				dstColorSpace == B_RGB15_BIG, srcFunc, NULL);
			break;

		case B_GRAY8:
			ConvertBits(srcBits, (uint8 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 24, greenShift - 16, blueShift - 8,
				0, 0, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000,
				srcBytesPerRow, dstBytesPerRow, srcBitsPerPixel, 8,
				srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, srcSwap, false, srcFunc, WriteGray8);
			break;

		case B_GRAY1:
			ConvertBits(srcBits, (uint8 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 24, greenShift - 16, blueShift - 8,
				0, 0, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000,
				srcBytesPerRow, dstBytesPerRow, srcBitsPerPixel, 1,
				srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, srcSwap, false, srcFunc, WriteGray1);
			break;

		case B_CMAP8:
			PaletteConverter::InitializeDefault();
			ConvertBits(srcBits, (uint8 *)dstBits, srcBitsLength,
				dstBitsLength, redShift - 32, greenShift - 24, blueShift - 16,
				alphaShift - 8, alphaBits, 0xff000000, 0x00ff0000, 0x0000ff00,
				0x000000ff, srcBytesPerRow, dstBytesPerRow, srcBitsPerPixel, 8,
				srcColorSpace, dstColorSpace, srcOffset, dstOffset,
				width, height, srcSwap, false, srcFunc, WriteCMAP8);
			break;

		default:
			return B_BAD_VALUE;
			break;
	}

	return B_OK;
}


/**
 * @brief Convert a rectangular pixel buffer between two color spaces.
 *
 * Convenience overload that starts conversion at pixel (0, 0) in both buffers.
 * Delegates to the full-offset overload with BPoint(0, 0) for both offsets.
 *
 * @param srcBits        Pointer to the raw source pixel data.
 * @param dstBits        Pointer to the raw destination pixel buffer.
 * @param srcBitsLength  Byte length of the source buffer.
 * @param dstBitsLength  Byte length of the destination buffer.
 * @param srcBytesPerRow Row stride of the source buffer in bytes.
 * @param dstBytesPerRow Row stride of the destination buffer in bytes.
 * @param srcColorSpace  Color space of the source data.
 * @param dstColorSpace  Desired color space of the destination data.
 * @param width          Number of pixels per row to convert.
 * @param height         Number of rows to convert.
 * @return B_OK on success.
 * @retval B_BAD_VALUE A NULL pointer, negative dimension, or unsupported color space
 *                     was supplied.
 * @see ConvertBits(const void*, void*, int32, int32, int32, int32, color_space,
 *      color_space, BPoint, BPoint, int32, int32)
 */
status_t
ConvertBits(const void *srcBits, void *dstBits, int32 srcBitsLength,
	int32 dstBitsLength, int32 srcBytesPerRow, int32 dstBytesPerRow,
	color_space srcColorSpace, color_space dstColorSpace, int32 width,
	int32 height)
{
	return ConvertBits(srcBits, dstBits, srcBitsLength, dstBitsLength,
		srcBytesPerRow, dstBytesPerRow, srcColorSpace, dstColorSpace,
		BPoint(0, 0), BPoint(0, 0), width, height);
}


/**
 * @brief Convert a sub-region of a pixel buffer between two color spaces.
 *
 * Full-featured entry point.  Validates all arguments, then dispatches to the
 * appropriate typed template based on \a srcColorSpace.  Supports all color
 * spaces enumerated in color_space, including packed formats (B_RGB24,
 * B_GRAY1, B_CMAP8) via per-pixel read/write callbacks.
 *
 * @param srcBits        Pointer to the raw source pixel data.
 * @param dstBits        Pointer to the raw destination pixel buffer.
 * @param srcBitsLength  Byte length of the source buffer.
 * @param dstBitsLength  Byte length of the destination buffer.
 * @param srcBytesPerRow Row stride of the source buffer in bytes.
 * @param dstBytesPerRow Row stride of the destination buffer in bytes.
 * @param srcColorSpace  Color space of the source data.
 * @param dstColorSpace  Desired color space of the destination data.
 * @param srcOffset      First pixel to read in the source buffer (may be negative to
 *                       skip leading destination pixels instead).
 * @param dstOffset      First pixel to write in the destination buffer.
 * @param width          Number of pixels per row to convert.
 * @param height         Number of rows to convert.
 * @return B_OK on success.
 * @retval B_BAD_VALUE A NULL pointer, a negative dimension or stride, or an
 *                     unsupported source/destination color space was supplied.
 */
status_t
ConvertBits(const void *srcBits, void *dstBits, int32 srcBitsLength,
	int32 dstBitsLength, int32 srcBytesPerRow, int32 dstBytesPerRow,
	color_space srcColorSpace, color_space dstColorSpace, BPoint srcOffset,
	BPoint dstOffset, int32 width, int32 height)
{
	if (!srcBits || !dstBits || srcBitsLength < 0 || dstBitsLength < 0
		|| width < 0 || height < 0 || srcBytesPerRow < 0 || dstBytesPerRow < 0)
		return B_BAD_VALUE;

	switch (srcColorSpace) {
		case B_RGBA64:
		case B_RGBA64_BIG:
			return ConvertBits64((const uint64 *)srcBits, dstBits,
				srcBitsLength, dstBitsLength, 16, 32, 48, 64, 16,
				srcBytesPerRow, dstBytesPerRow, 64, srcColorSpace,
				dstColorSpace, srcOffset, dstOffset, width, height,
				srcColorSpace == B_RGBA64_BIG, NULL);

		case B_RGB48:
		case B_RGB48_BIG:
			return ConvertBits64((const uint16 *)srcBits, dstBits,
				srcBitsLength, dstBitsLength, 16, 32, 48, 0, 0, srcBytesPerRow,
				dstBytesPerRow, 48, srcColorSpace, dstColorSpace, srcOffset,
				dstOffset, width, height, srcColorSpace == B_RGB48_BIG,
				ReadRGB48);

		case B_RGBA32:
			return ConvertBits((const uint32 *)srcBits, dstBits, srcBitsLength,
				dstBitsLength, 24, 16, 8, 32, 8, srcBytesPerRow,
				dstBytesPerRow, 32, srcColorSpace, dstColorSpace, srcOffset,
				dstOffset, width, height, false, NULL);

		case B_RGBA32_BIG:
			return ConvertBits((const uint32 *)srcBits, dstBits, srcBitsLength,
				dstBitsLength, 16, 24, 32, 8, 8, srcBytesPerRow,
				dstBytesPerRow, 32, srcColorSpace, dstColorSpace, srcOffset,
				dstOffset, width, height, false, NULL);

		case B_RGB32:
			return ConvertBits((const uint32 *)srcBits, dstBits, srcBitsLength,
				dstBitsLength, 24, 16, 8, 0, 0, srcBytesPerRow, dstBytesPerRow,
				32, srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, false, NULL);

		case B_RGB32_BIG:
			return ConvertBits((const uint32 *)srcBits, dstBits, srcBitsLength,
				dstBitsLength, 16, 24, 32, 0, 0, srcBytesPerRow,
				dstBytesPerRow, 32, srcColorSpace, dstColorSpace, srcOffset,
				dstOffset, width, height, false, NULL);

		case B_RGB24:
			return ConvertBits((const uint8 *)srcBits, dstBits, srcBitsLength,
				dstBitsLength, 24, 16, 8, 0, 0, srcBytesPerRow, dstBytesPerRow,
				24, srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, false, ReadRGB24);

		case B_RGB24_BIG:
			return ConvertBits((const uint8 *)srcBits, dstBits, srcBitsLength,
				dstBitsLength, 8, 16, 24, 0, 0, srcBytesPerRow, dstBytesPerRow,
				24, srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, false, ReadRGB24);

		case B_RGB16:
		case B_RGB16_BIG:
			return ConvertBits((const uint16 *)srcBits, dstBits, srcBitsLength,
				dstBitsLength, 16, 11, 5, 0, 0, srcBytesPerRow, dstBytesPerRow,
				16, srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, srcColorSpace == B_RGB16_BIG, NULL);

		case B_RGBA15:
		case B_RGBA15_BIG:
			return ConvertBits((const uint16 *)srcBits, dstBits, srcBitsLength,
				dstBitsLength, 15, 10, 5, 16, 1, srcBytesPerRow,
				dstBytesPerRow, 16, srcColorSpace, dstColorSpace, srcOffset,
				dstOffset, width, height, srcColorSpace == B_RGBA15_BIG, NULL);

		case B_RGB15:
		case B_RGB15_BIG:
			return ConvertBits((const uint16 *)srcBits, dstBits, srcBitsLength,
				dstBitsLength, 15, 10, 5, 0, 0, srcBytesPerRow, dstBytesPerRow,
				16, srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, srcColorSpace == B_RGB15_BIG, NULL);

		case B_GRAY8:
			return ConvertBits((const uint8 *)srcBits, dstBits, srcBitsLength,
				dstBitsLength, 8, 8, 8, 0, 0, srcBytesPerRow, dstBytesPerRow,
				8, srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, false, ReadGray8);

		case B_GRAY1:
			return ConvertBits((const uint8 *)srcBits, dstBits, srcBitsLength,
				dstBitsLength, 8, 8, 8, 0, 0, srcBytesPerRow, dstBytesPerRow,
				1, srcColorSpace, dstColorSpace, srcOffset, dstOffset, width,
				height, false, ReadGray1);

		case B_CMAP8:
			PaletteConverter::InitializeDefault();
			return ConvertBits((const uint8 *)srcBits, dstBits, srcBitsLength,
				dstBitsLength, 24, 16, 8, 32, 8, srcBytesPerRow,
				dstBytesPerRow, 8, srcColorSpace, dstColorSpace, srcOffset,
				dstOffset, width, height, false, ReadCMAP8);

		default:
			return B_BAD_VALUE;
	}

	return B_OK;
}

} // namespace BPrivate
