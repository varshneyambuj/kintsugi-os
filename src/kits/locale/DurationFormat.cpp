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
 *   Copyright 2010, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe <zooey@hirschkaefer.de>
 */


/**
 * @file DurationFormat.cpp
 * @brief Implementation of BDurationFormat for human-readable elapsed times.
 *
 * BDurationFormat formats the difference between two bigtime_t timestamps into
 * a natural-language string (e.g. "3 hours 15 minutes") by iterating over
 * calendar fields with ICU's GregorianCalendar::fieldDifference(). Individual
 * non-zero fields are formatted by a BTimeUnitFormat instance and joined with
 * a configurable separator string.
 *
 * @see BTimeUnitFormat, BFormat
 */


#include <unicode/uversion.h>
#include <DurationFormat.h>

#include <new>

#include <unicode/gregocal.h>
#include <unicode/utypes.h>

#include <Locale.h>
#include <LocaleRoster.h>
#include <TimeZone.h>

#include <TimeZonePrivate.h>


U_NAMESPACE_USE


/** @brief Maps BTimeUnitElement indices to the corresponding ICU calendar fields. */
static const UCalendarDateFields skUnitMap[] = {
	UCAL_YEAR,
	UCAL_MONTH,
	UCAL_WEEK_OF_MONTH,
	UCAL_DAY_OF_WEEK,
	UCAL_HOUR_OF_DAY,
	UCAL_MINUTE,
	UCAL_SECOND,
};


/**
 * @brief Construct a BDurationFormat with explicit language, conventions, and style.
 *
 * @param language    Language to use for time unit names.
 * @param conventions Formatting conventions for the time unit formatter.
 * @param separator   String inserted between adjacent time unit fields.
 * @param style       Abbreviated or full time unit style.
 */
BDurationFormat::BDurationFormat(const BLanguage& language,
	const BFormattingConventions& conventions,
	const BString& separator, const time_unit_style style)
	:
	Inherited(language, conventions),
	fSeparator(separator),
	fTimeUnitFormat(language, conventions, style)
{
	UErrorCode icuStatus = U_ZERO_ERROR;
	fCalendar = new GregorianCalendar(icuStatus);
	if (fCalendar == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}
}


/**
 * @brief Construct a BDurationFormat with default locale and given separator/style.
 *
 * @param separator  String inserted between adjacent time unit fields.
 * @param style      Abbreviated or full time unit style.
 */
BDurationFormat::BDurationFormat(const BString& separator,
	const time_unit_style style)
	:
	Inherited(),
	fSeparator(separator),
	fTimeUnitFormat(style)
{
	UErrorCode icuStatus = U_ZERO_ERROR;
	fCalendar = new GregorianCalendar(icuStatus);
	if (fCalendar == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}
}


/**
 * @brief Copy-construct a BDurationFormat, cloning the internal calendar.
 *
 * @param other  Source BDurationFormat to copy.
 */
BDurationFormat::BDurationFormat(const BDurationFormat& other)
	:
	Inherited(other),
	fSeparator(other.fSeparator),
	fTimeUnitFormat(other.fTimeUnitFormat),
	fCalendar(other.fCalendar != NULL
		? new GregorianCalendar(*other.fCalendar) : NULL)
{
	if (fCalendar == NULL && other.fCalendar != NULL)
		fInitStatus = B_NO_MEMORY;
}


/**
 * @brief Destroy the BDurationFormat and free the internal ICU calendar.
 */
BDurationFormat::~BDurationFormat()
{
	delete fCalendar;
}


/**
 * @brief Set the string used to join adjacent time unit components.
 *
 * @param separator  New separator string (e.g. ", " or " ").
 */
void
BDurationFormat::SetSeparator(const BString& separator)
{
	fSeparator = separator;
}


/**
 * @brief Set the time zone used for elapsed-time calculations.
 *
 * The ICU GregorianCalendar must be configured with the correct time zone so
 * that DST transitions are correctly accounted for during fieldDifference().
 * Passing NULL selects the system default time zone.
 *
 * @param timeZone  Time zone to use, or NULL for the system default.
 * @return B_OK on success, B_NO_INIT if the calendar was not created,
 *         or another error code on failure.
 */
status_t
BDurationFormat::SetTimeZone(const BTimeZone* timeZone)
{
	if (fCalendar == NULL)
		return B_NO_INIT;

	BTimeZone::Private zonePrivate;
	if (timeZone == NULL) {
		BTimeZone defaultTimeZone;
		status_t result
			= BLocaleRoster::Default()->GetDefaultTimeZone(&defaultTimeZone);
		if (result != B_OK)
			return result;
		zonePrivate.SetTo(&defaultTimeZone);
	} else
		zonePrivate.SetTo(timeZone);

	TimeZone* icuTimeZone = zonePrivate.ICUTimeZone();
	if (icuTimeZone != NULL)
		fCalendar->setTimeZone(*icuTimeZone);

	return B_OK;
}


/**
 * @brief Format the duration between two bigtime_t values into a BString.
 *
 * Iterates over calendar fields from years down to seconds, appending each
 * non-zero field to \a buffer separated by the configured separator.
 *
 * @param buffer      Output BString that receives the formatted duration.
 * @param startValue  Start of the interval in microseconds since the epoch.
 * @param stopValue   End of the interval in microseconds since the epoch.
 * @return B_OK on success, B_ERROR on ICU failure.
 */
status_t
BDurationFormat::Format(BString& buffer, const bigtime_t startValue,
	const bigtime_t stopValue) const
{
	UErrorCode icuStatus = U_ZERO_ERROR;
	fCalendar->setTime((UDate)startValue / 1000, icuStatus);
	if (!U_SUCCESS(icuStatus))
		return B_ERROR;

	UDate stop = (UDate)stopValue / 1000;
	bool needSeparator = false;
	for (int unit = 0; unit <= B_TIME_UNIT_LAST; ++unit) {
		int delta
			= fCalendar->fieldDifference(stop, skUnitMap[unit], icuStatus);
		if (!U_SUCCESS(icuStatus))
			return B_ERROR;

		if (delta != 0) {
			if (needSeparator)
				buffer.Append(fSeparator);
			else
				needSeparator = true;
			status_t status = fTimeUnitFormat.Format(buffer, delta,
				(time_unit_element)unit);
			if (status != B_OK)
				return status;
		}
	}

	return B_OK;
}
