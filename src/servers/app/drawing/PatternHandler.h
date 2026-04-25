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
 * MIT License. Copyright 2001-2007, Haiku.
 * Original authors: DarkWyrm, Stephan Aßmus.
 */

/** @file PatternHandler.h
    @brief 8x8 stipple pattern resolver supplying per-pixel high/low colors to the rasteriser. */

#ifndef PATTERNHANDLER_H
#define PATTERNHANDLER_H


#include <stdio.h>
#include <string.h>

#include <GraphicsDefs.h>

class BPoint;


/** @brief Compact 64-bit storage of an 8x8 monochrome stipple pattern.
 *
 * Pattern keeps the eight pattern rows in a single uint64 so they can be
 * compared and copied atomically. Conversions to and from the BeOS-style
 * @c pattern struct and packed @c int8[8] arrays are provided for use by the
 * legacy DisplayDriver / Painter code.
 */
class Pattern {
 public:

								Pattern() {}

								Pattern(const uint64& p)
									{ fPattern.type64 = p; }

								Pattern(const int8* p)
									{ fPattern.type64 = *((const uint64*)p); }

								Pattern(const Pattern& src)
									{ fPattern.type64 = src.fPattern.type64; }

								Pattern(const pattern& src)
									{ fPattern.type64 = *(uint64*)src.data; }

	/** @brief Returns the pattern as an 8 byte array (one byte per row). */
	inline	const int8*			GetInt8() const
									{ return fPattern.type8; }

	/** @brief Returns the pattern as a single packed 64-bit value. */
	inline	uint64				GetInt64() const
									{ return fPattern.type64; }

	/** @brief Returns a reference to the underlying BeOS-style @c pattern struct. */
	inline	const ::pattern&	GetPattern() const
									{ return *(const ::pattern*)&fPattern.type64; }

	/** @brief Sets the pattern from an 8 byte array. */
	inline	void				Set(const int8* p)
									{ fPattern.type64 = *((const uint64*)p); }

	/** @brief Sets the pattern from a packed 64-bit value. */
	inline	void				Set(const uint64& p)
									{ fPattern.type64 = p; }

			Pattern&			operator=(const Pattern& from)
									{ fPattern.type64 = from.fPattern.type64; return *this; }

			Pattern&			operator=(const int64 &from)
									{ fPattern.type64 = from; return *this; }

			Pattern&			operator=(const pattern &from)
									{ memcpy(&fPattern.type64, &from, sizeof(pattern)); return *this; }

			bool				operator==(const Pattern& other) const
									{ return fPattern.type64 == other.fPattern.type64; }

			bool				operator==(const pattern& other) const
									{ return fPattern.type64 == *(uint64*)other.data; }

 private:

	typedef union
	{
		uint64	type64;
		int8	type8[8];
	} pattern_union;

			pattern_union		fPattern;
};

/** @brief Pre-built solid-high pattern (B_SOLID_HIGH); every bit set. */
extern const Pattern kSolidHigh;
/** @brief Pre-built solid-low pattern (B_SOLID_LOW); every bit clear. */
extern const Pattern kSolidLow;
/** @brief Pre-built mixed-color pattern (alternating high / low pixels). */
extern const Pattern kMixedColors;

/** @brief Pairs a stipple pattern with high and low colors and resolves per-pixel color queries.
 *
 * PatternHandlers are designed specifically for DisplayDriver subclasses.
 * Pattern support can be easily added by setting the pattern to use via
 * SetTarget, and then merely retrieving the value for the coordinates
 * specified.
 */
class PatternHandler {
 public:
	/** @brief Default constructs to B_SOLID_HIGH with black high color and white low color. */
								PatternHandler(void);
	/** @brief Constructs from a raw 8 byte pattern array; NULL falls back to B_SOLID_HIGH. */
								PatternHandler(const int8* p);
	/** @brief Constructs from a packed 64-bit pattern value. */
								PatternHandler(const uint64& p);
	/** @brief Constructs from an existing Pattern object. */
								PatternHandler(const Pattern& p);
	/** @brief Copy constructor. */
								PatternHandler(const PatternHandler& other);
	virtual						~PatternHandler(void);

	/** @brief Replaces the pattern from a raw 8 byte array; NULL resets to B_SOLID_HIGH. */
			void				SetPattern(const int8* p);
	/** @brief Replaces the pattern from a packed 64-bit value. */
			void				SetPattern(const uint64& p);
	/** @brief Replaces the pattern from a Pattern object. */
			void				SetPattern(const Pattern& p);
	/** @brief Replaces the pattern from a BeOS-style @c pattern struct. */
			void				SetPattern(const pattern& p);

	/** @brief Sets both the high and low colors used to resolve pattern bits. */
			void				SetColors(const rgb_color& high,
									const rgb_color& low);
	/** @brief Sets only the high color. */
			void				SetHighColor(const rgb_color& color);
	/** @brief Sets only the low color. */
			void				SetLowColor(const rgb_color& color);

	/** @brief Returns the current high color. */
			rgb_color			HighColor() const
									{ return fHighColor; }
	/** @brief Returns the current low color. */
			rgb_color			LowColor() const
									{ return fLowColor; }

	/** @brief Resolves the pattern color at the given BPoint. */
			rgb_color			ColorAt(const BPoint& pt) const;
	/** @brief Resolves the pattern color at the given floating point coordinate. */
			rgb_color			ColorAt(float x, float y) const;
	/** @brief Resolves the pattern color at the given integer pixel. */
	inline	rgb_color			ColorAt(int x, int y) const;

	/** @brief Returns true if the pattern bit at @a pt selects the high color. */
			bool				IsHighColor(const BPoint& pt) const;
	/** @brief Returns true if the pattern bit at integer (x, y) selects the high color. */
	inline	bool				IsHighColor(int x, int y) const;
	/** @brief Returns true if the current pattern is B_SOLID_HIGH. */
	inline	bool				IsSolidHigh() const
									{ return fPattern == B_SOLID_HIGH; }
	/** @brief Returns true if the current pattern is B_SOLID_LOW. */
	inline	bool				IsSolidLow() const
									{ return fPattern == B_SOLID_LOW; }
	/** @brief Returns true if the pattern is either solid high or solid low. */
	inline	bool				IsSolid() const
									{ return IsSolidHigh() || IsSolidLow(); }

	/** @brief Returns the underlying BeOS-style @c pattern pointer. */
			const pattern*		GetR5Pattern(void) const
									{ return (const pattern*)fPattern.GetInt8(); }
	/** @brief Returns the wrapped Pattern object. */
			const Pattern&		GetPattern(void) const
									{ return fPattern; }

	/** @brief Sets the pattern phase offset (typically the parent view's scroll origin). */
			void				SetOffsets(int32 x, int32 y);

 private:
			Pattern				fPattern;
			rgb_color			fHighColor;
			rgb_color			fLowColor;

			uint16				fXOffset;
			uint16				fYOffset;
};


/** @brief Inline pixel-color resolver used on the rasteriser hot path.
 *  @param x X coordinate in pattern space.
 *  @param y Y coordinate in pattern space.
 *  @return  HighColor() when the pattern bit is set, otherwise LowColor(). */
inline rgb_color
PatternHandler::ColorAt(int x, int y) const
{
	return IsHighColor(x, y) ? fHighColor : fLowColor;
}


/** @brief Inline bit lookup used on the rasteriser hot path.
 *  @param x X coordinate in pattern space.
 *  @param y Y coordinate in pattern space.
 *  @return  True when the bit at the (offset-adjusted) (x, y) is set. */
inline bool
PatternHandler::IsHighColor(int x, int y) const
{
	x -= fXOffset;
	y -= fYOffset;
	const int8* ptr = fPattern.GetInt8();
	int32 value = ptr[y & 7] & (1 << (7 - (x & 7)) );

	return value != 0;
}

#endif
