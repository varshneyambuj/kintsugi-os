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
 *   Copyright 2001-2006, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Frans van Nispen
 *       Stephan Aßmus <superstippi@gmx.de>
 */

/** @file IntPoint.cpp
 *  @brief Integer 2D point type with arithmetic and constraint operations. */


#include "IntPoint.h"

#include <stdio.h>

#include "IntRect.h"


/**
 * @brief Constrains this point to lie within the given rectangle.
 * @param r The bounding IntRect to constrain within.
 */
void
IntPoint::ConstrainTo(const IntRect& r)
{
	x = max_c(min_c(x, r.right), r.left);
	y = max_c(min_c(y, r.bottom), r.top);
}


/**
 * @brief Prints the point coordinates to standard output.
 */
void
IntPoint::PrintToStream() const
{
	printf("IntPoint(x:%" B_PRId32 ", y:%" B_PRId32 ")\n", x, y);
}


/**
 * @brief Adds two IntPoints component-wise.
 * @param p The point to add.
 * @return A new IntPoint equal to (*this + p).
 */
IntPoint
IntPoint::operator+(const IntPoint& p) const
{
	return IntPoint(x + p.x, y + p.y);
}


/**
 * @brief Subtracts @a p from this point component-wise.
 * @param p The point to subtract.
 * @return A new IntPoint equal to (*this - p).
 */
IntPoint
IntPoint::operator-(const IntPoint& p) const
{
	return IntPoint(x - p.x, y - p.y);
}


/**
 * @brief Adds @a p to this point in place.
 * @param p The point to add.
 * @return Reference to this point after addition.
 */
IntPoint &
IntPoint::operator+=(const IntPoint& p)
{
	x += p.x;
	y += p.y;

	return *this;
}


/**
 * @brief Subtracts @a p from this point in place.
 * @param p The point to subtract.
 * @return Reference to this point after subtraction.
 */
IntPoint &
IntPoint::operator-=(const IntPoint& p)
{
	x -= p.x;
	y -= p.y;

	return *this;
}


/**
 * @brief Returns whether this point differs from @a p.
 * @param p The point to compare with.
 * @return true if the coordinates differ.
 */
bool
IntPoint::operator!=(const IntPoint& p) const
{
	return x != p.x || y != p.y;
}


/**
 * @brief Returns whether this point equals @a p.
 * @param p The point to compare with.
 * @return true if both coordinates are equal.
 */
bool
IntPoint::operator==(const IntPoint& p) const
{
	return x == p.x && y == p.y;
}
