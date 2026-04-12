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
 * @file DateTimeFormat.cpp
 * @brief Implementation of BDateTimeFormat for combined date-and-time formatting.
 *
 * BDateTimeFormat combines a date style and a time style into a single
 * formatted string using ICU DateFormat. It also supports building custom
 * patterns from a skeleton (a set of date/time element flags) through
 * ICU's DateTimePatternGenerator.
 *
 * @see BDateFormat, BTimeFormat, BFormattingConventions
 */


#include <unicode/uversion.h>
#include <DateTimeFormat.h>

#include <AutoDeleter.h>
#include <Autolock.h>
#include <FormattingConventionsPrivate.h>
#include <LanguagePrivate.h>
#include <TimeZone.h>

#include <ICUWrapper.h>

#include <unicode/datefmt.h>
#include <unicode/dtptngen.h>
#include <unicode/smpdtfmt.h>


U_NAMESPACE_USE


/**
 * @brief Construct a BDateTimeFormat using the given BLocale.
 *
 * @param locale  Locale to use, or NULL for the system default.
 */
BDateTimeFormat::BDateTimeFormat(const BLocale* locale)
	: BFormat(locale)
{
}


/**
 * @brief Construct a BDateTimeFormat from explicit language and conventions.
 *
 * @param language     Language for name strings.
 * @param conventions  Formatting conventions for date/time patterns.
 */
BDateTimeFormat::BDateTimeFormat(const BLanguage& language,
	const BFormattingConventions& conventions)
	: BFormat(language, conventions)
{
}


/**
 * @brief Copy-construct a BDateTimeFormat from another instance.
 *
 * @param other  Source BDateTimeFormat.
 */
BDateTimeFormat::BDateTimeFormat(const BDateTimeFormat &other)
	: BFormat(other)
{
}


/**
 * @brief Destroy the BDateTimeFormat object.
 */
BDateTimeFormat::~BDateTimeFormat()
{
}


/**
 * @brief Build and store a combined date-time pattern from a set of element flags.
 *
 * Uses ICU's DateTimePatternGenerator to derive the best pattern for the
 * provided skeleton (a bitmask of B_DATE_ELEMENT_* and B_TIME_ELEMENT_*
 * flags), then stores it as an explicit override for the given style pair.
 *
 * @param dateStyle  Date style slot to store the generated pattern.
 * @param timeStyle  Time style slot to store the generated pattern.
 * @param elements   Bitmask of B_DATE_ELEMENT_* / B_TIME_ELEMENT_* flags.
 */
void
BDateTimeFormat::SetDateTimeFormat(BDateFormatStyle dateStyle,
	BTimeFormatStyle timeStyle, int32 elements) {
	UErrorCode error = U_ZERO_ERROR;
	DateTimePatternGenerator* generator
		= DateTimePatternGenerator::createInstance(
			*BLanguage::Private(&fLanguage).ICULocale(), error);

	BString skeleton;
	if (elements & B_DATE_ELEMENT_YEAR)
		skeleton << "yyyy";
	if (elements & B_DATE_ELEMENT_MONTH)
		skeleton << "MM";
	if (elements & B_DATE_ELEMENT_WEEKDAY)
		skeleton << "eee";
	if (elements & B_DATE_ELEMENT_DAY)
		skeleton << "dd";
	if (elements & B_DATE_ELEMENT_AM_PM)
		skeleton << "a";
	if (elements & B_DATE_ELEMENT_HOUR) {
		if (fConventions.Use24HourClock())
			skeleton << "k";
		else
			skeleton << "K";
	}
	if (elements & B_DATE_ELEMENT_MINUTE)
		skeleton << "mm";
	if (elements & B_DATE_ELEMENT_SECOND)
		skeleton << "ss";
	if (elements & B_DATE_ELEMENT_TIMEZONE)
		skeleton << "z";

	UnicodeString pattern = generator->getBestPattern(
		UnicodeString::fromUTF8(skeleton.String()), error);

	BString buffer;
	BStringByteSink stringConverter(&buffer);
	pattern.toUTF8(stringConverter);

	fConventions.SetExplicitDateTimeFormat(dateStyle, timeStyle, buffer);

	delete generator;
}


// #pragma mark - Formatting


/**
 * @brief Format a time_t as a combined date-time string into a C buffer.
 *
 * @param target     Destination character buffer.
 * @param maxSize    Size of the destination buffer in bytes.
 * @param time       POSIX timestamp to format.
 * @param dateStyle  Date style level.
 * @param timeStyle  Time style level.
 * @return Number of bytes written on success, or a negative error code.
 */
ssize_t
BDateTimeFormat::Format(char* target, size_t maxSize, time_t time,
	BDateFormatStyle dateStyle, BTimeFormatStyle timeStyle) const
{
	BString format;
	fConventions.GetDateTimeFormat(dateStyle, timeStyle, format);
	ObjectDeleter<DateFormat> dateFormatter(_CreateDateTimeFormatter(format));
	if (!dateFormatter.IsSet())
		return B_NO_MEMORY;

	UnicodeString icuString;
	dateFormatter->format((UDate)time * 1000, icuString);

	CheckedArrayByteSink stringConverter(target, maxSize);
	icuString.toUTF8(stringConverter);

	if (stringConverter.Overflowed())
		return B_BAD_VALUE;

	return stringConverter.NumberOfBytesWritten();
}


/**
 * @brief Format a time_t as a combined date-time string with optional time zone.
 *
 * @param target     Output BString that receives the formatted string.
 * @param time       POSIX timestamp to format.
 * @param dateStyle  Date style level.
 * @param timeStyle  Time style level.
 * @param timeZone   Time zone for display, or NULL for the local zone.
 * @return B_OK on success, B_NO_MEMORY if ICU allocation fails.
 */
status_t
BDateTimeFormat::Format(BString& target, const time_t time,
	BDateFormatStyle dateStyle, BTimeFormatStyle timeStyle,
	const BTimeZone* timeZone) const
{
	BString format;
	fConventions.GetDateTimeFormat(dateStyle, timeStyle, format);
	ObjectDeleter<DateFormat> dateFormatter(_CreateDateTimeFormatter(format));
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

	target.Truncate(0);
	BStringByteSink stringConverter(&target);
	icuString.toUTF8(stringConverter);

	return B_OK;
}


/**
 * @brief Create an ICU DateFormat configured with the given combined pattern.
 *
 * Selects the locale from the preferred language or formatting conventions,
 * creates a date-time instance, then applies the explicit pattern.
 *
 * @param format  ICU SimpleDateFormat pattern string.
 * @return A heap-allocated DateFormat; caller must delete it. NULL on failure.
 */
DateFormat*
BDateTimeFormat::_CreateDateTimeFormatter(const BString& format) const
{
	Locale* icuLocale
		= fConventions.UseStringsFromPreferredLanguage()
			? BLanguage::Private(&fLanguage).ICULocale()
			: BFormattingConventions::Private(&fConventions).ICULocale();

	icu::DateFormat* dateFormatter = icu::DateFormat::createDateTimeInstance(
		DateFormat::kDefault, DateFormat::kDefault, *icuLocale);
	if (dateFormatter == NULL)
		return NULL;

	SimpleDateFormat* dateFormatterImpl
		= static_cast<SimpleDateFormat*>(dateFormatter);

	UnicodeString pattern(format.String());
	dateFormatterImpl->applyPattern(pattern);

	return dateFormatter;
}
