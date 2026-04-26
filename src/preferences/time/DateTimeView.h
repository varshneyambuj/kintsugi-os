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
 * MIT License. Copyright 2004-2011, Haiku, Inc.
 * Original authors: Andrew McCall, Mike Berg, Julun, Philippe Saint-Pierre,
 *                   Hamish Morrison.
 */

/** @file DateTimeView.h
    @brief Date and time preference page combining calendar, clock, and edits. */

#ifndef _DATE_TIME_VIEW_H
#define _DATE_TIME_VIEW_H


#include <LayoutBuilder.h>


class TAnalogClock;


namespace BPrivate {
	class BCalendarView;
	class DateEdit;
	class TimeEdit;
}
using BPrivate::BCalendarView;
using BPrivate::DateEdit;
using BPrivate::TimeEdit;


/**
 * @brief Preference page that exposes the system clock and calendar.
 *
 * Hosts a calendar widget, date/time spin edits, and an interactive analog
 * clock. Records the wall-clock time at launch so Revert can restore the
 * original moment plus the elapsed running time.
 */
class DateTimeView : public BGroupView {
public:
								DateTimeView(const char* name);
	virtual 					~DateTimeView();

	virtual	void			 	AttachedToWindow();
	virtual	void 				MessageReceived(BMessage* message);

			bool				CheckCanRevert();

private:
			void 				_InitView();
			void 				_UpdateDateTime(BMessage* message);
			void				_Revert();
			time_t				_PrefletUptime() const;

			DateEdit*			fDateEdit;
			TimeEdit*			fTimeEdit;
			BCalendarView*		fCalendarView;
			TAnalogClock*		fClock;

			bool				fInitialized;

			time_t				fTimeAtStart;
			bigtime_t			fSystemTimeAtStart;
};


#endif	// _DATE_TIME_VIEW_H
