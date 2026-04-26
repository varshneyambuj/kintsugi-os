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
 * Original authors: Andrew McCall, Mike Berg, Julun, Philippe Saint-Pierre.
 */

/** @file TimeMessages.h
    @brief BMessage 'what' codes shared between the Time preference views. */

#ifndef _TIME_MESSAGES_H
#define _TIME_MESSAGES_H


/** @brief Posted by ZoneView when the user picks a different city. */
const uint32 H_CITY_CHANGED = 'h_CC';
/** @brief Posted by ZoneView when the user commits a new city selection. */
const uint32 H_CITY_SET = 'h_CS';

/** @brief Posted by the Apply/Set button on the time-zone tab. */
const uint32 H_SET_TIME_ZONE = 'hSTZ';

/** @brief Selects between local time and UTC for the RTC interpretation. */
const uint32 RTC_SETTINGS = 'RTse';

/** @brief Periodic clock-tick message emitted by TTimeBaseView::Pulse(). */
const uint32 H_TIME_UPDATE ='obTU';

/** @brief Notice broadcast on a clock tick to observers. */
const uint32 H_TM_CHANGED = 'obTC';

/** @brief Notice broadcast when the user explicitly changes the clock. */
const uint32 H_USER_CHANGE = 'obUC';

/** @brief Switches between RTC-as-local-time and RTC-as-GMT. */
const uint32 kRTCUpdate = '_rtc';

/** @brief Sets whether the calendar week begins on Sunday or Monday. */
const uint32 kWeekStart = '_kws';

/** @brief Posted when the user clicks a day cell in the calendar view. */
const uint32 kDayChanged = '_kdc';

/** @brief Posted when the user clicks the Revert button. */
const uint32 kMsgRevert = 'rvrt';

/** @brief Generic notification that a setting has changed. */
const uint32 kMsgChange = 'chng';

/** @brief Sent to release the analog clock from drag-edit mode. */
const uint32 kChangeTimeFinished = 'tcfi';

/** @brief Toggles whether the Deskbar shows the clock at all. */
const uint32 kShowHideTime = 'ShTm';

/** @brief Toggles seconds in the Deskbar clock display. */
const uint32 kShowSeconds = 'SwSc';

/** @brief Toggles the day-of-week label in the Deskbar clock. */
const uint32 kShowDayOfWeek = 'SwDw';

/** @brief Toggles the time-zone abbreviation in the Deskbar clock. */
const uint32 kShowTimeZone = 'SwTz';

/** @brief Asks the Deskbar to send back its current clock settings. */
const uint32 kGetClockSettings = 'GCkS';

/** @brief Brings the Clock tab to the front of the preference window. */
const uint32 kSelectClockTab = 'SlCk';

#endif	// _TIME_MESSAGES_H

