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
 * Copyright (c) 2001-2002, Haiku, Inc.
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file SystemPalette.h
 *  @brief Functions to generate and access the default BeOS/Kintsugi system palette. */

#ifndef _SYSTEM_PALETTE_H_
#define _SYSTEM_PALETTE_H_

#include <GraphicsDefs.h>

/** @brief Initialises the global system color map; must be called once at startup. */
extern void InitializeColorMap();

/** @brief Returns a pointer to the 256-entry system color palette.
 *  @return Pointer to an array of 256 rgb_color values. */
extern const rgb_color *SystemPalette();

/** @brief Returns a pointer to the full system color map including inverse lookup.
 *  @return Pointer to the color_map structure. */
extern const color_map *SystemColorMap();

#endif
