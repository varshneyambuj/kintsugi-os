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
 *   Copyright 2008-10, Oliver Ruiz Dorantes,
 *       <oliver.ruiz.dorantes_at_gmail.com>
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file BluetoothMain.cpp
 * @brief Implementation of the Bluetooth preferences BApplication.
 *
 * Hosts the BluetoothApplication entry point that drives the Bluetooth
 * preference panel. The application waits for the system bluetooth_server
 * to be running, optionally launches it, and then opens the main
 * BluetoothWindow. Routes a small set of inter-window messages and shows
 * the About dialog.
 *
 * @see BluetoothWindow, BluetoothApplication
 */


#include <stdio.h>

#include <Alert.h>
#include <Catalog.h>
#include <MessageRunner.h>
#include <Roster.h>
#include <private/interface/AboutWindow.h>

#include "BluetoothMain.h"
#include "BluetoothWindow.h"
#include "defs.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "main"


/**
 * @brief Constructs the Bluetooth preference application.
 *
 * Registers the application with the BApplication runtime using the
 * Bluetooth preference panel's MIME signature. The main window is not
 * yet created; ReadyToRun() defers that until the bluetooth_server is
 * confirmed to be running.
 */
BluetoothApplication::BluetoothApplication()
	:
	BApplication(BLUETOOTH_APP_SIGNATURE)
{
}


/**
 * @brief Verifies the bluetooth_server is running before opening the UI.
 *
 * If the bluetooth_server is not currently running, prompts the user with
 * an alert offering to launch it. On launch the application schedules a
 * deferred 'Xtmp' message via BMessageRunner to retry creating the main
 * window once the server has had time to start. If the server is already
 * running the window is created immediately by posting 'Xtmp' to self.
 *
 * @note Quits the application if the user declines to launch the server.
 */
void
BluetoothApplication::ReadyToRun()
{
	if (!be_roster->IsRunning(BLUETOOTH_SIGNATURE)) {
		BAlert* alert = new BAlert("Services not running",
			B_TRANSLATE("The Bluetooth services are not currently running "
				"on this system."),
			B_TRANSLATE("Launch now"), B_TRANSLATE("Quit"), "",
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(1, B_ESCAPE);
		int32 choice = alert->Go();

		switch (choice) {
			case 0:
			{
				status_t error;
				error = be_roster->Launch(BLUETOOTH_SIGNATURE);
				printf("kMsgStartServices: %s\n", strerror(error));
				// TODO: This is temporal
				// BMessage handcheck: use the version of Launch()
				// that includes a BMessage	in that message include
				// a BMessenger to yourself and the BT server could
				// use that messenger to send back a reply indicating
				// when it's ready and you could just create window
				BMessageRunner::StartSending(be_app_messenger,
					new BMessage('Xtmp'), 2 * 1000000, 1);
				break;
			}
			case 1:
				PostMessage(B_QUIT_REQUESTED);
				break;
		}

		return;
	}

	PostMessage(new BMessage('Xtmp'));
}


/**
 * @brief Dispatches application-level messages.
 *
 * Forwards remote-list updates to the main window and handles the deferred
 * 'Xtmp' bootstrap message that creates the BluetoothWindow once the
 * bluetooth_server is confirmed running.
 *
 * @param message  Incoming BMessage. Unhandled messages fall through to
 *                 BApplication::MessageReceived.
 */
void
BluetoothApplication::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAddToRemoteList:
			fWindow->PostMessage(message);
			break;

		case 'Xtmp':
			if (!be_roster->IsRunning(BLUETOOTH_SIGNATURE)) {
				// Give another chance
				BMessageRunner::StartSending(be_app_messenger,
					new BMessage('Xtmp'), 2 * 1000000, 1);
			} else {
				fWindow = new BluetoothWindow(BRect(100, 100, 750, 420));
				fWindow->Show();
			}
			break;

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


/**
 * @brief Shows the About window for the Bluetooth preference panel.
 *
 * Creates a BAboutWindow populated with copyright and contributor
 * acknowledgements derived from the original Haiku Bluetooth preflet.
 */
void
BluetoothApplication::AboutRequested()
{
	BAboutWindow* about = new BAboutWindow(B_TRANSLATE_SYSTEM_NAME("Bluetooth"),
		BLUETOOTH_APP_SIGNATURE);
	about->AddCopyright(2010, "Oliver Ruiz Dorantes");
	about->AddText(B_TRANSLATE(
		"With support of:\n"
		" - Mika Lindqvist\n"
		" - Adrien Destugues\n"
		" - Maksym Yevmenkin\n\n"
		"Thanks to the individuals who helped" B_UTF8_ELLIPSIS "\n\n"
		"Shipping/donating hardware:\n"
		" - Henry Jair Abril Florez (el Colombian)\n"
		"	& Stefanie Bartolich\n"
		" - Edwin Erik Amsler\n"
		" - Dennis d'Entremont\n"
		" - Luroh\n"
		" - Pieter Panman\n\n"
		"Economically:\n"
		" - Karl vom Dorff, Andrea Bernardi (OSDrawer),\n"
		" - Matt M, Doug F, Hubert H,\n"
		" - Sebastian B, Andrew M, Jared E,\n"
		" - Frederik H, Tom S, Ferry B,\n"
		" - Greg G, David F, Richard S, Martin W:\n\n"
		"With patches:\n"
		" - Michael Weirauch\n"
		" - Fredrik Ekdahl\n"
		" - Raynald Lesieur\n"
		" - Andreas Färber\n"
		" - Joerg Meyer\n"
		"Testing:\n"
		" - Petter H. Juliussen\n"
		"Who gave me all the knowledge:\n"
		" - the yellowTAB team"));
	about->Show();
}


/**
 * @brief Process entry point for the Bluetooth preference panel.
 *
 * Instantiates the BluetoothApplication and runs its message loop until
 * the user closes the panel.
 *
 * @return Always returns 0 once the application loop terminates.
 */
int
main(int, char**)
{
	BluetoothApplication app;
	app.Run();

	return 0;
}
