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
 *   Copyright 2005-2007, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file drawing_support.cpp
 * @brief Out-of-line drawing helpers used by Painter and the DrawingEngine.
 *
 * Most helpers in @c drawing_support.h are inline; this translation unit only
 * carries the slow-path geometry helpers that are not worth inlining.
 */


#include "drawing_support.h"

#include <Rect.h>


/**
 * @brief Snaps the corners of @a rect to the nearest integer pixel boundaries.
 *
 * The rectangle is offset to its rounded top-left corner first so that the
 * width and height after rounding match what the caller expects when drawing
 * pixel-aligned rectangles. Rounding is performed with @c roundf, which
 * minimises the average snap distance.
 *
 * @param rect Rectangle to align in place. Must not be NULL.
 */
void
align_rect_to_pixels(BRect* rect)
{
	// round the rect with the least ammount of distortion
	rect->OffsetTo(roundf(rect->left), roundf(rect->top));
	rect->right = roundf(rect->right);
	rect->bottom = roundf(rect->bottom);
}

