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
 *   Copyright 2008-2010, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephen Deken, stephen.deken@gmail.com
 *       Stephan Aßmus <superstippi@gmx.de>
 *
 *   This file also incorporates work from Anti-Grain Geometry (AGG):
 *   Copyright 2002-2005 Maxim Shemanarev (http://www.antigrain.com)
 *   Permission to copy, use, modify, sell and distribute this software
 *   is granted provided this copyright notice appears in all copies.
 */


/**
 * @file AffineTransform.cpp
 * @brief Implementation of BAffineTransform for 2D affine coordinate transformations
 *
 * BAffineTransform wraps a 3x2 affine matrix supporting translation, scaling,
 * rotation, shearing, and composition of transforms. The implementation
 * derives from the Anti-Grain Geometry library.
 *
 * @see BView, BShape
 */


#include <AffineTransform.h>

#include <TypeConstants.h>


/** @brief The identity transform: no translation, rotation, scale, or shear. */
const BAffineTransform B_AFFINE_IDENTITY_TRANSFORM;


/**
 * @brief Construct an identity BAffineTransform.
 *
 * Initialises all scale components to 1.0 and all remaining components to 0.0,
 * producing a transform that leaves every point unchanged.
 */
BAffineTransform::BAffineTransform()
	:
	sx(1.0),
	shy(0.0),
	shx(0.0),
	sy(1.0),
	tx(0.0),
	ty(0.0)
{
}


/**
 * @brief Construct a BAffineTransform from explicit matrix components.
 *
 * The six parameters correspond directly to the elements of the 3x2 affine
 * matrix: [sx shy shx sy tx ty].
 *
 * @param sx  X-axis scale factor (column 0, row 0).
 * @param shy Y-axis shear factor applied to the X column (column 0, row 1).
 * @param shx X-axis shear factor applied to the Y column (column 1, row 0).
 * @param sy  Y-axis scale factor (column 1, row 1).
 * @param tx  X translation (column 2, row 0).
 * @param ty  Y translation (column 2, row 1).
 */
BAffineTransform::BAffineTransform(double sx, double shy, double shx,
		double sy, double tx, double ty)
	:
	sx(sx),
	shy(shy),
	shx(shx),
	sy(sy),
	tx(tx),
	ty(ty)
{
}


/**
 * @brief Copy constructor — duplicate an existing BAffineTransform.
 *
 * @param other The transform to copy.
 */
BAffineTransform::BAffineTransform(const BAffineTransform& other)
	:
	sx(other.sx),
	shy(other.shy),
	shx(other.shx),
	sy(other.sy),
	tx(other.tx),
	ty(other.ty)
{
}


/**
 * @brief Destroy the BAffineTransform.
 */
BAffineTransform::~BAffineTransform()
{
}


// #pragma mark -


/**
 * @brief Report whether the flattened form has a fixed size.
 *
 * @return Always true; a BAffineTransform always flattens to exactly six
 *         doubles (48 bytes).
 */
bool
BAffineTransform::IsFixedSize() const
{
	return true;
}


/**
 * @brief Return the type code used to identify this flattenable type.
 *
 * @return B_AFFINE_TRANSFORM_TYPE.
 */
type_code
BAffineTransform::TypeCode() const
{
	return B_AFFINE_TRANSFORM_TYPE;
}


/**
 * @brief Return the number of bytes required to flatten this transform.
 *
 * @return Six times sizeof(double), i.e. 48 bytes.
 */
ssize_t
BAffineTransform::FlattenedSize() const
{
	return 6 * sizeof(double);
}


/**
 * @brief Write the transform matrix into a raw byte buffer.
 *
 * The six matrix components are written in the order sx, shy, shx, sy, tx, ty
 * as native-endian doubles.
 *
 * @param _buffer Destination buffer; must be at least FlattenedSize() bytes.
 * @param size    Byte capacity of \a _buffer.
 * @return B_OK on success.
 * @retval B_BAD_VALUE If \a _buffer is NULL or \a size is too small.
 */
status_t
BAffineTransform::Flatten(void* _buffer, ssize_t size) const
{
	if (_buffer == NULL || size < BAffineTransform::FlattenedSize())
		return B_BAD_VALUE;

	double* buffer = reinterpret_cast<double*>(_buffer);

	buffer[0] = sx;
	buffer[1] = shy;
	buffer[2] = shx;
	buffer[3] = sy;
	buffer[4] = tx;
	buffer[5] = ty;

	return B_OK;
}


/**
 * @brief Read the transform matrix from a raw byte buffer.
 *
 * Restores the six matrix components from a buffer previously written by
 * Flatten(). The type code must match B_AFFINE_TRANSFORM_TYPE.
 *
 * @param code    Type code from the stream; must equal TypeCode().
 * @param _buffer Source buffer containing the flattened data.
 * @param size    Byte size of \a _buffer; must be at least FlattenedSize().
 * @return B_OK on success.
 * @retval B_BAD_VALUE If any argument is invalid or the type code mismatches.
 */
status_t
BAffineTransform::Unflatten(type_code code, const void* _buffer, ssize_t size)
{
	if (_buffer == NULL || size < BAffineTransform::FlattenedSize()
			|| code != BAffineTransform::TypeCode()) {
		return B_BAD_VALUE;
	}

	const double* buffer = reinterpret_cast<const double*>(_buffer);

	sx = buffer[0];
	shy = buffer[1];
	shx = buffer[2];
	sy = buffer[3];
	tx = buffer[4];
	ty = buffer[5];

	return B_OK;
}


// #pragma mark -


/**
 * @brief Create a pure-translation transform.
 *
 * @param x Translation distance along the X axis.
 * @param y Translation distance along the Y axis.
 * @return A new BAffineTransform that translates by (x, y).
 */
/*static*/ BAffineTransform
BAffineTransform::AffineTranslation(double x, double y)
{
	return BAffineTransform(1.0, 0.0, 0.0, 1.0, x, y);
}


/**
 * @brief Create a pure-rotation transform around the origin.
 *
 * @param angle Rotation angle in radians, measured counter-clockwise.
 * @return A new BAffineTransform that rotates by \a angle.
 */
/*static*/ BAffineTransform
BAffineTransform::AffineRotation(double angle)
{
	return BAffineTransform(cos(angle), sin(angle), -sin(angle), cos(angle),
		0.0, 0.0);
}


/**
 * @brief Create a non-uniform scaling transform.
 *
 * @param x Scale factor along the X axis.
 * @param y Scale factor along the Y axis.
 * @return A new BAffineTransform that scales by (x, y).
 */
/*static*/ BAffineTransform
BAffineTransform::AffineScaling(double x, double y)
{
	return BAffineTransform(x, 0.0, 0.0, y, 0.0, 0.0);
}


/**
 * @brief Create a uniform scaling transform.
 *
 * @param scale Scale factor applied equally to both axes.
 * @return A new BAffineTransform that scales uniformly by \a scale.
 */
/*static*/ BAffineTransform
BAffineTransform::AffineScaling(double scale)
{
	return BAffineTransform(scale, 0.0, 0.0, scale, 0.0, 0.0);
}


/**
 * @brief Create a shearing transform.
 *
 * @param x Shear angle in radians along the X axis (tan of the X shear angle).
 * @param y Shear angle in radians along the Y axis (tan of the Y shear angle).
 * @return A new BAffineTransform that applies the specified shear.
 */
/*static*/ BAffineTransform
BAffineTransform::AffineShearing(double x, double y)
{
	return BAffineTransform(1.0, tan(y), tan(x), 1.0, 0.0, 0.0);
}


// #pragma mark -


/**
 * @brief Apply this transform to a BPoint and return the result.
 *
 * @param point The input point in the source coordinate system.
 * @return The transformed point in the destination coordinate system.
 */
BPoint
BAffineTransform::Apply(const BPoint& point) const
{
	double x = point.x;
	double y = point.y;
	Apply(&x, &y);
	return BPoint(x, y);
}


/**
 * @brief Apply the inverse of this transform to a BPoint and return the result.
 *
 * @param point The input point in the destination coordinate system.
 * @return The back-transformed point in the source coordinate system.
 */
BPoint
BAffineTransform::ApplyInverse(const BPoint& point) const
{
	double x = point.x;
	double y = point.y;
	ApplyInverse(&x, &y);
	return BPoint(x, y);
}


/**
 * @brief Apply this transform to a BPoint in place.
 *
 * @param point Pointer to the point to transform; updated in place.
 *              Does nothing if NULL.
 */
void
BAffineTransform::Apply(BPoint* point) const
{
	if (point == NULL)
		return;
	double x = point->x;
	double y = point->y;
	Apply(&x, &y);
	point->x = x;
	point->y = y;
}


/**
 * @brief Apply the inverse of this transform to a BPoint in place.
 *
 * @param point Pointer to the point to back-transform; updated in place.
 *              Does nothing if NULL.
 */
void
BAffineTransform::ApplyInverse(BPoint* point) const
{
	if (point == NULL)
		return;
	double x = point->x;
	double y = point->y;
	ApplyInverse(&x, &y);
	point->x = x;
	point->y = y;
}


/**
 * @brief Apply this transform to an array of BPoints in place.
 *
 * @param points Pointer to the first element of the point array.
 *               Does nothing if NULL.
 * @param count  Number of points to transform.
 */
void
BAffineTransform::Apply(BPoint* points, uint32 count) const
{
	if (points != NULL) {
		for (uint32 i = 0; i < count; ++i)
			Apply(&points[i]);
	}
}


/**
 * @brief Apply the inverse of this transform to an array of BPoints in place.
 *
 * @param points Pointer to the first element of the point array.
 *               Does nothing if NULL.
 * @param count  Number of points to back-transform.
 */
void
BAffineTransform::ApplyInverse(BPoint* points, uint32 count) const
{
	if (points != NULL) {
		for (uint32 i = 0; i < count; ++i)
			ApplyInverse(&points[i]);
	}
}


// #pragma mark -


/**
 * @brief Post-translate this transform by a BPoint delta.
 *
 * Equivalent to calling TranslateBy(delta.x, delta.y).
 *
 * @param delta The (dx, dy) displacement to add.
 * @return A const reference to this transform after modification.
 */
const BAffineTransform&
BAffineTransform::TranslateBy(const BPoint& delta)
{
	return TranslateBy(delta.x, delta.y);
}


/**
 * @brief Return a translated copy of this transform.
 *
 * @param x Displacement along the X axis.
 * @param y Displacement along the Y axis.
 * @return A new transform equal to *this post-translated by (x, y).
 */
BAffineTransform
BAffineTransform::TranslateByCopy(double x, double y) const
{
	BAffineTransform copy(*this);
	copy.TranslateBy(x, y);
	return copy;
}


/**
 * @brief Return a translated copy of this transform using a BPoint delta.
 *
 * @param delta The (dx, dy) displacement to add.
 * @return A new transform equal to *this post-translated by \a delta.
 */
BAffineTransform
BAffineTransform::TranslateByCopy(const BPoint& delta) const
{
	return TranslateByCopy(delta.x, delta.y);
}


// #pragma mark -


/**
 * @brief Post-rotate this transform around an arbitrary center point.
 *
 * Temporarily translates the origin to \a center, applies the rotation,
 * then translates back.
 *
 * @param center The pivot point for the rotation.
 * @param angle  Rotation angle in radians (counter-clockwise).
 * @return A const reference to this transform after modification.
 */
const BAffineTransform&
BAffineTransform::RotateBy(const BPoint& center, double angle)
{
	TranslateBy(-center.x, -center.y);
	RotateBy(angle);
	return TranslateBy(center.x, center.y);
}


/**
 * @brief Return a rotated copy of this transform around the origin.
 *
 * @param angle Rotation angle in radians (counter-clockwise).
 * @return A new transform equal to *this post-rotated by \a angle.
 */
BAffineTransform
BAffineTransform::RotateByCopy(double angle) const
{
	BAffineTransform copy(*this);
	copy.RotateBy(angle);
	return copy;
}


/**
 * @brief Return a rotated copy of this transform around an arbitrary center point.
 *
 * @param center The pivot point for the rotation.
 * @param angle  Rotation angle in radians (counter-clockwise).
 * @return A new transform equal to *this post-rotated around \a center by \a angle.
 */
BAffineTransform
BAffineTransform::RotateByCopy(const BPoint& center, double angle) const
{
	BAffineTransform copy(*this);
	copy.RotateBy(center, angle);
	return copy;
}


// #pragma mark -


/**
 * @brief Post-scale this transform uniformly around an arbitrary center point.
 *
 * @param center The fixed point around which scaling is applied.
 * @param scale  Uniform scale factor for both axes.
 * @return A const reference to this transform after modification.
 */
const BAffineTransform&
BAffineTransform::ScaleBy(const BPoint& center, double scale)
{
	return ScaleBy(center, scale, scale);
}


/**
 * @brief Post-scale this transform non-uniformly around an arbitrary center point.
 *
 * @param center The fixed point around which scaling is applied.
 * @param x      Scale factor along the X axis.
 * @param y      Scale factor along the Y axis.
 * @return A const reference to this transform after modification.
 */
const BAffineTransform&
BAffineTransform::ScaleBy(const BPoint& center, double x, double y)
{
	TranslateBy(-center.x, -center.y);
	ScaleBy(x, y);
	return TranslateBy(center.x, center.y);
}


/**
 * @brief Post-scale this transform using a BPoint as the (sx, sy) scale vector.
 *
 * @param scale A point whose x and y components are the scale factors.
 * @return A const reference to this transform after modification.
 */
const BAffineTransform&
BAffineTransform::ScaleBy(const BPoint& scale)
{
	return ScaleBy(scale.x, scale.y);
}


/**
 * @brief Post-scale this transform around a center point using a BPoint scale vector.
 *
 * @param center The fixed point around which scaling is applied.
 * @param scale  A point whose x and y components are the scale factors.
 * @return A const reference to this transform after modification.
 */
const BAffineTransform&
BAffineTransform::ScaleBy(const BPoint& center, const BPoint& scale)
{
	return ScaleBy(center, scale.x, scale.y);
}


/**
 * @brief Return a uniformly scaled copy of this transform.
 *
 * @param scale Uniform scale factor.
 * @return A new transform equal to *this post-scaled by \a scale on both axes.
 */
BAffineTransform
BAffineTransform::ScaleByCopy(double scale) const
{
	return ScaleByCopy(scale, scale);
}


/**
 * @brief Return a uniformly scaled copy of this transform around an arbitrary center.
 *
 * @param center The fixed point around which scaling is applied.
 * @param scale  Uniform scale factor.
 * @return A new transform equal to *this post-scaled uniformly around \a center.
 */
BAffineTransform
BAffineTransform::ScaleByCopy(const BPoint& center, double scale) const
{
	return ScaleByCopy(center, scale, scale);
}


/**
 * @brief Return a non-uniformly scaled copy of this transform.
 *
 * @param x Scale factor along the X axis.
 * @param y Scale factor along the Y axis.
 * @return A new transform equal to *this post-scaled by (x, y).
 */
BAffineTransform
BAffineTransform::ScaleByCopy(double x, double y) const
{
	BAffineTransform copy(*this);
	copy.ScaleBy(x, y);
	return copy;
}


/**
 * @brief Return a non-uniformly scaled copy of this transform around an arbitrary center.
 *
 * @param center The fixed point around which scaling is applied.
 * @param x      Scale factor along the X axis.
 * @param y      Scale factor along the Y axis.
 * @return A new transform equal to *this post-scaled by (x, y) around \a center.
 */
BAffineTransform
BAffineTransform::ScaleByCopy(const BPoint& center, double x, double y) const
{
	BAffineTransform copy(*this);
	copy.ScaleBy(center, x, y);
	return copy;
}


/**
 * @brief Return a copy scaled by a BPoint scale vector.
 *
 * @param scale A point whose x and y components are the scale factors.
 * @return A new transform equal to *this post-scaled by \a scale.
 */
BAffineTransform
BAffineTransform::ScaleByCopy(const BPoint& scale) const
{
	return ScaleByCopy(scale.x, scale.y);
}


/**
 * @brief Return a copy scaled around a center point using a BPoint scale vector.
 *
 * @param center The fixed point around which scaling is applied.
 * @param scale  A point whose x and y components are the scale factors.
 * @return A new transform equal to *this post-scaled by \a scale around \a center.
 */
BAffineTransform
BAffineTransform::ScaleByCopy(const BPoint& center, const BPoint& scale) const
{
	return ScaleByCopy(center, scale.x, scale.y);
}


/**
 * @brief Set the overall uniform scale while preserving rotation and shear.
 *
 * Decomposes the current transform into its constituent parameters, replaces the
 * scale with a uniform \a scale value, and recomposes the matrix.
 *
 * @param scale The desired uniform scale factor.
 * @return A const reference to this transform after modification.
 */
const BAffineTransform&
BAffineTransform::SetScale(double scale)
{
	return SetScale(scale, scale);
}


/**
 * @brief Set the overall non-uniform scale while preserving rotation and shear.
 *
 * Decomposes the current transform, replaces the X and Y scale factors, and
 * recomposes the matrix.
 *
 * @param x Desired scale factor along the X axis.
 * @param y Desired scale factor along the Y axis.
 * @return A const reference to this transform after modification.
 */
const BAffineTransform&
BAffineTransform::SetScale(double x, double y)
{
	double tx;
	double ty;
	double rotation;
	double shearX;
	double shearY;
	if (!GetAffineParameters(&tx, &ty, &rotation, NULL, NULL,
			&shearX, &shearY)) {
		return *this;
	}

	BAffineTransform result;
	result.ShearBy(shearX, shearY);
	result.ScaleBy(x, y);
	result.RotateBy(rotation);
	result.TranslateBy(tx, ty);

	return *this = result;
}


// #pragma mark -


/**
 * @brief Post-shear this transform around an arbitrary center point.
 *
 * @param center The fixed point around which the shear is applied.
 * @param x      X-axis shear angle in radians.
 * @param y      Y-axis shear angle in radians.
 * @return A const reference to this transform after modification.
 */
const BAffineTransform&
BAffineTransform::ShearBy(const BPoint& center, double x, double y)
{
	TranslateBy(-center.x, -center.y);
	ShearBy(x, y);
	return TranslateBy(center.x, center.y);
}


/**
 * @brief Post-shear this transform using a BPoint as the (shx, shy) shear vector.
 *
 * @param shear A point whose x and y components are the shear angles (in radians).
 * @return A const reference to this transform after modification.
 */
const BAffineTransform&
BAffineTransform::ShearBy(const BPoint& shear)
{
	return ShearBy(shear.x, shear.y);
}


/**
 * @brief Post-shear this transform around a center point using a BPoint shear vector.
 *
 * @param center The fixed point around which the shear is applied.
 * @param shear  A point whose x and y components are the shear angles (in radians).
 * @return A const reference to this transform after modification.
 */
const BAffineTransform&
BAffineTransform::ShearBy(const BPoint& center, const BPoint& shear)
{
	return ShearBy(center, shear.x, shear.y);
}


/**
 * @brief Return a sheared copy of this transform.
 *
 * @param x X-axis shear angle in radians.
 * @param y Y-axis shear angle in radians.
 * @return A new transform equal to *this post-sheared by (x, y).
 */
BAffineTransform
BAffineTransform::ShearByCopy(double x, double y) const
{
	BAffineTransform copy(*this);
	copy.ShearBy(x, y);
	return copy;
}


/**
 * @brief Return a sheared copy of this transform around an arbitrary center point.
 *
 * @param center The fixed point around which the shear is applied.
 * @param x      X-axis shear angle in radians.
 * @param y      Y-axis shear angle in radians.
 * @return A new transform equal to *this post-sheared by (x, y) around \a center.
 */
BAffineTransform
BAffineTransform::ShearByCopy(const BPoint& center, double x, double y) const
{
	BAffineTransform copy(*this);
	copy.ShearBy(center, x, y);
	return copy;
}


/**
 * @brief Return a sheared copy of this transform using a BPoint shear vector.
 *
 * @param shear A point whose x and y components are the shear angles (in radians).
 * @return A new transform equal to *this post-sheared by \a shear.
 */
BAffineTransform
BAffineTransform::ShearByCopy(const BPoint& shear) const
{
	BAffineTransform copy(*this);
	copy.ShearBy(shear);
	return copy;
}


/**
 * @brief Return a sheared copy around a center point using a BPoint shear vector.
 *
 * @param center The fixed point around which the shear is applied.
 * @param shear  A point whose x and y components are the shear angles (in radians).
 * @return A new transform equal to *this post-sheared by \a shear around \a center.
 */
BAffineTransform
BAffineTransform::ShearByCopy(const BPoint& center, const BPoint& shear) const
{
	BAffineTransform copy(*this);
	copy.ShearBy(center, shear);
	return copy;
}


// #pragma mark -


/**
 * @brief Pre-multiply this transform by another, i.e. compute other * this.
 *
 * Post-multiplying appends a transform on the right; pre-multiplying prepends
 * it on the left. Use this when \a other should be applied before \a *this in
 * the transformation pipeline.
 *
 * @param other The transform to prepend.
 * @return A const reference to this transform after modification.
 */
const BAffineTransform&
BAffineTransform::PreMultiply(const BAffineTransform& other)
{
	double t0 = sx * other.sx + shy * other.shx;
	double t2 = shx * other.sx + sy * other.shx;
	double t4 = tx * other.sx + ty * other.shx + other.tx;
	shy = sx * other.shy + shy * other.sy;
	sy = shx * other.shy + sy * other.sy;
	ty = tx * other.shy + ty * other.sy + other.ty;
	sx = t0;
	shx = t2;
	tx = t4;
	return *this;
}


/**
 * @brief Test whether this transform has non-degenerate scale components.
 *
 * A transform is considered valid when both scale components exceed \a epsilon
 * in absolute value, guaranteeing that the matrix is invertible.
 *
 * @param epsilon Tolerance below which a scale component is considered zero.
 * @return true if the transform can be inverted, false if it is degenerate.
 */
bool
BAffineTransform::IsValid(double epsilon) const
{
	return fabs(sx) > epsilon && fabs(sy) > epsilon;
}


/**
 * @brief Test whether two doubles are equal within a tolerance.
 *
 * @param v1      First value.
 * @param v2      Second value.
 * @param epsilon Maximum allowed absolute difference for equality.
 * @return true if |v1 - v2| <= epsilon.
 */
static inline bool
IsEqualEpsilon(double v1, double v2, double epsilon)
{
    return fabs(v1 - v2) <= double(epsilon);
}


/**
 * @brief Test whether this transform is the identity, within a tolerance.
 *
 * @param epsilon Maximum allowed deviation of each component from its identity
 *                value (1.0 for sx/sy, 0.0 for all others).
 * @return true if every component is within \a epsilon of the identity value.
 */
bool
BAffineTransform::IsIdentity(double epsilon) const
{
	return IsEqualEpsilon(sx, 1.0, epsilon)
		&& IsEqualEpsilon(shy, 0.0, epsilon)
		&& IsEqualEpsilon(shx, 0.0, epsilon)
		&& IsEqualEpsilon(sy, 1.0, epsilon)
		&& IsEqualEpsilon(tx, 0.0, epsilon)
		&& IsEqualEpsilon(ty, 0.0, epsilon);
}


/**
 * @brief Test whether this transform is a dilation (scale only, no shear).
 *
 * @param epsilon Tolerance for comparing shear components to zero.
 * @return true if both shear components are within \a epsilon of zero.
 */
bool
BAffineTransform::IsDilation(double epsilon) const
{
	return IsEqualEpsilon(shy, 0.0, epsilon)
		&& IsEqualEpsilon(shx, 0.0, epsilon);
}


/**
 * @brief Test whether this transform is component-wise equal to another.
 *
 * @param other   The transform to compare against.
 * @param epsilon Maximum allowed per-component absolute difference.
 * @return true if every matrix component differs by at most \a epsilon.
 */
bool
BAffineTransform::IsEqual(const BAffineTransform& other, double epsilon) const
{
	return IsEqualEpsilon(sx, other.sx, epsilon)
		&& IsEqualEpsilon(shy, other.shy, epsilon)
		&& IsEqualEpsilon(shx, other.shx, epsilon)
		&& IsEqualEpsilon(sy, other.sy, epsilon)
		&& IsEqualEpsilon(tx, other.tx, epsilon)
		&& IsEqualEpsilon(ty, other.ty, epsilon);
}


/**
 * @brief Invert this transform in place.
 *
 * Replaces the matrix with its inverse using the standard 2x2 adjugate formula
 * extended to the translation components. The result is undefined if
 * IsValid() returns false (degenerate matrix).
 *
 * @return A const reference to this transform after inversion.
 * @see IsValid()
 */
const BAffineTransform&
BAffineTransform::Invert()
{
	double d  = InverseDeterminant();

	double t0 = sy * d;
	sy =  sx * d;
	shy = -shy * d;
	shx = -shx * d;

	double t4 = -tx * t0 - ty * shx;
	ty = -tx * shy - ty * sy;

	sx = t0;
	tx = t4;

    return *this;
}


/**
 * @brief Flip this transform along the X axis (negate the X components).
 *
 * @return A const reference to this transform after the flip.
 */
const BAffineTransform&
BAffineTransform::FlipX()
{
	sx = -sx;
	shy = -shy;
	tx = -tx;
	return *this;
}


/**
 * @brief Flip this transform along the Y axis (negate the Y components).
 *
 * @return A const reference to this transform after the flip.
 */
const BAffineTransform&
BAffineTransform::FlipY()
{
	shx = -shx;
	sy = -sy;
	ty = -ty;
	return *this;
}


/**
 * @brief Reset this transform to the identity.
 *
 * Sets sx = sy = 1.0 and all other components to 0.0.
 *
 * @return A const reference to this transform after the reset.
 */
const BAffineTransform&
BAffineTransform::Reset()
{
	sx = sy = 1.0;
	shy = shx = tx = ty = 0.0;
	return *this;
}


/**
 * @brief Retrieve the translation components of this transform.
 *
 * @param _tx Output parameter for the X translation; may be NULL.
 * @param _ty Output parameter for the Y translation; may be NULL.
 */
void
BAffineTransform::GetTranslation(double* _tx, double* _ty) const
{
	if (_tx)
		*_tx = tx;
	if (_ty)
		*_ty = ty;
}


/**
 * @brief Compute the net rotation angle encoded in this transform.
 *
 * Transforms two reference points to determine the angle of the rotated X axis.
 *
 * @return The rotation angle in radians (counter-clockwise from positive X).
 */
double
BAffineTransform::Rotation() const
{
	double x1 = 0.0;
	double y1 = 0.0;
	double x2 = 1.0;
	double y2 = 0.0;
	Apply(&x1, &y1);
	Apply(&x2, &y2);
	return atan2(y2 - y1, x2 - x1);
}


/**
 * @brief Compute the average scale magnitude of this transform.
 *
 * Uses a weighted combination of the matrix components to approximate the
 * geometric mean scale factor without a full decomposition.
 *
 * @return The approximate uniform scale factor.
 */
double
BAffineTransform::Scale() const
{
	double x = 0.707106781 * sx + 0.707106781 * shx;
	double y = 0.707106781 * shy + 0.707106781 * sy;
	return sqrt(x * x + y * y);
}


/**
 * @brief Retrieve the per-axis scale factors by decomposing the transform.
 *
 * Removes the rotation component and then measures how much the unit X and Y
 * vectors are stretched. More accurate than reading sx/sy directly when shear
 * is present.
 *
 * @param _sx Output parameter for the X scale factor; may be NULL.
 * @param _sy Output parameter for the Y scale factor; may be NULL.
 */
void
BAffineTransform::GetScale(double* _sx, double* _sy) const
{
	double x1 = 0.0;
	double y1 = 0.0;
	double x2 = 1.0;
	double y2 = 1.0;
	BAffineTransform t(*this);
	t.PreMultiply(AffineRotation(-Rotation()));
	t.Apply(&x1, &y1);
	t.Apply(&x2, &y2);
	if (_sx)
		*_sx = x2 - x1;
	if (_sy)
		*_sy = y2 - y1;
}


/**
 * @brief Retrieve the per-axis scale magnitudes using the column-norm method.
 *
 * Computes the Euclidean lengths of the two matrix columns, which gives a
 * reliable scale estimate even under significant shear.
 *
 * @param _sx Output parameter for the X scale magnitude; may be NULL.
 * @param _sy Output parameter for the Y scale magnitude; may be NULL.
 */
void
BAffineTransform::GetScaleAbs(double* _sx, double* _sy) const
{
	// When there is considerable shear this method gives us much
	// better estimation than just sx, sy.
	if (_sx)
		*_sx = sqrt(sx * sx + shx * shx);
	if (_sy)
		*_sy = sqrt(shy * shy + sy * sy);
}


/**
 * @brief Decompose this transform into its constituent affine parameters.
 *
 * Extracts the translation, rotation angle, per-axis scale, and per-axis shear
 * by progressively removing each effect and measuring the remainder.
 *
 * @param _translationX Output X translation; may be NULL.
 * @param _translationY Output Y translation; may be NULL.
 * @param _rotation     Output rotation angle in radians; may be NULL.
 * @param _scaleX       Output X scale factor; may be NULL.
 * @param _scaleY       Output Y scale factor; may be NULL.
 * @param _shearX       Output X shear (normalised by scaleX); may be NULL.
 * @param _shearY       Output Y shear (normalised by scaleY); may be NULL.
 * @return true on success; false if the matrix is degenerate (zero scale).
 */
bool
BAffineTransform::GetAffineParameters(double* _translationX,
	double* _translationY, double* _rotation, double* _scaleX, double* _scaleY,
	double* _shearX, double* _shearY) const
{
	GetTranslation(_translationX, _translationY);

	double rotation = Rotation();
	if (_rotation != NULL)
		*_rotation = rotation;

	// Calculate shear
	double x1 = 0.0;
	double y1 = 0.0;
	double x2 = 1.0;
	double y2 = 0.0;
	double x3 = 0.0;
	double y3 = 1.0;

	// Reverse the effects of any rotation
	BAffineTransform t(*this);
	t.PreMultiply(AffineRotation(-rotation));

	t.Apply(&x1, &y1);
	t.Apply(&x2, &y2);
	t.Apply(&x3, &y3);

	double shearX = y2 - y1;
	double shearY = x3 - x1;

	// Calculate scale
	x1 = 0.0;
	y1 = 0.0;
	x2 = 1.0;
	y2 = 0.0;
	x3 = 0.0;
	y3 = 1.0;

	// Reverse the effects of any shear
	t.PreMultiplyInverse(AffineShearing(shearX, shearY));

	t.Apply(&x1, &y1);
	t.Apply(&x2, &y2);
	t.Apply(&x3, &y3);

	double scaleX = x2 - x1;
	double scaleY = y3 - y1;

	if (_scaleX != NULL)
		*_scaleX = scaleX;
	if (_scaleY != NULL)
		*_scaleY = scaleY;

	// Since scale was calculated last, the shear values are still scaled.
	// We cannot get the affine parameters from a matrix with 0 scale.
	if (scaleX == 0.0 && scaleY == 0.0)
		return false;

	if (_shearX != NULL)
		*_shearX = shearX / scaleX;
	if (_shearY != NULL)
		*_shearY = shearY / scaleY;

	return true;
}
