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
 * MIT License. Copyright 2001-2015, Haiku, Inc.
 * Original authors: DarkWyrm <bpmagic@columbus.rr.com>,
 *                   Axel Dörfler, axeld@pinc-software.de.
 */

/** @file AppServer.h
    @brief Top-level application server class that manages desktop instances. */

#ifndef	APP_SERVER_H
#define	APP_SERVER_H


#include <Application.h>
#include <List.h>
#include <Locker.h>
#include <ObjectList.h>
#include <OS.h>
#include <String.h>
#include <Window.h>

#include "MessageLooper.h"
#include "ServerConfig.h"


#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#	include <Server.h>
#	define SERVER_BASE BServer
#else
#	include "TestServerLoopAdapter.h"
#	define SERVER_BASE TestServerLoopAdapter
#endif


class ServerApp;
class BitmapManager;
class Desktop;


/** @brief The central application server object that owns all Desktop instances
           and dispatches incoming client messages. */
class AppServer : public SERVER_BASE {
public:
								AppServer(status_t* status);
	virtual						~AppServer();

	/** @brief Handles an incoming BMessage directed at the app server.
	    @param message The message to process. */
	virtual	void				MessageReceived(BMessage* message);

	/** @brief Called when a quit is requested; returns true to allow quitting.
	    @return true if the server should quit. */
	virtual	bool				QuitRequested();

private:
			/** @brief Creates a new Desktop for the given user and target screen.
			    @param userID UID of the user that owns the desktop.
			    @param targetScreen Name of the target screen, or NULL for default.
			    @return Pointer to the newly created Desktop. */
			Desktop*			_CreateDesktop(uid_t userID,
									const char* targetScreen);

	/** @brief Locates an existing Desktop for the given user and target screen.
	    @param userID UID of the user.
	    @param targetScreen Name of the target screen, or NULL.
	    @return Pointer to the matching Desktop, or NULL if not found. */
	virtual	Desktop*			_FindDesktop(uid_t userID,
									const char* targetScreen);

			/** @brief Starts the input server helper process. */
			void				_LaunchInputServer();

private:
			BObjectList<Desktop> fDesktops;
			BLocker				fDesktopLock;
};


/** @brief Global BitmapManager instance shared across the app server. */
extern BitmapManager *gBitmapManager;

/** @brief Port on which the app server listens for new client connections. */
extern port_id gAppServerPort;


#endif	/* APP_SERVER_H */
