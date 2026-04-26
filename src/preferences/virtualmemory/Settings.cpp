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
 *   Copyright 2005, Axel Dörfler, axeld@pinc-software.de
 *   All rights reserved. Distributed under the terms of the MIT License.
 *
 *   Copyright 2010-2012 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Hamish Morrison, hamish@lavabit.com
 *       Alexander von Gluck, kallisti5@unixzen.com
 */


/**
 * @file Settings.cpp
 * @brief Implementation of the Settings model for the VirtualMemory preflet.
 *
 * Reads and writes both the window-position file and the kernel
 * @c virtual_memory driver settings file under
 * @c B_USER_SETTINGS_DIRECTORY/kernel/drivers. Maintains current, initial,
 * and default snapshots so the UI can offer Revert and Defaults actions.
 *
 * @see SettingsWindow
 */


#include "Settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <AutoDeleter.h>
#include <AutoDeleterDrivers.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <VolumeRoster.h>

#include <driver_settings.h>


/** @brief Filename, relative to B_USER_SETTINGS_DIRECTORY, of the saved window position. */
static const char* const kWindowSettingsFile = "virtualmemory_preferences";
/** @brief Driver settings name used by the kernel for the swap configuration. */
static const char* const kVirtualMemorySettings = "virtual_memory";
/** @brief One mebibyte expressed in bytes. */
static const off_t kMegaByte = 1024 * 1024;
/** @brief One gibibyte expressed in bytes. */
static const off_t kGigaByte = kMegaByte * 1024;


/**
 * @brief Computes the default swap configuration from system memory and
 *        the boot volume.
 *
 * Defaults are: swap enabled, automatic, size equal to physical RAM (or
 * doubled when RAM is at most 1 GiB to mirror kernel behaviour), and the
 * boot volume as the swap host.
 */
Settings::Settings()
{
	fDefaultSettings.enabled = true;
	fDefaultSettings.automatic = true;

	system_info sysInfo;
	get_system_info(&sysInfo);

	fDefaultSettings.size = (off_t)sysInfo.max_pages * B_PAGE_SIZE;
	if (fDefaultSettings.size <= kGigaByte) {
		// Memory under 1GB? double the swap
		// This matches the behaviour of the kernel
		fDefaultSettings.size *= 2;
	}

	fDefaultSettings.volume = dev_for_path("/boot");
}


/**
 * @brief Sets the swap-enabled flag in the current snapshot.
 *
 * @param enabled    New value for the enabled flag.
 * @param revertable When false, the change is also written to the initial
 *                   snapshot so it becomes the new Revert baseline.
 */
void
Settings::SetSwapEnabled(bool enabled, bool revertable)
{
	fCurrentSettings.enabled = enabled;
	if (!revertable)
		fInitialSettings.enabled = enabled;
}


/**
 * @brief Sets the automatic-swap flag in the current snapshot.
 *
 * @param automatic  New value for the automatic flag.
 * @param revertable When false, also updates the initial snapshot.
 */
void
Settings::SetSwapAutomatic(bool automatic, bool revertable)
{
	fCurrentSettings.automatic = automatic;
	if (!revertable)
		fInitialSettings.automatic = automatic;
}


/**
 * @brief Sets the requested swap file size in the current snapshot.
 *
 * @param size       Desired swap size in bytes.
 * @param revertable When false, also updates the initial snapshot.
 */
void
Settings::SetSwapSize(off_t size, bool revertable)
{
	fCurrentSettings.size = size;
	if (!revertable)
		fInitialSettings.size = size;
}


/**
 * @brief Sets the swap-hosting volume in the current snapshot.
 *
 * @param volume     Device id (@c dev_t) of the chosen volume.
 * @param revertable When false, also updates the initial snapshot.
 */
void
Settings::SetSwapVolume(dev_t volume, bool revertable)
{
	fCurrentSettings.volume = volume;
	if (!revertable)
		fInitialSettings.volume = volume;

}


/**
 * @brief Records the current preflet window position for later restore.
 *
 * @param position Top-left point in screen coordinates.
 */
void
Settings::SetWindowPosition(BPoint position)
{
	fWindowPosition = position;
}


/**
 * @brief Loads the previously saved preflet window position from disk.
 *
 * @return @c B_OK on success.
 * @retval B_OK     The stored position was read into @c fWindowPosition.
 * @retval B_ERROR  The settings directory or file could not be opened, or
 *                  the file did not contain a full BPoint.
 */
status_t
Settings::ReadWindowSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return B_ERROR;

	path.Append(kWindowSettingsFile);
	BFile file;
	if (file.SetTo(path.Path(), B_READ_ONLY) != B_OK)
		return B_ERROR;

	if (file.Read(&fWindowPosition, sizeof(BPoint)) == sizeof(BPoint))
		return B_OK;

	return B_ERROR;
}


/**
 * @brief Persists the preflet window position to the user settings file.
 *
 * The file is created (and truncated if present) under
 * @c B_USER_SETTINGS_DIRECTORY.
 *
 * @return @c B_OK on success.
 * @retval B_OK     The position was written.
 * @retval B_ERROR  The settings directory or file could not be opened.
 */
status_t
Settings::WriteWindowSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) < B_OK)
		return B_ERROR;

	path.Append(kWindowSettingsFile);

	BFile file;
	if (file.SetTo(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE)
		!= B_OK)
		return B_ERROR;

	file.Write(&fWindowPosition, sizeof(BPoint));
	return B_OK;
}


/**
 * @brief Loads the kernel swap settings and resolves the recorded volume.
 *
 * Parses the @c virtual_memory driver settings file. The recorded volume is
 * matched against the currently mounted set by name, device, capacity, and
 * filesystem; the highest-scoring persistent, writable, fixed,
 * non-shared volume wins (provided it scores at least 4).
 *
 * @return Status code.
 * @retval B_OK                       Settings were loaded and the volume found.
 * @retval kErrorSettingsNotFound     The driver settings file is not present.
 * @retval kErrorSettingsInvalid      A required field was missing or empty.
 * @retval kErrorVolumeNotFound       The stored volume could not be matched.
 */
status_t
Settings::ReadSwapSettings()
{
	DriverSettingsUnloader settings(
		load_driver_settings(kVirtualMemorySettings));
	if (!settings.IsSet())
		return kErrorSettingsNotFound;

	const char* enabled = get_driver_parameter(settings.Get(),
		"vm", NULL, NULL);
	const char* automatic = get_driver_parameter(settings.Get(),
		"swap_auto", NULL, NULL);
	const char* size = get_driver_parameter(settings.Get(),
		"swap_size", NULL, NULL);
	const char* volume = get_driver_parameter(settings.Get(),
		"swap_volume_name", NULL, NULL);
	const char* device = get_driver_parameter(settings.Get(),
		"swap_volume_device", NULL, NULL);
	const char* filesystem = get_driver_parameter(settings.Get(),
		"swap_volume_filesystem", NULL, NULL);
	const char* capacity = get_driver_parameter(settings.Get(),
		"swap_volume_capacity", NULL, NULL);

	if (enabled == NULL	|| automatic == NULL || size == NULL || device == NULL
		|| volume == NULL || capacity == NULL || filesystem == NULL)
		return kErrorSettingsInvalid;

	off_t volCapacity = atoll(capacity);

	SetSwapEnabled(get_driver_boolean_parameter(settings.Get(),
		"vm", true, false));
	SetSwapAutomatic(get_driver_boolean_parameter(settings.Get(),
		"swap_auto", true, false));
	SetSwapSize(atoll(size));

	int32 bestScore = -1;
	dev_t bestVol = -1;

	BVolume vol;
	fs_info volStat;
	BVolumeRoster roster;
	while (roster.GetNextVolume(&vol) == B_OK) {
		if (!vol.IsPersistent() || vol.IsReadOnly() || vol.IsRemovable()
			|| vol.IsShared())
			continue;
		if (fs_stat_dev(vol.Device(), &volStat) == 0) {
			int32 score = 0;
			if (strcmp(volume, volStat.volume_name) == 0)
				score += 4;
			if (strcmp(device, volStat.device_name) == 0)
				score += 3;
			if (volCapacity == volStat.total_blocks * volStat.block_size)
				score += 2;
			if (strcmp(filesystem, volStat.fsh_name) == 0)
				score += 1;
			if (score >= 4 && score > bestScore) {
				bestVol = vol.Device();
				bestScore = score;
			}
		}
	}

	SetSwapVolume(bestVol);
	fInitialSettings = fCurrentSettings;

	if (bestVol < 0)
		return kErrorVolumeNotFound;

	return B_OK;
}


/**
 * @brief Writes the current swap configuration to the kernel driver
 *        settings file.
 *
 * Composes a key/value text representation including @c vm,
 * @c swap_auto, @c swap_size, @c swap_volume_name, @c swap_volume_device,
 * @c swap_volume_filesystem, and @c swap_volume_capacity, and stores it
 * under @c B_USER_SETTINGS_DIRECTORY/kernel/drivers/virtual_memory.
 *
 * @return @c B_OK on success.
 * @retval B_OK     The file was written.
 * @retval B_ERROR  The settings directory could not be located, the file
 *                  could not be opened, or the chosen volume could not be
 *                  stat'd.
 */
status_t
Settings::WriteSwapSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return B_ERROR;

	path.Append("kernel/drivers");
	path.Append(kVirtualMemorySettings);

	BFile file;
	if (file.SetTo(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE)
		!= B_OK)
		return B_ERROR;

	fs_info info;
	if (fs_stat_dev(SwapVolume(), &info) != 0)
		return B_ERROR;

	char buffer[1024];
	snprintf(buffer, sizeof(buffer), "vm %s\nswap_auto %s\nswap_size %"
		B_PRIdOFF "\nswap_volume_name %s\nswap_volume_device %s\n"
		"swap_volume_filesystem %s\nswap_volume_capacity %" B_PRIdOFF "\n",
		SwapEnabled() ? "on" : "off", SwapAutomatic() ? "yes" : "no",
		SwapSize(), info.volume_name, info.device_name, info.fsh_name,
		info.total_blocks * info.block_size);

	file.Write(buffer, strlen(buffer));
	return B_OK;
}


/**
 * @brief Reports whether the current settings differ from the loaded baseline.
 *
 * @return @c true when any of enabled, automatic, size, or volume has been
 *         modified since the last read or non-revertable write.
 */
bool
Settings::IsRevertable()
{
	return SwapEnabled() != fInitialSettings.enabled
		|| SwapAutomatic() != fInitialSettings.automatic
		|| SwapSize() != fInitialSettings.size
		|| SwapVolume() != fInitialSettings.volume;
}


/**
 * @brief Restores the current snapshot to the values held in the initial
 *        snapshot.
 */
void
Settings::RevertSwapSettings()
{
	SetSwapEnabled(fInitialSettings.enabled);
	SetSwapAutomatic(fInitialSettings.automatic);
	SetSwapSize(fInitialSettings.size);
	SetSwapVolume(fInitialSettings.volume);
}


/**
 * @brief Reports whether the current settings differ from the computed defaults.
 *
 * @return @c true when any field would change if DefaultSwapSettings() ran.
 */
bool
Settings::IsDefaultable()
{
	return SwapEnabled() != fDefaultSettings.enabled
		|| SwapAutomatic() != fDefaultSettings.automatic
		|| SwapSize() != fDefaultSettings.size
		|| SwapVolume() != fDefaultSettings.volume;
}


/**
 * @brief Replaces the current snapshot with the computed defaults.
 *
 * @param revertable When false, the defaults also become the new initial
 *                   snapshot, so the Revert button targets them.
 */
void
Settings::DefaultSwapSettings(bool revertable)
{
	SetSwapEnabled(fDefaultSettings.enabled);
	SetSwapAutomatic(fDefaultSettings.automatic);
	SetSwapSize(fDefaultSettings.size);
	SetSwapVolume(fDefaultSettings.volume);
	if (!revertable)
		fInitialSettings = fDefaultSettings;
}
