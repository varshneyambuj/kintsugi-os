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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2006-2008, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Artur Wyszynski <harakash@gmail.com>
 */


/**
 * @file GradientDiamond.cpp
 * @brief Implementation of BGradientDiamond, a diamond-shaped gradient fill
 *
 * BGradientDiamond defines a gradient that radiates outward in a diamond shape
 * from a center point. Used as a fill pattern in BView drawing operations.
 *
 * @see BGradient, BView
 */


#include <Point.h>
#include <Gradient.h>
#include <GradientDiamond.h>


/**
 * @brief Construct a default diamond gradient centered at the origin.
 *
 * Initializes the center point to (0, 0) and sets the gradient type to
 * TYPE_DIAMOND.
 */
BGradientDiamond::BGradientDiamond()
{
	fData.diamond.cx = 0.0f;
	fData.diamond.cy = 0.0f;
	fType = TYPE_DIAMOND;
}


/**
 * @brief Construct a diamond gradient with a BPoint center.
 *
 * @param center The center point of the diamond gradient.
 */
BGradientDiamond::BGradientDiamond(const BPoint& center)
{
	fData.diamond.cx = center.x;
	fData.diamond.cy = center.y;
	fType = TYPE_DIAMOND;
}


/**
 * @brief Construct a diamond gradient with explicit center coordinates.
 *
 * @param cx X coordinate of the center point.
 * @param cy Y coordinate of the center point.
 */
BGradientDiamond::BGradientDiamond(float cx, float cy)
{
	fData.diamond.cx = cx;
	fData.diamond.cy = cy;
	fType = TYPE_DIAMOND;
}


/**
 * @brief Return the center point of the diamond gradient.
 *
 * @return The center point as a BPoint.
 */
BPoint
BGradientDiamond::Center() const
{
	return BPoint(fData.diamond.cx, fData.diamond.cy);
}


/**
 * @brief Set the center point of the diamond gradient from a BPoint.
 *
 * @param center The new center point.
 */
void
BGradientDiamond::SetCenter(const BPoint& center)
{
	fData.diamond.cx = center.x;
	fData.diamond.cy = center.y;
}


/**
 * @brief Set the center point of the diamond gradient from explicit coordinates.
 *
 * @param cx New X coordinate of the center.
 * @param cy New Y coordinate of the center.
 */
void
BGradientDiamond::SetCenter(float cx, float cy)
{
	fData.diamond.cx = cx;
	fData.diamond.cy = cy;
}
