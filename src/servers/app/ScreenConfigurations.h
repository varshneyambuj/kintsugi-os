/*
 * Copyright 2025, Kintsugi OS Contributors.
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright 2009, Axel Dörfler, axeld@pinc-software.de.
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file ScreenConfigurations.h
 *  @brief Stores and retrieves per-monitor display configurations. */

#ifndef SCREEN_CONFIGURATIONS_H
#define SCREEN_CONFIGURATIONS_H


#include <Accelerant.h>
#include <Rect.h>

#include <ObjectList.h>


class BMessage;


/** @brief Aggregated configuration record for a single physical screen. */
struct screen_configuration {
	int32			id;        /**< Unique screen identifier. */
	monitor_info	info;      /**< Monitor hardware information. */
	BRect			frame;     /**< Screen frame in the virtual desktop. */
	display_mode	mode;      /**< Active display mode. */
	float			brightness;/**< Screen brightness level. */
	bool			has_info;  /**< Whether monitor_info is valid. */
	bool			is_current;/**< Whether this is the currently applied config. */
};


/** @brief Manages a collection of screen_configuration records with persistence. */
class ScreenConfigurations {
public:
								ScreenConfigurations();
								~ScreenConfigurations();

	/** @brief Finds the configuration currently active for the given screen ID.
	 *  @param id Screen identifier.
	 *  @return Pointer to the matching configuration, or NULL if not found. */
			screen_configuration* CurrentByID(int32 id) const;

	/** @brief Finds the best-matching stored configuration for the given screen.
	 *  @param id Screen identifier.
	 *  @param info Monitor hardware info used for matching.
	 *  @param _exactMatch Optional; set to true if an exact match is found.
	 *  @return Pointer to the best-matching configuration, or NULL. */
			screen_configuration* BestFit(int32 id, const monitor_info* info,
									bool* _exactMatch = NULL) const;

	/** @brief Stores or updates the configuration for the given screen.
	 *  @param id Screen identifier.
	 *  @param info Monitor hardware info (may be NULL).
	 *  @param frame Current screen frame in the virtual desktop.
	 *  @param mode Active display mode.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			Set(int32 id, const monitor_info* info,
									const BRect& frame,
									const display_mode& mode);

	/** @brief Updates only the brightness for the given screen.
	 *  @param id Screen identifier.
	 *  @param brightness New brightness level. */
			void				SetBrightness(int32 id, float brightness);

	/** @brief Returns the stored brightness for the given screen.
	 *  @param id Screen identifier.
	 *  @return Brightness value, or a default if not found. */
			float				Brightness(int32 id);

	/** @brief Removes the given configuration record from the collection.
	 *  @param configuration Pointer to the record to remove. */
			void				Remove(screen_configuration* configuration);

	/** @brief Serialises all configurations into a BMessage.
	 *  @param settings Target message.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			Store(BMessage& settings) const;

	/** @brief Restores configurations from a previously serialised BMessage.
	 *  @param settings Source message.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			Restore(const BMessage& settings);

private:
	typedef BObjectList<screen_configuration, true> ConfigurationList;

			ConfigurationList	fConfigurations;
};


#endif	// SCREEN_CONFIGURATIONS_H
