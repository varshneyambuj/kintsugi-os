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
 * @file SettingsDescription.cpp
 * @brief Schema describing a collection of named Setting objects.
 *
 * SettingsDescription is the catalog backing the generic Settings store: it
 * owns references to Setting objects keyed by their stable id, and lets
 * Settings look up the right Setting by id when persisting or restoring
 * values.
 */


#include "SettingsDescription.h"

#include "Setting.h"


/**
 * @brief Construct an empty description with no settings registered.
 */
SettingsDescription::SettingsDescription()
{
}


/**
 * @brief Releases the reference held on each registered Setting.
 */
SettingsDescription::~SettingsDescription()
{
	for (int32 i = 0; Setting* setting = SettingAt(i); i++)
		setting->ReleaseReference();
}


/**
 * @brief Returns the number of settings in the description.
 */
int32
SettingsDescription::CountSettings() const
{
	return fSettings.CountItems();
}


/**
 * @brief Returns the setting at @a index.
 *
 * @param index  Zero-based index.
 * @return Setting pointer, or @c NULL when @a index is out of range.
 */
Setting*
SettingsDescription::SettingAt(int32 index) const
{
	return fSettings.ItemAt(index);
}


/**
 * @brief Looks up a setting by its stable id.
 *
 * @param id  Identifier to match against Setting::ID().
 * @return Matching setting or @c NULL when no setting carries that id.
 */
Setting*
SettingsDescription::SettingByID(const char* id) const
{
	for (int32 i = 0; Setting* setting = fSettings.ItemAt(i); i++) {
		if (strcmp(setting->ID(), id) == 0)
			return setting;
	}

	return NULL;
}


/**
 * @brief Registers @a setting and acquires a reference on it.
 *
 * @param setting  Setting to add to the description.
 * @return @c true on success, @c false on allocation failure.
 */
bool
SettingsDescription::AddSetting(Setting* setting)
{
	if (!fSettings.AddItem(setting))
		return false;

	setting->AcquireReference();
	return true;
}
