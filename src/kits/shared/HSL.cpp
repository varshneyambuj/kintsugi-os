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
 *   Copyright 2024, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file HSL.cpp
 *  @brief Implements HSL <-> RGB colour space conversions for the
 *         \c hsl_color struct.
 *
 *  All hue, saturation, and lightness values are in the range [0, 1].
 *  Conversion formulae follow the standard HSL model described at
 *  https://en.wikipedia.org/wiki/HSL_and_HSV#Color_conversion_formulae
 */

#include <HSL.h>


/**
 * @brief Converts an \c rgb_color to \c hsl_color.
 *
 * Each RGB channel is first normalised to [0, 1]. The maximum and minimum
 * normalised channel values are used to derive lightness, saturation, and
 * hue. Achromatic colours (where max == min) produce a hue and saturation
 * of zero.
 *
 * @param rgb The source colour in RGB format (channels 0-255, alpha ignored).
 * @return The equivalent colour expressed in HSL with all components in
 *         [0, 1].
 */
hsl_color
hsl_color::from_rgb(const rgb_color& rgb)
{
	hsl_color result;

	float r = rgb.red / 255.0f;
	float g = rgb.green / 255.0f;
	float b = rgb.blue / 255.0f;

	float max = max_c(max_c(r, g), b);
	float min = min_c(min_c(r, g), b);

	result.hue = result.saturation = result.lightness = (max + min) / 2;

	if (max == min) {
		// grayscale
		result.hue = result.saturation = 0;
	} else {
		float diff = max - min;
		result.saturation
			= (result.lightness > 0.5) ? (diff / (2 - max - min)) : (diff / (max + min));

		if (max == r)
			result.hue = (g - b) / diff + (g < b ? 6 : 0);
		else if (max == g)
			result.hue = (b - r) / diff + 2;
		else if (max == b)
			result.hue = (r - g) / diff + 4;

		result.hue /= 6;
	}

	return result;
}


/**
 * @brief Converts this \c hsl_color to \c rgb_color.
 *
 * Achromatic colours (saturation == 0) map directly to the grey defined by
 * \c lightness. Chromatic colours are converted using the standard two-step
 * intermediary \c p / \c q approach, delegating per-channel interpolation to
 * \c hue_to_rgb(). The alpha channel of the result is always set to 255.
 *
 * @return The equivalent colour in RGB format.
 */
rgb_color
hsl_color::to_rgb() const
{
	rgb_color result;
	result.alpha = 255;

	if (saturation == 0) {
		// grayscale
		result.red = result.green = result.blue = uint8(lightness * 255);
	} else {
		float q = lightness < 0.5 ? (lightness * (1 + saturation))
			: (lightness + saturation - lightness * saturation);
		float p = 2 * lightness - q;
		result.red = uint8(hue_to_rgb(p, q, hue + 1./3) * 255);
		result.green = uint8(hue_to_rgb(p, q, hue) * 255);
		result.blue = uint8(hue_to_rgb(p, q, hue - 1./3) * 255);
	}

	return result;
}


// reference: https://en.wikipedia.org/wiki/HSL_and_HSV#Color_conversion_formulae
// (from_rgb() and to_rgb() are derived from the same)
/**
 * @brief Maps a normalised hue offset \a t to a single RGB channel value.
 *
 * This is the standard piecewise linear interpolation helper used during
 * HSL-to-RGB conversion. The parameter \a t is first wrapped into [0, 1]
 * before the four segments of the hue circle are evaluated.
 *
 * @param p Lower interpolation bound (derived from lightness and saturation).
 * @param q Upper interpolation bound (derived from lightness and saturation).
 * @param t Hue offset for the channel being computed (may be outside [0, 1];
 *          will be wrapped automatically).
 * @return The channel intensity in [0, 1].
 */
float
hsl_color::hue_to_rgb(float p, float q, float t)
{
	if (t < 0)
		t += 1;
	if (t > 1)
		t -= 1;
	if (t < 1./6)
		return p + (q - p) * 6 * t;
	if (t < 1./2)
		return q;
	if (t < 2./3)
		return p + (q - p) * (2./3 - t) * 6;

	return p;
}
