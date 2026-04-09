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
 *   Copyright 2007-2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   Copyright 2021, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Ruiz Dorantes <oliver.ruiz.dorantes@gmail.com>
 *       Tri-Edge AI <triedgeai@gmail.com>
 */

/** @file PincodeWindow.h
 *  @brief Window that prompts the user to enter or confirm a Bluetooth PIN
 *         code during legacy pairing. */

#ifndef	_PINCODE_REQUEST_WINDOW_H_
#define	_PINCODE_REQUEST_WINDOW_H_


#include <View.h>
#include <Window.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/HCI/btHCI.h>

#include <BluetoothIconView.h>

class BStringView;
class BButton;
class BTextControl;

namespace Bluetooth {

class RemoteDevice;

/** @brief A BWindow that presents a PIN entry dialog for legacy Bluetooth
 *         pairing, displaying device information and forwarding the user's
 *         response to the Bluetooth server. */
class PincodeWindow : public BWindow {
public:
	/** @brief Constructs a PincodeWindow for a device identified by its raw
	 *         Bluetooth address and the local HCI device ID.
	 *  @param address Bluetooth address of the remote device requesting pairing.
	 *  @param hid     HCI device identifier of the local controller. */
							PincodeWindow(bdaddr_t address, hci_id hid);

	/** @brief Constructs a PincodeWindow using a RemoteDevice object for
	 *         richer device information display.
	 *  @param rDevice Pointer to the RemoteDevice initiating the pairing request. */
							PincodeWindow(RemoteDevice* rDevice);

	/** @brief Handles incoming BMessages including button presses for Accept
	 *         and Cancel actions.
	 *  @param msg Pointer to the message delivered to this window. */
	virtual void			MessageReceived(BMessage* msg);

	/** @brief Called when the user or system requests that the window close.
	 *  @return true to allow the window to quit, false to prevent it. */
	virtual bool			QuitRequested();

	/** @brief Updates the displayed Bluetooth address string.
	 *  @param address The Bluetooth address to display in the window. */
			void			SetBDaddr(BString address);

private:
			void			InitUI();
			bdaddr_t		fBdaddr;
			hci_id			fHid;

			BStringView*	fMessage;
			BStringView*	fRemoteInfo;
			BButton*		fAcceptButton;
			BButton*		fCancelButton;
			BTextControl*	fPincodeText;

			BluetoothIconView* 	fIcon;
			BStringView*		fMessage2;
			BStringView*		fDeviceLabel;
			BStringView*		fDeviceText;
			BStringView*		fAddressLabel;
			BStringView*		fAddressText;
};

}

#ifndef	_BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::PincodeWindow;
#endif

#endif /* _PINCODE_REQUEST_WINDOW_H_ */
