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
 */

/** @file RGBColor.cpp
    @brief Color class providing 32-bit RGBA storage with lazy conversion to 8-, 15-, and 16-bit formats. */

#include "RGBColor.h"
#include "SystemPalette.h"

#include <stdio.h>
#include <stdlib.h>


/*!
	\brief An approximation of 31/255, which is needed for converting from 32-bit
		colors to 16-bit and 15-bit.
*/
#define RATIO_8_TO_5_BIT .121568627451

/*!
	\brief An approximation of 63/255, which is needed for converting from 32-bit
		colors to 16-bit.
*/
#define RATIO_8_TO_6_BIT .247058823529

/*!
	\brief An approximation of 255/31, which is needed for converting from 16-bit
		and 15-bit colors to 32-bit.
*/
#define RATIO_5_TO_8_BIT 8.22580645161

/*!
	\brief An approximation of 255/63, which is needed for converting from 16-bit
		colors to 32-bit.
*/
#define RATIO_6_TO_8_BIT 4.04761904762

#if 0
/*!
	\brief Function for easy conversion of 16-bit colors to 32-bit
	\param col Pointer to an rgb_color.
	\param color RGB16 color

	This function will do nothing if passed a NULL 32-bit color.
*/
void
SetRGBColor16(rgb_color *col,uint16 color)
{
	if(!col)
		return;

	uint16 r16,g16,b16;

	// alpha's the easy part
	col->alpha=0;

	r16= (color >> 11) & 31;
	g16= (color >> 5) & 63;
	b16= color & 31;

	col->red=uint8(r16 * RATIO_5_TO_8_BIT);
	col->green=uint8(g16 * RATIO_6_TO_8_BIT);
	col->blue=uint8(b16 * RATIO_5_TO_8_BIT);
}
#endif

/** @brief Finds the index of the closest matching color in a 256-entry rgb_color palette.
    @param palette Array of 256 rgb_color objects to search.
    @param color   The target color to match.
    @return Index of the closest matching palette entry, or 0 if palette is NULL. */
static uint8
FindClosestColor(const rgb_color *palette, rgb_color color)
{
	if (!palette)
		return 0;

	uint16 cindex = 0, cdelta = 765, delta = 765;

	for (uint16 i = 0; i < 256; i++) {
		const rgb_color *c = &(palette[i]);
		delta = abs(c->red-color.red) + abs(c->green-color.green)
			+ abs(c->blue-color.blue);

		if (delta == 0) {
			cindex = i;
			break;
		}

		if (delta < cdelta) {
			cindex = i;
			cdelta = delta;
		}
	}

	return (uint8)cindex;
}


/** @brief Constructs the closest RGBA15 (1:5:5:5) representation of a 32-bit color.
    @param color The source 32-bit color.
    @return A 16-bit value in ARGB 1:5:5:5 format. */
static uint16
FindClosestColor15(rgb_color color)
{
	uint16 r16 = uint16(color.red * RATIO_8_TO_5_BIT);
	uint16 g16 = uint16(color.green * RATIO_8_TO_5_BIT);
	uint16 b16 = uint16(color.blue * RATIO_8_TO_5_BIT);

	// start with alpha value
	uint16 color16 = color.alpha > 127 ? 0x8000 : 0;

	color16 |= r16 << 10;
	color16 |= g16 << 5;
	color16 |= b16;

	return color16;
}


/** @brief Constructs the closest RGB16 (5:6:5) representation of a 32-bit color.
    @param color The source 32-bit color.
    @return A 16-bit value in RGB 5:6:5 format. */
static uint16
FindClosestColor16(rgb_color color)
{
	uint16 r16 = uint16(color.red * RATIO_8_TO_5_BIT);
	uint16 g16 = uint16(color.green * RATIO_8_TO_6_BIT);
	uint16 b16 = uint16(color.blue * RATIO_8_TO_5_BIT);

	uint16 color16 = r16 << 11;
	color16 |= g16 << 5;
	color16 |= b16;

	return color16;
}


//	#pragma mark -


/** @brief Constructs an RGBColor from individual uint8 component values.
    @param r Red component (0–255).
    @param g Green component (0–255).
    @param b Blue component (0–255).
    @param a Alpha component (0–255), defaults to 255. */
RGBColor::RGBColor(uint8 r, uint8 g, uint8 b, uint8 a)
{
	SetColor(r,g,b,a);
}


/** @brief Constructs an RGBColor from individual int component values.
    @param r Red component.
    @param g Green component.
    @param b Blue component.
    @param a Alpha component, defaults to 255. */
RGBColor::RGBColor(int r, int g, int b, int a)
{
	SetColor(r, g, b, a);
}


/** @brief Constructs an RGBColor from an existing rgb_color struct.
    @param color The rgb_color to initialise from. */
RGBColor::RGBColor(const rgb_color &color)
{
	SetColor(color);
}

#if 0
/*!
	\brief Create an RGBColor from a 16-bit RGBA color
	\param color color to initialize from
*/
RGBColor::RGBColor(uint16 color)
{
	SetColor(color);
}
#endif

/** @brief Constructs an RGBColor from a system-palette index.
    @param color Index into the 256-entry system palette. */
RGBColor::RGBColor(uint8 color)
{
	SetColor(color);
}


/** @brief Copy constructor.
    @param color The RGBColor to copy from. */
RGBColor::RGBColor(const RGBColor &color)
{
	fColor32 = color.fColor32;
	fColor16 = color.fColor16;
	fColor15 = color.fColor15;
	fColor8 = color.fColor8;
	fUpdate8 = color.fUpdate8;
	fUpdate15 = color.fUpdate15;
	fUpdate16 = color.fUpdate16;
}


/** @brief Default constructor. Initialises the color to (0, 0, 0, 0). */
RGBColor::RGBColor()
{
	SetColor(0, 0, 0, 0);
}


/** @brief Returns the closest 8-bit palette index for the current color.
    @return A palette index in the range [0, 255]. */
uint8
RGBColor::GetColor8() const
{
	if (fUpdate8) {
		fColor8 = FindClosestColor(SystemPalette(), fColor32);
		fUpdate8 = false;
	}

	return fColor8;
}


/** @brief Returns the closest 15-bit (ARGB 1:5:5:5) representation of the current color.
    @return A 16-bit value encoding the color in ARGB 1:5:5:5 format. */
uint16
RGBColor::GetColor15() const
{
	if (fUpdate15) {
		fColor15 = FindClosestColor15(fColor32);
		fUpdate15 = false;
	}

	return fColor15;
}


/** @brief Returns the closest 16-bit (RGB 5:6:5) representation of the current color.
    @return A 16-bit value encoding the color in RGB 5:6:5 format. */
uint16
RGBColor::GetColor16() const
{
	if (fUpdate16) {
		fColor16 = FindClosestColor16(fColor32);
		fUpdate16 = false;
	}

	return fColor16;
}


/** @brief Returns the color as a 32-bit RGBA value.
    @return The color as an rgb_color struct including alpha. */
rgb_color
RGBColor::GetColor32() const
{
	return fColor32;
}


/** @brief Sets the color from individual uint8 component values.
    @param r Red component (0–255).
    @param g Green component (0–255).
    @param b Blue component (0–255).
    @param a Alpha component (0–255). */
void
RGBColor::SetColor(uint8 r, uint8 g, uint8 b, uint8 a)
{
	fColor32.red = r;
	fColor32.green = g;
	fColor32.blue = b;
	fColor32.alpha = a;

	fUpdate8 = fUpdate15 = fUpdate16 = true;
}


/** @brief Sets the color from individual int component values.
    @param r Red component.
    @param g Green component.
    @param b Blue component.
    @param a Alpha component. */
void
RGBColor::SetColor(int r, int g, int b, int a)
{
	fColor32.red = (uint8)r;
	fColor32.green = (uint8)g;
	fColor32.blue = (uint8)b;
	fColor32.alpha = (uint8)a;

	fUpdate8 = fUpdate15 = fUpdate16 = true;
}

#if 0
/*!
	\brief Set the object to specified value
	\param col16 color to copy
*/
void
RGBColor::SetColor(uint16 col16)
{
	fColor16 = col16;
	SetRGBColor(&fColor32, col16);

	fUpdate8 = true;
	fUpdate15 = true;
	fUpdate16 = false;
}
#endif


/** @brief Sets the color from a system-palette index.
    @param col8 Index into the 256-entry system palette. */
void
RGBColor::SetColor(uint8 col8)
{
	fColor8 = col8;
	fColor32 = SystemPalette()[col8];

	fUpdate8 = false;
	fUpdate15 = true;
	fUpdate16 = true;
}


/** @brief Sets the color from an rgb_color struct.
    @param color The source rgb_color. */
void
RGBColor::SetColor(const rgb_color &color)
{
	fColor32 = color;
	fUpdate8 = fUpdate15 = fUpdate16 = true;
}


/** @brief Sets the color by copying all cached values from another RGBColor.
    @param color The source RGBColor to copy from. */
void
RGBColor::SetColor(const RGBColor &color)
{
	fColor32 = color.fColor32;
	fColor16 = color.fColor16;
	fColor15 = color.fColor15;
	fColor8 = color.fColor8;
	fUpdate8 = color.fUpdate8;
	fUpdate15 = color.fUpdate15;
	fUpdate16 = color.fUpdate16;
}


/** @brief Assignment operator from another RGBColor.
    @param color The source RGBColor.
    @return A const reference to this object. */
const RGBColor&
RGBColor::operator=(const RGBColor &color)
{
	fColor32 = color.fColor32;
	fColor16 = color.fColor16;
	fColor15 = color.fColor15;
	fColor8 = color.fColor8;
	fUpdate8 = color.fUpdate8;
	fUpdate15 = color.fUpdate15;
	fUpdate16 = color.fUpdate16;

	return *this;
}


/** @brief Assignment operator from an rgb_color struct.
    @param color The source rgb_color.
    @return A const reference to this object. */
const RGBColor&
RGBColor::operator=(const rgb_color &color)
{
	fColor32 = color;
	fUpdate8 = fUpdate15 = fUpdate16 = true;

	return *this;
}


/** @brief Prints the 32-bit RGBA component values to standard output. */
void
RGBColor::PrintToStream(void) const
{
	printf("RGBColor(%u,%u,%u,%u)\n",
		fColor32.red, fColor32.green, fColor32.blue, fColor32.alpha);
}


/** @brief Compares this color to an rgb_color for exact equality.
    @param color The rgb_color to compare against.
    @return true if all RGBA components are identical. */
bool
RGBColor::operator==(const rgb_color &color) const
{
	return fColor32.red == color.red
		&& fColor32.green == color.green
		&& fColor32.blue == color.blue
		&& fColor32.alpha == color.alpha;
}


/** @brief Compares this color to another RGBColor for exact equality.
    @param color The RGBColor to compare against.
    @return true if all RGBA components are identical. */
bool
RGBColor::operator==(const RGBColor &color) const
{
	return fColor32.red == color.fColor32.red
		&& fColor32.green == color.fColor32.green
		&& fColor32.blue == color.fColor32.blue
		&& fColor32.alpha == color.fColor32.alpha;
}


/** @brief Inequality comparison operator against an rgb_color.
    @param color The rgb_color to compare against.
    @return true if any RGBA component differs. */
bool
RGBColor::operator!=(const rgb_color &color) const
{
	return fColor32.red != color.red
		|| fColor32.green != color.green
		|| fColor32.blue != color.blue
		|| fColor32.alpha != color.alpha;
}


/** @brief Inequality comparison operator against another RGBColor.
    @param color The RGBColor to compare against.
    @return true if any RGBA component differs. */
bool
RGBColor::operator!=(const RGBColor &color) const
{
	return fColor32.red != color.fColor32.red
		|| fColor32.green != color.fColor32.green
		|| fColor32.blue != color.fColor32.blue
		|| fColor32.alpha != color.fColor32.alpha;
}


/** @brief Returns whether this color is the transparent magic color (B_TRANSPARENT_COLOR).
    @return true if the color equals B_TRANSPARENT_COLOR. */
bool
RGBColor::IsTransparentMagic() const
{
	// TODO: validate this for B_CMAP8 for example
	return *this == B_TRANSPARENT_COLOR;
}
