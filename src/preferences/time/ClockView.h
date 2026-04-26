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
 * Original author: John Scipione.
 */

/** @file ClockView.h
    @brief Tab page for configuring the Deskbar's clock display. */

#ifndef _CLOCK_VIEW_H
#define _CLOCK_VIEW_H


#include <View.h>


class BCheckBox;
class BRadioButton;


/**
 * @brief Preference page that toggles Deskbar clock display options.
 *
 * Mirrors four boolean settings owned by the Deskbar (show clock, show
 * seconds, show day-of-week, show time-zone). Initial state is fetched
 * asynchronously from the Deskbar and changes are pushed back via the
 * matching kShowHideTime / kShowSeconds / etc. messages.
 */
class ClockView : public BView {
public:
								ClockView(const char* name);
	virtual 					~ClockView();

	virtual	void			 	AttachedToWindow();
	virtual	void 				MessageReceived(BMessage* message);

			bool				CheckCanRevert();

private:
			void				_Revert();

			BCheckBox*			fShowClock;
			BCheckBox*			fShowSeconds;
			BCheckBox*			fShowDayOfWeek;
			BCheckBox*			fShowTimeZone;

			int32				fCachedShowClock;
			int32				fCachedShowSeconds;
			int32				fCachedShowDayOfWeek;
			int32				fCachedShowTimeZone;
};


#endif	// _CLOCK_VIEW_H
