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
 *   Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file VMPageQueue.cpp
 * @brief Intrusive doubly-linked queue of vm_page objects used for page state
 *        management (free, clear, modified, etc.).
 *
 * @see vm_page
 */

#include "VMPageQueue.h"


// #pragma mark - VMPageQueue


/**
 * @brief Initialise the queue to an empty, named state.
 *
 * Placement-constructs the intrusive page list, initialises the spinlock that
 * serialises concurrent access, stores the human-readable @p name, and resets
 * the page counter to zero.
 *
 * @param name  A short descriptive label for the queue (e.g. "free", "clear",
 *              "modified"). The pointer is stored directly — the caller must
 *              ensure the string outlives the queue.
 */
void
VMPageQueue::Init(const char* name)
{
	new(&fPages) PageList;

	B_INITIALIZE_SPINLOCK(&fLock);

	fName = name;
	fCount = 0;
}
