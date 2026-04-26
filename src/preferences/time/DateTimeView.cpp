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
 *       Mike Berg <mike@berg-net.us>
 *       Julun <host.haiku@gmx.de>
 *       Philippe Saint-Pierre <stpere@gmail.com>
 *       Hamish Morrison <hamish@lavabit.com>
 */


/**
 * @file DateTimeView.cpp
 * @brief Implementation of DateTimeView, the Date and time preference page.
 *
 * Builds the calendar / date-edit / time-edit / analog-clock layout, listens
 * for clock-tick notices to refresh visible widgets, and translates user
 * gestures (calendar selection, drag on the analog clock, edits in the spin
 * controls) into H_USER_CHANGE messages handled by the parent window.
 */


#include "DateTimeView.h"

#include <time.h>
#include <syscalls.h>

#include <Box.h>
#include <CalendarView.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <DateTime.h>
#include <DateTimeEdit.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <LocaleRoster.h>
#include <Message.h>
#include <Path.h>
#include <StringView.h>
#include <Window.h>

#include "AnalogClock.h"
#include "TimeMessages.h"
#include "TimeWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Time"


using BPrivate::BCalendarView;
using BPrivate::BDateTime;
using BPrivate::B_LOCAL_TIME;
using BPrivate::DateEdit;
using BPrivate::TimeEdit;


/**
 * @brief Constructs the page, builds its layout, and snapshots the launch time.
 *
 * Records both wall-clock and monotonic system time so Revert can compute
 * the equivalent moment after an arbitrary delay.
 *
 * @param name View name passed to BGroupView.
 */
DateTimeView::DateTimeView(const char* name)
	:
	BGroupView(name, B_HORIZONTAL, 5),
	fInitialized(false),
	fSystemTimeAtStart(system_time())
{
	_InitView();

	// record the current time to enable revert.
	time(&fTimeAtStart);
}


/**
 * @brief Destructor; child views are owned by the layout.
 */
DateTimeView::~DateTimeView()
{
}


/**
 * @brief Adopts parent colors and binds the calendar to this view as target.
 */
void
DateTimeView::AttachedToWindow()
{
	AdoptParentColors();

	if (!fInitialized) {
		fInitialized = true;

		fCalendarView->SetTarget(this);
	}
}


/**
 * @brief Handles ticks, locale changes, calendar clicks, and Revert.
 *
 * Refreshes the on-screen widgets from H_TM_CHANGED notices, rebuilds the
 * calendar header on B_LOCALE_CHANGED, posts H_USER_CHANGE on calendar
 * selection, restores the launch-time moment on Revert, and clears the
 * analog clock's drag state on kChangeTimeFinished.
 *
 * @param message Incoming message.
 */
void
DateTimeView::MessageReceived(BMessage* message)
{
	int32 change;
	switch (message->what) {
		case B_OBSERVER_NOTICE_CHANGE:
			message->FindInt32(B_OBSERVE_WHAT_CHANGE, &change);
			switch (change) {
				case H_TM_CHANGED:
					_UpdateDateTime(message);
					break;

				default:
					BView::MessageReceived(message);
					break;
			}
			break;

		case B_LOCALE_CHANGED:
			fCalendarView->UpdateDayNameHeader();
			break;

		case kDayChanged:
		{
			BMessage msg(*message);
			msg.what = H_USER_CHANGE;
			msg.AddBool("time", false);
			Window()->PostMessage(&msg);
			break;
		}

		case kMsgRevert:
			_Revert();
			break;

		case kChangeTimeFinished:
			if (fClock->IsChangingTime())
				fTimeEdit->MakeFocus(false);
			fClock->ChangeTimeFinished();
			break;

		case kRTCUpdate:
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Returns true when the wall clock has drifted from the
 *        launch-time-plus-uptime baseline.
 */
bool
DateTimeView::CheckCanRevert()
{
	// check for changed time
	time_t unchangedNow = fTimeAtStart + _PrefletUptime();
	time_t changedNow;
	time(&changedNow);

	return changedNow != unchangedNow;
}


/**
 * @brief Restores the system clock to the launch-time moment plus uptime.
 *
 * Computes (fTimeAtStart + uptime), splits into local-time fields, and
 * pushes the result back via set_real_time_clock().
 */
void
DateTimeView::_Revert()
{
	// Set the clock and calendar as they were at launch time +
	// time elapsed since application launch.

	time_t timeNow = fTimeAtStart + _PrefletUptime();
	struct tm result;
	struct tm* timeInfo;
	timeInfo = localtime_r(&timeNow, &result);

	BDateTime dateTime = BDateTime::CurrentDateTime(B_LOCAL_TIME);
	BTime time = dateTime.Time();
	BDate date = dateTime.Date();
	time.SetTime(timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec % 60);
	date.SetDate(timeInfo->tm_year + 1900, timeInfo->tm_mon + 1,
		timeInfo->tm_mday);
	dateTime.SetTime(time);
	dateTime.SetDate(date);

	set_real_time_clock(dateTime.Time_t());
}


/**
 * @brief Returns seconds elapsed since the page was created.
 */
time_t
DateTimeView::_PrefletUptime() const
{
	return (time_t)((system_time() - fSystemTimeAtStart) / 1000000);
}


/**
 * @brief Builds the calendar, date edit, time edit, and analog clock.
 *
 * Splits the page into two side-by-side columns separated by a thin
 * BBox divider: date controls on the left, time controls on the right.
 */
void
DateTimeView::_InitView()
{
	fCalendarView = new BCalendarView("calendar");
	fCalendarView->SetWeekNumberHeaderVisible(false);
	fCalendarView->SetSelectionMessage(new BMessage(kDayChanged));
	fCalendarView->SetInvocationMessage(new BMessage(kDayChanged));

	fDateEdit = new DateEdit("dateEdit", 3, new BMessage(H_USER_CHANGE));
	fTimeEdit = new TimeEdit("timeEdit", 5, new BMessage(H_USER_CHANGE));
	fClock = new TAnalogClock("analogClock");

	BTime time(BTime::CurrentTime(B_LOCAL_TIME));
	fClock->SetTime(time.Hour(), time.Minute(), time.Second());

	BBox* divider = new BBox(BRect(0, 0, 1, 1),
		B_EMPTY_STRING, B_FOLLOW_ALL_SIDES,
		B_WILL_DRAW | B_FRAME_EVENTS, B_FANCY_BORDER);
	divider->SetExplicitMaxSize(BSize(1, B_SIZE_UNLIMITED));

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, B_USE_DEFAULT_SPACING)
		.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING)
			.Add(fDateEdit)
			.Add(fCalendarView)
		.End()
		.Add(divider)
		.AddGroup(B_VERTICAL)
			.Add(fTimeEdit)
			.Add(fClock)
		.End()
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING,
			B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING);
}


/**
 * @brief Pushes the date and time fields from a notice message into the UI.
 *
 * Date fields are coalesced via static "last seen" values so the calendar
 * does not redraw on every tick. Time fields are forwarded unconditionally.
 *
 * @param message Notice message with day/month/year and hour/minute/second.
 */
void
DateTimeView::_UpdateDateTime(BMessage* message)
{
	int32 day;
	int32 month;
	int32 year;
	if (message->FindInt32("month", &month) == B_OK
		&& message->FindInt32("day", &day) == B_OK
		&& message->FindInt32("year", &year) == B_OK) {
		static int32 lastDay;
		static int32 lastMonth;
		static int32 lastYear;
		if (day != lastDay || month != lastMonth || year != lastYear) {
			fDateEdit->SetDate(year, month, day);
			fCalendarView->SetDate(year, month, day);
			lastDay = day;
			lastMonth = month;
			lastYear = year;
		}
	}

	int32 hour;
	int32 minute;
	int32 second;
	if (message->FindInt32("hour", &hour) == B_OK
		&& message->FindInt32("minute", &minute) == B_OK
		&& message->FindInt32("second", &second) == B_OK) {
		fClock->SetTime(hour, minute, second);
		fTimeEdit->SetTime(hour, minute, second);
	}
}

