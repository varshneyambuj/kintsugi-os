/*
 * Copyright 2025, Kintsugi OS Contributors.
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright (c) 2001-2015, Haiku, Inc.
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Adrien Destugues <pulkomandy@pulkomandy.tk>
 *		Julian Harnath <julian.harnath@rwth-aachen.de>
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file SimpleTransform.h
 *  @brief Lightweight 2-D scale-and-translate transform applied to drawing primitives. */

#ifndef SIMPLE_TRANSFORM_H
#define SIMPLE_TRANSFORM_H

#include <GradientLinear.h>
#include <GradientRadial.h>
#include <GradientRadialFocus.h>
#include <GradientDiamond.h>
#include <GradientConic.h>
#include <Point.h>
#include <Region.h>

#include "IntPoint.h"
#include "IntRect.h"


/** @brief Encapsulates a uniform scale and a 2-D pixel offset transform. */
class SimpleTransform {
public:
	/** @brief Constructs an identity transform (scale 1.0, zero offset). */
	SimpleTransform()
		:
		fScale(1.0)
	{
	}

	/** @brief Adds a pixel offset to the current translation.
	 *  @param x Horizontal offset to add.
	 *  @param y Vertical offset to add. */
	void AddOffset(float x, float y)
	{
		fOffset.x += x;
		fOffset.y += y;
	}

	/** @brief Sets the uniform scale factor.
	 *  @param scale New scale value. */
	void SetScale(float scale)
	{
		fScale = scale;
	}

	/** @brief Applies the transform to a BPoint in place.
	 *  @param point Point to transform. */
	void Apply(BPoint* point) const
	{
		_Apply(point->x, point->y);
	}

	/** @brief Applies the transform to an IntPoint in place.
	 *  @param point Integer point to transform. */
	void Apply(IntPoint* point) const
	{
		_Apply(point->x, point->y);
	}

	/** @brief Applies the transform to a BRect in place.
	 *  @param rect Rectangle to transform. */
	void Apply(BRect* rect) const
	{
		if (fScale == 1.0) {
			rect->OffsetBy(fOffset.x, fOffset.y);
		} else {
			_Apply(rect->left, rect->top);
			_Apply(rect->right, rect->bottom);
		}
	}

	/** @brief Applies the transform to an IntRect in place.
	 *  @param rect Integer rectangle to transform. */
	void Apply(IntRect* rect) const
	{
		if (fScale == 1.0) {
			rect->OffsetBy(fOffset.x, fOffset.y);
		} else {
			_Apply(rect->left, rect->top);
			_Apply(rect->right, rect->bottom);
		}
	}

	/** @brief Applies the transform to a BRegion in place.
	 *  @param region Region to transform. */
	void Apply(BRegion* region) const
	{
		if (fScale == 1.0) {
			region->OffsetBy(fOffset.x, fOffset.y);
		} else {
			// TODO: optimize some more
			BRegion converted;
			int32 count = region->CountRects();
			for (int32 i = 0; i < count; i++) {
				BRect r = region->RectAt(i);
				BPoint lt(r.LeftTop());
				BPoint rb(r.RightBottom());
				// offset to bottom right corner of pixel before transformation
				rb.x++;
				rb.y++;
				// apply transformation
				_Apply(lt.x, lt.y);
				_Apply(rb.x, rb.y);
				// reset bottom right to pixel "index"
				rb.x--;
				rb.y--;
				// add rect to converted region
				// NOTE/TODO: the rect would not have to go
				// through the whole intersection test process,
				// it is guaranteed not to overlap with any rect
				// already contained in the region
				converted.Include(BRect(lt, rb));
			}
			*region = converted;
		}
	}

	/** @brief Applies the transform to a BGradient's control points in place.
	 *  @param gradient Gradient whose control points are transformed. */
	void Apply(BGradient* gradient) const
	{
		switch (gradient->GetType()) {
			case BGradient::TYPE_LINEAR:
			{
				BGradientLinear* linear = (BGradientLinear*) gradient;
				BPoint start = linear->Start();
				BPoint end = linear->End();
				Apply(&start);
				Apply(&end);
				linear->SetStart(start);
				linear->SetEnd(end);
				break;
			}

			case BGradient::TYPE_RADIAL:
			{
				BGradientRadial* radial = (BGradientRadial*) gradient;
				BPoint center = radial->Center();
				Apply(&center);
				radial->SetCenter(center);
				break;
			}

			case BGradient::TYPE_RADIAL_FOCUS:
			{
				BGradientRadialFocus* radialFocus =
					(BGradientRadialFocus*)gradient;
				BPoint center = radialFocus->Center();
				BPoint focal = radialFocus->Focal();
				Apply(&center);
				Apply(&focal);
				radialFocus->SetCenter(center);
				radialFocus->SetFocal(focal);
				break;
			}

			case BGradient::TYPE_DIAMOND:
			{
				BGradientDiamond* diamond = (BGradientDiamond*) gradient;
				BPoint center = diamond->Center();
				Apply(&center);
				diamond->SetCenter(center);
				break;
			}

			case BGradient::TYPE_CONIC:
			{
				BGradientConic* conic = (BGradientConic*) gradient;
				BPoint center = conic->Center();
				Apply(&center);
				conic->SetCenter(center);
				break;
			}

			case BGradient::TYPE_NONE:
			{
				break;
			}
		}

		int32 colorStopsCount = gradient->CountColorStops();
		if (colorStopsCount == 0)
			return;

		// Make sure the gradient is fully padded so that out of bounds access
		// get the correct colors
		gradient->SortColorStopsByOffset();

		BGradient::ColorStop* end = gradient->ColorStopAtFast(
			gradient->CountColorStops() - 1);

		if (end->offset != 255)
			gradient->AddColor(end->color, 255);

		BGradient::ColorStop* start = gradient->ColorStopAtFast(0);

		if (start->offset != 0)
			gradient->AddColor(start->color, 0);

		gradient->SortColorStopsByOffset();
	}

	/** @brief Transforms an array of BPoints from source to destination.
	 *  @param destination Output array.
	 *  @param source      Input array.
	 *  @param count       Number of points. */
	void Apply(BPoint* destination, const BPoint* source, int32 count) const
	{
		// TODO: optimize this, it should be smarter
		while (count--) {
			*destination = *source;
			Apply(destination);
			source++;
			destination++;
		}
	}

	/** @brief Transforms an array of BRects from source to destination.
	 *  @param destination Output array.
	 *  @param source      Input array.
	 *  @param count       Number of rectangles. */
	void Apply(BRect* destination, const BRect* source, int32 count) const
	{
		// TODO: optimize this, it should be smarter
		while (count--) {
			*destination = *source;
			Apply(destination);
			source++;
			destination++;
		}
	}

	/** @brief Transforms an array of BRegions from source to destination.
	 *  @param destination Output array.
	 *  @param source      Input array.
	 *  @param count       Number of regions. */
	void Apply(BRegion* destination, const BRegion* source, int32 count) const
	{
		// TODO: optimize this, it should be smarter
		while (count--) {
			*destination = *source;
			Apply(destination);
			source++;
			destination++;
		}
	}

private:
	void _Apply(int32& x, int32& y) const
	{
		x *= (int32)fScale;
		y *= (int32)fScale;
		x += (int32)fOffset.x;
		y += (int32)fOffset.y;
	}

	void _Apply(float& x, float& y) const
	{
		x *= fScale;
		y *= fScale;
		x += fOffset.x;
		y += fOffset.y;
	}

private:
	BPoint	fOffset;
	float	fScale;
};


#endif // SIMPLE_TRANSFORM_H
