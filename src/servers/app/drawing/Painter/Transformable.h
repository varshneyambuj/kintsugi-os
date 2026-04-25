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
 * MIT License. Copyright 2005, Stephan Aßmus.
 */

/** @file Transformable.h
    @brief Archivable affine transform that wraps agg::trans_affine and adds
           BPoint/BRect helpers used by Painter and the interface kit. */

#ifndef TRANSFORMABLE_H
#define TRANSFORMABLE_H

#include <Archivable.h>
#include <Rect.h>

#include <agg_trans_affine.h>


/**
 * @brief BArchivable affine transform built on agg::trans_affine.
 *
 * Combines BeOS-style BPoint/BRect transformation helpers with the AGG
 * 2x3 matrix used by the Painter pipeline. Notifies subclasses through
 * @ref TransformationChanged() whenever the matrix is mutated.
 */
class Transformable : public BArchivable,
					  public agg::trans_affine {
 public:
								Transformable();
								Transformable(const Transformable& other);
								Transformable(const BMessage* archive);
	virtual						~Transformable();

								// the BArchivable protocol
								// stores matrix directly to message, deep is ignored
	virtual	status_t			Archive(BMessage* into, bool deep = true) const;

			void				StoreTo(double matrix[6]) const;
			void				LoadFrom(double matrix[6]);

								// set to or combine with other matrix
			void				SetTransformable(const Transformable& other);
			Transformable&		operator=(const agg::trans_affine& other);
			Transformable&		operator=(const Transformable& other);
			Transformable&		Multiply(const Transformable& other);
			void				Reset();

			bool				IsIdentity() const;
			bool				IsDilation() const;
//			bool				operator==(const Transformable& other) const;
//			bool				operator!=(const Transformable& other) const;

								// transforms coordiantes
			void				Transform(double* x, double* y) const;
			void				Transform(BPoint* point) const;
			BPoint				Transform(const BPoint& point) const;

			void				InverseTransform(double* x, double* y) const;
			void				InverseTransform(BPoint* point) const;
			BPoint				InverseTransform(const BPoint& point) const;

								// transforms the rectangle "bounds" and
								// returns the *bounding box* of that
			BRect				TransformBounds(const BRect& bounds) const;

			bool				IsTranslationOnly() const;

								// some convenience functions
	virtual	void				TranslateBy(BPoint offset);
	virtual	void				RotateBy(BPoint origin, double radians);
	virtual	void				ScaleBy(BPoint origin, double xScale, double yScale);
	virtual	void				ShearBy(BPoint origin, double xShear, double yShear);

	/** @brief Hook invoked after any mutation; default implementation is a no-op. */
	virtual	void				TransformationChanged() {}
};

#endif // TRANSFORMABLE_H

