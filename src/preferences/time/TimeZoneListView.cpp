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
 *   Copyright 2012, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Sean Bailey <ziusudra@gmail.com>
 */


/**
 * @file TimeZoneListView.cpp
 * @brief Implementation of the outline list view that renders time zones.
 *
 * Specialises BOutlineListView so each leaf row exposes a tool tip listing
 * the short and daylight names of the zone along with the current local
 * time and date in that zone. Region and country header rows have no zone
 * data and therefore no tool tip.
 */


#include "TimeZoneListView.h"

#include <new>

#include <Catalog.h>
#include <DateFormat.h>
#include <Locale.h>
#include <String.h>
#include <TimeZone.h>
#include <ToolTip.h>

#include "TimeZoneListItem.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Time"


/**
 * @brief Builds an empty single-selection outline list named "cityList".
 */
TimeZoneListView::TimeZoneListView(void)
	:
	BOutlineListView("cityList", B_SINGLE_SELECTION_LIST)
{
}


/**
 * @brief Default destructor; underlying items are owned by BOutlineListView.
 */
TimeZoneListView::~TimeZoneListView()
{
}


/**
 * @brief Composes a per-zone tool tip showing the current time at that zone.
 *
 * Looks up the row at @a point; if it is a real time-zone row the tool tip
 * combines the row label, short and daylight names, and the formatted
 * "now" time and date in that zone.
 *
 * @param point     Cursor location in view coordinates.
 * @param[out] _tip Receives the BToolTip pointer when one is provided.
 * @return True if a tool tip was assigned, false to fall back to default.
 */
bool
TimeZoneListView::GetToolTipAt(BPoint point, BToolTip** _tip)
{
	TimeZoneListItem* item = static_cast<TimeZoneListItem*>(
		this->ItemAt(this->IndexOf(point)));
	if (item == NULL || !item->HasTimeZone())
		return false;

	BString nowInTimeZone;
	time_t now = time(NULL);
	fTimeFormat.Format(nowInTimeZone, now, B_SHORT_TIME_FORMAT,
		&item->TimeZone());

	BString dateInTimeZone;
	fDateFormat.Format(dateInTimeZone, now, B_SHORT_DATE_FORMAT,
		&item->TimeZone());

	BString toolTip = item->Text();
	toolTip << '\n' << item->TimeZone().ShortName() << " / "
			<< item->TimeZone().ShortDaylightSavingName()
			<< B_TRANSLATE("\nNow: ") << nowInTimeZone
			<< " (" << dateInTimeZone << ')';

	SetToolTip(toolTip.String());

	*_tip = ToolTip();

	return true;
}
