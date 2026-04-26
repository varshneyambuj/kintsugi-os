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
 */

/** @file Utility.h
    @brief Inline geometry helpers for mapping unit-square coordinates onto a BRect. */

#ifndef UTILITY_H
#define UTILITY_H


#include <Rect.h>


/**
 * @brief Maps unit-square coordinates @a x and @a y onto a point inside @a area.
 *
 * @param x    Horizontal fraction in [0, 1].
 * @param y    Vertical fraction in [0, 1].
 * @param area Reference rectangle.
 * @return The point @c (area.left + area.Width() * x, area.top + area.Height() * y).
 */
inline BPoint
scale_direct(float x, float y, BRect area)
{
	return BPoint(area.Width() * x + area.left, area.Height() * y + area.top);
}


/**
 * @brief Maps unit-square coordinates onto a sub-rectangle of @a area.
 *
 * @param x1   Left fraction in [0, 1].
 * @param x2   Right fraction in [0, 1].
 * @param y1   Top fraction in [0, 1].
 * @param y2   Bottom fraction in [0, 1].
 * @param area Reference rectangle.
 * @return The sub-rectangle of @a area whose edges are placed at the given fractions.
 */
inline BRect
scale_direct(float x1, float x2, float y1, float y2, BRect area)
{
	return BRect(area.Width() * x1 + area.left, area.Height() * y1 + area.top,
		area.Width()* x2 + area.left, area.Height() * y2 + area.top);
}

/** @brief Tabulated horizontal positions used by the indexed scale() helpers. */
static const float kPositionalX[] = { 0, 0.1, 0.25, 0.3, 0.7, 0.75, 0.9, 1.0 };
/** @brief Tabulated vertical positions used by the indexed scale() helpers. */
static const float kPositionalY[] = { 0, 0.1, 0.7, 0.8, 0.9, 1.0 };

/**
 * @brief Indexed point lookup that maps integer table indices onto @a area.
 *
 * @param x    Index into kPositionalX.
 * @param y    Index into kPositionalY.
 * @param area Reference rectangle.
 * @return The corresponding scaled point in @a area.
 */
inline BPoint
scale(int x, int y,BRect area)
{
	return scale_direct(kPositionalX[x], kPositionalY[y], area);
}


/**
 * @brief Indexed rectangle lookup that maps integer table indices onto @a area.
 *
 * @param x1   Index into kPositionalX for the left edge.
 * @param x2   Index into kPositionalX for the right edge.
 * @param y1   Index into kPositionalY for the top edge.
 * @param y2   Index into kPositionalY for the bottom edge.
 * @param area Reference rectangle.
 * @return The corresponding scaled rectangle in @a area.
 */
inline BRect
scale(int x1, int x2, int y1, int y2,BRect area)
{
	return scale_direct(kPositionalX[x1], kPositionalX[x2],
		kPositionalY[y1], kPositionalY[y2], area);
}


#endif	// UTILITY_H
