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
 * @file TeamInfo.cpp
 * @brief Implementation of TeamInfo, the lightweight identifier-and-args
 *        descriptor for a team known to the debugger.
 *
 * TeamInfo is used by the team selection and target-host enumeration
 * paths before the heavy-weight Team object is created; it carries the
 * kernel team id and the command-line argument string.
 */


#include "TeamInfo.h"


/**
 * @brief Constructs an empty TeamInfo with invalid team id and no arguments.
 */
TeamInfo::TeamInfo()
	:
	fTeam(-1),
	fArguments()
{
}


/**
 * @brief Copy-constructs from another TeamInfo.
 *
 * @param other Source instance to copy.
 */
TeamInfo::TeamInfo(const TeamInfo &other)
{
	fTeam = other.fTeam;
	fArguments = other.fArguments;
}


/**
 * @brief Constructs a TeamInfo from a kernel team_info structure.
 *
 * @param team Team identifier.
 * @param info Kernel team_info whose @c args field is captured.
 */
TeamInfo::TeamInfo(team_id team, const team_info& info)
{
	SetTo(team, info);
}


/**
 * @brief Replaces the team id and arguments from a kernel team_info.
 *
 * @param team Team identifier.
 * @param info Kernel team_info whose @c args field is captured.
 */
void
TeamInfo::SetTo(team_id team, const team_info& info)
{
	fTeam = team;
	fArguments.SetTo(info.args);
}


/**
 * @brief Replaces the team id and arguments from a string.
 *
 * @param team      Team identifier.
 * @param arguments Replacement command-line argument string.
 */
void
TeamInfo::SetTo(team_id team, const BString& arguments)
{
	fTeam = team;
	fArguments.SetTo(arguments);
}
