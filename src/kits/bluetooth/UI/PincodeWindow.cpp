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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2007-2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   Copyright 2021 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Tri-Edge AI <triedgeai@gmail.com>
 */


/**
 * @file PincodeWindow.cpp
 * @brief Implementation of PincodeWindow, the Bluetooth PIN entry dialog
 *
 * PincodeWindow presents a dialog prompting the user to enter a PIN code
 * for Bluetooth pairing. The entered PIN is sent back to the Bluetooth
 * server to complete the legacy pairing procedure with the remote device.
 *
 * @see ConnectionIncoming, LocalDevice
 */


#include <stdio.h>
#include <unistd.h>
#include <malloc.h>

#include <String.h>
#include <Message.h>
#include <Application.h>

#include <Button.h>
#include <GroupLayoutBuilder.h>
#include <InterfaceDefs.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <TextControl.h>

#include <bluetooth/RemoteDevice.h>
#include <bluetooth/LocalDevice.h>
#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/bluetooth_error.h>

#include <bluetooth/HCI/btHCI_command.h>
#include <bluetooth/HCI/btHCI_event.h>

#include <PincodeWindow.h>
#include <bluetoothserver_p.h>
#include <CommandManager.h>


#define H_SEPARATION  15
#define V_SEPARATION  10
#define BD_ADDR_LABEL "BD_ADDR: "


/** @brief Message constant sent when the user clicks the "Pair" button. */
static const uint32 skMessageAcceptButton = 'acCp';
/** @brief Message constant sent when the user clicks the "Cancel" button. */
static const uint32 skMessageCancelButton = 'mVch';


namespace Bluetooth
{

/**
 * @brief Constructs a PincodeWindow from a raw Bluetooth address and HCI device id.
 *
 * Creates the PIN entry dialog, builds the UI, and populates the address
 * label with the string representation of @p address. This constructor is
 * intended for use when a RemoteDevice object is not yet available (e.g.
 * when the pairing request arrives before device enumeration is complete).
 *
 * @param address The Bluetooth device address of the remote device requesting
 *                pairing, used to populate the MAC address label.
 * @param hid     The HCI device identifier of the local Bluetooth adapter
 *                through which the pairing is taking place.
 */
PincodeWindow::PincodeWindow(bdaddr_t address, hci_id hid)
	: BWindow(BRect(700, 200, 1000, 400), "PIN Code Request",
		B_FLOATING_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE),
		fBdaddr(address),
		fHid(hid)
{
	InitUI();

	// TODO: Get more info about device" ote name/features/encry/auth... etc
	SetBDaddr(bdaddrUtils::ToString(fBdaddr));

}


/**
 * @brief Constructs a PincodeWindow from an existing RemoteDevice.
 *
 * Creates the PIN entry dialog, builds the UI, and populates the address
 * label from the remote device's Bluetooth address. The HCI device identifier
 * is obtained from the device's associated LocalDevice owner.
 *
 * @param rDevice Pointer to the RemoteDevice for which pairing is being
 *                requested. Must have a valid LocalDevice owner set.
 */
PincodeWindow::PincodeWindow(RemoteDevice* rDevice)
	: BWindow(BRect(700, 200, 1000, 400), "PIN Code Request",
		B_FLOATING_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE)
{
	InitUI();

	// TODO: Get more info about device" ote name/features/encry/auth... etc
	SetBDaddr(bdaddrUtils::ToString(rDevice->GetBluetoothAddress()));
	fHid = (rDevice->GetLocalDeviceOwner())->ID();
}


/**
 * @brief Creates and arranges all UI widgets inside the window.
 *
 * Instantiates the Bluetooth icon view, descriptive string views, device name
 * and MAC address labels, the PIN code text control, and the "Pair" / "Cancel"
 * buttons. All widgets are composed into a nested BGroupLayoutBuilder hierarchy
 * and added to the window's root layout.
 *
 * @note This method is called from both constructors and must not depend on any
 *       state that differs between them. Device-specific values (e.g. the MAC
 *       address) are applied separately via SetBDaddr() after InitUI() returns.
 */
void
PincodeWindow::InitUI()
{
	SetLayout(new BGroupLayout(B_HORIZONTAL));

	fIcon = new BluetoothIconView();

	fMessage = new BStringView("fMessage", "Input the PIN code to pair with");
	fMessage2 = new BStringView("fMessage2", "the following Bluetooth device.");

	fDeviceLabel = new BStringView("fDeviceLabel","Device Name: ");
	fDeviceLabel->SetFont(be_bold_font);

	fDeviceText = new BStringView("fDeviceText", "<unknown_device>");

	fAddressLabel = new BStringView("fAddressLabel", "MAC Address: ");
	fAddressLabel->SetFont(be_bold_font);

	fAddressText = new BStringView("fAddressText", "<mac_address>");

	fPincodeText = new BTextControl("fPINCode", "PIN Code:", "0000", NULL);
	fPincodeText->TextView()->SetMaxBytes(16 * sizeof(fPincodeText->Text()[0]));
	fPincodeText->MakeFocus();

	fAcceptButton = new BButton("fAcceptButton", "Pair",
		new BMessage(skMessageAcceptButton));

	fCancelButton = new BButton("fCancelButton", "Cancel",
		new BMessage(skMessageCancelButton));

	AddChild(BGroupLayoutBuilder(B_VERTICAL, 0)
		.Add(BGroupLayoutBuilder(B_HORIZONTAL, 0)
			.Add(BGroupLayoutBuilder(B_HORIZONTAL, 8)
				.Add(fIcon)
			)
			.Add(BGroupLayoutBuilder(B_VERTICAL, 0)
				.Add(fMessage)
				.Add(fMessage2)
				.AddGlue()
			)
		)
		.Add(BGroupLayoutBuilder(B_HORIZONTAL, 0)
			.Add(fDeviceLabel)
			.AddGlue()
			.Add(fDeviceText)
		)
		.Add(BGroupLayoutBuilder(B_HORIZONTAL, 0)
			.Add(fAddressLabel)
			.AddGlue()
			.Add(fAddressText)
		)
		.AddGlue()
		.Add(fPincodeText)
		.AddGlue()
		.Add(BGroupLayoutBuilder(B_HORIZONTAL, 10)
			.AddGlue()
			.Add(fCancelButton)
			.Add(fAcceptButton)
		)
		.SetInsets(8, 8, 8, 8)
	);
}


/**
 * @brief Handles button-press messages and forwards unknown messages to BWindow.
 *
 * Processes two internal message codes:
 * - @c skMessageAcceptButton – builds an HCI PIN Code Request Reply command
 *   from the text currently in the PIN code field and sends it to the
 *   Bluetooth server via @c be_app_messenger. On a successful server reply the
 *   window posts @c B_QUIT_REQUESTED to itself.
 * - @c skMessageCancelButton – builds an HCI PIN Code Request Negative Reply
 *   command and sends it to the Bluetooth server, rejecting the pairing
 *   request. On a successful server reply the window posts
 *   @c B_QUIT_REQUESTED to itself.
 *
 * All other messages are forwarded to BWindow::MessageReceived().
 *
 * @param msg The BMessage delivered by the window's message loop.
 * @see QuitRequested(), InitUI()
 */
void
PincodeWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what)
	{
		case skMessageAcceptButton:
		{
			BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
			BMessage reply;
			size_t size;
			int8 bt_status = BT_ERROR;

			void* command = buildPinCodeRequestReply(fBdaddr,
				strlen(fPincodeText->Text()),
				(char*)fPincodeText->Text(), &size);

			if (command == NULL) {
				break;
			}

			request.AddInt32("hci_id", fHid);
			request.AddData("raw command", B_ANY_TYPE, command, size);
			request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
			request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_LINK_CONTROL,
				OCF_PIN_CODE_REPLY));

			// we reside in the server
			if (be_app_messenger.SendMessage(&request, &reply) == B_OK) {
				if (reply.FindInt8("status", &bt_status ) == B_OK) {
					PostMessage(B_QUIT_REQUESTED);
				}
				// TODO: something failed here
			}
			break;
		}

		case skMessageCancelButton:
		{
			BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
			BMessage reply;
			size_t size;
			int8 bt_status = BT_ERROR;

			void* command = buildPinCodeRequestNegativeReply(fBdaddr, &size);

			if (command == NULL) {
				break;
			}

			request.AddInt32("hci_id", fHid);
			request.AddData("raw command", B_ANY_TYPE, command, size);
			request.AddInt16("eventExpected",  HCI_EVENT_CMD_COMPLETE);
			request.AddInt16("opcodeExpected", PACK_OPCODE(OGF_LINK_CONTROL,
				OCF_PIN_CODE_NEG_REPLY));

			if (be_app_messenger.SendMessage(&request, &reply) == B_OK) {
				if (reply.FindInt8("status", &bt_status ) == B_OK ) {
					PostMessage(B_QUIT_REQUESTED);
				}
				// TODO: something failed here
			}
			break;
		}

		default:
			BWindow::MessageReceived(msg);
			break;
	}
}


/**
 * @brief Handles the window close request.
 *
 * Delegates directly to BWindow::QuitRequested(), allowing the default
 * framework behaviour (closing and deleting the window) to proceed.
 *
 * @return @c true to allow the window to quit, as returned by
 *         BWindow::QuitRequested().
 */
bool
PincodeWindow::QuitRequested()
{
	return BWindow::QuitRequested();
}


/**
 * @brief Updates the MAC address label displayed in the dialog.
 *
 * Sets the text of the address BStringView to the supplied string. This is
 * called from both constructors after InitUI() to populate the label with
 * the human-readable form of the remote device's Bluetooth address.
 *
 * @param address A string containing the formatted Bluetooth address
 *                (e.g. "00:11:22:33:44:55") to display.
 */
void
PincodeWindow::SetBDaddr(BString address)
{
	fAddressText->SetText(address);
}

} /* end namespace Bluetooth */
