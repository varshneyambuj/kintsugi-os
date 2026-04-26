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
 *   Copyright 2019, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Author:
 *       Preetpal Kaur <preetpalok123@gmail.com>
 */


/**
 * @file MouseSettings.cpp
 * @brief Per-device and aggregate mouse settings models for the input preferences pane.
 *
 * MouseSettings reads, mutates, and persists the input-server settings
 * for one mouse (button mapping, click speed, acceleration, focus mode,
 * accept-first-click). MultipleMouseSettings is a per-device registry
 * that hands out one MouseSettings instance per connected mouse name.
 */


#include "MouseSettings.h"

#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <String.h>
#include <View.h>

#include <stdio.h>


/**
 * @brief Constructs a MouseSettings model for the named device.
 *
 * Pulls the device's current settings from the input server. If retrieval
 * fails, the object falls back to system defaults. The values active at
 * construction time are also captured as the "original" snapshot so that
 * Revert() can restore them later.
 *
 * @param name  Device-instance name as known to the input server.
 */
MouseSettings::MouseSettings(BString name)
	:
	fName(name)
{
	if (_RetrieveSettings() != B_OK)
		Defaults();

	fOriginalSettings = fSettings;
	fOriginalMode = fMode;
	fOriginalFocusFollowsMouseMode = fFocusFollowsMouseMode;
	fOriginalAcceptFirstClick = fAcceptFirstClick;
}


/**
 * @brief Destroys the model. No owned resources are released here.
 */
MouseSettings::~MouseSettings()
{
}


/**
 * @brief Pulls the current settings for this device from the input server.
 *
 * Reads button mapping, click speed, mouse speed, acceleration factor,
 * device type, and the global focus/click-through fields.
 *
 * @return     Status code summarising whether all reads succeeded.
 * @retval B_OK     All settings were retrieved.
 * @retval B_ERROR  At least one input-server query failed.
 */
status_t
MouseSettings::_RetrieveSettings()
{
	// retrieve current values
	if (get_mouse_map(fName, &fSettings.map) != B_OK)
		return B_ERROR;
	if (get_click_speed(fName, &fSettings.click_speed) != B_OK)
		return B_ERROR;
	if (get_mouse_speed(fName, &fSettings.accel.speed) != B_OK)
		return B_ERROR;
	if (get_mouse_acceleration(fName, &fSettings.accel.accel_factor) != B_OK)
		return B_ERROR;
	if (get_mouse_type(fName, &fSettings.type) != B_OK)
		return B_ERROR;

	fMode = mouse_mode();
	fFocusFollowsMouseMode = focus_follows_mouse_mode();
	fAcceptFirstClick = accept_first_click();

	return B_OK;
}


/**
 * @brief Resets every setting to the system defaults.
 *
 * Restores click speed, mouse speed, type, acceleration factor, focus
 * mode, focus-follows-mouse mode, accept-first-click, and a 1:1 button
 * mapping spanning B_MAX_MOUSE_BUTTONS.
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

	mouse_map map;
	for (int i = 0; i < B_MAX_MOUSE_BUTTONS; i++)
		map.button[i] = B_MOUSE_BUTTON(i + 1);
	SetMapping(map);
}


/**
 * @brief Tests whether the current settings differ from the system defaults.
 *
 * @return true if at least one tracked field differs from its default.
 */
bool
MouseSettings::IsDefaultable() const
{
	return fSettings.click_speed != kDefaultClickSpeed
		|| fSettings.accel.speed != kDefaultMouseSpeed
		|| fSettings.type != kDefaultMouseType
		|| fSettings.accel.accel_factor != kDefaultAccelerationFactor
		|| fMode != B_NORMAL_MOUSE
		|| fFocusFollowsMouseMode != B_NORMAL_FOCUS_FOLLOWS_MOUSE
		|| fAcceptFirstClick != kDefaultAcceptFirstClick
		|| fSettings.map.button[0] != B_PRIMARY_MOUSE_BUTTON
		|| fSettings.map.button[1] != B_SECONDARY_MOUSE_BUTTON
		|| fSettings.map.button[2] != B_TERTIARY_MOUSE_BUTTON
		|| fSettings.map.button[3] != B_MOUSE_BUTTON(4)
		|| fSettings.map.button[4] != B_MOUSE_BUTTON(5)
		|| fSettings.map.button[5] != B_MOUSE_BUTTON(6);
}


/**
 * @brief Restores the settings that were active when this object was constructed.
 */
void
MouseSettings::Revert()
{
	SetClickSpeed(fOriginalSettings.click_speed);
	SetMouseSpeed(fOriginalSettings.accel.speed);
	SetMouseType(fOriginalSettings.type);
	SetAccelerationFactor(fOriginalSettings.accel.accel_factor);
	SetMouseMode(fOriginalMode);
	SetFocusFollowsMouseMode(fOriginalFocusFollowsMouseMode);
	SetAcceptFirstClick(fOriginalAcceptFirstClick);

	SetMapping(fOriginalSettings.map);
}


/**
 * @brief Tests whether the current settings differ from the captured original snapshot.
 *
 * @return true if Revert() would change at least one tracked field.
 */
bool
MouseSettings::IsRevertable() const
{
	return fSettings.click_speed != fOriginalSettings.click_speed
		|| fSettings.accel.speed != fOriginalSettings.accel.speed
		|| fSettings.type != fOriginalSettings.type
		|| fSettings.accel.accel_factor != fOriginalSettings.accel.accel_factor
		|| fMode != fOriginalMode
		|| fFocusFollowsMouseMode != fOriginalFocusFollowsMouseMode
		|| fAcceptFirstClick != fOriginalAcceptFirstClick
		|| fSettings.map.button[0] != fOriginalSettings.map.button[0]
		|| fSettings.map.button[1] != fOriginalSettings.map.button[1]
		|| fSettings.map.button[2] != fOriginalSettings.map.button[2]
		|| fSettings.map.button[3] != fOriginalSettings.map.button[3]
		|| fSettings.map.button[4] != fOriginalSettings.map.button[4]
		|| fSettings.map.button[5] != fOriginalSettings.map.button[5];
}


/**
 * @brief Updates the device's button-count "type" if the input server accepts it.
 *
 * @param type  Number of mouse buttons (1..B_MAX_MOUSE_BUTTONS).
 */
void
MouseSettings::SetMouseType(int32 type)
{
	if (set_mouse_type(fName, type) == B_OK)
		fSettings.type = type;
}


/**
 * @brief Returns the current double-click time threshold.
 *
 * @return Click speed in microseconds (smaller value means faster click).
 */
bigtime_t
MouseSettings::ClickSpeed() const
{
	return fSettings.click_speed;
}


/**
 * @brief Updates the double-click time threshold for this device.
 *
 * @param clickSpeed  Click speed in microseconds.
 */
void
MouseSettings::SetClickSpeed(bigtime_t clickSpeed)
{
	if (set_click_speed(fName, clickSpeed) == B_OK)
		fSettings.click_speed = clickSpeed;
}


/**
 * @brief Updates the pointer-movement speed for this device.
 *
 * @param speed  Pointer speed value as understood by the input server.
 */
void
MouseSettings::SetMouseSpeed(int32 speed)
{
	if (set_mouse_speed(fName, speed) == B_OK)
		fSettings.accel.speed = speed;
}


/**
 * @brief Updates the pointer-acceleration factor for this device.
 *
 * @param factor  Acceleration factor as understood by the input server.
 */
void
MouseSettings::SetAccelerationFactor(int32 factor)
{
	if (set_mouse_acceleration(fName, factor) == B_OK)
		fSettings.accel.accel_factor = factor;
}


/**
 * @brief Returns the logical button assigned to the given physical button index.
 *
 * @param index  Zero-based physical button index.
 * @return       The B_*_MOUSE_BUTTON value mapped to that physical button.
 * @note         No bounds checking is performed.
 */
uint32
MouseSettings::Mapping(int32 index) const
{
	return fSettings.map.button[index];
}


/**
 * @brief Copies the entire button-mapping table to @a map.
 *
 * @param map  Output structure receiving the full mapping.
 */
void
MouseSettings::Mapping(mouse_map& map) const
{
	map = fSettings.map;
}


/**
 * @brief Reassigns one physical button to a different logical button and persists it.
 *
 * @param index   Zero-based physical button index.
 * @param button  Logical B_*_MOUSE_BUTTON value to assign.
 */
void
MouseSettings::SetMapping(int32 index, uint32 button)
{
	fSettings.map.button[index] = button;
	set_mouse_map(fName, &fSettings.map);
}


/**
 * @brief Replaces the entire button-mapping table for this device.
 *
 * @param map  New mapping. The cached copy is updated only if the input
 *             server accepted the change.
 */
void
MouseSettings::SetMapping(mouse_map& map)
{
	if (set_mouse_map(fName, &map) == B_OK)
		fSettings.map = map;
}


/**
 * @brief Updates the global mouse focus mode.
 *
 * @param mode  Focus mode (e.g. B_NORMAL_MOUSE, B_FOCUS_FOLLOWS_MOUSE).
 * @note  This setting is global, not per device.
 */
void
MouseSettings::SetMouseMode(mode_mouse mode)
{
	set_mouse_mode(mode);
	fMode = mode;
}


/**
 * @brief Updates the global focus-follows-mouse behaviour.
 *
 * @param mode  Focus-follows-mouse mode (instant, delayed, etc.).
 * @note  This setting is global, not per device.
 */
void
MouseSettings::SetFocusFollowsMouseMode(mode_focus_follows_mouse mode)
{
	set_focus_follows_mouse_mode(mode);
	fFocusFollowsMouseMode = mode;
}


/**
 * @brief Updates the "accept first click" (click-through) behaviour.
 *
 * @param accept_first_click  Whether clicks on inactive windows are delivered.
 * @note  This setting is global, not per device.
 */
void
MouseSettings::SetAcceptFirstClick(bool accept_first_click)
{
	set_accept_first_click(accept_first_click);
	fAcceptFirstClick = accept_first_click;
}


/**
 * @brief Returns a mutable pointer to the underlying settings record.
 *
 * @return Pointer to the cached mouse_settings struct owned by this object.
 */
mouse_settings*
MouseSettings::GetSettings()
{
	return &fSettings;
}


/**
 * @brief Constructs an empty registry of per-device MouseSettings models.
 */
MultipleMouseSettings::MultipleMouseSettings()
{
}


/**
 * @brief Releases every per-device MouseSettings object owned by the registry.
 */
MultipleMouseSettings::~MultipleMouseSettings()
{
	std::map<BString, MouseSettings*>::iterator itr;
	for (itr = fMouseSettingsObject.begin(); itr != fMouseSettingsObject.end(); ++itr)
		delete itr->second;
}


/**
 * @brief Returns the MouseSettings entry for @a mouse_name, creating it on first use.
 *
 * @param mouse_name  Device-instance name as reported by the input server.
 * @return            Pointer to the cached entry, or NULL on allocation failure.
 * @note              The returned object remains owned by the registry.
 */
MouseSettings*
MultipleMouseSettings::AddMouseSettings(BString mouse_name)
{
	std::map<BString, MouseSettings*>::iterator itr;
	itr = fMouseSettingsObject.find(mouse_name);

	if (itr != fMouseSettingsObject.end())
		return itr->second;

	MouseSettings* settings = new(std::nothrow) MouseSettings(mouse_name);
	if (settings == NULL)
		return NULL;

	fMouseSettingsObject.insert(
		std::pair<BString, MouseSettings*>(mouse_name, settings));
	return settings;
}
