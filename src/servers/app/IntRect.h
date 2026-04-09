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
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2001-2006, Haiku, Inc. All Rights Reserved.
 * Original authors: Frans van Nispen, Stephan Aßmus <superstippi@gmx.de>.
 */

/** @file IntRect.h
    @brief Integer-coordinate rectangle type used internally by the app server. */

#ifndef	INT_RECT_H
#define	INT_RECT_H


#include <Region.h>

#include "IntPoint.h"

/** @brief A 2-D rectangle with integer coordinates, convertible to BRect and clipping_rect. */
class IntRect {
 public:
			int32				left;
			int32				top;
			int32				right;
			int32				bottom;

								IntRect();
								IntRect(const IntRect& r);
								IntRect(const BRect& r);
								IntRect(int32 l, int32 t, int32 r, int32 b);
								IntRect(const IntPoint& lt,
										const IntPoint& rb);

			/** @brief Assigns another IntRect's coordinates to this one.
			    @param r Source IntRect.
			    @return Reference to this IntRect. */
			IntRect&			operator=(const IntRect &r);

			/** @brief Sets all four coordinates at once.
			    @param l Left edge.
			    @param t Top edge.
			    @param r Right edge.
			    @param b Bottom edge. */
			void				Set(int32 l, int32 t, int32 r, int32 b);

			/** @brief Prints the rectangle coordinates to stdout for debugging. */
			void				PrintToStream() const;

			/** @brief Returns the top-left corner as an IntPoint.
			    @return Top-left IntPoint. */
			IntPoint			LeftTop() const;

			/** @brief Returns the bottom-right corner as an IntPoint.
			    @return Bottom-right IntPoint. */
			IntPoint			RightBottom() const;

			/** @brief Returns the bottom-left corner as an IntPoint.
			    @return Bottom-left IntPoint. */
			IntPoint			LeftBottom() const;

			/** @brief Returns the top-right corner as an IntPoint.
			    @return Top-right IntPoint. */
			IntPoint			RightTop() const;

			/** @brief Sets the top-left corner.
			    @param p New top-left IntPoint. */
			void				SetLeftTop(const IntPoint& p);

			/** @brief Sets the bottom-right corner.
			    @param p New bottom-right IntPoint. */
			void				SetRightBottom(const IntPoint& p);

			/** @brief Sets the bottom-left corner.
			    @param p New bottom-left IntPoint. */
			void				SetLeftBottom(const IntPoint& p);

			/** @brief Sets the top-right corner.
			    @param p New top-right IntPoint. */
			void				SetRightTop(const IntPoint& p);

			// transformation
			/** @brief Shrinks the rectangle by the given point's x and y values on each side.
			    @param p Inset amounts. */
			void				InsetBy(const IntPoint& p);

			/** @brief Shrinks the rectangle by dx horizontally and dy vertically on each side.
			    @param dx Horizontal inset.
			    @param dy Vertical inset. */
			void				InsetBy(int32 dx, int32 dy);

			/** @brief Moves the rectangle by the given point's coordinates.
			    @param p Offset amounts. */
			void				OffsetBy(const IntPoint& p);

			/** @brief Moves the rectangle by dx and dy.
			    @param dx Horizontal offset.
			    @param dy Vertical offset. */
			void				OffsetBy(int32 dx, int32 dy);

			/** @brief Moves the rectangle so its top-left is at the given point.
			    @param p New top-left position. */
			void				OffsetTo(const IntPoint& p);

			/** @brief Moves the rectangle so its top-left is at (x, y).
			    @param x New left edge.
			    @param y New top edge. */
			void				OffsetTo(int32 x, int32 y);

			// expression transformations
			/** @brief Insets this rectangle and returns a reference to it.
			    @param p Inset amounts.
			    @return Reference to this IntRect. */
			IntRect&			InsetBySelf(const IntPoint& p);

			/** @brief Insets this rectangle by dx/dy and returns a reference to it.
			    @param dx Horizontal inset.
			    @param dy Vertical inset.
			    @return Reference to this IntRect. */
			IntRect&			InsetBySelf(int32 dx, int32 dy);

			/** @brief Returns a new rectangle inset by the given amounts.
			    @param p Inset amounts.
			    @return Inset copy. */
			IntRect				InsetByCopy(const IntPoint& p);

			/** @brief Returns a new rectangle inset by dx/dy.
			    @param dx Horizontal inset.
			    @param dy Vertical inset.
			    @return Inset copy. */
			IntRect				InsetByCopy(int32 dx, int32 dy);

			/** @brief Offsets this rectangle and returns a reference to it.
			    @param p Offset amounts.
			    @return Reference to this IntRect. */
			IntRect&			OffsetBySelf(const IntPoint& p);

			/** @brief Offsets this rectangle by dx/dy and returns a reference.
			    @param dx Horizontal offset.
			    @param dy Vertical offset.
			    @return Reference to this IntRect. */
			IntRect&			OffsetBySelf(int32 dx, int32 dy);

			/** @brief Returns a new rectangle offset by the given amounts.
			    @param p Offset amounts.
			    @return Offset copy. */
			IntRect				OffsetByCopy(const IntPoint& p);

			/** @brief Returns a new rectangle offset by dx/dy.
			    @param dx Horizontal offset.
			    @param dy Vertical offset.
			    @return Offset copy. */
			IntRect				OffsetByCopy(int32 dx, int32 dy);

			/** @brief Moves this rectangle to the given top-left and returns a reference.
			    @param p New top-left position.
			    @return Reference to this IntRect. */
			IntRect&			OffsetToSelf(const IntPoint& p);

			/** @brief Moves this rectangle to (x,y) and returns a reference.
			    @param x New left edge.
			    @param y New top edge.
			    @return Reference to this IntRect. */
			IntRect&			OffsetToSelf(int32 dx, int32 dy);

			/** @brief Returns a copy of this rectangle moved to the given top-left.
			    @param p New top-left position.
			    @return Moved copy. */
			IntRect				OffsetToCopy(const IntPoint& p);

			/** @brief Returns a copy of this rectangle moved to (x,y).
			    @param x New left edge.
			    @param y New top edge.
			    @return Moved copy. */
			IntRect				OffsetToCopy(int32 dx, int32 dy);

			// comparison
			bool				operator==(const IntRect& r) const;
			bool				operator!=(const IntRect& r) const;

			// intersection and union
			/** @brief Returns the intersection of this rectangle with another.
			    @param r The other IntRect.
			    @return Intersected IntRect (may be invalid if no overlap). */
			IntRect				operator&(const IntRect& r) const;

			/** @brief Returns the union (bounding box) of this rectangle and another.
			    @param r The other IntRect.
			    @return Union IntRect. */
			IntRect				operator|(const IntRect& r) const;

			// conversion to BRect and clipping_rect
								operator clipping_rect() const;
								operator BRect() const
									{ return BRect(left, top,
												   right, bottom); }

			/** @brief Returns true if this rectangle overlaps with another.
			    @param r The other IntRect.
			    @return true if they intersect. */
			bool				Intersects(const IntRect& r) const;

			/** @brief Returns true if both dimensions are non-negative.
			    @return true if left <= right and top <= bottom. */
			bool				IsValid() const;

			/** @brief Returns the width (right - left).
			    @return Width in pixels. */
			int32				Width() const;

			/** @brief Returns the integer width (same as Width() for this type).
			    @return Integer width in pixels. */
			int32				IntegerWidth() const;

			/** @brief Returns the height (bottom - top).
			    @return Height in pixels. */
			int32				Height() const;

			/** @brief Returns the integer height (same as Height() for this type).
			    @return Integer height in pixels. */
			int32				IntegerHeight() const;

			/** @brief Returns true if the given point lies within this rectangle.
			    @param p The point to test.
			    @return true if contained. */
			bool				Contains(const IntPoint& p) const;

			/** @brief Returns true if another rectangle is entirely within this one.
			    @param r The rectangle to test.
			    @return true if fully contained. */
			bool				Contains(const IntRect& r) const;
};


// inline definitions ----------------------------------------------------------

inline IntPoint
IntRect::LeftTop() const
{
	return *(const IntPoint*)&left;
}


inline IntPoint
IntRect::RightBottom() const
{
	return *(const IntPoint*)&right;
}


inline IntPoint
IntRect::LeftBottom() const
{
	return IntPoint(left, bottom);
}


inline IntPoint
IntRect::RightTop() const
{
	return IntPoint(right, top);
}


inline
IntRect::IntRect()
{
	top = left = 0;
	bottom = right = -1;
}


inline
IntRect::IntRect(int32 l, int32 t, int32 r, int32 b)
{
	left = l;
	top = t;
	right = r;
	bottom = b;
}


inline
IntRect::IntRect(const IntRect &r)
{
	left = r.left;
	top = r.top;
	right = r.right;
	bottom = r.bottom;
}


inline
IntRect::IntRect(const BRect &r)
{
	left = (int32)r.left;
	top = (int32)r.top;
	right = (int32)r.right;
	bottom = (int32)r.bottom;
}


inline
IntRect::IntRect(const IntPoint& leftTop, const IntPoint& rightBottom)
{
	left = leftTop.x;
	top = leftTop.y;
	right = rightBottom.x;
	bottom = rightBottom.y;
}


inline IntRect &
IntRect::operator=(const IntRect& from)
{
	left = from.left;
	top = from.top;
	right = from.right;
	bottom = from.bottom;
	return *this;
}


inline void
IntRect::Set(int32 l, int32 t, int32 r, int32 b)
{
	left = l;
	top = t;
	right = r;
	bottom = b;
}


inline bool
IntRect::IsValid() const
{
	return left <= right && top <= bottom;
}


inline int32
IntRect::IntegerWidth() const
{
	return right - left;
}


inline int32
IntRect::Width() const
{
	return right - left;
}


inline int32
IntRect::IntegerHeight() const
{
	return bottom - top;
}


inline int32
IntRect::Height() const
{
	return bottom - top;
}

inline
IntRect::operator clipping_rect() const
{
	clipping_rect r;
	r.left = left;
	r.top = top;
	r.right = right;
	r.bottom = bottom;
	return r;
}


#endif	// INT_RECT_H
