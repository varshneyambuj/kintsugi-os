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
 *   Copyright 2007-2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   Copyright 2021 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Tri-Edge AI <triedgeai@gmail.com>
 */


/**
 * @file ConnectionIncoming.cpp
 * @brief Implementation of ConnectionIncoming, the incoming Bluetooth connection dialog
 *
 * ConnectionIncoming is a floating window shown when a remote Bluetooth device
 * requests a connection to the local adapter. It displays the remote device's
 * address and prompts the user to accept or reject the connection.
 *
 * @see ConnectionView, PincodeWindow
 */


#include <ConnectionIncoming.h>
#include <ConnectionView.h>

namespace Bluetooth
{

/**
 * @brief Construct an incoming-connection dialog for a raw Bluetooth address.
 *
 * Creates a non-resizable, non-zoomable floating window with a 1-second pulse
 * rate and a ConnectionView that shows @c "<unknown_device>" as the device
 * name alongside the string representation of @a address.
 *
 * @param address The raw Bluetooth device address of the connecting device.
 * @see ConnectionIncoming::ConnectionIncoming(RemoteDevice*), ConnectionView
 */
ConnectionIncoming::ConnectionIncoming(bdaddr_t address)
	:
	BWindow(BRect(600, 100, 1000, 180), "Incoming Connection..",
		B_FLOATING_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE)
					// 400x80
{
	SetPulseRate(1 * 1000 * 1000);
		// 1 second
	fView = new ConnectionView(BRect(0, 0, 400, 80), "<unknown_device>",
		bdaddrUtils::ToString(address));
	AddChild(fView);
}


/**
 * @brief Construct an incoming-connection dialog for a known RemoteDevice.
 *
 * Creates a non-resizable, non-zoomable floating window with a 1-second pulse
 * rate. If @a rDevice is non-null the ConnectionView is populated with the
 * device's friendly name and Bluetooth address; otherwise both fields fall
 * back to safe placeholder values.
 *
 * @param rDevice Pointer to the RemoteDevice that initiated the connection,
 *                or @c NULL if the device is unknown.
 * @see ConnectionIncoming::ConnectionIncoming(bdaddr_t), ConnectionView
 */
ConnectionIncoming::ConnectionIncoming(RemoteDevice* rDevice)
	:
	BWindow(BRect(600, 100, 1000, 180), "Incoming Connection",
		B_FLOATING_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE)
{
	SetPulseRate(1 * 1000 * 1000);
		// 1 second

	if (rDevice != NULL)
		fView = new ConnectionView(BRect(0, 0, 400, 80), rDevice->GetFriendlyName(),
					bdaddrUtils::ToString(rDevice->GetBluetoothAddress()));
	else
		fView = new ConnectionView(BRect(0, 0, 400, 80), "<unknown_device>",
					bdaddrUtils::ToString(bdaddrUtils::NullAddress()));

	AddChild(fView);
}


/**
 * @brief Destroy the ConnectionIncoming window.
 *
 * Child views added via AddChild() are owned by the window and are
 * freed automatically by the BWindow destructor.
 */
ConnectionIncoming::~ConnectionIncoming()
{
}


/**
 * @brief Handle messages delivered to this window.
 *
 * Currently a placeholder; no application-level messages are processed
 * by ConnectionIncoming itself. Override in a subclass to react to
 * custom messages posted to this window.
 *
 * @param message The incoming BMessage to dispatch.
 */
void
ConnectionIncoming::MessageReceived(BMessage* message)
{
}


/**
 * @brief Respond to a quit request for this window.
 *
 * Delegates directly to BWindow::QuitRequested(), which allows the
 * window to close unconditionally. Override to add confirmation logic
 * before the window is destroyed.
 *
 * @return @c true to allow the window to quit, @c false to cancel.
 */
bool
ConnectionIncoming::QuitRequested()
{
	return BWindow::QuitRequested();
}


} /* end namespace Bluetooth */
