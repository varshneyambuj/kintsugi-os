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
 *       Axel Dörfler, <axeld@pinc-software.de>
 *       Alexander von Gluck, kallisti5@unixzen.com
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file InterfaceView.cpp
 * @brief Implementation of InterfaceView, the live status / control pane
 *        for a single network interface.
 *
 * Reads the interface flags, hardware address, media type, byte counters,
 * and (for wireless interfaces) the list of nearby networks. Hosts the
 * Disable/Enable toggle and Renegotiate button. The wireless menu is
 * repopulated on a longer cadence than the rest of the fields so the UI
 * stays responsive while the radio scans.
 */


#include "InterfaceView.h"

#include <set>

#include <net/if_media.h>

#include <AutoDeleter.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <NetworkAddress.h>
#include <StringForSize.h>
#include <StringView.h>
#include <TextControl.h>

#include "MediaTypes.h"
#include "WirelessNetworkMenuItem.h"


/** @brief Toggle-button message: enable/disable the interface (IFF_UP). */
static const uint32 kMsgInterfaceToggle = 'onof';
/** @brief Renegotiate-button message: re-run address acquisition. */
static const uint32 kMsgInterfaceRenegotiate = 'redo';
/** @brief Wireless menu message: join the selected network. */
static const uint32 kMsgJoinNetwork = 'join';


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "IntefaceView"


// #pragma mark - InterfaceView


/**
 * @brief Builds the static layout (status grid, network picker, buttons)
 *        and enables pulse delivery.
 */
InterfaceView::InterfaceView()
	:
	BGroupView(B_VERTICAL),
	fPulseCount(0)
{
	SetFlags(Flags() | B_PULSE_NEEDED);

	// TODO: Small graph of throughput?

	BStringView* statusLabel = new BStringView("status label", B_TRANSLATE("Status:"));
	statusLabel->SetAlignment(B_ALIGN_RIGHT);
	fStatusField = new BStringView("status field", "");
	BStringView* macAddressLabel = new BStringView("mac address label",
		B_TRANSLATE("MAC address:"));
	macAddressLabel->SetAlignment(B_ALIGN_RIGHT);
	fMacAddressField = new BStringView("mac address field", "");
	BStringView* linkSpeedLabel = new BStringView("link speed label",
		B_TRANSLATE("Link speed:"));
	linkSpeedLabel->SetAlignment(B_ALIGN_RIGHT);
	fLinkSpeedField = new BStringView("link speed field", "");

	// TODO: These metrics may be better in a BScrollView?
	BStringView* linkTxLabel = new BStringView("tx label",
		B_TRANSLATE("Sent:"));
	linkTxLabel->SetAlignment(B_ALIGN_RIGHT);
	fLinkTxField = new BStringView("tx field", "");
	BStringView* linkRxLabel = new BStringView("rx label",
		B_TRANSLATE("Received:"));
	linkRxLabel->SetAlignment(B_ALIGN_RIGHT);
	fLinkRxField = new BStringView("rx field", "");

	fNetworkMenuField = new BMenuField(B_TRANSLATE("Network:"), new BMenu(
		B_TRANSLATE("Choose automatically")));
	fNetworkMenuField->SetAlignment(B_ALIGN_RIGHT);
	fNetworkMenuField->Menu()->SetLabelFromMarked(true);

	// Construct the BButtons
	fToggleButton = new BButton("onoff", B_TRANSLATE("Disable"),
		new BMessage(kMsgInterfaceToggle));

	fRenegotiateButton = new BButton("heal", B_TRANSLATE("Renegotiate"),
		new BMessage(kMsgInterfaceRenegotiate));
	fRenegotiateButton->SetEnabled(false);

	BLayoutBuilder::Group<>(this)
		.AddGrid()
			.Add(statusLabel, 0, 0)
			.Add(fStatusField, 1, 0)
			.AddMenuField(fNetworkMenuField, 0, 1, B_ALIGN_RIGHT, 1, 2)
			.Add(macAddressLabel, 0, 2)
			.Add(fMacAddressField, 1, 2)
			.Add(linkSpeedLabel, 0, 3)
			.Add(fLinkSpeedField, 1, 3)
			.Add(linkTxLabel, 0, 4)
			.Add(fLinkTxField, 1, 4)
			.Add(linkRxLabel, 0, 5)
			.Add(fLinkRxField, 1, 5)
		.End()
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fToggleButton)
			.Add(fRenegotiateButton)
		.End();
}


/**
 * @brief Destructor. Owned controls are deleted by the BView hierarchy.
 */
InterfaceView::~InterfaceView()
{
}


/**
 * @brief Binds the view to the named interface.
 *
 * @param name  Interface device name (e.g. "/dev/net/eth0").
 */
void
InterfaceView::SetTo(const char* name)
{
	fInterface.SetTo(name);
}


/**
 * @brief Performs an initial _Update() and retargets buttons once the view
 *        joins a window.
 */
void
InterfaceView::AttachedToWindow()
{
	_Update();
		// Populate the fields

	fToggleButton->SetTarget(this);
	fRenegotiateButton->SetTarget(this);
}


/**
 * @brief Routes wireless join, interface toggle, and renegotiate messages.
 *
 * @param message  Incoming BMessage; @c what selects the action.
 */
void
InterfaceView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgJoinNetwork:
		{
			const char* name;
			BNetworkAddress address;
			if (message->FindString("name", &name) == B_OK
				&& message->FindFlat("address", &address) == B_OK) {
				BNetworkDevice device(fInterface.Name());
				status_t status = device.JoinNetwork(address);
				if (status != B_OK) {
					// This does not really matter, as it's stored this way,
					// anyway.
				}
				// TODO: store value
			}
			break;
		}
		case kMsgInterfaceToggle:
		{
			// Disable/enable interface
			uint32 flags = fInterface.Flags();
			if ((flags & IFF_UP) != 0)
				flags &= ~IFF_UP;
			else
				flags |= IFF_UP;

			if (fInterface.SetFlags(flags) == B_OK)
				_Update();
			break;
		}

		case kMsgInterfaceRenegotiate:
		{
			// TODO: renegotiate addresses
			break;
		}

		default:
			BView::MessageReceived(message);
	}
}


/**
 * @brief Periodic refresh hook.
 *
 * Called once per pulse; a full wireless rescan happens every fifth tick to
 * keep the menu lively without thrashing the radio.
 */
void
InterfaceView::Pulse()
{
	// Update the wireless network menu every 5 seconds
	_Update((fPulseCount++ % 5) == 0);
}


/**
 * @brief Populate fields with current settings.
 *
 * Refreshes status, MAC address, link speed, and TX/RX counters, then -- for
 * wireless devices -- reconciles the network menu against the current scan
 * result. When @a updateWirelessNetworks is false the wireless block is
 * skipped to limit work between full rescans.
 *
 * @param updateWirelessNetworks  Refresh and rebuild the wireless menu.
 * @return Always B_OK.
 */
status_t
InterfaceView::_Update(bool updateWirelessNetworks)
{
	BNetworkDevice device(fInterface.Name());
	bool isWireless = device.IsWireless();
	bool disabled = (fInterface.Flags() & IFF_UP) == 0;

	uint32 flags = fInterface.Flags();

	if ((flags & IFF_LINK) == 0)
		fStatusField->SetText(B_TRANSLATE("no link"));
	else if ((flags & (IFF_UP | IFF_CONFIGURING)) == 0)
		fStatusField->SetText(B_TRANSLATE("no stateful configuration"));
	else if ((flags & IFF_CONFIGURING) == IFF_CONFIGURING)
		fStatusField->SetText(B_TRANSLATE("configuring"));
	else if ((flags & IFF_UP) == IFF_UP)
		fStatusField->SetText(B_TRANSLATE("connected"));
	else
		fStatusField->SetText(B_TRANSLATE("unknown"));

	BNetworkAddress hardwareAddress;
	if (device.GetHardwareAddress(hardwareAddress) == B_OK)
		fMacAddressField->SetText(hardwareAddress.ToString());
	else
		fMacAddressField->SetText("-");

	int media = fInterface.Media();
	if ((media & IFM_ACTIVE) != 0)
		fLinkSpeedField->SetText(media_type_to_string(media));
	else
		fLinkSpeedField->SetText("-");

	// Update Link stats
	ifreq_stats stats;
	if (fInterface.GetStats(stats) == B_OK) {
		char buffer[100];

		string_for_size(stats.send.bytes, buffer, sizeof(buffer));
		fLinkTxField->SetText(buffer);

		string_for_size(stats.receive.bytes, buffer, sizeof(buffer));
		fLinkRxField->SetText(buffer);
	}

	// TODO: move the wireless info to a separate tab. We should have a
	// BListView of available networks, rather than a menu, to make them more
	// readable and easier to browse and select.
	if (fNetworkMenuField->IsHidden(fNetworkMenuField) && isWireless)
		fNetworkMenuField->Show();
	else if (!fNetworkMenuField->IsHidden(fNetworkMenuField) && !isWireless)
		fNetworkMenuField->Hide();

	if (isWireless && updateWirelessNetworks) {
		// Rebuild network menu
		BMenu* menu = fNetworkMenuField->Menu();
		int32 count = menu->CountItems();

		// remove non-network items from menu and save them for later
		BMenuItem* chooseItem = NULL;
		BSeparatorItem* separatorItem = NULL;
		if (count > 0 && strcmp(menu->ItemAt(0)->Label(),
				B_TRANSLATE("Choose automatically")) == 0) {
			// remove Choose automatically item
			chooseItem = menu->RemoveItem((int32)0);
			// remove separator item too
			separatorItem = (BSeparatorItem*)menu->RemoveItem((int32)0);
			count -= 2;
		}

		BMenuItem* noNetworksFoundItem = NULL;
		if (menu->CountItems() > 0 && strcmp(menu->ItemAt(0)->Label(),
				B_TRANSLATE("<no wireless networks found>")) == 0) {
			// remove <no wireless networks found> item
			noNetworksFoundItem = menu->RemoveItem((int32)0);
			count--;
		}

		std::set<BNetworkAddress> associated;
		BNetworkAddress address;
		uint32 cookie = 0;
		while (device.GetNextAssociatedNetwork(cookie, address) == B_OK)
			associated.insert(address);

		wireless_network* networks = NULL;
		uint32 networksCount = 0;
		device.GetNetworks(networks, networksCount);

		if ((fPulseCount % 15) == 0 && networksCount == 0) {
			// We don't seem to know of any networks, and it's been long
			// enough since the last scan, so trigger one to try and
			// find some networks.
			device.Scan(false, false);

			// We don't want to block for the full length of the scan, but
			// 50ms is often more than enough to find at least one network,
			// and the increase in perceived QoS to the user of not seeing
			// "no wireless networks" if we can avoid it is great enough
			// to merit such a wait. It's only just over ~4 vertical
			// retraces, anyway.
			snooze(50 * 1000);

			device.GetNetworks(networks, networksCount);
		}

		ArrayDeleter<wireless_network> networksDeleter(networks);

		// go through menu items and remove networks that have dropped out
		for (int32 index = 0; index < count; index++) {
			WirelessNetworkMenuItem* networkItem =
				dynamic_cast<WirelessNetworkMenuItem*>(
					menu->ItemAt(index));
			if (networkItem == NULL)
				break;

			bool networkFound = false;
			for (uint32 i = 0; i < networksCount; i++) {
				if (networkItem->Network() == networks[i]) {
					networkFound = true;
					break;
				}
			}

			if (!networkFound) {
				menu->RemoveItem(networkItem);
				count--;
			}
		}

		// go through networks and add new ones to menu
		for (uint32 i = 0; i < networksCount; i++) {
			const wireless_network& network = networks[i];

			bool networkFound = false;
			for (int32 index = 0; index < count; index++) {
				WirelessNetworkMenuItem* networkItem =
					dynamic_cast<WirelessNetworkMenuItem*>(
						menu->ItemAt(index));
				if (networkItem == NULL)
					break;

				if (networkItem->Network() == network) {
					// found it
					networkFound = true;
					if (associated.find(network.address) != associated.end())
						networkItem->SetMarked(true);
					break;
				}
			}

			if (!networkFound) {
				BMessage* message = new BMessage(kMsgJoinNetwork);
				message->AddString("device", fInterface.Name());
				message->AddString("name", network.name);
				message->AddFlat("address", &network.address);
				BMenuItem* item = new WirelessNetworkMenuItem(network,
					message);
				menu->AddItem(item);
				if (associated.find(network.address) != associated.end())
					item->SetMarked(true);
			}

			count++;
		}

		if (count == 0) {
			// no networks found
			if (noNetworksFoundItem != NULL)
				menu->AddItem(noNetworksFoundItem);
			else {
				BMenuItem* item = new BMenuItem(
					B_TRANSLATE("<no wireless networks found>"), NULL);
				item->SetEnabled(false);
				menu->AddItem(item);
			}
		} else {
			// sort items by signal strength
			menu->SortItems(WirelessNetworkMenuItem::CompareSignalStrength);

			// add Choose automatically item to start
			if (chooseItem != NULL) {
				menu->AddItem(chooseItem, 0);
				menu->AddItem(separatorItem, 1);
			} else {
				BMenuItem* item = new BMenuItem(
					B_TRANSLATE("Choose automatically"), NULL);
				if (menu->FindMarked() == NULL)
					item->SetMarked(true);
				menu->AddItem(item, 0);
				menu->AddItem(new BSeparatorItem(), 1);
			}
		}

		menu->SetTargetForItems(this);
	}

	//fRenegotiateButton->SetEnabled(!disabled);
	fToggleButton->SetLabel(disabled
		? B_TRANSLATE("Enable") : B_TRANSLATE("Disable"));

	return B_OK;
}
