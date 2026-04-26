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
 * MIT License. Copyright 2004-2010, Haiku, Inc.
 * Original authors: Andrew McCall, Julun.
 */

/** @file TimeWindow.h
    @brief Tabbed top-level window of the Time & Date preference panel. */

#ifndef _TIME_WINDOW_H
#define _TIME_WINDOW_H


#include <Window.h>


class BMessage;
class BTabView;
class ClockView;
class DateTimeView;
class NetworkTimeView;
class TimeZoneView;
class TTimeBaseView;


/**
 * @brief Tabbed preference window combining all Time & Date editors.
 *
 * Owns four pages (Date and time, Time zone, Network time, Clock) plus a
 * Revert button that rolls every page back to its original state. Drives
 * one-second pulses through TTimeBaseView so that the visible clock keeps
 * ticking.
 */
class TTimeWindow : public BWindow {
public:
								TTimeWindow();
	virtual						~TTimeWindow();

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_InitWindow();
			void				_AlignWindow();
			void				_SendTimeChangeFinished();
			void				_SetRevertStatus();

			TTimeBaseView*		fBaseView;

			BTabView*			fTabView;
			DateTimeView*		fDateTimeView;
			TimeZoneView*		fTimeZoneView;
			NetworkTimeView*	fNetworkTimeView;
			ClockView*			fClockView;

			BButton*			fRevertButton;
};


#endif	// _TIME_WINDOW_H

