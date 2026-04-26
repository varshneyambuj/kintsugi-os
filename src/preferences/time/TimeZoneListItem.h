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
 * MIT License. Copyright 2010, Haiku Inc.
 * Original author: Adrien Destugues.
 */

/** @file TimeZoneListItem.h
    @brief BListItem subclass that pairs a country flag with a time-zone label. */

#ifndef _TIME_ZONE_LIST_ITEM_H
#define _TIME_ZONE_LIST_ITEM_H


#include <StringItem.h>


class BBitmap;
class BCountry;
class BTimeZone;


/**
 * @brief List row representing a region, country, or specific time zone.
 *
 * Wraps an optional BCountry (used to render a flag icon on the left) and
 * an optional BTimeZone (used to query the zone's ID, name, and UTC
 * offset). Region rows have neither; country rows have only the BCountry;
 * leaf zone rows usually have both.
 */
class TimeZoneListItem : public BStringItem {
public:
								TimeZoneListItem(const char* text,
									BCountry* country, BTimeZone* timeZone);
	virtual						~TimeZoneListItem();

	virtual	void				DrawItem(BView* owner, BRect frame,
									bool complete = false);

	virtual	void				Update(BView* owner, const BFont* font);

			/** @brief Returns true when a country (and thus a flag) exists. */
			bool				HasCountry() const
									{ return fCountry != NULL; };
			/** @brief Returns the associated country (assumes HasCountry()). */
			const BCountry&		Country() const { return *fCountry; };
			void				SetCountry(BCountry* country);

			/** @brief Returns true when a time zone is associated. */
			bool				HasTimeZone() const
									{ return fTimeZone != NULL; };
			/** @brief Returns the associated time zone (assumes HasTimeZone()). */
			const BTimeZone&	TimeZone() const
									{ return *fTimeZone; };
			void				SetTimeZone(BTimeZone* timeZone);

			const BString&		ID() const;
			const BString&		Name() const;
			int					OffsetFromGMT() const;

private:
			void				_DrawItemWithTextOffset(BView* owner,
									BRect frame, bool complete,
									float textOffset);

			BCountry*			fCountry;
			BTimeZone*			fTimeZone;
			BBitmap*			fIcon;
};


#endif	// _TIME_ZONE_LIST_ITEM_H
