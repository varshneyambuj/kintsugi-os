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
 * @file GeneralView.cpp
 * @brief Implementation of GeneralView, the General tab of the
 *        Notifications preflet.
 *
 * Edits the master enable flag, popup window width, display timeout, and
 * on-screen position used by notification_server. The pane keeps an
 * original snapshot of every value so Revert() can restore the user's
 * loaded state and DefaultsPossible() can compare against the factory
 * defaults.
 */


#include <stdio.h>
#include <stdlib.h>

#include <vector>

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <Node.h>
#include <Path.h>
#include <Query.h>
#include <Roster.h>
#include <String.h>
#include <StringFormat.h>
#include <SymLink.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <notification/Notifications.h>

#include "GeneralView.h"
#include "NotificationsConstants.h"
#include "SettingsHost.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "GeneralView"

/** @brief Notifications-enable checkbox toggled. */
const uint32 kToggleNotifications = '_TSR';
/** @brief Window-width slider value changed. */
const uint32 kWidthChanged = '_WIC';
/** @brief Duration slider value changed. */
const uint32 kTimeoutChanged = '_TIC';
/** @brief Position pop-up selection changed. */
const uint32 kPositionChanged = '_NPC';
/** @brief Reserved: dispatched after a server-state change is requested. */
const uint32 kServerChangeTriggered = '_SCT';
/** @brief Stable identifier used for the Apply-with-example notification. */
const BString kSampleMessageID("NotificationsSample");


/**
 * @brief Maps a B_FOLLOW_* position bitmask to the corresponding pop-up
 *        item index.
 *
 * @param notification_position  Position flags read from settings.
 * @return Index in the position pop-up; defaults to 0 (Follow Deskbar).
 */
static int32
notification_position_to_index(uint32 notification_position) {
	if (notification_position == B_FOLLOW_NONE)
		return 0;
	else if (notification_position == (B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM))
		return 1;
	else if (notification_position == (B_FOLLOW_LEFT | B_FOLLOW_BOTTOM))
		return 2;
	else if (notification_position == (B_FOLLOW_RIGHT | B_FOLLOW_TOP))
		return 3;
	else if (notification_position == (B_FOLLOW_LEFT | B_FOLLOW_TOP))
		return 4;
	return 0;
}


/**
 * @brief Constructs the General view: enable checkbox, width and duration
 *        sliders, and the position pop-up.
 *
 * @param host  Settings host receiving change notifications.
 */
GeneralView::GeneralView(SettingsHost* host)
	:
	SettingsPane("general", host)
{
	// Notification server
	fNotificationBox = new BCheckBox("server",
		B_TRANSLATE("Enable notifications"),
		new BMessage(kToggleNotifications));
	BBox* box = new BBox("box");
	box->SetLabel(fNotificationBox);

	// Window width
	float ratio = be_plain_font->Size() / 12.f;
	int32 minWidth = int32(kMinimumWidth / kWidthStep * ratio);
	int32 maxWidth = int32(kMaximumWidth / kWidthStep * ratio);
	fWidthSlider = new BSlider("width", B_TRANSLATE("Window width"),
		new BMessage(kWidthChanged), minWidth, maxWidth, B_HORIZONTAL);
	fWidthSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fWidthSlider->SetHashMarkCount(maxWidth - minWidth + 1);
	fWidthSlider->SetLimitLabels(
		B_TRANSLATE_COMMENT("narrow", "Window width: Slider low text"),
		B_TRANSLATE_COMMENT("wide", "Window width: Slider high text"));

	// Display time
	fDurationSlider = new BSlider("duration", B_TRANSLATE("Duration:"),
		new BMessage(kTimeoutChanged), kMinimumTimeout, kMaximumTimeout,
		B_HORIZONTAL);
	fDurationSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fDurationSlider->SetHashMarkCount(kMaximumTimeout - kMinimumTimeout + 1);
	BString minLabel;
	minLabel << kMinimumTimeout;
	BString maxLabel;
	maxLabel << kMaximumTimeout;
	fDurationSlider->SetLimitLabels(
		B_TRANSLATE_COMMENT(minLabel.String(), "Slider low text"),
		B_TRANSLATE_COMMENT(maxLabel.String(), "Slider high text"));

	// Notification Position
	fPositionMenu = new BPopUpMenu(B_TRANSLATE("Follow Deskbar"));
	const char* positionLabels[] = {
		B_TRANSLATE_MARK("Follow Deskbar"),
		B_TRANSLATE_MARK("Lower right"),
		B_TRANSLATE_MARK("Lower left"),
		B_TRANSLATE_MARK("Upper right"),
		B_TRANSLATE_MARK("Upper left")
	};
	const uint32 positions[] = {
		B_FOLLOW_DESKBAR,                   // Follow Deskbar
		B_FOLLOW_BOTTOM | B_FOLLOW_RIGHT,   // Lower right
		B_FOLLOW_BOTTOM | B_FOLLOW_LEFT,    // Lower left
		B_FOLLOW_TOP    | B_FOLLOW_RIGHT,   // Upper right
		B_FOLLOW_TOP    | B_FOLLOW_LEFT     // Upper left
	};
	for (int i=0; i < 5; i++) {
		BMessage* message = new BMessage(kPositionChanged);
		message->AddInt32(kNotificationPositionName, positions[i]);

		fPositionMenu->AddItem(new BMenuItem(B_TRANSLATE_NOCOLLECT(
			positionLabels[i]), message));
	}
	BMenuField* positionField = new BMenuField(B_TRANSLATE("Position:"),
		fPositionMenu);

	box->AddChild(BLayoutBuilder::Group<>(B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fWidthSlider)
		.Add(fDurationSlider)
		.Add(positionField)
		.AddGlue()
		.View());

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(box)
	.End();
}


/**
 * @brief Retargets every control to this view once it joins a window.
 */
void
GeneralView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fNotificationBox->SetTarget(this);
	fWidthSlider->SetTarget(this);
	fDurationSlider->SetTarget(this);
	fPositionMenu->SetTargetForItems(this);
}


/**
 * @brief Routes the four control-changed messages.
 *
 * Edits other than the server toggle and width slider also display a
 * sample notification on Apply so the user can preview the change.
 *
 * @param msg  Incoming BMessage.
 */
void
GeneralView::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kToggleNotifications:
		{
			SettingsPane::SettingsChanged(false);
			_EnableControls();
			break;
		}
		case kWidthChanged: {
			SettingsPane::SettingsChanged(true);
			break;
		}
		case kTimeoutChanged:
		{
			int32 value = fDurationSlider->Value();
			_SetTimeoutLabel(value);
			SettingsPane::SettingsChanged(true);
			break;
		}
		case kPositionChanged:
		{
			int32 position;
			if (msg->FindInt32(kNotificationPositionName, &position) == B_OK) {
				fNewPosition = position;
				SettingsPane::SettingsChanged(true);
			}
			break;
		}
		default:
			BView::MessageReceived(msg);
			break;
	}
}


/**
 * @brief Loads persisted values from @a settings, clamping out-of-range
 *        entries to defaults, then resets the controls via Revert().
 *
 * @param settings  BMessage previously written by Save().
 * @return Result of Revert() (always B_OK in current code).
 * @todo Re-save when out-of-range values are clamped.
 */
status_t
GeneralView::Load(BMessage& settings)
{
	bool autoStart = settings.GetBool(kAutoStartName, true);
	fNotificationBox->SetValue(autoStart ? B_CONTROL_ON : B_CONTROL_OFF);

	if (settings.FindFloat(kWidthName, &fOriginalWidth) != B_OK
		|| fOriginalWidth > kMaximumWidth
		|| fOriginalWidth < kMinimumWidth)
		fOriginalWidth = kDefaultWidth;

	if (settings.FindInt32(kTimeoutName, &fOriginalTimeout) != B_OK
		|| fOriginalTimeout > kMaximumTimeout
		|| fOriginalTimeout < kMinimumTimeout)
		fOriginalTimeout = kDefaultTimeout;
// TODO need to save again if values outside of expected range
	int32 setting;
	if (settings.FindInt32(kIconSizeName, &setting) != B_OK)
		fOriginalIconSize = kDefaultIconSize;
	else
		fOriginalIconSize = (icon_size)setting;

	int32 position;
	if (settings.FindInt32(kNotificationPositionName, &position) != B_OK)
		fOriginalPosition = kDefaultNotificationPosition;
	else
		fOriginalPosition = position;

	_EnableControls();

	return Revert();
}


/**
 * @brief Writes the current control values into @a settings.
 *
 * Always emits B_LARGE_ICON for the icon size since the UI does not yet
 * expose a control for it.
 *
 * @param settings  Message to populate.
 * @return Always B_OK.
 */
status_t
GeneralView::Save(BMessage& settings)
{
	bool autoStart = (fNotificationBox->Value() == B_CONTROL_ON);
	settings.AddBool(kAutoStartName, autoStart);

	int32 timeout = fDurationSlider->Value();
	settings.AddInt32(kTimeoutName, timeout);

	float width = fWidthSlider->Value() * kWidthStep;
	settings.AddFloat(kWidthName, width);

	icon_size iconSize = B_LARGE_ICON;
	settings.AddInt32(kIconSizeName, (int32)iconSize);

	settings.AddInt32(kNotificationPositionName, (int32)fNewPosition);

	return B_OK;
}


/**
 * @brief Restores controls to the values captured by Load().
 *
 * @return Always B_OK.
 */
status_t
GeneralView::Revert()
{
	fDurationSlider->SetValue(fOriginalTimeout);
	_SetTimeoutLabel(fOriginalTimeout);

	fWidthSlider->SetValue(fOriginalWidth / kWidthStep);

	fNewPosition = fOriginalPosition;
	BMenuItem* item = fPositionMenu->ItemAt(
		notification_position_to_index(fNewPosition));
	if (item != NULL)
		item->SetMarked(true);

	return B_OK;
}


/**
 * @brief Reports whether any of timeout, width, or position differ from
 *        their loaded values.
 *
 * @return true when Revert() would change something.
 */
bool
GeneralView::RevertPossible()
{
	int32 timeout = fDurationSlider->Value();
	if (fOriginalTimeout != timeout)
		return true;

	int32 width = fWidthSlider->Value() * kWidthStep;
	if (fOriginalWidth != width)
		return true;

	if (fOriginalPosition != fNewPosition)
		return true;

	return false;
}


/**
 * @brief Resets controls to factory defaults.
 *
 * @return Always B_OK.
 */
status_t
GeneralView::Defaults()
{
	fDurationSlider->SetValue(kDefaultTimeout);
	_SetTimeoutLabel(kDefaultTimeout);

	fWidthSlider->SetValue(kDefaultWidth / kWidthStep);

	fNewPosition = kDefaultNotificationPosition;
	BMenuItem* item = fPositionMenu->ItemAt(
		notification_position_to_index(fNewPosition));
	if (item != NULL)
		item->SetMarked(true);

	return B_OK;
}


/**
 * @brief Reports whether the current control values differ from the
 *        factory defaults.
 *
 * @return true when Defaults() would change something.
 */
bool
GeneralView::DefaultsPossible()
{
	int32 timeout = fDurationSlider->Value();
	if (kDefaultTimeout != timeout)
		return true;

	int32 width = fWidthSlider->Value() * kWidthStep;
	if (kDefaultWidth != width)
		return true;

	if (kDefaultNotificationPosition != fNewPosition)
		return true;

	return false;
}


/**
 * @brief Indicates that this pane participates in the global Defaults /
 *        Revert button strip.
 *
 * @return Always true.
 */
bool
GeneralView::UseDefaultRevertButtons()
{
	return true;
}


/**
 * @brief Mirrors the master enable checkbox into the dependent controls
 *        and selects the loaded position in the pop-up.
 */
void
GeneralView::_EnableControls()
{
	bool enabled = fNotificationBox->Value() == B_CONTROL_ON;
	fWidthSlider->SetEnabled(enabled);
	fDurationSlider->SetEnabled(enabled);
	BMenuItem* item = fPositionMenu->ItemAt(
		notification_position_to_index(fOriginalPosition));
	if (item != NULL)
		item->SetMarked(true);
}


/**
 * @brief Updates the duration slider's label to a localized
 *        "Timeout: N second(s)" string.
 *
 * @param value  Current slider value in seconds.
 */
void
GeneralView::_SetTimeoutLabel(int32 value)
{
	static BStringFormat format(B_TRANSLATE("{0, plural, "
		"=1{Timeout: # second}"
		"other{Timeout: # seconds}}"));
	BString label;
	format.Format(label, value);
	fDurationSlider->SetLabel(label.String());
}


/**
 * @brief Reports whether notification_server is currently running.
 *
 * @return true when the roster lists the notification server signature.
 */
bool
GeneralView::_IsServerRunning()
{
	return be_roster->IsRunning(kNotificationServerSignature);
}
