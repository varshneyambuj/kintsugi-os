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
 *   Copyright 2005-2009, Axel Dörfler, axeld@pinc-software.de
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
 * @file SettingsWindow.cpp
 * @brief Implementation of the VirtualMemory preflet's main BWindow.
 *
 * Builds the layout (enable/automatic checkboxes, volume picker, size
 * slider, usage bar, Defaults/Revert buttons), watches volume mounts via
 * the node monitor, validates the loaded Settings on startup, and writes
 * both the window position and the swap settings on quit.
 *
 * @see Settings
 */


#include "SettingsWindow.h"

#include <Application.h>
#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <MenuField.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <StringForSize.h>
#include <StringView.h>
#include <String.h>
#include <Slider.h>
#include <system_info.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include "Settings.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SettingsWindow"


/** @brief Message sent by the Defaults button. */
static const uint32 kMsgDefaults = 'dflt';
/** @brief Message sent by the Revert button. */
static const uint32 kMsgRevert = 'rvrt';
/** @brief Message sent when the size slider value changes. */
static const uint32 kMsgSliderUpdate = 'slup';
/** @brief Message sent when the enable-swap checkbox is toggled. */
static const uint32 kMsgSwapEnabledUpdate = 'swen';
/** @brief Message sent when the automatic-swap checkbox is toggled. */
static const uint32 kMsgSwapAutomaticUpdate = 'swat';
/** @brief Message sent when an entry in the volume picker is chosen. */
static const uint32 kMsgVolumeSelected = 'vlsl';
/** @brief One mebibyte expressed in bytes; the slider's tick unit. */
static const off_t kMegaByte = 1024 * 1024;
/** @brief Cached device id for the boot volume, set during window construction. */
static dev_t gBootDev = -1;


/**
 * @brief Constructs the slider with horizontal orientation and the system
 *        control highlight color.
 *
 * @param name    Internal BView name.
 * @param label   Label drawn above the slider.
 * @param message Message dispatched on value change.
 * @param min     Minimum slider value (in megabytes).
 * @param max     Maximum slider value (in megabytes).
 * @param flags   Standard BView flags.
 */
SizeSlider::SizeSlider(const char* name, const char* label,
	BMessage* message, int32 min, int32 max, uint32 flags)
	:
	BSlider(name, label, message, min, max, B_HORIZONTAL,
		B_BLOCK_THUMB, flags)
{
	rgb_color color = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
	UseFillColor(true, &color);
}


/**
 * @brief Returns the slider's current value as a human-readable byte size.
 *
 * @return Pointer to an internal mutable buffer holding the formatted text.
 *         The buffer is reused across calls; do not free it.
 */
const char*
SizeSlider::UpdateText() const
{
	return string_for_size(Value() * kMegaByte, fText, sizeof(fText));
}


/**
 * @brief Constructs a menu item bound to the given BVolume.
 *
 * @param volume  Volume to represent.
 * @param message Message dispatched when the item is selected.
 */
VolumeMenuItem::VolumeMenuItem(BVolume volume, BMessage* message)
	:
	BMenuItem("", message),
	fVolume(volume)
{
	GenerateLabel();
}


/**
 * @brief BHandler hook that refreshes the label on a node-monitor rename.
 *
 * @param message Incoming message; only @c B_NODE_MONITOR with opcode
 *                @c B_ENTRY_MOVED triggers a label rebuild.
 */
void
VolumeMenuItem::MessageReceived(BMessage* message)
{
	if (message->what == B_NODE_MONITOR) {
		int32 code;
		if (message->FindInt32("opcode", &code) == B_OK)
			if (code == B_ENTRY_MOVED)
				GenerateLabel();
	}
}


/**
 * @brief Builds the menu item label as @c "name (mount-path)".
 *
 * Falls back to just the volume name when the mount path cannot be queried.
 */
void
VolumeMenuItem::GenerateLabel()
{
	char name[B_FILE_NAME_LENGTH + 1];
	fVolume.GetName(name);

	BDirectory dir;
	if (fVolume.GetRootDirectory(&dir) == B_OK) {
		BEntry entry;
		if (dir.GetEntry(&entry) == B_OK) {
			BPath path;
			if (entry.GetPath(&path) == B_OK) {
				BString label;
				label << name << " (" << path.Path() << ")";
				SetLabel(label);
				return;
			}
		}
	}

	SetLabel(name);
}


/**
 * @brief Constructs the preflet window, loads settings, and lays out controls.
 *
 * On construction the window: caches the boot device, restores its previous
 * position (or centers on screen), reads the kernel swap settings (with
 * BAlert prompts for malformed or volume-missing files), populates the
 * volume picker from the BVolumeRoster, registers a node monitor for
 * mount/unmount events, builds the layout, and refreshes UI state.
 *
 * @note If the user picks "Quit" in either error dialog, the window posts
 *       @c B_QUIT_REQUESTED to the application and returns early.
 */
SettingsWindow::SettingsWindow()
	:
	BWindow(BRect(0, 0, 269, 172), B_TRANSLATE_SYSTEM_NAME("VirtualMemory"),
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS
		| B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fSwapEnabledCheckBox(NULL),
	fSwapAutomaticCheckBox(NULL),
	fSizeSlider(NULL),
	fDefaultsButton(NULL),
	fRevertButton(NULL),
	fWarningStringView(NULL),
	fVolumeMenuField(NULL),
	fSwapUsageBar(NULL),
	fSetupComplete(false)
{
	gBootDev = dev_for_path("/boot");
	BAlignment align(B_ALIGN_LEFT, B_ALIGN_MIDDLE);

	if (fSettings.ReadWindowSettings() != B_OK)
		CenterOnScreen();
	else
		MoveTo(fSettings.WindowPosition());

	status_t result = fSettings.ReadSwapSettings();
	if (result == kErrorSettingsNotFound)
		fSettings.DefaultSwapSettings(false);
	else if (result == kErrorSettingsInvalid) {
		int32 choice = (new BAlert(B_TRANSLATE_SYSTEM_NAME("VirtualMemory"),
			B_TRANSLATE("The settings specified in the settings file "
			"are invalid. You can load the defaults or quit."),
			B_TRANSLATE("Load defaults"), B_TRANSLATE("Quit")))->Go();
		if (choice == 1) {
			be_app->PostMessage(B_QUIT_REQUESTED);
			return;
		}
		fSettings.DefaultSwapSettings(false);
	} else if (result == kErrorVolumeNotFound) {
		int32 choice = (new BAlert(B_TRANSLATE_SYSTEM_NAME("VirtualMemory"),
			B_TRANSLATE("The volume specified in the settings file "
			"could not be found. You can use the boot volume or quit."),
			B_TRANSLATE("Use boot volume"), B_TRANSLATE("Quit")))->Go();
		if (choice == 1) {
			be_app->PostMessage(B_QUIT_REQUESTED);
			return;
		}
		fSettings.SetSwapVolume(gBootDev, false);
	}

	fSwapEnabledCheckBox = new BCheckBox("enable swap",
		B_TRANSLATE("Enable virtual memory"),
		new BMessage(kMsgSwapEnabledUpdate));
	fSwapEnabledCheckBox->SetExplicitAlignment(align);

	fSwapAutomaticCheckBox = new BCheckBox("auto swap",
		B_TRANSLATE("Automatic swap management"),
		new BMessage(kMsgSwapAutomaticUpdate));
	fSwapEnabledCheckBox->SetExplicitAlignment(align);

	fSwapUsageBar = new BStatusBar("swap usage");

	BPopUpMenu* menu = new BPopUpMenu("volume menu");
	fVolumeMenuField = new BMenuField("volume menu field",
		B_TRANSLATE("Use volume:"), menu);
	fVolumeMenuField->SetExplicitAlignment(align);

	BVolumeRoster roster;
	BVolume vol;
	while (roster.GetNextVolume(&vol) == B_OK) {
		if (!vol.IsPersistent() || vol.IsReadOnly() || vol.IsRemovable()
			|| vol.IsShared())
			continue;
		_AddVolumeMenuItem(vol.Device());
	}

	watch_node(NULL, B_WATCH_MOUNT, this, this);

	fSizeSlider = new SizeSlider("size slider",
		B_TRANSLATE("Requested swap file size:"),
		new BMessage(kMsgSliderUpdate),	0, 0, B_WILL_DRAW | B_FRAME_EVENTS);
	fSizeSlider->SetViewColor(255, 0, 255);
	fSizeSlider->SetExplicitAlignment(align);

	fWarningStringView = new BStringView("warning",
		B_TRANSLATE("Changes will take effect upon reboot."));

	BBox* box = new BBox("box");
	box->SetLabel(fSwapEnabledCheckBox);

	box->AddChild(BLayoutBuilder::Group<>(B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fSwapUsageBar)
		.Add(fSwapAutomaticCheckBox)
		.Add(fVolumeMenuField)
		.Add(fSizeSlider)
		.Add(fWarningStringView)
		.View());

	fDefaultsButton = new BButton("defaults", B_TRANSLATE("Defaults"),
		new BMessage(kMsgDefaults));

	fRevertButton = new BButton("revert", B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert));
	fRevertButton->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(box)
		.AddGroup(B_HORIZONTAL)
			.Add(fDefaultsButton)
			.Add(fRevertButton)
			.AddGlue()
		.End();

	BScreen screen;
	BRect screenFrame = screen.Frame();
	if (!screenFrame.Contains(fSettings.WindowPosition()))
		CenterOnScreen();

#ifdef SWAP_VOLUME_IMPLEMENTED
	// Validate the volume specified in settings file
	status_t result = fSettings.SwapVolume().InitCheck();

	if (result != B_OK) {
		BAlert* alert = new BAlert(B_TRANSLATE_SYSTEM_NAME("VirtualMemory"),
			B_TRANSLATE("The swap volume specified in the settings file is ",
			"invalid.\n You can keep the current setting or switch to the "
			"default swap volume."),
			B_TRANSLATE("Keep"), B_TRANSLATE("Switch"), NULL,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		int32 choice = alert->Go();
		if (choice == 1) {
			BVolumeRoster volumeRoster;
			BVolume bootVolume;
			volumeRoster.GetBootVolume(&bootVolume);
			fSettings.SetSwapVolume(bootVolume);
		}
	}
#endif

	_Update();

	// TODO: We may want to run this at an interval
	_UpdateSwapInfo();
	fSetupComplete = true;
}


/**
 * @brief BWindow message hook: dispatches UI and node-monitor events.
 *
 * Handles mount/unmount notifications by updating the volume picker, and
 * acts on the various control messages (Revert, Defaults, slider, volume
 * pick, enable/automatic toggles). When the user disables swap a
 * confirmation alert is shown.
 *
 * @param message Incoming message; unhandled messages fall through to
 *                BWindow::MessageReceived().
 */
void
SettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NODE_MONITOR:
		{
			int32 opcode;
			if (message->FindInt32("opcode", &opcode) != B_OK)
				break;
			dev_t device;
			if (opcode == B_DEVICE_MOUNTED
				&& message->FindInt32("new device", &device) == B_OK) {
				BVolume vol(device);
				if (!vol.IsPersistent() || vol.IsReadOnly()
					|| vol.IsRemovable() || vol.IsShared()) {
					break;
				}
				_AddVolumeMenuItem(device);
			} else if (opcode == B_DEVICE_UNMOUNTED
				&& message->FindInt32("device", &device) == B_OK) {
				_RemoveVolumeMenuItem(device);
			}
			_Update();
			break;
		}
		case kMsgRevert:
			fSettings.RevertSwapSettings();
			_Update();
			break;
		case kMsgDefaults:
			fSettings.DefaultSwapSettings();
			_Update();
			break;
		case kMsgSliderUpdate:
			_RecordChoices();
			_Update();
			break;
		case kMsgVolumeSelected:
			_RecordChoices();
			_Update();
			break;
		case kMsgSwapEnabledUpdate:
		{
			if (fSwapEnabledCheckBox->Value() == 0) {
				// print out warning, give the user the
				// time to think about it :)
				// ToDo: maybe we want to remove this possibility in the GUI
				// as Be did, but I thought a proper warning could be helpful
				// (for those that want to change that anyway)
				BAlert* alert = new BAlert(
					B_TRANSLATE_SYSTEM_NAME("VirtualMemory"), B_TRANSLATE(
					"Disabling virtual memory will have unwanted effects on "
					"system stability once the memory is used up.\n"
					"Virtual memory does not affect system performance "
					"until this point is reached.\n\n"
					"Are you really sure you want to turn it off?"),
					B_TRANSLATE("Turn off"), B_TRANSLATE("Keep enabled"), NULL,
					B_WIDTH_AS_USUAL, B_WARNING_ALERT);
				alert->SetShortcut(1, B_ESCAPE);
				int32 choice = alert->Go();
				if (choice == 1) {
					fSwapEnabledCheckBox->SetValue(1);
					break;
				}
			}

			_RecordChoices();
			_Update();
			break;
		}
		case kMsgSwapAutomaticUpdate:
		{
			_RecordChoices();
			_Update();
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}


/**
 * @brief BWindow hook called when the user requests to close the window.
 *
 * Persists the window position and the current swap settings, then asks
 * the application to quit.
 *
 * @return Always @c true; the window is allowed to close.
 * @note When the window failed to finish setup (early return from the
 *       constructor), no settings are written.
 */
bool
SettingsWindow::QuitRequested()
{
	if (!fSetupComplete)
		return true;

	fSettings.SetWindowPosition(Frame().LeftTop());
	_RecordChoices();
	fSettings.WriteWindowSettings();
	fSettings.WriteSwapSettings();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


/**
 * @brief Adds a VolumeMenuItem for @a device to the volume picker.
 *
 * Also subscribes the new item to node-monitor name changes on the
 * volume's root entry.
 *
 * @param device Device id of the volume to add.
 * @return Status code.
 * @retval B_OK     The item was created and inserted.
 * @retval B_ERROR  An item for @a device already exists.
 */
status_t
SettingsWindow::_AddVolumeMenuItem(dev_t device)
{
	if (_FindVolumeMenuItem(device) != NULL)
		return B_ERROR;

	VolumeMenuItem* item = new VolumeMenuItem(device,
		new BMessage(kMsgVolumeSelected));

	fs_info info;
	if (fs_stat_dev(device, &info) == 0) {
		node_ref node;
		node.device = info.dev;
		node.node = info.root;
		AddHandler(item);
		watch_node(&node, B_WATCH_NAME, item);
	}

	fVolumeMenuField->Menu()->AddItem(item);
	return B_OK;
}


/**
 * @brief Removes the VolumeMenuItem for @a device from the volume picker.
 *
 * @param device Device id of the volume to drop.
 * @return Status code.
 * @retval B_OK     The matching item was removed and deleted.
 * @retval B_ERROR  No item for @a device was found.
 */
status_t
SettingsWindow::_RemoveVolumeMenuItem(dev_t device)
{
	VolumeMenuItem* item = _FindVolumeMenuItem(device);
	if (item != NULL) {
		fVolumeMenuField->Menu()->RemoveItem(item);
		delete item;
		return B_OK;
	}
	return B_ERROR;
}


/**
 * @brief Locates the VolumeMenuItem in the picker that matches @a device.
 *
 * @param device Device id to look up.
 * @return Pointer to the matching item, or @c NULL when no item matches.
 */
VolumeMenuItem*
SettingsWindow::_FindVolumeMenuItem(dev_t device)
{
	VolumeMenuItem* item = NULL;
	int32 count = fVolumeMenuField->Menu()->CountItems();
	for (int i = 0; i < count; i++) {
		item = (VolumeMenuItem*)fVolumeMenuField->Menu()->ItemAt(i);
		if (item->Volume().Device() == device)
			return item;
	}

	return NULL;
}


/**
 * @brief Pushes the current control values into the Settings model.
 *
 * Called whenever a control changes so that the model stays in sync with
 * the UI; the next _Update() pass then re-derives button enable states.
 */
void
SettingsWindow::_RecordChoices()
{
	fSettings.SetSwapAutomatic(fSwapAutomaticCheckBox->Value());
	fSettings.SetSwapEnabled(fSwapEnabledCheckBox->Value());
	fSettings.SetSwapSize((off_t)fSizeSlider->Value() * kMegaByte);
	fSettings.SetSwapVolume(((VolumeMenuItem*)fVolumeMenuField
		->Menu()->FindMarked())->Volume().Device());
}


/**
 * @brief Refreshes UI state from the Settings model.
 *
 * Updates checkbox values, the size-slider range and label, the warning
 * string visibility, the Revert and Defaults button enable states, and
 * the dependent controls (the slider and volume picker are disabled when
 * swap is off or running in automatic mode). The slider range adapts to
 * the chosen volume's free space, leaving a 15 % safety margin.
 */
void
SettingsWindow::_Update()
{
	fSwapEnabledCheckBox->SetValue(fSettings.SwapEnabled());
	fSwapAutomaticCheckBox->SetValue(fSettings.SwapAutomatic());

	VolumeMenuItem* item = _FindVolumeMenuItem(fSettings.SwapVolume());
	if (item != NULL) {
		fSizeSlider->SetEnabled(true);
		item->SetMarked(true);
		BEntry swapFile;
		if (gBootDev == item->Volume().Device())
			swapFile.SetTo("/var/swap");
		else {
			BDirectory root;
			item->Volume().GetRootDirectory(&root);
			swapFile.SetTo(&root, "swap");
		}

		off_t swapFileSize = 0;
		swapFile.GetSize(&swapFileSize);

		char sizeStr[16];

		off_t freeSpace = item->Volume().FreeBytes() + swapFileSize;
		off_t safeSpace = freeSpace - (off_t)(0.15 * freeSpace);
		(safeSpace >>= 20) <<= 20;
		off_t minSize = B_PAGE_SIZE + kMegaByte;
		(minSize >>= 20) <<= 20;
		BString minLabel, maxLabel;
		minLabel << string_for_size(minSize, sizeStr, sizeof(sizeStr));
		maxLabel << string_for_size(safeSpace, sizeStr, sizeof(sizeStr));

		fSizeSlider->SetLimitLabels(minLabel.String(), maxLabel.String());
		fSizeSlider->SetLimits(minSize / kMegaByte, safeSpace / kMegaByte);
		fSizeSlider->SetValue(fSettings.SwapSize() / kMegaByte);
	} else
		fSizeSlider->SetEnabled(false);

	bool revertable = fSettings.IsRevertable();
	if (revertable)
		fWarningStringView->Show();
	else
		fWarningStringView->Hide();

	fRevertButton->SetEnabled(revertable);
	fDefaultsButton->SetEnabled(fSettings.IsDefaultable());

	// Automatic Swap depends on swap being enabled
	fSwapAutomaticCheckBox->SetEnabled(fSettings.SwapEnabled());

	// Manual swap settings depend on enabled swap
	// and automatic swap being disabled
	fSizeSlider->SetEnabled(fSettings.SwapEnabled()
		&& !fSwapAutomaticCheckBox->Value());
	fVolumeMenuField->SetEnabled(fSettings.SwapEnabled()
		&& !fSwapAutomaticCheckBox->Value());
}


/**
 * @brief Refreshes the swap usage status bar from current system info.
 *
 * Queries the kernel for the maximum and free swap pages, formats the
 * values for display, and pushes them into the @c fSwapUsageBar widget.
 */
void
SettingsWindow::_UpdateSwapInfo()
{
	system_info info;
	get_system_info(&info);

	off_t currentSwapSize = info.max_swap_pages * B_PAGE_SIZE;
	off_t currentSwapUsed
		= (info.max_swap_pages - info.free_swap_pages) * B_PAGE_SIZE;

	char sizeStr[16];
	BString swapSizeStr = string_for_size(currentSwapSize, sizeStr,
		sizeof(sizeStr));
	BString swapUsedStr = string_for_size(currentSwapUsed, sizeStr,
		sizeof(sizeStr));

	BString string = swapUsedStr << " / " << swapSizeStr;

	fSwapUsageBar->SetMaxValue(currentSwapSize / kMegaByte);
	fSwapUsageBar->Update(currentSwapUsed / kMegaByte,
		B_TRANSLATE("Current Swap:"), string.String());
}
