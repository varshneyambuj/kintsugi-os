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
 * @file RemoteDevicesView.cpp
 * @brief Implementation of RemoteDevicesView, the remote-devices tab.
 *
 * RemoteDevicesView is the BView the user spends most time with. It hosts
 * the BListView of paired/known remote devices and the action buttons
 * (Add, Remove, Pair, Disconnect). Add launches an InquiryPanel; the
 * other actions operate on the currently selected DeviceListItem.
 */


#include <stdio.h>

#include <Alert.h>
#include <Catalog.h>
#include <Messenger.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Path.h>

#include <LayoutBuilder.h>
#include <SpaceLayoutItem.h>

#include <PincodeWindow.h>
#include <bluetooth/RemoteDevice.h>

#include "BluetoothWindow.h"
#include "defs.h"
#include "DeviceListItem.h"
#include "InquiryPanel.h"
#include "RemoteDevicesView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Remote devices"

/** @brief Internal message: open the inquiry panel to add new devices. */
static const uint32 kMsgAddDevices = 'ddDv';
/** @brief Internal message: remove the currently selected device. */
static const uint32 kMsgRemoveDevice = 'rmDv';
/** @brief Internal message: pair with the selected remote device. */
static const uint32 kMsgPairDevice = 'trDv';
/** @brief Internal message: disconnect from the selected remote device. */
static const uint32 kMsgDisconnectDevice = 'dsDv';
//static const uint32 kMsgBlockDevice = 'blDv';
//static const uint32 kMsgRefreshDevices = 'rfDv';

using namespace Bluetooth;


/**
 * @brief Constructs the remote-devices tab view.
 *
 * Builds the device list (single selection), its scroll wrapper, and the
 * Add/Remove/Pair/Disconnect buttons, and lays them side-by-side.
 *
 * @param name   BView name passed up to BView.
 * @param flags  BView creation flags.
 */
RemoteDevicesView::RemoteDevicesView(const char* name, uint32 flags)
 :	BView(name, flags)
{
	addButton = new BButton("add", B_TRANSLATE("Add" B_UTF8_ELLIPSIS),
		new BMessage(kMsgAddDevices));

	removeButton = new BButton("remove", B_TRANSLATE("Remove"),
		new BMessage(kMsgRemoveDevice));

	pairButton = new BButton("pair", B_TRANSLATE("Pair" B_UTF8_ELLIPSIS),
		new BMessage(kMsgPairDevice));

	disconnectButton = new BButton("disconnect", B_TRANSLATE("Disconnect"),
		new BMessage(kMsgDisconnectDevice));
	/*
		blockButton = new BButton("block", B_TRANSLATE("As blocked"),
			new BMessage(kMsgBlockDevice));

		//TODO:Here use GetFriendlyName(true)
		availButton = new BButton("check", B_TRANSLATE("Refresh" B_UTF8_ELLIPSIS),
			new BMessage(kMsgRefreshDevices));
	*/
	// Set up device list
	fDeviceList = new BListView("DeviceList", B_SINGLE_SELECTION_LIST);

	fScrollView = new BScrollView("ScrollView", fDeviceList, 0, false, true);

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, 10)
		.SetInsets(5)
		.Add(fScrollView)
		//.Add(BSpaceLayoutItem::CreateHorizontalStrut(5))
		.AddGroup(B_VERTICAL)
			.SetInsets(0, 15, 0, 15)
			.Add(addButton)
			.Add(removeButton)
			.AddGlue()
//			.Add(availButton)
	//		.AddGlue()
			.Add(pairButton)
			.Add(disconnectButton)
//			.Add(blockButton)
			.AddGlue()
		.End()
	.End();

	fDeviceList->SetSelectionMessage(NULL);
}


/**
 * @brief Destroys the view.
 */
RemoteDevicesView::~RemoteDevicesView(void)
{

}


/**
 * @brief Wires the buttons and list view to this view as the message target.
 *
 * Also calls LoadSettings() to populate the list and selects the first
 * entry.
 */
void
RemoteDevicesView::AttachedToWindow(void)
{
	fDeviceList->SetTarget(this);
	addButton->SetTarget(this);
	removeButton->SetTarget(this);
	pairButton->SetTarget(this);
	disconnectButton->SetTarget(this);
//	blockButton->SetTarget(this);
//	availButton->SetTarget(this);

	LoadSettings();
	fDeviceList->Select(0);
}


/**
 * @brief Handles button clicks and inter-window list updates.
 *
 * Add launches a new InquiryPanel against ActiveLocalDevice. Remove drops
 * the selected entry. Pair and Disconnect dispatch to the matching
 * RemoteDevice methods. Incoming kMsgAddToRemoteList messages from the
 * inquiry panel are appended (de-duplicating by BD_ADDR).
 *
 * @param message  Incoming BMessage. Unhandled messages fall through to
 *                 BView::MessageReceived.
 */
void
RemoteDevicesView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAddDevices:
		{
			InquiryPanel* inquiryPanel= new InquiryPanel(
				BRect(100, 100, 450, 450), ActiveLocalDevice);
			inquiryPanel->Show();
			break;
		}

		case kMsgRemoveDevice:
			fDeviceList->RemoveItem(fDeviceList->CurrentSelection(0));
			break;
		case kMsgAddToRemoteList:
		{
			DeviceListItem* device = NULL;
			message->FindPointer("device", (void**)&device);
			bool isDuplicate = false;

			// check the list for duplicates
			for (int32 i = 0; i < fDeviceList->CountItems(); i++) {
				DeviceListItem* existingDevice
					= static_cast<DeviceListItem*>(fDeviceList->ItemAt(i));

				if (DeviceListItem::Compare(&existingDevice, &device)) {
					isDuplicate = true;
					break;
				}
			}

			if (!isDuplicate) {
				fDeviceList->AddItem((BListItem*)device);
				fDeviceList->Invalidate();
			} else {
				delete device;
			}

			break;
		}

		case kMsgPairDevice:
		{
			DeviceListItem* device = static_cast<DeviceListItem*>(fDeviceList
				->ItemAt(fDeviceList->CurrentSelection(0)));
			if (device == NULL)
				break;

			RemoteDevice* remote = dynamic_cast<RemoteDevice*>(device->Device());
			if (remote == NULL)
				break;

			remote->Authenticate();

			break;
		}
		case kMsgDisconnectDevice:
		{
			DeviceListItem* device = static_cast<DeviceListItem*>(fDeviceList
				->ItemAt(fDeviceList->CurrentSelection(0)));
			if (device == NULL)
				break;

			RemoteDevice* remote = dynamic_cast<RemoteDevice*>(device->Device());
			if (remote == NULL)
				break;

			remote->Disconnect();

			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Loads the persisted list of remote devices.
 *
 * @note Currently a stub; the persistence backend is not yet implemented.
 */
void RemoteDevicesView::LoadSettings(void)
{

}


/**
 * @brief Indicates whether the tab can be reset to its defaults.
 *
 * @return Always true; the parent window decides whether to enable the
 *         Defaults button on this basis.
 */
bool RemoteDevicesView::IsDefaultable(void)
{
	return true;
}

