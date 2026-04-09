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

/** @file IntRect.cpp
 *  @brief Integer 2D rectangle type with inset, offset, and containment operations. */

#include "IntRect.h"

#include <stdio.h>


/**
 * @brief Sets the left-top corner to @a p.
 * @param p The new left-top IntPoint.
 */
void
IntRect::SetLeftTop(const IntPoint& p)
{
	left = p.x;
	top = p.y;
}


/**
 * @brief Sets the right-bottom corner to @a p.
 * @param p The new right-bottom IntPoint.
 */
void
IntRect::SetRightBottom(const IntPoint& p)
{
	right = p.x;
	bottom = p.y;
}


/**
 * @brief Sets the left-bottom corner to @a p.
 * @param p The new left-bottom IntPoint.
 */
void
IntRect::SetLeftBottom(const IntPoint& p)
{
	left = p.x;
	bottom = p.y;
}


/**
 * @brief Sets the right-top corner to @a p.
 * @param p The new right-top IntPoint.
 */
void
IntRect::SetRightTop(const IntPoint& p)
{
	right = p.x;
	top = p.y;
}


/**
 * @brief Insets all sides by the components of @a point.
 * @param point Amount to inset on each axis.
 */
void
IntRect::InsetBy(const IntPoint& point)
{
	 left += point.x;
	 right -= point.x;
	 top += point.y;
	 bottom -= point.y;
}


/**
 * @brief Insets all sides by the given deltas.
 * @param dx Horizontal inset amount.
 * @param dy Vertical inset amount.
 */
void
IntRect::InsetBy(int32 dx, int32 dy)
{
	 left += dx;
	 right -= dx;
	 top += dy;
	 bottom -= dy;
}


/**
 * @brief Insets this rectangle in place and returns a reference to it.
 * @param point Amount to inset on each axis.
 * @return Reference to this rectangle.
 */
IntRect&
IntRect::InsetBySelf(const IntPoint& point)
{
	InsetBy(point);
	return *this;
}


/**
 * @brief Insets this rectangle in place and returns a reference to it.
 * @param dx Horizontal inset amount.
 * @param dy Vertical inset amount.
 * @return Reference to this rectangle.
 */
IntRect&
IntRect::InsetBySelf(int32 dx, int32 dy)
{
	InsetBy(dx, dy);
	return *this;
}


/**
 * @brief Returns a copy of this rectangle inset by @a point.
 * @param point Amount to inset on each axis.
 * @return The inset copy.
 */
IntRect
IntRect::InsetByCopy(const IntPoint& point)
{
	IntRect copy(*this);
	copy.InsetBy(point);
	return copy;
}


/**
 * @brief Returns a copy of this rectangle inset by the given deltas.
 * @param dx Horizontal inset amount.
 * @param dy Vertical inset amount.
 * @return The inset copy.
 */
IntRect
IntRect::InsetByCopy(int32 dx, int32 dy)
{
	IntRect copy(*this);
	copy.InsetBy(dx, dy);
	return copy;
}


/**
 * @brief Offsets this rectangle by the components of @a point.
 * @param point Translation vector.
 */
void
IntRect::OffsetBy(const IntPoint& point)
{
	 left += point.x;
	 right += point.x;
	 top += point.y;
	 bottom += point.y;
}


/**
 * @brief Offsets this rectangle by the given deltas.
 * @param dx Horizontal offset.
 * @param dy Vertical offset.
 */
void
IntRect::OffsetBy(int32 dx, int32 dy)
{
	 left += dx;
	 right += dx;
	 top += dy;
	 bottom += dy;
}


/**
 * @brief Offsets this rectangle in place and returns a reference to it.
 * @param point Translation vector.
 * @return Reference to this rectangle.
 */
IntRect&
IntRect::OffsetBySelf(const IntPoint& point)
{
	OffsetBy(point);
	return *this;
}


/**
 * @brief Offsets this rectangle in place and returns a reference to it.
 * @param dx Horizontal offset.
 * @param dy Vertical offset.
 * @return Reference to this rectangle.
 */
IntRect&
IntRect::OffsetBySelf(int32 dx, int32 dy)
{
	OffsetBy(dx, dy);
	return *this;
}


/**
 * @brief Returns a copy of this rectangle offset by @a point.
 * @param point Translation vector.
 * @return The offset copy.
 */
IntRect
IntRect::OffsetByCopy(const IntPoint& point)
{
	IntRect copy(*this);
	copy.OffsetBy(point);
	return copy;
}


/**
 * @brief Returns a copy of this rectangle offset by the given deltas.
 * @param dx Horizontal offset.
 * @param dy Vertical offset.
 * @return The offset copy.
 */
IntRect
IntRect::OffsetByCopy(int32 dx, int32 dy)
{
	IntRect copy(*this);
	copy.OffsetBy(dx, dy);
	return copy;
}


/**
 * @brief Moves the left-top corner to @a point, preserving dimensions.
 * @param point The new left-top position.
 */
void
IntRect::OffsetTo(const IntPoint& point)
{
	 right = (right - left) + point.x;
	 left = point.x;
	 bottom = (bottom - top) + point.y;
	 top = point.y;
}


/**
 * @brief Moves the left-top corner to (x, y), preserving dimensions.
 * @param x New left coordinate.
 * @param y New top coordinate.
 */
void
IntRect::OffsetTo(int32 x, int32 y)
{
	 right = (right - left) + x;
	 left = x;
	 bottom = (bottom - top) + y;
	 top=y;
}


/**
 * @brief Moves the left-top corner to @a point in place and returns a reference.
 * @param point The new left-top position.
 * @return Reference to this rectangle.
 */
IntRect&
IntRect::OffsetToSelf(const IntPoint& point)
{
	OffsetTo(point);
	return *this;
}


/**
 * @brief Moves the left-top corner to (dx, dy) in place and returns a reference.
 * @param dx New left coordinate.
 * @param dy New top coordinate.
 * @return Reference to this rectangle.
 */
IntRect&
IntRect::OffsetToSelf(int32 dx, int32 dy)
{
	OffsetTo(dx, dy);
	return *this;
}


/**
 * @brief Returns a copy with the left-top corner moved to @a point.
 * @param point The new left-top position.
 * @return The repositioned copy.
 */
IntRect
IntRect::OffsetToCopy(const IntPoint& point)
{
	IntRect copy(*this);
	copy.OffsetTo(point);
	return copy;
}


/**
 * @brief Returns a copy with the left-top corner moved to (dx, dy).
 * @param dx New left coordinate.
 * @param dy New top coordinate.
 * @return The repositioned copy.
 */
IntRect
IntRect::OffsetToCopy(int32 dx, int32 dy)
{
	IntRect copy(*this);
	copy.OffsetTo(dx, dy);
	return copy;
}


/**
 * @brief Prints the rectangle coordinates to standard output.
 */
void
IntRect::PrintToStream() const
{
	printf("IntRect(l:%" B_PRId32 ", t:%" B_PRId32 ", r:%" B_PRId32 ", b:%"
		B_PRId32 ")\n", left, top, right, bottom);
}


/**
 * @brief Returns whether this rectangle is equal to @a rect.
 * @param rect The rectangle to compare with.
 * @return true if all four edges are equal.
 */
bool
IntRect::operator==(const IntRect& rect) const
{
	 return left == rect.left && right == rect.right &&
	 		top == rect.top && bottom == rect.bottom;
}


/**
 * @brief Returns whether this rectangle differs from @a rect.
 * @param rect The rectangle to compare with.
 * @return true if any edge differs.
 */
bool
IntRect::operator!=(const IntRect& rect) const
{
	 return !(*this == rect);
}


/**
 * @brief Returns the intersection of this rectangle and @a rect.
 * @param rect The rectangle to intersect with.
 * @return The intersected IntRect.
 */
IntRect
IntRect::operator&(const IntRect& rect) const
{
	 return IntRect(max_c(left, rect.left), max_c(top, rect.top),
	 				min_c(right, rect.right), min_c(bottom, rect.bottom));
}


/**
 * @brief Returns the bounding union of this rectangle and @a rect.
 * @param rect The rectangle to unite with.
 * @return The united IntRect.
 */
IntRect
IntRect::operator|(const IntRect& rect) const
{
	 return IntRect(min_c(left, rect.left), min_c(top, rect.top),
	 				max_c(right, rect.right), max_c(bottom, rect.bottom));
}


/**
 * @brief Returns whether this rectangle intersects @a rect.
 *
 * Both rectangles must be valid (Width >= 0 && Height >= 0) for intersection
 * to be possible.
 *
 * @param rect The rectangle to test against.
 * @return true if the rectangles overlap.
 */
bool
IntRect::Intersects(const IntRect& rect) const
{
	if (!IsValid() || !rect.IsValid())
		return false;

	return !(rect.left > right || rect.right < left
			|| rect.top > bottom || rect.bottom < top);
}


/**
 * @brief Returns whether @a point lies within this rectangle.
 * @param point The IntPoint to test.
 * @return true if the point is inside or on the boundary.
 */
bool
IntRect::Contains(const IntPoint& point) const
{
	return point.x >= left && point.x <= right
			&& point.y >= top && point.y <= bottom;
}


/**
 * @brief Returns whether @a rect is fully contained within this rectangle.
 * @param rect The IntRect to test.
 * @return true if @a rect is a subset of this rectangle.
 */
bool
IntRect::Contains(const IntRect& rect) const
{
	return rect.left >= left && rect.right <= right
			&& rect.top >= top && rect.bottom <= bottom;
}
