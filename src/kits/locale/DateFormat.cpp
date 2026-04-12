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
 *       Adrien Desutugues <pulkomandy@pulkomandy.tk>
 */


/**
 * @file DateFormat.cpp
 * @brief Implementation of BDateFormat for locale-aware date formatting.
 *
 * BDateFormat wraps ICU DateFormat to produce localized date strings for
 * four style levels (short, medium, long, full). It supports formatting
 * from a POSIX time_t, a BDate value object, or a combined time_t with
 * optional time zone override. Field-position iterators allow callers to
 * identify individual date components (year, month, day, weekday) within
 * the resulting string.
 *
 * @see BTimeFormat, BDateTimeFormat, BFormattingConventions
 */


#include <unicode/uversion.h>
#include <DateFormat.h>

#include <AutoDeleter.h>
#include <Autolock.h>
#include <FormattingConventionsPrivate.h>
#include <LanguagePrivate.h>
#include <Locale.h>
#include <LocaleRoster.h>
#include <TimeZone.h>

#include <ICUWrapper.h>

#include <unicode/datefmt.h>
#include <unicode/dtfmtsym.h>
#include <unicode/smpdtfmt.h>

#include <vector>


U_NAMESPACE_USE


/** @brief Maps BDateFormatStyle indices to ICU DateFormatSymbols width types. */
static const DateFormatSymbols::DtWidthType kDateFormatStyleToWidth[] = {
	DateFormatSymbols::WIDE,
	DateFormatSymbols::ABBREVIATED,
	DateFormatSymbols::SHORT,
	DateFormatSymbols::NARROW,
};


/**
 * @brief Construct a BDateFormat using the given BLocale.
 *
 * @param locale  Locale whose formatting conventions and language are used,
 *                or NULL for the system default locale.
 */
BDateFormat::BDateFormat(const BLocale* locale)
	: BFormat(locale)
{
}


/**
 * @brief Construct a BDateFormat from explicit language and conventions objects.
 *
 * @param language     Language that controls the name strings (month, weekday).
 * @param conventions  Formatting conventions that supply the date pattern.
 */
BDateFormat::BDateFormat(const BLanguage& language,
	const BFormattingConventions& conventions)
	: BFormat(language, conventions)
{
}


/**
 * @brief Copy-construct a BDateFormat from another instance.
 *
 * @param other  The source BDateFormat to copy.
 */
BDateFormat::BDateFormat(const BDateFormat &other)
	: BFormat(other)
{
}


/**
 * @brief Destroy the BDateFormat object.
 */
BDateFormat::~BDateFormat()
{
}


/**
 * @brief Retrieve the active ICU date pattern string for the given style.
 *
 * @param style      Date style level (e.g. B_SHORT_DATE_FORMAT).
 * @param outFormat  Output BString that receives the ICU pattern.
 * @return B_OK on success, or an error from BFormattingConventions.
 */
status_t
BDateFormat::GetDateFormat(BDateFormatStyle style,
	BString& outFormat) const
{
	return fConventions.GetDateFormat(style, outFormat);
}


/**
 * @brief Override the ICU date pattern used for a particular style level.
 *
 * @param style   Date style level to override.
 * @param format  ICU SimpleDateFormat pattern string to use.
 */
void
BDateFormat::SetDateFormat(BDateFormatStyle style,
	const BString& format)
{
	fConventions.SetExplicitDateFormat(style, format);
}


/**
 * @brief Format a time_t value into a C string buffer.
 *
 * @param string    Destination character buffer.
 * @param maxSize   Size of the destination buffer in bytes.
 * @param time      POSIX timestamp to format.
 * @param style     Date style level.
 * @return Number of bytes written on success, or a negative error code.
 */
ssize_t
BDateFormat::Format(char* string, const size_t maxSize, const time_t time,
	const BDateFormatStyle style) const
{
	ObjectDeleter<DateFormat> dateFormatter(_CreateDateFormatter(style));
	if (!dateFormatter.IsSet())
		return B_NO_MEMORY;

	UnicodeString icuString;
	dateFormatter->format((UDate)time * 1000, icuString);

	CheckedArrayByteSink stringConverter(string, maxSize);
	icuString.toUTF8(stringConverter);

	if (stringConverter.Overflowed())
		return B_BAD_VALUE;

	return stringConverter.NumberOfBytesWritten();
}


/**
 * @brief Format a time_t value into a BString with optional time zone.
 *
 * @param string    Output BString that receives the formatted date.
 * @param time      POSIX timestamp to format.
 * @param style     Date style level.
 * @param timeZone  Time zone for display, or NULL to use the local zone.
 * @return B_OK on success, B_NO_MEMORY if ICU allocation fails.
 */
status_t
BDateFormat::Format(BString& string, const time_t time,
	const BDateFormatStyle style, const BTimeZone* timeZone) const
{
	ObjectDeleter<DateFormat> dateFormatter(_CreateDateFormatter(style));
	if (!dateFormatter.IsSet())
		return B_NO_MEMORY;

	if (timeZone != NULL) {
		ObjectDeleter<TimeZone> icuTimeZone(
			TimeZone::createTimeZone(timeZone->ID().String()));
		if (!icuTimeZone.IsSet())
			return B_NO_MEMORY;
		dateFormatter->setTimeZone(*icuTimeZone.Get());
	}

	UnicodeString icuString;
	dateFormatter->format((UDate)time * 1000, icuString);

	string.Truncate(0);
	BStringByteSink stringConverter(&string);
	icuString.toUTF8(stringConverter);

	return B_OK;
}


/**
 * @brief Format a BDate value object into a BString with optional time zone.
 *
 * Constructs an ICU Calendar from the BDate fields (year, month, day) and
 * formats it. Note that ICU months are zero-based while BDate months are
 * one-based; this method compensates automatically.
 *
 * @param string    Output BString that receives the formatted date.
 * @param time      BDate value to format.
 * @param style     Date style level.
 * @param timeZone  Time zone for display, or NULL for the local zone.
 * @return B_OK on success, B_BAD_DATA if the BDate is invalid, B_NO_MEMORY
 *         on allocation failure.
 */
status_t
BDateFormat::Format(BString& string, const BDate& time,
	const BDateFormatStyle style, const BTimeZone* timeZone) const
{
	if (!time.IsValid())
		return B_BAD_DATA;

	ObjectDeleter<DateFormat> dateFormatter(_CreateDateFormatter(style));
	if (!dateFormatter.IsSet())
		return B_NO_MEMORY;

	UErrorCode err = U_ZERO_ERROR;
	ObjectDeleter<Calendar> calendar(Calendar::createInstance(err));
	if (!U_SUCCESS(err))
		return B_NO_MEMORY;

	if (timeZone != NULL) {
		ObjectDeleter<TimeZone> icuTimeZone(
			TimeZone::createTimeZone(timeZone->ID().String()));
		if (!icuTimeZone.IsSet())
			return B_NO_MEMORY;
		dateFormatter->setTimeZone(*icuTimeZone.Get());
		calendar->setTimeZone(*icuTimeZone.Get());
	}

	// Note ICU calendar uses months in range 0..11, while we use the more
	// natural 1..12 in BDate.
	calendar->set(time.Year(), time.Month() - 1, time.Day());

	UnicodeString icuString;
	FieldPosition p;
	dateFormatter->format(*calendar.Get(), icuString, p);

	string.Truncate(0);
	BStringByteSink stringConverter(&string);
	icuString.toUTF8(stringConverter);

	return B_OK;
}


/**
 * @brief Format a time_t and return field begin/end positions for each element.
 *
 * The caller receives a heap-allocated array of begin/end pairs (two ints per
 * field) in \a fieldPositions. The caller is responsible for freeing this array
 * with free().
 *
 * @param string          Output BString for the formatted date.
 * @param fieldPositions  Output pointer that receives the position array.
 * @param fieldCount      Output count of integers written (2 per field).
 * @param time            POSIX timestamp to format.
 * @param style           Date style level.
 * @return B_OK on success, B_NO_MEMORY or B_BAD_VALUE on failure.
 */
status_t
BDateFormat::Format(BString& string, int*& fieldPositions, int& fieldCount,
	const time_t time, const BDateFormatStyle style) const
{
	ObjectDeleter<DateFormat> dateFormatter(_CreateDateFormatter(style));
	if (!dateFormatter.IsSet())
		return B_NO_MEMORY;

	fieldPositions = NULL;
	UErrorCode error = U_ZERO_ERROR;
	icu::FieldPositionIterator positionIterator;
	UnicodeString icuString;
	dateFormatter->format((UDate)time * 1000, icuString, &positionIterator,
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
 * @brief Enumerate the BDateElement field types present in a formatted date.
 *
 * Uses the current time as a sample input to determine which fields (year,
 * month, day, weekday) appear in the pattern.  The caller must free the
 * returned array with free().
 *
 * @param fields      Output pointer that receives the BDateElement array.
 * @param fieldCount  Output count of elements in the array.
 * @param style       Date style level to inspect.
 * @return B_OK on success, B_NO_MEMORY or B_BAD_VALUE on failure.
 */
status_t
BDateFormat::GetFields(BDateElement*& fields, int& fieldCount,
	BDateFormatStyle style) const
{
	ObjectDeleter<DateFormat> dateFormatter(_CreateDateFormatter(style));
	if (!dateFormatter.IsSet())
		return B_NO_MEMORY;

	fields = NULL;
	UErrorCode error = U_ZERO_ERROR;
	icu::FieldPositionIterator positionIterator;
	UnicodeString icuString;
	time_t now;
	dateFormatter->format((UDate)time(&now) * 1000, icuString,
		&positionIterator, error);

	if (U_FAILURE(error))
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
			case UDAT_YEAR_FIELD:
				fields[i] = B_DATE_ELEMENT_YEAR;
				break;
			case UDAT_MONTH_FIELD:
				fields[i] = B_DATE_ELEMENT_MONTH;
				break;
			case UDAT_DATE_FIELD:
				fields[i] = B_DATE_ELEMENT_DAY;
				break;
			case UDAT_DAY_OF_WEEK_FIELD:
				fields[i] = B_DATE_ELEMENT_WEEKDAY;
				break;
			default:
				fields[i] = B_DATE_ELEMENT_INVALID;
				break;
		}
	}

	return B_OK;
}


/**
 * @brief Get the first day of the week for the current locale's calendar.
 *
 * @param startOfWeek  Output pointer that receives the BWeekday constant.
 * @return B_OK on success, B_BAD_VALUE if the pointer is NULL, B_ERROR on
 *         ICU failure.
 */
status_t
BDateFormat::GetStartOfWeek(BWeekday* startOfWeek) const
{
	if (startOfWeek == NULL)
		return B_BAD_VALUE;

	UErrorCode err = U_ZERO_ERROR;
	ObjectDeleter<Calendar> calendar(Calendar::createInstance(
		*BFormattingConventions::Private(&fConventions).ICULocale(), err));

	if (U_FAILURE(err))
		return B_ERROR;

	UCalendarDaysOfWeek icuWeekStart = calendar->getFirstDayOfWeek(err);
	if (U_FAILURE(err))
		return B_ERROR;

	switch (icuWeekStart) {
		case UCAL_SUNDAY:
			*startOfWeek = B_WEEKDAY_SUNDAY;
			break;
		case UCAL_MONDAY:
			*startOfWeek = B_WEEKDAY_MONDAY;
			break;
		case UCAL_TUESDAY:
			*startOfWeek = B_WEEKDAY_TUESDAY;
			break;
		case UCAL_WEDNESDAY:
			*startOfWeek = B_WEEKDAY_WEDNESDAY;
			break;
		case UCAL_THURSDAY:
			*startOfWeek = B_WEEKDAY_THURSDAY;
			break;
		case UCAL_FRIDAY:
			*startOfWeek = B_WEEKDAY_FRIDAY;
			break;
		case UCAL_SATURDAY:
			*startOfWeek = B_WEEKDAY_SATURDAY;
			break;
		default:
			return B_ERROR;
	}

	return B_OK;
}


/**
 * @brief Get the localized month name for a one-based month number.
 *
 * @param month    Month number in the range [1, 12].
 * @param outName  Output BString that receives the month name.
 * @param style    Date format style that controls the name width.
 * @return B_OK on success, B_BAD_VALUE for invalid style, B_BAD_DATA for
 *         out-of-range month, B_ERROR if the formatter could not be created.
 */
status_t
BDateFormat::GetMonthName(int month, BString& outName,
	const BDateFormatStyle style) const
{
	if (style < 0 || style >= B_DATE_FORMAT_STYLE_COUNT)
		return B_BAD_VALUE;

	DateFormat* format = _CreateDateFormatter(B_LONG_DATE_FORMAT);

	SimpleDateFormat* simpleFormat = dynamic_cast<SimpleDateFormat*>(format);
	if (simpleFormat == NULL) {
		delete format;
		return B_ERROR;
	}

	const DateFormatSymbols* symbols = simpleFormat->getDateFormatSymbols();

	int32_t count;
	const UnicodeString* names = symbols->getMonths(count,
		DateFormatSymbols::STANDALONE, kDateFormatStyleToWidth[style]);

	if (month > count || month <= 0) {
		delete simpleFormat;
		return B_BAD_DATA;
	}

	BStringByteSink stringConverter(&outName);
	names[month - 1].toUTF8(stringConverter);

	delete simpleFormat;
	return B_OK;
}


/**
 * @brief Get the localized weekday name for a one-based day-of-week number.
 *
 * @param day      Day number in the range [1, 7] where 1 = Sunday.
 * @param outName  Output BString that receives the weekday name.
 * @param style    Date format style that controls the name width.
 * @return B_OK on success, B_BAD_VALUE for invalid style, B_BAD_DATA for
 *         out-of-range day, B_ERROR if the formatter could not be created.
 */
status_t
BDateFormat::GetDayName(int day, BString& outName,
	const BDateFormatStyle style) const
{
	if (style < 0 || style >= B_DATE_FORMAT_STYLE_COUNT)
		return B_BAD_VALUE;

	DateFormat* format = _CreateDateFormatter(B_LONG_DATE_FORMAT);

	SimpleDateFormat* simpleFormat = dynamic_cast<SimpleDateFormat*>(format);
	if (simpleFormat == NULL) {
		delete format;
		return B_ERROR;
	}

	const DateFormatSymbols* symbols = simpleFormat->getDateFormatSymbols();

	int32_t count;
	const UnicodeString* names = symbols->getWeekdays(count,
		DateFormatSymbols::STANDALONE, kDateFormatStyleToWidth[style]);

	if (day >= count || day <= 0) {
		delete simpleFormat;
		return B_BAD_DATA;
	}

	BStringByteSink stringConverter(&outName);
	names[_ConvertDayNumberToICU(day)].toUTF8(stringConverter);

	delete simpleFormat;
	return B_OK;
}


/**
 * @brief Parse a date string into a BDate using the given style.
 *
 * The parsed result is expressed as days offset from 1970-01-01 UTC. Any
 * time zone specification in the source string is honored; if none is given,
 * the local zone is assumed.
 *
 * @param source  Input date string to parse.
 * @param style   Date format style that determines the expected pattern.
 * @param output  Output BDate populated with the parsed date.
 * @return B_OK on success, B_NO_MEMORY if the formatter cannot be created.
 */
status_t
BDateFormat::Parse(BString source, BDateFormatStyle style, BDate& output)
{
	// FIXME currently this parses a date in any timezone (or the local one if
	// none is specified) to a BDate in UTC. This may not be a good idea, we
	// may want to parse to a "local" date instead. But BDate should be made
	// timezone aware so things like BDate::Difference can work for dates in
	// different timezones.
	ObjectDeleter<DateFormat> dateFormatter(_CreateDateFormatter(style));
	if (!dateFormatter.IsSet())
		return B_NO_MEMORY;

	ParsePosition p(0);
	UDate date = dateFormatter->parse(UnicodeString::fromUTF8(source.String()),
		p);

	output.SetDate(1970, 1, 1);
	output.AddDays(date / U_MILLIS_PER_DAY + 0.5);

	return B_OK;
}


/**
 * @brief Create an ICU DateFormat instance configured for the given style.
 *
 * Selects the locale from either the preferred language or the formatting
 * conventions depending on UseStringsFromPreferredLanguage(), then applies
 * the explicit or cached pattern from the conventions.
 *
 * @param style  Date style level.
 * @return A heap-allocated DateFormat; the caller is responsible for deleting
 *         it. Returns NULL on allocation failure.
 */
DateFormat*
BDateFormat::_CreateDateFormatter(const BDateFormatStyle style) const
{
	Locale* icuLocale
		= fConventions.UseStringsFromPreferredLanguage()
			? BLanguage::Private(&fLanguage).ICULocale()
			: BFormattingConventions::Private(&fConventions).ICULocale();

	icu::DateFormat* dateFormatter
		= icu::DateFormat::createDateInstance(DateFormat::kShort, *icuLocale);
	if (dateFormatter == NULL)
		return NULL;

	SimpleDateFormat* dateFormatterImpl
		= static_cast<SimpleDateFormat*>(dateFormatter);

	BString format;
	fConventions.GetDateFormat(style, format);

	UnicodeString pattern(format.String());
	dateFormatterImpl->applyPattern(pattern);

	return dateFormatter;
}
