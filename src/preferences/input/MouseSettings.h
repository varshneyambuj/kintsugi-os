/*
 * Copyright 2026, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2019, Haiku, Inc.
 * Original author: Preetpal Kaur.
 */

/** @file MouseSettings.h
    @brief Per-device and aggregate mouse settings models for the input preferences pane. */

#ifndef MOUSE_SETTINGS_H
#define MOUSE_SETTINGS_H


#include <map>

#include <String.h>
#include <SupportDefs.h>

#include "kb_mouse_settings.h"


class BPath;

/**
 * @brief Mouse settings model for a single physical input device.
 *
 * Wraps the input-server queries for one named mouse instance and
 * caches an "original" snapshot so that the preferences pane can offer
 * Revert and Defaults actions. A few of the underlying settings
 * (focus mode, focus-follows-mouse, accept-first-click) are global
 * rather than per-device but are exposed here for convenience.
 */
class MouseSettings {
public:
		MouseSettings(BString name);
		~MouseSettings();

		void Revert();
		bool IsRevertable() const;
		void Defaults();
		bool IsDefaultable() const;

		/** @brief Returns the configured number of buttons for this device. */
		int32 MouseType() const { return fSettings.type; }
		void SetMouseType(int32 type);

		bigtime_t ClickSpeed() const;
		void SetClickSpeed(bigtime_t click_speed);

		/** @brief Returns the cached pointer-movement speed value. */
		int32 MouseSpeed() const { return fSettings.accel.speed; }
		void SetMouseSpeed(int32 speed);

		/** @brief Returns the cached pointer-acceleration factor. */
		int32 AccelerationFactor() const { return fSettings.accel.accel_factor; }
		void SetAccelerationFactor(int32 factor);

		uint32 Mapping(int32 index) const;
		void Mapping(mouse_map &map) const;
		void SetMapping(int32 index, uint32 button);
		void SetMapping(mouse_map &map);

		/** @brief Returns the global mouse focus mode. */
		mode_mouse MouseMode() const { return fMode; }
		void SetMouseMode(mode_mouse mode);

		/** @brief Returns the global focus-follows-mouse mode. */
		mode_focus_follows_mouse FocusFollowsMouseMode() const {
			return fFocusFollowsMouseMode;
		}
		void SetFocusFollowsMouseMode(mode_focus_follows_mouse mode);

		/** @brief Returns whether clicks on inactive windows are accepted. */
		bool AcceptFirstClick() const { return fAcceptFirstClick; }
		void SetAcceptFirstClick(bool accept_first_click);

		mouse_settings* GetSettings();

private:
		status_t _RetrieveSettings();

private:
		BString						fName;
		mode_mouse					fMode, fOriginalMode;
		mode_focus_follows_mouse	fFocusFollowsMouseMode;
		mode_focus_follows_mouse	fOriginalFocusFollowsMouseMode;
		bool						fAcceptFirstClick;
		bool						fOriginalAcceptFirstClick;

		mouse_settings	fSettings, fOriginalSettings;
};


/**
 * @brief Registry that hands out one MouseSettings object per device name.
 *
 * Lazily constructs and owns a MouseSettings instance for each unique
 * mouse name passed to AddMouseSettings(). Subsequent calls with the
 * same name return the existing instance.
 */
class MultipleMouseSettings
{
	public:
		MultipleMouseSettings();
		~MultipleMouseSettings();

		MouseSettings* AddMouseSettings(BString mouse_name);

	private:
		typedef std::map<BString, MouseSettings*> mouse_settings_object;
		mouse_settings_object  fMouseSettingsObject;
};

#endif	// MOUSE_SETTINGS_H
