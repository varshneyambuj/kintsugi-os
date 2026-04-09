/*
 * Copyright 2025, Kintsugi OS Contributors.
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright 2001-2006, Haiku.
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file RGBColor.h
 *  @brief Color class supporting 8-, 15-, 16- and 32-bit representations with lazy conversion. */

#ifndef RGB_COLOR_H
#define RGB_COLOR_H


#include <GraphicsDefs.h>


/** @brief Stores a color value and lazily converts between 8/15/16/32-bit formats. */
class RGBColor {
 public:
	/** @brief Constructs from individual 8-bit RGBA components.
	 *  @param r Red component (0-255).
	 *  @param g Green component (0-255).
	 *  @param b Blue component (0-255).
	 *  @param a Alpha component (0-255, default 255). */
								RGBColor(uint8 r,
										 uint8 g,
										 uint8 b,
										 uint8 a = 255);

	/** @brief Constructs from individual int RGBA components (clamped to 0-255).
	 *  @param r Red component.
	 *  @param g Green component.
	 *  @param b Blue component.
	 *  @param a Alpha component (default 255). */
								RGBColor(int r,
										 int g,
										 int b,
										 int a = 255);

	/** @brief Constructs from an rgb_color struct.
	 *  @param color Source 32-bit color. */
								RGBColor(const rgb_color& color);

	/** @brief Constructs from a packed 16-bit (5-6-5) color value.
	 *  @param color Packed 16-bit color. */
								RGBColor(uint16 color);

	/** @brief Constructs from an 8-bit palette index.
	 *  @param color 8-bit color index. */
								RGBColor(uint8 color);

	/** @brief Copy-constructs from another RGBColor.
	 *  @param color Source color. */
								RGBColor(const RGBColor& color);

	/** @brief Default-constructs a black, fully opaque color. */
								RGBColor();

	/** @brief Returns the color as an 8-bit palette index.
	 *  @return 8-bit color representation. */
			uint8				GetColor8() const;

	/** @brief Returns the color as a packed 15-bit (5-5-5) value.
	 *  @return 15-bit color representation. */
			uint16				GetColor15() const;

	/** @brief Returns the color as a packed 16-bit (5-6-5) value.
	 *  @return 16-bit color representation. */
			uint16				GetColor16() const;

	/** @brief Returns the color as a full 32-bit rgb_color struct.
	 *  @return 32-bit color representation. */
			rgb_color			GetColor32() const;

	/** @brief Sets the color from individual 8-bit RGBA components.
	 *  @param r Red. @param g Green. @param b Blue. @param a Alpha (default 255). */
			void				SetColor(uint8 r,
										 uint8 g,
										 uint8 b,
										 uint8 a = 255);

	/** @brief Sets the color from individual int RGBA components.
	 *  @param r Red. @param g Green. @param b Blue. @param a Alpha (default 255). */
			void				SetColor(int r,
										 int g,
										 int b,
										 int a = 255);

	/** @brief Sets the color from a packed 16-bit value.
	 *  @param color16 16-bit packed color. */
			void				SetColor(uint16 color16);

	/** @brief Sets the color from an 8-bit palette index.
	 *  @param color8 8-bit color index. */
			void				SetColor(uint8 color8);

	/** @brief Sets the color from an rgb_color struct.
	 *  @param color Source 32-bit color. */
			void				SetColor(const rgb_color& color);

	/** @brief Sets the color from another RGBColor.
	 *  @param color Source color. */
			void				SetColor(const RGBColor& color);

	/** @brief Assigns from another RGBColor.
	 *  @param color Source color.
	 *  @return Reference to this object. */
			const RGBColor&		operator=(const RGBColor& color);

	/** @brief Assigns from an rgb_color struct.
	 *  @param color Source 32-bit color.
	 *  @return Reference to this object. */
			const RGBColor&		operator=(const rgb_color& color);

	/** @brief Compares equality with an rgb_color.
	 *  @param color Color to compare against.
	 *  @return true if equal. */
			bool				operator==(const rgb_color& color) const;

	/** @brief Compares equality with another RGBColor.
	 *  @param color Color to compare against.
	 *  @return true if equal. */
			bool				operator==(const RGBColor& color) const;

	/** @brief Compares inequality with an rgb_color.
	 *  @param color Color to compare against.
	 *  @return true if not equal. */
			bool				operator!=(const rgb_color& color) const;

	/** @brief Compares inequality with another RGBColor.
	 *  @param color Color to compare against.
	 *  @return true if not equal. */
			bool				operator!=(const RGBColor& color) const;

			// conversion to rgb_color
								operator rgb_color() const
									{ return fColor32; }

	/** @brief Returns whether this color is the transparent magic color.
	 *  @return true if the color represents transparency. */
			bool				IsTransparentMagic() const;

	/** @brief Prints the color components to standard output for debugging. */
			void				PrintToStream() const;

	protected:
			rgb_color			fColor32;

	// caching
	mutable	uint16				fColor16;
	mutable	uint16				fColor15;
	mutable	uint8				fColor8;
	mutable	bool				fUpdate8;
	mutable	bool				fUpdate15;
	mutable	bool				fUpdate16;
};

#endif	// RGB_COLOR_H
