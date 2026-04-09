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
 * Copyright 2005, Haiku, Inc.
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file ServerTokenSpace.h
 *  @brief Global token space and well-known token-type constants for server objects. */

#ifndef	SERVER_TOKEN_SPACE_H
#define	SERVER_TOKEN_SPACE_H


#include <TokenSpace.h>


using BPrivate::BTokenSpace;

/** @brief Token type constant for cursor objects. */
const int32 kCursorToken = 3;

/** @brief Token type constant for bitmap objects. */
const int32 kBitmapToken = 4;

/** @brief Token type constant for picture objects. */
const int32 kPictureToken = 5;

/** @brief Token type constant for remote drawing engine objects. */
const int32 kRemoteDrawingEngineToken = 6;


/** @brief Global token space shared across all server objects. */
extern BTokenSpace gTokenSpace;

#endif	/* SERVER_TOKEN_SPACE_H */
