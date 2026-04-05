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
 * @file GradientRadialFocus.cpp
 * @brief Implementation of BGradientRadialFocus, a radial gradient with a focal point
 *
 * BGradientRadialFocus extends radial gradients with a distinct focal point offset
 * from the center, creating an asymmetric radial fill effect (similar to SVG's
 * radialGradient with fx/fy).
 *
 * @see BGradient, BGradientRadial, BView
 */


#include <Point.h>
#include <Gradient.h>
#include <GradientRadialFocus.h>


/**
 * @brief Construct a default radial-focus gradient with all parameters at the origin.
 *
 * Initializes the center, focal point, and radius all to zero, and sets the
 * gradient type to TYPE_RADIAL_FOCUS.
 */
BGradientRadialFocus::BGradientRadialFocus()
{
	fData.radial_focus.cx = 0.0f;
	fData.radial_focus.cy = 0.0f;
	fData.radial_focus.fx = 0.0f;
	fData.radial_focus.fy = 0.0f;
	fData.radial_focus.radius = 0.0f;
	fType = TYPE_RADIAL_FOCUS;
}


/**
 * @brief Construct a radial-focus gradient with a BPoint center, radius, and focal point.
 *
 * @param center The geometric center of the outermost circle.
 * @param radius The radius of the outermost circle.
 * @param focal  The focal point from which color stop 0 originates; may differ
 *               from @p center to produce an off-center highlight.
 */
BGradientRadialFocus::BGradientRadialFocus(const BPoint& center, float radius,
	const BPoint& focal)
{
	fData.radial_focus.cx = center.x;
	fData.radial_focus.cy = center.y;
	fData.radial_focus.fx = focal.x;
	fData.radial_focus.fy = focal.y;
	fData.radial_focus.radius = radius;
	fType = TYPE_RADIAL_FOCUS;
}


/**
 * @brief Construct a radial-focus gradient with explicit coordinates for center and focal point.
 *
 * @param cx     X coordinate of the center point.
 * @param cy     Y coordinate of the center point.
 * @param radius The radius of the outermost circle.
 * @param fx     X coordinate of the focal point.
 * @param fy     Y coordinate of the focal point.
 */
BGradientRadialFocus::BGradientRadialFocus(float cx, float cy, float radius,
	float fx, float fy)
{
	fData.radial_focus.cx = cx;
	fData.radial_focus.cy = cy;
	fData.radial_focus.fx = fx;
	fData.radial_focus.fy = fy;
	fData.radial_focus.radius = radius;
	fType = TYPE_RADIAL_FOCUS;
}


/**
 * @brief Return the geometric center of the radial-focus gradient.
 *
 * @return The center point as a BPoint.
 */
BPoint
BGradientRadialFocus::Center() const
{
	return BPoint(fData.radial_focus.cx, fData.radial_focus.cy);
}


/**
 * @brief Set the geometric center of the radial-focus gradient from a BPoint.
 *
 * @param center The new center point.
 */
void
BGradientRadialFocus::SetCenter(const BPoint& center)
{
	fData.radial_focus.cx = center.x;
	fData.radial_focus.cy = center.y;
}


/**
 * @brief Set the geometric center of the radial-focus gradient from explicit coordinates.
 *
 * @param cx New X coordinate of the center.
 * @param cy New Y coordinate of the center.
 */
void
BGradientRadialFocus::SetCenter(float cx, float cy)
{
	fData.radial_focus.cx = cx;
	fData.radial_focus.cy = cy;
}


/**
 * @brief Return the focal point of the radial-focus gradient.
 *
 * @return The focal point as a BPoint.
 * @see SetFocal()
 */
BPoint
BGradientRadialFocus::Focal() const
{
	return BPoint(fData.radial_focus.fx, fData.radial_focus.fy);
}


/**
 * @brief Set the focal point of the radial-focus gradient from a BPoint.
 *
 * The focal point controls where the innermost color stop appears; it need not
 * coincide with the center.
 *
 * @param focal The new focal point.
 */
void
BGradientRadialFocus::SetFocal(const BPoint& focal)
{
	fData.radial_focus.fx = focal.x;
	fData.radial_focus.fy = focal.y;
}


/**
 * @brief Set the focal point of the radial-focus gradient from explicit coordinates.
 *
 * @param fx New X coordinate of the focal point.
 * @param fy New Y coordinate of the focal point.
 */
void
BGradientRadialFocus::SetFocal(float fx, float fy)
{
	fData.radial_focus.fx = fx;
	fData.radial_focus.fy = fy;
}


/**
 * @brief Return the radius of the radial-focus gradient.
 *
 * @return The radius in pixels.
 */
float
BGradientRadialFocus::Radius() const
{
	return fData.radial_focus.radius;
}


/**
 * @brief Set the radius of the radial-focus gradient.
 *
 * @param radius The new radius in pixels.
 */
void
BGradientRadialFocus::SetRadius(float radius)
{
	fData.radial_focus.radius = radius;
}
