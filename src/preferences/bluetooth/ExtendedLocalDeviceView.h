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

/** @file ExtendedLocalDeviceView.h
    @brief Declares ExtendedLocalDeviceView, a properties + live-toggles BView. */

#ifndef EXTENDEDLOCALDEVICEVIEW_H_
#define EXTENDEDLOCALDEVICEVIEW_H_

#include <View.h>
#include <Message.h>
#include <Invoker.h>
#include <Box.h>
#include <Bitmap.h>

#include <bluetooth/LocalDevice.h>

#include "BluetoothDeviceView.h"

class BStringView;
class BitmapView;
class BCheckBox;


/**
 * @brief BView combining device properties with live LocalDevice toggles.
 *
 * Wraps a BluetoothDeviceView (read-only properties) and three BCheckBox
 * controls for discoverable, visibility, and authentication state. Pushes
 * user changes directly to the bound LocalDevice.
 */
class ExtendedLocalDeviceView : public BView
{
public:
	ExtendedLocalDeviceView(LocalDevice* bDevice,
		uint32 flags = B_WILL_DRAW);
	~ExtendedLocalDeviceView();

	void SetLocalDevice(LocalDevice* lDevice);


	virtual void MessageReceived(BMessage* message);
	virtual void AttachedToWindow();
	virtual void SetTarget(BHandler* target);
	virtual void SetEnabled(bool value);
			void ClearDevice();

protected:
	LocalDevice*		fDevice;
	BCheckBox*			fAuthentication;
	BCheckBox*			fDiscoverable;
	BCheckBox*			fVisible;
	BluetoothDeviceView* fDeviceView;
	uint8 fScanMode;

};


#endif
