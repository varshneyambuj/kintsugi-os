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
 *   Copyright 2008-09, Oliver Ruiz Dorantes,
 *       <oliver.ruiz.dorantes_at_gmail.com>
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file BluetoothDeviceView.cpp
 * @brief Implementation of BluetoothDeviceView, a read-only summary view.
 *
 * BluetoothDeviceView is a BView that arranges a device icon next to a
 * vertical stack of BStringViews summarising name, BD_ADDR, class of
 * device, HCI/LMP version, manufacturer, and ACL/SCO buffer counts for a
 * given BluetoothDevice. It is reused by both local and remote device
 * detail panels.
 *
 * @see ExtendedLocalDeviceView, BluetoothDevice
 */


#include "BluetoothDeviceView.h"
#include <bluetooth/bdaddrUtils.h>

#include <bluetooth/LocalDevice.h>
#include <bluetooth/HCI/btHCI_command.h>


#include <Bitmap.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <TextView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Device View"


/**
 * @brief Constructs a BluetoothDeviceView wrapping the given device.
 *
 * Creates the labelled BStringViews for each device property, the icon
 * BView, and arranges them in a horizontal layout. SetBluetoothDevice()
 * is called to populate the labels from @a bDevice.
 *
 * @param bDevice  Device whose properties are displayed; may be NULL to
 *                 produce an empty placeholder view.
 * @param flags    Additional BView flags OR'd with B_WILL_DRAW.
 */
BluetoothDeviceView::BluetoothDeviceView(BluetoothDevice* bDevice, uint32 flags)
	:
	BView("BluetoothDeviceView", flags | B_WILL_DRAW),
	fDevice(bDevice)
{
	fName = new BStringView("name", "");
	fName->SetFont(be_bold_font);
	fName->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_MIDDLE));

	fBdaddr = new BStringView("bdaddr",
		bdaddrUtils::ToString(bdaddrUtils::NullAddress()));
	fBdaddr->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_MIDDLE));

	fClassService = new BStringView("ServiceClass",
		B_TRANSLATE("Service classes: "));
	fClassService->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_MIDDLE));

	fClass = new BStringView("class", "- / -");
	fClass->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_MIDDLE));

	fHCIVersionProperties = new BStringView("hci", "");
	fHCIVersionProperties->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_MIDDLE));
	fLMPVersionProperties = new BStringView("lmp", "");
	fLMPVersionProperties->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_MIDDLE));
	fManufacturerProperties = new BStringView("manufacturer", "");
	fManufacturerProperties->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_MIDDLE));
	fACLBuffersProperties = new BStringView("buffers acl", "");
	fACLBuffersProperties->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_MIDDLE));
	fSCOBuffersProperties = new BStringView("buffers sco", "");
	fSCOBuffersProperties->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_MIDDLE));

	fIcon = new BView(BRect(0, 0, 32 - 1, 32 - 1), "Icon", B_FOLLOW_ALL,
		B_WILL_DRAW);
	fIcon->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	SetBluetoothDevice(bDevice);

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, 5)
		.SetInsets(10)
		.Add(fIcon)
		.AddGroup(B_VERTICAL, 0)
			.SetInsets(5)
			.Add(fName)
			.Add(fBdaddr)
			.Add(fClass)
			.Add(fClassService)
			.Add(fHCIVersionProperties)
			.Add(fLMPVersionProperties)
			.Add(fManufacturerProperties)
			.Add(fACLBuffersProperties)
			.Add(fSCOBuffersProperties)
		.End()
		.AddGlue()
	.End();

}


/**
 * @brief Destroys the view.
 *
 * @note Child BStringViews and the icon BView are owned by the BView tree
 *       and are released when the parent is destroyed; this destructor
 *       does no extra work.
 */
BluetoothDeviceView::~BluetoothDeviceView()
{
}


/**
 * @brief Re-binds the view to a different BluetoothDevice and refreshes labels.
 *
 * Pulls friendly name, address, class of device, HCI/LMP versions,
 * manufacturer, and ACL/SCO buffer parameters from @a bDevice and writes
 * them into the corresponding BStringViews. The device class icon is
 * redrawn into the icon child view.
 *
 * @param bDevice  New device to display; if NULL the view is left as is.
 */
void
BluetoothDeviceView::SetBluetoothDevice(BluetoothDevice* bDevice)
{
	if (bDevice != NULL) {
		SetName(bDevice->GetFriendlyName().String());

		fName->SetText(bDevice->GetFriendlyName().String());
		fBdaddr->SetText(bdaddrUtils::ToString(bDevice->GetBluetoothAddress()));

		BString string(B_TRANSLATE("Service classes: "));
		bDevice->GetDeviceClass().GetServiceClass(string);
		fClassService->SetText(string.String());

		string = "";
		bDevice->GetDeviceClass().GetMajorDeviceClass(string);
		string << " / ";
		bDevice->GetDeviceClass().GetMinorDeviceClass(string);
		fClass->SetText(string.String());

		bDevice->GetDeviceClass().Draw(fIcon, BPoint(Bounds().left, Bounds().top));

		uint32 value;

		string = "";
		if (bDevice->GetProperty("hci_version", &value) == B_OK)
			string << "HCI ver: " << BluetoothHciVersion(value);
		if (bDevice->GetProperty("hci_revision", &value) == B_OK)
			string << " HCI rev: " << value ;

		fHCIVersionProperties->SetText(string.String());

		string = "";
		if (bDevice->GetProperty("lmp_version", &value) == B_OK)
			string << "LMP ver: " << BluetoothLmpVersion(value);
		if (bDevice->GetProperty("lmp_subversion", &value) == B_OK)
			string << " LMP subver: " << value;
		fLMPVersionProperties->SetText(string.String());

		string = "";
		if (bDevice->GetProperty("manufacturer", &value) == B_OK)
			string << B_TRANSLATE("Manufacturer: ")
			   	<< BluetoothManufacturer(value);
		fManufacturerProperties->SetText(string.String());

		string = "";
		if (bDevice->GetProperty("acl_mtu", &value) == B_OK)
			string << "ACL mtu: " << value;
		if (bDevice->GetProperty("acl_max_pkt", &value) == B_OK)
			string << B_TRANSLATE(" packets: ") << value;
		fACLBuffersProperties->SetText(string.String());

		string = "";
		if (bDevice->GetProperty("sco_mtu", &value) == B_OK)
			string << "SCO mtu: " << value;
		if (bDevice->GetProperty("sco_max_pkt", &value) == B_OK)
			string << B_TRANSLATE(" packets: ") << value;
		fSCOBuffersProperties->SetText(string.String());
	}
}


/**
 * @brief No-op target hook kept for BInvoker-style API compatibility.
 *
 * @param target  Ignored.
 */
void
BluetoothDeviceView::SetTarget(BHandler* target)
{
}


/**
 * @brief Handles messages delivered to the view.
 *
 * Currently only delegates to BView::MessageReceived after detecting a
 * dropped message; reserved for future drag-and-drop handling.
 *
 * @param message  Incoming BMessage.
 */
void
BluetoothDeviceView::MessageReceived(BMessage* message)
{
	// If we received a dropped message, try to see if it has color data
	// in it
	if (message->WasDropped()) {

	}

	// The default
	BView::MessageReceived(message);
}


/**
 * @brief Reserved hook for enabling or disabling the view.
 *
 * @param value  Ignored in the current implementation.
 */
void
BluetoothDeviceView::SetEnabled(bool value)
{
}
