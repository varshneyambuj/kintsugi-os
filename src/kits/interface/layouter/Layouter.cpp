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
 * @file Layouter.cpp
 * @brief Base classes for the Kintsugi layout engine: LayoutInfo and Layouter.
 *
 * LayoutInfo is the result object produced by a Layouter after a layout pass;
 * it answers geometric queries (location and size) for each element.
 * Layouter is the abstract strategy interface that concrete implementations
 * (SimpleLayouter, ComplexLayouter, CollapsingLayouter, etc.) must satisfy.
 *
 * @see SimpleLayouter, ComplexLayouter, CollapsingLayouter
 */


#include "Layouter.h"


/**
 * @brief Constructs a LayoutInfo base object.
 */
LayoutInfo::LayoutInfo()
{
}

/**
 * @brief Destroys the LayoutInfo base object.
 */
LayoutInfo::~LayoutInfo()
{
}

/**
 * @brief Returns the combined pixel size of a contiguous range of elements.
 *
 * The default implementation sums individual element locations and sizes.
 * Subclasses may override this for more efficient range queries.
 *
 * @param position Index of the first element in the range.
 * @param length   Number of consecutive elements to include.
 * @return Combined size in pixels spanning from the start of @p position to
 *         the end of the last element.
 */
float
LayoutInfo::ElementRangeSize(int32 position, int32 length)
{
	if (length == 1)
		return ElementSize(position);

	int lastIndex = position + length - 1;
	return ElementLocation(lastIndex) + ElementSize(lastIndex)
		- ElementLocation(position);
}


// #pragma mark -


/**
 * @brief Constructs a Layouter base object.
 */
Layouter::Layouter()
{
}

/**
 * @brief Destroys the Layouter base object.
 */
Layouter::~Layouter()
{
}
