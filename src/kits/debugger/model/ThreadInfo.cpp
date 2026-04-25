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


/**
 * @file ThreadInfo.cpp
 * @brief Implementation of ThreadInfo, a value-type identifier for a
 *        thread observed by the debugger.
 *
 * ThreadInfo couples the thread's owning team, its kernel-assigned
 * thread id, and its display name. It is the lightweight handle passed
 * through enumeration and notification paths before a full Thread
 * object is constructed.
 */

#include "ThreadInfo.h"


/**
 * @brief Constructs an empty ThreadInfo with invalid team and thread ids.
 */
ThreadInfo::ThreadInfo()
	:
	fTeam(-1),
	fThread(-1),
	fName()
{
}


/**
 * @brief Copy-constructs from another ThreadInfo.
 *
 * @param other Source instance to copy.
 */
ThreadInfo::ThreadInfo(const ThreadInfo& other)
	:
	fTeam(other.fTeam),
	fThread(other.fThread),
	fName(other.fName)
{
}


/**
 * @brief Constructs a fully-populated ThreadInfo.
 *
 * @param team   Owning team identifier.
 * @param thread Kernel thread identifier.
 * @param name   Human-readable thread name.
 */
ThreadInfo::ThreadInfo(team_id team, thread_id thread, const BString& name)
	:
	fTeam(team),
	fThread(thread),
	fName(name)
{
}


/**
 * @brief Replaces all fields with new values.
 *
 * @param team   Owning team identifier.
 * @param thread Kernel thread identifier.
 * @param name   Human-readable thread name.
 */
void
ThreadInfo::SetTo(team_id team, thread_id thread, const BString& name)
{
	fTeam = team;
	fThread = thread;
	fName = name;
}
