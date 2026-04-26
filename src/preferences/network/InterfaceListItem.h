/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2004-2015, Haiku, Inc.
 * Original authors: Alexander von Gluck, Philippe Houdoin, Fredrik Modéen,
 *                   John Scipione.
 */

/** @file InterfaceListItem.h
    @brief BListItem renderer for a single network interface in the
           Network preflet's left-hand interface list. */

#ifndef INTERFACE_LIST_ITEM_H
#define INTERFACE_LIST_ITEM_H


#include <ListItem.h>
#include <NetworkInterface.h>
#include <NetworkSettingsAddOn.h>


/**
 * @brief Family of network interface drawn by InterfaceListItem.
 *
 * Determines which icon set is loaded and which subtitle text is shown.
 */
enum BNetworkInterfaceType {
	B_NETWORK_INTERFACE_TYPE_WIFI = 'wifi',
	B_NETWORK_INTERFACE_TYPE_ETHERNET = 'ethr',
	B_NETWORK_INTERFACE_TYPE_DIAL_UP = 'dial',
	B_NETWORK_INTERFACE_TYPE_VPN = 'nvpn',
	B_NETWORK_INTERFACE_TYPE_OTHER = 'othe',
};


class BBitmap;


/**
 * @brief Two-line list row showing an interface's icon, name, status, and
 *        a media-type subtitle.
 *
 * Listens to network configuration notifications so the row's link state
 * and disabled flag stay in sync with the running stack.
 */
class InterfaceListItem : public BListItem,
	public BNetworkKit::BNetworkConfigurationListener {
public:
								InterfaceListItem(const char* name,
									BNetworkInterfaceType type);
								~InterfaceListItem();

			void				DrawItem(BView* owner,
									BRect bounds, bool complete);
			void				Update(BView* owner, const BFont* font);

	/** @brief Returns the interface device name (e.g. "/dev/net/eth0"). */
	inline	const char*			Name() const { return fInterface.Name(); }

	virtual	void				ConfigurationUpdated(const BMessage& message);

private:
			void 				_Init();
			void				_PopulateBitmaps(const char* mediaType);
			void				_UpdateState();
			BBitmap*			_StateIcon() const;
			const char*			_StateText() const;

private:
			BNetworkInterfaceType fType;

			BBitmap* 			fIcon;
			BBitmap*			fIconOffline;
			BBitmap*			fIconPending;
			BBitmap*			fIconOnline;

			BNetworkInterface	fInterface;
				// Hardware Interface

			float				fFirstLineOffset;
			float				fLineOffset;

			BString				fDeviceName;
			bool				fDisabled;
			bool				fHasLink;
			bool				fConnecting;
			BString				fSubtitle;
};


#endif // INTERFACE_LIST_ITEM_H
