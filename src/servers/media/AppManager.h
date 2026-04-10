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
 *   Copyright 2002, Marcus Overhagen. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file AppManager.h
 *  @brief Manages registration and lifecycle of media client applications. */

#ifndef APP_MANAGER_H
#define APP_MANAGER_H


#include <map>

#include <Locker.h>
#include <Messenger.h>


/** @brief Tracks registered media client application teams and their messengers. */
class AppManager : BLocker {
public:
	/** @brief Construct the application manager. */
								AppManager();
	/** @brief Destroy the application manager. */
								~AppManager();

	/** @brief Register a team with its messenger for message delivery. */
			status_t			RegisterTeam(team_id team,
									const BMessenger& messenger);
	/** @brief Unregister a team and clean up its resources. */
			status_t			UnregisterTeam(team_id team);
	/** @brief Check whether a given team is currently registered. */
			bool				HasTeam(team_id team);

	/** @brief Return the team_id of the media_addon_server. */
			team_id				AddOnServerTeam();

	/** @brief Send a message to the specified registered team. */
			status_t			SendMessage(team_id team, BMessage* message);

	/** @brief Dump the list of registered applications to stdout. */
			void				Dump();

	/** @brief Notify all registered roster messengers that the server is alive. */
			void				NotifyRosters();

private:
			void				_CleanupTeam(team_id team);

private:
			typedef std::map<team_id, BMessenger> AppMap;

			AppMap				fMap;
};


#endif // APP_MANAGER_H
