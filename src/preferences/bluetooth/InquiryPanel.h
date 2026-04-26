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

/** @file InquiryPanel.h
    @brief Declares InquiryPanel, the device-discovery floating window. */

#ifndef INQUIRY_WINDOW_H
#define INQUIRY_WINDOW_H

#include <Application.h>
#include <Button.h>
#include <Window.h>
#include <Message.h>
#include <TabView.h>

class BStatusBar;
class BButton;
class BTextView;
class BListView;
class BScrollView;
namespace Bluetooth {
class LocalDevice;
class DiscoveryAgent;
class DiscoveryListener;
}


/**
 * @brief Floating BWindow that performs an inquiry against a LocalDevice.
 *
 * Drives the discovery state machine through cooperating BMessageRunners,
 * shows progress in a BStatusBar, and lets the user forward selected
 * remote devices into the main remote-devices list.
 */
class InquiryPanel : public BWindow
{
public:
			InquiryPanel(BRect frame, LocalDevice* lDevice = NULL);
	bool	QuitRequested(void);
	void	MessageReceived(BMessage *message);

private:
	BStatusBar*				fScanProgress;
	BButton*				fAddButton;
	BButton*				fInquiryButton;
	BTextView*				fMessage;
	BListView*				fRemoteList;
	BScrollView*			fScrollView;
	BMessage*				fRetrieveMessage;
	BMessage*				fSecondsMessage;
	BMessenger				fMessenger;

	bool					fScanning;
	bool					fRetrieving;
	Bluetooth::LocalDevice*	fLocalDevice;
	Bluetooth::DiscoveryAgent* fDiscoveryAgent;
	Bluetooth::DiscoveryListener* fDiscoveryListener;

	void UpdateListStatus(void);

	rgb_color				activeColor;
};

#endif
