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
 *   Copyright 2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file KernelReferenceable.cpp
 * @brief Reference-counted base class with interrupt-aware destruction.
 */


#include <util/KernelReferenceable.h>

#include <interrupts.h>


/**
 * @brief Destroy the object when its final reference drops.
 *
 * If interrupts are currently enabled the object is deleted inline; otherwise
 * the delete is queued via deferred_delete() because ::operator delete may
 * acquire the kernel heap lock and must not run with interrupts disabled.
 */
void
KernelReferenceable::LastReferenceReleased()
{
	if (are_interrupts_enabled())
		delete this;
	else
		deferred_delete(this);
}
