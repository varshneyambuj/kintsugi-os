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
 * MIT License. Copyright 2008-09, Oliver Ruiz Dorantes.
 */

/** @file RemoteDevicesView.h
    @brief Declares RemoteDevicesView, the remote-devices BView in the Bluetooth preflet. */

#ifndef REMOTE_DEVICES_VIEW_H_
#define REMOTE_DEVICES_VIEW_H_

#include <View.h>
#include <ColorControl.h>
#include <Message.h>
#include <ListItem.h>
#include <ListView.h>
#include <Button.h>
#include <ScrollView.h>
#include <ScrollBar.h>
#include <String.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <StringView.h>
#include <Invoker.h>


/**
 * @brief BView managing the list of remote Bluetooth devices.
 *
 * Hosts a BListView of DeviceListItems plus the Add/Remove/Pair/Disconnect
 * buttons. Add launches an InquiryPanel; Pair and Disconnect operate on
 * the currently selected RemoteDevice.
 */
class RemoteDevicesView : public BView
{
public:
			RemoteDevicesView(const char *name, uint32 flags);
			~RemoteDevicesView(void);
	void	AttachedToWindow(void);
	void	MessageReceived(BMessage *msg);

	void	LoadSettings(void);
	bool	IsDefaultable(void);

protected:

	void	SetCurrentColor(rgb_color color);
	void	UpdateControls();
	void	UpdateAllColors();

	BButton*		addButton;
	BButton*		removeButton;
	BButton*		pairButton;
	BButton*		disconnectButton;
//	BButton*		blockButton;
//	BButton*		availButton;
	BListView*		fDeviceList;
	BScrollView*	fScrollView;


};

#endif
