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
 *   Copyright 2008-2009, Pier Luigi Fiorini. All Rights Reserved.
 *   Copyright 2004-2008, Michael Davidson. All Rights Reserved.
 *   Copyright 2004-2007, Mikael Eiman. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Davidson, slaad@bong.com.au
 *       Mikael Eiman, mikael@eiman.tv
 *       Pier Luigi Fiorini, pierluigi.fiorini@gmail.com
 *       Brian Hill, supernova@tycho.email
 */

/** @file NotificationWindow.cpp
 *  @brief Implementation of the on-screen notification window: layout, settings, message routing. */

#include "NotificationWindow.h"

#include <algorithm>

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Deskbar.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <GroupLayout.h>
#include <NodeMonitor.h>
#include <Notifications.h>
#include <Path.h>
#include <Point.h>
#include <PropertyInfo.h>
#include <Screen.h>

#include "AppGroupView.h"
#include "AppUsage.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NotificationWindow"


/** @brief Scripting property descriptors for the notification window. */
property_info main_prop_list[] = {
	{"message", {B_GET_PROPERTY, 0}, {B_INDEX_SPECIFIER, 0},
		"get a message"},
	{"message", {B_COUNT_PROPERTIES, 0}, {B_DIRECT_SPECIFIER, 0},
		"count messages"},
	{"message", {B_CREATE_PROPERTY, 0}, {B_DIRECT_SPECIFIER, 0},
		"create a message"},
	{"message", {B_SET_PROPERTY, 0}, {B_INDEX_SPECIFIER, 0},
		"modify a message"},

	{ 0 }
};


/**
 * @brief Checks whether the notification position overlaps the Deskbar position.
 *
 * Compares the current Deskbar location against the user-configured notification
 * corner.  If they occupy the same screen edge/corner the notification window
 * should follow the Deskbar instead to avoid visual overlap.
 *
 * @param deskbar      Current Deskbar location constant.
 * @param notification Notification position flags (combination of B_FOLLOW_*).
 * @return @c true if the two positions overlap, @c false otherwise.
 */
static bool
is_overlapping(deskbar_location deskbar,
		uint32 notification) {
	if (deskbar == B_DESKBAR_RIGHT_TOP
			&& notification == (B_FOLLOW_RIGHT | B_FOLLOW_TOP))
		return true;
	if (deskbar == B_DESKBAR_RIGHT_BOTTOM
			&& notification == (B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM))
		return true;
	if (deskbar == B_DESKBAR_LEFT_TOP
			&& notification == (B_FOLLOW_LEFT | B_FOLLOW_TOP))
		return true;
	if (deskbar == B_DESKBAR_LEFT_BOTTOM
			&& notification == (B_FOLLOW_LEFT | B_FOLLOW_BOTTOM))
		return true;
	if (deskbar == B_DESKBAR_TOP
			&& (notification == (B_FOLLOW_LEFT | B_FOLLOW_TOP)
			|| notification == (B_FOLLOW_RIGHT | B_FOLLOW_TOP)))
		return true;
	if (deskbar == B_DESKBAR_BOTTOM
			&& (notification == (B_FOLLOW_LEFT | B_FOLLOW_BOTTOM)
			|| notification == (B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM)))
		return true;
	return false;
}


/**
 * @brief Constructs the floating notification window.
 *
 * Creates a borderless, non-interactive floating window on all workspaces,
 * initialises the notification cache directory, applies a vertical group
 * layout, loads the user settings (with filesystem monitoring), and enters
 * the hidden message loop.
 */
NotificationWindow::NotificationWindow()
	:
	BWindow(BRect(0, 0, -1, -1), B_TRANSLATE_MARK("Notification"),
		B_BORDERED_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL, B_AVOID_FRONT
		| B_AVOID_FOCUS | B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
		| B_NOT_RESIZABLE | B_NOT_MOVABLE | B_AUTO_UPDATE_SIZE_LIMITS,
		B_ALL_WORKSPACES),
	fShouldRun(true)
{
	status_t result = find_directory(B_USER_CACHE_DIRECTORY, &fCachePath);
	fCachePath.Append("Notifications");
	BDirectory cacheDir;
	result = cacheDir.SetTo(fCachePath.Path());
	if (result == B_ENTRY_NOT_FOUND)
		cacheDir.CreateDirectory(fCachePath.Path(), NULL);

	SetLayout(new BGroupLayout(B_VERTICAL, 0));

	_LoadSettings(true);

	// Start the message loop
	Hide();
	Show();
}


/**
 * @brief Destroys the notification window and frees all application filter entries.
 */
NotificationWindow::~NotificationWindow()
{
	appfilter_t::iterator aIt;
	for (aIt = fAppFilters.begin(); aIt != fAppFilters.end(); aIt++)
		delete aIt->second;
}


/**
 * @brief Handles the window-close request by tearing down all app-group views.
 *
 * Removes and deletes every AppGroupView, then asks the application to quit.
 *
 * @return The value returned by BWindow::QuitRequested().
 */
bool
NotificationWindow::QuitRequested()
{
	appview_t::iterator aIt;
	for (aIt = fAppViews.begin(); aIt != fAppViews.end(); aIt++) {
		aIt->second->RemoveSelf();
		delete aIt->second;
	}

	BMessenger(be_app).SendMessage(B_QUIT_REQUESTED);
	return BWindow::QuitRequested();
}


/**
 * @brief Repositions the window when a workspace becomes active.
 *
 * Ensures the notification window stays in the correct screen corner when
 * the user switches workspaces.
 *
 * @param workspace The workspace index (unused).
 * @param active    @c true if the workspace is now active.
 */
void
NotificationWindow::WorkspaceActivated(int32 /*workspace*/, bool active)
{
	// Ensure window is in the correct position
	if (active)
		SetPosition();
}


/**
 * @brief Repositions the window after a frame resize.
 *
 * @param width  New width of the window frame.
 * @param height New height of the window frame.
 */
void
NotificationWindow::FrameResized(float width, float height)
{
	SetPosition();
}


/**
 * @brief Repositions the window when the screen resolution or color space changes.
 *
 * @param frame The new screen frame rectangle.
 * @param mode  The new color space.
 */
void
NotificationWindow::ScreenChanged(BRect frame, color_space mode)
{
	SetPosition();
}


/**
 * @brief Dispatches messages related to notification display and lifecycle.
 *
 * Handles B_NODE_MONITOR (settings file changed), kNotificationMessage
 * (display a new notification), and kRemoveGroupView (remove an empty
 * app-group view).  Unrecognised messages are forwarded to the base class.
 *
 * @param message The incoming BMessage.
 */
void
NotificationWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NODE_MONITOR:
		{
			_LoadSettings();
			break;
		}
		case kNotificationMessage:
		{
			if (!fShouldRun)
				break;

			BMessage reply(B_REPLY);
			BNotification* notification = new BNotification(message);

			if (notification->InitCheck() == B_OK) {
				bigtime_t timeout;
				if (message->FindInt64("timeout", &timeout) != B_OK)
					timeout = fTimeout;
				BString sourceSignature(notification->SourceSignature());
				BString sourceName(notification->SourceName());

				bool allow = false;
				appfilter_t::iterator it = fAppFilters
					.find(sourceSignature.String());

				AppUsage* appUsage = NULL;
				if (it == fAppFilters.end()) {
					if (sourceSignature.Length() > 0
						&& sourceName.Length() > 0) {
						appUsage = new AppUsage(sourceName.String(),
							sourceSignature.String(), true);
						fAppFilters[sourceSignature.String()] = appUsage;
						// TODO save back to settings file
					}
					allow = true;
				} else {
					appUsage = it->second;
					allow = appUsage->Allowed();
				}

				if (allow) {
					BString groupName(notification->Group());
					appview_t::iterator aIt = fAppViews.find(groupName);
					AppGroupView* group = NULL;
					if (aIt == fAppViews.end()) {
						group = new AppGroupView(this,
							groupName == "" ? NULL : groupName.String());
						fAppViews[groupName] = group;
						GetLayout()->AddView(group);
					} else
						group = aIt->second;

					NotificationView* view = new NotificationView(notification, timeout);

					group->AddInfo(view);

					SetPosition();
					_ShowHide();

					reply.AddInt32("error", B_OK);
				} else
					reply.AddInt32("error", B_NOT_ALLOWED);
			} else {
				reply.what = B_MESSAGE_NOT_UNDERSTOOD;
				reply.AddInt32("error", B_ERROR);
			}

			message->SendReply(&reply);
			break;
		}
		case kRemoveGroupView:
		{
			AppGroupView* view = NULL;
			if (message->FindPointer("view", (void**)&view) != B_OK)
				return;

			// It's possible that between sending this message, and us receiving
			// it, the view has become used again, in which case we shouldn't
			// delete it.
			if (view->HasChildren())
				return;

			// this shouldn't happen
			if (fAppViews.erase(view->Group()) < 1)
				break;

			view->RemoveSelf();
			delete view;

			_ShowHide();
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}


/**
 * @brief Returns the configured notification timeout in microseconds.
 *
 * @return The timeout value loaded from settings.
 */
int32
NotificationWindow::Timeout()
{
	return fTimeout;
}


/**
 * @brief Returns the configured notification window width in pixels.
 *
 * @return The window width loaded from settings.
 */
float
NotificationWindow::Width()
{
	return fWidth;
}


/**
 * @brief Shows or hides the notification window based on whether any views remain.
 *
 * If there are no app-group views the window is hidden; otherwise it is shown.
 */
void
NotificationWindow::_ShowHide()
{
	if (fAppViews.empty() && !IsHidden()) {
		Hide();
		return;
	}

	if (IsHidden())
		Show();
}


/**
 * @brief Computes and applies the on-screen position of the notification window.
 *
 * Takes into account the user-configured corner, the current Deskbar location
 * (to avoid overlap), decorator border offsets, and the screen frame.  The
 * window is moved to the resulting position after a layout pass.
 */
void
NotificationWindow::SetPosition()
{
	Layout(true);

	BRect bounds = DecoratorFrame();
	float width = Bounds().Width() + 1;
	float height = Bounds().Height() + 1;

	float leftOffset = Frame().left - bounds.left;
	float topOffset = Frame().top - bounds.top + 1;
	float rightOffset = bounds.right - Frame().right;
	float bottomOffset = bounds.bottom - Frame().bottom;
		// Size of the borders around the window

	float x = Frame().left;
	float y = Frame().top;
		// If we cant guess, don't move...
	BPoint location(x, y);

	BDeskbar deskbar;

	// If notification and deskbar position are same
	// then follow deskbar position
	bool overlapping = is_overlapping(deskbar.Location(), fPosition);
	uint32 position = overlapping ? B_FOLLOW_DESKBAR : fPosition;

	if (position == B_FOLLOW_DESKBAR) {
		BRect frame = deskbar.Frame();
		switch (deskbar.Location()) {
			case B_DESKBAR_TOP:
				// In case of overlapping here or for bottom
				// use user's notification position
				y = frame.bottom + topOffset;
				x = (fPosition == (B_FOLLOW_LEFT | B_FOLLOW_TOP))
					? frame.left + rightOffset
					: frame.right - width + rightOffset;
				break;
			case B_DESKBAR_BOTTOM:
				y = frame.top - height - bottomOffset;
				x = (fPosition == (B_FOLLOW_LEFT | B_FOLLOW_BOTTOM))
					? frame.left + rightOffset
					: frame.right - width + rightOffset;
				break;
			case B_DESKBAR_RIGHT_TOP:
				y = frame.top - topOffset + 1;
				x = frame.left - width - rightOffset;
				break;
			case B_DESKBAR_LEFT_TOP:
				y = frame.top - topOffset + 1;
				x = frame.right + leftOffset;
				break;
			case B_DESKBAR_RIGHT_BOTTOM:
				y = frame.bottom - height + bottomOffset;
				x = frame.left - width - rightOffset;
				break;
			case B_DESKBAR_LEFT_BOTTOM:
				y = frame.bottom - height + bottomOffset;
				x = frame.right + leftOffset;
				break;
			default:
				break;
		}
		location = BPoint(x, y);
	} else if (position == (B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM)) {
		location = BScreen().Frame().RightBottom();
		location -= BPoint(width, height);
	} else if (position == (B_FOLLOW_LEFT | B_FOLLOW_BOTTOM)) {
		location = BScreen().Frame().LeftBottom();
		location -= BPoint(0, height);
	} else if (position == (B_FOLLOW_RIGHT | B_FOLLOW_TOP)) {
		location = BScreen().Frame().RightTop();
		location -= BPoint(width, 0);
	} else if (position == (B_FOLLOW_LEFT | B_FOLLOW_TOP)) {
		location = BScreen().Frame().LeftTop();
	}

	MoveTo(location);
}


/**
 * @brief Loads all notification settings from the user settings file.
 *
 * Reads and unflattens the settings BMessage, then delegates to
 * _LoadGeneralSettings(), _LoadDisplaySettings(), and _LoadAppFilters().
 * Optionally starts a node monitor on the settings file so that live
 * changes are detected.
 *
 * @param startMonitor If @c true, a B_WATCH_ALL node monitor is installed
 *                     on the settings file.
 */
void
NotificationWindow::_LoadSettings(bool startMonitor)
{
	BPath path;
	BMessage settings;

	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;

	path.Append(kSettingsFile);

	BFile file(path.Path(), B_READ_ONLY | B_CREATE_FILE);
	settings.Unflatten(&file);

	_LoadGeneralSettings(settings);
	_LoadDisplaySettings(settings);
	_LoadAppFilters(settings);

	if (startMonitor) {
		node_ref nref;
		BEntry entry(path.Path());
		entry.GetNodeRef(&nref);

		if (watch_node(&nref, B_WATCH_ALL, BMessenger(this)) != B_OK) {
			BAlert* alert = new BAlert(B_TRANSLATE("Warning"),
				B_TRANSLATE("Couldn't start general settings monitor.\n"
					"Live filter changes disabled."), B_TRANSLATE("OK"));
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go(NULL);
		}
	}
}


/**
 * @brief Loads per-application notification filter entries from the settings message.
 *
 * Each "app_usage" flat entry is unflattened into an AppUsage object and stored
 * in the fAppFilters map keyed by application signature.
 *
 * @param settings The settings BMessage containing zero or more "app_usage" entries.
 */
void
NotificationWindow::_LoadAppFilters(BMessage& settings)
{
	type_code type;
	int32 count = 0;

	if (settings.GetInfo("app_usage", &type, &count) != B_OK)
		return;

	for (int32 i = 0; i < count; i++) {
		AppUsage* app = new AppUsage();
		if (settings.FindFlat("app_usage", i, app) == B_OK)
			fAppFilters[app->Signature()] = app;
		else
			delete app;
	}
}


/**
 * @brief Loads general daemon settings (auto-start flag, timeout) from the settings message.
 *
 * If auto-start is disabled the application is asked to quit.  The timeout
 * value is converted from seconds to microseconds.
 *
 * @param settings The settings BMessage to read from.
 */
void
NotificationWindow::_LoadGeneralSettings(BMessage& settings)
{
	if (settings.FindBool(kAutoStartName, &fShouldRun) == B_OK) {
		if (fShouldRun == false) {
			// We should not start. Quit the app!
			be_app_messenger.SendMessage(B_QUIT_REQUESTED);
		}
	} else
		fShouldRun = true;

	if (settings.FindInt32(kTimeoutName, &fTimeout) != B_OK)
		fTimeout = kDefaultTimeout;
	fTimeout *= 1000000;
		// Convert from seconds to microseconds
}


/**
 * @brief Loads display settings (width, position) from the settings message.
 *
 * Updates the window width and notification position.  If the width changed,
 * the layout's explicit size is updated.  All existing app-group views are
 * invalidated so they redraw with the new settings.
 *
 * @param settings The settings BMessage to read from.
 */
void
NotificationWindow::_LoadDisplaySettings(BMessage& settings)
{
	float originalWidth = fWidth;
	if (settings.FindFloat(kWidthName, &fWidth) != B_OK)
		fWidth = kDefaultWidth * be_plain_font->Size() / 12.0f;
	if (originalWidth != fWidth)
		GetLayout()->SetExplicitSize(BSize(fWidth, B_SIZE_UNSET));

	int32 position;
	if (settings.FindInt32(kNotificationPositionName, &position) != B_OK)
		fPosition = kDefaultNotificationPosition;
	else
		fPosition = position;

	// Notify the views about the change
	appview_t::iterator aIt;
	for (aIt = fAppViews.begin(); aIt != fAppViews.end(); ++aIt) {
		AppGroupView* view = aIt->second;
		view->Invalidate();
	}
}
