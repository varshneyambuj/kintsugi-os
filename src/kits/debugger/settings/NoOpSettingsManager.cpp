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
 * @file NoOpSettingsManager.cpp
 * @brief Stub SettingsManager that ignores all save/load requests.
 *
 * NoOpSettingsManager is used in environments where persistent settings
 * storage is not desired (test harnesses, ephemeral debugging sessions).
 * Loads succeed with an empty settings object and saves are silently
 * dropped.
 */


#include "NoOpSettingsManager.h"


/**
 * @brief Construct a no-op settings manager.
 */
NoOpSettingsManager::NoOpSettingsManager()
	:
	SettingsManager()
{
}


/**
 * @brief Destructor.
 */
NoOpSettingsManager::~NoOpSettingsManager()
{
}


/**
 * @brief Pretends to load settings for the named team.
 *
 * Always succeeds without modifying @a settings.
 *
 * @param teamName  Ignored. Identifier of the team whose settings would
 *                  normally be loaded.
 * @param settings  Out: left untouched.
 * @return Always @c B_OK.
 */
status_t
NoOpSettingsManager::LoadTeamSettings(const char* teamName,
	TeamSettings& settings)
{
	return B_OK;
}


/**
 * @brief Pretends to persist team settings.
 *
 * Always succeeds without writing anywhere.
 *
 * @param settings  Ignored. Settings that would normally be persisted.
 * @return Always @c B_OK.
 */
status_t
NoOpSettingsManager::SaveTeamSettings(const TeamSettings& settings)
{
	return B_OK;
}



