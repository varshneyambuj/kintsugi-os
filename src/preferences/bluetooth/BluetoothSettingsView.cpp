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
 *   Copyright 2008-2009, Oliver Ruiz Dorantes
 *       <oliver.ruiz.dorantes@gmail.com>
 *   Copyright 2012-2013, Tri-Edge AI, <triedgeai@gmail.com>
 *   Copyright 2021, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Fredrik Modéen <fredrik_at_modeen.se>
 */


/**
 * @file BluetoothSettingsView.cpp
 * @brief Implementation of BluetoothSettingsView, the local-device tab.
 *
 * BluetoothSettingsView hosts the controls that govern the local Bluetooth
 * adapter: the inbound connection policy, the device-class identity, the
 * default inquiry duration, the picked LocalDevice, and the embedded
 * ExtendedLocalDeviceView showing live discoverable/visibility toggles.
 *
 * @see BluetoothSettings, ExtendedLocalDeviceView
 */


#include "BluetoothSettingsView.h"

#include "defs.h"
#include "BluetoothSettings.h"
#include "BluetoothWindow.h"
#include "ExtendedLocalDeviceView.h"

#include <bluetooth/LocalDevice.h>

#include <Box.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <OptionPopUp.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <String.h>
#include <TextView.h>

#include <stdio.h>
#include <stdlib.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Settings view"

/** @brief Connection-policy label: accept connections from every device. */
static const char* kAllLabel = B_TRANSLATE_MARK("From all devices");
/** @brief Connection-policy label: only accept from trusted devices. */
static const char* kTrustedLabel =
	B_TRANSLATE_MARK("Only from trusted devices");
/** @brief Connection-policy label: prompt the user on every connection. */
static const char* kAlwaysLabel = B_TRANSLATE_MARK("Always ask");

/** @brief Device-class label for a desktop computer. */
static const char* kDesktopLabel = B_TRANSLATE_MARK("Desktop");
/** @brief Device-class label for a server. */
static const char* kServerLabel = B_TRANSLATE_MARK("Server");
/** @brief Device-class label for a laptop. */
static const char* kLaptopLabel = B_TRANSLATE_MARK("Laptop");
/** @brief Device-class label for a handheld computer. */
static const char* kHandheldLabel = B_TRANSLATE_MARK("Handheld");
/** @brief Device-class label for a smart phone. */
static const char* kPhoneLabel = B_TRANSLATE_MARK("Smart phone");

//	#pragma mark -


/**
 * @brief Constructs the local-device settings view.
 *
 * Loads persisted preferences, builds the policy and device-class option
 * pop-ups, the inquiry-time slider, the local-device picker, and embeds
 * an ExtendedLocalDeviceView. The view is wired to the active LocalDevice
 * if one is already selected.
 *
 * @param name  BView name passed up to BView's constructor.
 */
BluetoothSettingsView::BluetoothSettingsView(const char* name)
	:
	BView(name, 0),
	fLocalDevicesMenu(NULL)
{
	fSettings.LoadSettings();

	fPolicyMenu = new BOptionPopUp("policy",
		B_TRANSLATE("Incoming connections policy:"),
		new BMessage(kMsgSetConnectionPolicy));
	fPolicyMenu->AddOption(B_TRANSLATE_NOCOLLECT(kAllLabel), 1);
	fPolicyMenu->AddOption(B_TRANSLATE_NOCOLLECT(kTrustedLabel), 2);
	fPolicyMenu->AddOption(B_TRANSLATE_NOCOLLECT(kAlwaysLabel), 3);

	fPolicyMenu->SetValue(fSettings.Policy());

	BString label(B_TRANSLATE("Default inquiry time:"));
	label <<  " " << fSettings.InquiryTime();
	fInquiryTimeControl = new BSlider("time", label.String()
		, new BMessage(kMsgSetInquiryTime), 15, 61, B_HORIZONTAL);
	fInquiryTimeControl->SetLimitLabels(B_TRANSLATE("15 secs"),
		B_TRANSLATE("61 secs"));
	fInquiryTimeControl->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fInquiryTimeControl->SetHashMarkCount(20);
	fInquiryTimeControl->SetEnabled(true);
	fInquiryTimeControl->SetValue(fSettings.InquiryTime());

	fExtDeviceView = new ExtendedLocalDeviceView(NULL);

	// localdevices menu
	_BuildLocalDevicesMenu();
	fLocalDevicesMenuField = new BMenuField("devices",
		B_TRANSLATE("Local devices found on system:"),
		fLocalDevicesMenu);

	if (ActiveLocalDevice != NULL) {
		fExtDeviceView->SetLocalDevice(ActiveLocalDevice);
		fExtDeviceView->SetEnabled(true);

		DeviceClass rememberedClass = ActiveLocalDevice->GetDeviceClass();
		if (!rememberedClass.IsUnknownDeviceClass())
			fSettings.SetLocalDeviceClass(rememberedClass);
	}

	fClassMenu = new BOptionPopUp("DeviceClass", B_TRANSLATE("Identify host as:"),
		new BMessage(kMsgSetDeviceClass));
	fClassMenu->AddOption(B_TRANSLATE_NOCOLLECT(kDesktopLabel), 1);
	fClassMenu->AddOption(B_TRANSLATE_NOCOLLECT(kServerLabel), 2);
	fClassMenu->AddOption(B_TRANSLATE_NOCOLLECT(kLaptopLabel), 3);
	fClassMenu->AddOption(B_TRANSLATE_NOCOLLECT(kHandheldLabel), 4);
	fClassMenu->AddOption(B_TRANSLATE_NOCOLLECT(kPhoneLabel), 5);

	fClassMenu->SetValue(_GetClassForMenu());

	BLayoutBuilder::Grid<>(this, 0)
		.SetInsets(10)
		.Add(fClassMenu, 0, 0)
		.Add(fPolicyMenu, 0, 1)

		.Add(fInquiryTimeControl, 0, 2, 2)

		.Add(fLocalDevicesMenuField->CreateLabelLayoutItem(), 0, 5)
		.Add(fLocalDevicesMenuField->CreateMenuBarLayoutItem(), 1, 5)

		.Add(fExtDeviceView, 0, 6, 2)
	.End();
}


/**
 * @brief Destroys the view and persists current preferences.
 *
 * Calls BluetoothSettings::SaveSettings() so user changes are flushed to
 * disk when the preference window closes.
 */
BluetoothSettingsView::~BluetoothSettingsView()
{
	fSettings.SaveSettings();
}


/**
 * @brief Hooks up message targets after the view joins a window.
 *
 * Adopts the parent's view color, points the local-devices menu items
 * and the inquiry-time slider at this view as their target.
 */
void
BluetoothSettingsView::AttachedToWindow()
{
	if (Parent() != NULL)
		SetViewColor(Parent()->ViewColor());
	else
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fLocalDevicesMenu->SetTargetForItems(this);
	fInquiryTimeControl->SetTarget(this);
}


/**
 * @brief Routes user-driven preference changes into BluetoothSettings.
 *
 * Handles local-device switching, connection policy changes, inquiry-time
 * slider updates, device-class menu changes, and refresh requests that
 * rebuild the local-device pop-up from the kit.
 *
 * @param message  Incoming BMessage. Falls through to BView::MessageReceived
 *                 for anything not recognised here.
 */
void
BluetoothSettingsView::MessageReceived(BMessage* message)
{
	//message->PrintToStream();
	switch (message->what) {

		case kMsgLocalSwitched:
		{
			LocalDevice* lDevice;

			if (message->FindPointer("LocalDevice",
				(void**)&lDevice) == B_OK) {

				_MarkLocalDevice(lDevice);
			}

			break;
		}

		case kMsgSetConnectionPolicy:
		{
			int32 policy;
			if (message->FindInt32("be:value", (int32*)&policy) == B_OK) {
				fSettings.SetPolicy(policy);
			}
			break;
		}

		case kMsgSetInquiryTime:
		{
			fSettings.SetInquiryTime(fInquiryTimeControl->Value());
			BString label(B_TRANSLATE("Default inquiry time:"));
			label <<  " " << fInquiryTimeControl->Value();
			fInquiryTimeControl->SetLabel(label.String());
			break;
		}

		case kMsgSetDeviceClass:
		{
			int32 deviceClass;
			if (message->FindInt32("be:value",
				(int32*)&deviceClass) == B_OK) {

				if (deviceClass == 5)
					_SetDeviceClass(2, 3, 0x72);
				else
					_SetDeviceClass(1, deviceClass, 0x72);
			}

			break;
		}
		case kMsgRefresh:
		{
			_BuildLocalDevicesMenu();
			fLocalDevicesMenu->SetTargetForItems(this);

			break;
		}
		default:
			BView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Applies a class-of-device tuple to the active LocalDevice.
 *
 * Stores the new DeviceClass into the in-memory settings and pushes it to
 * the active LocalDevice when one exists.
 *
 * @param major    Major device-class code.
 * @param minor    Minor device-class code.
 * @param service  Service-class bitmask.
 * @return true if the class was pushed to a live LocalDevice, false if no
 *         LocalDevice is currently selected.
 */
bool
BluetoothSettingsView::_SetDeviceClass(uint8 major, uint8 minor,
	uint16 service)
{
	bool haveRun = true;

	fSettings.SetLocalDeviceClass(DeviceClass(major, minor, service));

	if (ActiveLocalDevice != NULL)
		ActiveLocalDevice->SetDeviceClass(fSettings.LocalDeviceClass());
	else
		haveRun = false;

	return haveRun;
}


/**
 * @brief Rebuilds the local-devices BPopUpMenu from the Bluetooth kit.
 *
 * Lazily constructs the BPopUpMenu, removes any existing items, then
 * walks LocalDevice::GetLocalDevice() up to GetLocalDeviceCount() to add a
 * BMenuItem per discovered adapter. The item matching the persisted
 * picked device is marked, and ActiveLocalDevice is updated accordingly.
 */
void
BluetoothSettingsView::_BuildLocalDevicesMenu()
{
	LocalDevice* lDevice;

	if (!fLocalDevicesMenu)
		fLocalDevicesMenu = new BPopUpMenu(B_TRANSLATE("Pick device"
			B_UTF8_ELLIPSIS));

	while (fLocalDevicesMenu->CountItems() > 0) {
		BMenuItem* item = fLocalDevicesMenu->RemoveItem((int32)0);

		if (item != NULL) {
			delete item;
		}
	}

	ActiveLocalDevice = NULL;

	for (uint32 i = 0; i < LocalDevice::GetLocalDeviceCount(); i++) {
		lDevice = LocalDevice::GetLocalDevice();

		if (lDevice != NULL) {
			BMessage* message = new BMessage(kMsgLocalSwitched);
			message->AddPointer("LocalDevice", lDevice);

			BMenuItem* item = new BMenuItem(
				(lDevice->GetFriendlyName().String()), message);

			if (bdaddrUtils::Compare(lDevice->GetBluetoothAddress(),
				fSettings.PickedDevice())) {

				item->SetMarked(true);
				ActiveLocalDevice = lDevice;
			}

			fLocalDevicesMenu->AddItem(item);
		}
	}
}


/**
 * @brief Records the chosen LocalDevice as active and refreshes the UI.
 *
 * Skips devices with the null address. Otherwise points the embedded
 * ExtendedLocalDeviceView at @a lDevice, enables it, marks the device as
 * the global ActiveLocalDevice, and persists the choice.
 *
 * @param lDevice  The LocalDevice the user just picked from the pop-up.
 */
void
BluetoothSettingsView::_MarkLocalDevice(LocalDevice* lDevice)
{
	if (bdaddrUtils::Compare(lDevice->GetBluetoothAddress(), BDADDR_NULL))
		return;

	fExtDeviceView->SetLocalDevice(lDevice);
	fExtDeviceView->SetEnabled(true);
	ActiveLocalDevice = lDevice;
	fSettings.SetPickedDevice(lDevice->GetBluetoothAddress());
}


/**
 * @brief Maps the saved DeviceClass back to the device-class menu index.
 *
 * Because the menu only exposes a small subset of major/minor pairs, this
 * helper performs the inverse mapping used by SetDeviceClass(): a phone
 * (major 2, minor 3) maps to entry 5, and major-1 minors 1-4 map directly.
 *
 * @return Menu option value to mark in the device-class BOptionPopUp.
 */
int
BluetoothSettingsView::_GetClassForMenu()
{
	int deviceClass =
			fSettings.LocalDeviceClass().MajorDeviceClass()+
			fSettings.LocalDeviceClass().MinorDeviceClass();

	// As of now we only support MajorDeviceClass = 1 and MinorDeviceClass 1-4
	// and MajorDeviceClass = 2 and MinorDeviceClass 3.
	if (fSettings.LocalDeviceClass().MajorDeviceClass() == 1
			&& (fSettings.LocalDeviceClass().MinorDeviceClass() > 0
			&& fSettings.LocalDeviceClass().MinorDeviceClass() < 5))
		deviceClass -= 1;

	return deviceClass;
}
