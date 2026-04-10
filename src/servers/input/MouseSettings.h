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
 *   Copyright (c) 2004, Haiku
 *   This software is part of the Haiku distribution and is covered
 *   by the Haiku license.
 *
 *   Authors:
 *       Jérôme Duval
 *       Andrew McCall (mccall@digitalparadise.co.uk)
 *       Axel Dörfler (axeld@pinc-software.de)
 */

/** @file MouseSettings.h
 *  @brief Per-mouse and global mouse preferences (speed, click speed, button map, focus mode). */

#ifndef MOUSE_SETTINGS_H_
#define MOUSE_SETTINGS_H_

#include <Archivable.h>
#include <InterfaceDefs.h>
#include <kb_mouse_settings.h>
#include <Path.h>
#include <String.h>
#include <SupportDefs.h>

#include <map>


/** @brief Mutable per-mouse settings record (speed, mapping, mode). */
class MouseSettings {
	public:
		MouseSettings();
		/** @brief Constructs a new record initialised from an existing one. */
		MouseSettings(const mouse_settings* originalSettings);
		~MouseSettings();

		/** @brief Resets the record to built-in defaults. */
		void Defaults();
		/** @brief Dumps the record to standard output for debugging. */
		void Dump();

		/** @brief Returns the configured pointing device type id. */
		int32 MouseType() const { return fSettings.type; }
		void SetMouseType(int32 type);

		/** @brief Returns the double-click interval in microseconds. */
		bigtime_t ClickSpeed() const;
		void SetClickSpeed(bigtime_t click_speed);

		/** @brief Returns the cursor speed multiplier. */
		int32 MouseSpeed() const { return fSettings.accel.speed; }
		void SetMouseSpeed(int32 speed);

		/** @brief Returns the cursor acceleration factor. */
		int32 AccelerationFactor() const
			{ return fSettings.accel.accel_factor; }
		void SetAccelerationFactor(int32 factor);

		/** @brief Returns the button code mapped to physical button @p index. */
		uint32 Mapping(int32 index) const;
		/** @brief Returns a copy of the full button map. */
		void Mapping(mouse_map &map) const;
		/** @brief Maps physical button @p index to @p button. */
		void SetMapping(int32 index, uint32 button);
		/** @brief Replaces the entire button map. */
		void SetMapping(mouse_map &map);

		/** @brief Returns the configured window-focus mouse mode. */
		mode_mouse MouseMode() const { return fMode; }
		void SetMouseMode(mode_mouse mode);

		/** @brief Returns the configured focus-follows-mouse sub-mode. */
		mode_focus_follows_mouse FocusFollowsMouseMode() const
			{ return fFocusFollowsMouseMode; }
		void SetFocusFollowsMouseMode(mode_focus_follows_mouse mode);

		/** @brief Returns true if a click on an unfocused window should be acted upon. */
		bool AcceptFirstClick() const { return fAcceptFirstClick; }
		void SetAcceptFirstClick(bool acceptFirstClick);

		/** @brief Returns a pointer to the underlying mouse_settings struct. */
		const mouse_settings* GetSettings() { return &fSettings; }

	private:
		void _EnsureValidMapping();

	private:
		mouse_settings	fSettings;                  /**< Underlying kernel-shaped settings struct. */

		// FIXME all these extra settings are not specific to each mouse.
		// They should be moved into MultipleMouseSettings directly
		mode_mouse		fMode;                      /**< Window focus mouse mode. */
		mode_focus_follows_mouse	fFocusFollowsMouseMode;  /**< Focus-follows-mouse sub-mode. */
		bool			fAcceptFirstClick;          /**< Whether the first click on a window is delivered. */
};


/** @brief Container of per-mouse-name MouseSettings records, persisted to disk. */
class MultipleMouseSettings: public BArchivable {
	public:
		/** @brief Constructs the container and loads any persisted records. */
		MultipleMouseSettings();
		~MultipleMouseSettings();

		/** @brief Archives every contained MouseSettings record into @p into. */
		status_t Archive(BMessage* into, bool deep = false) const;

		/** @brief Resets every record to defaults. */
		void Defaults();
		/** @brief Dumps every contained record. */
		void Dump();
		/** @brief Persists the container to its on-disk settings file. */
		status_t SaveSettings();


		/** @brief Adds (and returns) a new MouseSettings record for @p mouse_name. */
		MouseSettings* AddMouseSettings(BString mouse_name);
		/** @brief Looks up the settings record for @p mouse_name, or NULL if absent. */
		MouseSettings* GetMouseSettings(BString mouse_name);

	private:
		static status_t GetSettingsPath(BPath &path);
		void RetrieveSettings();

		typedef std::map<BString, MouseSettings*> mouse_settings_object;
		mouse_settings_object  fMouseSettingsObject;  /**< Map from mouse name to its settings record. */
};

#endif	// MOUSE_SETTINGS_H
