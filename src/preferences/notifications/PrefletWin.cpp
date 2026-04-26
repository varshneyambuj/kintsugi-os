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
 *   Copyright 2010-2017, Haiku, Inc. All Rights Reserved.
 *   Copyright 2009, Pier Luigi Fiorini.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Pier Luigi Fiorini, pierluigi.fiorini@gmail.com
 *       Brian Hill, supernova@tycho.email
 */


/**
 * @file PrefletWin.cpp
 * @brief Implementation of PrefletWin, the Notifications preflet's main
 *        window.
 *
 * Builds the tab view and the global Defaults/Revert button strip, drives
 * the Apply / Revert / Defaults flow across every tab, and persists the
 * combined settings to a flattened BMessage in the user settings
 * directory. As the SettingsHost it routes pane-level edits back through
 * Apply (with optional sample notification).
 */


#include "PrefletWin.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <FindDirectory.h>
#include <Notification.h>
#include <Path.h>
#include <SeparatorView.h>

#include <notification/Notifications.h>

#include "NotificationsConstants.h"
#include "PrefletView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PrefletWin"

/** @brief Stable message identifier used for the Apply-with-example
           preview notification. */
const BString kSampleMessageID("NotificationsSample");


/**
 * @brief Constructs the window, builds the layout, loads persisted
 *        settings, and shows itself centered on screen.
 */
PrefletWin::PrefletWin()
	:
	BWindow(BRect(0, 0, 160 + 20 * be_plain_font->Size(), 300),
		B_TRANSLATE_SYSTEM_NAME("Notifications"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS
		| B_AUTO_UPDATE_SIZE_LIMITS)
{
	// Preflet container view
	fMainView = new PrefletView(this);
	fMainView->SetBorder(B_NO_BORDER);
	fMainView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));

	// Defaults button
	fDefaults = new BButton("defaults", B_TRANSLATE("Defaults"),
		new BMessage(kDefaults));
	fDefaults->SetEnabled(false);

	// Revert button
	fRevert = new BButton("revert", B_TRANSLATE("Revert"),
		new BMessage(kRevert));
	fRevert->SetEnabled(false);

	// Build the layout
	fButtonsView = new BGroupView();
	BLayoutBuilder::Group<>(fButtonsView, B_VERTICAL, 0)
		.AddGroup(B_HORIZONTAL)
			.Add(fDefaults)
			.Add(fRevert)
			.AddGlue()
			.SetInsets(B_USE_WINDOW_SPACING, 0, B_USE_WINDOW_SPACING,
				B_USE_WINDOW_SPACING)
		.End();
	fButtonsLayout = fButtonsView->GroupLayout();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0, B_USE_DEFAULT_SPACING, 0, 0)
		.Add(fMainView)
		.Add(fButtonsView)
	.End();
	fMainView->SetExplicitMinSize(BSize(Frame().Width(), B_SIZE_UNSET));

	ReloadSettings();

	// Center this window on screen and show it
	CenterOnScreen();
	Show();
}


/**
 * @brief Window message dispatcher.
 *
 * Handles Apply / Apply-with-example (which iterates panes, writes the
 * combined settings file, and updates button enable state), Defaults and
 * Revert (which delegate to the panes), and the kShowButtons toggle from
 * PrefletView.
 *
 * @param msg  Incoming BMessage.
 */
void
PrefletWin::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kApply:
		case kApplyWithExample:
		{
			BPath path;

			status_t ret = B_OK;
			ret = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
			if (ret != B_OK)
				return;

			path.Append(kSettingsFile);

			BMessage settingsStore;
			int32 count = fMainView->CountTabs();
			for (int32 i = 0; i < count; i++) {
				SettingsPane* pane =
					dynamic_cast<SettingsPane*>(fMainView->PageAt(i));
				if (pane) {
					if (pane->Save(settingsStore) == B_OK) {
						fDefaults->SetEnabled(_DefaultsPossible());
						fRevert->SetEnabled(_RevertPossible());
					} else
						break;
				}
			}

			// Save settings file
			BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
			ret = settingsStore.Flatten(&file);
			if (ret != B_OK) {
				BAlert* alert = new BAlert("",
					B_TRANSLATE("An error occurred saving the preferences.\n"
						"It's possible you are running out of disk space."),
					B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
					B_STOP_ALERT);
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				(void)alert->Go();
			}
			file.Unset();

			if (msg->what == kApplyWithExample)
				_SendExampleNotification();

			break;
		}
		case kDefaults:
			fDefaults->SetEnabled(false);
			_Defaults();
			PostMessage(kApply);
			break;
		case kRevert:
			fRevert->SetEnabled(false);
			_Revert();
			PostMessage(kApply);
			break;
		case kShowButtons: {
			bool show = msg->GetBool(kShowButtonsKey, true);
			fButtonsLayout->SetVisible(show);
			break;
		}
		default:
			BWindow::MessageReceived(msg);
	}
}


/**
 * @brief Forwards quit to the application so the process terminates when
 *        the window closes.
 *
 * @return Always true.
 */
bool
PrefletWin::QuitRequested()
{
	be_app_messenger.SendMessage(B_QUIT_REQUESTED);
	return true;
}


/**
 * @brief SettingsHost callback. Posts an Apply (optionally with sample
 *        preview) when a pane reports an edit.
 *
 * @param showExample  When true also display a sample notification.
 */
void
PrefletWin::SettingChanged(bool showExample)
{
	if (showExample)
		PostMessage(kApplyWithExample);
	else
		PostMessage(kApply);
}


/**
 * @brief Reloads the settings file from disk and pushes the values into
 *        each pane's Load().
 *
 * Also recomputes whether the Defaults button should be enabled.
 *
 * @todo Avoid loading the file once per tab; share the parsed BMessage.
 */
void
PrefletWin::ReloadSettings()
{
	BPath path;

	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;

	// FIXME don't load this again here, share with other tabs!
	path.Append(kSettingsFile);

	BMessage settings;
	BFile file(path.Path(), B_READ_ONLY);
	settings.Unflatten(&file);

	int32 count = fMainView->CountTabs();
	for (int32 i = 0; i < count; i++) {
		SettingsPane* pane =
			dynamic_cast<SettingsPane*>(fMainView->PageAt(i));
		if (pane)
			pane->Load(settings);
	}
	fDefaults->SetEnabled(_DefaultsPossible());
}


/**
 * @brief Calls Revert() on every settings pane.
 *
 * @return Always B_OK.
 */
status_t
PrefletWin::_Revert()
{
	int32 count = fMainView->CountTabs();
	for (int32 i = 0; i < count; i++) {
		SettingsPane* pane =
			dynamic_cast<SettingsPane*>(fMainView->PageAt(i));
		if (pane)
			pane->Revert();
	}
	return B_OK;
}


/**
 * @brief Reports whether any pane has an edit that Revert() could undo.
 *
 * @return true when at least one pane reports RevertPossible().
 */
bool
PrefletWin::_RevertPossible()
{
	int32 count = fMainView->CountTabs();
	for (int32 i = 0; i < count; i++) {
		SettingsPane* pane =
			dynamic_cast<SettingsPane*>(fMainView->PageAt(i));
		if (pane && pane->RevertPossible())
			return true;
	}
	return false;
}


/**
 * @brief Calls Defaults() on every settings pane.
 *
 * @return Always B_OK.
 */
status_t
PrefletWin::_Defaults()
{
	int32 count = fMainView->CountTabs();
	for (int32 i = 0; i < count; i++) {
		SettingsPane* pane =
			dynamic_cast<SettingsPane*>(fMainView->PageAt(i));
		if (pane)
			pane->Defaults();
	}
	return B_OK;
}


/**
 * @brief Reports whether any pane is currently not at its default values.
 *
 * @return true when at least one pane reports DefaultsPossible().
 */
bool
PrefletWin::_DefaultsPossible()
{
	int32 count = fMainView->CountTabs();
	for (int32 i = 0; i < count; i++) {
		SettingsPane* pane =
			dynamic_cast<SettingsPane*>(fMainView->PageAt(i));
		if (pane && pane->DefaultsPossible())
			return true;
	}
	return false;
}


/**
 * @brief Posts a sample BNotification so the user can preview the live
 *        appearance of their settings.
 */
void
PrefletWin::_SendExampleNotification()
{
	BString title(B_TRANSLATE("%prefname% preflet sample"));
	title.ReplaceFirst("%prefname%", B_TRANSLATE_SYSTEM_NAME("Notifications"));

	BNotification notification(B_INFORMATION_NOTIFICATION);
	notification.SetMessageID(kSampleMessageID);
	notification.SetGroup(B_TRANSLATE_SYSTEM_NAME("Notifications"));
	notification.SetTitle(title);
	notification.SetContent(B_TRANSLATE("This is a test notification message"));
	notification.Send();
}
