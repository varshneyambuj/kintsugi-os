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
 * @file GradientRadial.cpp
 * @brief Implementation of BGradientRadial, a radial (circular) gradient fill
 *
 * BGradientRadial defines a gradient that radiates outward from a center point in
 * concentric circles. Used as a fill pattern in BView drawing operations.
 *
 * @see BGradient, BView
 */


#include <Point.h>
#include <Gradient.h>
#include <GradientRadial.h>


/**
 * @brief Construct a default radial gradient centered at the origin with zero radius.
 *
 * Initializes the center point to (0, 0), the radius to 0, and sets the
 * gradient type to TYPE_RADIAL.
 */
BGradientRadial::BGradientRadial()
{
	fData.radial.cx = 0.0f;
	fData.radial.cy = 0.0f;
	fData.radial.radius = 0.0f;
	fType = TYPE_RADIAL;
}


/**
 * @brief Construct a radial gradient with a BPoint center and a radius.
 *
 * @param center The center point of the concentric circles.
 * @param radius The radius of the outermost circle.
 */
BGradientRadial::BGradientRadial(const BPoint& center, float radius)
{
	fData.radial.cx = center.x;
	fData.radial.cy = center.y;
	fData.radial.radius = radius;
	fType = TYPE_RADIAL;
}


/**
 * @brief Construct a radial gradient with explicit center coordinates and a radius.
 *
 * @param cx     X coordinate of the center point.
 * @param cy     Y coordinate of the center point.
 * @param radius The radius of the outermost circle.
 */
BGradientRadial::BGradientRadial(float cx, float cy, float radius)
{
	fData.radial.cx = cx;
	fData.radial.cy = cy;
	fData.radial.radius = radius;
	fType = TYPE_RADIAL;
}


/**
 * @brief Return the center point of the radial gradient.
 *
 * @return The center point as a BPoint.
 */
BPoint
BGradientRadial::Center() const
{
	return BPoint(fData.radial.cx, fData.radial.cy);
}


/**
 * @brief Set the center point of the radial gradient from a BPoint.
 *
 * @param center The new center point.
 */
void
BGradientRadial::SetCenter(const BPoint& center)
{
	fData.radial.cx = center.x;
	fData.radial.cy = center.y;
}


/**
 * @brief Set the center point of the radial gradient from explicit coordinates.
 *
 * @param cx New X coordinate of the center.
 * @param cy New Y coordinate of the center.
 */
void
BGradientRadial::SetCenter(float cx, float cy)
{
	fData.radial.cx = cx;
	fData.radial.cy = cy;
}


/**
 * @brief Return the radius of the radial gradient.
 *
 * @return The radius in pixels.
 */
float
BGradientRadial::Radius() const
{
	return fData.radial.radius;
}


/**
 * @brief Set the radius of the radial gradient.
 *
 * @param radius The new radius in pixels.
 */
void
BGradientRadial::SetRadius(float radius)
{
	fData.radial.radius = radius;
}
