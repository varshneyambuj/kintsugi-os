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
 *   Copyright 2002-2009 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Jerome Duval, jerome.duval@free.fr
 */


/**
 * @file Backgrounds.cpp
 * @brief Entry point and top-level window of the Backgrounds preference app.
 *
 * Defines BackgroundsApplication and BackgroundsWindow, the BApplication
 * and BWindow subclasses that host the BackgroundsView. Color drops on
 * the desktop are forwarded into the window so the picker can pick them
 * up without an additional roundtrip.
 *
 * @see BackgroundsView
 */


#include <Application.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <TrackerAddOnAppLaunch.h>
#include <Window.h>

#include "BackgroundsView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Main Window"


/** @brief MIME signature for the Backgrounds preference application. */
static const char* kSignature = "application/x-vnd.Haiku-Backgrounds";


/**
 * @brief BWindow subclass that holds the BackgroundsView.
 *
 * Saves layout settings on close and forwards workspace activation
 * events down to the view so per-workspace bitmaps can update.
 */
class BackgroundsWindow : public BWindow {
public:
							BackgroundsWindow();

			void			RefsReceived(BMessage* message);

protected:
	virtual	bool			QuitRequested();
	virtual	void			WorkspaceActivated(int32 oldWorkspaces,
								bool active);

			BackgroundsView*	fBackgroundsView;
};


/**
 * @brief BApplication subclass that owns the BackgroundsWindow.
 *
 * Handles silent relaunches and forwards file-ref drops from Tracker
 * down to the window.
 */
class BackgroundsApplication : public BApplication {
public:
							BackgroundsApplication();
	virtual	void			MessageReceived(BMessage* message);
	virtual	void			RefsReceived(BMessage* message);

private:
			BackgroundsWindow*	fWindow;
};


//	#pragma mark - BackgroundsApplication


/**
 * @brief Constructs the application and shows its main window.
 */
BackgroundsApplication::BackgroundsApplication()
	:
	BApplication(kSignature),
	fWindow(NULL)
{
	fWindow = new BackgroundsWindow();
	fWindow->Show();
}


/**
 * @brief Forwards desktop color drops and silent-relaunch messages.
 *
 * Color drops are forwarded to the window so the picker can adopt the
 * dropped color; B_SILENT_RELAUNCH is mapped to a window activation.
 *
 * @param message The incoming BMessage.
 */
void
BackgroundsApplication::MessageReceived(BMessage* message)
{
	const void *data;
	ssize_t size;

	if (message->WasDropped() && message->FindData("RGBColor", B_RGB_COLOR_TYPE,
			&data, &size) == B_OK) {
		// This is the desktop telling us that it was changed by a color drop
		BMessenger(fWindow).SendMessage(message);
		return;
	}
	switch (message->what) {
		case B_SILENT_RELAUNCH:
			fWindow->Activate();
			break;
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


/**
 * @brief Forwards a file-ref drop to the window.
 *
 * Tracker uses this to send images opened with the Backgrounds preflet.
 *
 * @param message Drop message containing one or more "refs" entries.
 */
void
BackgroundsApplication::RefsReceived(BMessage* message)
{
	fWindow->RefsReceived(message);
}


//	#pragma mark - BackgroundsWindow


/**
 * @brief Constructs the Backgrounds window with an auto-sized BackgroundsView.
 *
 * Centers the window on the screen unless the view restored a saved
 * position from disk.
 */
BackgroundsWindow::BackgroundsWindow()
	:
	BWindow(BRect(0, 0, 0, 0), B_TRANSLATE_SYSTEM_NAME("Backgrounds"),
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS,
		B_ALL_WORKSPACES)
{
	fBackgroundsView = new BackgroundsView();

	BLayoutBuilder::Group<>(this)
		.AddGroup(B_HORIZONTAL, 0)
			.Add(fBackgroundsView)
			.End()
		.End();

	if (!fBackgroundsView->FoundPositionSetting())
		CenterOnScreen();
}


/**
 * @brief Forwards a ref drop into the BackgroundsView and brings it to front.
 *
 * @param message Drop message containing one or more "refs" entries.
 */
void
BackgroundsWindow::RefsReceived(BMessage* message)
{
	fBackgroundsView->RefsReceived(message);
	Activate();
}


/**
 * @brief Saves view settings and asks the application to quit.
 *
 * @return Always @c true so the framework closes the window.
 */
bool
BackgroundsWindow::QuitRequested()
{
	fBackgroundsView->SaveSettings();
	be_app->PostMessage(B_QUIT_REQUESTED);

	return true;
}


/**
 * @brief Forwards workspace-activation notifications to the view.
 *
 * @param oldWorkspaces Bitmask of the workspaces being deactivated.
 * @param active        @c true when entering a new workspace.
 */
void
BackgroundsWindow::WorkspaceActivated(int32 oldWorkspaces, bool active)
{
	fBackgroundsView->WorkspaceActivated(oldWorkspaces, active);
}


//	#pragma mark - main method


/**
 * @brief Process entry point for the Backgrounds preference application.
 *
 * @return Always zero on normal termination.
 */
int
main(int argc, char** argv)
{
	BackgroundsApplication app;
	app.Run();
	return 0;
}
