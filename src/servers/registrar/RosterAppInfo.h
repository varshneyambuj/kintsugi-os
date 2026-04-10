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

/** @file RosterAppInfo.h
 *  @brief Extended app_info structure with pre-registration state, token, and timestamp. */
#ifndef ROSTER_APP_INFO_H
#define ROSTER_APP_INFO_H

#include <Roster.h>

enum application_state {
	APP_STATE_UNREGISTERED,
	APP_STATE_PRE_REGISTERED,
	APP_STATE_REGISTERED,
};


/** @brief Extends app_info with registration state, a pre-registration token, and a timestamp. */
struct RosterAppInfo : app_info {
	application_state	state;	/**< Current registration state (unregistered, pre-registered, or registered). */
	uint32				token;	/**< Unique token assigned during pre-registration when team is unknown. */
		// token is meaningful only if state is APP_STATE_PRE_REGISTERED and
		// team is -1.
	bigtime_t			registration_time;	/**< Timestamp of when this entry was first added to the roster. */

	RosterAppInfo();
	/** @brief Initializes the struct fields with the given application metadata. */
	void Init(thread_id thread, team_id team, port_id port, uint32 flags,
		const entry_ref *ref, const char *signature);

	/** @brief Creates a heap-allocated copy of this RosterAppInfo. */
	RosterAppInfo *Clone() const;
	/** @brief Returns whether the application's team is valid and running. */
	bool IsRunning() const;
};

#endif	// ROSTER_APP_INFO_H
