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
 *   Open Tracker License
 *
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of
 *   this software and associated documentation files (the "Software"), to deal in
 *   the Software without restriction, including without limitation the rights to
 *   use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *   of the Software, and to permit persons to whom the Software is furnished to do
 *   so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice applies to all licensees
 *   and shall be included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 *   AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
 *   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *   Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
 *   trademarks of Be Incorporated in the United States and other countries.
 *   All rights reserved.
 */


/**
 * @file AutoMounterSettings.cpp
 * @brief Settings panel for the Tracker auto-mounter disk management preferences.
 *
 * Implements AutomountSettingsPanel, a SettingsView subclass that lets users
 * configure automatic and boot-time disk mounting behaviour. The panel communicates
 * with the mount server via BMessenger to query and apply mounting parameters.
 *
 * @see SettingsView, BMountServer
 */


#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Debug.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Message.h>
#include <RadioButton.h>
#include <SeparatorView.h>
#include <SpaceLayoutItem.h>
#include <Window.h>

#include <MountServer.h>

#include "SettingsViews.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AutoMounterSettings"


const uint32 kAutomountSettingsChanged = 'achg';
const uint32 kBootMountSettingsChanged = 'bchg';
const uint32 kEjectWhenUnmountingChanged = 'ejct';


//	#pragma mark - AutomountSettingsPanel


/**
 * @brief Construct the automount settings panel and lay out all controls.
 *
 * Creates two grouped boxes (automatic disk mounting and boot-time mounting),
 * populates them with radio buttons and checkboxes, fetches current settings
 * from the mount server, and applies them to the UI.
 */
AutomountSettingsPanel::AutomountSettingsPanel()
	:
	SettingsView(""),
	fTarget(kMountServerSignature)
{
	const float spacing = be_control_look->DefaultItemSpacing();

	// "Automatic Disk Mounting" group

	BBox* autoMountBox = new BBox("autoMountBox", B_WILL_DRAW | B_FRAME_EVENTS
		| B_PULSE_NEEDED | B_NAVIGABLE_JUMP);
	autoMountBox->SetLabel(B_TRANSLATE("Automatic disk mounting"));
	BGroupLayout* autoMountLayout = new BGroupLayout(B_VERTICAL, 0);
	autoMountBox->SetLayout(autoMountLayout);
	autoMountLayout->SetInsets(spacing,
		autoMountBox->InnerFrame().top + spacing / 2, spacing, spacing);

	fScanningDisabledCheck = new BRadioButton("scanningOff",
		B_TRANSLATE("Don't automount"),
		new BMessage(kAutomountSettingsChanged));

	fAutoMountAllBFSCheck = new BRadioButton("autoBFS",
		B_TRANSLATE("All Haiku disks"),
			new BMessage(kAutomountSettingsChanged));

	fAutoMountAllCheck = new BRadioButton("autoAll",
		B_TRANSLATE("All disks"), new BMessage(kAutomountSettingsChanged));

	// "Disk Mounting During Boot" group

	BBox* bootMountBox = new BBox("", B_WILL_DRAW | B_FRAME_EVENTS
		| B_PULSE_NEEDED | B_NAVIGABLE_JUMP);
	bootMountBox->SetLabel(B_TRANSLATE("Disk mounting during boot"));
	BGroupLayout* bootMountLayout = new BGroupLayout(B_VERTICAL, 0);
	bootMountBox->SetLayout(bootMountLayout);
	bootMountLayout->SetInsets(spacing,
		bootMountBox->InnerFrame().top + spacing / 2, spacing, spacing);

	fInitialDontMountCheck = new BRadioButton("initialNone",
		B_TRANSLATE("Only the boot disk"),
		new BMessage(kBootMountSettingsChanged));

	fInitialMountRestoreCheck = new BRadioButton("initialRestore",
		B_TRANSLATE("Previously mounted disks"),
		new BMessage(kBootMountSettingsChanged));

	fInitialMountAllBFSCheck = new BRadioButton("initialBFS",
		B_TRANSLATE("All Haiku disks"),
		new BMessage(kBootMountSettingsChanged));

	fInitialMountAllCheck = new BRadioButton("initialAll",
		B_TRANSLATE("All disks"), new BMessage(kBootMountSettingsChanged));

	fEjectWhenUnmountingCheckBox = new BCheckBox("ejectWhenUnmounting",
		B_TRANSLATE("Eject when unmounting"),
		new BMessage(kEjectWhenUnmountingChanged));

	// Buttons

	fMountAllNow = new BButton("mountAll", B_TRANSLATE("Mount all disks now"),
		new BMessage(kMountAllNow));

	// Layout the controls

	BGroupView* contentView = new BGroupView(B_VERTICAL, 0);
	AddChild(contentView);
	BLayoutBuilder::Group<>(contentView)
		.AddGroup(B_VERTICAL, spacing)
			.SetInsets(spacing, spacing, spacing, spacing)
			.AddGroup(autoMountLayout)
				.Add(fScanningDisabledCheck)
				.Add(fAutoMountAllBFSCheck)
				.Add(fAutoMountAllCheck)
				.End()
			.AddGroup(bootMountLayout)
				.Add(fInitialDontMountCheck)
				.Add(fInitialMountRestoreCheck)
				.Add(fInitialMountAllBFSCheck)
				.Add(fInitialMountAllCheck)
				.End()
			.Add(fEjectWhenUnmountingCheckBox)
			.End()
		.Add(new BSeparatorView(B_HORIZONTAL/*, B_FANCY_BORDER*/))
		.AddGroup(B_HORIZONTAL, spacing)
			.SetInsets(spacing, spacing, spacing, spacing)
			.Add(fMountAllNow)
			.AddGlue();

	ShowCurrentSettings();
}


/**
 * @brief Destructor.
 */
AutomountSettingsPanel::~AutomountSettingsPanel()
{
}


/**
 * @brief Report that this panel has no factory default state.
 *
 * @return false always; the Defaults button is hidden for this panel.
 */
bool
AutomountSettingsPanel::IsDefaultable() const
{
	return false;
}


/**
 * @brief Restore the panel to the settings captured by RecordRevertSettings().
 */
void
AutomountSettingsPanel::Revert()
{
	_ParseSettings(fInitialSettings);
	_SendSettings(false);
}


/**
 * @brief Query the mount server and update the UI to reflect the current settings.
 */
void
AutomountSettingsPanel::ShowCurrentSettings()
{
	// Apply the settings
	BMessage settings;
	_GetSettings(&settings);
	_ParseSettings(settings);
}


/**
 * @brief Snapshot the current settings so that Revert() can restore them.
 */
void
AutomountSettingsPanel::RecordRevertSettings()
{
	_GetSettings(&fInitialSettings);
}


/**
 * @brief Return whether the current settings differ from the recorded revert snapshot.
 *
 * @return true if settings have changed since RecordRevertSettings() was called.
 */
bool
AutomountSettingsPanel::IsRevertable() const
{
	BMessage currentSettings;
	_GetSettings(&currentSettings);

	return !currentSettings.HasSameData(fInitialSettings);
}


/**
 * @brief Wire all control targets to this panel and point the mount-all button at the server.
 */
void
AutomountSettingsPanel::AttachedToWindow()
{
	fInitialMountAllCheck->SetTarget(this);
	fInitialMountAllBFSCheck->SetTarget(this);
	fInitialMountRestoreCheck->SetTarget(this);
	fInitialDontMountCheck->SetTarget(this);

	fAutoMountAllCheck->SetTarget(this);
	fAutoMountAllBFSCheck->SetTarget(this);
	fScanningDisabledCheck->SetTarget(this);
	fEjectWhenUnmountingCheckBox->SetTarget(this);

	fMountAllNow->SetTarget(fTarget);
}


/**
 * @brief Dispatch incoming messages to update settings or quit the window.
 *
 * @param message  The BMessage to handle.
 */
void
AutomountSettingsPanel::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_QUIT_REQUESTED:
			Window()->Quit();
			break;

		case kAutomountSettingsChanged:
			_SendSettings(true);
			break;

		case kBootMountSettingsChanged:
		case kEjectWhenUnmountingChanged:
			_SendSettings(false);
			break;

		default:
			_inherited::MessageReceived(message);
			break;
	}
}


/**
 * @brief Compose a settings message from the UI state and deliver it to the mount server.
 *
 * @param rescan  If true, requests that the mount server rescans for new volumes.
 */
void
AutomountSettingsPanel::_SendSettings(bool rescan)
{
	BMessage message(kSetAutomounterParams);

	message.AddBool("autoMountAll", (bool)fAutoMountAllCheck->Value());
	message.AddBool("autoMountAllBFS", (bool)fAutoMountAllBFSCheck->Value());
	if (fAutoMountAllBFSCheck->Value())
		message.AddBool("autoMountAllHFS", false);

	message.AddBool("suspended", (bool)fScanningDisabledCheck->Value());
	message.AddBool("rescanNow", rescan);

	message.AddBool("initialMountAll", (bool)fInitialMountAllCheck->Value());
	message.AddBool("initialMountAllBFS",
		(bool)fInitialMountAllBFSCheck->Value());
	message.AddBool("initialMountRestore",
		(bool)fInitialMountRestoreCheck->Value());
	if (fInitialDontMountCheck->Value())
		message.AddBool("initialMountAllHFS", false);

	message.AddBool("ejectWhenUnmounting",
		(bool)fEjectWhenUnmountingCheckBox->Value());

	fTarget.SendMessage(&message);

	// Tell the settings window the contents have changed:
	Window()->PostMessage(kSettingsContentsModified);
}


/**
 * @brief Query the mount server for its current automount parameters.
 *
 * Shows an error alert if the server cannot be contacted within 2.5 seconds.
 *
 * @param reply  BMessage to receive the server's parameter reply.
 */
void
AutomountSettingsPanel::_GetSettings(BMessage* reply) const
{
	BMessage message(kGetAutomounterParams);
	if (fTarget.SendMessage(&message, reply, 2500000) != B_OK) {
		BAlert* alert = new BAlert(B_TRANSLATE("Mount server error"),
			B_TRANSLATE("The mount server could not be contacted."),
			B_TRANSLATE("OK"),
			NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
	}
}


/**
 * @brief Apply the values in @a settings to the radio buttons and checkboxes.
 *
 * Falls back to safe defaults when individual keys are absent from the message.
 *
 * @param settings  BMessage containing the mount-server parameter set.
 */
void
AutomountSettingsPanel::_ParseSettings(const BMessage& settings)
{
	bool result;
	if (settings.FindBool("autoMountAll", &result) == B_OK && result)
		fAutoMountAllCheck->SetValue(B_CONTROL_ON);
	else if (settings.FindBool("autoMountAllBFS", &result) == B_OK && result)
		fAutoMountAllBFSCheck->SetValue(B_CONTROL_ON);
	else
		fScanningDisabledCheck->SetValue(B_CONTROL_ON);

	if (settings.FindBool("suspended", &result) == B_OK && result)
		fScanningDisabledCheck->SetValue(B_CONTROL_ON);

	if (settings.FindBool("initialMountAll", &result) == B_OK && result)
		fInitialMountAllCheck->SetValue(B_CONTROL_ON);
	else if (settings.FindBool("initialMountRestore", &result) == B_OK
		&& result) {
		fInitialMountRestoreCheck->SetValue(B_CONTROL_ON);
	} else if (settings.FindBool("initialMountAllBFS", &result) == B_OK
		&& result) {
		fInitialMountAllBFSCheck->SetValue(B_CONTROL_ON);
	} else
		fInitialDontMountCheck->SetValue(B_CONTROL_ON);

	if (settings.FindBool("ejectWhenUnmounting", &result) == B_OK && result)
		fEjectWhenUnmountingCheckBox->SetValue(B_CONTROL_ON);
}

