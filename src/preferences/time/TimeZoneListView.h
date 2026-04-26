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
 * MIT License. Copyright 2012, Haiku Inc.
 * Original authors: Sean Bailey.
 */

/** @file TimeZoneListView.h
    @brief Outline list view showing IANA time zones grouped by region. */

#ifndef _TIME_ZONE_LIST_VIEW_H
#define _TIME_ZONE_LIST_VIEW_H


#include <DateFormat.h>
#include <OutlineListView.h>
#include <TimeFormat.h>


/**
 * @brief Outline list view used by the Time preflet to pick a zone.
 *
 * Provides per-row tool tips with the short and daylight names plus the
 * current time and date in that zone. The actual TimeZoneListItem entries
 * are populated by ZoneView.
 */
class TimeZoneListView : public BOutlineListView {
public:
								TimeZoneListView(void);
	virtual						~TimeZoneListView();

protected:
	virtual	bool				GetToolTipAt(BPoint point, BToolTip** _tip);

private:
	BDateFormat					fDateFormat;
	BTimeFormat					fTimeFormat;
};


#endif	// _TIME_ZONE_LIST_VIEW_H
