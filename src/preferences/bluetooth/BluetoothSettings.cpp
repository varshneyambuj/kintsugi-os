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
 *   Copyright 2008-2009, Oliver Ruiz Dorantes,
 *       <oliver.ruiz.dorantes@gmail.com>
 *   Copyright 2012-2013, Tri-Edge AI <triedgeai@gmail.com>
 *   Copyright 2021, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Fredrik Modéen <fredrik_at_modeen.se>
 */


/**
 * @file BluetoothSettings.cpp
 * @brief Implementation of BluetoothSettings, on-disk preference storage.
 *
 * Wraps a SettingsMessage that persists per-user Bluetooth preferences
 * under B_USER_SETTINGS_DIRECTORY/Bluetooth_settings: the preferred
 * LocalDevice address, the local device class, the inbound connection
 * policy, and the default inquiry duration in seconds.
 */


#include "BluetoothSettings.h"

#include <SettingsMessage.h>


/**
 * @brief Constructs a BluetoothSettings object with safe defaults.
 *
 * Opens (or creates) the per-user Bluetooth_settings file via
 * SettingsMessage and seeds in-memory defaults: NULL picked device, empty
 * device class, policy 0, 15 second inquiry time. Call LoadSettings() to
 * overlay any persisted values.
 */
BluetoothSettings::BluetoothSettings()
	:
	fSettingsMessage(B_USER_SETTINGS_DIRECTORY, "Bluetooth_settings")
{
	fCurrentSettings.pickeddevice = bdaddrUtils::NullAddress();
	fCurrentSettings.localdeviceclass = DeviceClass();
	fCurrentSettings.policy = 0;
	fCurrentSettings.inquirytime = 15;
}


/**
 * @brief Records the BD_ADDR of the user's preferred local device.
 *
 * @param pickeddevice  New picked-device address.
 */
void
BluetoothSettings::SetPickedDevice(bdaddr_t pickeddevice)
{
	fCurrentSettings.pickeddevice = pickeddevice;
}


/**
 * @brief Records the class-of-device the local adapter advertises.
 *
 * @param localdeviceclass  New DeviceClass to advertise.
 */
void
BluetoothSettings::SetLocalDeviceClass(DeviceClass localdeviceclass)
{
	fCurrentSettings.localdeviceclass = localdeviceclass;
}


/**
 * @brief Records the inbound-connection policy code.
 *
 * @param policy  Encoded policy value as used by the Bluetooth settings
 *                view (1 = all, 2 = trusted only, 3 = always ask).
 */
void
BluetoothSettings::SetPolicy(int32 policy)
{
	fCurrentSettings.policy = policy;
}


/**
 * @brief Records the default device-inquiry duration.
 *
 * @param inquirytime  Inquiry length in seconds.
 */
void
BluetoothSettings::SetInquiryTime(int32 inquirytime)
{
	fCurrentSettings.inquirytime = inquirytime;
}


/**
 * @brief Loads the persisted preferences into the in-memory cache.
 *
 * Reads BDAddress, DeviceClass, Policy, and InquiryTime fields from the
 * underlying SettingsMessage. Missing fields fall back to the defaults
 * established by the constructor.
 */
void
BluetoothSettings::LoadSettings()
{
	bdaddr_t* addr;
	ssize_t size;
	status_t status = fSettingsMessage.FindData("BDAddress", B_RAW_TYPE,
		(const void**)&addr, &size);
	if (status == B_OK)
		SetPickedDevice(*addr);
	else
		SetPickedDevice(bdaddrUtils::NullAddress());

	DeviceClass* devclass;
	status = fSettingsMessage.FindData("DeviceClass", B_RAW_TYPE,
		(const void**)&devclass, &size);
	if (status == B_OK)
		SetLocalDeviceClass(*devclass);
	else
		SetLocalDeviceClass(DeviceClass());

	SetPolicy(fSettingsMessage.GetValue("Policy", (int32)0));
	SetInquiryTime(fSettingsMessage.GetValue("InquiryTime", (int32)15));
}


/**
 * @brief Writes the in-memory preferences back to disk.
 *
 * Stores DeviceClass, BDAddress, Policy, and InquiryTime into the
 * SettingsMessage and asks it to flush to the user settings directory.
 */
void
BluetoothSettings::SaveSettings()
{
	fSettingsMessage.SetValue("DeviceClass", B_RAW_TYPE,
		&fCurrentSettings.localdeviceclass, sizeof(DeviceClass));
	fSettingsMessage.SetValue("BDAddress", B_RAW_TYPE, &fCurrentSettings.pickeddevice,
		sizeof(bdaddr_t));
	fSettingsMessage.SetValue("Policy", fCurrentSettings.policy);
	fSettingsMessage.SetValue("InquiryTime", fCurrentSettings.inquirytime);

	fSettingsMessage.Save();
}
