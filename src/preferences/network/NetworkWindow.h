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
 * Original authors: Axel Dörfler, Alexander von Gluck.
 */

/** @file NetworkWindow.h
    @brief Top-level Network preflet window: outline list of interfaces /
           services / VPN, the selected item's editor pane, and global
           Revert / Deskbar replicant controls. */

#ifndef NETWORK_WINDOW_H
#define NETWORK_WINDOW_H


#include <map>

#include <ObjectList.h>
#include <Window.h>

#include <NetworkSettings.h>
#include <NetworkSettingsAddOn.h>


using namespace BNetworkKit;

class BButton;
class BMenu;
class InterfaceListItem;
class InterfaceView;


/**
 * @brief Window-level message codes used by the Network preflet.
 *
 * @c kMsgSettingsItemUpdated is posted by add-on items when their settings
 * mutate so the window can refresh the Revert button.
 */
enum {
	kMsgSettingsItemUpdated = 'SIup'
};


/**
 * @brief Main BWindow for the Network preference application.
 *
 * Lays out an outline list of interfaces, services, and add-on items on the
 * left and an "add-on shell" on the right that hosts whichever editor view
 * matches the selection. Subscribes to BNetworkSettings and the network
 * monitor so external configuration changes are reflected live.
 */
class NetworkWindow : public BWindow {
public:
								NetworkWindow();
	virtual						~NetworkWindow();

			bool				QuitRequested();
			void				MessageReceived(BMessage* message);

private:
	typedef	BWindow				inherited;

			void				_BuildProfilesMenu(BMenu* menu, int32 what);
			void				_ScanInterfaces();
			void				_ScanAddOns();
			BNetworkSettingsItem*
								_SettingsItemFor(BListItem* item);
			void				_SortItemsUnder(BListItem* item);
			BListItem*			_ListItemFor(BNetworkSettingsType type);
			BListItem*			_CreateItem(const char* label);
			void				_SelectItem(BListItem* listItem);
			void				_BroadcastSettingsUpdate(uint32 type);
			void				_BroadcastConfigurationUpdate(
									const BMessage& message);
			void				_UpdateRevertButton();

			bool				_IsReplicantInstalled();
			void				_ShowReplicant(bool show);

	static	const char*			_ItemName(const BListItem* item);
	static	int					_CompareTopLevelListItems(const BListItem* a,
									const BListItem* b);
	static	int					_CompareListItems(const BListItem* a,
									const BListItem* b);

private:
	typedef BObjectList<BNetworkSettingsAddOn> AddOnList;
	typedef std::map<BString, BListItem*> ItemMap;
	typedef std::map<BListItem*, BNetworkSettingsItem*> SettingsMap;

			BNetworkSettings	fSettings;
			AddOnList			fAddOns;

			BOutlineListView*	fListView;
			ItemMap				fInterfaceItemMap;
			BListItem*			fServicesItem;
			BListItem*			fDialUpItem;
			BListItem*			fVPNItem;
			BListItem*			fOtherItem;

			SettingsMap			fSettingsMap;

			InterfaceView*		fInterfaceView;
			BView*				fAddOnShellView;

			BButton*			fRevertButton;
};


/** @brief Global messenger pointing at the live NetworkWindow; used by
           add-ons to post notifications without a direct reference. */
extern BMessenger gNetworkWindow;


#endif // NETWORK_WINDOW_H
