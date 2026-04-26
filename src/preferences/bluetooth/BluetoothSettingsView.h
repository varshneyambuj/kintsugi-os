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
 * MIT License. Copyright 2008-2009, Oliver Ruiz Dorantes; 2012-2013,
 * Tri-Edge AI.
 */

/** @file BluetoothSettingsView.h
    @brief Declares BluetoothSettingsView, the local-device settings tab. */

#ifndef BLUETOOTH_SETTINGS_VIEW_H
#define BLUETOOTH_SETTINGS_VIEW_H

#include "BluetoothSettings.h"

#include <View.h>

class BluetoothSettings;
class ExtendedLocalDeviceView;

class BBox;
class BMenuField;
class BPopUpMenu;
class BSlider;
class BOptionPopUp;


/**
 * @brief BView containing the local-adapter preferences tab.
 *
 * Lays out the connection-policy menu, the device-class identity menu,
 * the inquiry-time slider, the local-device picker, and the embedded
 * ExtendedLocalDeviceView. Persists changes through a BluetoothSettings
 * member that loads on construction and saves on destruction.
 */
class BluetoothSettingsView : public BView {
public:
								BluetoothSettingsView(const char* name);
	virtual						~BluetoothSettingsView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);


private:
			void				_BuildLocalDevicesMenu();
			bool				_SetDeviceClass(uint8 major, uint8 minor,
									uint16 service);
			void				_MarkLocalDevice(LocalDevice* lDevice);
			int					_GetClassForMenu();

protected:
			BluetoothSettings	fSettings;

			float				fDivider;

			BOptionPopUp*		fPolicyMenu;
			BOptionPopUp*		fClassMenu;
			BMenuField*			fLocalDevicesMenuField;
			BPopUpMenu*			fLocalDevicesMenu;

			ExtendedLocalDeviceView* 	fExtDeviceView;

			BSlider*			fInquiryTimeControl;

};

#endif // BLUETOOTH_SETTINGS_VIEW_H
