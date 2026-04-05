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
 *   Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file Alignment.cpp
 * @brief Implementation of BAlignment, a helper structure for layout alignment
 *
 * BAlignment encapsulates horizontal and vertical alignment values used by the
 * layout system to position items within their allocated space.
 *
 * @see BLayoutItem, BAbstractLayout
 */


#include <Alignment.h>

/**
 * @brief Return the horizontal alignment as a normalised [0, 1] fraction.
 *
 * Maps the symbolic @c alignment constants to floating-point values usable
 * by layout engines: @c B_ALIGN_LEFT → 0.0, @c B_ALIGN_RIGHT → 1.0, and
 * @c B_ALIGN_HORIZONTAL_CENTER → 0.5. Any other value is cast directly to
 * float, allowing numeric alignment constants to pass through unchanged.
 *
 * @return A float in the range [0, 1] representing the horizontal position,
 *         or the raw cast value for non-symbolic constants.
 * @see RelativeVertical()
 */
float
BAlignment::RelativeHorizontal() const
{
	switch (horizontal) {
		case B_ALIGN_LEFT:
			return 0;
		case B_ALIGN_RIGHT:
			return 1;
		case B_ALIGN_HORIZONTAL_CENTER:
			return 0.5f;

		default:
			return (float)horizontal;
	}
}

/**
 * @brief Return the vertical alignment as a normalised [0, 1] fraction.
 *
 * Maps the symbolic @c vertical_alignment constants to floating-point values
 * usable by layout engines: @c B_ALIGN_TOP → 0.0, @c B_ALIGN_BOTTOM → 1.0,
 * and @c B_ALIGN_VERTICAL_CENTER → 0.5. Any other value is cast directly to
 * float, allowing numeric alignment constants to pass through unchanged.
 *
 * @return A float in the range [0, 1] representing the vertical position,
 *         or the raw cast value for non-symbolic constants.
 * @see RelativeHorizontal()
 */
float
BAlignment::RelativeVertical() const
{
	switch (vertical) {
		case B_ALIGN_TOP:
			return 0;
		case B_ALIGN_BOTTOM:
			return 1;
		case B_ALIGN_VERTICAL_CENTER:
			return 0.5f;

		default:
			return (float)vertical;
	}
}
