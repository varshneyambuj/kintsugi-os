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
 *   Copyright 2006 Ingo Weinhold. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */


/**
 * @file Size.cpp
 * @brief Implementation of BSize, a 2D width/height dimension
 *
 * BSize stores width and height float values used by the layout system for
 * size constraints. It is fully implemented inline in the header; this file
 * exists to satisfy the linker for any out-of-line symbols.
 *
 * @see BLayoutItem, BPoint, BRect
 */


#include <Size.h>

/** @brief Sentinel value indicating an unset (unconstrained) size dimension. */
// B_SIZE_UNSET = -2  (defined in Size.h)

/** @brief Effectively-infinite upper bound for layout size constraints. */
// B_SIZE_UNLIMITED = 1024*1024*1024  (defined in Size.h)

// All BSize methods (Width(), Height(), Set(), SetWidth(), SetHeight(),
// IntegerWidth(), IntegerHeight(), IsWidthSet(), IsHeightSet(),
// operator==(), operator!=(), operator=(), and all constructors) are
// implemented inline in <Size.h> and require no out-of-line definitions.
