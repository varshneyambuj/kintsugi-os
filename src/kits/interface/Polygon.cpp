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
 *   Copyright 2001-2009 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       Marc Flerackers, mflerackers@androme.be
 *       Marcus Overhagen
 */


/**
 * @file Polygon.cpp
 * @brief Implementation of BPolygon, a collection of points defining a polygon
 *
 * BPolygon stores an ordered list of BPoint vertices defining a closed or open polygon.
 * BView drawing functions accept BPolygon for filled and stroked polygon rendering.
 *
 * @see BView, BPoint, BRect
 */


#include <Polygon.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <AffineTransform.h>


/** @brief Maximum number of vertices allowed to prevent integer overflow during allocation. */
#define MAX_POINT_COUNT 10000000


/**
 * @brief Construct a BPolygon from an array of points.
 *
 * All @a count points are copied into internal storage and the bounding
 * rectangle is computed immediately.
 *
 * @param points Pointer to the first element of an array of BPoint vertices.
 * @param count  Number of points in the array.
 * @note If @a points is NULL or @a count is zero or negative, the polygon is
 *       created empty.
 */
BPolygon::BPolygon(const BPoint* points, int32 count)
	:
	fBounds(0.0f, 0.0f, -1.0f, -1.0f),
	fCount(0),
	fPoints(NULL)
{
	_AddPoints(points, count, true);
}


/**
 * @brief Copy-construct a BPolygon from another BPolygon passed by reference.
 *
 * @param other The source BPolygon to copy.
 */
BPolygon::BPolygon(const BPolygon& other)
	:
	fBounds(0.0f, 0.0f, -1.0f, -1.0f),
	fCount(0),
	fPoints(NULL)
{
	*this = other;
}


/**
 * @brief Copy-construct a BPolygon from a pointer to another BPolygon.
 *
 * @param other Pointer to the source BPolygon to copy. Must not be NULL.
 */
BPolygon::BPolygon(const BPolygon* other)
	:
	fBounds(0.0f, 0.0f, -1.0f, -1.0f),
	fCount(0),
	fPoints(NULL)
{
	*this = *other;
}


/**
 * @brief Construct an empty BPolygon with no vertices.
 *
 * The bounding rectangle is initialised to an invalid state. Use AddPoints()
 * to populate the polygon before drawing.
 *
 * @see AddPoints()
 */
BPolygon::BPolygon()
	:
	fBounds(0.0f, 0.0f, -1.0f, -1.0f),
	fCount(0),
	fPoints(NULL)
{
}


/**
 * @brief Destroy the BPolygon and release all allocated vertex storage.
 */
BPolygon::~BPolygon()
{
	free(fPoints);
}


/**
 * @brief Replace the contents of this polygon with a copy of another.
 *
 * Self-assignment is handled safely. The existing vertex array is freed before
 * the new data is copied. The bounding rectangle is copied directly from
 * @a other without recomputation.
 *
 * @param other The source BPolygon to copy.
 * @return A reference to this BPolygon after the assignment.
 */
BPolygon&
BPolygon::operator=(const BPolygon& other)
{
	// Make sure we aren't trying to perform a "self assignment".
	if (this == &other)
		return *this;

	free(fPoints);
	fPoints = NULL;
	fCount = 0;
	fBounds.Set(0.0f, 0.0f, -1.0f, -1.0f);

	if (_AddPoints(other.fPoints, other.fCount, false))
		fBounds = other.fBounds;

	return *this;
}


/**
 * @brief Return the bounding rectangle that encloses all vertices.
 *
 * @return The smallest axis-aligned BRect that contains all vertices of the
 *         polygon. Returns an invalid BRect if the polygon has no vertices.
 */
BRect
BPolygon::Frame() const
{
	return fBounds;
}


/**
 * @brief Append additional vertices to the polygon.
 *
 * The bounding rectangle is recomputed to include the new points.
 *
 * @param points Pointer to an array of BPoint vertices to append.
 * @param count  Number of points to add.
 * @note If the total vertex count would exceed MAX_POINT_COUNT the points
 *       are silently discarded and an error is printed to stderr.
 * @see CountPoints()
 */
void
BPolygon::AddPoints(const BPoint* points, int32 count)
{
	_AddPoints(points, count, true);
}


/**
 * @brief Return the number of vertices currently stored in the polygon.
 *
 * @return The vertex count as an int32.
 */
int32
BPolygon::CountPoints() const
{
	return fCount;
}


/**
 * @brief Map all vertices from one rectangular coordinate space to another.
 *
 * Each vertex is proportionally repositioned so that the geometry that
 * occupied @a source now occupies @a destination. The bounding rectangle is
 * mapped as well.
 *
 * @param source      The rectangle representing the original coordinate space.
 * @param destination The rectangle representing the target coordinate space.
 * @note Both @a source and @a destination must have non-zero width and height
 *       to avoid division by zero.
 */
void
BPolygon::MapTo(BRect source, BRect destination)
{
	for (uint32 i = 0; i < fCount; i++)
		_MapPoint(fPoints + i, source, destination);

	_MapRectangle(&fBounds, source, destination);
}


/**
 * @brief Print all vertices of the polygon to standard output.
 *
 * Each vertex is printed on its own line using BPoint::PrintToStream().
 */
void
BPolygon::PrintToStream() const
{
	for (uint32 i = 0; i < fCount; i++)
		fPoints[i].PrintToStream();
}


//void
//BPolygon::TransformBy(const BAffineTransform& transform)
//{
//	transform.Apply(fPoints, (int32)fCount);
//	fBounds = _ComputeBounds(fPoints, fCount);
//}
//
//
//BPolygon&
//BPolygon::TransformBySelf(const BAffineTransform& transform)
//{
//	TransformBy(transform);
//	return *this;
//}
//
//
//BPolygon
//BPolygon::TransformByCopy(const BAffineTransform& transform) const
//{
//	BPolygon copy(this);
//	copy.TransformBy(transform);
//	return copy;
//}


// #pragma mark - BPolygon private methods


/**
 * @brief Internal helper that appends vertices and optionally recomputes the bounding box.
 *
 * The internal point buffer is grown with realloc(). If allocation fails an
 * error is printed to stderr and the method returns false without modifying
 * the polygon.
 *
 * @param points        Pointer to the source point array to copy from.
 * @param count         Number of points to copy.
 * @param computeBounds If true, _ComputeBounds() is called after appending to
 *                      update fBounds; pass false when the caller will set
 *                      fBounds directly (e.g. copy assignment).
 * @return true if the points were successfully appended, false on allocation
 *         failure or invalid arguments.
 */
bool
BPolygon::_AddPoints(const BPoint* points, int32 count, bool computeBounds)
{
	if (points == NULL || count <= 0)
		return false;
	if (count > MAX_POINT_COUNT || (fCount + count) > MAX_POINT_COUNT) {
		fprintf(stderr, "BPolygon::_AddPoints(%" B_PRId32 ") - too many points"
			"\n", count);
		return false;
	}

	BPoint* newPoints = (BPoint*)realloc((void*)fPoints, (fCount + count)
		* sizeof(BPoint));
	if (newPoints == NULL) {
		fprintf(stderr, "BPolygon::_AddPoints(%" B_PRId32 ") out of memory\n",
			count);
		return false;
	}

	fPoints = newPoints;
	memcpy((void*)(fPoints + fCount), points, count * sizeof(BPoint));
	fCount += count;

	if (computeBounds)
		fBounds = _ComputeBounds(fPoints, fCount);

	return true;
}


/**
 * @brief Compute the axis-aligned bounding rectangle for an array of points.
 *
 * Iterates through every point in the array and tracks the minimum and
 * maximum x and y values.
 *
 * @param points Pointer to the point array to examine.
 * @param count  Number of points in the array.
 * @return The smallest BRect enclosing all @a count points, or an invalid
 *         BRect if @a count is zero.
 */
BRect
BPolygon::_ComputeBounds(const BPoint* points, uint32 count)
{
	if (count == 0)
		return BRect(0.0, 0.0, -1.0f, -1.0f);

	BRect bounds(points[0], points[0]);

	for (uint32 i = 1; i < count; i++) {
		if (points[i].x < bounds.left)
			bounds.left = points[i].x;

		if (points[i].y < bounds.top)
			bounds.top = points[i].y;

		if (points[i].x > bounds.right)
			bounds.right = points[i].x;

		if (points[i].y > bounds.bottom)
			bounds.bottom = points[i].y;
	}

	return bounds;
}


/**
 * @brief Map a single point from one rectangular coordinate space to another.
 *
 * Applies a proportional linear mapping so that a point at position
 * @a point within @a source is repositioned to the corresponding location
 * within @a destination.
 *
 * @param point       The point to transform; modified in place.
 * @param source      The source coordinate rectangle.
 * @param destination The destination coordinate rectangle.
 */
void
BPolygon::_MapPoint(BPoint* point, const BRect& source,
	const BRect& destination)
{
	point->x = (point->x - source.left) * destination.Width() / source.Width()
		+ destination.left;
	point->y = (point->y - source.top) * destination.Height() / source.Height()
		+ destination.top;
}


/**
 * @brief Map a rectangle from one coordinate space to another by mapping its corners.
 *
 * The top-left and bottom-right corners of @a rect are each passed through
 * _MapPoint() and the result is stored back into @a rect.
 *
 * @param rect        The rectangle to transform; modified in place.
 * @param source      The source coordinate rectangle.
 * @param destination The destination coordinate rectangle.
 */
void
BPolygon::_MapRectangle(BRect* rect, const BRect& source,
	const BRect& destination)
{
	BPoint leftTop = rect->LeftTop();
	BPoint bottomRight = rect->RightBottom();

	_MapPoint(&leftTop, source, destination);
	_MapPoint(&bottomRight, source, destination);

	*rect = BRect(leftTop, bottomRight);
}
