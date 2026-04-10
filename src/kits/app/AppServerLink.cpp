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
 *   Copyright 2001-2005 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Erik Jaesler (erik@cgsoftware.com)
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file AppServerLink.cpp
 *  @brief BPrivate::AppServerLink implementation for app_server communication.
 *
 *  Provides a scoped, RAII-style proxy for accessing the application's
 *  connection with the app_server. Acquiring an AppServerLink locks the
 *  shared connection; destroying it releases the lock.
 */

#include <Application.h>

#include <ApplicationPrivate.h>
#include <AppServerLink.h>

#include <locks.h>


/**	AppServerLink provides proxied access to the application's
 *	connection with the app_server.
 *	It has BAutolock semantics:
 *	creating one locks the app_server connection; destroying one
 *	unlocks the connection.
 */


/** @brief Recursive lock protecting the shared app_server connection. */
static recursive_lock sLock = RECURSIVE_LOCK_INITIALIZER("AppServerLink_sLock");


namespace BPrivate {

/** @brief Constructs an AppServerLink, locking the app_server connection.
 *
 *  Acquires the recursive lock on the shared connection and sets up the
 *  sender and receiver from the current BApplication's server link.
 *  If no be_app exists, invokes the debugger with an error message.
 */
AppServerLink::AppServerLink()
{
	recursive_lock_lock(&sLock);

	// if there is no be_app, we can't do a whole lot, anyway
	if (be_app != NULL) {
		fReceiver = &BApplication::Private::ServerLink()->Receiver();
		fSender = &BApplication::Private::ServerLink()->Sender();
	} else {
		debugger("You need to have a valid app_server connection first!");
	}
}


/** @brief Destroys the AppServerLink, unlocking the app_server connection. */
AppServerLink::~AppServerLink()
{
	recursive_lock_unlock(&sLock);
}

}	// namespace BPrivate
