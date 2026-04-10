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
 *   Copyright 2004-2009, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Jérôme Duval
 *       Andrew McCall (mccall@digitalparadise.co.uk)
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file MouseSettings.cpp
 *  @brief Per-mouse and global mouse-preference persistence implementation. */


#include "MouseSettings.h"

#include <stdio.h>

#include <FindDirectory.h>
#include <File.h>
#include <Path.h>
#include <View.h>


/**
 * @brief Constructs MouseSettings with all values set to compiled-in defaults.
 */
MouseSettings::MouseSettings()
{
	Defaults();

#ifdef DEBUG
	Dump();
#endif
}


/**
 * @brief Constructs MouseSettings by copying from an existing mouse_settings struct.
 *
 * Starts with defaults, then overlays the given settings.  Queries the system
 * for mouse mode, focus-follows-mouse mode, and accept-first-click state.
 * Validates the mouse type range and ensures a valid button mapping.
 *
 * @param originalSettings Pointer to an existing settings struct to copy from,
 *                         or NULL to keep the defaults.
 */
MouseSettings::MouseSettings(const mouse_settings* originalSettings)
{
	Defaults();

	if (originalSettings != NULL) {
		fMode = mouse_mode();
		fFocusFollowsMouseMode = focus_follows_mouse_mode();
		fAcceptFirstClick = accept_first_click();

		fSettings = *originalSettings;

		if (MouseType() < 1 || MouseType() > B_MAX_MOUSE_BUTTONS)
			SetMouseType(kDefaultMouseType);
		_EnsureValidMapping();
	}

#ifdef DEBUG
	Dump();
#endif
}


/** @brief Destructor. */
MouseSettings::~MouseSettings()
{
}


#ifdef DEBUG
/**
 * @brief Prints all current mouse settings to stdout for debugging.
 */
void
MouseSettings::Dump()
{
	printf("type:\t\t%" B_PRId32 " button mouse\n", fSettings.type);
	printf("map:\t\tleft = %" B_PRIu32 " : middle = %" B_PRIu32 " : "
		"right = %" B_PRIu32 "\n",
		fSettings.map.button[0], fSettings.map.button[2],
		fSettings.map.button[1]);
	printf("click speed:\t%" B_PRId64 "\n", fSettings.click_speed);
	printf("accel:\t\t%s\n", fSettings.accel.enabled
		? "enabled" : "disabled");
	printf("accel factor:\t%" B_PRId32 "\n", fSettings.accel.accel_factor);
	printf("speed:\t\t%" B_PRId32 "\n", fSettings.accel.speed);

	const char *mode = "unknown";
	switch (fMode) {
		case B_NORMAL_MOUSE:
			mode = "activate";
			break;
		case B_CLICK_TO_FOCUS_MOUSE:
			mode = "focus";
			break;
		case B_FOCUS_FOLLOWS_MOUSE:
			mode = "auto-focus";
			break;
	}
	printf("mouse mode:\t%s\n", mode);

	const char *focus_follows_mouse_mode = "unknown";
	switch (fFocusFollowsMouseMode) {
		case B_NORMAL_FOCUS_FOLLOWS_MOUSE:
			focus_follows_mouse_mode = "normal";
			break;
		case B_WARP_FOCUS_FOLLOWS_MOUSE:
			focus_follows_mouse_mode = "warp";
			break;
		case B_INSTANT_WARP_FOCUS_FOLLOWS_MOUSE:
			focus_follows_mouse_mode = "instant warp";
			break;
	}
	printf("focus follows mouse mode:\t%s\n", focus_follows_mouse_mode);
	printf("accept first click:\t%s\n", fAcceptFirstClick
		? "enabled" : "disabled");
}
#endif


/**
 * @brief Resets all mouse settings to the compiled-in system defaults.
 */
void
MouseSettings::Defaults()
{
	SetClickSpeed(kDefaultClickSpeed);
	SetMouseSpeed(kDefaultMouseSpeed);
	SetMouseType(kDefaultMouseType);
	SetAccelerationFactor(kDefaultAccelerationFactor);
	SetMouseMode(B_NORMAL_MOUSE);
	SetFocusFollowsMouseMode(B_NORMAL_FOCUS_FOLLOWS_MOUSE);
	SetAcceptFirstClick(kDefaultAcceptFirstClick);

	for (int i = 0; i < B_MAX_MOUSE_BUTTONS; i++)
		fSettings.map.button[i] = B_MOUSE_BUTTON(i + 1);
}


/**
 * @brief Sets the number of mouse buttons, clamping to valid range.
 *
 * @param type The number of buttons (1 to B_MAX_MOUSE_BUTTONS). Out-of-range
 *             values are silently ignored.
 */
void
MouseSettings::SetMouseType(int32 type)
{
	if (type <= 0 || type > B_MAX_MOUSE_BUTTONS)
		return;

	fSettings.type = type;
}


/**
 * @brief Returns the double-click speed threshold in microseconds.
 *
 * @return The click speed.
 */
bigtime_t
MouseSettings::ClickSpeed() const
{
	return fSettings.click_speed;
}


/**
 * @brief Sets the double-click speed threshold.
 *
 * @param clickSpeed The new click speed in microseconds.
 */
void
MouseSettings::SetClickSpeed(bigtime_t clickSpeed)
{
	fSettings.click_speed = clickSpeed;
}


/**
 * @brief Sets the mouse pointer speed.
 *
 * @param speed The new mouse speed value.
 */
void
MouseSettings::SetMouseSpeed(int32 speed)
{
	fSettings.accel.speed = speed;
}


/**
 * @brief Sets the mouse acceleration factor.
 *
 * @param factor The new acceleration factor.
 */
void
MouseSettings::SetAccelerationFactor(int32 factor)
{
	fSettings.accel.accel_factor = factor;
}


/**
 * @brief Returns the logical button mapped at the given physical button index.
 *
 * @param index Zero-based physical button index.
 * @return The logical button bitmask, or 0 if @a index is out of range.
 */
uint32
MouseSettings::Mapping(int32 index) const
{
	if (index < 0 || index >= B_MAX_MOUSE_BUTTONS)
		return 0;

	return fSettings.map.button[index];
}


/**
 * @brief Copies the entire mouse button mapping into the caller-supplied struct.
 *
 * @param map Output reference that receives the current mouse_map.
 */
void
MouseSettings::Mapping(mouse_map &map) const
{
	map = fSettings.map;
}


/**
 * @brief Sets the logical button mapping for a single physical button index.
 *
 * Validates the mapping after the change to ensure a primary button exists.
 *
 * @param index  Zero-based physical button index.
 * @param button The logical button bitmask to assign.
 */
void
MouseSettings::SetMapping(int32 index, uint32 button)
{
	if (index < 0 || index >= B_MAX_MOUSE_BUTTONS)
		return;

	fSettings.map.button[index] = button;
	_EnsureValidMapping();
}


/**
 * @brief Replaces the entire button mapping with the given mouse_map.
 *
 * @param map The new button mapping.
 */
void
MouseSettings::SetMapping(mouse_map &map)
{
	fSettings.map = map;
	_EnsureValidMapping();
}


/**
 * @brief Validates the button mapping, ensuring at least one button maps to primary.
 *
 * Replaces any zero-mapped buttons with their default identity mapping.
 * If no button maps to the primary (left) button, forces button 0 to primary.
 */
void
MouseSettings::_EnsureValidMapping()
{
	bool hasPrimary = false;

	for (int i = 0; i < MouseType(); i++) {
		if (fSettings.map.button[i] == 0)
			fSettings.map.button[i] = B_MOUSE_BUTTON(i + 1);
		hasPrimary |= fSettings.map.button[i] & B_MOUSE_BUTTON(1);
	}

	if (!hasPrimary)
		fSettings.map.button[0] = B_MOUSE_BUTTON(1);
}


/**
 * @brief Sets the mouse focus mode (e.g. click-to-focus, focus-follows-mouse).
 *
 * @param mode The new mouse focus mode.
 */
void
MouseSettings::SetMouseMode(mode_mouse mode)
{
	fMode = mode;
}


/**
 * @brief Sets the focus-follows-mouse sub-mode (normal, warp, instant warp).
 *
 * @param mode The new focus-follows-mouse mode.
 */
void
MouseSettings::SetFocusFollowsMouseMode(mode_focus_follows_mouse mode)
{
	fFocusFollowsMouseMode = mode;
}


/**
 * @brief Enables or disables accept-first-click behavior.
 *
 * @param acceptFirstClick @c true to enable, @c false to disable.
 */
void
MouseSettings::SetAcceptFirstClick(bool acceptFirstClick)
{
	fAcceptFirstClick = acceptFirstClick;
}


/* MultiMouseSettings functions */

/**
 * @brief Constructs the multi-mouse settings manager and loads persisted settings from disk.
 */
MultipleMouseSettings::MultipleMouseSettings()
{
	RetrieveSettings();

#ifdef DEBUG
	Dump();
#endif
}


/**
 * @brief Destructor; saves all settings to disk and frees per-mouse objects.
 */
MultipleMouseSettings::~MultipleMouseSettings()
{
	SaveSettings();

#ifdef DEBUG
	Dump();
#endif

	std::map<BString, MouseSettings*>::iterator itr;
	for (itr = fMouseSettingsObject.begin(); itr != fMouseSettingsObject.end(); ++itr)
		delete itr->second;
}


/**
 * @brief Resolves the filesystem path to the mouse settings file.
 *
 * @param path Output BPath that receives the full settings file path.
 * @return B_OK on success, or an error code if the directory cannot be found.
 */
status_t
MultipleMouseSettings::GetSettingsPath(BPath &path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status < B_OK)
		return status;

	path.Append(mouse_settings_file);
	return B_OK;
}


/**
 * @brief Loads per-mouse settings from the flattened BMessage on disk.
 *
 * Each entry in the file contains a device name and its corresponding
 * mouse_settings struct.  A new MouseSettings object is created for each.
 */
void
MultipleMouseSettings::RetrieveSettings()
{
	BPath path;
	if (GetSettingsPath(path) < B_OK)
		return;

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() < B_OK)
		return;

	BMessage message;

	if (message.Unflatten(&file) == B_OK) {
		int i = 0;
		BString deviceName;
		mouse_settings* settings;
		ssize_t size = 0;

		while (message.FindString("mouseDevice", i, &deviceName) == B_OK) {
			message.FindData("mouseSettings", B_ANY_TYPE, i,
				(const void**)&settings, &size);
			MouseSettings* mouseSettings = new MouseSettings(settings);
			fMouseSettingsObject.insert(std::pair<BString, MouseSettings*>
				(deviceName, mouseSettings));
			i++;
		}
	}
}


/**
 * @brief Archives all per-mouse settings into the given BMessage.
 *
 * Each mouse device is serialized as a "mouseDevice" string and a
 * "mouseSettings" raw data blob.
 *
 * @param into The message to archive into.
 * @param deep Unused; present for API compatibility.
 * @return B_OK.
 */
status_t
MultipleMouseSettings::Archive(BMessage* into, bool deep) const
{
	std::map<BString, MouseSettings*>::const_iterator itr;
	for (itr = fMouseSettingsObject.begin(); itr != fMouseSettingsObject.end();
		++itr) {
		into->AddString("mouseDevice", itr->first);
		into->AddData("mouseSettings", B_ANY_TYPE, itr->second->GetSettings(),
			sizeof(*(itr->second->GetSettings())));
	}

	return B_OK;
}


/**
 * @brief Writes all per-mouse settings to disk as a flattened BMessage.
 *
 * @return B_OK on success, or an error code on failure.
 */
status_t
MultipleMouseSettings::SaveSettings()
{
	BPath path;
	status_t status = GetSettingsPath(path);
	if (status < B_OK)
		return status;

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;

	BMessage message;
	Archive(&message, true);
	message.Flatten(&file);

	return B_OK;
}


/**
 * @brief Resets all per-mouse settings objects to their compiled-in defaults.
 */
void
MultipleMouseSettings::Defaults()
{
	std::map<BString, MouseSettings*>::iterator itr;
	for (itr = fMouseSettingsObject.begin(); itr != fMouseSettingsObject.end();
		++itr) {
		itr->second->Defaults();
	}
}


#ifdef DEBUG
/**
 * @brief Prints all per-mouse settings to stdout for debugging.
 */
void
MultipleMouseSettings::Dump()
{
	std::map<BString, MouseSettings*>::iterator itr;
	for (itr = fMouseSettingsObject.begin();
		itr != fMouseSettingsObject.end(); ++itr) {
		printf("mouse_name:\t%s\n", itr->first.String());
		itr->second->Dump();
		printf("\n");
	}

}
#endif


/**
 * @brief Returns the settings object for the named mouse, creating one if it does not exist.
 *
 * @param mouse_name The device name of the mouse.
 * @return Pointer to the (possibly newly created) MouseSettings, or NULL on
 *         allocation failure.
 */
MouseSettings*
MultipleMouseSettings::AddMouseSettings(BString mouse_name)
{
	MouseSettings* settings = GetMouseSettings(mouse_name);
	if (settings != NULL)
		return settings;

	settings = new(std::nothrow) MouseSettings();

	if(settings != NULL) {
		fMouseSettingsObject.insert(std::pair<BString, MouseSettings*>
			(mouse_name, settings));
		return settings;
	}
	return NULL;
}


/**
 * @brief Looks up the settings object for the named mouse.
 *
 * @param mouse_name The device name of the mouse.
 * @return Pointer to the MouseSettings if found, or NULL otherwise.
 */
MouseSettings*
MultipleMouseSettings::GetMouseSettings(BString mouse_name)
{
	std::map<BString, MouseSettings*>::iterator itr;
	itr = fMouseSettingsObject.find(mouse_name);

	if (itr != fMouseSettingsObject.end())
		return itr->second;
	return NULL;
 }

