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
 *   Copyright 2017 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Brian Hill
 */


/**
 * @file RepositoriesWindow.cpp
 * @brief Implementation of the top-level window for the Repositories preflet.
 *
 * Owns a single RepositoriesView, restores its previous frame from settings,
 * watches the system package-repositories directory for outside changes,
 * and drives the modal Add-by-URL dialog.
 */


#include "RepositoriesWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <NodeMonitor.h>
#include <Region.h>
#include <Screen.h>

#include "AddRepoWindow.h"
#include "constants.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "RepositoriesWindow"


/**
 * @brief Builds the preflet window, restores its frame, and starts watching.
 *
 * Resolves the system package-repositories directory (settings preferred,
 * cache fallback), grabs its node_ref so that subsequent external changes
 * can be observed, and registers a node monitor.
 */
RepositoriesWindow::RepositoriesWindow()
	:
	BWindow(BRect(50, 50, 500, 400), B_TRANSLATE_SYSTEM_NAME("Repositories"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS
			| B_AUTO_UPDATE_SIZE_LIMITS),
	fAddWindow(NULL),
	fPackageNodeStatus(B_ERROR)
{
	fView = new RepositoriesView();
	BLayoutBuilder::Group<>(this, B_VERTICAL).Add(fView).End();

	// Size and location on screen
	BRect frame = fSettings.GetFrame();
	ResizeTo(frame.Width(), frame.Height());
	BScreen screen;
	BRect screenFrame = screen.Frame();
	if (screenFrame.right < frame.right || screenFrame.left > frame.left
		|| screenFrame.top > frame.top || screenFrame.bottom < frame.bottom) {
		CenterOnScreen();
	}
	else
		MoveTo(frame.left, frame.top);
	Show();

	fMessenger.SetTo(this);

	// Find the pkgman settings or cache directory
	BPath packagePath;
	// /boot/system/settings/package-repositories
	status_t status = find_directory(B_SYSTEM_SETTINGS_DIRECTORY,
		&packagePath);
	if (status == B_OK)
		status = packagePath.Append("package-repositories");
	else {
		// /boot/system/cache/package-repositories
		status = find_directory(B_SYSTEM_CACHE_DIRECTORY, &packagePath);
		if (status == B_OK)
			status = packagePath.Append("package-repositories");
	}
	if (status == B_OK) {
		BNode packageNode(packagePath.Path());
		if (packageNode.InitCheck()==B_OK && packageNode.IsDirectory())
			fPackageNodeStatus = packageNode.GetNodeRef(&fPackageNodeRef);
	}

	// watch the pkgman settings or cache directory for changes
	_StartWatching();
}


/**
 * @brief Stops watching the package directory before the window is destroyed.
 */
RepositoriesWindow::~RepositoriesWindow()
{
	_StopWatching();
}


/**
 * @brief Begins monitoring the package-repositories directory for changes.
 *
 * Sets fWatchingPackageNode to true on success so _StopWatching() knows
 * whether a corresponding stop call is required.
 */
void
RepositoriesWindow::_StartWatching()
{
	if (fPackageNodeStatus == B_OK) {
		status_t result = watch_node(&fPackageNodeRef, B_WATCH_DIRECTORY, this);
		fWatchingPackageNode = (result == B_OK);
	}
}


/**
 * @brief Stops monitoring the package-repositories directory.
 */
void
RepositoriesWindow::_StopWatching()
{
	if (fPackageNodeStatus == B_OK && fWatchingPackageNode) {
		watch_node(&fPackageNodeRef, B_STOP_WATCHING, this);
		fWatchingPackageNode = false;
	}
}


/**
 * @brief Confirms quit while tasks run, then persists the window frame.
 *
 * If background tasks are still running the user is prompted with a Stop
 * and quit / Cancel alert; cancelling aborts the close. On confirm the
 * window frame is saved and the application is told to quit.
 *
 * @return True to allow the window to close, false to keep it open.
 */
bool
RepositoriesWindow::QuitRequested()
{
	if (fView->IsTaskRunning()) {
		BAlert *alert = new BAlert("tasks",
			B_TRANSLATE_COMMENT("Some tasks are still running. Stop these "
				"tasks and quit?", "Application quit alert message"),
			B_TRANSLATE_COMMENT("Stop and quit", "Button label"),
			kCancelLabel, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		int32 result = alert->Go();
		if (result != 0)
			return false;
	}
	fSettings.SetFrame(Frame());
	be_app->PostMessage(B_QUIT_REQUESTED);
	return BWindow::QuitRequested();
}


/**
 * @brief Routes preflet messages, including node-monitor notifications.
 *
 * Handles the Add-by-URL dialog lifecycle, forwards Delete key presses to
 * the inner view as REMOVE_REPOS, and treats outside changes to the
 * package-repositories directory as a cue to refresh the list.
 *
 * @param message Incoming BMessage.
 */
void
RepositoriesWindow::MessageReceived(BMessage* message)
{
	switch (message->what)
	{
		case ADD_REPO_WINDOW:
		{
			BRect frame = Frame();
			fAddWindow = new AddRepoWindow(frame, fMessenger);
			break;
		}
		
		case ADD_REPO_URL:
		{
			BString url;
			status_t result = message->FindString(key_url, &url);
			if (result == B_OK)
				fView->AddManualRepository(url);
			break;
		}
		
		case ADD_WINDOW_CLOSED:
		{
			fAddWindow = NULL;
			break;
		}
		
		case DELETE_KEY_PRESSED:
		{
			BMessage message(REMOVE_REPOS);
			fView->MessageReceived(&message);
			break;
		}
		
		// captures pkgman changes while the Repositories application is running
		case B_NODE_MONITOR:
		{
			// This preflet is making the changes, so ignore this message
			if (fView->IsTaskRunning())
				break;

			int32 opcode;
			if (message->FindInt32("opcode", &opcode) == B_OK) {
				switch (opcode)
				{
					case B_ATTR_CHANGED:
					case B_ENTRY_CREATED:
					case B_ENTRY_REMOVED:
					{
						PostMessage(UPDATE_LIST, fView);
						break;
					}
				}
			}
			break;
		}
		
		default:
			BWindow::MessageReceived(message);
	}
}
