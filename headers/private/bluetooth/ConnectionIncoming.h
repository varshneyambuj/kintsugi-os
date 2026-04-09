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
 *       Tri-Edge AI <triedgeai@gmail.com>
 */

/** @file ConnectionIncoming.h
 *  @brief Window that notifies the user of an incoming Bluetooth connection
 *         request and prompts for acceptance or rejection. */

#ifndef _CONNECTION_INCOMING_H_
#define _CONNECTION_INCOMING_H_

#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#include <AppKit.h>
#include <SupportKit.h>
#include <InterfaceKit.h>

#include <ConnectionView.h>
#include <bluetooth/RemoteDevice.h>
#include <bluetooth/bdaddrUtils.h>


namespace Bluetooth {

class RemoteDevice;
class ConnectionView;

/** @brief A modal BWindow that presents information about an incoming Bluetooth
 *         connection and allows the user to accept or dismiss the request. */
class ConnectionIncoming : public BWindow {
public:
	/** @brief Constructs a ConnectionIncoming window for a device identified
	 *         only by its raw Bluetooth address.
	 *  @param address The Bluetooth device address of the connecting remote device. */
						ConnectionIncoming(bdaddr_t address);

	/** @brief Constructs a ConnectionIncoming window, optionally using a
	 *         RemoteDevice object for richer device information.
	 *  @param rDevice Pointer to the remote device; may be NULL to use a
	 *                 default display. */
						ConnectionIncoming(RemoteDevice* rDevice = NULL);

	/** @brief Destroys the ConnectionIncoming window and releases its resources. */
						~ConnectionIncoming();

	/** @brief Handles incoming BMessages, including user button actions.
	 *  @param message Pointer to the message delivered to this window. */
	virtual void		MessageReceived(BMessage* message);

	/** @brief Called when the user or system requests that the window close.
	 *  @return true to allow the window to quit, false to prevent it. */
	virtual bool		QuitRequested();

private:
	ConnectionView*				fView;
};

}

#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::ConnectionIncoming;
#endif

#endif /* _CONNECTION_INCOMING_H_ */
