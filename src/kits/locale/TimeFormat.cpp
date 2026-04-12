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
 *   Copyright 2010-2014, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe <zooey@hirschkaefer.de>
 */


/**
 * @file TimeFormat.cpp
 * @brief Implementation of BTimeFormat for locale-aware time formatting.
 *
 * BTimeFormat wraps ICU DateFormat configured as a time-only formatter.
 * It supports formatting from a POSIX time_t to a BString or a C buffer,
 * enumeration of the time field types present in a pattern, and reverse
 * parsing of a time string into a BTime value. An optional time zone can
 * override the formatter's default zone.
 *
 * @see BDateFormat, BFormattingConventions
 */


#include <unicode/uversion.h>
#include <TimeFormat.h>

#include <AutoDeleter.h>
#include <Autolock.h>
#include <DateTime.h>
#include <FormattingConventionsPrivate.h>
#include <LanguagePrivate.h>
#include <TimeZone.h>

#include <ICUWrapper.h>

#include <unicode/datefmt.h>
#include <unicode/smpdtfmt.h>

#include <vector>


U_NAMESPACE_USE


/**
 * @brief Construct a BTimeFormat using the system default locale.
 */
BTimeFormat::BTimeFormat()
	: BFormat()
{
}


/**
 * @brief Construct a BTimeFormat from explicit language and conventions.
 *
 * @param language     Language for name strings.
 * @param conventions  Formatting conventions supplying the time pattern.
 */
BTimeFormat::BTimeFormat(const BLanguage& language,
	const BFormattingConventions& conventions)
	: BFormat(language, conventions)
{
}


/**
 * @brief Copy-construct a BTimeFormat.
 *
 * @param other  Source BTimeFormat to copy.
 */
BTimeFormat::BTimeFormat(const BTimeFormat &other)
	: BFormat(other)
{
}


/**
 * @brief Destroy the BTimeFormat.
 */
BTimeFormat::~BTimeFormat()
{
}


/**
 * @brief Override the time pattern used for the given style level.
 *
 * @param style   Time style level to override.
 * @param format  ICU SimpleDateFormat time pattern string.
 */
void
BTimeFormat::SetTimeFormat(BTimeFormatStyle style,
	const BString& format)
{
	fConventions.SetExplicitTimeFormat(style, format);
}


// #pragma mark - Formatting


/**
 * @brief Format a time_t into a C string buffer.
 *
 * @param string   Destination character buffer.
 * @param maxSize  Buffer size in bytes.
 * @param time     POSIX timestamp to format.
 * @param style    Time style level.
 * @return Number of bytes written on success, or a negative error code.
 */
ssize_t
BTimeFormat::Format(char* string, size_t maxSize, time_t time,
	BTimeFormatStyle style) const
{
	ObjectDeleter<DateFormat> timeFormatter(_CreateTimeFormatter(style));
	if (!timeFormatter.IsSet())
		return B_NO_MEMORY;

	UnicodeString icuString;
	timeFormatter->format((UDate)time * 1000, icuString);

	CheckedArrayByteSink stringConverter(string, maxSize);
	icuString.toUTF8(stringConverter);

	if (stringConverter.Overflowed())
		return B_BAD_VALUE;

	return stringConverter.NumberOfBytesWritten();
}


/**
 * @brief Format a time_t into a BString with optional time zone override.
 *
 * @param string    Output BString.
 * @param time      POSIX timestamp to format.
 * @param style     Time style level.
 * @param timeZone  Time zone to use, or NULL for the local zone.
 * @return B_OK on success, B_NO_MEMORY if ICU allocation fails.
 */
status_t
BTimeFormat::Format(BString& string, const time_t time,
	const BTimeFormatStyle style, const BTimeZone* timeZone) const
{
	ObjectDeleter<DateFormat> timeFormatter(_CreateTimeFormatter(style));
	if (!timeFormatter.IsSet())
		return B_NO_MEMORY;

	if (timeZone != NULL) {
		ObjectDeleter<TimeZone> icuTimeZone(
			TimeZone::createTimeZone(timeZone->ID().String()));
		if (!icuTimeZone.IsSet())
			return B_NO_MEMORY;
		timeFormatter->setTimeZone(*icuTimeZone.Get());
	}

	UnicodeString icuString;
	timeFormatter->format((UDate)time * 1000, icuString);

	string.Truncate(0);
	BStringByteSink stringConverter(&string);
	icuString.toUTF8(stringConverter);

	return B_OK;
}


/**
 * @brief Format a time_t and return the begin/end positions of each field.
 *
 * The caller receives a heap-allocated int array of begin/end pairs (two ints
 * per field). The caller must free this array with free().
 *
 * @param string          Output BString for the formatted time.
 * @param fieldPositions  Output pointer for the position array.
 * @param fieldCount      Output count of integers (2 per field).
 * @param time            POSIX timestamp to format.
 * @param style           Time style level.
 * @return B_OK on success, B_NO_MEMORY or B_BAD_VALUE on failure.
 */
status_t
BTimeFormat::Format(BString& string, int*& fieldPositions, int& fieldCount,
	time_t time, BTimeFormatStyle style) const
{
	ObjectDeleter<DateFormat> timeFormatter(_CreateTimeFormatter(style));
	if (!timeFormatter.IsSet())
		return B_NO_MEMORY;

	fieldPositions = NULL;
	UErrorCode error = U_ZERO_ERROR;
	icu::FieldPositionIterator positionIterator;
	UnicodeString icuString;
	timeFormatter->format((UDate)time * 1000, icuString, &positionIterator,
		error);

	if (error != U_ZERO_ERROR)
		return B_BAD_VALUE;

	icu::FieldPosition field;
	std::vector<int> fieldPosStorage;
	fieldCount  = 0;
	while (positionIterator.next(field)) {
		fieldPosStorage.push_back(field.getBeginIndex());
		fieldPosStorage.push_back(field.getEndIndex());
		fieldCount += 2;
	}

	fieldPositions = (int*) malloc(fieldCount * sizeof(int));

	for (int i = 0 ; i < fieldCount ; i++ )
		fieldPositions[i] = fieldPosStorage[i];

	string.Truncate(0);
	BStringByteSink stringConverter(&string);
	icuString.toUTF8(stringConverter);

	return B_OK;
}


/**
 * @brief Enumerate the BDateElement field types present in a time pattern.
 *
 * Uses the current time as a sample to discover which time component fields
 * (hour, minute, second, AM/PM) appear in the active pattern. The caller
 * must free the returned array with free().
 *
 * @param fields      Output pointer for the BDateElement array.
 * @param fieldCount  Output count of elements.
 * @param style       Time style level to inspect.
 * @return B_OK on success, B_NO_MEMORY or B_BAD_VALUE on failure.
 */
status_t
BTimeFormat::GetTimeFields(BDateElement*& fields, int& fieldCount,
	BTimeFormatStyle style) const
{
	ObjectDeleter<DateFormat> timeFormatter(_CreateTimeFormatter(style));
	if (!timeFormatter.IsSet())
		return B_NO_MEMORY;

	fields = NULL;
	UErrorCode error = U_ZERO_ERROR;
	icu::FieldPositionIterator positionIterator;
	UnicodeString icuString;
	time_t now;
	timeFormatter->format((UDate)time(&now) * 1000, icuString,
		&positionIterator, error);

	if (error != U_ZERO_ERROR)
		return B_BAD_VALUE;

	icu::FieldPosition field;
	std::vector<int> fieldPosStorage;
	fieldCount  = 0;
	while (positionIterator.next(field)) {
		fieldPosStorage.push_back(field.getField());
		fieldCount ++;
	}

	fields = (BDateElement*) malloc(fieldCount * sizeof(BDateElement));

	for (int i = 0 ; i < fieldCount ; i++ ) {
		switch (fieldPosStorage[i]) {
			case UDAT_HOUR_OF_DAY1_FIELD:
			case UDAT_HOUR_OF_DAY0_FIELD:
			case UDAT_HOUR1_FIELD:
			case UDAT_HOUR0_FIELD:
				fields[i] = B_DATE_ELEMENT_HOUR;
				break;
			case UDAT_MINUTE_FIELD:
				fields[i] = B_DATE_ELEMENT_MINUTE;
				break;
			case UDAT_SECOND_FIELD:
				fields[i] = B_DATE_ELEMENT_SECOND;
				break;
			case UDAT_AM_PM_FIELD:
				fields[i] = B_DATE_ELEMENT_AM_PM;
				break;
			default:
				fields[i] = B_DATE_ELEMENT_INVALID;
				break;
		}
	}

	return B_OK;
}


/**
 * @brief Parse a time string into a BTime using the given style.
 *
 * If the source string does not specify a time zone, GMT is assumed so that
 * the parsed millisecond value can be stored directly in a BTime.
 *
 * @param source  Input time string to parse.
 * @param style   Time style level that determines the expected pattern.
 * @param output  Output BTime populated with the parsed time.
 * @return B_OK on success, B_NO_MEMORY if the formatter cannot be created.
 */
status_t
BTimeFormat::Parse(BString source, BTimeFormatStyle style, BTime& output)
{
	ObjectDeleter<DateFormat> timeFormatter(_CreateTimeFormatter(style));
	if (!timeFormatter.IsSet())
		return B_NO_MEMORY;

	// If no timezone is specified in the time string, assume GMT
	timeFormatter->setTimeZone(*icu::TimeZone::getGMT());

	ParsePosition p(0);
	UDate date = timeFormatter->parse(UnicodeString::fromUTF8(source.String()),
		p);

	output.SetTime(0, 0, 0);
	output.AddMilliseconds(date);

	return B_OK;
}


/**
 * @brief Create an ICU DateFormat configured as a time-only formatter.
 *
 * Selects the locale from the preferred language or the formatting conventions
 * and applies the time pattern from the conventions.
 *
 * @param style  Time style level.
 * @return A heap-allocated DateFormat; caller must delete it. NULL on failure.
 */
DateFormat*
BTimeFormat::_CreateTimeFormatter(const BTimeFormatStyle style) const
{
	Locale* icuLocale
		= fConventions.UseStringsFromPreferredLanguage()
			? BLanguage::Private(&fLanguage).ICULocale()
			: BFormattingConventions::Private(&fConventions).ICULocale();

	icu::DateFormat* timeFormatter
		= icu::DateFormat::createTimeInstance(DateFormat::kShort, *icuLocale);
	if (timeFormatter == NULL)
		return NULL;

	SimpleDateFormat* timeFormatterImpl
		= static_cast<SimpleDateFormat*>(timeFormatter);

	BString format;
	fConventions.GetTimeFormat(style, format);

	UnicodeString pattern(format.String());
	timeFormatterImpl->applyPattern(pattern);

	return timeFormatter;
}
