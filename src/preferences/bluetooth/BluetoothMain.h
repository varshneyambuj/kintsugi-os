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

/** @file BluetoothMain.h
    @brief Declares BluetoothApplication, the BApplication for the Bluetooth preflet. */

#ifndef BLUETOOTH_MAIN_H
#define BLUETOOTH_MAIN_H

#include <Application.h>

class BluetoothWindow;


/**
 * @brief BApplication subclass that drives the Bluetooth preferences panel.
 *
 * Owns the singleton BluetoothWindow, gates window creation on the
 * presence of a running bluetooth_server, and provides the standard
 * About dialog.
 */
class BluetoothApplication : public BApplication
{
public:
				 BluetoothApplication();
	virtual void ReadyToRun();
	virtual void MessageReceived(BMessage*);
	virtual void AboutRequested();

private:
	BluetoothWindow*	fWindow;
};

#endif
