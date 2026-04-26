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
 *   Copyright 2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, <axeld@pinc-software.de>
 */


/**
 * @file ServiceView.cpp
 * @brief Implementation of ServiceView, the detail pane for a single
 *        network service in the Network preflet.
 *
 * Shows a bold title, a description block, and an Enable/Disable button.
 * The button is briefly disabled after each toggle to debounce rapid
 * clicks while the underlying service starts or stops.
 */


#include "ServiceView.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <MessageRunner.h>
#include <StringView.h>
#include <TextView.h>


/** @brief Button-press message that toggles the service on or off. */
static const uint32 kMsgToggleService = 'tgls';
/** @brief Self-message that re-enables the toggle button after the
           debounce window. */
static const uint32 kMsgEnableToggleButton = 'entg';

/** @brief Duration in microseconds the toggle button stays disabled after
           a click to debounce rapid presses (500 ms). */
static const bigtime_t kDisableDuration = 500000;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ServiceView"


/**
 * @brief Builds the title, description, and toggle layout, and snapshots
 *        the initial enabled state.
 *
 * @param name        Service identifier used by BNetworkSettings.
 * @param executable  Path to the service executable for service creation.
 * @param title       Bold heading drawn at the top of the view.
 * @param description Long-form description shown beneath the title.
 * @param settings    Live network settings backing this service.
 */
ServiceView::ServiceView(const char* name, const char* executable,
	const char* title, const char* description, BNetworkSettings& settings)
	:
	BGroupView(B_VERTICAL),
	fName(name),
	fExecutable(executable),
	fSettings(settings)
{
	BStringView* titleView = new BStringView("service", title);
	titleView->SetFont(be_bold_font);
	titleView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BTextView* descriptionView = new BTextView("description");
	descriptionView->SetText(description);
	descriptionView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	descriptionView->MakeEditable(false);

	fEnableButton = new BButton("toggler", B_TRANSLATE("Enable"),
		new BMessage(kMsgToggleService));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.Add(titleView)
		.Add(descriptionView)
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fEnableButton);

	SetExplicitMinSize(BSize(200, B_SIZE_UNSET));
	_UpdateEnableButton();

	fWasEnabled = IsEnabled();
}


/**
 * @brief Destructor.
 */
ServiceView::~ServiceView()
{
}


/**
 * @brief Reports whether the user has toggled the service since this view
 *        was constructed.
 *
 * @return true if the live state differs from the snapshot.
 */
bool
ServiceView::IsRevertable() const
{
	return IsEnabled() != fWasEnabled;
}


/**
 * @brief Restores the service to its initial enabled/disabled state.
 *
 * @return Always B_OK.
 */
status_t
ServiceView::Revert()
{
	if (IsRevertable())
		_Toggle();

	return B_OK;
}


/**
 * @brief Refreshes the toggle button label when service settings change.
 *
 * @param which  Settings category that changed.
 */
void
ServiceView::SettingsUpdated(uint32 which)
{
	if (which == BNetworkSettings::kMsgServiceSettingsUpdated)
		_UpdateEnableButton();
}


/**
 * @brief Retargets the toggle button to this view once attached.
 */
void
ServiceView::AttachedToWindow()
{
	fEnableButton->SetTarget(this);
}


/**
 * @brief Handles the toggle press and the deferred re-enable message.
 *
 * @param message  Incoming BMessage.
 */
void
ServiceView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgToggleService:
			_Toggle();
			break;

		case kMsgEnableToggleButton:
			fEnableButton->SetEnabled(true);
			_UpdateEnableButton();
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Reports whether the service is currently running.
 *
 * @return true when the service is alive in the network kit.
 */
bool
ServiceView::IsEnabled() const
{
	return fSettings.Service(fName).IsRunning();
}


/**
 * @brief Adds the service to the live settings, starting it.
 *
 * Builds a BNetworkServiceSettings record around fName/fExecutable and
 * inserts it via BNetworkSettings::AddService.
 */
void
ServiceView::Enable()
{
	BNetworkServiceSettings settings;
	settings.SetName(fName);
	settings.AddArgument(fExecutable);

	BMessage service;
	if (settings.GetMessage(service) == B_OK)
		fSettings.AddService(service);
}


/**
 * @brief Removes the service from the live settings, stopping it.
 */
void
ServiceView::Disable()
{
	fSettings.RemoveService(fName);
}


/**
 * @brief Flips the running state and starts a debounce timer.
 *
 * The button is disabled and a single-shot kMsgEnableToggleButton
 * notification is posted so the user can't click again before the service
 * has settled.
 */
void
ServiceView::_Toggle()
{
	if (IsEnabled())
		Disable();
	else
		Enable();

	fEnableButton->SetEnabled(false);
	BMessage reenable(kMsgEnableToggleButton);
	BMessageRunner::StartSending(this, &reenable, kDisableDuration, 1);
}


/**
 * @brief Synchronizes the button label with the current running state.
 */
void
ServiceView::_UpdateEnableButton()
{
	fEnableButton->SetLabel(IsEnabled()
		? B_TRANSLATE("Disable") : B_TRANSLATE("Enable"));
}
