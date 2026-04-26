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
 *   Copyright 2004-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Alexander von Gluck, kallisti5@unixzen.com
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file InterfaceAddressView.cpp
 * @brief Implementation of InterfaceAddressView, the per-family network
 *        addressing editor used inside the Network preflet.
 *
 * For a single interface and address family (AF_INET or AF_INET6), this
 * view exposes a mode selector (DHCP/Automatic, Static, Disabled), the
 * address/netmask/gateway fields, and an Apply button. Settings are read
 * via BNetworkSettings, and changes are pushed back through the same
 * facility so net_server can reconfigure the live stack.
 *
 * @see IPAddressControl, NetworkWindow
 */


#include "InterfaceAddressView.h"

#include <stdio.h>

#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <MenuField.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <Size.h>
#include <StringView.h>
#include <TextControl.h>

#include "IPAddressControl.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "IntefaceAddressView"


/** @brief Mode message: configure address automatically (DHCP / RA). */
const uint32 kModeAuto = 'iato';
/** @brief Mode message: configure address from user-entered static values. */
const uint32 kModeStatic = 'istc';
/** @brief Mode message: disable this address family on the interface. */
const uint32 kModeDisabled = 'ioff';
/** @brief Apply-button message: push the current field values to settings. */
const uint32 kMsgApply = 'aply';


// #pragma mark - InterfaceAddressView


/**
 * @brief Builds the view, populates the mode pop-up, and snapshots the
 *        current interface settings for Revert().
 *
 * @param family     Address family edited by this view (AF_INET or AF_INET6).
 * @param interface  Name of the interface to edit.
 * @param settings   Live network settings backing the editor.
 */
InterfaceAddressView::InterfaceAddressView(int family,
	const char* interface, BNetworkSettings& settings)
	:
	BGroupView(B_VERTICAL),
	fFamily(family),
	fInterface(interface),
	fSettings(settings)
{
	SetLayout(new BGroupLayout(B_VERTICAL));

	// Create our controls
	fModePopUpMenu = new BPopUpMenu("modes");

	if (fFamily == AF_INET) {
		fModePopUpMenu->AddItem(new BMenuItem(B_TRANSLATE("DHCP"),
			new BMessage(kModeAuto)));
	}

	if (fFamily == AF_INET6) {
		// Automatic can be DHCPv6 or Router Advertisements
		fModePopUpMenu->AddItem(new BMenuItem(B_TRANSLATE("Automatic"),
			new BMessage(kModeAuto)));
	}

	fModePopUpMenu->AddItem(new BMenuItem(B_TRANSLATE("Static"),
		new BMessage(kModeStatic)));
	fModePopUpMenu->AddSeparatorItem();
	fModePopUpMenu->AddItem(new BMenuItem(B_TRANSLATE("Disabled"),
		new BMessage(kModeDisabled)));

	fModeField = new BMenuField(B_TRANSLATE("Mode:"), fModePopUpMenu);
	fModeField->SetToolTip(
		B_TRANSLATE("The method for obtaining an IP address"));

	float minimumWidth = be_control_look->DefaultItemSpacing() * 15;

	fAddressField = new IPAddressControl(fFamily, B_TRANSLATE("IP address:"),
		NULL);
	fAddressField->SetToolTip(B_TRANSLATE("Your IP address"));
	fAddressField->TextView()->SetExplicitMinSize(
		BSize(minimumWidth, B_SIZE_UNSET));
	fAddressField->SetAllowEmpty(false);
	fNetmaskField = new IPAddressControl(fFamily, B_TRANSLATE("Netmask:"),
		NULL);
	fNetmaskField->SetToolTip(B_TRANSLATE(
		"The netmask defines your local network"));
	fNetmaskField->TextView()->SetExplicitMinSize(
		BSize(minimumWidth, B_SIZE_UNSET));
	fGatewayField = new IPAddressControl(fFamily, B_TRANSLATE("Gateway:"),
		NULL);
	fGatewayField->SetToolTip(B_TRANSLATE("Your gateway to the internet"));
	fGatewayField->TextView()->SetExplicitMinSize(
		BSize(minimumWidth, B_SIZE_UNSET));

	fApplyButton = new BButton("apply", B_TRANSLATE("Apply"),
		new BMessage(kMsgApply));

	fSettings.GetInterface(interface, fOriginalSettings);
	_UpdateFields();

	BLayoutBuilder::Group<>(this)
		.AddGrid()
			.AddMenuField(fModeField, 0, 0, B_ALIGN_RIGHT)
			.AddTextControl(fAddressField, 0, 1, B_ALIGN_RIGHT)
			.AddTextControl(fNetmaskField, 0, 2, B_ALIGN_RIGHT)
			.AddTextControl(fGatewayField, 0, 3, B_ALIGN_RIGHT)
		.End()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fApplyButton)
		.End()
		.AddGlue();
}


/**
 * @brief Destructor. Owned controls are deleted by the BView hierarchy.
 */
InterfaceAddressView::~InterfaceAddressView()
{
}


// #pragma mark - InterfaceAddressView virtual methods


/**
 * @brief Routes mode-menu and Apply-button messages to this view once
 *        the layout is attached to a window.
 */
void
InterfaceAddressView::AttachedToWindow()
{
	fModePopUpMenu->SetTargetForItems(this);
	fApplyButton->SetTarget(this);
}


/**
 * @brief Handles mode-change and Apply messages.
 *
 * Switching to a non-Static mode pushes the change immediately; switching
 * to Static only updates the field enable/disable state and waits for the
 * user to press Apply.
 *
 * @param message  Incoming BMessage; @c what selects the action.
 */
void
InterfaceAddressView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kModeAuto:
		case kModeStatic:
		case kModeDisabled:
			if (message->what == fLastMode)
				break;

			_SetModeField(message->what);
			if (message->what != kModeStatic)
				_UpdateSettings();
			break;

		case kMsgApply:
			_UpdateSettings();
			break;

		default:
			BView::MessageReceived(message);
	}
}


// #pragma mark - InterfaceAddressView public methods


/**
 * @brief Restores the interface to the settings captured at construction.
 *
 * @return Status from the underlying BNetworkSettings::AddInterface call.
 */
status_t
InterfaceAddressView::Revert()
{
	return fSettings.AddInterface(fOriginalSettings);
}


/**
 * @brief Reports whether the live settings still match the snapshot.
 *
 * @return true if the user has changed something Revert() could undo.
 */
bool
InterfaceAddressView::IsRevertable() const
{
	BMessage settings;
	fSettings.GetInterface(fInterface.Name(), settings);

	return !settings.HasSameData(fOriginalSettings);
}


/**
 * @brief Notifies the view that interface configuration changed externally.
 *
 * Ignores notifications for other interfaces.
 *
 * @param message  Configuration message; expected to carry an "interface"
 *                 string field.
 */
void
InterfaceAddressView::ConfigurationUpdated(const BMessage& message)
{
	const char* interface = message.GetString("interface", NULL);
	if (interface == NULL || strcmp(interface, fInterface.Name()) != 0)
		return;

	_UpdateFields();
}


// #pragma mark - InterfaceAddressView private methods


/**
 * @brief Enables or disables the address fields and Apply button as a group.
 *
 * @param enable  true to make fields editable, false to lock them.
 */
void
InterfaceAddressView::_EnableFields(bool enable)
{
	fAddressField->SetEnabled(enable);
	fNetmaskField->SetEnabled(enable);
	fGatewayField->SetEnabled(enable);
	fApplyButton->SetEnabled(enable);
}


/**
 * @brief Updates the UI to match the current interface configuration.
 *
 * The interface settings may be consulted to determine if the
 * automatic configuration has been specified, if there was no
 * configuration yet.
 */
void
InterfaceAddressView::_UpdateFields()
{
	BMessage interfaceSettings;
	fSettings.GetInterface(fInterface.Name(), interfaceSettings);

	bool autoConfigure = interfaceSettings.IsEmpty();
	if (!autoConfigure) {
		BNetworkInterfaceSettings settings(interfaceSettings);
		autoConfigure = settings.IsAutoConfigure(fFamily);
	}

	BNetworkInterfaceAddress address;
	status_t status = B_ERROR;

	int32 index = fInterface.FindFirstAddress(fFamily);
	if (index >= 0)
		status = fInterface.GetAddressAt(index, address);
	if (!autoConfigure && (index < 0 || status != B_OK
			|| address.Address().IsEmpty())) {
		_SetModeField(kModeDisabled);
		return;
	}

	if (autoConfigure)
		_SetModeField(kModeAuto);
	else
		_SetModeField(kModeStatic);

	fAddressField->SetText(address.Address().ToString());
	fNetmaskField->SetText(address.Mask().ToString());

	BNetworkAddress gateway;
	if (fInterface.GetDefaultGateway(fFamily, gateway) == B_OK)
		fGatewayField->SetText(gateway.ToString());
	else
		fGatewayField->SetText(NULL);
}


/**
 * @brief Marks @a mode as active in the pop-up and adjusts field state.
 *
 * Disabled mode clears all text fields; Static mode focuses the address
 * field for immediate entry. Records the mode as the last applied mode.
 *
 * @param mode  One of kModeAuto, kModeStatic, kModeDisabled.
 */
void
InterfaceAddressView::_SetModeField(uint32 mode)
{
	BMenuItem* item = fModePopUpMenu->FindItem(mode);
	if (item != NULL)
		item->SetMarked(true);

	_EnableFields(mode == kModeStatic);

	if (mode == kModeDisabled) {
		fAddressField->SetText(NULL);
		fNetmaskField->SetText(NULL);
		fGatewayField->SetText(NULL);
	} else if (mode == kModeStatic)
		fAddressField->MakeFocus(true);

	fLastMode = mode;
}


/**
 * @brief Updates the current settings from the controls.
 */
void
InterfaceAddressView::_UpdateSettings()
{
	BMessage interface;
	fSettings.GetInterface(fInterface.Name(), interface);
	BNetworkInterfaceSettings settings(interface);

	settings.SetName(fInterface.Name());

	// Remove previous address for family

	int32 index = settings.FindFirstAddress(fFamily);
	if (index < 0)
		index = settings.FindFirstAddress(AF_UNSPEC);
	if (index >= 0 && index < settings.CountAddresses()) {
		BNetworkInterfaceAddressSettings& address = settings.AddressAt(index);
		_ConfigureAddress(address);
	} else {
		BNetworkInterfaceAddressSettings address;
		_ConfigureAddress(address);
		settings.AddAddress(address);
	}

	interface.MakeEmpty();

	// TODO: better error reporting!
	status_t status = settings.GetMessage(interface);
	if (status == B_OK)
		fSettings.AddInterface(interface);
	else
		fprintf(stderr, "Could not add interface: %s\n", strerror(status));
}


/**
 * @brief Reads the currently selected mode message from the pop-up.
 *
 * @return The marked menu item's message @c what, or kModeAuto if none.
 */
uint32
InterfaceAddressView::_Mode() const
{
	uint32 mode = kModeAuto;
	BMenuItem* item = fModePopUpMenu->FindMarked();
	if (item != NULL)
		mode = item->Message()->what;

	return mode;
}


/**
 * @brief Populates @a settings with the current family, mode, and (when
 *        Static) the user-entered address/mask/gateway.
 *
 * Always clears address fields first so stale auto/static values do not
 * leak across mode switches.
 *
 * @param settings  Address record to update in place.
 */
void
InterfaceAddressView::_ConfigureAddress(
	BNetworkInterfaceAddressSettings& settings)
{
	uint32 mode = _Mode();

	settings.SetFamily(fFamily);
	settings.SetAutoConfigure(mode == kModeAuto);

	settings.Address().Unset();
	settings.Mask().Unset();
	settings.Peer().Unset();
	settings.Broadcast().Unset();
	settings.Gateway().Unset();

	if (mode == kModeStatic) {
		_SetAddress(settings.Address(), fAddressField->Text());
		_SetAddress(settings.Mask(), fNetmaskField->Text());
		_SetAddress(settings.Gateway(), fGatewayField->Text());
	}
}


/**
 * @brief Parses @a text and writes the result into @a address, suppressing
 *        DNS resolution.
 *
 * Empty input (after trimming) leaves @a address untouched.
 *
 * @param address  Destination address; modified on success.
 * @param text     User-entered address string.
 */
void
InterfaceAddressView::_SetAddress(BNetworkAddress& address, const char* text)
{
	BString string(text);
	string.Trim();
	if (string.IsEmpty())
		return;

	address.SetTo(string.String(), static_cast<uint16>(0),
		B_NO_ADDRESS_RESOLUTION);
}
