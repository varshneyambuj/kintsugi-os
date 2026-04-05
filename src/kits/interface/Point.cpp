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
 *   Copyright 2001-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Frans van Nispen
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file Point.cpp
 * @brief Implementation of BPoint, a 2D coordinate point
 *
 * BPoint represents a location in a 2D coordinate system with floating-point x and y
 * components. It supports arithmetic operations, constraint clamping, and printable
 * string output.
 *
 * @see BRect, BSize
 */


#include <Point.h>

#include <algorithm>

#include <stdio.h>

#include <SupportDefs.h>
#include <Rect.h>


/** @brief The origin point (0, 0) in the default coordinate system. */
const BPoint B_ORIGIN(0, 0);


/**
 * @brief Clamp the point so that it lies within the bounds of a rectangle.
 *
 * Each coordinate is independently clamped: x is constrained to
 * [rect.left, rect.right] and y to [rect.top, rect.bottom]. If the point
 * is already inside @a rect it is left unchanged.
 *
 * @param rect The bounding rectangle to constrain the point to.
 */
void
BPoint::ConstrainTo(BRect rect)
{
	x = std::max(std::min(x, rect.right), rect.left);
	y = std::max(std::min(y, rect.bottom), rect.top);
}


/**
 * @brief Print the point coordinates to standard output.
 *
 * Output format: @c BPoint(x:<value>, y:<value>)
 */
void
BPoint::PrintToStream() const
{
	printf("BPoint(x:%f, y:%f)\n", x, y);
}


/**
 * @brief Return the negation of this point.
 *
 * @return A new BPoint whose x and y components are the negatives of this
 *         point's components.
 */
BPoint
BPoint::operator-() const
{
	return BPoint(-x, -y);
}


/**
 * @brief Add two points component-wise and return the result.
 *
 * @param other The point to add to this point.
 * @return A new BPoint equal to (x + other.x, y + other.y).
 */
BPoint
BPoint::operator+(const BPoint& other) const
{
	return BPoint(x + other.x, y + other.y);
}


/**
 * @brief Subtract @a other from this point component-wise and return the result.
 *
 * @param other The point to subtract from this point.
 * @return A new BPoint equal to (x - other.x, y - other.y).
 */
BPoint
BPoint::operator-(const BPoint& other) const
{
	return BPoint(x - other.x, y - other.y);
}


/**
 * @brief Add @a other to this point in place and return a reference to self.
 *
 * @param other The point to add.
 * @return A reference to this BPoint after the addition.
 */
BPoint&
BPoint::operator+=(const BPoint& other)
{
	x += other.x;
	y += other.y;

	return *this;
}


/**
 * @brief Subtract @a other from this point in place and return a reference to self.
 *
 * @param other The point to subtract.
 * @return A reference to this BPoint after the subtraction.
 */
BPoint&
BPoint::operator-=(const BPoint& other)
{
	x -= other.x;
	y -= other.y;

	return *this;
}


/**
 * @brief Test whether two points differ in at least one coordinate.
 *
 * @param other The point to compare against.
 * @return true if x or y differs between this point and @a other, false if
 *         both coordinates are equal.
 */
bool
BPoint::operator!=(const BPoint& other) const
{
	return x != other.x || y != other.y;
}


/**
 * @brief Test whether two points are equal in both coordinates.
 *
 * @param other The point to compare against.
 * @return true if both x and y are identical to those of @a other.
 */
bool
BPoint::operator==(const BPoint& other) const
{
	return x == other.x && y == other.y;
}
