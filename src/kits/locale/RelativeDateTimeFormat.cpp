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
 *   Copyright 2017, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Akshay Agarwal <agarwal.akshay.akshay8@gmail.com>
 */


/**
 * @file RelativeDateTimeFormat.cpp
 * @brief Implementation of BRelativeDateTimeFormat for "N minutes ago" strings.
 *
 * BRelativeDateTimeFormat uses ICU's RelativeDateTimeFormatter and
 * GregorianCalendar to compute the difference between a reference time and
 * the current moment, then renders it as a localized relative string such as
 * "3 hours ago" or "in 2 days". The largest non-zero calendar field (year
 * through second) is selected as the display unit.
 *
 * @see BDurationFormat, BFormat
 */


#include <unicode/uversion.h>
#include <RelativeDateTimeFormat.h>

#include <stdlib.h>
#include <time.h>

#include <unicode/gregocal.h>
#include <unicode/reldatefmt.h>
#include <unicode/utypes.h>

#include <ICUWrapper.h>

#include <Language.h>
#include <Locale.h>
#include <LocaleRoster.h>
#include <TimeUnitFormat.h>


U_NAMESPACE_USE


/** @brief Maps BTimeUnitElement indices to ICU RelativeDateTimeFormatter unit constants. */
static const URelativeDateTimeUnit kTimeUnitToRelativeDateTime[] = {
	UDAT_REL_UNIT_YEAR,
	UDAT_REL_UNIT_MONTH,
	UDAT_REL_UNIT_WEEK,
	UDAT_REL_UNIT_DAY,
	UDAT_REL_UNIT_HOUR,
	UDAT_REL_UNIT_MINUTE,
	UDAT_REL_UNIT_SECOND,
};


/** @brief Maps BTimeUnitElement indices to ICU calendar field constants. */
static const UCalendarDateFields kTimeUnitToICUDateField[] = {
	UCAL_YEAR,
	UCAL_MONTH,
	UCAL_WEEK_OF_MONTH,
	UCAL_DAY_OF_WEEK,
	UCAL_HOUR_OF_DAY,
	UCAL_MINUTE,
	UCAL_SECOND,
};


/**
 * @brief Construct a BRelativeDateTimeFormat using the default locale.
 *
 * Creates the ICU RelativeDateTimeFormatter and a GregorianCalendar for the
 * language derived from the default locale.
 */
BRelativeDateTimeFormat::BRelativeDateTimeFormat()
	: Inherited()
{
	Locale icuLocale(fLanguage.Code());
	UErrorCode icuStatus = U_ZERO_ERROR;

	fFormatter = new RelativeDateTimeFormatter(icuLocale, icuStatus);
	if (fFormatter == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	fCalendar = new GregorianCalendar(icuStatus);
	if (fCalendar == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	if (!U_SUCCESS(icuStatus))
		fInitStatus = B_ERROR;
}


/**
 * @brief Construct a BRelativeDateTimeFormat with explicit language and conventions.
 *
 * @param language     Language for the relative time strings.
 * @param conventions  Formatting conventions (currently not directly used).
 */
BRelativeDateTimeFormat::BRelativeDateTimeFormat(const BLanguage& language,
	const BFormattingConventions& conventions)
	: Inherited(language, conventions)
{
	Locale icuLocale(fLanguage.Code());
	UErrorCode icuStatus = U_ZERO_ERROR;

	fFormatter = new RelativeDateTimeFormatter(icuLocale, icuStatus);
	if (fFormatter == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	fCalendar = new GregorianCalendar(icuStatus);
	if (fCalendar == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	if (!U_SUCCESS(icuStatus))
		fInitStatus = B_ERROR;
}


/**
 * @brief Copy-construct a BRelativeDateTimeFormat, cloning internal ICU objects.
 *
 * @param other  Source BRelativeDateTimeFormat to copy.
 */
BRelativeDateTimeFormat::BRelativeDateTimeFormat(const BRelativeDateTimeFormat& other)
	: Inherited(other),
	fFormatter(other.fFormatter != NULL
		? new RelativeDateTimeFormatter(*other.fFormatter) : NULL),
	fCalendar(other.fCalendar != NULL
		? new GregorianCalendar(*other.fCalendar) : NULL)
{
	if ((fFormatter == NULL && other.fFormatter != NULL)
		|| (fCalendar == NULL && other.fCalendar != NULL))
		fInitStatus = B_NO_MEMORY;
}


/**
 * @brief Destroy the BRelativeDateTimeFormat and free ICU objects.
 */
BRelativeDateTimeFormat::~BRelativeDateTimeFormat()
{
	delete fFormatter;
	delete fCalendar;
}


/**
 * @brief Format a time_t value relative to the current time.
 *
 * Computes the difference between \a timeValue and the current time using
 * ICU fieldDifference, then delegates to ICU RelativeDateTimeFormatter to
 * produce a string like "3 hours ago" or "in 2 days".
 *
 * @param string     Output BString that receives the relative time string.
 * @param timeValue  The reference POSIX timestamp to describe.
 * @return B_OK on success, B_NO_INIT if the formatter is unset, B_ERROR on
 *         ICU failure.
 */
status_t
BRelativeDateTimeFormat::Format(BString& string,
	const time_t timeValue) const
{
	if (fFormatter == NULL)
		return B_NO_INIT;

	time_t currentTime = time(NULL);

	UErrorCode icuStatus = U_ZERO_ERROR;
	fCalendar->setTime((UDate)currentTime * 1000, icuStatus);
	if (!U_SUCCESS(icuStatus))
		return B_ERROR;

	UDate UTimeValue = (UDate)timeValue * 1000;

	int delta = 0;
	int offset = 1;
	URelativeDateTimeUnit unit = UDAT_REL_UNIT_SECOND;

	for (int timeUnit = 0; timeUnit <= B_TIME_UNIT_LAST; ++timeUnit) {
		delta = fCalendar->fieldDifference(UTimeValue,
			kTimeUnitToICUDateField[timeUnit], icuStatus);

		if (!U_SUCCESS(icuStatus))
			return B_ERROR;

		if (abs(delta) >= offset) {
			unit = kTimeUnitToRelativeDateTime[timeUnit];
			break;
		}
	}

	UnicodeString unicodeResult;

	// Note: icu::RelativeDateTimeFormatter::formatNumeric() is a part of ICU Draft API
	// and may be changed in the future versions and was introduced in ICU 57.
	fFormatter->formatNumeric(delta, unit, unicodeResult, icuStatus);

	if (!U_SUCCESS(icuStatus))
		return B_ERROR;

	BStringByteSink byteSink(&string);
	unicodeResult.toUTF8(byteSink);

	return B_OK;
}
