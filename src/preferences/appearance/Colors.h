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
 * MIT License. Copyright 2001-2015, Haiku.
 * Original authors: DarkWyrm, Rene Gollent, Joseph Groover.
 */

/** @file Colors.h
    @brief Color descriptor table and palette accessors used by the Colors tab. */

#ifndef COLORS_H
#define COLORS_H


#include <InterfaceDefs.h>


/**
 * @brief Pairs a @c color_which slot with a translatable display label.
 */
typedef struct {
	color_which	which;
	const char*	text;
} ColorDescription;


const ColorDescription* get_color_description(int32 index);
int32 color_description_count(void);
void get_default_colors(BMessage* storage);
void get_current_colors(BMessage* storage);


#endif	// COLORS_H
