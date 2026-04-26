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
 *       Jonas Sundström, jonas@kirilla.com
 */


/**
 * @file DeskbarPreferences.cpp
 * @brief Tiny launcher stub that opens the Deskbar's preferences window.
 *
 * This standalone executable lives under the preferences folder so that the
 * Deskbar preferences entry can be invoked like any other preference panel.
 * It simply asks the Deskbar (via the roster) to display its built-in
 * configuration UI.
 */


#include "PreferencesWindow.h"

#include <Application.h>
#include <Catalog.h>
#include <Roster.h>


/**
 * @brief Program entry point; asks the Deskbar to show its preferences.
 *
 * Constructs a transient BApplication so the launcher participates in the
 * messaging system, then sends a kConfigShow message to the Deskbar via
 * be_roster->Launch().
 *
 * @param argc Number of command-line arguments (unused).
 * @param argv Command-line argument vector (unused).
 * @return Zero on a clean shutdown.
 */
int
main(int argc, char **argv)
{
	B_TRANSLATE_MARK_SYSTEM_NAME_VOID("Deskbar");
	BApplication app("application/x-vnd.Haiku-DeskbarPreferences");
	be_roster->Launch("application/x-vnd.Be-TSKB", new BMessage(kConfigShow));
	return 0;
}

