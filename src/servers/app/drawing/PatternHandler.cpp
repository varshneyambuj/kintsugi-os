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
 *   Copyright (c) 2001-2007, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file PatternHandler.cpp
 * @brief Implementation of the 8x8 stipple pattern resolver used by the rasteriser.
 *
 * PatternHandler keeps the active pattern, the high color, the low color,
 * and a phase offset (for scrolling views). The hot lookup paths are inline
 * in the header; this file carries only the constructors / setters and the
 * non-inline coordinate-flavour overloads.
 */


#include "PatternHandler.h"

#include <stdio.h>

#include <Point.h>


/** @brief All-bits-set pattern equivalent to B_SOLID_HIGH. */
const Pattern kSolidHigh(0xFFFFFFFFFFFFFFFFLL);
/** @brief All-bits-clear pattern equivalent to B_SOLID_LOW. */
const Pattern kSolidLow((uint64)0);
/** @brief Alternating bit pattern that produces a 50% mix of the high and low colors. */
const Pattern kMixedColors(0xAAAAAAAAAAAAAAAALL);

/** @brief Default high color used by newly constructed PatternHandlers (opaque black). */
const rgb_color kBlack = (rgb_color){ 0, 0, 0, 255 };
/** @brief Default low color used by newly constructed PatternHandlers (opaque white). */
const rgb_color kWhite = (rgb_color){ 255, 255, 255, 255 };


/**
 * @brief Default constructor.
 *
 * The pattern is set to B_SOLID_HIGH, the high color to opaque black, and
 * the low color to opaque white. The phase offset is reset to (0, 0).
 */
PatternHandler::PatternHandler(void)
	: fPattern(kSolidHigh),
	  fHighColor(kBlack),
	  fLowColor(kWhite),
	  fXOffset(0),
	  fYOffset(0)
{
}


/**
 * @brief Constructs the handler with a raw 8 byte pattern array.
 *
 * @param pat Pointer to an 8 byte pattern. NULL falls back to B_SOLID_HIGH.
 *
 * @note High color is set to black, low color to white.
 */
PatternHandler::PatternHandler(const int8* pat)
	: fPattern(pat ? Pattern(pat) : Pattern(kSolidHigh)),
	  fHighColor(kBlack),
	  fLowColor(kWhite),
	  fXOffset(0),
	  fYOffset(0)
{
}


/**
 * @brief Constructs the handler with a packed 64-bit pattern value.
 *
 * @param pat Packed 64-bit value where each byte represents one pattern row.
 *
 * @note High color is set to black, low color to white.
 */
PatternHandler::PatternHandler(const uint64& pat)
	: fPattern(pat),
	  fHighColor(kBlack),
	  fLowColor(kWhite),
	  fXOffset(0),
	  fYOffset(0)
{
}


/**
 * @brief Constructs the handler from an existing Pattern object.
 *
 * @param pat Pattern to copy.
 *
 * @note High color is set to black, low color to white.
 */
PatternHandler::PatternHandler(const Pattern& pat)
	: fPattern(pat),
	  fHighColor(kBlack),
	  fLowColor(kWhite),
	  fXOffset(0),
	  fYOffset(0)
{
}


/**
 * @brief Copy constructor.
 *
 * @param other Source handler whose pattern, colors, and offsets are copied.
 */
PatternHandler::PatternHandler(const PatternHandler& other)
	: fPattern(other.fPattern),
	  fHighColor(other.fHighColor),
	  fLowColor(other.fLowColor),
	  fXOffset(other.fXOffset),
	  fYOffset(other.fYOffset)
{
}


/**
 * @brief Destructor; PatternHandler owns no external resources.
 */
PatternHandler::~PatternHandler(void)
{
}


/**
 * @brief Replaces the active pattern from a raw 8 byte array.
 *
 * @param pat Pointer to an 8 byte pattern. NULL resets to B_SOLID_HIGH.
 */
void
PatternHandler::SetPattern(const int8* pat)
{
	if (pat)
		fPattern.Set(pat);
	else
		fPattern = kSolidHigh;
}


/**
 * @brief Replaces the active pattern from a packed 64-bit value.
 *
 * @param pat Packed 64-bit value where each byte represents one pattern row.
 */
void
PatternHandler::SetPattern(const uint64& pat)
{
	fPattern = pat;
}


/**
 * @brief Replaces the active pattern from a Pattern object.
 *
 * @param pat Source Pattern.
 */
void
PatternHandler::SetPattern(const Pattern& pat)
{
	fPattern = pat;
}


/**
 * @brief Replaces the active pattern from a BeOS R5-style pattern struct.
 *
 * @param pat Source pattern.
 */
void
PatternHandler::SetPattern(const pattern& pat)
{
	fPattern = pat;
}


/**
 * @brief Sets both high and low colors used by the pattern.
 *
 * @param high High color (drawn where the pattern bit is set).
 * @param low  Low color (drawn where the pattern bit is clear).
 */
void
PatternHandler::SetColors(const rgb_color& high, const rgb_color& low)
{
	fHighColor = high;
	fLowColor = low;
}


/**
 * @brief Sets only the high color.
 *
 * @param color High color drawn where the pattern bit is set.
 */
void
PatternHandler::SetHighColor(const rgb_color& color)
{
	fHighColor = color;
}


/**
 * @brief Sets only the low color.
 *
 * @param color Low color drawn where the pattern bit is clear.
 */
void
PatternHandler::SetLowColor(const rgb_color& color)
{
	fLowColor = color;
}


/**
 * @brief Resolves the pattern color at a BPoint.
 *
 * @param pt Coordinates to evaluate.
 * @return   The pattern color at @a pt (HighColor() or LowColor()).
 */
rgb_color
PatternHandler::ColorAt(const BPoint &pt) const
{
	return ColorAt(pt.x, pt.y);
}


/**
 * @brief Resolves the pattern color at a floating point coordinate.
 *
 * @param x X coordinate.
 * @param y Y coordinate.
 * @return  The pattern color at (@a x, @a y) after truncating to integer pixels.
 */
rgb_color
PatternHandler::ColorAt(float x, float y) const
{
	return ColorAt(int(x), int(y));
}


/**
 * @brief Returns true when the pattern bit at @a pt selects the high color.
 *
 * @param pt Coordinates to evaluate.
 * @return   True if the pattern bit at @a pt is set, false otherwise.
 */
bool
PatternHandler::IsHighColor(const BPoint &pt) const
{
	return IsHighColor((int)pt.x, (int)pt.y);
}


/**
 * @brief Sets the pattern phase offset.
 *
 * Used by BView scrolling so that the stipple pattern remains stable in
 * world coordinates as a view scrolls. Only the low three bits of each
 * offset are kept (the pattern is 8x8).
 *
 * @param x Horizontal offset, may be positive or negative.
 * @param y Vertical offset, may be positive or negative.
 */
void
PatternHandler::SetOffsets(int32 x, int32 y)
{
	fXOffset = x & 7;
	fYOffset = y & 7;
}

