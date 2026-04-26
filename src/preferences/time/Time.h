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
 * MIT License. Copyright 2002-2010, Haiku.
 * Original authors: Andrew McCall, Mike Berg, Julun.
 */

/** @file Time.h
    @brief BApplication subclass for the Time & Date preference panel. */

#ifndef _TIME_H
#define _TIME_H


#include <Application.h>


class BMessage;
class TTimeWindow;


/**
 * @brief Top-level BApplication that owns the Time & Date window.
 *
 * Displays the preference UI, services the About dialog, and forwards
 * external commands (clock-tab selection, show/hide of the deskbar clock,
 * locale changes) to the preference window.
 */
class TimeApplication : public BApplication {
public:
								TimeApplication();
	virtual						~TimeApplication();

	virtual void				ReadyToRun();
	virtual void				AboutRequested();

	virtual void				MessageReceived(BMessage* message);

private:
			TTimeWindow*		fWindow;
};


#endif	// _TIME_H

