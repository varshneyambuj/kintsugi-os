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
 *       Adrien Destugues <pulkomandy@gmail.com>
 *       Oliver Tappe <zooey@hirschkaefer.de>
 */


/**
 * @file TimeUnitFormat.cpp
 * @brief Implementation of BTimeUnitFormat for singular/plural time unit strings.
 *
 * BTimeUnitFormat wraps ICU's TimeUnitFormat to render a numeric time-unit
 * quantity as a localized string obeying the locale's plural rules (e.g.
 * "1 minute" vs. "3 minutes"). Two style levels are supported: abbreviated
 * (B_TIME_UNIT_ABBREVIATED) and full (B_TIME_UNIT_FULL). This class is used
 * internally by BDurationFormat.
 *
 * @see BDurationFormat, BFormat
 */


#include <unicode/uversion.h>
#include <TimeUnitFormat.h>

#include <new>

#include <unicode/format.h>
#include <unicode/locid.h>
#include <unicode/tmutfmt.h>
#include <unicode/utypes.h>
#include <ICUWrapper.h>

#include <Language.h>
#include <Locale.h>
#include <LocaleRoster.h>


U_NAMESPACE_USE


/** @brief Maps BTimeUnitElement indices to the corresponding ICU TimeUnit fields. */
static const TimeUnit::UTimeUnitFields skUnitMap[] = {
	TimeUnit::UTIMEUNIT_YEAR,
	TimeUnit::UTIMEUNIT_MONTH,
	TimeUnit::UTIMEUNIT_WEEK,
	TimeUnit::UTIMEUNIT_DAY,
	TimeUnit::UTIMEUNIT_HOUR,
	TimeUnit::UTIMEUNIT_MINUTE,
	TimeUnit::UTIMEUNIT_SECOND,
};

/** @brief Maps BTimeUnitStyle constants to ICU UTimeUnitFormatStyle values. */
static const UTimeUnitFormatStyle kTimeUnitStyleToICU[] = {
	UTMUTFMT_ABBREVIATED_STYLE,
	UTMUTFMT_FULL_STYLE,
};


/**
 * @brief Construct a BTimeUnitFormat using the default locale and given style.
 *
 * @param style  B_TIME_UNIT_ABBREVIATED or B_TIME_UNIT_FULL.
 */
BTimeUnitFormat::BTimeUnitFormat(const time_unit_style style)
	: Inherited()
{
	Locale icuLocale(fLanguage.Code());
	UErrorCode icuStatus = U_ZERO_ERROR;
	if (style != B_TIME_UNIT_ABBREVIATED && style != B_TIME_UNIT_FULL) {
		fFormatter = NULL;
		fInitStatus = B_BAD_VALUE;
		return;
	}

	fFormatter = new TimeUnitFormat(icuLocale, kTimeUnitStyleToICU[style],
		icuStatus);
	if (fFormatter == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	if (!U_SUCCESS(icuStatus))
		fInitStatus = B_ERROR;
}


/**
 * @brief Construct a BTimeUnitFormat with explicit language, conventions, and style.
 *
 * @param language     Language for plural rules and unit names.
 * @param conventions  Formatting conventions (passed to BFormat base class).
 * @param style        B_TIME_UNIT_ABBREVIATED or B_TIME_UNIT_FULL.
 */
BTimeUnitFormat::BTimeUnitFormat(const BLanguage& language,
	const BFormattingConventions& conventions,
	const time_unit_style style)
	: Inherited(language, conventions)
{
	Locale icuLocale(fLanguage.Code());
	UErrorCode icuStatus = U_ZERO_ERROR;
	if (style != B_TIME_UNIT_ABBREVIATED && style != B_TIME_UNIT_FULL) {
		fFormatter = NULL;
		fInitStatus = B_BAD_VALUE;
		return;
	}

	fFormatter = new TimeUnitFormat(icuLocale, kTimeUnitStyleToICU[style],
		icuStatus);
	if (fFormatter == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	if (!U_SUCCESS(icuStatus))
		fInitStatus = B_ERROR;
}


/**
 * @brief Copy-construct a BTimeUnitFormat, cloning the ICU formatter.
 *
 * @param other  Source BTimeUnitFormat to copy.
 */
BTimeUnitFormat::BTimeUnitFormat(const BTimeUnitFormat& other)
	:
	Inherited(other),
	fFormatter(other.fFormatter != NULL
		? new TimeUnitFormat(*other.fFormatter) : NULL)
{
	if (fFormatter == NULL && other.fFormatter != NULL)
		fInitStatus = B_NO_MEMORY;
}


/**
 * @brief Destroy the BTimeUnitFormat and free the ICU formatter.
 */
BTimeUnitFormat::~BTimeUnitFormat()
{
	delete fFormatter;
}


/**
 * @brief Format a numeric time-unit quantity into a BString.
 *
 * Creates a TimeUnitAmount from \a value and \a unit, formats it using the
 * ICU TimeUnitFormat, and appends the result to \a buffer.
 *
 * @param buffer  Output BString that receives the formatted string.
 * @param value   Numeric quantity (e.g. 3 for "3 minutes").
 * @param unit    Time unit element (e.g. B_TIME_UNIT_MINUTE).
 * @return B_OK on success, B_BAD_VALUE for an out-of-range unit, B_NO_INIT
 *         if no formatter was created, B_NO_MEMORY or B_ERROR on ICU failure.
 */
status_t
BTimeUnitFormat::Format(BString& buffer, const int32 value,
	const time_unit_element unit) const
{
	if (unit < 0 || unit > B_TIME_UNIT_LAST)
		return B_BAD_VALUE;

	if (fFormatter == NULL)
		return B_NO_INIT;

	UErrorCode icuStatus = U_ZERO_ERROR;
	TimeUnitAmount* timeUnitAmount
		= new TimeUnitAmount((double)value, skUnitMap[unit], icuStatus);
	if (timeUnitAmount == NULL)
		return B_NO_MEMORY;
	if (!U_SUCCESS(icuStatus)) {
		delete timeUnitAmount;
		return B_ERROR;
	}

	Formattable formattable;
	formattable.adoptObject(timeUnitAmount);
	FieldPosition pos(FieldPosition::DONT_CARE);
	UnicodeString unicodeResult;
	fFormatter->format(formattable, unicodeResult, pos, icuStatus);
	if (!U_SUCCESS(icuStatus))
		return B_ERROR;

	BStringByteSink byteSink(&buffer);
	unicodeResult.toUTF8(byteSink);

	return B_OK;
}
