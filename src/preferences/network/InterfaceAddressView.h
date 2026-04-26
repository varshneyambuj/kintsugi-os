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
 * Original authors: Alexander von Gluck, John Scipione.
 */

/** @file InterfaceAddressView.h
    @brief Per-family address-configuration view (mode + address + mask +
           gateway) used inside the Network preflet's interface tab. */

#ifndef INTERFACE_ADDRESS_VIEW_H
#define INTERFACE_ADDRESS_VIEW_H


#include <GroupView.h>
#include <NetworkInterface.h>
#include <NetworkSettings.h>
#include <NetworkSettingsAddOn.h>


class BButton;
class BMenuField;
class BMessage;
class BPopUpMenu;
class BRect;
class BTextControl;
class IPAddressControl;


using namespace BNetworkKit;


/**
 * @brief Composite view that edits a single (interface, address-family)
 *        pair.
 *
 * Renders a mode pop-up (DHCP/Automatic, Static, Disabled), three address
 * fields, and an Apply button. Holds a snapshot of the original settings so
 * Revert() can restore them.
 */
class InterfaceAddressView : public BGroupView {
public:
								InterfaceAddressView(int family,
									const char* interface,
									BNetworkSettings& settings);
	virtual						~InterfaceAddressView();

	virtual void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

			status_t			Revert();
			bool				IsRevertable() const;

			void				ConfigurationUpdated(const BMessage& message);

private:
			void				_EnableFields(bool enable);
			void				_UpdateFields();
			void				_SetModeField(uint32 mode);
			void				_UpdateSettings();
			uint32				_Mode() const;

			void				_ConfigureAddress(
									BNetworkInterfaceAddressSettings& address);
			void				_SetAddress(BNetworkAddress& address,
									const char* text);

private:
			int					fFamily;
			BNetworkInterface	fInterface;
			BNetworkSettings&	fSettings;
			uint32				fLastMode;

			BMessage			fOriginalSettings;

			BPopUpMenu*			fModePopUpMenu;
			BMenuField*			fModeField;
			IPAddressControl*	fAddressField;
			IPAddressControl*	fNetmaskField;
			IPAddressControl*	fGatewayField;
			BButton*			fApplyButton;
};


#endif // INTERFACE_ADDRESS_VIEW_H
