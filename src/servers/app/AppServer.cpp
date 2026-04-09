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
 *   Copyright 2001-2016, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Axel Dörfler, axeld@pinc-software.de
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Christian Packmann
 */

/** @file AppServer.cpp
    @brief Entry point and top-level manager for the Kintsugi OS application server. */


#include "AppServer.h"

#include <syslog.h>

#include <AutoDeleter.h>
#include <LaunchRoster.h>
#include <PortLink.h>
#include <RosterPrivate.h>

#include "BitmapManager.h"
#include "Desktop.h"
#include "GlobalFontManager.h"
#include "InputManager.h"
#include "ScreenManager.h"
#include "ServerProtocol.h"


//#define DEBUG_SERVER
#ifdef DEBUG_SERVER
#	include <stdio.h>
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif


// Globals
port_id gAppServerPort;
BTokenSpace gTokenSpace;
uint32 gAppServerSIMDFlags = 0;


/** @brief Constructs the AppServer, initialising all major subsystems.
 *
 * Loads default fonts, allocates global objects (InputManager, GlobalFontManager,
 * ScreenManager, BitmapManager), spawns housekeeping threads, and notifies the
 * registrar that the server has (re)started.
 *
 * @param status Output parameter set to B_OK on success or an error code on failure.
 */
AppServer::AppServer(status_t* status)
	:
	SERVER_BASE("application/x-vnd.Haiku-app_server", "picasso", -1, false,
		status),
	fDesktopLock("AppServerDesktopLock")
{
	openlog("app_server", 0, LOG_DAEMON);

	gInputManager = new InputManager();

	// Create the font server and scan the proper directories.
	gFontManager = new GlobalFontManager;
	if (gFontManager->InitCheck() != B_OK)
		debugger("font manager could not be initialized!");

	gFontManager->Run();

	gScreenManager = new ScreenManager();
	gScreenManager->Run();

	// Create the bitmap allocator. Object declared in BitmapManager.cpp
	gBitmapManager = new BitmapManager();

#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#if 0
	// This is not presently needed, as app_server is launched from the login session.
	// TODO: check the attached displays, and launch login session for them
	BMessage data;
	data.AddString("name", "app_server");
	data.AddInt32("session", 0);
	BLaunchRoster().Target("login", data);
#endif

	// Inform the registrar we've (re)started.
	BMessage request(kMsgAppServerStarted);
	BRoster::Private().SendTo(&request, NULL, false);
#endif
}


/** @brief Destructor — reached only in test mode when the server is asked to shut down.
 *
 * Deletes the bitmap manager, then requests quit on the screen and font managers
 * and closes the system log.
 */
AppServer::~AppServer()
{
	delete gBitmapManager;

	gScreenManager->Lock();
	gScreenManager->Quit();

	gFontManager->Lock();
	gFontManager->Quit();

	closelog();
}


/** @brief Handles incoming BMessages directed at the AppServer.
 *
 * Currently only processes AS_GET_DESKTOP, which locates or creates a Desktop
 * object for the requesting user and returns its message port. Unrecognised
 * messages are silently ignored (scripting is not supported).
 *
 * @param message The incoming BMessage to process.
 */
void
AppServer::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case AS_GET_DESKTOP:
		{
			Desktop* desktop = NULL;

			int32 userID = message->GetInt32("user", 0);
			int32 version = message->GetInt32("version", 0);
			const char* targetScreen = message->GetString("target");

			if (version != AS_PROTOCOL_VERSION) {
				syslog(LOG_ERR, "Application for user %" B_PRId32 " does not "
					"support the current server protocol (%" B_PRId32 ").\n",
					userID, version);
			} else {
				desktop = _FindDesktop(userID, targetScreen);
				if (desktop == NULL) {
					// we need to create a new desktop object for this user
					// TODO: test if the user exists on the system
					// TODO: maybe have a separate AS_START_DESKTOP_SESSION for
					// authorizing the user
					desktop = _CreateDesktop(userID, targetScreen);
				}
			}

			BMessage reply;
			if (desktop != NULL)
				reply.AddInt32("port", desktop->MessagePort());
			else
				reply.what = (uint32)B_ERROR;

			message->SendReply(&reply);
			break;
		}

		default:
			// We don't allow application scripting
			STRACE(("AppServer received unexpected code %" B_PRId32 "\n",
				message->what));
			break;
	}
}


/** @brief Handles quit requests; in test mode tears down all desktops and exits.
 *
 * In normal (non-test) mode the server refuses to quit and returns false.
 *
 * @return false in production mode; does not return in test mode (calls exit()).
 */
bool
AppServer::QuitRequested()
{
#if TEST_MODE
	while (fDesktops.CountItems() > 0) {
		Desktop *desktop = fDesktops.RemoveItemAt(0);

		thread_id thread = desktop->Thread();
		desktop->PostMessage(B_QUIT_REQUESTED);

		// we just wait for the desktop to kill itself
		status_t status;
		wait_for_thread(thread, &status);
	}

	delete this;
	exit(0);

	return SERVER_BASE::QuitRequested();
#else
	return false;
#endif

}


/** @brief Creates and initialises a new Desktop for the given user.
 *
 * Allocates a Desktop object, calls Init() and Run() on it, and adds it to
 * the internal desktop list. If any step fails the desktop is destroyed and
 * NULL is returned.
 *
 * @param userID       POSIX user ID of the session owner.
 * @param targetScreen Optional target screen identifier string; may be NULL.
 * @return             Pointer to the new Desktop on success, or NULL on failure.
 */
Desktop*
AppServer::_CreateDesktop(uid_t userID, const char* targetScreen)
{
	BAutolock locker(fDesktopLock);
	ObjectDeleter<Desktop> desktop;
	try {
		desktop.SetTo(new Desktop(userID, targetScreen));

		status_t status = desktop->Init();
		if (status == B_OK)
			status = desktop->Run();
		if (status == B_OK && !fDesktops.AddItem(desktop.Get()))
			status = B_NO_MEMORY;

		if (status != B_OK) {
			syslog(LOG_ERR, "Cannot initialize Desktop object: %s\n",
				strerror(status));
			return NULL;
		}
	} catch (...) {
		// there is obviously no memory left
		return NULL;
	}

	return desktop.Detach();
}


/** @brief Looks up the Desktop belonging to a specific user and target screen.
 *
 * Iterates the list of active desktops under the desktop lock and returns the
 * first one whose user ID and target screen match the given values.
 *
 * @param userID       POSIX user ID to search for.
 * @param targetScreen Optional target screen string; NULL matches a NULL target.
 * @return             Pointer to the matching Desktop, or NULL if not found.
 */
Desktop*
AppServer::_FindDesktop(uid_t userID, const char* targetScreen)
{
	BAutolock locker(fDesktopLock);

	for (int32 i = 0; i < fDesktops.CountItems(); i++) {
		Desktop* desktop = fDesktops.ItemAt(i);

		if (desktop->UserID() == userID
			&& ((desktop->TargetScreen() == NULL && targetScreen == NULL)
				|| (desktop->TargetScreen() != NULL && targetScreen != NULL
					&& strcmp(desktop->TargetScreen(), targetScreen) == 0))) {
			return desktop;
		}
	}

	return NULL;
}


//	#pragma mark -


/** @brief Application entry point — creates the AppServer and runs its message loop.
 *
 * Seeds the random number generator, constructs an AppServer, and calls Run()
 * if initialisation succeeded.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return EXIT_SUCCESS if the server ran and exited cleanly, EXIT_FAILURE otherwise.
 */
int
main(int argc, char** argv)
{
	srand(real_time_clock_usecs());

	status_t status;
	AppServer* server = new AppServer(&status);
	if (status == B_OK)
		server->Run();

	return status == B_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
