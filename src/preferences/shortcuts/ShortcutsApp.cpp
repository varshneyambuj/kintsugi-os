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
 *       Fredrik Modéen
 */


/**
 * @file ShortcutsApp.cpp
 * @brief Implementation of the Shortcuts preferences BApplication.
 *
 * Defines the BApplication subclass whose ReadyToRun() handler instantiates
 * and shows the main ShortcutsWindow.
 */


#include "ShortcutsApp.h"

#include <Catalog.h>

#include "ShortcutsWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ShortcutsApp"


/**
 * @brief Constructs the application with its registered MIME signature.
 */
ShortcutsApp::ShortcutsApp()
	:
	BApplication("application/x-vnd.Haiku-Shortcuts")
{
}


/**
 * @brief Creates and shows the main shortcuts preferences window.
 *
 * Called once by the BApplication framework after the message loop is up.
 * The window owns its own lifetime; this method simply hands it off.
 */
void
ShortcutsApp::ReadyToRun()
{
	ShortcutsWindow* window = new ShortcutsWindow();
	window->Show();
}


/**
 * @brief Destructor; no per-instance teardown is required.
 */
ShortcutsApp::~ShortcutsApp()
{
}
