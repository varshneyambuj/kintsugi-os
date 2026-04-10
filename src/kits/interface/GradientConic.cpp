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
 *   Copyright 2006-2008, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Artur Wyszynski <harakash@gmail.com>
 */


/**
 * @file GradientConic.cpp
 * @brief Implementation of BGradientConic, a conic (angular) gradient fill
 *
 * BGradientConic defines a gradient that sweeps angularly around a center point,
 * transitioning through color stops by angle. Used as a fill pattern in BView
 * drawing operations.
 *
 * @see BGradient, BView
 */


#include <Point.h>
#include <Gradient.h>
#include <GradientConic.h>


/**
 * @brief Construct a default conic gradient centered at the origin with zero angle.
 *
 * Initializes the center point to (0, 0) and the start angle to 0 degrees.
 * The gradient type is set to TYPE_CONIC.
 */
BGradientConic::BGradientConic()
{
	fData.conic.cx = 0.0f;
	fData.conic.cy = 0.0f;
	fData.conic.angle = 0.0f;
	fType = TYPE_CONIC;
}


/**
 * @brief Construct a conic gradient with a BPoint center and a start angle.
 *
 * @param center The center point around which colors sweep.
 * @param angle  The starting angle of the sweep, in degrees.
 */
BGradientConic::BGradientConic(const BPoint& center, float angle)
{
	fData.conic.cx = center.x;
	fData.conic.cy = center.y;
	fData.conic.angle = angle;
	fType = TYPE_CONIC;
}


/**
 * @brief Construct a conic gradient with explicit center coordinates and a start angle.
 *
 * @param cx    X coordinate of the center point.
 * @param cy    Y coordinate of the center point.
 * @param angle The starting angle of the sweep, in degrees.
 */
BGradientConic::BGradientConic(float cx, float cy, float angle)
{
	fData.conic.cx = cx;
	fData.conic.cy = cy;
	fData.conic.angle = angle;
	fType = TYPE_CONIC;
}


/**
 * @brief Return the center point of the conic gradient.
 *
 * @return The center point as a BPoint.
 */
BPoint
BGradientConic::Center() const
{
	return BPoint(fData.conic.cx, fData.conic.cy);
}


/**
 * @brief Set the center point of the conic gradient from a BPoint.
 *
 * @param center The new center point.
 */
void
BGradientConic::SetCenter(const BPoint& center)
{
	fData.conic.cx = center.x;
	fData.conic.cy = center.y;
}


/**
 * @brief Set the center point of the conic gradient from explicit coordinates.
 *
 * @param cx New X coordinate of the center.
 * @param cy New Y coordinate of the center.
 */
void
BGradientConic::SetCenter(float cx, float cy)
{
	fData.conic.cx = cx;
	fData.conic.cy = cy;
}


/**
 * @brief Return the start angle of the conic gradient.
 *
 * @return The start angle in degrees.
 */
float
BGradientConic::Angle() const
{
	return fData.conic.angle;
}


/**
 * @brief Set the start angle of the conic gradient.
 *
 * @param angle The new start angle in degrees.
 */
void
BGradientConic::SetAngle(float angle)
{
	fData.conic.angle = angle;
}
