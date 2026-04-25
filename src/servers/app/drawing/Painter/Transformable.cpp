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
 *   Copyright 2005, Stephan Aßmus <superstippi@gmx.de>. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   A handy front-end to agg::trans_affine transformation matrix.
 */


/**
 * @file Transformable.cpp
 * @brief Archivable affine transform built on top of agg::trans_affine.
 *
 * Implements Transformable, the BArchivable wrapper used by the app_server
 * Painter and BAffineTransform-aware code paths in the interface kit. Adds
 * BPoint/BRect helpers, identity/translation-only detection, and a
 * TransformationChanged() notification hook used by subclasses (such as
 * BView state) to invalidate cached pipelines when the matrix mutates.
 *
 * @see Painter, BAffineTransform
 */


#include <stdio.h>
#include <string.h>

#include <Message.h>

#include "Transformable.h"


/**
 * @brief Returns the smallest of four floats.
 *
 * @param a First value.
 * @param b Second value.
 * @param c Third value.
 * @param d Fourth value.
 * @return The minimum of @a a, @a b, @a c and @a d.
 */
inline float
min4(float a, float b, float c, float d)
{
	return min_c(a, min_c(b, min_c(c, d)));
}


/**
 * @brief Returns the largest of four floats.
 *
 * @param a First value.
 * @param b Second value.
 * @param c Third value.
 * @param d Fourth value.
 * @return The maximum of @a a, @a b, @a c and @a d.
 */
inline float
max4(float a, float b, float c, float d)
{
	return max_c(a, max_c(b, max_c(c, d)));
}


/**
 * @brief Constructs an identity Transformable.
 */
Transformable::Transformable()
	: agg::trans_affine()
{
}


/**
 * @brief Copy constructor; clones the underlying agg::trans_affine matrix.
 *
 * @param other Source transform to copy.
 */
Transformable::Transformable(const Transformable& other)
	: agg::trans_affine(other)
{
}


/**
 * @brief Constructs from an archived BMessage previously written by Archive().
 *
 * Reads the six "affine matrix" doubles in declaration order. If any field is
 * missing the matrix is left at identity.
 *
 * @param archive Source archive, may be NULL.
 */
Transformable::Transformable(const BMessage* archive)
	: agg::trans_affine()
{
	if (archive != NULL) {
		double storage[6];
		status_t ret = B_OK;
		for (int32 i = 0; i < 6; i++) {
			ret = archive->FindDouble("affine matrix", i, &storage[i]);
			if (ret < B_OK)
				break;
		}
		if (ret >= B_OK)
			load_from(storage);
	}
}


/**
 * @brief Destructor.
 */
Transformable::~Transformable()
{
}


/**
 * @brief Serializes the matrix into a BMessage as six "affine matrix" doubles.
 *
 * @param into Destination archive; must be non-NULL.
 * @param deep Ignored; the matrix has no children to archive recursively.
 * @return B_OK on success, or a status code propagated from BArchivable or
 *         BMessage on failure.
 */
status_t
Transformable::Archive(BMessage* into, bool deep) const
{
	status_t ret = BArchivable::Archive(into, deep);
	if (ret == B_OK) {
		double storage[6];
		store_to(storage);
		for (int32 i = 0; i < 6; i++) {
			ret = into->AddDouble("affine matrix", storage[i]);
			if (ret < B_OK)
				break;
		}
		// finish off
		if (ret == B_OK)
			ret = into->AddString("class", "Transformable");
	}
	return ret;
}


/**
 * @brief Copies the six matrix coefficients into a caller-supplied array.
 *
 * @param matrix Destination array of at least six doubles.
 */
void
Transformable::StoreTo(double matrix[6]) const
{
	store_to(matrix);
}


/**
 * @brief Replaces the matrix from a six-double array.
 *
 * Performs a guarded comparison so TransformationChanged() is only invoked
 * when the new matrix actually differs from the current one.
 *
 * @param matrix Source array of six matrix coefficients.
 */
void
Transformable::LoadFrom(double matrix[6])
{
	// Before calling the potentially heavy TransformationChanged()
	// hook function, we make sure that it is actually true
	Transformable t;
	t.load_from(matrix);
	if (*this != t) {
		load_from(matrix);
		TransformationChanged();
	}
}


/**
 * @brief Replaces this matrix with @a other and notifies subclasses if it changed.
 *
 * @param other Source transform to copy from.
 */
void
Transformable::SetTransformable(const Transformable& other)
{
	if (*this != other) {
		*this = other;
		TransformationChanged();
	}
}


/**
 * @brief Assignment from another Transformable.
 *
 * @param other Source transform.
 * @return Reference to *this.
 * @note Triggers TransformationChanged() when the matrices differ.
 */
Transformable&
Transformable::operator=(const Transformable& other)
{
	if (other != *this) {
		agg::trans_affine::operator=(other);
		TransformationChanged();
	}
	return *this;
}


/**
 * @brief Assignment from a raw agg::trans_affine.
 *
 * @param other Source AGG transform.
 * @return Reference to *this.
 * @note Triggers TransformationChanged() when the matrices differ.
 */
Transformable&
Transformable::operator=(const agg::trans_affine& other)
{
	if (other != *this) {
		agg::trans_affine::operator=(other);
		TransformationChanged();
	}
	return *this;
}


/**
 * @brief Concatenates @a other onto this matrix in place.
 *
 * Identity inputs are skipped to avoid spurious change notifications.
 *
 * @param other Right-hand side transform.
 * @return Reference to *this.
 */
Transformable&
Transformable::Multiply(const Transformable& other)
{
	if (!other.IsIdentity()) {
		multiply(other);
		TransformationChanged();
	}
	return *this;
}


/**
 * @brief Resets the matrix to the identity transform.
 */
void
Transformable::Reset()
{
	reset();
}


/**
 * @brief Tests whether the current matrix is the identity transform.
 *
 * @retval true  Matrix equals identity (1,0,0,1,0,0).
 * @retval false Otherwise.
 */
bool
Transformable::IsIdentity() const
{
	double m[6];
	store_to(m);
	if (m[0] == 1.0 &&
		m[1] == 0.0 &&
		m[2] == 0.0 &&
		m[3] == 1.0 &&
		m[4] == 0.0 &&
		m[5] == 0.0)
		return true;
	return false;
}


/**
 * @brief Tests whether the matrix is a pure dilation (scale + translate).
 *
 * @retval true  Off-diagonal shear/rotation entries are zero.
 * @retval false Matrix has rotation or shear.
 */
bool
Transformable::IsDilation() const
{
	double m[6];
	store_to(m);
	return m[1] == 0.0 && m[2] == 0.0;
}


/**
 * @brief Transforms a coordinate pair in-place by this matrix.
 *
 * @param x Pointer to x-coordinate; updated in place.
 * @param y Pointer to y-coordinate; updated in place.
 */
void
Transformable::Transform(double* x, double* y) const
{
	transform(x, y);
}


/**
 * @brief Transforms a BPoint in-place.
 *
 * @param point Pointer to point to transform; ignored if NULL.
 */
void
Transformable::Transform(BPoint* point) const
{
	if (point) {
		double x = point->x;
		double y = point->y;

		transform(&x, &y);

		point->x = x;
		point->y = y;
	}
}


/**
 * @brief Transforms a point and returns the result by value.
 *
 * @param point Source point.
 * @return Transformed copy of @a point.
 */
BPoint
Transformable::Transform(const BPoint& point) const
{
	BPoint p(point);
	Transform(&p);
	return p;
}


/**
 * @brief Applies the inverse transform to a coordinate pair in-place.
 *
 * @param x Pointer to x-coordinate; updated in place.
 * @param y Pointer to y-coordinate; updated in place.
 */
void
Transformable::InverseTransform(double* x, double* y) const
{
	inverse_transform(x, y);
}


/**
 * @brief Applies the inverse transform to a BPoint in-place.
 *
 * @param point Pointer to point to transform; ignored if NULL.
 */
void
Transformable::InverseTransform(BPoint* point) const
{
	if (point) {
		double x = point->x;
		double y = point->y;

		inverse_transform(&x, &y);

		point->x = x;
		point->y = y;
	}
}


/**
 * @brief Returns the inverse-transformed copy of a point.
 *
 * @param point Source point.
 * @return Inverse-transformed copy of @a point.
 */
BPoint
Transformable::InverseTransform(const BPoint& point) const
{
	BPoint p(point);
	InverseTransform(&p);
	return p;
}


/**
 * @brief Returns the axis-aligned bounding box of a transformed rectangle.
 *
 * Each of the four corners of @a bounds is transformed individually and the
 * enclosing AABB is returned, so rotated/sheared rectangles still get a
 * tight, integer-aligned bounding box.
 *
 * @param bounds Source rectangle in untransformed space.
 * @return Axis-aligned bounding box in transformed space, or @a bounds itself
 *         if it is invalid.
 */
BRect
Transformable::TransformBounds(const BRect& bounds) const
{
	if (bounds.IsValid()) {
		BPoint lt(bounds.left, bounds.top);
		BPoint rt(bounds.right, bounds.top);
		BPoint lb(bounds.left, bounds.bottom);
		BPoint rb(bounds.right, bounds.bottom);

		Transform(&lt);
		Transform(&rt);
		Transform(&lb);
		Transform(&rb);

		return BRect(floorf(min4(lt.x, rt.x, lb.x, rb.x)),
					 floorf(min4(lt.y, rt.y, lb.y, rb.y)),
					 ceilf(max4(lt.x, rt.x, lb.x, rb.x)),
					 ceilf(max4(lt.y, rt.y, lb.y, rb.y)));
	}
	return bounds;
}


/**
 * @brief Tests whether the matrix is translation only (no scale/rotate/shear).
 *
 * @retval true  The 2x2 linear part equals the identity.
 * @retval false There is a non-trivial linear component.
 */
bool
Transformable::IsTranslationOnly() const
{
	double matrix[6];
	store_to(matrix);
	return matrix[0] == 1.0 && matrix[1] == 0.0
		&& matrix[2] == 0.0 && matrix[3] == 1.0;
}



/**
 * @brief Concatenates a translation onto the current matrix.
 *
 * @param offset Translation vector; zero offsets are skipped.
 */
void
Transformable::TranslateBy(BPoint offset)
{
	if (offset.x != 0.0 || offset.y != 0.0) {
		multiply(agg::trans_affine_translation(offset.x, offset.y));
		TransformationChanged();
	}
}


/**
 * @brief Rotates the matrix by @a radians around @a origin.
 *
 * Implemented as translate-by-(-origin), rotate, translate-by-origin.
 *
 * @param origin  Pivot point.
 * @param radians Rotation angle in radians; zero is a no-op.
 */
void
Transformable::RotateBy(BPoint origin, double radians)
{
	if (radians != 0.0) {
		multiply(agg::trans_affine_translation(-origin.x, -origin.y));
		multiply(agg::trans_affine_rotation(radians));
		multiply(agg::trans_affine_translation(origin.x, origin.y));
		TransformationChanged();
	}
}


/**
 * @brief Scales around @a origin by independent x/y factors.
 *
 * @param origin Scaling pivot.
 * @param xScale Horizontal scale factor.
 * @param yScale Vertical scale factor.
 */
void
Transformable::ScaleBy(BPoint origin, double xScale, double yScale)
{
	if (xScale != 1.0 || yScale != 1.0) {
		multiply(agg::trans_affine_translation(-origin.x, -origin.y));
		multiply(agg::trans_affine_scaling(xScale, yScale));
		multiply(agg::trans_affine_translation(origin.x, origin.y));
		TransformationChanged();
	}
}


/**
 * @brief Shears around @a origin by independent x/y skew factors.
 *
 * @param origin Shear pivot.
 * @param xShear Horizontal skew factor.
 * @param yShear Vertical skew factor.
 */
void
Transformable::ShearBy(BPoint origin, double xShear, double yShear)
{
	if (xShear != 0.0 || yShear != 0.0) {
		multiply(agg::trans_affine_translation(-origin.x, -origin.y));
		multiply(agg::trans_affine_skewing(xShear, yShear));
		multiply(agg::trans_affine_translation(origin.x, origin.y));
		TransformationChanged();
	}
}
