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
 *   Copyright 2003-2009, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Phipps
 *       Jérôme Duval, jerome.duval@free.fr
 */

/*!	This is the class that wraps the screensaver settings, as well as the
	settings of the screensaver preference application.
*/


/**
 * @file ScreenSaverSettings.cpp
 * @brief Persistent settings store for the Screen Saver preferences.
 *
 * ScreenSaverSettings reads and writes the flattened BMessage settings file
 * stored in B_USER_SETTINGS_DIRECTORY/ScreenSaver_settings. It holds the
 * active module name, timing parameters (blank/standby/suspend/off), DPMS
 * flags, hot-corner configuration, password lock settings, and per-module
 * state sub-messages. It also resolves the system password from shadow/passwd
 * when the "system" lock method is selected.
 *
 * @see ScreenSaverRunner, BScreenSaver
 */


#include "ScreenSaverSettings.h"

#include <pwd.h>
#include <shadow.h>
#include <string.h>

#include <Debug.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <StorageDefs.h>
#include <String.h>


/**
 * @brief Constructor — locates the settings file path and loads defaults.
 *
 * Resolves B_USER_SETTINGS_DIRECTORY and appends "ScreenSaver_settings" to
 * form fSettingsPath, then calls Defaults() to initialise all settings to
 * their factory values.
 */
ScreenSaverSettings::ScreenSaverSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		fSettingsPath = path;
		fSettingsPath.Append("ScreenSaver_settings", true);
	}

	Defaults();
}


/**
 * @brief Load and parse the settings file from disk.
 *
 * Opens the settings file at fSettingsPath, unflattens the BMessage, and
 * copies each recognised field into the corresponding member variable. When
 * the lock method is "system", also reads the shadow or passwd entry for the
 * current user to populate fPassword.
 *
 * @return true if the file was read and parsed successfully, false if the file
 *         does not exist or cannot be unflattened.
 */
//! Load the flattened settings BMessage from disk and parse it.
bool
ScreenSaverSettings::Load()
{
	BFile file(fSettingsPath.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;

	// File exists. Unflatten the message and call the settings parser.
	if (fSettings.Unflatten(&file) != B_OK)
		return false;

	PRINT_OBJECT(fSettings);

	BRect rect;
	if (fSettings.FindRect("windowframe", &rect) == B_OK)
		fWindowFrame = rect;
	int32 value;
	if (fSettings.FindInt32("windowtab", &value) == B_OK)
		fWindowTab = value;
	if (fSettings.FindInt32("timeflags", &value) == B_OK)
		fTimeFlags = value;

	if (fSettings.FindInt32("timefade", &value) == B_OK)
		fBlankTime = value * 1000000LL;
	if (fSettings.FindInt32("timestandby", &value) == B_OK)
		fStandByTime = value * 1000000LL;
	if (fSettings.FindInt32("timesuspend", &value) == B_OK)
		fSuspendTime = value * 1000000LL;
	if (fSettings.FindInt32("timeoff", &value) == B_OK)
		fOffTime = value * 1000000LL;

	if (fSettings.FindInt32("cornernow", &value) == B_OK)
		fBlankCorner = (screen_corner)value;
	if (fSettings.FindInt32("cornernever", &value) == B_OK)
		fNeverBlankCorner = (screen_corner)value;

	bool lockEnabled;
	if (fSettings.FindBool("lockenable", &lockEnabled) == B_OK)
		fLockEnabled = lockEnabled;
	if (fSettings.FindInt32("lockdelay", &value) == B_OK)
		fPasswordTime = value * 1000000LL;
	const char* string;
	if (fSettings.FindString("lockpassword", &string) == B_OK)
		fPassword = string;
	if (fSettings.FindString("lockmethod", &string) == B_OK)
		fLockMethod = string;

	if (fSettings.FindString("modulename", &string) == B_OK)
		fModuleName = string;

	if (!UseSystemPassword())
		return true;

	char* username = getlogin();
	if (username == NULL)
		return true;

	struct spwd *shadowpwd = getspnam(username);
	if (shadowpwd != NULL) {
		fPassword = shadowpwd->sp_pwdp;
	} else {
		struct passwd *pwd = getpwnam(username);
		if (pwd != NULL)
			fPassword = pwd->pw_passwd;
	}

	return true;
}


/**
 * @brief Reset all settings to their factory defaults.
 *
 * Sets the preferences window frame, selects DPMS blanking and all power
 * management features, and initialises timing, corner, lock, and module-name
 * fields to their default values.
 */
void
ScreenSaverSettings::Defaults()
{
	fWindowFrame = BRect(96.5, 77.0, 542.5, 402);
	fWindowTab = 0;

	// Enable blanker + turning off the screen
	fTimeFlags = ENABLE_SAVER | ENABLE_DPMS_STAND_BY | ENABLE_DPMS_SUSPEND
		| ENABLE_DPMS_OFF;

	// Times are in microseconds
	fBlankTime = 900 * 1000000LL;	// 15 minutes

	// All these times are relative to fBlankTime; standby will start 5 minutes
	// after the screen saver.
	fStandByTime = 300 * 1000000LL;	// 5 minutes
	fSuspendTime = 300 * 1000000LL;
	fOffTime = 300 * 1000000LL;

	fBlankCorner = NO_CORNER;
	fNeverBlankCorner = NO_CORNER;

	fLockEnabled = false;
	// This time is NOT referenced to when the screen saver starts, but to when
	// idle time starts, like fBlankTime. By default it is the same as
	// fBlankTime.
	fPasswordTime = 900 * 1000000LL;
	fPassword = "";
	fLockMethod = "custom";

	fModuleName = "";
}


/**
 * @brief Synchronise the internal BMessage with the current field values.
 *
 * Updates (or adds) each settings field in fSettings using ReplaceXxx() with
 * an AddXxx() fallback. All time values are converted from microseconds to
 * whole seconds before storage. The password field is stored as an empty
 * string when using the system password method.
 *
 * @return A reference to the updated fSettings BMessage, ready for flattening.
 */
BMessage&
ScreenSaverSettings::Message()
{
	// We can't just empty the message, because the module states are stored
	// in this message as well.

	if (fSettings.ReplaceRect("windowframe", fWindowFrame) != B_OK)
		fSettings.AddRect("windowframe", fWindowFrame);
	if (fSettings.ReplaceInt32("windowtab", fWindowTab) != B_OK)
		fSettings.AddInt32("windowtab", fWindowTab);
	if (fSettings.ReplaceInt32("timeflags", fTimeFlags) != B_OK)
		fSettings.AddInt32("timeflags", fTimeFlags);
	if (fSettings.ReplaceInt32("timefade", fBlankTime / 1000000LL) != B_OK)
		fSettings.AddInt32("timefade", fBlankTime / 1000000LL);
	if (fSettings.ReplaceInt32("timestandby", fStandByTime / 1000000LL) != B_OK)
		fSettings.AddInt32("timestandby", fStandByTime / 1000000LL);
	if (fSettings.ReplaceInt32("timesuspend", fSuspendTime / 1000000LL) != B_OK)
		fSettings.AddInt32("timesuspend", fSuspendTime / 1000000LL);
	if (fSettings.ReplaceInt32("timeoff", fOffTime / 1000000LL) != B_OK)
		fSettings.AddInt32("timeoff", fOffTime / 1000000LL);
	if (fSettings.ReplaceInt32("cornernow", fBlankCorner) != B_OK)
		fSettings.AddInt32("cornernow", fBlankCorner);
	if (fSettings.ReplaceInt32("cornernever", fNeverBlankCorner) != B_OK)
		fSettings.AddInt32("cornernever", fNeverBlankCorner);
	if (fSettings.ReplaceBool("lockenable", fLockEnabled) != B_OK)
		fSettings.AddBool("lockenable", fLockEnabled);
	if (fSettings.ReplaceInt32("lockdelay", fPasswordTime / 1000000LL) != B_OK)
		fSettings.AddInt32("lockdelay", fPasswordTime / 1000000LL);
	if (fSettings.ReplaceString("lockmethod", fLockMethod) != B_OK)
		fSettings.AddString("lockmethod", fLockMethod);

	const char* password = UseSystemPassword() ? "" : fPassword.String();
	if (fSettings.ReplaceString("lockpassword", password) != B_OK)
		fSettings.AddString("lockpassword", password);

	if (fSettings.ReplaceString("modulename", fModuleName) != B_OK)
		fSettings.AddString("modulename", fModuleName);

	return fSettings;
}


/**
 * @brief Retrieve the saved state for a specific screen saver module.
 *
 * Looks up the "modulesettings_<name>" sub-message from the settings store
 * and copies it into @a stateMessage.
 *
 * @param name          The module name used to key the stored state.
 * @param stateMessage  Output — receives the module's saved BMessage state.
 * @return B_OK if the state was found, B_NAME_NOT_FOUND if no state is stored,
 *         or B_BAD_VALUE if @a name is NULL or empty.
 */
status_t
ScreenSaverSettings::GetModuleState(const char* name, BMessage* stateMessage)
{
	if (name == NULL || *name == '\0')
		return B_BAD_VALUE;

	BString stateName("modulesettings_");
	stateName << name;
	return fSettings.FindMessage(stateName, stateMessage);
}


/**
 * @brief Store the state for a specific screen saver module.
 *
 * Removes any existing "modulesettings_<name>" entry and replaces it with a
 * copy of @a stateMessage. Silently returns if @a name is NULL or empty.
 *
 * @param name          The module name used to key the stored state.
 * @param stateMessage  The BMessage containing the module's current state.
 */
void
ScreenSaverSettings::SetModuleState(const char* name, BMessage* stateMessage)
{
	if (name == NULL || *name == '\0')
		return;

	BString stateName("modulesettings_");
	stateName << name;
	fSettings.RemoveName(stateName.String());
	fSettings.AddMessage(stateName.String(), stateMessage);
}


/**
 * @brief Flatten the current settings BMessage and write it to disk.
 *
 * Calls Message() to synchronise fSettings, then opens (or creates) the
 * settings file at fSettingsPath with B_ERASE_FILE and writes the flattened
 * message. Logs a warning to stderr if the file cannot be opened or the
 * flatten fails.
 */
void
ScreenSaverSettings::Save()
{
  	BMessage &settings = Message();
	PRINT_OBJECT(settings);
	BFile file(fSettingsPath.Path(),
		B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK || settings.Flatten(&file) != B_OK)
		fprintf(stderr, "Problem while saving screensaver preferences file!\n");
}
