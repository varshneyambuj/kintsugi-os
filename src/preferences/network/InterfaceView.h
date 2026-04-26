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
 * Original authors: Axel Dörfler, Alexander von Gluck, John Scipione.
 */

/** @file InterfaceView.h
    @brief Interface-status view shown in the Network preflet (link state,
           MAC, link speed, throughput, wireless network picker, on/off and
           renegotiate buttons). */

#ifndef INTERFACE_VIEW_H
#define INTERFACE_VIEW_H


#include <GroupView.h>
#include <NetworkInterface.h>


class BButton;
class BMenuField;
class BMessage;
class BStringView;


/**
 * @brief Live status pane for a single network interface.
 *
 * Polled via Pulse() at one-second cadence; refreshes status fields and,
 * for wireless devices, repopulates the network selection menu every fifth
 * pulse.
 */
class InterfaceView : public BGroupView {
public:
								InterfaceView();
	virtual						~InterfaceView();

			void				SetTo(const char* name);

	virtual	void				MessageReceived(BMessage* message);
	virtual void				AttachedToWindow();
	virtual	void				Pulse();

private:
			status_t			_Update(bool updateWirelessNetworks = true);
			void				_EnableFields(bool enabled);

private:
			BNetworkInterface	fInterface;
			int					fPulseCount;

			BStringView*		fStatusField;
			BStringView*		fMacAddressField;
			BStringView*		fLinkSpeedField;
			BStringView*		fLinkTxField;
			BStringView*		fLinkRxField;

			BMenuField*			fNetworkMenuField;

			BButton*			fToggleButton;
			BButton*			fRenegotiateButton;
};


#endif // INTERFACE_HARDWARE_VIEW_H

