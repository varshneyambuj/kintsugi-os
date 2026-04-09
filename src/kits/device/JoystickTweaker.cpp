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
 *   Copyright 2008, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Fredrik Modeen
 */


/**
 * @file JoystickTweaker.cpp
 * @brief Internal joystick configuration and device scanning helper
 *
 * Implements _BJoystickTweaker, a private class used by BJoystick to scan
 * the joystick device directory (including disabled devices) and to parse
 * joystick description (.joystick) files into joystick_info structures.
 *
 * @see Joystick.cpp
 */


#include "JoystickTweaker.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>

#include <Debug.h>
#include <Directory.h>
#include <Joystick.h>
#include <Path.h>
#include <String.h>
#include <UTF8.h>


#define STACK_STRING_BUFFER_SIZE	64


#if DEBUG
inline void
LOG(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);

	fputs(buf, _BJoystickTweaker::sLogFile);
	fflush(_BJoystickTweaker::sLogFile);
}
#	define LOG_ERR(text...) LOG(text)
FILE *_BJoystickTweaker::sLogFile = NULL;
#else
#	define LOG(text...)
#	define LOG_ERR(text...) fprintf(stderr, text)
#endif

#define CALLED() LOG("%s\n", __PRETTY_FUNCTION__)


/** @brief Constructs a _BJoystickTweaker with no associated BJoystick. */
_BJoystickTweaker::_BJoystickTweaker()
{
#if DEBUG
	sLogFile = fopen("/var/log/joystick.log", "a");
#endif
	CALLED();
}


/**
 * @brief Constructs a _BJoystickTweaker associated with the given BJoystick.
 * @param stick Reference to the BJoystick whose device list will be managed.
 */
_BJoystickTweaker::_BJoystickTweaker(BJoystick &stick)
{
#if DEBUG
	sLogFile = fopen("/var/log/joystick.log", "a");
#endif
	CALLED();

	fJoystick = &stick;
}


/** @brief Destroys the _BJoystickTweaker object. */
_BJoystickTweaker::~_BJoystickTweaker()
{
}


/**
 * @brief Saves the current joystick configuration to the given entry.
 * @param ref Entry reference identifying where to save the config.
 * @return B_OK on success, or an error code on failure.
 */
status_t
_BJoystickTweaker::save_config(const entry_ref *ref)
{
	CALLED();
	return B_ERROR;
}


/**
 * @brief Recursively scans a directory for joystick device entries.
 *
 * Walks the directory tree rooted at \a rootEntry (or \a rootPath if
 * \a rootEntry is NULL), stripping the root prefix from each discovered
 * device path and appending the relative name to \a list.
 *
 * @param rootPath Absolute path of the scan root.
 * @param list BList of BString* to receive the discovered device names.
 * @param rootEntry Optional BEntry for the current directory level.
 * @return B_OK on success, or an error code on failure.
 */
status_t
_BJoystickTweaker::_ScanIncludingDisabled(const char *rootPath, BList *list,
	BEntry *rootEntry)
{
	BDirectory root;

	if (rootEntry != NULL)
		root.SetTo(rootEntry);
	else if (rootPath != NULL)
		root.SetTo(rootPath);
	else
		return B_ERROR;

	BEntry entry;

	ASSERT(list != NULL);
	while (root.GetNextEntry(&entry) == B_OK) {
		if (entry.IsDirectory()) {
			status_t result = _ScanIncludingDisabled(rootPath, list, &entry);
			if (result != B_OK)
				return result;

			continue;
		}

		BPath path;
		status_t result = entry.GetPath(&path);
		if (result != B_OK)
			return result;

		BString *deviceName = new(std::nothrow) BString(path.Path());
		if (deviceName == NULL)
			return B_NO_MEMORY;

		deviceName->RemoveFirst(rootPath);
		if (!list->AddItem(deviceName)) {
			delete deviceName;
			return B_ERROR;
		}
	}

	return B_OK;
}


/**
 * @brief Scans DEVICE_BASE_PATH for all joystick devices, including disabled ones.
 *
 * Clears the associated BJoystick's device list and repopulates it with all
 * entries found under the joystick base path.
 */
void
_BJoystickTweaker::scan_including_disabled()
{
	CALLED();
	_EmpyList(fJoystick->fDevices);
	_ScanIncludingDisabled(DEVICE_BASE_PATH, fJoystick->fDevices);
}


/**
 * @brief Empties a BList of BString pointers, deleting each element.
 * @param list The list to clear.
 */
void
_BJoystickTweaker::_EmpyList(BList *list)
{
	for (int32 i = 0; i < list->CountItems(); i++)
		delete (BString *)list->ItemAt(i);

	list->MakeEmpty();
}


/**
 * @brief Returns basic joystick info (stub).
 * @return B_OK on success, or an error code on failure.
 */
status_t
_BJoystickTweaker::get_info()
{
	CALLED();
	return B_ERROR;
}


/**
 * @brief Parses a joystick description file and fills a joystick_info struct.
 *
 * Opens the config file at JOYSTICK_CONFIG_BASE_PATH/<ref>, reads it line
 * by line, and delegates each line to _BuildFromJoystickDesc().
 *
 * @param info Pointer to the joystick_info structure to populate.
 * @param ref Name of the joystick description file (relative to config base).
 * @return B_OK on success, or B_ERROR if the file cannot be opened.
 */
status_t
_BJoystickTweaker::GetInfo(_joystick_info *info, const char *ref)
{
	CALLED();
	BString configFilePath(JOYSTICK_CONFIG_BASE_PATH);
	configFilePath.Append(ref);

	FILE *file = fopen(configFilePath.String(), "r");
	if (file == NULL)
		return B_ERROR;

	char line[STACK_STRING_BUFFER_SIZE];
	while (fgets(line, sizeof(line), file) != NULL) {
		int length = strlen(line);
		if (length > 0 && line[length - 1] == '\n')
			line[length - 1] = '\0';

		_BuildFromJoystickDesc(line, info);
	}

	fclose(file);
	return B_OK;
}


/**
 * @brief Parses a single line from a joystick description file.
 *
 * Recognises key=value pairs for module name, gadget name, num_axes,
 * num_hats, num_buttons, and num_sticks, and writes the parsed values into
 * the supplied _joystick_info.
 *
 * @param string One line of text from the description file.
 * @param info Pointer to the structure to update.
 */
void
_BJoystickTweaker::_BuildFromJoystickDesc(char *string, _joystick_info *info)
{
	BString str(string);
	str.RemoveAll("\"");

	if (str.IFindFirst("module") != -1) {
		str.RemoveFirst("module = ");
		strlcpy(info->module_info.module_name, str.String(),
			STACK_STRING_BUFFER_SIZE);
	} else if (str.IFindFirst("gadget") != -1) {
		str.RemoveFirst("gadget = ");
		strlcpy(info->module_info.device_name, str.String(),
			STACK_STRING_BUFFER_SIZE);
	} else if (str.IFindFirst("num_axes") != -1) {
		str.RemoveFirst("num_axes = ");
		info->module_info.num_axes = atoi(str.String());
	} else if (str.IFindFirst("num_hats") != -1) {
		str.RemoveFirst("num_hats = ");
		info->module_info.num_hats = atoi(str.String());
	} else if (str.IFindFirst("num_buttons") != -1) {
		str.RemoveFirst("num_buttons = ");
		info->module_info.num_buttons = atoi(str.String());
	} else if (str.IFindFirst("num_sticks") != -1) {
		str.RemoveFirst("num_sticks = ");
		info->module_info.num_sticks = atoi(str.String());
	} else {
		LOG("Path = %s\n", str.String());
	}
}


/**
 * @brief Sends an ioctl operation to the joystick device (stub).
 * @param op One of the B_JOYSTICK_* ioctl operation codes.
 * @return B_ERROR (not yet implemented).
 */
status_t
_BJoystickTweaker::SendIOCT(uint32 op)
{
	switch (op) {
		case B_JOYSTICK_SET_DEVICE_MODULE:
				break;

		case B_JOYSTICK_GET_DEVICE_MODULE:
				break;

		case B_JOYSTICK_GET_SPEED_COMPENSATION:
		case B_JOYSTICK_SET_SPEED_COMPENSATION:
		case B_JOYSTICK_GET_MAX_LATENCY:
		case B_JOYSTICK_SET_MAX_LATENCY:
		case B_JOYSTICK_SET_RAW_MODE:
		default:
				break;
	}

	return B_ERROR;
}
