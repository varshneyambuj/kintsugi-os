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
 *   Copyright 2001-2010, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Pfeiffer
 */


/**
 * @file Printers.cpp
 * @brief BApplication entry point for the Printers preference panel.
 *
 * Hosts the PrintersWindow and forwards print_server broadcast messages to
 * every open window so the UI can stay in sync with the active printer
 * list.
 *
 * @see PrintersWindow, ScreenSettings
 */


#include "Printers.h"

#include <Locale.h>

#include "pr_server.h"
#include "Messages.h"
#include "PrintersWindow.h"
#include "ScreenSettings.h"


/**
 * @brief Process entry point.
 *
 * Constructs the PrintersApp and runs the BApplication message loop.
 *
 * @return Always 0 once the application loop exits.
 */
int
main()
{
	PrintersApp app;
	app.Run();
	return 0;
}


/**
 * @brief Constructs the Printers BApplication using PRINTERS_SIGNATURE.
 */
PrintersApp::PrintersApp()
	: Inherited(PRINTERS_SIGNATURE)
{
}


/**
 * @brief Hook called when the application is ready for its first window.
 *
 * Loads the persisted screen settings and shows the main PrintersWindow.
 */
void
PrintersApp::ReadyToRun()
{
	PrintersWindow* win = new PrintersWindow(new ScreenSettings());
	win->Show();
}


/**
 * @brief Dispatches application-level BMessages.
 *
 * Re-broadcasts B_PRINTER_CHANGED and PRINTERS_ADD_PRINTER events to every
 * open window. PRINTERS_ADD_PRINTER is translated to the internal
 * kMsgAddPrinter constant.
 *
 * @param msg Incoming BMessage.
 */
void
PrintersApp::MessageReceived(BMessage* msg)
{
	if (msg->what == B_PRINTER_CHANGED || msg->what == PRINTERS_ADD_PRINTER) {
			// broadcast message
		uint32 what = msg->what;
		if (what == PRINTERS_ADD_PRINTER)
			what = kMsgAddPrinter;

		BWindow* w;
		for (int32 i = 0; (w = WindowAt(i)) != NULL; i++) {
			BMessenger msgr(NULL, w);
			msgr.SendMessage(what);
		}
	} else {
		BApplication::MessageReceived(msg);
	}
}


/**
 * @brief Handles command-line arguments passed to the application.
 *
 * Currently just iterates the args; a per-argument add-printer dialog is
 * planned but not yet implemented.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 *
 * @todo Pop up a pre-filled add printer dialog for each argument.
 */
void
PrintersApp::ArgvReceived(int32 argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		// TODO: show a pre-filled add printer dialog here
	}
}

