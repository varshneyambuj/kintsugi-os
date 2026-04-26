/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2005, 2010-2012, Haiku.
 * Original authors: Axel Dörfler, Hamish Morrison, Alexander von Gluck.
 */

/** @file Settings.h
    @brief Settings model for the VirtualMemory preflet: swap state, size, volume. */

#ifndef SETTINGS_H
#define SETTINGS_H


#include <stdio.h>
#include <stdlib.h>

#include <Point.h>


/** @brief Returned by ReadSwapSettings() when no settings file is present. */
static const int32 kErrorSettingsNotFound = B_ERRORS_END + 1;
/** @brief Returned by ReadSwapSettings() when the file exists but is malformed. */
static const int32 kErrorSettingsInvalid = B_ERRORS_END + 2;
/** @brief Returned by ReadSwapSettings() when the recorded swap volume is missing. */
static const int32 kErrorVolumeNotFound = B_ERRORS_END + 3;


/**
 * @brief Holds the swap configuration model: enabled flag, automatic mode,
 *        size, volume, and saved window position.
 *
 * The model carries three snapshots: @c fCurrentSettings (the live UI
 * values), @c fInitialSettings (the values loaded from disk, used for
 * Revert), and @c fDefaultSettings (computed from system info and the boot
 * volume, used for Defaults). Mutators take a @c revertable flag so that
 * callers can either record a user edit (revertable) or commit a baseline
 * (non-revertable).
 */
class Settings {
public:
							Settings();

			/** @brief Returns whether swap is currently enabled in the model. */
			bool			SwapEnabled() const
								{ return fCurrentSettings.enabled; }
			/** @brief Returns whether automatic swap management is selected. */
			bool			SwapAutomatic() const
								{ return fCurrentSettings.automatic; }
			/** @brief Returns the requested swap file size in bytes. */
			off_t			SwapSize() const { return fCurrentSettings.size; }
			/** @brief Returns the device id of the swap-hosting volume. */
			dev_t			SwapVolume() { return fCurrentSettings.volume; }
			/** @brief Returns the persisted preference window top-left position. */
			BPoint			WindowPosition() const { return fWindowPosition; }


			void			SetSwapEnabled(bool enabled,
								bool revertable = true);
			void			SetSwapAutomatic(bool automatic,
								bool revertable = true);
			void			SetSwapSize(off_t size, bool revertable = true);
			void			SetSwapVolume(dev_t volume,
								bool revertable = true);
			void			SetWindowPosition(BPoint position);

			status_t		ReadWindowSettings();
			status_t		WriteWindowSettings();
			status_t		ReadSwapSettings();
			status_t		WriteSwapSettings();

			bool			IsRevertable();
			void			RevertSwapSettings();

			bool			IsDefaultable();
			void			DefaultSwapSettings(bool revertable = true);
private:
			/** @brief Plain-old-data view of one swap configuration snapshot. */
			struct SwapSettings {
				bool enabled;
				bool automatic;
				off_t size;
				dev_t volume;
			};

			BPoint			fWindowPosition;

			SwapSettings	fCurrentSettings;
			SwapSettings	fInitialSettings;
			SwapSettings	fDefaultSettings;
};

#endif	/* SETTINGS_H */
