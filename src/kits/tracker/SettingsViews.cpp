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
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the Be Sample Code License.
 */

/**
 * @file SettingsViews.cpp
 * @brief Tracker preferences panel views for desktop, window, space bar, and automount settings.
 *
 * @see BGroupView, TrackerSettings, SettingsView
 */


#include "SettingsViews.h"

#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ColorControl.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <NodeMonitor.h>
#include <Point.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <StringView.h>

#include "Commands.h"
#include "DeskWindow.h"
#include "Model.h"
#include "Tracker.h"
#include "TrackerDefaults.h"
#include "WidgetAttributeText.h"


static const uint32 kSpaceBarSwitchColor = 'SBsc';


/**
 * @brief Broadcast a boolean settings-change notification to the Tracker application.
 *
 * @param what   Message constant identifying the changed setting.
 * @param name   Field name for the boolean value in the notification.
 * @param value  The new boolean value.
 */
static void
send_bool_notices(uint32 what, const char* name, bool value)
{
	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	if (tracker == NULL)
		return;

	BMessage message;
	message.AddBool(name, value);
	tracker->SendNotices(what, &message);
}


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SettingsView"


//	#pragma mark - SettingsView


/**
 * @brief Construct a SettingsView with the given name.
 *
 * @param name  View name passed to BGroupView.
 */
SettingsView::SettingsView(const char* name)
	:
	BGroupView(name)
{
}


/**
 * @brief Destructor.
 */
SettingsView::~SettingsView()
{
}


/**
 * @brief Restore this settings view to its default values.
 *
 * Subclasses should apply default values to TrackerSettings and then
 * call ShowCurrentSettings() to refresh the UI widgets.
 */
void
SettingsView::SetDefaults()
{
}


/**
 * @brief Return whether the Defaults button should be enabled for this view.
 *
 * @return true if the current values differ from the defaults.
 */
bool
SettingsView::IsDefaultable() const
{
	return true;
}


/**
 * @brief Restore the settings that were active when the window was opened.
 *
 * Subclasses should reapply the saved revert values to TrackerSettings and
 * call ShowCurrentSettings() to refresh the UI.
 */
void
SettingsView::Revert()
{
}


/**
 * @brief Snapshot the current settings so they can be restored by Revert().
 *
 * Called by TrackerSettingsWindow::Show() before the window becomes visible.
 */
void
SettingsView::RecordRevertSettings()
{
}


/**
 * @brief Refresh all UI widgets to match the current TrackerSettings values.
 */
void
SettingsView::ShowCurrentSettings()
{
}


/**
 * @brief Return whether the Revert button should be enabled for this view.
 *
 * @return true if the current values differ from the saved revert state.
 */
bool
SettingsView::IsRevertable() const
{
	return true;
}


// #pragma mark - DesktopSettingsView


/**
 * @brief Construct the Desktop settings view with volume-placement radio buttons.
 */
DesktopSettingsView::DesktopSettingsView()
	:
	SettingsView("DesktopSettingsView"),
	fShowDisksIconRadioButton(NULL),
	fMountVolumesOntoDesktopRadioButton(NULL),
	fMountSharedVolumesOntoDesktopCheckBox(NULL),
	fShowDisksIcon(kDefaultShowDisksIcon),
	fMountVolumesOntoDesktop(kDefaultMountVolumesOntoDesktop),
	fMountSharedVolumesOntoDesktop(kDefaultMountSharedVolumesOntoDesktop),
	fIntegrateNonBootBeOSDesktops(false),
	fEjectWhenUnmounting(kDefaultEjectWhenUnmounting)
{
	fShowDisksIconRadioButton = new BRadioButton("",
		B_TRANSLATE("Show Disks icon"),
		new BMessage(kShowDisksIconChanged));

	fMountVolumesOntoDesktopRadioButton = new BRadioButton("",
		B_TRANSLATE("Show volumes on Desktop"),
		new BMessage(kVolumesOnDesktopChanged));

	fMountSharedVolumesOntoDesktopCheckBox = new BCheckBox("",
		B_TRANSLATE("Show shared volumes on Desktop"),
		new BMessage(kVolumesOnDesktopChanged));

	const float spacing = be_control_look->DefaultItemSpacing();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fShowDisksIconRadioButton)
		.Add(fMountVolumesOntoDesktopRadioButton)
		.AddGroup(B_VERTICAL, 0)
			.Add(fMountSharedVolumesOntoDesktopCheckBox)
			.SetInsets(spacing * 2, 0, 0, 0)
			.End()
		.AddGlue()
		.SetInsets(spacing);
}


/**
 * @brief Set the message targets for radio buttons when attached to a window.
 */
void
DesktopSettingsView::AttachedToWindow()
{
	fShowDisksIconRadioButton->SetTarget(this);
	fMountVolumesOntoDesktopRadioButton->SetTarget(this);
	fMountSharedVolumesOntoDesktopCheckBox->SetTarget(this);
}


/**
 * @brief Handle preference-change messages from the radio buttons and checkboxes.
 *
 * @param message  The incoming control-change message.
 */
void
DesktopSettingsView::MessageReceived(BMessage* message)
{
	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	if (tracker == NULL)
		return;

	TrackerSettings settings;

	switch (message->what) {
		case kShowDisksIconChanged:
		{
			// Turn on and off related settings:
			fMountVolumesOntoDesktopRadioButton->SetValue(
				!(fShowDisksIconRadioButton->Value() == 1));
			fMountSharedVolumesOntoDesktopCheckBox->SetEnabled(
				fMountVolumesOntoDesktopRadioButton->Value() == 1);

			// Set the new settings in the tracker:
			settings.SetShowDisksIcon(fShowDisksIconRadioButton->Value() == 1);
			settings.SetMountVolumesOntoDesktop(
				fMountVolumesOntoDesktopRadioButton->Value() == 1);
			settings.SetMountSharedVolumesOntoDesktop(
				fMountSharedVolumesOntoDesktopCheckBox->Value() == 1);

			// Construct the notification message:
			BMessage notificationMessage;
			notificationMessage.AddBool("ShowDisksIcon",
				fShowDisksIconRadioButton->Value() == 1);
			notificationMessage.AddBool("MountVolumesOntoDesktop",
				fMountVolumesOntoDesktopRadioButton->Value() == 1);
			notificationMessage.AddBool("MountSharedVolumesOntoDesktop",
				fMountSharedVolumesOntoDesktopCheckBox->Value() == 1);

			// Send the notification message:
			tracker->SendNotices(kVolumesOnDesktopChanged,
				&notificationMessage);

			// Tell the settings window the contents have changed:
			Window()->PostMessage(kSettingsContentsModified);
			break;
		}

		case kVolumesOnDesktopChanged:
		{
			// Turn on and off related settings:
			fShowDisksIconRadioButton->SetValue(
				!(fMountVolumesOntoDesktopRadioButton->Value() == 1));
			fMountSharedVolumesOntoDesktopCheckBox->SetEnabled(
				fMountVolumesOntoDesktopRadioButton->Value() == 1);

			// Set the new settings in the tracker:
			settings.SetShowDisksIcon(fShowDisksIconRadioButton->Value() == 1);
			settings.SetMountVolumesOntoDesktop(
				fMountVolumesOntoDesktopRadioButton->Value() == 1);
			settings.SetMountSharedVolumesOntoDesktop(
				fMountSharedVolumesOntoDesktopCheckBox->Value() == 1);

			// Construct the notification message:
			BMessage notificationMessage;
			notificationMessage.AddBool("ShowDisksIcon",
				fShowDisksIconRadioButton->Value() == 1);
			notificationMessage.AddBool("MountVolumesOntoDesktop",
				fMountVolumesOntoDesktopRadioButton->Value() == 1);
			notificationMessage.AddBool("MountSharedVolumesOntoDesktop",
				fMountSharedVolumesOntoDesktopCheckBox->Value() == 1);

			// Send the notification message:
			tracker->SendNotices(kVolumesOnDesktopChanged, &notificationMessage);

			// Tell the settings window the contents have changed:
			Window()->PostMessage(kSettingsContentsModified);
			break;
		}

		default:
			_inherited::MessageReceived(message);
			break;
	}
}


/**
 * @brief Restore Desktop preferences to their factory defaults.
 */
void
DesktopSettingsView::SetDefaults()
{
	// ToDo: Avoid the duplication of the default values.
	TrackerSettings settings;

	settings.SetShowDisksIcon(kDefaultShowDisksIcon);
	settings.SetMountVolumesOntoDesktop(kDefaultMountVolumesOntoDesktop);
	settings.SetMountSharedVolumesOntoDesktop(kDefaultMountSharedVolumesOntoDesktop);
	settings.SetEjectWhenUnmounting(kDefaultEjectWhenUnmounting);

	ShowCurrentSettings();
	_SendNotices();
}


/**
 * @brief Return true if any Desktop preference differs from its factory default.
 *
 * @return true if the Defaults button should be enabled.
 */
bool
DesktopSettingsView::IsDefaultable() const
{
	TrackerSettings settings;

	return settings.ShowDisksIcon() != kDefaultShowDisksIcon
		|| settings.MountVolumesOntoDesktop() != kDefaultMountVolumesOntoDesktop
		|| settings.MountSharedVolumesOntoDesktop() != kDefaultMountSharedVolumesOntoDesktop
		|| settings.EjectWhenUnmounting() != kDefaultEjectWhenUnmounting;
}


/**
 * @brief Restore Desktop preferences to the values recorded when the window opened.
 */
void
DesktopSettingsView::Revert()
{
	TrackerSettings settings;

	settings.SetShowDisksIcon(fShowDisksIcon);
	settings.SetMountVolumesOntoDesktop(fMountVolumesOntoDesktop);
	settings.SetMountSharedVolumesOntoDesktop(fMountSharedVolumesOntoDesktop);
	settings.SetEjectWhenUnmounting(fEjectWhenUnmounting);

	ShowCurrentSettings();
	_SendNotices();
}


void
DesktopSettingsView::_SendNotices()
{
	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	if (tracker == NULL)
		return;

	// Construct the notification message:
	BMessage notificationMessage;
	notificationMessage.AddBool("ShowDisksIcon",
		fShowDisksIconRadioButton->Value() == 1);
	notificationMessage.AddBool("MountVolumesOntoDesktop",
		fMountVolumesOntoDesktopRadioButton->Value() == 1);
	notificationMessage.AddBool("MountSharedVolumesOntoDesktop",
		fMountSharedVolumesOntoDesktopCheckBox->Value() == 1);

	// Send notices to the tracker about the change:
	tracker->SendNotices(kVolumesOnDesktopChanged, &notificationMessage);
	tracker->SendNotices(kDesktopIntegrationChanged, &notificationMessage);
}


/**
 * @brief Refresh the Desktop settings view widgets from the current TrackerSettings.
 */
void
DesktopSettingsView::ShowCurrentSettings()
{
	TrackerSettings settings;

	fShowDisksIconRadioButton->SetValue(settings.ShowDisksIcon());
	fMountVolumesOntoDesktopRadioButton->SetValue(
		settings.MountVolumesOntoDesktop());

	fMountSharedVolumesOntoDesktopCheckBox->SetValue(
		settings.MountSharedVolumesOntoDesktop());
	fMountSharedVolumesOntoDesktopCheckBox->SetEnabled(
		settings.MountVolumesOntoDesktop());
}


/**
 * @brief Snapshot the current Desktop settings for later use by Revert().
 */
void
DesktopSettingsView::RecordRevertSettings()
{
	TrackerSettings settings;

	fShowDisksIcon = settings.ShowDisksIcon();
	fMountVolumesOntoDesktop = settings.MountVolumesOntoDesktop();
	fMountSharedVolumesOntoDesktop = settings.MountSharedVolumesOntoDesktop();
	fEjectWhenUnmounting = settings.EjectWhenUnmounting();
}


/**
 * @brief Return true if any Desktop preference differs from the saved revert state.
 *
 * @return true if the Revert button should be enabled.
 */
bool
DesktopSettingsView::IsRevertable() const
{
	return fShowDisksIcon != (fShowDisksIconRadioButton->Value() > 0)
		|| fMountVolumesOntoDesktop !=
			(fMountVolumesOntoDesktopRadioButton->Value() > 0)
		|| fMountSharedVolumesOntoDesktop !=
			(fMountSharedVolumesOntoDesktopCheckBox->Value() > 0);
}


// #pragma mark - WindowsSettingsView


/**
 * @brief Construct the Windows settings view with all window-behaviour checkboxes.
 */
WindowsSettingsView::WindowsSettingsView()
	:
	SettingsView("WindowsSettingsView"),
	fShowFullPathInTitleBarCheckBox(NULL),
	fSingleWindowBrowseCheckBox(NULL),
	fShowNavigatorCheckBox(NULL),
	fOutlineSelectionCheckBox(NULL),
	fSortFolderNamesFirstCheckBox(NULL),
	fHideDotFilesCheckBox(NULL),
	fTypeAheadFilteringCheckBox(NULL),
	fGenerateImageThumbnailsCheckBox(NULL),
	fShowFullPathInTitleBar(kDefaultShowFullPathInTitleBar),
	fSingleWindowBrowse(kDefaultSingleWindowBrowse),
	fShowNavigator(kDefaultShowNavigator),
	fTransparentSelection(kDefaultTransparentSelection),
	fSortFolderNamesFirst(kDefaultSortFolderNamesFirst),
	fHideDotFiles(kDefaultHideDotFiles),
	fTypeAheadFiltering(kDefaultTypeAheadFiltering),
	fGenerateImageThumbnails(kDefaultGenerateImageThumbnails)
{
	fShowFullPathInTitleBarCheckBox = new BCheckBox("",
		B_TRANSLATE("Show folder location in title tab"),
		new BMessage(kWindowsShowFullPathChanged));

	fSingleWindowBrowseCheckBox = new BCheckBox("",
		B_TRANSLATE("Single window navigation"),
		new BMessage(kSingleWindowBrowseChanged));

	fShowNavigatorCheckBox = new BCheckBox("",
		B_TRANSLATE("Show navigator"),
		new BMessage(kShowNavigatorChanged));

	fOutlineSelectionCheckBox = new BCheckBox("",
		B_TRANSLATE("Outline selection rectangle only"),
		new BMessage(kTransparentSelectionChanged));

	fSortFolderNamesFirstCheckBox = new BCheckBox("",
		B_TRANSLATE("List folders first"),
		new BMessage(kSortFolderNamesFirstChanged));

	fHideDotFilesCheckBox = new BCheckBox("",
		B_TRANSLATE("Hide dotfiles"),
		new BMessage(kHideDotFilesChanged));

	fTypeAheadFilteringCheckBox = new BCheckBox("",
		B_TRANSLATE("Enable type-ahead filtering"),
		new BMessage(kTypeAheadFilteringChanged));

	fGenerateImageThumbnailsCheckBox = new BCheckBox("",
		B_TRANSLATE("Generate image thumbnails"),
		new BMessage(kGenerateImageThumbnailsChanged));

	const float spacing = be_control_look->DefaultItemSpacing();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.AddGroup(B_VERTICAL, 0)
			.Add(fShowFullPathInTitleBarCheckBox)
			.Add(fSingleWindowBrowseCheckBox)
			.End()
		.AddGroup(B_VERTICAL)
			.Add(fShowNavigatorCheckBox)
			.SetInsets(spacing * 2, 0, 0, 0)
			.End()
		.AddGroup(B_VERTICAL, 0)
			.Add(fOutlineSelectionCheckBox)
			.Add(fSortFolderNamesFirstCheckBox)
			.Add(fHideDotFilesCheckBox)
			.Add(fTypeAheadFilteringCheckBox)
			.Add(fGenerateImageThumbnailsCheckBox)
			.End()
		.AddGlue()
		.SetInsets(spacing);
}


/**
 * @brief Set message targets for all checkbox controls when attached to a window.
 */
void
WindowsSettingsView::AttachedToWindow()
{
	fSingleWindowBrowseCheckBox->SetTarget(this);
	fShowNavigatorCheckBox->SetTarget(this);
	fShowFullPathInTitleBarCheckBox->SetTarget(this);
	fOutlineSelectionCheckBox->SetTarget(this);
	fSortFolderNamesFirstCheckBox->SetTarget(this);
	fHideDotFilesCheckBox->SetTarget(this);
	fTypeAheadFilteringCheckBox->SetTarget(this);
	fGenerateImageThumbnailsCheckBox->SetTarget(this);
}


/**
 * @brief Handle window-preference changes from the checkboxes and sliders.
 *
 * @param message  The incoming control-change message.
 */
void
WindowsSettingsView::MessageReceived(BMessage* message)
{
	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	if (tracker == NULL)
		return;

	TrackerSettings settings;

	switch (message->what) {
		case kWindowsShowFullPathChanged:
			settings.SetShowFullPathInTitleBar(
				fShowFullPathInTitleBarCheckBox->Value() == 1);
			tracker->SendNotices(kWindowsShowFullPathChanged);
			Window()->PostMessage(kSettingsContentsModified);
			break;

		case kSingleWindowBrowseChanged:
			settings.SetSingleWindowBrowse(
				fSingleWindowBrowseCheckBox->Value() == 1);
			if (fSingleWindowBrowseCheckBox->Value() == 0) {
				fShowNavigatorCheckBox->SetEnabled(false);
				settings.SetShowNavigator(0);
			} else {
				fShowNavigatorCheckBox->SetEnabled(true);
				settings.SetShowNavigator(
					fShowNavigatorCheckBox->Value() != 0);
			}
			tracker->SendNotices(kShowNavigatorChanged);
			tracker->SendNotices(kSingleWindowBrowseChanged);
			Window()->PostMessage(kSettingsContentsModified);
			break;

		case kShowNavigatorChanged:
			settings.SetShowNavigator(fShowNavigatorCheckBox->Value() == 1);
			tracker->SendNotices(kShowNavigatorChanged);
			Window()->PostMessage(kSettingsContentsModified);
			break;

		case kTransparentSelectionChanged:
		{
			settings.SetTransparentSelection(
				fOutlineSelectionCheckBox->Value() == B_CONTROL_OFF);

			// Make the notification message and send it to the tracker:
			send_bool_notices(kTransparentSelectionChanged,
				"TransparentSelection", settings.TransparentSelection());

			Window()->PostMessage(kSettingsContentsModified);
			break;
		}

		case kSortFolderNamesFirstChanged:
		{
			settings.SetSortFolderNamesFirst(
				fSortFolderNamesFirstCheckBox->Value() == 1);

			// Make the notification message and send it to the tracker:
			send_bool_notices(kSortFolderNamesFirstChanged,
				"SortFolderNamesFirst",
				fSortFolderNamesFirstCheckBox->Value() == 1);

			Window()->PostMessage(kSettingsContentsModified);
			break;
		}

		case kHideDotFilesChanged:
		{
			settings.SetHideDotFiles(
				fHideDotFilesCheckBox->Value() == 1);
			send_bool_notices(kHideDotFilesChanged,
				"HideDotFiles",
				fHideDotFilesCheckBox->Value() == 1);
			Window()->PostMessage(kSettingsContentsModified);
			break;
		}

		case kTypeAheadFilteringChanged:
		{
			settings.SetTypeAheadFiltering(
				fTypeAheadFilteringCheckBox->Value() == 1);
			send_bool_notices(kTypeAheadFilteringChanged,
				"TypeAheadFiltering",
				fTypeAheadFilteringCheckBox->Value() == 1);
			Window()->PostMessage(kSettingsContentsModified);
			break;
		}

		case kGenerateImageThumbnailsChanged:
		{
			settings.SetGenerateImageThumbnails(
				fGenerateImageThumbnailsCheckBox->Value() == 1);
			send_bool_notices(kGenerateImageThumbnailsChanged,
				"GenerateImageThumbnails",
				fGenerateImageThumbnailsCheckBox->Value() == 1);
			Window()->PostMessage(kSettingsContentsModified);
			break;
		}

		default:
			_inherited::MessageReceived(message);
			break;
	}
}


/**
 * @brief Restore Windows preferences to their factory defaults.
 */
void
WindowsSettingsView::SetDefaults()
{
	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	if (tracker == NULL)
		return;

	TrackerSettings settings;

	if (settings.ShowFullPathInTitleBar() != kDefaultShowFullPathInTitleBar) {
		settings.SetShowFullPathInTitleBar(kDefaultShowFullPathInTitleBar);
		tracker->SendNotices(kWindowsShowFullPathChanged);
	}

	if (settings.SingleWindowBrowse() != kDefaultSingleWindowBrowse) {
		settings.SetSingleWindowBrowse(kDefaultSingleWindowBrowse);
		tracker->SendNotices(kSingleWindowBrowseChanged);
	}

	if (settings.ShowNavigator() != kDefaultShowNavigator) {
		settings.SetShowNavigator(kDefaultShowNavigator);
		tracker->SendNotices(kShowNavigatorChanged);
	}

	if (settings.TransparentSelection() != kDefaultTransparentSelection) {
		settings.SetTransparentSelection(kDefaultTransparentSelection);
		send_bool_notices(kTransparentSelectionChanged,
			"TransparentSelection", kDefaultTransparentSelection);
	}

	if (settings.SortFolderNamesFirst() != kDefaultSortFolderNamesFirst) {
		settings.SetSortFolderNamesFirst(kDefaultSortFolderNamesFirst);
		send_bool_notices(kSortFolderNamesFirstChanged,
			"SortFolderNamesFirst", kDefaultSortFolderNamesFirst);
	}

	if (settings.HideDotFiles() != kDefaultHideDotFiles) {
		settings.SetHideDotFiles(kDefaultHideDotFiles);
		send_bool_notices(kHideDotFilesChanged,
			"HideDotFiles", kDefaultHideDotFiles);
	}

	if (settings.TypeAheadFiltering() != kDefaultTypeAheadFiltering) {
		settings.SetTypeAheadFiltering(kDefaultTypeAheadFiltering);
		send_bool_notices(kTypeAheadFilteringChanged,
			"TypeAheadFiltering", kDefaultTypeAheadFiltering);
	}

	if (settings.GenerateImageThumbnails() != kDefaultGenerateImageThumbnails) {
		settings.SetGenerateImageThumbnails(kDefaultGenerateImageThumbnails);
		send_bool_notices(kGenerateImageThumbnailsChanged,
			"GenerateImageThumbnails", kDefaultGenerateImageThumbnails);
	}

	ShowCurrentSettings();
}


/**
 * @brief Return true if any Windows preference differs from its factory default.
 *
 * @return true if the Defaults button should be enabled.
 */
bool
WindowsSettingsView::IsDefaultable() const
{
	TrackerSettings settings;

	return settings.ShowFullPathInTitleBar() != kDefaultShowFullPathInTitleBar
		|| settings.SingleWindowBrowse() != kDefaultSingleWindowBrowse
		|| settings.ShowNavigator() != kDefaultShowNavigator
		|| settings.TransparentSelection() != kDefaultTransparentSelection
		|| settings.SortFolderNamesFirst() != kDefaultSortFolderNamesFirst
		|| settings.HideDotFiles() != kDefaultHideDotFiles
		|| settings.TypeAheadFiltering() != kDefaultTypeAheadFiltering
		|| settings.GenerateImageThumbnails() != kDefaultGenerateImageThumbnails;
}


/**
 * @brief Restore Windows preferences to the values recorded when the window opened.
 */
void
WindowsSettingsView::Revert()
{
	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	if (tracker == NULL)
		return;

	TrackerSettings settings;

	if (settings.ShowFullPathInTitleBar() != fShowFullPathInTitleBar) {
		settings.SetShowFullPathInTitleBar(fShowFullPathInTitleBar);
		tracker->SendNotices(kWindowsShowFullPathChanged);
	}

	if (settings.SingleWindowBrowse() != fSingleWindowBrowse) {
		settings.SetSingleWindowBrowse(fSingleWindowBrowse);
		tracker->SendNotices(kSingleWindowBrowseChanged);
	}

	if (settings.ShowNavigator() != fShowNavigator) {
		settings.SetShowNavigator(fShowNavigator);
		tracker->SendNotices(kShowNavigatorChanged);
	}

	if (settings.TransparentSelection() != fTransparentSelection) {
		settings.SetTransparentSelection(fTransparentSelection);
		send_bool_notices(kTransparentSelectionChanged,
			"TransparentSelection", fTransparentSelection);
	}

	if (settings.SortFolderNamesFirst() != fSortFolderNamesFirst) {
		settings.SetSortFolderNamesFirst(fSortFolderNamesFirst);
		send_bool_notices(kSortFolderNamesFirstChanged,
			"SortFolderNamesFirst", fSortFolderNamesFirst);
	}

	if (settings.HideDotFiles() != fHideDotFiles) {
		settings.SetSortFolderNamesFirst(fHideDotFiles);
		send_bool_notices(kHideDotFilesChanged,
			"HideDotFiles", fHideDotFiles);
	}

	if (settings.TypeAheadFiltering() != fTypeAheadFiltering) {
		settings.SetTypeAheadFiltering(fTypeAheadFiltering);
		send_bool_notices(kTypeAheadFilteringChanged,
			"TypeAheadFiltering", fTypeAheadFiltering);
	}

	if (settings.GenerateImageThumbnails() != fGenerateImageThumbnails) {
		settings.SetGenerateImageThumbnails(fGenerateImageThumbnails);
		send_bool_notices(kGenerateImageThumbnailsChanged,
			"GenerateImageThumbnails", fGenerateImageThumbnails);
	}

	ShowCurrentSettings();
}


/**
 * @brief Refresh the Windows settings view widgets from the current TrackerSettings.
 */
void
WindowsSettingsView::ShowCurrentSettings()
{
	TrackerSettings settings;

	fShowFullPathInTitleBarCheckBox->SetValue(
		settings.ShowFullPathInTitleBar());
	fSingleWindowBrowseCheckBox->SetValue(settings.SingleWindowBrowse());
	fShowNavigatorCheckBox->SetEnabled(settings.SingleWindowBrowse());
	fShowNavigatorCheckBox->SetValue(settings.ShowNavigator());
	fOutlineSelectionCheckBox->SetValue(settings.TransparentSelection()
		? B_CONTROL_OFF : B_CONTROL_ON);
	fSortFolderNamesFirstCheckBox->SetValue(settings.SortFolderNamesFirst());
	fHideDotFilesCheckBox->SetValue(settings.HideDotFiles());
	fTypeAheadFilteringCheckBox->SetValue(settings.TypeAheadFiltering());
	fGenerateImageThumbnailsCheckBox->SetValue(
		settings.GenerateImageThumbnails());
}


/**
 * @brief Snapshot the current Windows preferences for later use by Revert().
 */
void
WindowsSettingsView::RecordRevertSettings()
{
	TrackerSettings settings;

	fShowFullPathInTitleBar = settings.ShowFullPathInTitleBar();
	fSingleWindowBrowse = settings.SingleWindowBrowse();
	fShowNavigator = settings.ShowNavigator();
	fTransparentSelection = settings.TransparentSelection();
	fSortFolderNamesFirst = settings.SortFolderNamesFirst();
	fHideDotFiles = settings.HideDotFiles();
	fTypeAheadFiltering = settings.TypeAheadFiltering();
	fGenerateImageThumbnails = settings.GenerateImageThumbnails();
}


/**
 * @brief Return true if any Windows preference differs from the saved revert state.
 *
 * @return true if the Revert button should be enabled.
 */
bool
WindowsSettingsView::IsRevertable() const
{
	TrackerSettings settings;

	return fShowFullPathInTitleBar != settings.ShowFullPathInTitleBar()
		|| fSingleWindowBrowse != settings.SingleWindowBrowse()
		|| fShowNavigator != settings.ShowNavigator()
		|| fTransparentSelection != settings.TransparentSelection()
		|| fSortFolderNamesFirst != settings.SortFolderNamesFirst()
		|| fHideDotFiles != settings.HideDotFiles()
		|| fTypeAheadFiltering != settings.TypeAheadFiltering()
		|| fGenerateImageThumbnails != settings.GenerateImageThumbnails();
}


// #pragma mark - SpaceBarSettingsView


/**
 * @brief Construct the Volume icons settings view with space-bar colour controls.
 */
SpaceBarSettingsView::SpaceBarSettingsView()
	:
	SettingsView("SpaceBarSettingsView")
{
	fSpaceBarShowCheckBox = new BCheckBox("",
		B_TRANSLATE("Show space bars on volumes"),
		new BMessage(kUpdateVolumeSpaceBar));

	BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING);
	menu->SetFont(be_plain_font);

	BMenuItem* item;
	menu->AddItem(item = new BMenuItem(
		B_TRANSLATE("Used space color"),
		new BMessage(kSpaceBarSwitchColor)));
	item->SetMarked(true);
	fCurrentColor = 0;
	menu->AddItem(new BMenuItem(
		B_TRANSLATE("Free space color"),
		new BMessage(kSpaceBarSwitchColor)));
	menu->AddItem(new BMenuItem(
		B_TRANSLATE("Warning space color"),
		new BMessage(kSpaceBarSwitchColor)));

	fColorPicker = new BMenuField("menu", NULL, menu);

	fColorControl = new BColorControl(BPoint(0, 0),
		B_CELLS_16x16, 1, "SpaceColorControl",
		new BMessage(kSpaceBarColorChanged));
	fColorControl->SetValue(TrackerSettings().UsedSpaceColor());

	BBox* box = new BBox("box");
	box->SetLabel(fColorPicker);
	box->AddChild(BLayoutBuilder::Group<>(B_HORIZONTAL)
		.Add(fColorControl)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.View());

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.Add(fSpaceBarShowCheckBox)
		.Add(box)
		.AddGlue()
		.SetInsets(B_USE_DEFAULT_SPACING);
}


SpaceBarSettingsView::~SpaceBarSettingsView()
{
}


/**
 * @brief Set message targets for space-bar controls when attached to a window.
 */
void
SpaceBarSettingsView::AttachedToWindow()
{
	fSpaceBarShowCheckBox->SetTarget(this);
	fColorControl->SetTarget(this);
	fColorPicker->Menu()->SetTargetForItems(this);
}


/**
 * @brief Handle space-bar and colour-picker change messages.
 *
 * @param message  The incoming control-change message.
 */
void
SpaceBarSettingsView::MessageReceived(BMessage* message)
{
	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	if (tracker == NULL)
		return;

	TrackerSettings settings;

	switch (message->what) {
		case kUpdateVolumeSpaceBar:
		{
			settings.SetShowVolumeSpaceBar(
				fSpaceBarShowCheckBox->Value() == 1);
			Window()->PostMessage(kSettingsContentsModified);
			tracker->PostMessage(kShowVolumeSpaceBar);
			break;
		}

		case kSpaceBarSwitchColor:
		{
			fCurrentColor = message->FindInt32("index");
			switch (fCurrentColor) {
				case 0:
					fColorControl->SetValue(settings.UsedSpaceColor());
					break;

				case 1:
					fColorControl->SetValue(settings.FreeSpaceColor());
					break;

				case 2:
					fColorControl->SetValue(settings.WarningSpaceColor());
					break;
			}
			break;
		}

		case kSpaceBarColorChanged:
		{
			rgb_color color = fColorControl->ValueAsColor();
			color.alpha = kDefaultSpaceBarAlpha;
				// alpha is ignored by BColorControl but is checked
				// in equalities

			switch (fCurrentColor) {
				case 0:
					settings.SetUsedSpaceColor(color);
					break;

				case 1:
					settings.SetFreeSpaceColor(color);
					break;

				case 2:
					settings.SetWarningSpaceColor(color);
					break;
			}

			BWindow* window = Window();
			if (window != NULL)
				window->PostMessage(kSettingsContentsModified);

			tracker->PostMessage(kSpaceBarColorChanged);
			break;
		}

		default:
			_inherited::MessageReceived(message);
			break;
	}
}


/**
 * @brief Restore Volume icons / space-bar preferences to their factory defaults.
 */
void
SpaceBarSettingsView::SetDefaults()
{
	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	if (tracker == NULL)
		return;

	TrackerSettings settings;

	if (!settings.ShowVolumeSpaceBar()) {
		settings.SetShowVolumeSpaceBar(true);
		send_bool_notices(kShowVolumeSpaceBar, "ShowVolumeSpaceBar", kDefaultShowVolumeSpaceBar);
	}

	if (settings.UsedSpaceColor() != kDefaultUsedSpaceColor
		|| settings.FreeSpaceColor() != kDefaultFreeSpaceColor
		|| settings.WarningSpaceColor() != kDefaultWarningSpaceColor) {
		settings.SetUsedSpaceColor(kDefaultUsedSpaceColor);
		settings.SetFreeSpaceColor(kDefaultFreeSpaceColor);
		settings.SetWarningSpaceColor(kDefaultWarningSpaceColor);
		tracker->SendNotices(kSpaceBarColorChanged);
	}

	ShowCurrentSettings();
}


/**
 * @brief Return true if any Volume icons preference differs from its factory default.
 *
 * @return true if the Defaults button should be enabled.
 */
bool
SpaceBarSettingsView::IsDefaultable() const
{
	TrackerSettings settings;

	return settings.ShowVolumeSpaceBar() != kDefaultShowVolumeSpaceBar
		|| settings.UsedSpaceColor() != kDefaultUsedSpaceColor
		|| settings.FreeSpaceColor() != kDefaultFreeSpaceColor
		|| settings.WarningSpaceColor() != kDefaultWarningSpaceColor;
}


/**
 * @brief Restore Volume icons preferences to the values recorded when the window opened.
 */
void
SpaceBarSettingsView::Revert()
{
	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	if (tracker == NULL)
		return;

	TrackerSettings settings;

	if (settings.ShowVolumeSpaceBar() != fSpaceBarShow) {
		settings.SetShowVolumeSpaceBar(fSpaceBarShow);
		send_bool_notices(kShowVolumeSpaceBar, "ShowVolumeSpaceBar",
			fSpaceBarShow);
	}

	if (settings.UsedSpaceColor() != fUsedSpaceColor
		|| settings.FreeSpaceColor() != fFreeSpaceColor
		|| settings.WarningSpaceColor() != fWarningSpaceColor) {
		settings.SetUsedSpaceColor(fUsedSpaceColor);
		settings.SetFreeSpaceColor(fFreeSpaceColor);
		settings.SetWarningSpaceColor(fWarningSpaceColor);
		tracker->SendNotices(kSpaceBarColorChanged);
	}

	ShowCurrentSettings();
}


/**
 * @brief Refresh Volume icons view widgets from the current TrackerSettings.
 */
void
SpaceBarSettingsView::ShowCurrentSettings()
{
	TrackerSettings settings;

	fSpaceBarShowCheckBox->SetValue(settings.ShowVolumeSpaceBar());

	switch (fCurrentColor) {
		case 0:
			fColorControl->SetValue(settings.UsedSpaceColor());
			break;
		case 1:
			fColorControl->SetValue(settings.FreeSpaceColor());
			break;
		case 2:
			fColorControl->SetValue(settings.WarningSpaceColor());
			break;
	}
}


/**
 * @brief Snapshot the current Volume icons preferences for later use by Revert().
 */
void
SpaceBarSettingsView::RecordRevertSettings()
{
	TrackerSettings settings;

	fSpaceBarShow = settings.ShowVolumeSpaceBar();
	fUsedSpaceColor = settings.UsedSpaceColor();
	fFreeSpaceColor = settings.FreeSpaceColor();
	fWarningSpaceColor = settings.WarningSpaceColor();
}


/**
 * @brief Return true if any Volume icons preference differs from the saved revert state.
 *
 * @return true if the Revert button should be enabled.
 */
bool
SpaceBarSettingsView::IsRevertable() const
{
	TrackerSettings settings;

	return fSpaceBarShow != settings.ShowVolumeSpaceBar()
		|| fUsedSpaceColor != settings.UsedSpaceColor()
		|| fFreeSpaceColor != settings.FreeSpaceColor()
		|| fWarningSpaceColor != settings.WarningSpaceColor();
}
