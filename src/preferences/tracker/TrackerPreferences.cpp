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
 *   Copyright 2009, Haiku Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Alexandre Deckner, alex@zappotek.com
 */


/**
 * @file TrackerPreferences.cpp
 * @brief Tiny launcher stub that opens the Tracker preferences window.
 *
 * Lives under the preferences folder so the Tracker preference panel can be
 * invoked like any other. It launches Tracker if needed, then sends a scripted
 * EXECUTE_PROPERTY("Preferences") message to bring up the dialog.
 */


#include <Application.h>
#include <Catalog.h>
#include <Roster.h>


/**
 * @brief Program entry point; asks Tracker to show its preferences window.
 *
 * Constructs a transient BApplication, ensures Tracker is running via the
 * roster, then dispatches a scripted "Preferences" EXECUTE_PROPERTY message
 * to the Tracker application.
 *
 * @param argc Number of command-line arguments (unused).
 * @param argv Command-line argument vector (unused).
 * @return Zero on a clean shutdown.
 */
int
main(int argc, char **argv)
{
	B_TRANSLATE_MARK_SYSTEM_NAME_VOID("Tracker");
	BApplication app("application/x-vnd.Haiku-TrackerPreferences");

	// launch Tracker if it's not running
	be_roster->Launch("application/x-vnd.Be-TRAK");

	BMessage message;
	message.what = B_EXECUTE_PROPERTY;
	message.AddSpecifier("Preferences");

	BMessenger("application/x-vnd.Be-TRAK").SendMessage(&message);

	return 0;
}
