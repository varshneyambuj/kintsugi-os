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
   Copyright 2001-2007, Ingo Weinhold, bonefish@users.sf.net.
   Distributed under the terms of the MIT License.
 */

/** @file RosterAppInfo.cpp
 *  @brief Extended app_info structure with registration state and token tracking. */

#include "RosterAppInfo.h"

#include <new>
#include <string.h>

#include <Entry.h>
#include <SupportDefs.h>


using std::nothrow;


/**
 * @brief Constructs a RosterAppInfo in the unregistered state.
 *
 * All fields are zeroed and the state is set to APP_STATE_UNREGISTERED.
 */
RosterAppInfo::RosterAppInfo()
	: app_info(),
	state(APP_STATE_UNREGISTERED),
	token(0),
	registration_time(0)
{
}


/**
 * @brief Initializes the app info with the given process and entry details.
 *
 * Resolves the entry_ref through a BEntry to follow symlinks, then copies
 * the provided values into the corresponding fields.
 *
 * @param thread    The application's main thread ID.
 * @param team      The application's team ID.
 * @param port      The application's message port.
 * @param flags     Application launch flags.
 * @param ref       Entry ref pointing to the application executable.
 * @param signature The application's MIME signature, or NULL.
 */
void
RosterAppInfo::Init(thread_id thread, team_id team, port_id port, uint32 flags,
	const entry_ref *ref, const char *signature)
{
	this->thread = thread;
	this->team = team;
	this->port = port;
	this->flags = flags;
	BEntry entry(ref, true);
	if (entry.GetRef(&this->ref) != B_OK)
		this->ref = *ref;
	if (signature)
		strlcpy(this->signature, signature, B_MIME_TYPE_LENGTH);
	else
		this->signature[0] = '\0';
}


/**
 * @brief Creates a heap-allocated deep copy of this RosterAppInfo.
 *
 * The clone is initialized via Init() and also copies the registration_time.
 *
 * @return A new RosterAppInfo that is a copy of this object, or NULL if
 *         memory allocation fails.
 */
RosterAppInfo *
RosterAppInfo::Clone() const
{
	RosterAppInfo *clone = new(nothrow) RosterAppInfo;
	if (!clone)
		return NULL;

	clone->Init(thread, team, port, flags, &ref, signature);
	clone->registration_time = registration_time;
	return clone;
}


/**
 * @brief Checks whether the application's team is still alive.
 *
 * Queries the kernel for team_info to determine if the process is running.
 *
 * @return @c true if the team is still running, @c false otherwise.
 */
bool
RosterAppInfo::IsRunning() const
{
	team_info teamInfo;
	return get_team_info(team, &teamInfo) == B_OK;
}

