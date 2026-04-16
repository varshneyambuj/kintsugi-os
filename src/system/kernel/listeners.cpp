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


/**
 * @brief Virtual destructor; derived listeners clean up their own state.
 */
WaitObjectListener::~WaitObjectListener()
{
}


/**
 * @brief Add @a listener to the global wait-object listener list.
 *
 * Caller must hold @c gWaitObjectListenerLock in write mode.
 *
 * @param listener Listener to insert.
 */
void
add_wait_object_listener(struct WaitObjectListener* listener)
{
	gWaitObjectListeners.Add(listener);
}


/**
 * @brief Remove @a listener from the global wait-object listener list.
 *
 * Caller must hold @c gWaitObjectListenerLock in write mode.
 *
 * @param listener Listener previously passed to add_wait_object_listener().
 */
void
remove_wait_object_listener(struct WaitObjectListener* listener)
{
	gWaitObjectListeners.Remove(listener);
}
