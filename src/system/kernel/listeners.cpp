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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file listeners.cpp
 *  @brief Global registry of wait-object listeners notified when threads block. */

#include <listeners.h>


/** @brief Global list of registered wait object listeners. */
WaitObjectListenerList gWaitObjectListeners;
/** @brief Reader/writer spinlock guarding @c gWaitObjectListeners. */
rw_spinlock gWaitObjectListenerLock = B_RW_SPINLOCK_INITIALIZER;


WaitObjectListener::~WaitObjectListener()
{
}


/** @brief Adds @p listener to the global list.
 *
 * @c gWaitObjectListenerLock must be held in write mode. */
void
add_wait_object_listener(struct WaitObjectListener* listener)
{
	gWaitObjectListeners.Add(listener);
}


/** @brief Removes @p listener from the global list.
 *
 * @c gWaitObjectListenerLock must be held in write mode. */
void
remove_wait_object_listener(struct WaitObjectListener* listener)
{
	gWaitObjectListeners.Remove(listener);
}
