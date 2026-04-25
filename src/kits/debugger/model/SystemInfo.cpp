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
 * @file SystemInfo.cpp
 * @brief Implementation of SystemInfo, a snapshot of host system metadata
 *        attached to a debugged team.
 *
 * SystemInfo bundles a team identifier with the kernel's @c system_info
 * structure and the @c utsname so the debugger UI can render the host
 * environment when reviewing a live or post-mortem session.
 */


#include "SystemInfo.h"


/**
 * @brief Constructs an empty SystemInfo with invalid team and zeroed structs.
 */
SystemInfo::SystemInfo()
	:
	fTeam(-1)
{
	memset(&fSystemInfo, 0, sizeof(system_info));
	memset(&fSystemName, 0, sizeof(utsname));
}


/**
 * @brief Copy-constructs from another SystemInfo via SetTo().
 *
 * @param other Source instance to copy.
 */
SystemInfo::SystemInfo(const SystemInfo &other)
{
	SetTo(other.fTeam, other.fSystemInfo, other.fSystemName);
}


/**
 * @brief Constructs a SystemInfo from explicit values.
 *
 * @param team Owning team identifier.
 * @param info Kernel system_info snapshot.
 * @param name uname()-style system name structure.
 */
SystemInfo::SystemInfo(team_id team, const system_info& info,
	const utsname& name)
{
	SetTo(team, info, name);
}


/**
 * @brief Replaces all fields with new values.
 *
 * @param team Owning team identifier.
 * @param info Kernel system_info snapshot to copy.
 * @param name uname()-style system name structure to copy.
 */
void
SystemInfo::SetTo(team_id team, const system_info& info, const utsname& name)
{
	fTeam = team;
	memcpy(&fSystemInfo, &info, sizeof(system_info));
	memcpy(&fSystemName, &name, sizeof(utsname));
}
