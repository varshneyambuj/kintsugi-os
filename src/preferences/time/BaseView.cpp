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
 *   Copyright 2004-2007, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Mike Berg <mike@berg-net.us>
 *       Julun <host.haiku@gmx.de>
 */


/**
 * @file BaseView.cpp
 * @brief Implementation of TTimeBaseView, the pulse and notice hub for the
 *        Time preference window.
 *
 * Drives a periodic Pulse() that broadcasts H_TM_CHANGED notices to any
 * observers (tabs that need to redraw the live clock), and provides a
 * helper that translates an H_USER_CHANGE message into a system clock
 * update.
 */


#include "BaseView.h"

#include <DateTime.h>
#include <OS.h>

#include "TimeMessages.h"


/**
 * @brief Constructs a base view named for layout purposes.
 *
 * Initializes the cached notice message and turns on B_PULSE_NEEDED so the
 * window framework will call Pulse() every PulseRate() microseconds.
 *
 * @param name View name.
 */
TTimeBaseView::TTimeBaseView(const char* name)
	:
	BGroupView(name, B_VERTICAL, 0),
	fMessage(H_TIME_UPDATE)
{
	SetFlags(Flags() | B_PULSE_NEEDED);
}


/**
 * @brief Destructor; nothing to release.
 */
TTimeBaseView::~TTimeBaseView()
{
}


/**
 * @brief Pulse callback; broadcasts H_TM_CHANGED if anyone is observing.
 */
void
TTimeBaseView::Pulse()
{
	if (IsWatched())
		_SendNotices();
}


/**
 * @brief Synchronizes the view's colors with the panel UI palette.
 */
void
TTimeBaseView::AttachedToWindow()
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(ViewUIColor());
}


/**
 * @brief Applies a user-driven date or time change to the system clock.
 *
 * Reads "time" (bool) from the message: when true, hour/minute/second are
 * pulled from the message and combined with today's date; when false,
 * day/month/year override the current date and the time is preserved. The
 * resulting BDateTime is pushed to the kernel via set_real_time_clock().
 *
 * @param message Source message; must contain a bool "time" field. Other
 *                fields are read opportunistically.
 */
void
TTimeBaseView::ChangeTime(BMessage* message)
{
	bool isTime;
	if (message->FindBool("time", &isTime) != B_OK)
		return;

	BDateTime dateTime = BDateTime::CurrentDateTime(B_LOCAL_TIME);

	if (isTime) {
		BTime time = dateTime.Time();
		int32 hour;
		if (message->FindInt32("hour", &hour) != B_OK)
			hour  = time.Hour();

		int32 minute;
		if (message->FindInt32("minute", &minute) != B_OK)
			minute = time.Minute();

		int32 second;
		if (message->FindInt32("second", &second) != B_OK)
			second = time.Second();

		time.SetTime(hour, minute, second);
		dateTime.SetTime(time);
	} else {
		BDate date = dateTime.Date();
		int32 day;
		if (message->FindInt32("day", &day) != B_OK)
			day = date.Day();

		int32 year;
		if (message->FindInt32("year", &year) != B_OK)
			year = date.Year();

		int32 month;
		if (message->FindInt32("month", &month) != B_OK)
			month = date.Month();

		date.SetDate(year, month, day);
		dateTime.SetDate(date);
	}

	set_real_time_clock(dateTime.Time_t());
}


/**
 * @brief Broadcasts the current date and time to all observers.
 *
 * Repacks the cached BMessage with day/month/year and hour/minute/second
 * fields and emits it as an H_TM_CHANGED notice.
 */
void
TTimeBaseView::_SendNotices()
{
	fMessage.MakeEmpty();

	BDate date = BDate::CurrentDate(B_LOCAL_TIME);
	fMessage.AddInt32("day", date.Day());
	fMessage.AddInt32("year", date.Year());
	fMessage.AddInt32("month", date.Month());

	BTime time = BTime::CurrentTime(B_LOCAL_TIME);
	fMessage.AddInt32("hour", time.Hour());
	fMessage.AddInt32("minute", time.Minute());
	fMessage.AddInt32("second", time.Second());

	SendNotices(H_TM_CHANGED, &fMessage);
}

