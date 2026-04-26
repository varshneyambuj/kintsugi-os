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
 * @file BluetoothWindow.cpp
 * @brief Implementation of BluetoothWindow, the top-level preference window.
 *
 * BluetoothWindow hosts the BTabView containing the remote-devices tab and
 * the local-device settings tab, exposes a Server menu to start, stop, and
 * refresh bluetooth_server, and surfaces the standard Defaults and Revert
 * preference buttons.
 *
 * @see BluetoothSettingsView, RemoteDevicesView
 */


#include "BluetoothWindow.h"
#include "RemoteDevicesView.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Roster.h>
#include <SeparatorView.h>
#include <TabView.h>

#include <stdio.h>

#include <bluetooth/LocalDevice.h>

#include "defs.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Window"

/** @brief Window message: revert all settings to their defaults. */
static const uint32 kMsgSetDefaults = 'dflt';
/** @brief Window message: revert all settings to their on-entry values. */
static const uint32 kMsgRevert = 'rvrt';

/** @brief Server menu message: start the bluetooth_server process. */
static const uint32 kMsgStartServices = 'SrSR';
/** @brief Server menu message: stop the bluetooth_server process. */
static const uint32 kMsgStopServices = 'StST';

/** @brief Currently active LocalDevice shared between settings views. */
LocalDevice* ActiveLocalDevice = NULL;


/**
 * @brief Constructs the Bluetooth preference window.
 *
 * Builds the Defaults and Revert buttons, the Server and Help menu bars,
 * and the BTabView containing the remote devices and settings tabs. Lays
 * everything out vertically with a separator above the action buttons.
 *
 * @param frame  Initial screen rectangle for the window.
 */
BluetoothWindow::BluetoothWindow(BRect frame)
	:
	BWindow(frame, B_TRANSLATE_SYSTEM_NAME("Bluetooth"), B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
	fDefaultsButton = new BButton("defaults", B_TRANSLATE("Defaults"),
		new BMessage(kMsgSetDefaults), B_WILL_DRAW);
	fDefaultsButton->SetEnabled(false);

	fRevertButton = new BButton("revert", B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert), B_WILL_DRAW);
	fRevertButton->SetEnabled(false);

	// Add the menu bar
	fMenubar = new BMenuBar(Bounds(), "menu_bar");

	// Add File menu to menu bar
	BMenu* menu = new BMenu(B_TRANSLATE("Server"));
	menu->AddItem(new BMenuItem(
		B_TRANSLATE("Start bluetooth services" B_UTF8_ELLIPSIS),
		new BMessage(kMsgStartServices), 0));
	menu->AddItem(new BMenuItem(
		B_TRANSLATE("Stop bluetooth services" B_UTF8_ELLIPSIS),
		new BMessage(kMsgStopServices), 0));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(
		B_TRANSLATE("Refresh local devices" B_UTF8_ELLIPSIS),
		new BMessage(kMsgRefresh), 0));
	fMenubar->AddItem(menu);

	menu = new BMenu(B_TRANSLATE("Help"));
	menu->AddItem(new BMenuItem(B_TRANSLATE("About Bluetooth" B_UTF8_ELLIPSIS),
		new BMessage(B_ABOUT_REQUESTED), 0));
	fMenubar->AddItem(menu);

	BTabView* tabView = new BTabView("tabview", B_WIDTH_FROM_LABEL);
	tabView->SetBorder(B_NO_BORDER);

	fSettingsView = new BluetoothSettingsView(B_TRANSLATE("Settings"));
	fRemoteDevices = new RemoteDevicesView(
		B_TRANSLATE("Remote devices"), B_WILL_DRAW);

	tabView->AddTab(fRemoteDevices);
	tabView->AddTab(fSettingsView);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0)
		.Add(fMenubar)
		.AddStrut(B_USE_HALF_ITEM_SPACING)
		.Add(tabView)
		.AddStrut(B_USE_HALF_ITEM_SPACING)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL)
			.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
				B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING)
			.Add(fDefaultsButton)
			.Add(fRevertButton)
			.AddGlue()
		.End()
	.End();
}


/**
 * @brief Routes window-level messages to the appropriate child view.
 *
 * Connection-policy and device-class changes are forwarded to the settings
 * view; remote-list additions are forwarded to the remote devices tab; the
 * Server menu items start or stop bluetooth_server via BRoster and
 * BMessenger respectively.
 *
 * @param message  Incoming BMessage. Unhandled messages fall through to
 *                 BWindow::MessageReceived.
 */
void
BluetoothWindow::MessageReceived(BMessage* message)
{
	//message->PrintToStream();
	switch (message->what) {
		case kMsgSetConnectionPolicy:
		case kMsgSetDeviceClass:
			fSettingsView->MessageReceived(message);
		break;

		case kMsgSetDefaults:
/*			fColorsView -> MessageReceived(new BMessage(DEFAULT_SETTINGS));
			fAntialiasingSettings->SetDefaults();
			fDefaultsButton->SetEnabled(false);
			fRevertButton->SetEnabled(true);
*/			break;

		case kMsgRevert:
/*			fColorsView -> MessageReceived(new BMessage(REVERT_SETTINGS));
			fAntialiasingSettings->Revert();
			fDefaultsButton->SetEnabled(fColorsView->IsDefaultable()
								|| fAntialiasingSettings->IsDefaultable());
			fRevertButton->SetEnabled(false);
*/			break;

		case kMsgStartServices:
			if (!be_roster->IsRunning(BLUETOOTH_SIGNATURE)) {
				status_t error = be_roster->Launch(BLUETOOTH_SIGNATURE);
				printf("kMsgStartServices: %s\n", strerror(error));
			}
			break;
		case kMsgStopServices:
			if (be_roster->IsRunning(BLUETOOTH_SIGNATURE)) {
				status_t error = BMessenger(BLUETOOTH_SIGNATURE).SendMessage(B_QUIT_REQUESTED);
				printf("kMsgStopServices: %s\n", strerror(error));
			}
			break;

		case kMsgAddToRemoteList:
			PostMessage(message, fRemoteDevices);
			break;
		case kMsgRefresh:
			fSettingsView->MessageReceived(message);
			break;
		case B_ABOUT_REQUESTED:
			be_app->PostMessage(message);
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


/**
 * @brief Asks the application to quit when the window is closed.
 *
 * Forwards a B_QUIT_REQUESTED to be_app so closing the only window also
 * terminates the application.
 *
 * @return Always returns true to allow the close to proceed.
 */
bool
BluetoothWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
