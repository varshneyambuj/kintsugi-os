/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2008-09, Oliver Ruiz Dorantes.
 */

/** @file BluetoothWindow.h
    @brief Declares BluetoothWindow, the top-level Bluetooth preference window. */

#ifndef BLUETOOTH_WINDOW_H
#define BLUETOOTH_WINDOW_H

#include "BluetoothSettingsView.h"

#include <Application.h>
#include <Button.h>
#include <Window.h>
#include <Message.h>
#include <TabView.h>

class BluetoothSettingsView;
class RemoteDevicesView;


/**
 * @brief Top-level BWindow that hosts the Bluetooth preferences UI.
 *
 * Combines a Server menu, a tabbed view (remote devices and local settings),
 * and Defaults/Revert buttons. Acts as the routing point for messages
 * coming from the application object and the contained views.
 */
class BluetoothWindow : public BWindow {
public:
			BluetoothWindow(BRect frame);
	bool	QuitRequested();
	void	MessageReceived(BMessage *message);

private:
			RemoteDevicesView*		fRemoteDevices;
			BButton*				fDefaultsButton;
			BButton*				fRevertButton;
			BMenuBar*				fMenubar;

			BluetoothSettingsView*	fSettingsView;
};

#endif
