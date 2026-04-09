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

/** @file IntPoint.h
    @brief Integer-coordinate point type used internally by the app server. */

#ifndef	INT_POINT_H
#define	INT_POINT_H

#include <Point.h>

class IntRect;

/** @brief A 2-D point with integer coordinates, convertible to and from BPoint. */
class IntPoint {
 public:
			int32				x;
			int32				y;

								IntPoint();
								IntPoint(int32 X, int32 Y);
								IntPoint(const IntPoint& p);
								IntPoint(const BPoint& p);

			/** @brief Assigns the coordinates of another IntPoint to this one.
			    @param p The source IntPoint.
			    @return Reference to this IntPoint. */
			IntPoint&			operator=(const IntPoint& p);

			/** @brief Sets both coordinates at once.
			    @param x New x coordinate.
			    @param y New y coordinate. */
			void				Set(int32 x, int32 y);

			/** @brief Constrains this point to lie within the given IntRect.
			    @param r The bounding rectangle. */
			void				ConstrainTo(const IntRect& r);

			/** @brief Prints the coordinates to stdout for debugging. */
			void				PrintToStream() const;

			IntPoint			operator+(const IntPoint& p) const;
			IntPoint			operator-(const IntPoint& p) const;
			IntPoint&			operator+=(const IntPoint& p);
			IntPoint&			operator-=(const IntPoint& p);

			bool				operator!=(const IntPoint& p) const;
			bool				operator==(const IntPoint& p) const;

			// conversion to BPoint
								operator BPoint() const
									{ return BPoint((float)x, (float)y); }
};


inline
IntPoint::IntPoint()
	: x(0),
	  y(0)
{
}


inline
IntPoint::IntPoint(int32 x, int32 y)
	: x(x),
	  y(y)
{
}


inline
IntPoint::IntPoint(const IntPoint& p)
	: x(p.x),
	  y(p.y)
{
}


inline
IntPoint::IntPoint(const BPoint& p)
	: x((int32)p.x),
	  y((int32)p.y)
{
}


inline IntPoint&
IntPoint::operator=(const IntPoint& from)
{
	x = from.x;
	y = from.y;
	return *this;
}


inline void
IntPoint::Set(int32 x, int32 y)
{
	this->x = x;
	this->y = y;
}

#endif	// INT_POINT_H
