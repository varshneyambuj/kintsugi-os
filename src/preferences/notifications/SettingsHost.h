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
 * MIT License. Copyright 2010-2017, Haiku, Inc.
 */

/** @file SettingsHost.h
    @brief Tiny callback interface implemented by the preflet window so
           individual SettingsPane instances can report local edits. */

#ifndef _SETTINGS_HOST_H
#define _SETTINGS_HOST_H

#include <vector>

#include "SettingsPane.h"


/**
 * @brief Pure-virtual host interface for SettingsPane callbacks.
 *
 * The hosting window (PrefletWin) implements SettingChanged so each pane
 * can post Apply (with or without a sample notification) when the user
 * edits a control.
 */
class SettingsHost {
public:
	/** @brief Trivial default constructor. */
					SettingsHost() {}

	/**
	 * @brief Notifies the host that pane state changed.
	 *
	 * @param showExample  When true the host should also fire a sample
	 *                     notification so the user can preview the result.
	 */
	virtual	void	SettingChanged(bool showExample = false) = 0;
};

#endif // _SETTINGS_HOST_H
