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
 *   Copyright 2006-2011, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */
#ifndef SERVER_MEMORY_ALLOCATOR_H
#define SERVER_MEMORY_ALLOCATOR_H


#include <map>

#include <OS.h>


namespace BPrivate {


struct area_mapping {
	int32	reference_count;
	area_id	server_area;
	area_id local_area;
	uint8*	local_base;
};


class ServerMemoryAllocator {
public:
								ServerMemoryAllocator();
								~ServerMemoryAllocator();

			status_t			InitCheck();

			status_t			AddArea(area_id serverArea, area_id& _localArea,
									uint8*& _base, size_t size,
									bool readOnly = false);
			void				RemoveArea(area_id serverArea);

private:
			std::map<area_id, area_mapping>
								fAreas;
};


}	// namespace BPrivate


#endif	// SERVER_MEMORY_ALLOCATOR_H
