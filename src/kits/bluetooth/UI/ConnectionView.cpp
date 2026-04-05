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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2021 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Tri-Edge AI <triedgeai@gmail.com>
 */


/**
 * @file ConnectionView.cpp
 * @brief Implementation of ConnectionView, the content view for Bluetooth connection dialogs
 *
 * ConnectionView is a BView that displays the Bluetooth icon alongside the
 * remote device name and address. It is used as the content area inside
 * ConnectionIncoming windows to present a consistent, informative layout.
 *
 * @see ConnectionIncoming, BluetoothIconView
 */


#include <ConnectionView.h>
#include <BluetoothIconView.h>

namespace Bluetooth
{

/**
 * @brief Construct a ConnectionView displaying the given device name and address.
 *
 * Builds a horizontally laid-out view containing a BluetoothIconView on the
 * left and a column of BStringView labels on the right showing a status
 * message, the remote device's friendly name, and its MAC address. The view
 * requests pulse events so that ConnectionView::Pulse() can animate the
 * status message and auto-close the parent window after a timeout.
 *
 * @param frame   The bounding rectangle for this view.
 * @param device  The friendly name of the remote Bluetooth device to display.
 * @param address The MAC address of the remote Bluetooth device to display.
 * @see ConnectionView::Pulse(), BluetoothIconView, ConnectionIncoming
 */
ConnectionView::ConnectionView(BRect frame, BString device, BString address)
	:
	BView(frame, "ConnectionView", 0, B_PULSE_NEEDED)
{
	SetLayout(new BGroupLayout(B_HORIZONTAL));
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fIcon = new BluetoothIconView();

	strMessage = "A new connection is incoming..";

	fMessage = new BStringView(frame, "", strMessage, B_FOLLOW_LEFT);
	fMessage->SetAlignment(B_ALIGN_LEFT);

	fDeviceLabel = new BStringView(frame, "", "Device Name:", B_FOLLOW_LEFT);
	fDeviceLabel->SetFont(be_bold_font);

	fDeviceText = new BStringView(frame, "", device, B_FOLLOW_RIGHT);
	fDeviceText->SetAlignment(B_ALIGN_RIGHT);

	fAddressLabel = new BStringView(frame, "", "MAC Address:", B_FOLLOW_LEFT);
	fAddressLabel->SetFont(be_bold_font);

	fAddressText = new BStringView(frame, "", address, B_FOLLOW_RIGHT);
	fAddressText->SetAlignment(B_ALIGN_RIGHT);

	AddChild(BGroupLayoutBuilder(B_HORIZONTAL, 0)
			.Add(BGroupLayoutBuilder(B_VERTICAL, 8)
				.Add(fIcon)
			)
			.Add(BGroupLayoutBuilder(B_VERTICAL, 0)
				.Add(fMessage)
				.AddGlue()
				.Add(BGroupLayoutBuilder(B_HORIZONTAL, 10)
					.Add(fDeviceLabel)
					.AddGlue()
					.Add(fDeviceText)
				)
				.Add(BGroupLayoutBuilder(B_HORIZONTAL, 10)
					.Add(fAddressLabel)
					.AddGlue()
					.Add(fAddressText)
				)
				.AddGlue()
			)
			.AddGlue()
			.SetInsets(8, 8, 8, 8)
	);
}


/**
 * @brief Animate the status message and auto-close the dialog after five pulses.
 *
 * Called once per second by the window's pulse mechanism. Each invocation
 * appends a period to @c strMessage to create a simple progress animation.
 * After five pulses the parent window is sent @c B_QUIT_REQUESTED so that
 * the dialog closes automatically if the user has not interacted with it.
 *
 * @note The pulse rate is set to 1 second in ConnectionIncoming's constructor
 *       via @c SetPulseRate(1 * 1000 * 1000).
 * @see ConnectionIncoming::ConnectionIncoming(bdaddr_t)
 */
void
ConnectionView::Pulse()
{
	static int pulses = 0;

	pulses++;

	if (pulses >= 5) {
		Window()->PostMessage(B_QUIT_REQUESTED);
	} else {
		strMessage += ".";
		fMessage->SetText(strMessage);
	}
}

}
