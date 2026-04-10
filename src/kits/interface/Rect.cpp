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
 *   Copyright 2001-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Frans van Nispen
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file Rect.cpp
 * @brief Implementation of BRect, a 2D axis-aligned rectangle
 *
 * BRect represents a rectangle by its left, top, right, and bottom float coordinates.
 * It supports hit-testing, intersection, union, inset/offset operations, and printable
 * string output.
 *
 * @see BPoint, BSize, BView
 */


#include <Rect.h>

#include <algorithm>

#include <stdio.h>


/**
 * @brief Set the left and top edges of the rectangle from a BPoint.
 *
 * @param point The point whose x component becomes left and y component
 *              becomes top.
 */
void
BRect::SetLeftTop(const BPoint point)
{
	left = point.x;
	top = point.y;
}


/**
 * @brief Set the right and bottom edges of the rectangle from a BPoint.
 *
 * @param point The point whose x component becomes right and y component
 *              becomes bottom.
 */
void
BRect::SetRightBottom(const BPoint point)
{
	right = point.x;
	bottom = point.y;
}


/**
 * @brief Set the left and bottom edges of the rectangle from a BPoint.
 *
 * @param point The point whose x component becomes left and y component
 *              becomes bottom.
 */
void
BRect::SetLeftBottom(const BPoint point)
{
	left = point.x;
	bottom = point.y;
}


/**
 * @brief Set the right and top edges of the rectangle from a BPoint.
 *
 * @param point The point whose x component becomes right and y component
 *              becomes top.
 */
void
BRect::SetRightTop(const BPoint point)
{
	right = point.x;
	top = point.y;
}


/**
 * @brief Inset all four edges by the x and y components of a point.
 *
 * left and top are increased by point.x and point.y respectively while
 * right and bottom are decreased by the same amounts, shrinking the
 * rectangle symmetrically. Negative values expand the rectangle.
 *
 * @param point A BPoint whose x is the horizontal inset and y is the
 *              vertical inset.
 * @see InsetBy(float, float)
 */
void
BRect::InsetBy(BPoint point)
{
	left += point.x;
	right -= point.x;
	top += point.y;
	bottom -= point.y;
}


/**
 * @brief Inset all four edges by the given horizontal and vertical amounts.
 *
 * left and top are increased by @a dx and @a dy respectively while right
 * and bottom are decreased by the same amounts. Negative values expand the
 * rectangle.
 *
 * @param dx The horizontal inset applied to both left and right.
 * @param dy The vertical inset applied to both top and bottom.
 * @see InsetBy(BPoint)
 */
void
BRect::InsetBy(float dx, float dy)
{
	left += dx;
	right -= dx;
	top += dy;
	bottom -= dy;
}


/**
 * @brief Inset this rectangle by a point offset and return a reference to self.
 *
 * @param point A BPoint whose x and y specify the horizontal and vertical insets.
 * @return A reference to this BRect after the inset.
 * @see InsetBy(BPoint)
 */
BRect&
BRect::InsetBySelf(BPoint point)
{
	InsetBy(point);
	return *this;
}


/**
 * @brief Inset this rectangle by scalar offsets and return a reference to self.
 *
 * @param dx The horizontal inset.
 * @param dy The vertical inset.
 * @return A reference to this BRect after the inset.
 * @see InsetBy(float, float)
 */
BRect&
BRect::InsetBySelf(float dx, float dy)
{
	InsetBy(dx, dy);
	return *this;
}


/**
 * @brief Return a copy of this rectangle inset by a point offset.
 *
 * @param point A BPoint whose x and y specify the horizontal and vertical insets.
 * @return A new BRect equal to this rectangle after the inset is applied.
 * @see InsetBy(BPoint)
 */
BRect
BRect::InsetByCopy(BPoint point) const
{
	BRect copy(*this);
	copy.InsetBy(point);
	return copy;
}


/**
 * @brief Return a copy of this rectangle inset by scalar offsets.
 *
 * @param dx The horizontal inset.
 * @param dy The vertical inset.
 * @return A new BRect equal to this rectangle after the inset is applied.
 * @see InsetBy(float, float)
 */
BRect
BRect::InsetByCopy(float dx, float dy) const
{
	BRect copy(*this);
	copy.InsetBy(dx, dy);
	return copy;
}


/**
 * @brief Translate all four edges by the x and y components of a point.
 *
 * Unlike InsetBy(), all four edges move in the same direction so the
 * rectangle's size is preserved.
 *
 * @param point A BPoint whose x is the horizontal delta and y is the
 *              vertical delta.
 * @see OffsetBy(float, float)
 */
void
BRect::OffsetBy(BPoint point)
{
	left += point.x;
	right += point.x;
	top += point.y;
	bottom += point.y;
}


/**
 * @brief Translate all four edges by the given horizontal and vertical amounts.
 *
 * The rectangle's size is preserved; only its position changes.
 *
 * @param dx The horizontal translation added to left and right.
 * @param dy The vertical translation added to top and bottom.
 * @see OffsetBy(BPoint)
 */
void
BRect::OffsetBy(float dx, float dy)
{
	left += dx;
	right += dx;
	top += dy;
	bottom += dy;
}


/**
 * @brief Translate this rectangle by a point offset and return a reference to self.
 *
 * @param point A BPoint specifying the horizontal and vertical translation.
 * @return A reference to this BRect after the translation.
 * @see OffsetBy(BPoint)
 */
BRect&
BRect::OffsetBySelf(BPoint point)
{
	OffsetBy(point);
	return *this;
}


/**
 * @brief Translate this rectangle by scalar offsets and return a reference to self.
 *
 * @param dx The horizontal translation.
 * @param dy The vertical translation.
 * @return A reference to this BRect after the translation.
 * @see OffsetBy(float, float)
 */
BRect&
BRect::OffsetBySelf(float dx, float dy)
{
	OffsetBy(dx, dy);
	return *this;
}


/**
 * @brief Return a copy of this rectangle translated by a point offset.
 *
 * @param point A BPoint specifying the horizontal and vertical translation.
 * @return A new BRect equal to this rectangle after the translation.
 * @see OffsetBy(BPoint)
 */
BRect
BRect::OffsetByCopy(BPoint point) const
{
	BRect copy(*this);
	copy.OffsetBy(point);
	return copy;
}


/**
 * @brief Return a copy of this rectangle translated by scalar offsets.
 *
 * @param dx The horizontal translation.
 * @param dy The vertical translation.
 * @return A new BRect equal to this rectangle after the translation.
 * @see OffsetBy(float, float)
 */
BRect
BRect::OffsetByCopy(float dx, float dy) const
{
	BRect copy(*this);
	copy.OffsetBy(dx, dy);
	return copy;
}


/**
 * @brief Move the rectangle so that its top-left corner is at the given point.
 *
 * The rectangle's size is preserved; the right and bottom edges are adjusted
 * to keep the same width and height.
 *
 * @param point The new position for the top-left corner.
 * @see OffsetTo(float, float)
 */
void
BRect::OffsetTo(BPoint point)
{
	right = (right - left) + point.x;
	left = point.x;
	bottom = (bottom - top) + point.y;
	top = point.y;
}


/**
 * @brief Move the rectangle so that its top-left corner is at (x, y).
 *
 * The rectangle's size is preserved; the right and bottom edges are adjusted
 * to keep the same width and height.
 *
 * @param x The new left coordinate.
 * @param y The new top coordinate.
 * @see OffsetTo(BPoint)
 */
void
BRect::OffsetTo(float x, float y)
{
	right = (right - left) + x;
	left = x;
	bottom = (bottom - top) + y;
	top=y;
}


/**
 * @brief Move this rectangle to the given point position and return a reference to self.
 *
 * @param point The new position for the top-left corner.
 * @return A reference to this BRect after the move.
 * @see OffsetTo(BPoint)
 */
BRect&
BRect::OffsetToSelf(BPoint point)
{
	OffsetTo(point);
	return *this;
}


/**
 * @brief Move this rectangle to the given coordinates and return a reference to self.
 *
 * @param x The new left coordinate.
 * @param y The new top coordinate.
 * @return A reference to this BRect after the move.
 * @see OffsetTo(float, float)
 */
BRect&
BRect::OffsetToSelf(float x, float y)
{
	OffsetTo(x, y);
	return *this;
}


/**
 * @brief Return a copy of this rectangle moved so that its top-left corner is at a point.
 *
 * @param point The new position for the top-left corner.
 * @return A new BRect with the same size but the top-left corner at @a point.
 * @see OffsetTo(BPoint)
 */
BRect
BRect::OffsetToCopy(BPoint point) const
{
	BRect copy(*this);
	copy.OffsetTo(point);
	return copy;
}


/**
 * @brief Return a copy of this rectangle moved so that its top-left corner is at (x, y).
 *
 * @param x The new left coordinate.
 * @param y The new top coordinate.
 * @return A new BRect with the same size but the top-left corner at (x, y).
 * @see OffsetTo(float, float)
 */
BRect
BRect::OffsetToCopy(float x, float y) const
{
	BRect copy(*this);
	copy.OffsetTo(x, y);
	return copy;
}


/**
 * @brief Print the rectangle coordinates to standard output.
 *
 * Output format: @c BRect(l:<left>, t:<top>, r:<right>, b:<bottom>)
 * with one decimal place for each coordinate.
 */
void
BRect::PrintToStream() const
{
	printf("BRect(l:%.1f, t:%.1f, r:%.1f, b:%.1f)\n", left, top, right, bottom);
}


/**
 * @brief Test whether two rectangles have identical edge coordinates.
 *
 * @param other The rectangle to compare against.
 * @return true if left, top, right, and bottom all match those of @a other.
 */
bool
BRect::operator==(BRect other) const
{
	return left == other.left && right == other.right &&
		top == other.top && bottom == other.bottom;
}


/**
 * @brief Test whether two rectangles differ in at least one edge coordinate.
 *
 * @param other The rectangle to compare against.
 * @return true if any of left, top, right, or bottom differs from @a other.
 */
bool
BRect::operator!=(BRect other) const
{
	return !(*this == other);
}


/**
 * @brief Compute the intersection of two rectangles.
 *
 * Returns the largest rectangle that lies within both operands. If the
 * rectangles do not overlap the returned rectangle will be invalid
 * (right < left or bottom < top).
 *
 * @param other The rectangle to intersect with.
 * @return A new BRect representing the intersection.
 * @see Intersects()
 * @see operator|()
 */
BRect
BRect::operator&(BRect other) const
{
	return BRect(std::max(left, other.left), std::max(top, other.top),
		std::min(right, other.right), std::min(bottom, other.bottom));
}


/**
 * @brief Compute the union (bounding rectangle) of two rectangles.
 *
 * Returns the smallest rectangle that contains both operands entirely.
 *
 * @param other The rectangle to form the union with.
 * @return A new BRect representing the bounding union.
 * @see operator&()
 */
BRect
BRect::operator|(BRect other) const
{
	return BRect(std::min(left, other.left), std::min(top, other.top),
		std::max(right, other.right), std::max(bottom, other.bottom));
}


/**
 * @brief Test whether this rectangle overlaps another rectangle.
 *
 * Both rectangles must be valid (IsValid() returns true) for an overlap to
 * be detected. A shared edge or corner is not considered an intersection.
 *
 * @param rect The rectangle to test against.
 * @return true if the two rectangles have a non-zero overlapping area.
 * @see operator&()
 * @see Contains()
 */
bool
BRect::Intersects(BRect rect) const
{
	if (!IsValid() || !rect.IsValid())
		return false;

	return !(rect.left > right || rect.right < left
		|| rect.top > bottom || rect.bottom < top);
}


/**
 * @brief Test whether a point lies within this rectangle.
 *
 * The test is inclusive: points on the boundary edges are considered inside.
 *
 * @param point The point to test.
 * @return true if @a point is within or on the boundary of this rectangle.
 * @see Contains(BRect)
 */
bool
BRect::Contains(BPoint point) const
{
	return point.x >= left && point.x <= right
		&& point.y >= top && point.y <= bottom;
}


/**
 * @brief Test whether another rectangle is entirely contained within this rectangle.
 *
 * The test is inclusive: a rectangle that exactly matches the boundary of
 * this rectangle is considered contained.
 *
 * @param rect The rectangle to test.
 * @return true if all four edges of @a rect lie within this rectangle.
 * @see Contains(BPoint)
 */
bool
BRect::Contains(BRect rect) const
{
	return rect.left >= left && rect.right <= right
		&& rect.top >= top && rect.bottom <= bottom;
}


// #pragma mark - BeOS compatibility only
#if __GNUC__ == 2


extern "C" BRect
InsetByCopy__5BRectG6BPoint(BRect* self, BPoint point)
{
	BRect copy(*self);
	copy.InsetBy(point);
	return copy;
}


extern "C" BRect
InsetByCopy__5BRectff(BRect* self, float dx, float dy)
{
	BRect copy(*self);
	copy.InsetBy(dx, dy);
	return copy;
}


extern "C" BRect
OffsetByCopy__5BRectG6BPoint(BRect* self, BPoint point)
{
	BRect copy(*self);
	copy.OffsetBy(point);
	return copy;
}


extern "C" BRect
OffsetByCopy__5BRectff(BRect* self, float dx, float dy)
{
	BRect copy(*self);
	copy.OffsetBy(dx, dy);
	return copy;
}


extern "C" BRect
OffsetToCopy__5BRectG6BPoint(BRect* self, BPoint point)
{
	BRect copy(*self);
	copy.OffsetTo(point);
	return copy;
}


extern "C" BRect
OffsetToCopy__5BRectff(BRect* self, float dx, float dy)
{
	BRect copy(*self);
	copy.OffsetTo(dx, dy);
	return copy;
}


#elif __GNUC__ >= 4
// TODO: remove this when new GCC 4 packages have to be built anyway


extern "C" BRect
_ZN5BRect11InsetByCopyE6BPoint(BRect* self, BPoint point)
{
	BRect copy(*self);
	copy.InsetBy(point);
	return copy;
}


extern "C" BRect
_ZN5BRect11InsetByCopyEff(BRect* self, float dx, float dy)
{
	BRect copy(*self);
	copy.InsetBy(dx, dy);
	return copy;
}


extern "C" BRect
_ZN5BRect12OffsetByCopyE6BPoint(BRect* self, BPoint point)
{
	BRect copy(*self);
	copy.OffsetBy(point);
	return copy;
}


extern "C" BRect
_ZN5BRect12OffsetByCopyEff(BRect* self, float dx, float dy)
{
	BRect copy(*self);
	copy.OffsetBy(dx, dy);
	return copy;
}


extern "C" BRect
_ZN5BRect12OffsetToCopyE6BPoint(BRect* self, BPoint point)
{
	BRect copy(*self);
	copy.OffsetTo(point);
	return copy;
}


extern "C" BRect
_ZN5BRect12OffsetToCopyEff(BRect* self, float dx, float dy)
{
	BRect copy(*self);
	copy.OffsetTo(dx, dy);
	return copy;
}


#endif	// __GNUC__ >= 4
