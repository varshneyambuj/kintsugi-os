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
 *   Copyright 2019-2025, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Preetpal Kaur <preetpalok123@gmail.com>
 *       Pawan Yerramilli <me@pawanyerramilli.com>
 *       Samuel Rodríguez Pérez <samuelrp84@gmail.com>
 */


/**
 * @file InputTouchpadPref.cpp
 * @brief Implementation of TouchpadPref, the touchpad settings model.
 *
 * TouchpadPref is the persistence and live-control layer behind the
 * TouchpadPrefView. It reads and writes the touchpad_settings file under
 * B_USER_SETTINGS_DIRECTORY, can build a BMessage representation for
 * shipping to the input server, and pushes speed/acceleration changes
 * through the standard kb_mouse helpers.
 *
 * @see TouchpadPrefView
 */


#include "InputTouchpadPref.h"
#include "InterfaceDefs.h"

#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <List.h>
#include <String.h>

#include <InputServerDevice.h>


/**
 * @brief Constructs the model bound to a specific touchpad device.
 *
 * Initialises the window position to "centred" (the (-1, -1) sentinel),
 * loads the persisted settings (or falls back to the kit defaults if the
 * settings file is missing or unreadable), and snapshots the current
 * settings as the on-entry baseline used by Revert().
 *
 * @param device  BInputDevice for the touchpad. Ownership is transferred
 *                to this object and released by the destructor.
 */
TouchpadPref::TouchpadPref(BInputDevice* device)
	:
	fTouchPad(device)
{
	// default center position
	fWindowPosition.x = -1;
	fWindowPosition.y = -1;

	if (LoadSettings() != B_OK)
		Defaults();

	fStartSettings = fSettings;
}


/**
 * @brief Destroys the model, persisting the current settings.
 *
 * Releases the BInputDevice and writes the in-memory settings back to
 * disk via SaveSettings().
 */
TouchpadPref::~TouchpadPref()
{
	delete fTouchPad;

	SaveSettings();
}


/**
 * @brief Restores the on-entry settings and re-applies live values.
 *
 * Resets the in-memory settings to the snapshot taken in the constructor
 * and pushes the speed and acceleration values back to the kernel driver
 * via set_mouse_speed and set_mouse_acceleration.
 */
void
TouchpadPref::Revert()
{
	fSettings = fStartSettings;
	set_mouse_speed(fTouchPad->Name(), fSettings.trackpad_speed);
	set_mouse_acceleration(fTouchPad->Name(), fSettings.trackpad_acceleration);
}


/**
 * @brief Serialises the current touchpad settings into a BMessage.
 *
 * The returned BMessage carries every field the input server cares about:
 * scroll behaviour, two-finger gestures, edge motion, click handling, and
 * cached trackpad speed/acceleration.
 *
 * @return BMessage filled with the serialised settings.
 */
BMessage
TouchpadPref::BuildSettingsMessage()
{
	BMessage msg;
	msg.AddBool("scroll_reverse", fSettings.scroll_reverse);
	msg.AddBool("scroll_twofinger", fSettings.scroll_twofinger);
	msg.AddBool(
		"scroll_twofinger_horizontal", fSettings.scroll_twofinger_horizontal);
	msg.AddFloat("scroll_rightrange", fSettings.scroll_rightrange);
	msg.AddFloat("scroll_bottomrange", fSettings.scroll_bottomrange);
	msg.AddInt16("scroll_xstepsize", fSettings.scroll_xstepsize);
	msg.AddInt16("scroll_ystepsize", fSettings.scroll_ystepsize);
	msg.AddInt8("scroll_acceleration", fSettings.scroll_acceleration);
	msg.AddInt8("tapgesture_sensibility", fSettings.tapgesture_sensibility);
	msg.AddInt16("padblocker_threshold", fSettings.padblocker_threshold);
	msg.AddInt32("trackpad_speed", fSettings.trackpad_speed);
	msg.AddInt32("trackpad_acceleration", fSettings.trackpad_acceleration);
	msg.AddBool("scroll_twofinger_natural_scrolling", fSettings.scroll_twofinger_natural_scrolling);
	msg.AddInt8("edge_motion", fSettings.edge_motion);
	msg.AddBool("finger_click", fSettings.finger_click);
	msg.AddBool("software_button_areas", fSettings.software_button_areas);

	return msg;
}


/**
 * @brief Pushes the current settings to the live touchpad driver.
 *
 * Calls B_SET_TOUCHPAD_SETTINGS on the BInputDevice with a freshly built
 * settings message so changes take effect without restarting the input
 * server.
 *
 * @return B_OK on success, or an error code propagated from
 *         BInputDevice::Control.
 */
status_t
TouchpadPref::UpdateRunningSettings()
{
	BMessage msg = BuildSettingsMessage();
	return fTouchPad->Control(B_SET_TOUCHPAD_SETTINGS, &msg);
}


/**
 * @brief Resets the in-memory settings to the kit defaults.
 *
 * Copies kDefaultTouchpadSettings into fSettings and re-applies the
 * trackpad speed and acceleration values to the driver.
 */
void
TouchpadPref::Defaults()
{
	fSettings = kDefaultTouchpadSettings;
	set_mouse_speed(fTouchPad->Name(), fSettings.trackpad_speed);
	set_mouse_acceleration(fTouchPad->Name(), fSettings.trackpad_acceleration);
}


/**
 * @brief Computes the absolute path to the per-user settings file.
 *
 * Resolves B_USER_SETTINGS_DIRECTORY and appends TOUCHPAD_SETTINGS_FILE.
 *
 * @param path  Output BPath to fill on success.
 * @return B_OK on success, or an error code from find_directory or
 *         BPath::Append.
 */
status_t
TouchpadPref::GetSettingsPath(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status < B_OK)
		return status;

	return path.Append(TOUCHPAD_SETTINGS_FILE);
}


/**
 * @brief Loads the persisted touchpad settings from disk.
 *
 * Tries to unflatten a BMessage from the user settings file. If that
 * fails the function falls back to a legacy 20-byte plus 8-byte BPoint
 * binary layout (size == 28) for backwards compatibility, filling
 * unspecified fields with kDefaultTouchpadSettings values.
 *
 * @return B_OK on success or B_ERROR if neither format could be parsed.
 *         Other status codes are propagated from BFile::InitCheck() and
 *         GetSettingsPath().
 *
 * @note Caller-side fallback (via Defaults()) is expected when this
 *       function returns anything other than B_OK.
 */
status_t
TouchpadPref::LoadSettings()
{
	BPath path;
	status_t status = GetSettingsPath(path);
	if (status != B_OK)
		return status;

	BFile settingsFile(path.Path(), B_READ_ONLY);
	status = settingsFile.InitCheck();
	if (status != B_OK)
		return status;

	BMessage settingsMsg;
	status = settingsMsg.Unflatten(&settingsFile);
	// Load an old settings file if we have that instead; remove after Beta 6?
	if (status != B_OK) {
		off_t size;
		settingsFile.Seek(0, SEEK_SET);
		// Old settings were 20 bytes, BPoint added 8
		if (settingsFile.GetSize(&size) == B_OK && size == 28) {
			if (settingsFile.Read(&fSettings, 20) != 20) {
				LOG("failed to load old settings\n");
				return B_ERROR;
			} else {
				fSettings.scroll_reverse = kDefaultTouchpadSettings.scroll_reverse;
				fSettings.trackpad_speed = kDefaultTouchpadSettings.trackpad_speed;
				fSettings.trackpad_acceleration = kDefaultTouchpadSettings.trackpad_acceleration;
				return B_OK;
			}
		} else {
			LOG("failed to load settings\n");
			return B_ERROR;
		}
	}

	settingsMsg.FindBool("scroll_reverse", &fSettings.scroll_reverse);
	settingsMsg.FindBool("scroll_twofinger", &fSettings.scroll_twofinger);
	settingsMsg.FindBool("scroll_twofinger_horizontal", &fSettings.scroll_twofinger_horizontal);
	settingsMsg.FindFloat("scroll_rightrange", &fSettings.scroll_rightrange);
	settingsMsg.FindFloat("scroll_bottomrange", &fSettings.scroll_bottomrange);
	settingsMsg.FindInt16("scroll_xstepsize", (int16*)&fSettings.scroll_xstepsize);
	settingsMsg.FindInt16("scroll_ystepsize", (int16*)&fSettings.scroll_ystepsize);
	settingsMsg.FindInt8("scroll_acceleration", (int8*)&fSettings.scroll_acceleration);
	settingsMsg.FindInt8("tapgesture_sensibility", (int8*)&fSettings.tapgesture_sensibility);
	settingsMsg.FindInt16("padblocker_threshold", (int16*)&fSettings.padblocker_threshold);
	settingsMsg.FindInt32("trackpad_speed", &fSettings.trackpad_speed);
	settingsMsg.FindInt32("trackpad_acceleration", &fSettings.trackpad_acceleration);
	settingsMsg.FindPoint("window_position", &fWindowPosition);

	fSettings.scroll_twofinger_natural_scrolling = settingsMsg.GetBool(
		"scroll_twofinger_natural_scrolling",
		kDefaultTouchpadSettings.scroll_twofinger_natural_scrolling);
	fSettings.edge_motion = settingsMsg.GetInt8(
		"edge_motion",
		kDefaultTouchpadSettings.edge_motion);
	fSettings.finger_click = settingsMsg.GetBool(
		"finger_click",
		kDefaultTouchpadSettings.finger_click);
	fSettings.software_button_areas = settingsMsg.GetBool(
		"software_button_areas",
		kDefaultTouchpadSettings.software_button_areas);

	return B_OK;
}


/**
 * @brief Writes the current settings out to the per-user settings file.
 *
 * Builds a BMessage with BuildSettingsMessage(), augments it with the
 * window position, and flattens the result to disk creating the file if
 * needed.
 *
 * @return B_OK on success or an error code propagated from BFile or
 *         BMessage::Flatten.
 */
status_t
TouchpadPref::SaveSettings()
{
	BPath path;
	status_t status = GetSettingsPath(path);
	if (status != B_OK)
		return status;

	BFile settingsFile(path.Path(), B_READ_WRITE | B_CREATE_FILE);
	status = settingsFile.InitCheck();
	if (status != B_OK)
		return status;

	BMessage settingsMsg = BuildSettingsMessage();
	settingsMsg.AddPoint("window_position", fWindowPosition);

	status = settingsMsg.Flatten(&settingsFile);
	if (status != B_OK) {
		LOG("can't save settings\n");
		return status;
	}

	return B_OK;
}


/**
 * @brief Updates the trackpad speed and pushes it to the driver.
 *
 * Maps the slider's 0-1000 range through an exponential curve into the
 * 8192-524287 range expected by the kernel driver, then writes both the
 * cached value and the live value through set_mouse_speed.
 *
 * @param speed  Slider value in the range 0 (slow) to 1000 (fast).
 */
void
TouchpadPref::SetSpeed(int32 speed)
{
	int32 value = (int32)pow(2, speed * 6.0 / 1000) * 8192;
		// slow = 8192, fast = 524287; taken from InputMouse.cpp
	if (set_mouse_speed(fTouchPad->Name(), value) == B_OK) {
		fSettings.trackpad_speed = value;
		UpdateRunningSettings();
	}
}


/**
 * @brief Updates the trackpad acceleration factor and pushes it to the driver.
 *
 * Maps the slider's 0-1000 range through a quadratic curve into the
 * 0-262144 range expected by the kernel driver and writes the resulting
 * value through set_mouse_acceleration.
 *
 * @param accel  Slider value in the range 0 (low) to 1000 (high).
 */
void
TouchpadPref::SetAcceleration(int32 accel)
{
	int32 value = (int32)pow(accel * 4.0 / 1000, 2) * 16384;
		// slow = 0, fast = 262144; taken from InputMouse.cpp
	if (set_mouse_acceleration(fTouchPad->Name(), value) == B_OK) {
		fSettings.trackpad_acceleration = value;
		UpdateRunningSettings();
	}
}
