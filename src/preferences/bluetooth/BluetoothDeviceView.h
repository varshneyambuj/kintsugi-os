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

/** @file BluetoothDeviceView.h
    @brief Declares BluetoothDeviceView, a read-only summary BView for a BluetoothDevice. */

#ifndef BLUETOOTHDEVICEVIEW_H_
#define BLUETOOTHDEVICEVIEW_H_

#include <Box.h>
#include <Bitmap.h>
#include <Invoker.h>
#include <Message.h>
#include <View.h>

#include <bluetooth/BluetoothDevice.h>


class BStringView;
class BitmapView;


/**
 * @brief BView that displays the static properties of a BluetoothDevice.
 *
 * Renders friendly name, BD_ADDR, device class, HCI/LMP versions,
 * manufacturer, and ACL/SCO buffer parameters as a row of BStringViews
 * arranged next to a class-of-device icon.
 */
class BluetoothDeviceView : public BView
{
public:
	BluetoothDeviceView(BluetoothDevice* bDevice,
		uint32 flags = B_WILL_DRAW);
	~BluetoothDeviceView();

			void SetBluetoothDevice(BluetoothDevice* bDevice);

	virtual void MessageReceived(BMessage* message);
	virtual void SetTarget(BHandler* target);
	virtual void SetEnabled(bool value);

protected:
	BluetoothDevice*	fDevice;

	BStringView*		fName;
	BStringView*		fBdaddr;
	BStringView*		fClassService;
	BStringView*		fClass;

	BStringView*		fHCIVersionProperties;
	BStringView*		fLMPVersionProperties;
	BStringView*		fManufacturerProperties;

	BStringView*		fACLBuffersProperties;
	BStringView*		fSCOBuffersProperties;

	BView*				fIcon;
};


#endif
