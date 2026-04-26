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
 * MIT License. Copyright 2005-2006, 2010-2012, Haiku.
 * Original authors: Axel Dörfler, Hamish Morrison, Alexander von Gluck.
 */

/** @file SettingsWindow.h
    @brief BWindow and helper widgets for the VirtualMemory preflet. */

#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H


#include <MenuItem.h>
#include <Slider.h>
#include <StatusBar.h>
#include <Volume.h>
#include <Window.h>

#include "Settings.h"


class BStringView;
class BCheckBox;
class BSlider;
class BButton;
class BMenuField;


/**
 * @brief BSlider specialization that formats its current value as a byte size.
 *
 * The slider is calibrated in megabytes; UpdateText() multiplies by the
 * megabyte unit and produces a human-readable label via string_for_size().
 */
class SizeSlider : public BSlider {
public:
							SizeSlider(const char* name, const char* label,
								BMessage* message, int32 min, int32 max,
								uint32 flags);
	virtual 				~SizeSlider() {};

	virtual	const char*		UpdateText() const;

private:
	mutable	char			fText[128];
};


/**
 * @brief Menu item that represents one mountable BVolume in the volume picker.
 *
 * Inherits from BHandler so it can receive node-monitor messages and
 * regenerate its label when the underlying volume's root entry is renamed.
 */
class VolumeMenuItem : public BMenuItem, public BHandler {
public:
							VolumeMenuItem(BVolume volume, BMessage* message);
	virtual					~VolumeMenuItem() {}

	/** @brief Returns the BVolume backing this menu item. */
	virtual	BVolume			Volume() { return fVolume; }
	virtual	void			MessageReceived(BMessage* message);
	virtual	void			GenerateLabel();

private:
			BVolume			fVolume;
};


/**
 * @brief Top-level BWindow that hosts the VirtualMemory preflet UI.
 *
 * Loads the swap settings model, lays out the controls (enable checkbox,
 * automatic checkbox, volume picker, size slider, usage bar, defaults and
 * revert buttons), watches for volume mount/unmount changes, and persists
 * the settings on quit.
 */
class SettingsWindow : public BWindow {
public:
							SettingsWindow();
	virtual					~SettingsWindow() {};

	virtual void			MessageReceived(BMessage* message);
	virtual bool			QuitRequested();

private:
			status_t		_AddVolumeMenuItem(dev_t device);
			status_t		_RemoveVolumeMenuItem(dev_t device);
			VolumeMenuItem*	_FindVolumeMenuItem(dev_t device);

			void			_RecordChoices();
			void			_Update();
			void			_UpdateSwapInfo();

			BCheckBox*		fSwapEnabledCheckBox;
			BCheckBox*		fSwapAutomaticCheckBox;
			BSlider*		fSizeSlider;
			BButton*		fDefaultsButton;
			BButton*		fRevertButton;
			BStringView*	fWarningStringView;
			BMenuField*		fVolumeMenuField;
			BStatusBar*		fSwapUsageBar;
			Settings		fSettings;
			bool			fSetupComplete;
};

#endif	/* SETTINGS_WINDOW_H */
