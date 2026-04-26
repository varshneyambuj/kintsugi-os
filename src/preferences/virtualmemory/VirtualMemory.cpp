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
 *   Copyright 2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file VirtualMemory.cpp
 * @brief Implementation of the VirtualMemory preference BApplication.
 *
 * Hosts the swap configuration UI: spawns a SettingsWindow on launch and
 * provides an About dialog. The application persists no state of its own;
 * configuration lives in the Settings model and the kernel driver
 * settings file written by SettingsWindow.
 *
 * @see SettingsWindow, Settings
 */


#include "VirtualMemory.h"
#include "SettingsWindow.h"

#include <Alert.h>
#include <Catalog.h>
#include <TextView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "VirtualMemoryApp"


/**
 * @brief Constructs the BApplication with the VirtualMemory MIME signature.
 */
VirtualMemory::VirtualMemory()
	: BApplication("application/x-vnd.Haiku-VirtualMemory")
{
}


/**
 * @brief Destroys the VirtualMemory application instance.
 */
VirtualMemory::~VirtualMemory()
{
}


/**
 * @brief Creates and shows the SettingsWindow once the app loop is running.
 *
 * Invoked by BApplication after the initial @c B_READY_TO_RUN message is
 * dispatched. The window owns its own lifetime once shown.
 */
void
VirtualMemory::ReadyToRun()
{
	BWindow* window = new SettingsWindow();
	window->Show();
}


/**
 * @brief Handles the standard About menu request by displaying a styled BAlert.
 *
 * Builds a short credits dialog whose first line is rendered in a larger
 * bold font via direct BTextView styling. The alert auto-closes on Escape.
 */
void
VirtualMemory::AboutRequested()
{
	BAlert* alert = new BAlert("about", B_TRANSLATE("VirtualMemory\n"
		"\twritten by Axel Dörfler\n"
		"\tCopyright 2005, Haiku.\n"), B_TRANSLATE("OK"));
	BTextView* view = alert->TextView();
	BFont font;

	view->SetStylable(true);

	view->GetFont(&font);
	font.SetSize(18);
	font.SetFace(B_BOLD_FACE);
	view->SetFontAndColor(0, 13, &font);

	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


/**
 * @brief Process entry point: creates the VirtualMemory app and runs its loop.
 *
 * @param argc Standard argument count (unused).
 * @param argv Standard argument vector (unused).
 * @return Always zero; BApplication::Run() blocks until the app quits.
 */
int
main(int argc, char** argv)
{
	VirtualMemory app;
	app.Run();

	return 0;
}
