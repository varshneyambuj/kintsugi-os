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
 *   Copyright 2001-2006, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Stefano Ceccherini (burton666@libero.it)
 */

/** @file SystemPalette.cpp
 *  @brief Initialization and access functions for the system 8-bit color palette. */

//! Methods to initialize and get the system color_map.


#include "SystemPalette.h"

#include <stdio.h>
#include <string.h>

#include <Palette.h>

// TODO: BWindowScreen has a method to set the palette.
// maybe we should have a lock to protect this variable.
static color_map sColorMap;


/**
 * @brief Returns the "distance" between two RGB colors.
 *
 * Defines a perceptual metric on the RGB color space using psycho-visual
 * weights. The distance is 0 if and only if the two colors are identical.
 *
 * @param red1   Red component of the first color.
 * @param green1 Green component of the first color.
 * @param blue1  Blue component of the first color.
 * @param red2   Red component of the second color.
 * @param green2 Green component of the second color.
 * @param blue2  Blue component of the second color.
 * @return Perceptual distance between the two colors (larger = more different).
 */
static inline uint32
color_distance(uint8 red1, uint8 green1, uint8 blue1,
			   uint8 red2, uint8 green2, uint8 blue2)
{
	int rd = (int)red1 - (int)red2;
	int gd = (int)green1 - (int)green2;
	int bd = (int)blue1 - (int)blue2;

	// distance according to psycho-visual tests
	// algorithm taken from here:
	// http://www.stud.uni-hannover.de/~michaelt/juggle/Algorithms.pdf
	int rmean = ((int)red1 + (int)red2) / 2;
	return (((512 + rmean) * rd * rd) >> 8)
			+ 4 * gd * gd
			+ (((767 - rmean) * bd * bd) >> 8);
}


/**
 * @brief Finds the palette index whose color is closest to @a color.
 * @param color   The target RGB color.
 * @param palette Array of 256 rgb_color entries to search.
 * @return The palette index (0–255) of the closest matching color.
 */
static inline uint8
FindClosestColor(const rgb_color &color, const rgb_color *palette)
{
	uint8 closestIndex = 0;
	unsigned closestDistance = UINT_MAX;
	for (int32 i = 0; i < 256; i++) {
		const rgb_color &c = palette[i];
		unsigned distance = color_distance(color.red, color.green, color.blue,
										   c.red, c.green, c.blue);
		if (distance < closestDistance) {
			closestIndex = (uint8)i;
			closestDistance = distance;
		}
	}
	return closestIndex;
}


/**
 * @brief Returns the bitwise inverse of @a color.
 *
 * (255, 255, 255) is treated as a special case and returned unchanged, matching
 * original BeOS behavior.
 *
 * @param color The source RGB color.
 * @return The inverted color.
 */
static inline rgb_color
InvertColor(const rgb_color &color)
{
	// For some reason, Inverting (255, 255, 255) on beos
	// results in the same color.
	if (color.red == 255 && color.green == 255
		&& color.blue == 255)
		return color;

	rgb_color inverted;
	inverted.red = 255 - color.red;
	inverted.green = 255 - color.green;
	inverted.blue = 255 - color.blue;
	inverted.alpha = 255;

	return inverted;
}


/**
 * @brief Fills @a map's color list, index map, and inversion map from @a palette.
 *
 * The index_map is a 32768-entry table mapping 15-bit (5-5-5) RGB values to
 * the closest palette index. The inversion_map maps each palette entry to the
 * closest entry to its inverted color.
 *
 * @param palette Array of 256 rgb_color entries.
 * @param map     Output color_map structure to populate.
 */
static void
FillColorMap(const rgb_color *palette, color_map *map)
{
	memcpy((void*)map->color_list, palette, sizeof(map->color_list));

	// init index map
	for (int32 color = 0; color < 32768; color++) {
		// get components
		rgb_color rgbColor;
		rgbColor.red = (color & 0x7c00) >> 7;
		rgbColor.green = (color & 0x3e0) >> 2;
		rgbColor.blue = (color & 0x1f) << 3;

		map->index_map[color] = FindClosestColor(rgbColor, palette);
	}

	// init inversion map
	for (int32 index = 0; index < 256; index++) {
		rgb_color inverted = InvertColor(map->color_list[index]);
		map->inversion_map[index] = FindClosestColor(inverted, palette);
	}
}


/**
 * @brief Initializes the system color_map from the built-in system palette.
 *
 * Must be called once during system startup before any 8-bit color operations.
 */
void
InitializeColorMap()
{
	FillColorMap(kSystemPalette, &sColorMap);
}


/**
 * @brief Returns a pointer to the 256-entry system palette.
 * @return Pointer to the system palette's rgb_color array.
 */
const rgb_color *
SystemPalette()
{
	return sColorMap.color_list;
}


/**
 * @brief Returns a pointer to the system color_map structure.
 * @return Pointer to the global color_map including index and inversion maps.
 */
const color_map *
SystemColorMap()
{
	return &sColorMap;
}
