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
 *   Copyright 2003-2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       Marc Flerackers, mflerackers@androme.be
 *       Michael Lotz, mmlr@mlotz.ch
 *       Marcus Overhagen, marcus@overhagen.de
 */


/**
 * @file Shape.cpp
 * @brief Implementation of BShape and BShapeIterator for vector path operations
 *
 * BShape stores a sequence of path operations (move-to, line-to, bezier curve,
 * close) that describe a vector outline. BShapeIterator provides a visitor
 * interface for traversing shape operations. BView can fill and stroke BShape
 * objects.
 *
 * @see BView, BShapeIterator
 */


#include <Shape.h>

#include <Message.h>
#include <Point.h>
#include <Rect.h>

#include <ShapePrivate.h>

#include <new>
#include <stdlib.h>
#include <string.h>


//	#pragma mark - BShapeIterator


/**
 * @brief Construct a BShapeIterator.
 *
 * The default constructor performs no action; all state is managed by
 * concrete subclasses.
 */
BShapeIterator::BShapeIterator()
{
}


/**
 * @brief Destroy the BShapeIterator.
 *
 * The virtual destructor ensures that subclass resources are released when
 * the iterator is deleted through a base-class pointer.
 */
BShapeIterator::~BShapeIterator()
{
}


/**
 * @brief Walk every operation in \a shape, dispatching to the appropriate hook.
 *
 * Decodes the packed op-list stored in the shape's private data and calls
 * IterateMoveTo(), IterateLineTo(), IterateBezierTo(), IterateArcTo(), and
 * IterateClose() in order for each recorded operation.
 *
 * @param shape The shape whose operations are to be iterated.
 * @return B_OK on success. Individual hook methods may return error codes but
 *         the default implementations always return B_OK.
 * @see IterateMoveTo(), IterateLineTo(), IterateBezierTo(), IterateArcTo(),
 *      IterateClose()
 */
status_t
BShapeIterator::Iterate(BShape* shape)
{
	shape_data* data = (shape_data*)shape->fPrivateData;
	BPoint* points = data->ptList;

	for (int32 i = 0; i < data->opCount; i++) {
		int32 op = data->opList[i] & 0xFF000000;

		if ((op & OP_MOVETO) != 0) {
			IterateMoveTo(points);
			points++;
		}

		if ((op & OP_LINETO) != 0) {
			int32 count = data->opList[i] & 0x00FFFFFF;
			IterateLineTo(count, points);
			points += count;
		}

		if ((op & OP_BEZIERTO) != 0) {
			int32 count = data->opList[i] & 0x00FFFFFF;
			IterateBezierTo(count / 3, points);
			points += count;
		}

		if ((op & OP_LARGE_ARC_TO_CW) != 0 || (op & OP_LARGE_ARC_TO_CCW) != 0
			|| (op & OP_SMALL_ARC_TO_CW) != 0
			|| (op & OP_SMALL_ARC_TO_CCW) != 0) {
			int32 count = data->opList[i] & 0x00FFFFFF;
			for (int32 i = 0; i < count / 3; i++) {
				IterateArcTo(points[0].x, points[0].y, points[1].x,
					op & (OP_LARGE_ARC_TO_CW | OP_LARGE_ARC_TO_CCW),
					op & (OP_SMALL_ARC_TO_CCW | OP_LARGE_ARC_TO_CCW),
					points[2]);
				points += 3;
			}
		}

		if ((op & OP_CLOSE) != 0)
			IterateClose();
	}

	return B_OK;
}


/**
 * @brief Hook called when a MoveTo operation is encountered during iteration.
 *
 * Subclasses override this to respond to the start of a new sub-path.
 * The default implementation does nothing and returns B_OK.
 *
 * @param point Pointer to the destination point of the move.
 * @return B_OK (default); subclasses may return an error to abort.
 * @see Iterate()
 */
status_t
BShapeIterator::IterateMoveTo(BPoint* point)
{
	return B_OK;
}


/**
 * @brief Hook called when one or more LineTo operations are encountered.
 *
 * Subclasses override this to draw or process line segments. Multiple
 * consecutive LineTo operations may be batched into a single call.
 * The default implementation does nothing and returns B_OK.
 *
 * @param lineCount  The number of line endpoints in \a linePoints.
 * @param linePoints Array of \a lineCount destination points.
 * @return B_OK (default); subclasses may return an error to abort.
 * @see Iterate()
 */
status_t
BShapeIterator::IterateLineTo(int32 lineCount, BPoint* linePoints)
{
	return B_OK;
}


/**
 * @brief Hook called when one or more cubic Bezier segments are encountered.
 *
 * Each segment is described by three consecutive points: two control points
 * followed by the endpoint. Multiple segments may be batched into a single call.
 * The default implementation does nothing and returns B_OK.
 *
 * @param bezierCount The number of complete Bezier segments.
 * @param bezierPoints Array of 3 * \a bezierCount points (control1, control2,
 *                     endpoint repeated for each segment).
 * @return B_OK (default); subclasses may return an error to abort.
 * @see Iterate()
 */
status_t
BShapeIterator::IterateBezierTo(int32 bezierCount, BPoint* bezierPoints)
{
	return B_OK;
}


/**
 * @brief Hook called when a Close operation is encountered during iteration.
 *
 * Subclasses override this to close the current sub-path with a straight line
 * back to its most recent MoveTo point. The default implementation does
 * nothing and returns B_OK.
 *
 * @return B_OK (default); subclasses may return an error to abort.
 * @see Iterate()
 */
status_t
BShapeIterator::IterateClose()
{
	return B_OK;
}


/**
 * @brief Hook called when an ArcTo operation is encountered during iteration.
 *
 * Subclasses override this to draw or process an elliptical arc. The arc
 * parameters follow the SVG arc convention. The default implementation does
 * nothing and returns B_OK.
 *
 * @param rx               X radius of the ellipse.
 * @param ry               Y radius of the ellipse.
 * @param angle            Rotation of the ellipse's X axis in degrees.
 * @param largeArc         True to take the large-arc sweep, false for small.
 * @param counterClockWise True if the arc sweeps counter-clockwise.
 * @param point            The arc endpoint.
 * @return B_OK (default); subclasses may return an error to abort.
 * @see Iterate()
 */
status_t
BShapeIterator::IterateArcTo(float& rx, float& ry, float& angle, bool largeArc,
	bool counterClockWise, BPoint& point)
{
	return B_OK;
}


// #pragma mark - BShapeIterator FBC padding


void BShapeIterator::_ReservedShapeIterator2() {}
void BShapeIterator::_ReservedShapeIterator3() {}
void BShapeIterator::_ReservedShapeIterator4() {}


// #pragma mark - BShape


/**
 * @brief Construct an empty BShape with no path operations.
 */
BShape::BShape()
{
	InitData();
}


/**
 * @brief Copy-construct a BShape by duplicating all operations from \a other.
 *
 * @param other The shape to copy.
 * @see AddShape()
 */
BShape::BShape(const BShape& other)
{
	InitData();
	AddShape(&other);
}


#if defined(__cplusplus) && __cplusplus >= 201103L
/**
 * @brief Move-construct a BShape by transferring path data from \a other.
 *
 * After the move, \a other is left in an empty but valid state.
 *
 * @param other The shape to move from.
 * @see MoveFrom()
 */
BShape::BShape(BShape&& other)
{
	InitData();
	MoveFrom(other);
}
#endif


/**
 * @brief Unarchiving constructor — restore a BShape from a BMessage archive.
 *
 * Reads the "ops" (int32 array) and "pts" (BPoint array) fields from
 * \a archive to reconstruct the path data.
 *
 * @param archive The archive message produced by Archive().
 * @see Archive(), Instantiate()
 */
BShape::BShape(BMessage* archive)
	:
	BArchivable(archive)
{
	InitData();

	shape_data* data = (shape_data*)fPrivateData;

	ssize_t size = 0;
	int32 count = 0;
	type_code type = 0;
	archive->GetInfo("ops", &type, &count);
	if (!AllocateOps(count))
		return;

	int32 i = 0;
	const uint32* opPtr;
	while (archive->FindData("ops", B_INT32_TYPE, i++,
			(const void**)&opPtr, &size) == B_OK) {
		data->opList[data->opCount++] = *opPtr;
	}

	archive->GetInfo("pts", &type, &count);
	if (!AllocatePts(count)) {
		Clear();
		return;
	}

	i = 0;
	const BPoint* ptPtr;
	while (archive->FindData("pts", B_POINT_TYPE, i++,
			(const void**)&ptPtr, &size) == B_OK) {
		data->ptList[data->ptCount++] = *ptPtr;
	}
}


/**
 * @brief Destroy the BShape, releasing all path data.
 *
 * Frees the op-list and point-list arrays when the shape owns them, then
 * releases the reference to the private shape_data object.
 */
BShape::~BShape()
{
	shape_data* data = (shape_data*)fPrivateData;
	if (!data->fOwnsMemory) {
		free(data->opList);
		free(data->ptList);
	}

	data->ReleaseReference();
}


/**
 * @brief Archive the BShape's path data into a BMessage.
 *
 * Stores the point list under the "pts" key (B_POINT_TYPE) and the op list
 * under the "ops" key (B_INT32_TYPE). An empty shape (no ops or no points)
 * is archived with just the base BArchivable fields.
 *
 * @param archive The message to archive into.
 * @param deep    Passed to BArchivable::Archive(); unused by BShape itself.
 * @return B_OK on success, or an error code on the first failure.
 * @see Instantiate()
 */
status_t
BShape::Archive(BMessage* archive, bool deep) const
{
	status_t result = BArchivable::Archive(archive, deep);

	if (result != B_OK)
		return result;

	shape_data* data = (shape_data*)fPrivateData;

	// If no valid shape data, return
	if (data->opCount == 0 || data->ptCount == 0)
		return result;

	// Avoids allocation for each point
	result = archive->AddData("pts", B_POINT_TYPE, data->ptList,
		sizeof(BPoint), true, data->ptCount);
	if (result != B_OK)
		return result;

	for (int32 i = 1; i < data->ptCount && result == B_OK; i++)
		result = archive->AddPoint("pts", data->ptList[i]);

	// Avoids allocation for each op
	if (result == B_OK) {
		result = archive->AddData("ops", B_INT32_TYPE, data->opList,
			sizeof(int32), true, data->opCount);
	}

	for (int32 i = 1; i < data->opCount && result == B_OK; i++)
		result = archive->AddInt32("ops", data->opList[i]);

	return result;
}


/**
 * @brief Create a new BShape from an archive message.
 *
 * @param archive The archive message to instantiate from.
 * @return A newly allocated BShape on success, or NULL if validation fails.
 * @see Archive()
 */
BArchivable*
BShape::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BShape"))
		return new BShape(archive);
	else
		return NULL;
}


/**
 * @brief Copy-assign another shape's path data to this shape.
 *
 * Clears the existing path and copies all operations from \a other.
 *
 * @param other The source shape.
 * @return A reference to this shape.
 * @see AddShape(), Clear()
 */
BShape&
BShape::operator=(const BShape& other)
{
	if (this != &other) {
		Clear();
		AddShape(&other);
	}

	return *this;
}


#if defined(__cplusplus) && __cplusplus >= 201103L
/**
 * @brief Move-assign another shape's path data to this shape.
 *
 * Transfers all path data from \a other without copying, then leaves \a other
 * in an empty state.
 *
 * @param other The source shape to move from.
 * @return A reference to this shape.
 * @see MoveFrom()
 */
BShape&
BShape::operator=(BShape&& other)
{
	MoveFrom(other);

	return *this;
}
#endif


/**
 * @brief Compare two shapes for equality.
 *
 * Two shapes are equal if they have identical op-lists and point-lists.
 *
 * @param other The shape to compare against.
 * @return True if both shapes contain exactly the same path data.
 */
bool
BShape::operator==(const BShape& other) const
{
	if (this == &other)
		return true;

	shape_data* data = (shape_data*)fPrivateData;
	shape_data* otherData = (shape_data*)other.fPrivateData;

	if (data->opCount != otherData->opCount)
		return false;

	if (data->ptCount != otherData->ptCount)
		return false;

	return memcmp(data->opList, otherData->opList,
			data->opCount * sizeof(uint32)) == 0
		&& memcmp(data->ptList, otherData->ptList,
			data->ptCount * sizeof(BPoint)) == 0;
}


/**
 * @brief Compare two shapes for inequality.
 *
 * @param other The shape to compare against.
 * @return True if the shapes differ in any op or point.
 * @see operator==()
 */
bool
BShape::operator!=(const BShape& other) const
{
	return !(*this == other);
}


/**
 * @brief Remove all path operations and reset the shape to an empty state.
 *
 * Frees the op-list and point-list arrays and resets fState and fBuildingOp
 * to zero. The shape can be reused after calling Clear().
 */
void
BShape::Clear()
{
	shape_data* data = (shape_data*)fPrivateData;

	data->opCount = 0;
	data->opSize = 0;
	if (data->opList) {
		free(data->opList);
		data->opList = NULL;
	}

	data->ptCount = 0;
	data->ptSize = 0;
	if (data->ptList) {
		free(data->ptList);
		data->ptList = NULL;
	}

	fState = 0;
	fBuildingOp = 0;
}


/**
 * @brief Transfer path data from \a other into this shape without copying.
 *
 * Swaps the private shape_data pointers so that this shape takes ownership
 * of \a other's op-list and point-list. \a other is left empty.
 *
 * @param other The source shape whose data is moved.
 * @see operator=(BShape&&)
 */
void
BShape::MoveFrom(BShape& other)
{
	fState = other.fState;
	fBuildingOp = other.fBuildingOp;

	shape_data* data = (shape_data*)fPrivateData;
	fPrivateData = other.fPrivateData;
	other.fPrivateData = data;

	other.Clear();
}


/**
 * @brief Compute and return the axis-aligned bounding box of the shape.
 *
 * Delegates to shape_data::DetermineBoundingBox(), which scans all recorded
 * points.
 *
 * @return The smallest BRect that contains all points in the shape, or an
 *         invalid BRect if the shape is empty.
 */
BRect
BShape::Bounds() const
{
	shape_data* data = (shape_data*)fPrivateData;
	return data->DetermineBoundingBox();
}


/**
 * @brief Return the last point appended to the shape.
 *
 * Useful for computing relative moves or continuation points after a series
 * of path operations.
 *
 * @return The most recently added BPoint, or B_ORIGIN if the shape is empty.
 */
BPoint
BShape::CurrentPosition() const
{
	shape_data* data = (shape_data*)fPrivateData;

	if (data->ptCount == 0)
		return B_ORIGIN;

	return data->ptList[data->ptCount - 1];
}


/**
 * @brief Append all operations and points from \a otherShape to this shape.
 *
 * Allocates additional capacity as needed. The fBuildingOp is updated to
 * match the last building op of \a otherShape so that subsequent operations
 * are merged correctly.
 *
 * @param otherShape The shape whose path data is appended.
 * @return B_OK on success.
 * @retval B_NO_MEMORY If the op-list or point-list cannot be grown.
 */
status_t
BShape::AddShape(const BShape* otherShape)
{
	shape_data* data = (shape_data*)fPrivateData;
	shape_data* otherData = (shape_data*)otherShape->fPrivateData;

	if (!AllocateOps(otherData->opCount) || !AllocatePts(otherData->ptCount))
		return B_NO_MEMORY;

	memcpy(data->opList + data->opCount, otherData->opList,
		otherData->opCount * sizeof(uint32));
	data->opCount += otherData->opCount;

	memcpy((void*)(data->ptList + data->ptCount), otherData->ptList,
		otherData->ptCount * sizeof(BPoint));
	data->ptCount += otherData->ptCount;

	fBuildingOp = otherShape->fBuildingOp;

	return B_OK;
}


/**
 * @brief Begin a new sub-path at \a point.
 *
 * If the previous operation was also a MoveTo, the destination point is
 * replaced rather than adding a new op, keeping the op-list compact.
 *
 * @param point The starting point of the new sub-path.
 * @return B_OK on success.
 * @retval B_NO_MEMORY If the op-list or point-list cannot be grown.
 * @see LineTo(), BezierTo(), Close()
 */
status_t
BShape::MoveTo(BPoint point)
{
	shape_data* data = (shape_data*)fPrivateData;

	// If the last op is MoveTo, replace the point
	if (fBuildingOp == OP_MOVETO) {
		data->ptList[data->ptCount - 1] = point;
		return B_OK;
	}

	if (!AllocateOps(1) || !AllocatePts(1))
		return B_NO_MEMORY;

	fBuildingOp = OP_MOVETO;

	// Add op
	data->opList[data->opCount++] = fBuildingOp;

	// Add point
	data->ptList[data->ptCount++] = point;

	return B_OK;
}


/**
 * @brief Draw a straight line from the current position to \a point.
 *
 * Consecutive LineTo calls after a MoveTo (or other LineTo) are merged into
 * a single op entry with an incremented count to keep the op-list compact.
 *
 * @param point The endpoint of the line segment.
 * @return B_OK on success.
 * @retval B_NO_MEMORY If the op-list or point-list cannot be grown.
 * @see MoveTo(), BezierTo(), Close()
 */
status_t
BShape::LineTo(BPoint point)
{
	if (!AllocatePts(1))
		return B_NO_MEMORY;

	shape_data* data = (shape_data*)fPrivateData;

	// If the last op is MoveTo, replace the op and set the count
	// If the last op is LineTo increase the count
	// Otherwise add the op
	if (fBuildingOp & OP_LINETO || fBuildingOp == OP_MOVETO) {
		fBuildingOp |= OP_LINETO;
		fBuildingOp += 1;
		data->opList[data->opCount - 1] = fBuildingOp;
	} else {
		if (!AllocateOps(1))
			return B_NO_MEMORY;

		fBuildingOp = OP_LINETO + 1;
		data->opList[data->opCount++] = fBuildingOp;
	}

	// Add point
	data->ptList[data->ptCount++] = point;

	return B_OK;
}


/**
 * @brief Draw a cubic Bezier curve using an array of three control points.
 *
 * Convenience overload that unpacks \a controlPoints[0..2] and calls the
 * three-argument form.
 *
 * @param controlPoints Array of three points: control1, control2, endpoint.
 * @return B_OK on success.
 * @retval B_NO_MEMORY If the op-list or point-list cannot be grown.
 * @see BezierTo(const BPoint&, const BPoint&, const BPoint&)
 */
status_t
BShape::BezierTo(BPoint controlPoints[3])
{
	return BezierTo(controlPoints[0], controlPoints[1], controlPoints[2]);
}


/**
 * @brief Draw a cubic Bezier curve to \a endPoint via two control points.
 *
 * Consecutive BezierTo calls are merged into a single op entry with an
 * incremented count (in steps of 3) to keep the op-list compact.
 *
 * @param control1  The first control point.
 * @param control2  The second control point.
 * @param endPoint  The curve endpoint.
 * @return B_OK on success.
 * @retval B_NO_MEMORY If the op-list or point-list cannot be grown.
 * @see LineTo(), ArcTo(), Close()
 */
status_t
BShape::BezierTo(const BPoint& control1, const BPoint& control2,
	const BPoint& endPoint)
{
	if (!AllocatePts(3))
		return B_NO_MEMORY;

	shape_data* data = (shape_data*)fPrivateData;

	// If the last op is MoveTo, replace the op and set the count
	// If the last op is BezierTo increase the count
	// Otherwise add the op
	if (fBuildingOp & OP_BEZIERTO || fBuildingOp == OP_MOVETO) {
		fBuildingOp |= OP_BEZIERTO;
		fBuildingOp += 3;
		data->opList[data->opCount - 1] = fBuildingOp;
	} else {
		if (!AllocateOps(1))
			return B_NO_MEMORY;
		fBuildingOp = OP_BEZIERTO + 3;
		data->opList[data->opCount++] = fBuildingOp;
	}

	// Add points
	data->ptList[data->ptCount++] = control1;
	data->ptList[data->ptCount++] = control2;
	data->ptList[data->ptCount++] = endPoint;

	return B_OK;
}


/**
 * @brief Draw an elliptical arc following the SVG arc convention.
 *
 * The arc parameters (radii, axis rotation, large-arc flag, sweep direction)
 * map directly to the SVG 'A' path command. The parameters are packed into
 * three BPoint slots so they fit the existing point-list structure.
 *
 * @param rx               X radius of the arc ellipse.
 * @param ry               Y radius of the arc ellipse.
 * @param angle            Rotation of the ellipse's X axis in degrees.
 * @param largeArc         True to use the large-arc sweep.
 * @param counterClockWise True if the arc sweeps counter-clockwise.
 * @param point            The arc endpoint.
 * @return B_OK on success.
 * @retval B_NO_MEMORY If the op-list or point-list cannot be grown.
 * @see BezierTo(), LineTo(), Close()
 */
status_t
BShape::ArcTo(float rx, float ry, float angle, bool largeArc,
	bool counterClockWise, const BPoint& point)
{
	if (!AllocatePts(3))
		return B_NO_MEMORY;

	shape_data* data = (shape_data*)fPrivateData;

	uint32 op;
	if (largeArc) {
		if (counterClockWise)
			op = OP_LARGE_ARC_TO_CCW;
		else
			op = OP_LARGE_ARC_TO_CW;
	} else {
		if (counterClockWise)
			op = OP_SMALL_ARC_TO_CCW;
		else
			op = OP_SMALL_ARC_TO_CW;
	}

	// If the last op is MoveTo, replace the op and set the count
	// If the last op is ArcTo increase the count
	// Otherwise add the op
	if (fBuildingOp == op || fBuildingOp == (op | OP_MOVETO)) {
		fBuildingOp |= op;
		fBuildingOp += 3;
		data->opList[data->opCount - 1] = fBuildingOp;
	} else {
		if (!AllocateOps(1))
			return B_NO_MEMORY;

		fBuildingOp = op + 3;
		data->opList[data->opCount++] = fBuildingOp;
	}

	// Add points
	data->ptList[data->ptCount++] = BPoint(rx, ry);
	data->ptList[data->ptCount++] = BPoint(angle, 0);
	data->ptList[data->ptCount++] = point;

	return B_OK;
}


/**
 * @brief Close the current sub-path with a straight line back to its origin.
 *
 * A Close immediately after another Close or a MoveTo is silently ignored.
 *
 * @return B_OK on success.
 * @retval B_NO_MEMORY If the op-list cannot be grown.
 * @see MoveTo(), LineTo()
 */
status_t
BShape::Close()
{
	// If the last op is Close or MoveTo, ignore this
	if (fBuildingOp == OP_CLOSE || fBuildingOp == OP_MOVETO)
		return B_OK;

	if (!AllocateOps(1))
		return B_NO_MEMORY;

	shape_data* data = (shape_data*)fPrivateData;

	// ToDo: Decide about that, it's not BeOS compatible
	// If there was any op before we can attach the close to it
	/*if (fBuildingOp) {
		fBuildingOp |= OP_CLOSE;
		data->opList[data->opCount - 1] = fBuildingOp;
		return B_OK;
	}*/

	fBuildingOp = OP_CLOSE;
	data->opList[data->opCount++] = fBuildingOp;

	return B_OK;
}


//	#pragma mark - BShape private methods


/**
 * @brief Binary-compatibility hook for BShape virtual methods.
 *
 * Forwards unknown perform codes to BArchivable::Perform().
 *
 * @param code The perform code identifying the operation.
 * @param data Pointer to the perform_data structure for \a code.
 * @return B_OK on success, or an error from BArchivable::Perform().
 */
status_t
BShape::Perform(perform_code code, void* data)
{
	return BArchivable::Perform(code, data);
}


//	#pragma mark - BShape FBC methods


void BShape::_ReservedShape1() {}
void BShape::_ReservedShape2() {}
void BShape::_ReservedShape3() {}
void BShape::_ReservedShape4() {}


//	#pragma mark - BShape private methods


/**
 * @brief Read the raw op-list and point-list out of the shape's private data.
 *
 * Provides low-level access to the shape internals for use by the app server
 * or rendering back-ends that need to operate directly on the packed arrays.
 *
 * @param opCount  Set to the number of entries in the op-list on return.
 * @param ptCount  Set to the number of points in the point-list on return.
 * @param opList   Set to a pointer to the op-list array on return.
 * @param ptList   Set to a pointer to the point-list array on return.
 */
void
BShape::Private::GetData(int32* opCount, int32* ptCount, uint32** opList,
	BPoint** ptList)
{
	shape_data* data = PrivateData();

	*opCount = data->opCount;
	*ptCount = data->ptCount;
	*opList = data->opList;
	*ptList = data->ptList;
}


/**
 * @brief Replace the shape's raw op-list and point-list in bulk.
 *
 * Clears the existing shape data, allocates new arrays of the required size,
 * and copies the supplied lists. The fBuildingOp is set to the last op in the
 * new list so that subsequent path operations are merged correctly.
 *
 * @param opCount The number of entries in \a opList.
 * @param ptCount The number of points in \a ptList.
 * @param opList  The new op-list; copied into internal storage.
 * @param ptList  The new point-list; copied into internal storage.
 */
void
BShape::Private::SetData(int32 opCount, int32 ptCount, const uint32* opList,
	const BPoint* ptList)
{
	fShape.Clear();

	if (opCount == 0)
		return;

	shape_data* data = PrivateData();

	if (!fShape.AllocateOps(opCount) || !fShape.AllocatePts(ptCount))
		return;

	memcpy(data->opList, opList, opCount * sizeof(uint32));
	data->opCount = opCount;
	fShape.fBuildingOp = data->opList[data->opCount - 1];

	if (ptCount > 0) {
		memcpy((void*)data->ptList, ptList, ptCount * sizeof(BPoint));
		data->ptCount = ptCount;
	}
}


/**
 * @brief Allocate or zero-initialise the shape's private data.
 *
 * Creates a new shape_data object, sets fState and fBuildingOp to zero, and
 * initialises all list pointers and counts to NULL / 0. Called by every
 * constructor before any path data is added.
 */
void
BShape::InitData()
{
	fPrivateData = new shape_data;
	shape_data* data = (shape_data*)fPrivateData;

	fState = 0;
	fBuildingOp = 0;

	data->opList = NULL;
	data->opCount = 0;
	data->opSize = 0;
	data->ptList = NULL;
	data->ptCount = 0;
	data->ptSize = 0;
}


/**
 * @brief Ensure the op-list has capacity for at least \a count more entries.
 *
 * Grows the op-list in chunks of 256 entries using realloc() to amortise
 * allocation costs. The existing entries are preserved.
 *
 * @param count The number of additional entries required.
 * @return True if the array has sufficient capacity after the call, false if
 *         realloc() fails.
 */
inline bool
BShape::AllocateOps(int32 count)
{
	shape_data* data = (shape_data*)fPrivateData;

	int32 newSize = (data->opCount + count + 255) / 256 * 256;
	if (data->opSize >= newSize)
		return true;

	uint32* resizedArray = (uint32*)realloc(data->opList, newSize * sizeof(uint32));
	if (resizedArray) {
		data->opList = resizedArray;
		data->opSize = newSize;
		return true;
	}
	return false;
}


/**
 * @brief Ensure the point-list has capacity for at least \a count more points.
 *
 * Grows the point-list in chunks of 256 entries using realloc() to amortise
 * allocation costs. The existing points are preserved.
 *
 * @param count The number of additional BPoint slots required.
 * @return True if the array has sufficient capacity after the call, false if
 *         realloc() fails.
 */
inline bool
BShape::AllocatePts(int32 count)
{
	shape_data* data = (shape_data*)fPrivateData;

	int32 newSize = (data->ptCount + count + 255) / 256 * 256;
	if (data->ptSize >= newSize)
		return true;

	BPoint* resizedArray = (BPoint*)realloc((void*)data->ptList,
		newSize * sizeof(BPoint));
	if (resizedArray) {
		data->ptList = resizedArray;
		data->ptSize = newSize;
		return true;
	}
	return false;
}


//	#pragma mark - BShape binary compatibility methods


#if __GNUC__ < 3


/**
 * @brief GCC 2 in-place copy-constructor thunk for BShape.
 *
 * Constructs a BShape copy in pre-allocated memory \a self by copying all
 * path data from \a copyFrom. Used by the GCC 2 ABI when a BShape is
 * returned by value.
 *
 * @param self     Pre-allocated memory region for the new BShape.
 * @param copyFrom The source shape to copy from.
 * @return Pointer to the newly constructed BShape at \a self.
 */
extern "C" BShape*
__6BShapeR6BShape(void* self, BShape& copyFrom)
{
	return new (self) BShape(copyFrom);
		// we need to instantiate the object in the provided memory
}


/**
 * @brief GCC 2 binary-compatibility thunk for BShape::Bounds().
 *
 * @param self The BShape instance to query.
 * @return The bounding box of the shape.
 */
extern "C" BRect
Bounds__6BShape(BShape* self)
{
	return self->Bounds();
}


/**
 * @brief GCC 2 binary-compatibility placeholder for the first reserved
 *        BShapeIterator slot.
 *
 * @param self Unused iterator pointer.
 */
extern "C" void
_ReservedShapeIterator1__14BShapeIterator(BShapeIterator* self)
{
}


#else // __GNUC__ < 3


/**
 * @brief GCC 3+ binary-compatibility placeholder for the first reserved
 *        BShapeIterator slot.
 *
 * @param self Unused iterator pointer.
 */
extern "C" void
_ZN14BShapeIterator23_ReservedShapeIterator1Ev(BShapeIterator* self)
{
}


#endif // __GNUC__ >= 3
