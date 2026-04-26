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
 * Tri-Edge AI; 2021, Haiku, Inc. Original author: Fredrik Modéen.
 */

/** @file BluetoothSettings.h
    @brief Declares BluetoothSettings, the on-disk Bluetooth preference store. */

#ifndef BLUETOOTH_SETTINGS_H
#define BLUETOOTH_SETTINGS_H

#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/LocalDevice.h>

#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <SettingsMessage.h>


/**
 * @brief Persistent preferences for the Bluetooth preflet.
 *
 * Holds the picked LocalDevice address, the advertised device class, the
 * inbound connection policy, and the default inquiry duration. Backed by
 * a SettingsMessage in the user settings directory; LoadSettings() and
 * SaveSettings() drive the I/O.
 */
class BluetoothSettings
{
public:
							BluetoothSettings();

			/** @brief Returns the BD_ADDR of the user's preferred local device. */
			bdaddr_t		PickedDevice() const
								{ return fCurrentSettings.pickeddevice; }
			/** @brief Returns the advertised local device class. */
			DeviceClass		LocalDeviceClass() const
								{ return fCurrentSettings.localdeviceclass; }
			/** @brief Returns the inbound-connection policy code. */
			int32			Policy() const
								{ return fCurrentSettings.policy; }
			/** @brief Returns the configured inquiry duration in seconds. */
			int32			InquiryTime() const
								{ return fCurrentSettings.inquirytime; }

			void			SetPickedDevice(bdaddr_t pickeddevice);
			void			SetLocalDeviceClass(DeviceClass localdeviceclass);
			void			SetPolicy(int32 policy);
			void			SetInquiryTime(int32 inquirytime);

			void			LoadSettings();
			void			SaveSettings();

private:
			struct BTSetting {
				bdaddr_t pickeddevice;
				DeviceClass localdeviceclass;
				int32 policy;
				int32 inquirytime;
			};

			SettingsMessage		fSettingsMessage;

			BTSetting			fCurrentSettings;
};

#endif // BLUETOOTH_SETTINGS_H
