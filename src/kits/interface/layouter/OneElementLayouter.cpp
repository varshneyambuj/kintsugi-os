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
 *   Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file OneElementLayouter.cpp
 * @brief Optimised Layouter implementation for the single-element case.
 *
 * When a layout axis contains only one visible element, the full
 * ComplexLayouter machinery is unnecessary. OneElementLayouter tracks
 * the tightest min/max/preferred bounds collected across all AddConstraints()
 * calls and assigns the given size directly to the sole element.
 *
 * @see Layouter, SimpleLayouter, ComplexLayouter
 */


#include "OneElementLayouter.h"

#include <Size.h>


/**
 * @brief Internal LayoutInfo for a single-element layout.
 *
 * Stores the single size value assigned during Layout() and answers all
 * geometric queries for element 0.
 */
class OneElementLayouter::MyLayoutInfo : public LayoutInfo {
public:
	/** @brief Pixel size assigned to the one element. */
	float	fSize;

	/**
	 * @brief Constructs the layout info with size initialised to zero.
	 */
	MyLayoutInfo()
		: fSize(0)
	{
	}

	/**
	 * @brief Returns the pixel offset of the element (always 0).
	 *
	 * @param element Element index (must be 0).
	 * @return 0.
	 */
	virtual float ElementLocation(int32 element)
	{
		return 0;
	}

	/**
	 * @brief Returns the assigned pixel size of the element.
	 *
	 * @param element Element index (must be 0).
	 * @return The size stored by the last Layout() call.
	 */
	virtual float ElementSize(int32 element)
	{
		return fSize;
	}
};


/**
 * @brief Constructs a OneElementLayouter with unlimited max and no preferred.
 */
OneElementLayouter::OneElementLayouter()
	: fMin(-1),
	  fMax(B_SIZE_UNLIMITED),
	  fPreferred(-1)
{
}

/**
 * @brief Destroys the OneElementLayouter.
 */
OneElementLayouter::~OneElementLayouter()
{
}

/**
 * @brief Tightens the min/max/preferred bounds with new constraint values.
 *
 * The element and length parameters are accepted but ignored because there is
 * only one element.
 *
 * @param element   Ignored.
 * @param length    Ignored.
 * @param min       New minimum size candidate.
 * @param max       New maximum size candidate.
 * @param preferred New preferred size candidate.
 */
void
OneElementLayouter::AddConstraints(int32 element, int32 length,
	float min, float max, float preferred)
{
	fMin = max_c(fMin, min);
	fMax = min_c(fMax, max);
	fMax = max_c(fMax, fMin);
	fPreferred = max_c(fPreferred, preferred);
	fPreferred = max_c(fPreferred, fMin);
	fPreferred = min_c(fPreferred, fMax);
}

/**
 * @brief No-op: weight has no meaning for a single-element layout.
 *
 * @param element Ignored.
 * @param weight  Ignored.
 */
void
OneElementLayouter::SetWeight(int32 element, float weight)
{
	// not needed
}

/**
 * @brief Returns the minimum size for the single element.
 *
 * @return Minimum size in pixels.
 */
float
OneElementLayouter::MinSize()
{
	return fMin;
}

/**
 * @brief Returns the maximum size for the single element.
 *
 * @return Maximum size in pixels.
 */
float
OneElementLayouter::MaxSize()
{
	return fMax;
}

/**
 * @brief Returns the preferred size for the single element.
 *
 * @return Preferred size in pixels.
 */
float
OneElementLayouter::PreferredSize()
{
	return fPreferred;
}

/**
 * @brief Allocates a LayoutInfo for the single-element layout.
 *
 * @return A new MyLayoutInfo with size initialised to zero.
 */
LayoutInfo*
OneElementLayouter::CreateLayoutInfo()
{
	return new MyLayoutInfo;
}

/**
 * @brief Assigns the available size to the single element.
 *
 * The size is clamped to at least fMin; no upper clamping is applied here.
 *
 * @param layoutInfo LayoutInfo created by CreateLayoutInfo().
 * @param size       Available size in pixels.
 */
void
OneElementLayouter::Layout(LayoutInfo* layoutInfo, float size)
{
	((MyLayoutInfo*)layoutInfo)->fSize = max_c(size, fMin);
}

/**
 * @brief Creates an independent copy of this layouter with the same bounds.
 *
 * @return A new OneElementLayouter with identical min/max/preferred values.
 */
Layouter*
OneElementLayouter::CloneLayouter()
{
	OneElementLayouter* layouter = new OneElementLayouter;
	layouter->fMin = fMin;
	layouter->fMax = fMax;
	layouter->fPreferred = fPreferred;

	return layouter;
}
