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
 *   Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file Target.cpp
 *  @brief Implements the launch target that groups related jobs under a named launch milestone. */


#include "Target.h"


/**
 * @brief Constructs a launch target with the given name.
 *
 * @param name The human-readable name of this target.
 */
Target::Target(const char* name)
	:
	BaseJob(name),
	fLaunched(false)
{
}


/**
 * @brief Stores a named BMessage data block within this target.
 *
 * @param name The key under which to store the data.
 * @param data The BMessage to add to the target's internal data store.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Target::AddData(const char* name, BMessage& data)
{
	return fData.AddMessage(name, &data);
}


/**
 * @brief Sets the launched state of this target.
 *
 * @param launched @c true to mark the target as launched, @c false otherwise.
 */
void
Target::SetLaunched(bool launched)
{
	fLaunched = launched;
}


/**
 * @brief Executes the target.
 *
 * Targets do not perform any work themselves; their purpose is to group
 * jobs under a named milestone. This always returns B_OK.
 *
 * @return B_OK unconditionally.
 */
status_t
Target::Execute()
{
	return B_OK;
}
