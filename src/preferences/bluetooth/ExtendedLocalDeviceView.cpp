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
 *   Copyright 2021, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Fredrik Modéen <fredrik_at_modeen.se>
 */


/**
 * @file ExtendedLocalDeviceView.cpp
 * @brief Implementation of ExtendedLocalDeviceView.
 *
 * Combines a BluetoothDeviceView (read-only properties) with three
 * BCheckBox controls for the live discoverable, visibility, and
 * authentication settings of the active LocalDevice. User toggles are
 * pushed to the LocalDevice immediately.
 *
 * @see BluetoothDeviceView, LocalDevice
 */


#include "ExtendedLocalDeviceView.h"

#include <bluetooth/bdaddrUtils.h>

#include "defs.h"

#include <Bitmap.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Extended local device view"


/**
 * @brief Constructs an ExtendedLocalDeviceView for the given LocalDevice.
 *
 * Creates the inner BluetoothDeviceView and the three BCheckBoxes,
 * arranges them vertically, and disables them until SetLocalDevice() is
 * called with a non-NULL device.
 *
 * @param bDevice  Initial LocalDevice to display; may be NULL.
 * @param flags    Additional BView flags OR'd with B_WILL_DRAW.
 */
ExtendedLocalDeviceView::ExtendedLocalDeviceView(LocalDevice* bDevice,
	uint32 flags)
	:
	BView("ExtendedLocalDeviceView", flags | B_WILL_DRAW),
	fDevice(bDevice),
	fScanMode(0)
{
	fDeviceView = new BluetoothDeviceView(bDevice);

	fDiscoverable = new BCheckBox("Discoverable",
		B_TRANSLATE("Discoverable"), new BMessage(SET_DISCOVERABLE));
	fVisible = new BCheckBox("Visible",
		B_TRANSLATE("Show name"), new BMessage(SET_VISIBLE));
	fAuthentication = new BCheckBox("Authenticate",
		B_TRANSLATE("Authenticate"), new BMessage(SET_AUTHENTICATION));
	fAuthentication->SetEnabled(false);

	SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(5)
		.Add(fDeviceView)
		.AddGroup(B_HORIZONTAL, 0)
			.SetInsets(5)
			.Add(fDiscoverable)
			.Add(fVisible)
			.Add(fAuthentication)
		.End()
	.End();
}


/**
 * @brief Destroys the view.
 */
ExtendedLocalDeviceView::~ExtendedLocalDeviceView()
{
}


/**
 * @brief Re-binds the view to a different LocalDevice.
 *
 * Updates the BluetoothDeviceView with the new device, resets the check
 * box state with ClearDevice(), then reads the live discoverable/visible
 * mode (1 = discoverable, 2 = visible name, 3 = both) back into the
 * checkboxes.
 *
 * @param lDevice  New LocalDevice; if NULL the view is left untouched.
 */
void
ExtendedLocalDeviceView::SetLocalDevice(LocalDevice* lDevice)
{
	if (lDevice != NULL) {
		fDevice = lDevice;
		SetName(lDevice->GetFriendlyName().String());
		fDeviceView->SetBluetoothDevice(lDevice);

		ClearDevice();

		int value = fDevice->GetDiscoverable();
		if (value == 1)
			fDiscoverable->SetValue(true);
		else if (value == 2)
			fVisible->SetValue(true);
		else if  (value == 3) {
			fDiscoverable->SetValue(true);
			fVisible->SetValue(true);
		}
#if 0
//		TODO implement GetAuthentication in LocalDevice
		if (fDevice->GetAuthentication())
			fAuthentication->SetValue(true);
#endif
	}
}


/**
 * @brief Routes check-box changes back to this view once attached.
 */
void
ExtendedLocalDeviceView::AttachedToWindow()
{
	fDiscoverable->SetTarget(this);
	fVisible->SetTarget(this);
	fAuthentication->SetTarget(this);
}


/**
 * @brief Diagnostic hook printing a notice when the target changes.
 *
 * @param target  Ignored.
 */
void
ExtendedLocalDeviceView::SetTarget(BHandler* target)
{
	printf("ExtendedLocalDeviceView::SetTarget\n");
}


/**
 * @brief Translates check-box clicks into LocalDevice configuration calls.
 *
 * Combines the discoverable and visible check-box states into the
 * scan-mode value the controller expects (0 = neither, 1 = discoverable,
 * 2 = visible, 3 = both) and pushes it through SetDiscoverable. The
 * authentication toggle is forwarded directly.
 *
 * @param message  Incoming BMessage. Unhandled cases fall through to
 *                 BView::MessageReceived. If no LocalDevice is currently
 *                 bound the message is ignored.
 */
void
ExtendedLocalDeviceView::MessageReceived(BMessage* message)
{
	if (fDevice == NULL) {
		printf("ExtendedLocalDeviceView::Device missing\n");
		BView::MessageReceived(message);
		return;
	}

	if (message->WasDropped()) {

	}

	switch (message->what)
	{
		case SET_DISCOVERABLE:
		case SET_VISIBLE:
			fScanMode = 0;

			if (fDiscoverable->Value())
				fScanMode = 1;

			if (fVisible->Value())
				fScanMode = 2;

			if (fVisible->Value() && fDiscoverable->Value())
				fScanMode = 3;

			if (fDevice != NULL)
				fDevice->SetDiscoverable(fScanMode);

			break;
		case SET_AUTHENTICATION:
			if (fDevice != NULL)
				fDevice->SetAuthentication(fAuthentication->Value());
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Enables or disables every interactive control in the view.
 *
 * @param value  true to enable, false to disable the three check boxes.
 */
void
ExtendedLocalDeviceView::SetEnabled(bool value)
{
	fVisible->SetEnabled(value);
	fAuthentication->SetEnabled(value);
	fDiscoverable->SetEnabled(value);
}


/**
 * @brief Resets every check box to its unchecked state.
 */
void
ExtendedLocalDeviceView::ClearDevice()
{
	fVisible->SetValue(false);
	fAuthentication->SetValue(false);
	fDiscoverable->SetValue(false);
}
