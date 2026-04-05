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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2011, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold (bonefish@users.sf.net)
 */

/** @file InitTerminateLibBe.cpp
 *  @brief Global libbe initialization and termination routines.
 *
 *  Contains the library entry points that are called during image loading and
 *  unloading to initialize and clean up core application kit subsystems such as
 *  BMessage, BRoster, and the locale kit. Also registers a fork handler to
 *  reinitialize state in child processes.
 */

//!	Global library initialization/termination routines.


#include <stdio.h>
#include <stdlib.h>

#include <Application.h>
#include <AppMisc.h>
#include <LooperList.h>
#include <MessagePrivate.h>
#include <RosterPrivate.h>
#include <TokenSpace.h>


extern void __initialize_locale_kit();


// debugging
//#define DBG(x) x
#define DBG(x)
#define OUT	printf


/** @brief Reinitializes libbe state in a forked child process.
 *
 *  Called via atfork() after a fork. Reinitializes BMessage static data,
 *  the global looper list, and team information. Invalidates be_app since
 *  continuing to use BApplication after fork is not supported.
 */
static void
initialize_forked_child()
{
	DBG(OUT("initialize_forked_child()\n"));

	BMessage::Private::StaticReInitForkedChild();
	BPrivate::gLooperList.InitAfterFork();
	BPrivate::init_team_after_fork();

	// Continuing to use BApplication after forking is not supported.
	if (be_app != NULL)
		be_app = (BApplication*)-1;

	DBG(OUT("initialize_forked_child() done\n"));
}


/** @brief Early library initialization, called before global constructors.
 *
 *  Initializes BMessage static data, creates the global be_roster, and
 *  registers the fork handler for child process reinitialization.
 *
 *  @param image_id The image ID of the loaded library (unused).
 */
extern "C" void
initialize_before(image_id)
{
	DBG(OUT("initialize_before()\n"));

	BMessage::Private::StaticInit();
	BRoster::Private::InitBeRoster();

	atfork(initialize_forked_child);

	DBG(OUT("initialize_before() done\n"));
}


/** @brief Late library initialization, called after global constructors.
 *
 *  Initializes the locale kit after all global constructors have run.
 *
 *  @param image_id The image ID of the loaded library (unused).
 */
extern "C" void
initialize_after(image_id)
{
	DBG(OUT("initialize_after()\n"));

	__initialize_locale_kit();

	DBG(OUT("initialize_after() done\n"));
}


/** @brief Library termination, called after global destructors.
 *
 *  Cleans up the global be_roster, BMessage static data, and the message
 *  cache. Triggers a debugger break if exit() is called after a fork from
 *  a BApplication process.
 *
 *  @param image_id The image ID of the unloading library (unused).
 */
extern "C" void
terminate_after(image_id)
{
	DBG(OUT("terminate_after()\n"));

	if (be_app == (BApplication*)-1)
		debugger("exit() called after fork from a BApplication");

	BRoster::Private::DeleteBeRoster();
	BMessage::Private::StaticCleanup();
	BMessage::Private::StaticCacheCleanup();

	DBG(OUT("terminate_after() done\n"));
}
