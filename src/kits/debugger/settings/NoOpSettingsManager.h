/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2009, Ingo Weinhold; Copyright 2016, Rene Gollent.
 */

/** @file NoOpSettingsManager.h
    @brief No-op SettingsManager used when persistence is not required. */

#ifndef NOOP_SETTINGS_MANAGER_H
#define NOOP_SETTINGS_MANAGER_H

#include "SettingsManager.h"


/**
 * @brief Stub SettingsManager whose Load/Save operations always succeed
 *        without doing any I/O.
 */
class NoOpSettingsManager : public SettingsManager {
public:
								NoOpSettingsManager();
	virtual						~NoOpSettingsManager();

	virtual	status_t			LoadTeamSettings(const char* teamName,
									TeamSettings& settings);
	virtual	status_t			SaveTeamSettings(const TeamSettings& settings);
};


#endif	// NOOP_SETTINGS_MANAGER_H
