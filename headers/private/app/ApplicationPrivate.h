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
 *   Copyright 2001-2006, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */
#ifndef _APPLICATION_PRIVATE_H
#define _APPLICATION_PRIVATE_H


#include <Application.h>

struct server_read_only_memory;


class BApplication::Private {
	public:
		static inline BPrivate::PortLink *ServerLink()
			{ return be_app->fServerLink; }

		static inline BPrivate::ServerMemoryAllocator* ServerAllocator()
			{ return be_app->fServerAllocator; }
		
		static inline server_read_only_memory* ServerReadOnlyMemory()
			{ return (server_read_only_memory*)be_app->fServerReadOnlyMemory; }
};

#endif	// _APPLICATION_PRIVATE_H
