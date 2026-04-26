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
 *   Copyright 2008-2009, Oliver Ruiz Dorantes,
 *       <oliver.ruiz.dorantes@gmail.com>
 *   Copyright 2021, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Fredrik Modéen <fredrik_at_modeen.se>
 */


/**
 * @file InquiryPanel.cpp
 * @brief Implementation of InquiryPanel, the device-discovery floating window.
 *
 * InquiryPanel orchestrates the BT_GIAC inquiry against the chosen
 * LocalDevice, displays a progress bar driven by per-second BMessageRunner
 * ticks, lists each discovered RemoteDevice as a DeviceListItem, then
 * walks the result set retrieving friendly names. Selected devices can be
 * forwarded to the main remote-devices list via kMsgAddToRemoteList.
 *
 * @see RemoteDevicesView, DiscoveryAgent
 */


#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <ListItem.h>
#include <MessageRunner.h>
#include <ScrollView.h>
#include <StatusBar.h>
#include <SpaceLayoutItem.h>
#include <TextView.h>
#include <TabView.h>

#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/DiscoveryAgent.h>
#include <bluetooth/DiscoveryListener.h>
#include <bluetooth/LocalDevice.h>

#include "defs.h"
#include "DeviceListItem.h"
#include "InquiryPanel.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Inquiry panel"

using Bluetooth::DeviceListItem;

// private funcionaility provided by kit
extern uint8 GetInquiryTime();

/** @brief Internal message: discovery agent reported InquiryStarted. */
static const uint32 kMsgStart = 'InSt';
/** @brief Internal message: discovery agent reported InquiryCompleted. */
static const uint32 kMsgFinish = 'InFn';
static const uint32 kMsgShowDebug = 'ShDG';

/** @brief Window message: user pressed the Inquiry button. */
static const uint32 kMsgInquiry = 'iQbt';
/** @brief Internal message: append a DeviceListItem to the result list. */
static const uint32 kMsgAddListDevice = 'aDdv';

/** @brief List-view selection-changed notification. */
static const uint32 kMsgSelected = 'isLt';
/** @brief Per-second tick used to update the scan progress bar. */
static const uint32 kMsgSecond = 'sCMs';
/** @brief Tick used to drive friendly-name retrieval after the scan. */
static const uint32 kMsgRetrieve = 'IrEt';


/**
 * @brief Bluetooth kit listener that forwards discovery events to the panel.
 *
 * PanelDiscoveryListener subclasses DiscoveryListener so that asynchronous
 * inquiry callbacks from the kit are turned into BMessages posted to the
 * owning InquiryPanel, keeping all UI work on the panel's looper thread.
 */
class PanelDiscoveryListener : public DiscoveryListener {

public:

	/**
	 * @brief Constructs the listener bound to a single InquiryPanel.
	 *
	 * @param iPanel  Panel that should receive translated discovery events.
	 */
	PanelDiscoveryListener(InquiryPanel* iPanel)
		:
		DiscoveryListener(),
		fInquiryPanel(iPanel)
	{

	}


	/**
	 * @brief Called by the kit when a remote device is discovered.
	 *
	 * Wraps @a btDevice in a Bluetooth::DeviceListItem and posts a
	 * kMsgAddListDevice message to the panel.
	 *
	 * @param btDevice  Newly discovered remote device.
	 * @param cod       Class of device reported by the remote.
	 */
	void
	DeviceDiscovered(RemoteDevice* btDevice, DeviceClass cod)
	{
		BMessage* message = new BMessage(kMsgAddListDevice);
		message->AddPointer("remoteItem", new DeviceListItem(btDevice));
		fInquiryPanel->PostMessage(message);
	}


	/**
	 * @brief Called by the kit when the inquiry phase finishes.
	 *
	 * @param discType  Discovery termination type (unused here).
	 */
	void
	InquiryCompleted(int discType)
	{
		BMessage* message = new BMessage(kMsgFinish);
		fInquiryPanel->PostMessage(message);
	}


	/**
	 * @brief Called by the kit when the inquiry has actually started.
	 *
	 * @param status  Status code from the kit (unused here).
	 */
	void
	InquiryStarted(status_t status)
	{
		BMessage* message = new BMessage(kMsgStart);
		fInquiryPanel->PostMessage(message);
	}

private:
	InquiryPanel*	fInquiryPanel;

};


/**
 * @brief Constructs the inquiry floating window.
 *
 * Builds the status bar, the description BTextView, the result BListView
 * inside a BScrollView, and the Inquiry/Add buttons. If a LocalDevice is
 * available the panel is wired to its DiscoveryAgent; otherwise the
 * description explains that no Bluetooth adapter is available and the
 * Inquiry button is disabled.
 *
 * @param frame    Initial window rectangle.
 * @param lDevice  LocalDevice to scan with; if NULL, falls back to
 *                 LocalDevice::GetLocalDevice().
 */
InquiryPanel::InquiryPanel(BRect frame, LocalDevice* lDevice)
	:
	BWindow(frame, B_TRANSLATE_SYSTEM_NAME("Bluetooth"), B_FLOATING_WINDOW,
	B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS,	B_ALL_WORKSPACES ),
	fMessenger(this),
 	fScanning(false),
 	fRetrieving(false),
	fLocalDevice(lDevice)

{
	fScanProgress = new BStatusBar("status",
		B_TRANSLATE("Scanning progress"), "");
	activeColor = fScanProgress->BarColor();

	if (fLocalDevice == NULL)
		fLocalDevice = LocalDevice::GetLocalDevice();

	fMessage = new BTextView("description", B_WILL_DRAW);
	fMessage->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fMessage->SetLowColor(fMessage->ViewColor());
	fMessage->MakeEditable(false);
	fMessage->MakeSelectable(false);

	fInquiryButton = new BButton("Inquiry", B_TRANSLATE("Inquiry"),
		new BMessage(kMsgInquiry), B_WILL_DRAW);

	fAddButton = new BButton("add", B_TRANSLATE("Add device to list"),
		new BMessage(kMsgAddToRemoteList), B_WILL_DRAW);
	fAddButton->SetEnabled(false);

	fRemoteList = new BListView("AttributeList", B_SINGLE_SELECTION_LIST);
	fRemoteList->SetSelectionMessage(new BMessage(kMsgSelected));

	fScrollView = new BScrollView("ScrollView", fRemoteList, 0, false, true);
	fScrollView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	if (fLocalDevice != NULL) {
		fMessage->SetText(B_TRANSLATE(
			"Check that the Bluetooth capabilities of your"
			" remote device are activated. Press 'Inquiry' to start scanning."
			" The needed time for the retrieval of the names is unknown, "
			"although should not take more than 3 seconds per device. "
			"Afterwards you will be able to add them to your main list,"
			" where you will be able to pair with them."));
		fInquiryButton->SetEnabled(true);
		fDiscoveryAgent = fLocalDevice->GetDiscoveryAgent();
		fDiscoveryListener = new PanelDiscoveryListener(this);

		SetTitle((const char*)(fLocalDevice->GetFriendlyName().String()));
	} else {
		fMessage->SetText(B_TRANSLATE("There isn't any Bluetooth LocalDevice "
			"registered on the system."));
		fInquiryButton->SetEnabled(false);
	}

	fRetrieveMessage = new BMessage(kMsgRetrieve);
	fSecondsMessage = new BMessage(kMsgSecond);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(B_USE_SMALL_SPACING)
		.Add(fMessage, 0)
		.Add(fScanProgress, 10)
		.Add(fScrollView, 20)
		.AddGroup(B_HORIZONTAL, 10)
			.Add(fAddButton)
			.AddGlue()
			.Add(fInquiryButton)
		.End()
	.End();
}


/**
 * @brief Drives the inquiry/retrieval state machine.
 *
 * Implements the per-second progress tick, the friendly-name retrieval
 * after the inquiry completes, the kMsgInquiry start request, and
 * kMsgAddToRemoteList forwarding back to the main application. Static
 * locals carry the small amount of progress state across messages.
 *
 * @param message  Incoming BMessage. Unhandled messages fall through to
 *                 BWindow::MessageReceived.
 */
void
InquiryPanel::MessageReceived(BMessage* message)
{
	static float timer = 0; // expected time of the inquiry process
	static float scanningTime = 0;
	static int32 retrievalIndex = 0;
	static bool labelPlaced = false;

	switch (message->what) {
		case kMsgInquiry:

			fDiscoveryAgent->StartInquiry(BT_GIAC, fDiscoveryListener, GetInquiryTime());

			timer = BT_BASE_INQUIRY_TIME * GetInquiryTime() + 1;
			// does it works as expected?
			fScanProgress->SetMaxValue(timer);

		break;

		case kMsgAddListDevice:
		{
			DeviceListItem* listItem;

			message->FindPointer("remoteItem", (void **)&listItem);

			fRemoteList->AddItem(listItem);
		}
		break;

		case kMsgAddToRemoteList:
		{
			message->PrintToStream();
			int32 index = fRemoteList->CurrentSelection(0);
			DeviceListItem* item = (DeviceListItem*) fRemoteList->RemoveItem(index);;

			BMessage message(kMsgAddToRemoteList);
			message.AddPointer("device", item);

			be_app->PostMessage(&message);
			// TODO: all others listitems can be deleted
		}
		break;

		case kMsgSelected:
			UpdateListStatus();
		break;

		case kMsgStart:
			fRemoteList->MakeEmpty();
			fScanProgress->Reset();
			fScanProgress->SetTo(1);
			fScanProgress->SetTrailingText(B_TRANSLATE("Starting scan"
				B_UTF8_ELLIPSIS));
			fScanProgress->SetBarColor(activeColor);

			fAddButton->SetEnabled(false);
			fInquiryButton->SetEnabled(false);

			BMessageRunner::StartSending(fMessenger, fSecondsMessage, 1000000, timer);

			scanningTime = 1;
			fScanning = true;

		break;

		case kMsgFinish:

			retrievalIndex = 0;
			fScanning = false;
			fRetrieving = true;
			labelPlaced = false;
			fScanProgress->SetTo(100);
			fScanProgress->SetTrailingText(B_TRANSLATE("Retrieving names"
				B_UTF8_ELLIPSIS));
			BMessageRunner::StartSending(fMessenger, fRetrieveMessage, 1000000, 1);

		break;

		case kMsgSecond:
			if (fScanning && scanningTime < timer) {
				// TODO time formatting could use Locale Kit

				// TODO should not be needed if SetMaxValue works...
				fScanProgress->SetTo(scanningTime * 100 / timer);
				BString elapsedTime = B_TRANSLATE("Remaining %1 seconds");

				BString seconds("");
				seconds << (int)(timer - scanningTime);

				elapsedTime.ReplaceFirst("%1", seconds.String());
				fScanProgress->SetTrailingText(elapsedTime.String());

				scanningTime = scanningTime + 1;
			}
		break;

		case kMsgRetrieve:

			if (fRetrieving) {

				if (retrievalIndex < fDiscoveryAgent->RetrieveDevices(0).CountItems()) {

					if (!labelPlaced) {

						labelPlaced = true;
						BString progressText(B_TRANSLATE("Retrieving name of %1"));

						BString namestr;
						namestr << bdaddrUtils::ToString(fDiscoveryAgent
							->RetrieveDevices(0).ItemAt(retrievalIndex)
							->GetBluetoothAddress());
						progressText.ReplaceFirst("%1", namestr.String());
						fScanProgress->SetTrailingText(progressText.String());

					} else {
						// Really erally expensive operation should be done in a separate thread
						// once Haiku gets a BarberPole in API replacing the progress bar
						((DeviceListItem*)fRemoteList->ItemAt(retrievalIndex))
							->SetDevice(fDiscoveryAgent->RetrieveDevices(0).ItemAt(retrievalIndex));
						fRemoteList->InvalidateItem(retrievalIndex);

						retrievalIndex++;
						labelPlaced = false;
					}

					BMessageRunner::StartSending(fMessenger, fRetrieveMessage, 500000, 1);

				} else {

					fRetrieving = false;
					retrievalIndex = 0;

					fScanProgress->SetBarColor(
						ui_color(B_PANEL_BACKGROUND_COLOR));
					fScanProgress->SetTrailingText(
						B_TRANSLATE("Scanning completed."));
					fInquiryButton->SetEnabled(true);
					UpdateListStatus();
				}
			}

		break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


/**
 * @brief Enables or disables the Add button based on list/scan state.
 *
 * The Add button is only meaningful when an item is selected and no
 * scan or name-retrieval pass is currently running.
 */
void
InquiryPanel::UpdateListStatus(void)
{
	if (fRemoteList->CurrentSelection() < 0 || fScanning || fRetrieving)
		fAddButton->SetEnabled(false);
	else
		fAddButton->SetEnabled(true);
}


/**
 * @brief Allows the inquiry window to close unconditionally.
 *
 * @return Always true.
 */
bool
InquiryPanel::QuitRequested(void)
{

	return true;
}
