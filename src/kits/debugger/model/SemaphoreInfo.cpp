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
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file SemaphoreInfo.cpp
 * @brief Implementation of SemaphoreInfo, a value-type description of one
 *        kernel semaphore owned by a debugged team.
 *
 * SemaphoreInfo carries the team and semaphore identifiers, name, current
 * count, and the most recent acquiring thread, providing a snapshot for
 * the debugger's resource inspector.
 */


#include "SemaphoreInfo.h"


/**
 * @brief Constructs an empty SemaphoreInfo with invalid identifiers.
 */
SemaphoreInfo::SemaphoreInfo()
	:
	fTeam(-1),
	fSemaphore(-1),
	fName(),
	fCount(0),
	fLatestHolder(-1)
{
}


/**
 * @brief Copy-constructs from another SemaphoreInfo.
 *
 * @param other Source instance to copy.
 */
SemaphoreInfo::SemaphoreInfo(const SemaphoreInfo &other)
	:
	fTeam(other.fTeam),
	fSemaphore(other.fSemaphore),
	fName(other.fName),
	fCount(other.fCount),
	fLatestHolder(other.fLatestHolder)
{
}


/**
 * @brief Constructs a fully-populated SemaphoreInfo.
 *
 * @param team         Owning team identifier.
 * @param semaphore    Kernel semaphore identifier.
 * @param name         Human-readable semaphore name.
 * @param count        Current semaphore count.
 * @param latestHolder Most recent thread to acquire the semaphore.
 */
SemaphoreInfo::SemaphoreInfo(team_id team, sem_id semaphore,
	const BString& name, int32 count, thread_id latestHolder)
	:
	fTeam(team),
	fSemaphore(semaphore),
	fName(name),
	fCount(count),
	fLatestHolder(latestHolder)
{
}


/**
 * @brief Replaces all fields with new values.
 *
 * @param team         Owning team identifier.
 * @param semaphore    Kernel semaphore identifier.
 * @param name         Human-readable semaphore name.
 * @param count        Current semaphore count.
 * @param latestHolder Most recent thread to acquire the semaphore.
 */
void
SemaphoreInfo::SetTo(team_id team, sem_id semaphore, const BString& name,
	int32 count, thread_id latestHolder)
{
	fTeam = team;
	fSemaphore = semaphore;
	fName = name;
	fCount = count;
	fLatestHolder = latestHolder;
}
