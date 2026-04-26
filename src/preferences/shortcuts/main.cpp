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
 *   Copyright 1999-2009 Jeremy Friesner
 *   Copyright 2009-2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Jeremy Friesner
 */


/**
 * @file main.cpp
 * @brief Program entry point for the Shortcuts preference application.
 *
 * Initializes the global key-index lookup tables used by ShortcutsSpec, then
 * constructs and runs the BApplication subclass that drives the preference
 * window.
 */


#include <Catalog.h>
#include <Locale.h>

#include "KeyInfos.h"
#include "ShortcutsApp.h"


/**
 * @brief Program entry point.
 *
 * Initializes the keycode-to-name table and starts the BApplication message
 * loop that hosts the Shortcuts preference window.
 *
 * @param argc Number of command-line arguments (unused).
 * @param argv Command-line argument vector (unused).
 * @return Zero when the application loop has exited cleanly.
 */
int
main(int argc, char** argv)
{
	InitKeyIndices();
	ShortcutsApp app;

	app.Run();
}
