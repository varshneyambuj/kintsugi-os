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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2003-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       Stefano Ceccherini, burton666@libero.it
 */


/**
 * @file Region.cpp
 * @brief Implementation of BRegion, a union of non-overlapping rectangles
 *
 * BRegion stores an arbitrary screen region as a list of non-overlapping BRect tiles.
 * It supports union, intersection, exclusion, and offset operations, and is used by
 * the clipping system to track visible areas of views.
 *
 * @see BRect, BView
 */


#include <Region.h>

#include <stdlib.h>
#include <string.h>

#include <Debug.h>

#include "clipping.h"
#include "RegionSupport.h"


/** @brief Default allocation granularity for the internal rectangle array. */
const static int32 kDataBlockSize = 8;


/** @brief Construct an empty BRegion with a pre-allocated rectangle buffer.
 *
 *  Initialises the region to contain zero rectangles. Internal storage is
 *  pre-allocated to kDataBlockSize slots so that small regions avoid
 *  immediate reallocation.
 *
 *  @see MakeEmpty()
 */
BRegion::BRegion()
	:
	fCount(0),
	fDataSize(0),
	fBounds((clipping_rect){ 0, 0, 0, 0 }),
	fData(NULL)
{
	_SetSize(kDataBlockSize);
}


/** @brief Copy-construct a BRegion from another BRegion.
 *  @param other The source region to copy.
 *
 *  Delegates to operator=() after initialising member variables to safe
 *  default values.
 *
 *  @see operator=()
 */
BRegion::BRegion(const BRegion& other)
	:
	fCount(0),
	fDataSize(0),
	fBounds((clipping_rect){ 0, 0, 0, 0 }),
	fData(NULL)
{
	*this = other;
}


/** @brief Construct a BRegion from a single BRect.
 *  @param rect The rectangle that defines the initial region.
 *
 *  If \a rect is invalid the region is left empty. Otherwise the rectangle
 *  is converted to internal clipping format and stored directly in fBounds,
 *  avoiding a heap allocation for single-rectangle regions.
 *
 *  @see Set()
 */
BRegion::BRegion(const BRect rect)
	:
	fCount(0),
	fDataSize(1),
	fBounds((clipping_rect){ 0, 0, 0, 0 }),
	fData(&fBounds)
{
	if (!rect.IsValid())
		return;

	fBounds = _ConvertToInternal(rect);
	fCount = 1;
}


#if defined(__cplusplus) && __cplusplus >= 201103L
/** @brief Move-construct a BRegion from an rvalue BRegion (C++11).
 *  @param other The source region to move; left empty after the operation.
 *
 *  Transfers ownership of the internal rectangle array without copying.
 *
 *  @see MoveFrom()
 */
BRegion::BRegion(BRegion&& other)
	:
	fCount(0),
	fDataSize(0),
	fBounds((clipping_rect){ 0, 0, 0, 0 }),
	fData(NULL)
{
	MoveFrom(other);
}
#endif


// NOTE: private constructor
/** @brief Construct a single-rectangle BRegion directly from a clipping_rect.
 *  @param clipping The internal-format rectangle; stored in fBounds without
 *                  conversion. This constructor is private and avoids malloc().
 */
BRegion::BRegion(const clipping_rect& clipping)
	:
	fCount(1),
	fDataSize(1),
	fBounds(clipping),
	fData(&fBounds)
{
}


/** @brief Destroy the BRegion and release its heap-allocated rectangle array.
 *
 *  When fData points to fBounds (single-rect optimisation) no heap memory is
 *  freed; otherwise the dynamically allocated array is passed to free().
 */
BRegion::~BRegion()
{
	if (fData != &fBounds)
		free(fData);
}


/** @brief Copy-assign another BRegion to this one.
 *  @param other The source region to copy.
 *  @return A reference to this region.
 *
 *  Grows the internal rectangle array if necessary to accommodate all of
 *  \a other's rectangles, then copies them with memcpy.
 *
 *  @see _SetSize()
 */
BRegion&
BRegion::operator=(const BRegion& other)
{
	if (&other == this)
		return *this;

	// handle reallocation if we're too small to contain the other's data
	if (_SetSize(other.fDataSize)) {
		memcpy(fData, other.fData, other.fCount * sizeof(clipping_rect));

		fBounds = other.fBounds;
		fCount = other.fCount;
	}

	return *this;
}


#if defined(__cplusplus) && __cplusplus >= 201103L
/** @brief Move-assign an rvalue BRegion to this one (C++11).
 *  @param other The source region to move; left empty after the operation.
 *  @return A reference to this region.
 *  @see MoveFrom()
 */
BRegion&
BRegion::operator=(BRegion&& other)
{
	MoveFrom(other);

	return *this;
}
#endif


/** @brief Test whether two BRegions contain exactly the same set of rectangles.
 *  @param other The region to compare against.
 *  @return true if both regions have the same rectangle count and identical
 *          rectangle data; false otherwise.
 */
bool
BRegion::operator==(const BRegion& other) const
{
	if (&other == this)
		return true;

	if (fCount != other.fCount)
		return false;

	return memcmp(fData, other.fData, fCount * sizeof(clipping_rect)) == 0;
}


/** @brief Replace the region's contents with a single BRect.
 *  @param rect The new bounding rectangle; if invalid, MakeEmpty() is called.
 *  @see MakeEmpty()
 */
void
BRegion::Set(BRect rect)
{
	Set(_Convert(rect));
}


/** @brief Replace the region's contents with a single clipping_rect.
 *  @param clipping The new rectangle in external clipping format; if invalid,
 *                  MakeEmpty() is called.
 *  @see MakeEmpty()
 */
void
BRegion::Set(clipping_rect clipping)
{
	_SetSize(1);

	if (valid_rect(clipping) && fData != NULL) {
		fCount = 1;
		fData[0] = fBounds = _ConvertToInternal(clipping);
	} else
		MakeEmpty();
}


/** @brief Transfer all data from \a other into this region, leaving \a other empty.
 *
 *  For single-rectangle sources, copies via Set() and then empties \a other.
 *  For multi-rectangle sources, takes direct ownership of \a other's heap
 *  allocation and clears \a other's pointers to prevent double-free.
 *
 *  @param other The source region to move data from.
 *  @see operator=(BRegion&&)
 */
void
BRegion::MoveFrom(BRegion& other)
{
	if (other.CountRects() <= 0) {
		MakeEmpty();
		return;
	}
	if (other.CountRects() == 1) {
		Set(other.FrameInt());
		other.MakeEmpty();
		return;
	}
	fCount = other.fCount;
	fDataSize = other.fDataSize;
	fBounds = other.fBounds;
	fData = other.fData;

	other.fCount = 0;
	other.fDataSize = 0;
	other.fBounds = (clipping_rect){ 0, 0, 0, 0 };
	other.fData = NULL;
}


/** @brief Return the bounding rectangle of the entire region as a BRect.
 *  @return The smallest BRect enclosing all rectangles in the region, or an
 *          empty BRect if the region is empty.
 *  @see FrameInt()
 */
BRect
BRegion::Frame() const
{
	return BRect(fBounds.left, fBounds.top,
		fBounds.right - 1, fBounds.bottom - 1);
}


/** @brief Return the bounding rectangle of the entire region as a clipping_rect.
 *  @return The smallest clipping_rect enclosing all rectangles in the region.
 *  @see Frame()
 */
clipping_rect
BRegion::FrameInt() const
{
	return (clipping_rect){ fBounds.left, fBounds.top,
		fBounds.right - 1, fBounds.bottom - 1 };
}


/** @brief Return the rectangle at \a index as a BRect (non-const overload).
 *  @param index Zero-based index into the internal rectangle list.
 *  @return The rectangle converted from internal format to BRect, or an
 *          invalid BRect if \a index is out of range.
 *  @see RectAt(int32) const
 */
BRect
BRegion::RectAt(int32 index)
{
	return const_cast<const BRegion*>(this)->RectAt(index);
}


/** @brief Return the rectangle at \a index as a BRect.
 *  @param index Zero-based index into the internal rectangle list.
 *  @return The rectangle converted from internal format to BRect, or an
 *          invalid BRect if \a index is out of range.
 *  @see CountRects()
 */
BRect
BRegion::RectAt(int32 index) const
{
	if (index >= 0 && index < fCount) {
		const clipping_rect& r = fData[index];
		return BRect(r.left, r.top, r.right - 1, r.bottom - 1);
	}

	return BRect();
		// an invalid BRect
}


/** @brief Return the rectangle at \a index as a clipping_rect (non-const overload).
 *  @param index Zero-based index into the internal rectangle list.
 *  @return The rectangle in external clipping format, or an invalid
 *          clipping_rect (left > right) if \a index is out of range.
 *  @see RectAtInt(int32) const
 */
clipping_rect
BRegion::RectAtInt(int32 index)
{
	return const_cast<const BRegion*>(this)->RectAtInt(index);
}


/** @brief Return the rectangle at \a index as a clipping_rect.
 *  @param index Zero-based index into the internal rectangle list.
 *  @return The rectangle in external clipping format, or an invalid
 *          clipping_rect (left > right) if \a index is out of range.
 *  @see CountRects()
 */
clipping_rect
BRegion::RectAtInt(int32 index) const
{
	if (index >= 0 && index < fCount) {
		const clipping_rect& r = fData[index];
		return (clipping_rect){ r.left, r.top, r.right - 1, r.bottom - 1 };
	}

	return (clipping_rect){ 1, 1, 0, 0 };
		// an invalid clipping_rect
}


/** @brief Return the number of rectangles that make up this region (non-const overload).
 *  @return The rectangle count; 0 for an empty region.
 */
int32
BRegion::CountRects()
{
	return fCount;
}


/** @brief Return the number of rectangles that make up this region.
 *  @return The rectangle count; 0 for an empty region.
 */
int32
BRegion::CountRects() const
{
	return fCount;
}


/** @brief Test whether a BRect overlaps this region.
 *  @param rect The rectangle to test in BRect coordinates.
 *  @return true if any part of \a rect lies within the region; false otherwise.
 *  @see Contains()
 */
bool
BRegion::Intersects(BRect rect) const
{
	return Intersects(_Convert(rect));
}


/** @brief Test whether a clipping_rect overlaps this region.
 *  @param clipping The rectangle to test in external clipping format.
 *  @return true if any part of \a clipping lies within the region; false
 *          otherwise.
 *  @see Contains()
 */
bool
BRegion::Intersects(clipping_rect clipping) const
{
	clipping = _ConvertToInternal(clipping);

	int result = Support::XRectInRegion(this, clipping);

	return result > Support::RectangleOut;
}


/** @brief Test whether a BPoint is inside the region.
 *  @param point The point to test.
 *  @return true if \a point lies within one of the region's rectangles.
 *  @see Intersects()
 */
bool
BRegion::Contains(BPoint point) const
{
	return Support::XPointInRegion(this, (int)point.x, (int)point.y);
}


/** @brief Test whether an integer coordinate pair is inside the region (non-const overload).
 *  @param x Horizontal coordinate to test.
 *  @param y Vertical coordinate to test.
 *  @return true if (x, y) lies within one of the region's rectangles.
 */
bool
BRegion::Contains(int32 x, int32 y)
{
	return Support::XPointInRegion(this, x, y);
}


/** @brief Test whether an integer coordinate pair is inside the region.
 *  @param x Horizontal coordinate to test.
 *  @param y Vertical coordinate to test.
 *  @return true if (x, y) lies within one of the region's rectangles.
 */
bool
BRegion::Contains(int32 x, int32 y) const
{
	return Support::XPointInRegion(this, x, y);
}


// Prints the BRegion to stdout.
/** @brief Print the region's bounding box and each constituent rectangle to stdout.
 *
 *  Calls Frame().PrintToStream() first, then iterates over all internal
 *  rectangles and prints each one in BRect notation.
 */
void
BRegion::PrintToStream() const
{
	Frame().PrintToStream();

	for (int32 i = 0; i < fCount; i++) {
		clipping_rect *rect = &fData[i];
		printf("data[%" B_PRId32 "] = BRect(l:%" B_PRId32 ".0, t:%" B_PRId32
			".0, r:%" B_PRId32 ".0, b:%" B_PRId32 ".0)\n",
			i, rect->left, rect->top, rect->right - 1, rect->bottom - 1);
	}
}


/** @brief Translate all rectangles in the region by a BPoint offset.
 *  @param point The offset to apply; both x and y components are used.
 *  @see OffsetBy(int32, int32)
 */
void
BRegion::OffsetBy(const BPoint& point)
{
	OffsetBy(point.x, point.y);
}


/** @brief Translate all rectangles in the region by (x, y).
 *  @param x Horizontal offset in pixels (may be negative).
 *  @param y Vertical offset in pixels (may be negative).
 *  @note If both \a x and \a y are zero, no work is done.
 */
void
BRegion::OffsetBy(int32 x, int32 y)
{
	if (x == 0 && y == 0)
		return;

	if (fCount > 0) {
		if (fData != &fBounds) {
			for (int32 i = 0; i < fCount; i++)
				offset_rect(fData[i], x, y);
		}

		offset_rect(fBounds, x, y);
	}
}


/** @brief Scale all rectangles in the region by a BSize factor.
 *  @param scale The scale factor; Width() is applied to x coordinates and
 *               Height() to y coordinates.
 *  @see ScaleBy(float, float)
 */
void
BRegion::ScaleBy(BSize scale)
{
	ScaleBy(scale.Width(), scale.Height());
}


/** @brief Scale all rectangles in the region by independent x and y factors.
 *  @param x Horizontal scale factor; 1.0 means no change.
 *  @param y Vertical scale factor; 1.0 means no change.
 *  @note If both \a x and \a y are 1.0, no work is done.
 */
void
BRegion::ScaleBy(float x, float y)
{
	if (x == 1.0 && y == 1.0)
		return;

	if (fCount > 0) {
		if (fData != &fBounds) {
			for (int32 i = 0; i < fCount; i++)
				scale_rect(fData[i], x, y);
		}

		scale_rect(fBounds, x, y);
	}
}


/** @brief Reset the region to contain no rectangles.
 *
 *  Sets fBounds to the zero clipping_rect and fCount to 0. Does not release
 *  the internal storage allocation so that subsequent Include() calls can
 *  reuse it without reallocating.
 */
void
BRegion::MakeEmpty()
{
	fBounds = (clipping_rect){ 0, 0, 0, 0 };
	fCount = 0;
}


/** @brief Add a BRect to the region, merging it into the existing area.
 *  @param rect The rectangle to add (in BRect coordinates).
 *  @see Include(clipping_rect)
 *  @see Include(const BRegion*)
 */
void
BRegion::Include(BRect rect)
{
	Include(_Convert(rect));
}


/** @brief Add a clipping_rect to the region, merging it into the existing area.
 *
 *  Converts \a clipping to internal format, wraps it in a temporary single-rect
 *  BRegion, and calls XUnionRegion() to compute the union. The result is
 *  adopted via _AdoptRegionData().
 *
 *  @param clipping The rectangle to add in external clipping format.
 *  @see _AdoptRegionData()
 */
void
BRegion::Include(clipping_rect clipping)
{
	if (!valid_rect(clipping))
		return;

	// convert to internal clipping format
	clipping.right++;
	clipping.bottom++;

	// use private clipping_rect constructor which avoids malloc()
	BRegion temp(clipping);

	BRegion result;
	Support::XUnionRegion(this, &temp, &result);

	_AdoptRegionData(result);
}


/** @brief Add all rectangles from another BRegion to this region.
 *  @param region The region whose area is unioned into this region.
 *  @see Include(BRect)
 */
void
BRegion::Include(const BRegion* region)
{
	BRegion result;
	Support::XUnionRegion(this, region, &result);

	_AdoptRegionData(result);
}


/** @brief Remove a BRect from the region.
 *  @param rect The rectangle to exclude (in BRect coordinates).
 *  @see Exclude(clipping_rect)
 *  @see Exclude(const BRegion*)
 */
void
BRegion::Exclude(BRect rect)
{
	Exclude(_Convert(rect));
}


/** @brief Remove a clipping_rect from the region.
 *
 *  Converts \a clipping to internal format, wraps it in a temporary single-rect
 *  BRegion, and calls XSubtractRegion() to compute the difference. The result
 *  is adopted via _AdoptRegionData().
 *
 *  @param clipping The rectangle to remove in external clipping format.
 *  @see _AdoptRegionData()
 */
void
BRegion::Exclude(clipping_rect clipping)
{
	if (!valid_rect(clipping))
		return;

	// convert to internal clipping format
	clipping.right++;
	clipping.bottom++;

	// use private clipping_rect constructor which avoids malloc()
	BRegion temp(clipping);

	BRegion result;
	Support::XSubtractRegion(this, &temp, &result);

	_AdoptRegionData(result);
}


/** @brief Remove all areas covered by another BRegion from this region.
 *  @param region The region whose area is subtracted from this region.
 *  @see Exclude(BRect)
 */
void
BRegion::Exclude(const BRegion* region)
{
	BRegion result;
	Support::XSubtractRegion(this, region, &result);

	_AdoptRegionData(result);
}


/** @brief Reduce this region to the intersection with another BRegion.
 *
 *  Computes the intersection of this region and \a region using
 *  XIntersectRegion() and replaces this region's data with the result.
 *
 *  @param region The region to intersect with.
 *  @see Include()
 *  @see Exclude()
 */
void
BRegion::IntersectWith(const BRegion* region)
{
	BRegion result;
	Support::XIntersectRegion(this, region, &result);

	_AdoptRegionData(result);
}


/** @brief Replace this region with the symmetric difference (XOR) of itself and another region.
 *
 *  The resulting region contains areas that are in either this region or
 *  \a region, but not in both.
 *
 *  @param region The region to XOR with.
 *  @see IntersectWith()
 */
void
BRegion::ExclusiveInclude(const BRegion* region)
{
	BRegion result;
	Support::XXorRegion(this, region, &result);

	_AdoptRegionData(result);
}


//	#pragma mark - BRegion private methods


/*!
	\fn void BRegion::_AdoptRegionData(BRegion& region)
	\brief Takes over the data of \a region and empties it.

	\param region The \a region to adopt data from.
*/
/** @brief Take ownership of the internal rectangle array from a temporary BRegion.
 *
 *  Frees the current heap allocation (if any), then steals \a region's data
 *  pointer, count, size, and bounds. Sets \a region's fData to NULL to prevent
 *  double-free when \a region is later destroyed.
 *
 *  @param region The source region; must be a locally-constructed temporary.
 *                It is left in an unusable (but destructible) state after the call.
 *  @note This function is only safe to call with internally allocated regions
 *        that do not need to remain in a valid state afterward.
 */
void
BRegion::_AdoptRegionData(BRegion& region)
{
	fCount = region.fCount;
	fDataSize = region.fDataSize;
	fBounds = region.fBounds;
	if (fData != &fBounds)
		free(fData);
	if (region.fData != &region.fBounds)
		fData = region.fData;
	else
		fData = &fBounds;

	// NOTE: MakeEmpty() is not called since _AdoptRegionData is only
	// called with internally allocated regions, so they don't need to
	// be left in a valid state.
	region.fData = NULL;
//	region.MakeEmpty();
}


/*!
	\fn bool BRegion::_SetSize(int32 newSize)
	\brief Reallocate the memory in the region.

	\param newSize The amount of rectangles that the region should be
		able to hold.
*/
/** @brief Grow the internal rectangle array to hold at least \a newSize rectangles.
 *
 *  Never shrinks the allocation. Rounds \a newSize up to the nearest multiple
 *  of kDataBlockSize before reallocating. Handles the fBounds-pointer
 *  optimisation by switching to a heap allocation when necessary.
 *
 *  @param newSize Minimum number of rectangles the array must accommodate.
 *  @return true if the array is large enough after the call; false if a
 *          heap allocation failed, in which case MakeEmpty() is called and
 *          fData is set to NULL.
 */
bool
BRegion::_SetSize(int32 newSize)
{
	// we never shrink the size
	newSize = max_c(fDataSize, newSize);
		// The amount of rectangles that the region should be able to hold.
	if (newSize == fDataSize)
		return true;

	// align newSize to multiple of kDataBlockSize
	newSize = ((newSize + kDataBlockSize - 1) / kDataBlockSize) * kDataBlockSize;

	if (newSize > 0) {
		if (fData == &fBounds) {
			fData = (clipping_rect*)malloc(newSize * sizeof(clipping_rect));
			fData[0] = fBounds;
		} else if (fData) {
			clipping_rect* resizedData = (clipping_rect*)realloc(fData,
				newSize * sizeof(clipping_rect));
			if (!resizedData) {
				// failed to resize, but we cannot keep the
				// previous state of the object
				free(fData);
				fData = NULL;
			} else
				fData = resizedData;
		} else
			fData = (clipping_rect*)malloc(newSize * sizeof(clipping_rect));
	} else {
		// just an empty region, but no error
		MakeEmpty();
		return true;
	}

	if (!fData) {
		// allocation actually failed
		fDataSize = 0;
		MakeEmpty();
		return false;
	}

	fDataSize = newSize;
	return true;
}


/** @brief Convert a BRect to a clipping_rect using floor/ceil rounding.
 *
 *  Left and top edges are rounded down (floor), right and bottom edges are
 *  rounded up (ceil), preserving the full pixel coverage of the BRect.
 *
 *  @param rect The BRect to convert.
 *  @return The corresponding clipping_rect in external (non-exclusive) format.
 *  @see _ConvertToInternal()
 */
clipping_rect
BRegion::_Convert(const BRect& rect) const
{
	return (clipping_rect){ (int)floorf(rect.left), (int)floorf(rect.top),
		(int)ceilf(rect.right), (int)ceilf(rect.bottom) };
}


/** @brief Convert a BRect to the internal exclusive-end clipping_rect format.
 *
 *  Left and top are rounded down (floor); right and bottom are rounded up
 *  (ceil) and then incremented by 1 to produce the exclusive right/bottom
 *  sentinel used internally.
 *
 *  @param rect The BRect to convert.
 *  @return The clipping_rect in internal exclusive-end format.
 *  @see _Convert()
 */
clipping_rect
BRegion::_ConvertToInternal(const BRect& rect) const
{
	return (clipping_rect){ (int)floorf(rect.left), (int)floorf(rect.top),
		(int)ceilf(rect.right) + 1, (int)ceilf(rect.bottom) + 1 };
}


/** @brief Convert an external clipping_rect to the internal exclusive-end format.
 *
 *  Increments right and bottom by 1 to produce the exclusive sentinel used by
 *  the X11-derived region algebra routines.
 *
 *  @param rect The clipping_rect in external format.
 *  @return The clipping_rect in internal exclusive-end format.
 *  @see _ConvertToInternal(const BRect&) const
 */
clipping_rect
BRegion::_ConvertToInternal(const clipping_rect& rect) const
{
	return (clipping_rect){ rect.left, rect.top,
		rect.right + 1, rect.bottom + 1 };
}
