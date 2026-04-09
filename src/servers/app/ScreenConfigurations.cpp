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
 *   Copyright 2005-2009, Axel Dörfler, axeld@pinc-software.de.
 *   This file may be used under the terms of the MIT License.
 */

/** @file ScreenConfigurations.cpp
 *  @brief Stores and retrieves per-monitor screen configuration settings. */


#include "ScreenConfigurations.h"

#include <new>
#include <string.h>
#include <strings.h>

#include <Message.h>


/**
 * @brief Constructs an empty ScreenConfigurations container.
 */
ScreenConfigurations::ScreenConfigurations()
	:
	fConfigurations(10)
{
}


/**
 * @brief Destroys the ScreenConfigurations container, freeing all stored configs.
 */
ScreenConfigurations::~ScreenConfigurations()
{
}


/**
 * @brief Returns the currently active configuration for the given screen ID.
 * @param id The screen identifier to search for.
 * @return Pointer to the current screen_configuration, or NULL if not found.
 */
screen_configuration*
ScreenConfigurations::CurrentByID(int32 id) const
{
	for (int32 i = fConfigurations.CountItems(); i-- > 0;) {
		screen_configuration* configuration = fConfigurations.ItemAt(i);

		if (configuration->id == id && configuration->is_current)
			return configuration;
	}

	return NULL;
}


/**
 * @brief Finds the best-matching stored configuration for the given screen.
 *
 * When @a info is NULL, only the screen ID is used for matching. When monitor
 * information is provided, vendor, name, product ID, serial number, and
 * production date are scored to find the closest match.
 *
 * @param id          The screen identifier.
 * @param info        Optional monitor information for more precise matching.
 * @param _exactMatch If non-NULL, set to true when the serial number matches.
 * @return Pointer to the best matching screen_configuration, or NULL.
 */
screen_configuration*
ScreenConfigurations::BestFit(int32 id, const monitor_info* info,
	bool* _exactMatch) const
{
	if (info == NULL) {
		// Only look for a matching ID - this is all we have
		for (uint32 pass = 0; pass < 2; pass++) {
			for (int32 i = fConfigurations.CountItems(); i-- > 0;) {
				screen_configuration* configuration = fConfigurations.ItemAt(i);

				if ((pass != 0 || !configuration->has_info)
					&& id == configuration->id)
					return configuration;
			}
		}

		return NULL;
	}

	// Look for a configuration that matches the monitor

	bool exactMatch = false;
	int32 bestScore = 0;
	int32 bestIndex = -1;
	BMessage stored;

	for (int32 i = fConfigurations.CountItems(); i-- > 0;) {
		screen_configuration* configuration = fConfigurations.ItemAt(i);
		if (!configuration->has_info)
			continue;

		int32 score = 0;

		if (!strcasecmp(configuration->info.vendor, info->vendor)
			&& !strcasecmp(configuration->info.name, info->name)
			&& configuration->info.product_id == info->product_id) {
			score += 2;
			if (strcmp(configuration->info.serial_number,
					info->serial_number) == 0) {
				exactMatch = true;
				score += 2;
			}
			if (configuration->info.produced.year == info->produced.year
				&& configuration->info.produced.week == info->produced.week)
				score++;
		}

		if (score > bestScore) {
			bestScore = score;
			bestIndex = i;
		}
	}

	if (bestIndex < 0)
		return NULL;

	if (_exactMatch != NULL)
		*_exactMatch = exactMatch;

	return fConfigurations.ItemAt(bestIndex);
}


/**
 * @brief Stores or updates a screen configuration.
 *
 * If a matching configuration already exists and is an exact match (or has
 * no monitor info), it is updated in place. Otherwise a new entry is created.
 *
 * @param id    The screen identifier.
 * @param info  Optional monitor information (may be NULL).
 * @param frame The screen's frame rectangle.
 * @param mode  The active display mode.
 * @return B_OK on success, B_NO_MEMORY if allocation fails.
 */
status_t
ScreenConfigurations::Set(int32 id, const monitor_info* info,
	const BRect& frame, const display_mode& mode)
{
	// Find configuration that we can overwrite

	bool exactMatch;
	screen_configuration* configuration = BestFit(id, info, &exactMatch);

	if (configuration != NULL && configuration->has_info && !exactMatch) {
		// only overwrite exact or unspecified configurations
		configuration->is_current = false;
			// TODO: provide a more obvious current mechanism...
		configuration = NULL;
	}

	if (configuration == NULL) {
		// we need a new configuration to store
		configuration = new (std::nothrow) screen_configuration;
		if (configuration == NULL)
			return B_NO_MEMORY;

		fConfigurations.AddItem(configuration);
	}

	configuration->id = id;
	configuration->frame = frame;
	configuration->is_current = true;

	if (info != NULL) {
		memcpy(&configuration->info, info, sizeof(monitor_info));
		configuration->has_info = true;
	} else
		configuration->has_info = false;

	memcpy(&configuration->mode, &mode, sizeof(display_mode));

	return B_OK;
}


/**
 * @brief Sets the brightness value for the configuration with the given ID.
 * @param id         The screen identifier (currently applied to all configs).
 * @param brightness The brightness value to store.
 */
void
ScreenConfigurations::SetBrightness(int32 id, float brightness)
{
	for (int32 i = fConfigurations.CountItems(); i-- > 0;) {
		screen_configuration* configuration = fConfigurations.ItemAt(i);
		configuration->brightness = brightness;
	}
}


/**
 * @brief Returns the brightness for the first stored configuration.
 * @param id The screen identifier (currently unused).
 * @return The stored brightness value, or -1 if no configuration exists.
 */
float
ScreenConfigurations::Brightness(int32 id)
{
	screen_configuration* configuration = fConfigurations.ItemAt(0);

	if (configuration == NULL)
		return -1;

	return configuration->brightness;
}


/**
 * @brief Removes and deletes the given configuration from the container.
 * @param configuration The configuration to remove (may be NULL, which is a no-op).
 */
void
ScreenConfigurations::Remove(screen_configuration* configuration)
{
	if (configuration == NULL)
		return;

	fConfigurations.RemoveItem(configuration);
		// this also deletes the configuration
}


/**
 * @brief Stores all configurations as separate BMessages into @a settings.
 *
 * Each screen configuration is serialized into a child "screen" message within
 * the provided container message.
 *
 * @param settings The BMessage that receives the serialized configurations.
 * @return B_OK on success.
 */
status_t
ScreenConfigurations::Store(BMessage& settings) const
{
	// Store the configuration of all current screens

	for (int32 i = 0; i < fConfigurations.CountItems(); i++) {
		screen_configuration* configuration = fConfigurations.ItemAt(i);

		BMessage screenSettings;
		screenSettings.AddInt32("id", configuration->id);

		if (configuration->has_info) {
			screenSettings.AddString("vendor", configuration->info.vendor);
			screenSettings.AddString("name", configuration->info.name);
			screenSettings.AddInt32("product id",
				configuration->info.product_id);
			screenSettings.AddString("serial",
				configuration->info.serial_number);
			screenSettings.AddInt32("produced week",
				configuration->info.produced.week);
			screenSettings.AddInt32("produced year",
				configuration->info.produced.year);
		}

		screenSettings.AddRect("frame", configuration->frame);
		screenSettings.AddData("mode", B_RAW_TYPE, &configuration->mode,
			sizeof(display_mode));
		screenSettings.AddFloat("brightness", configuration->brightness);

		settings.AddMessage("screen", &screenSettings);
	}

	return B_OK;
}


/**
 * @brief Restores configurations from a previously stored BMessage.
 *
 * All current configurations are discarded before loading. Entries with
 * missing or invalid mandatory fields are silently skipped.
 *
 * @param settings The BMessage containing serialized screen configurations.
 * @return B_OK on success, B_NO_MEMORY if allocation fails during loading.
 */
status_t
ScreenConfigurations::Restore(const BMessage& settings)
{
	fConfigurations.MakeEmpty();

	BMessage stored;
	for (int32 i = 0; settings.FindMessage("screen", i, &stored) == B_OK; i++) {
		const display_mode* mode;
		ssize_t size;
		int32 id;
		if (stored.FindInt32("id", &id) != B_OK
			|| stored.FindData("mode", B_RAW_TYPE, (const void**)&mode,
					&size) != B_OK
			|| size != sizeof(display_mode))
			continue;

		screen_configuration* configuration
			= new(std::nothrow) screen_configuration;
		if (configuration == NULL)
			return B_NO_MEMORY;

		configuration->id = id;
		configuration->is_current = false;

		const char* vendor;
		const char* name;
		uint32 productID;
		const char* serial;
		int32 week, year;
		if (stored.FindString("vendor", &vendor) == B_OK
			&& stored.FindString("name", &name) == B_OK
			&& stored.FindInt32("product id", (int32*)&productID) == B_OK
			&& stored.FindString("serial", &serial) == B_OK
			&& stored.FindInt32("produced week", &week) == B_OK
			&& stored.FindInt32("produced year", &year) == B_OK) {
			// create monitor info
			strlcpy(configuration->info.vendor, vendor,
				sizeof(configuration->info.vendor));
			strlcpy(configuration->info.name, name,
				sizeof(configuration->info.name));
			strlcpy(configuration->info.serial_number, serial,
				sizeof(configuration->info.serial_number));
			configuration->info.product_id = productID;
			configuration->info.produced.week = week;
			configuration->info.produced.year = year;
			configuration->has_info = true;
		} else
			configuration->has_info = false;

		stored.FindRect("frame", &configuration->frame);
		memcpy(&configuration->mode, mode, sizeof(display_mode));

		if (stored.FindFloat("brightness", &configuration->brightness) != B_OK)
			configuration->brightness = 1.0f;

		fConfigurations.AddItem(configuration);
	}

	return B_OK;
}
