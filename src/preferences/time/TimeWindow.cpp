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
 *   Copyright 2004-2011, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Andrew McCall <mccall@@digitalparadise.co.uk>
 *       Julun <host.haiku@gmx.de>
 *       Hamish Morrison <hamish@lavabit.com>
 */


/**
 * @file TimeWindow.cpp
 * @brief Implementation of TTimeWindow, the tabbed Time & Date window.
 *
 * Builds the four preference tabs (Date and time, Time zone, Network time,
 * Clock), wires them to a shared TTimeBaseView pulse source, and routes
 * Revert / RTC / change messages through the active page.
 */


#include "TimeWindow.h"

#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <LocaleRoster.h>
#include <Message.h>
#include <Screen.h>
#include <SeparatorView.h>
#include <TabView.h>

#include "BaseView.h"
#include "ClockView.h"
#include "DateTimeView.h"
#include "NetworkTimeView.h"
#include "TimeMessages.h"
#include "TimeSettings.h"
#include "ZoneView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Time"

/**
 * @brief Constructs the Time preference window and restores its position.
 *
 * Sets up the tabbed layout, restores the last-used screen position, and
 * registers a Cmd-A shortcut that triggers the About dialog.
 */
TTimeWindow::TTimeWindow()
	:
	BWindow(BRect(0, 0, 0, 0), B_TRANSLATE_SYSTEM_NAME("Time"), B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
	_InitWindow();
	_AlignWindow();

	AddShortcut('A', B_COMMAND_KEY, new BMessage(B_ABOUT_REQUESTED));
}


/**
 * @brief Destructor; subviews are owned by the layout.
 */
TTimeWindow::~TTimeWindow()
{
}


/**
 * @brief Saves the window position and tears down observation links on quit.
 *
 * Persists the current frame to TimeSettings, stops the clock-pulse
 * observers held by the time-zone and date/time views, and asks the
 * application to quit.
 *
 * @return Result of BWindow::QuitRequested() (always true here).
 */
bool
TTimeWindow::QuitRequested()
{
	TimeSettings().SetLeftTop(Frame().LeftTop());

	fBaseView->StopWatchingAll(fTimeZoneView);
	fBaseView->StopWatchingAll(fDateTimeView);

	be_app->PostMessage(B_QUIT_REQUESTED);

	return BWindow::QuitRequested();
}


/**
 * @brief Routes preference-related messages to the active page.
 *
 * Dispatches user time changes to the base view, locale changes to the
 * date/time view, and revert / RTC / change messages to the relevant
 * pages. Updates the Revert button's enabled state in response.
 *
 * @param message Incoming message.
 */
void
TTimeWindow::MessageReceived(BMessage* message)
{
	switch(message->what) {
		case H_USER_CHANGE:
			fBaseView->ChangeTime(message);
			// To make sure no old time message is in the queue
			_SendTimeChangeFinished();
			_SetRevertStatus();
			break;

		case B_ABOUT_REQUESTED:
			be_app->PostMessage(B_ABOUT_REQUESTED);
			break;

		case B_LOCALE_CHANGED:
		{
			BLocaleRoster::Default()->Refresh();
			fDateTimeView->MessageReceived(message);
			break;
		}

		case kMsgRevert:
			fDateTimeView->MessageReceived(message);
			fTimeZoneView->MessageReceived(message);
			fNetworkTimeView->MessageReceived(message);
			fClockView->MessageReceived(message);
			fRevertButton->SetEnabled(false);
			break;

		case kRTCUpdate:
			fDateTimeView->MessageReceived(message);
			fTimeZoneView->MessageReceived(message);
			_SetRevertStatus();
			break;

		case kMsgChange:
			_SetRevertStatus();
			break;

		case kSelectClockTab:
			// focus the clock tab (last one)
			fTabView->Select(fTabView->CountTabs() - 1);
			break;

		case kShowHideTime:
			fClockView->MessageReceived(message);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


/**
 * @brief Builds the tabbed layout, all four preference views, and the
 *        Revert button.
 *
 * Configures a 2 Hz pulse rate so the live clock animates smoothly, hooks
 * the date/time and time-zone views into TTimeBaseView's notice channel,
 * and lays everything out vertically with a separator above the bottom
 * button row.
 */
void
TTimeWindow::_InitWindow()
{
	SetPulseRate(500000);

	fDateTimeView = new DateTimeView(B_TRANSLATE("Date and time"));
	fTimeZoneView = new TimeZoneView(B_TRANSLATE("Time zone"));
	fNetworkTimeView = new NetworkTimeView(B_TRANSLATE("Network time"));
	fClockView = new ClockView(B_TRANSLATE("Clock"));

	fBaseView = new TTimeBaseView("baseView");
	fBaseView->StartWatchingAll(fDateTimeView);
	fBaseView->StartWatchingAll(fTimeZoneView);

	fTabView = new BTabView("tabView", B_WIDTH_FROM_WIDEST);
	fTabView->AddTab(fDateTimeView);
	fTabView->AddTab(fTimeZoneView);
	fTabView->AddTab(fNetworkTimeView);
	fTabView->AddTab(fClockView);
	fTabView->SetBorder(B_NO_BORDER);

	fBaseView->AddChild(fTabView);

	fRevertButton = new BButton("revert", B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert));
	fRevertButton->SetEnabled(false);
	fRevertButton->SetTarget(this);
	fRevertButton->SetExplicitAlignment(
		BAlignment(B_ALIGN_LEFT, B_ALIGN_MIDDLE));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0, B_USE_DEFAULT_SPACING, 0, 0)
		.Add(fBaseView)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL)
			.Add(fRevertButton)
			.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
				B_USE_DEFAULT_SPACING, B_USE_WINDOW_SPACING);
}


/**
 * @brief Restores the saved window position, clamped to the visible screen.
 */
void
TTimeWindow::_AlignWindow()
{
	MoveTo(TimeSettings().LeftTop());
	MoveOnScreen();
}


/**
 * @brief Sends a kChangeTimeFinished message to the date/time view.
 *
 * Used after H_USER_CHANGE so the analog clock can release its drag-edit
 * mode and resume tracking the system clock.
 */
void
TTimeWindow::_SendTimeChangeFinished()
{
	BMessenger messenger(fDateTimeView);
	BMessage msg(kChangeTimeFinished);
	messenger.SendMessage(&msg);
}


/**
 * @brief Enables the Revert button when any page has pending changes.
 */
void
TTimeWindow::_SetRevertStatus()
{
	fRevertButton->SetEnabled(fDateTimeView->CheckCanRevert()
		|| fTimeZoneView->CheckCanRevert()
		|| fNetworkTimeView->CheckCanRevert()
		|| fClockView->CheckCanRevert());
}
