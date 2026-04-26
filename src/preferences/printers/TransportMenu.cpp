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
 *   Copyright 2002-2010, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Pfeiffer
 *       Philippe Houdoin
 */


/**
 * @file TransportMenu.cpp
 * @brief Lazy submenu listing ports exposed by a transport add-on.
 *
 * Used by AddPrinterDialog to expand a transport entry like "Serial Port"
 * into the actual list of port nodes (for example, those under
 * /dev/ports/) that the transport is willing to advertise. The list is
 * built the first time the submenu is opened by scripting print_server.
 */


#include "TransportMenu.h"


#include <Catalog.h>
#include <MenuItem.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TransportMenu"


/**
 * @brief Constructs a transport submenu.
 *
 * @param title         Menu label shown in the parent menu.
 * @param what          BMessage @c what code attached to every generated
 *                      port item.
 * @param messenger     BMessenger pointing at the transport add-on; used
 *                      to query its port list.
 * @param transportName Display name of the transport, included in
 *                      generated messages so AddPrinterDialog can
 *                      identify which transport produced the selection.
 */
TransportMenu::TransportMenu(const char* title, uint32 what,
	const BMessenger& messenger, const BString& transportName)
	:
	BMenu(title),
	fWhat(what),
	fMessenger(messenger),
	fTransportName(transportName)
{
}


/**
 * @brief Builds the submenu's items the first time it is opened.
 *
 * Clears any existing items, then queries the transport's @c Ports
 * property and adds one BMenuItem per reported port. If the transport
 * cannot advertise its ports an explanatory placeholder item is added
 * instead.
 *
 * @param state Phase reported by BMenu; only B_INITIAL_ADD does work.
 *
 * @return Always false: the framework should not call us again.
 */
bool
TransportMenu::AddDynamicItem(add_state state)
{
	if (state != B_INITIAL_ADD)
		return false;

	BMenuItem* item = RemoveItem((int32)0);
	while (item != NULL) {
		delete item;
		item = RemoveItem((int32)0);
	}

	BMessage msg;
	msg.MakeEmpty();
	msg.what = B_GET_PROPERTY;
	msg.AddSpecifier("Ports");
	BMessage reply;
	if (fMessenger.SendMessage(&msg, &reply) != B_OK)
		return false;

	BString portId;
	BString portName;
	if (reply.FindString("port_id", &portId) != B_OK) {
		// Show error message in submenu
		BMessage* portMsg = new BMessage(fWhat);
		AddItem(new BMenuItem(
			B_TRANSLATE("No printer found!"), portMsg));
		return false;
	}

	// Add ports to submenu
	for (int32 i = 0; reply.FindString("port_id", i, &portId) == B_OK;
		i++) {
		if (reply.FindString("port_name", i, &portName) != B_OK
			|| !portName.Length())
			portName = portId;

		// Create menu item in submenu for port
		BMessage* portMsg = new BMessage(fWhat);
		portMsg->AddString("name", fTransportName);
		portMsg->AddString("path", portId);
		AddItem(new BMenuItem(portName.String(), portMsg));
	}

	return false;
}
