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
 *   Copyright 2006, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */
#ifndef SERVER_READ_ONLY_MEMORY_H
#define SERVER_READ_ONLY_MEMORY_H


#include <GraphicsDefs.h>
#include <InterfaceDefs.h>


// Update kColorWhichLastContinuous with the largest color constant which
// leaves no gaps in the color_which integer values.
static const int32 kColorWhichLastContinuous = B_STATUS_BAR_COLOR;
static const int32 kColorWhichCount = kColorWhichLastContinuous + 3;
	// + 1 for index-offset, + 2 for B_SUCCESS_COLOR, B_FAILURE_COLOR


struct server_read_only_memory {
	color_map	colormap;
	rgb_color	colors[kColorWhichCount];
};


static inline int32
color_which_to_index(color_which which)
{
	if (which <= kColorWhichCount - 3)
		return which - 1;
	if (which >= B_SUCCESS_COLOR && which <= B_FAILURE_COLOR)
		return which - B_SUCCESS_COLOR + kColorWhichCount - 3;

	return -1;
}


static inline color_which
index_to_color_which(int32 index)
{
	if (index >= 0 && index < kColorWhichCount) {
		if ((color_which)index < kColorWhichCount - 3)
			return (color_which)(index + 1);
		else {
			return (color_which)(index + B_SUCCESS_COLOR
				- kColorWhichCount + 3);
		}
	}

	return (color_which)-1;
}

#endif	/* SERVER_READ_ONLY_MEMORY_H */
