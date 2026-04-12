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
 *   Copyright 2003-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2009-2010, Adrien Destugues, pulkomandy@gmail.com.
 *   Copyright 2010-2011, Oliver Tappe <zooey@hirschkaefer.de>.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file FormattingConventions.cpp
 * @brief Implementation of BFormattingConventions for locale-specific formats.
 *
 * BFormattingConventions encapsulates the regional formatting rules associated
 * with a locale: date, time, and date-time patterns for multiple style levels,
 * numeric and monetary format strings, the 12/24-hour clock preference, and
 * the measurement system. Patterns are lazily fetched from ICU and cached;
 * explicit overrides take precedence over the cached ICU values.
 *
 * @see BLocale, BDateFormat, BTimeFormat
 */


#include <unicode/uversion.h>
#include <FormattingConventions.h>

#include <AutoDeleter.h>
#include <IconUtils.h>
#include <List.h>
#include <Language.h>
#include <Locale.h>
#include <LocaleRoster.h>
#include <Resources.h>
#include <String.h>
#include <UnicodeChar.h>

#include <unicode/datefmt.h>
#include <unicode/locid.h>
#include <unicode/smpdtfmt.h>
#include <unicode/ulocdata.h>
#include <ICUWrapper.h>

#include <iostream>
#include <map>
#include <monetary.h>
#include <new>
#include <stdarg.h>
#include <stdlib.h>


U_NAMESPACE_USE


// #pragma mark - helpers


/**
 * @brief Determine whether a date/time format string uses an AM/PM marker.
 *
 * Scans the format for an unquoted 'a' character which represents the
 * AM/PM field in ICU SimpleDateFormat patterns.
 *
 * @param format  ICU SimpleDateFormat pattern string to inspect.
 * @return true if an unquoted 'a' is present, false otherwise.
 */
static bool
FormatUsesAmPm(const BString& format)
{
	if (format.Length() == 0)
		return false;

	bool inQuote = false;
	for (const char* s = format.String(); *s != '\0'; ++s) {
		switch (*s) {
			case '\'':
				inQuote = !inQuote;
				break;
			case 'a':
				if (!inQuote)
					return true;
				break;
		}
	}

	return false;
}


/**
 * @brief Rewrite a 24-hour format pattern to use a 12-hour clock.
 *
 * Replaces 'H' with 'h' and 'K' with 'k', then appends " a" for the
 * AM/PM marker.
 *
 * @param format  ICU pattern string modified in-place.
 */
static void
CoerceFormatTo12HourClock(BString& format)
{
	char* s = format.LockBuffer(format.Length());
	if (s == NULL)
		return;

	// change format to use h instead of H, k instead of K, and append an
	// am/pm marker
	bool inQuote = false;
	for (; *s != '\0'; ++s) {
		switch (*s) {
			case '\'':
				inQuote = !inQuote;
				break;
			case 'H':
				if (!inQuote)
					*s = 'h';
				break;
			case 'K':
				if (!inQuote)
					*s = 'k';
				break;
		}
	}
	format.UnlockBuffer(format.Length());

	format.Append(" a");
}


/**
 * @brief Rewrite a 12-hour format pattern to use a 24-hour clock.
 *
 * Replaces 'h' with 'H' and 'k' with 'K', and removes the AM/PM field
 * (including any leading whitespace).
 *
 * @param format  ICU pattern string modified in-place.
 */
static void
CoerceFormatTo24HourClock(BString& format)
{
	char* buffer = format.LockBuffer(format.Length());
	char* currentPos = buffer;
	if (currentPos == NULL)
		return;

	// change the format to use H instead of h, K instead of k, and determine
	// and remove the am/pm marker (including leading whitespace)
	bool inQuote = false;
	bool lastWasWhitespace = false;
	uint32 ch;
	const char* amPmStartPos = NULL;
	const char* amPmEndPos = NULL;
	const char* lastWhitespaceStart = NULL;
	for (char* previousPos = currentPos; (ch = BUnicodeChar::FromUTF8(
			(const char**)&currentPos)) != 0; previousPos = currentPos) {
		switch (ch) {
			case '\'':
				inQuote = !inQuote;
				break;
			case 'h':
				if (!inQuote)
					*previousPos = 'H';
				break;
			case 'k':
				if (!inQuote)
					*previousPos = 'K';
				break;
			case 'a':
				if (!inQuote) {
					if (lastWasWhitespace)
						amPmStartPos = lastWhitespaceStart;
					else
						amPmStartPos = previousPos;
					amPmEndPos = currentPos;
				}
				break;
			default:
				if (!inQuote && BUnicodeChar::IsWhitespace(ch)) {
					if (!lastWasWhitespace) {
						lastWhitespaceStart = previousPos;
						lastWasWhitespace = true;
					}
					continue;
				}
		}
		lastWasWhitespace = false;
	}

	format.UnlockBuffer(format.Length());
	if (amPmStartPos != NULL && amPmEndPos > amPmStartPos)
		format.Remove(amPmStartPos - buffer, amPmEndPos - amPmStartPos);
}


/**
 * @brief Replace a full timezone specifier 'z' with the abbreviated form 'V'.
 *
 * Only the first unquoted 'z' that is not followed by another 'z' is
 * replaced, preserving multi-letter timezone tokens.
 *
 * @param format  ICU pattern string modified in-place.
 */
static void
CoerceFormatToAbbreviatedTimezone(BString& format)
{
	char* s = format.LockBuffer(format.Length());
	if (s == NULL)
		return;

	// replace a single 'z' with 'V'
	bool inQuote = false;
	bool lastWasZ = false;
	for (; *s != '\0'; ++s) {
		switch (*s) {
			case '\'':
				inQuote = !inQuote;
				break;
			case 'z':
				if (!inQuote && !lastWasZ && *(s+1) != 'z')
					*s = 'V';
				lastWasZ = true;
				continue;
		}
		lastWasZ = false;
	}
	format.UnlockBuffer(format.Length());
}


// #pragma mark - BFormattingConventions


/** @brief Internal enum tracking the cached or explicit 12/24-hour state. */
enum ClockHoursState {
	CLOCK_HOURS_UNSET = 0,
	CLOCK_HOURS_24,
	CLOCK_HOURS_12
};


/**
 * @brief Construct BFormattingConventions from an ICU locale identifier string.
 *
 * @param id  ICU locale name (e.g. "en_US", "de_DE").
 */
BFormattingConventions::BFormattingConventions(const char* id)
	:
	fCachedUse24HourClock(CLOCK_HOURS_UNSET),
	fExplicitUse24HourClock(CLOCK_HOURS_UNSET),
	fUseStringsFromPreferredLanguage(false),
	fICULocale(new icu::Locale(id))
{
}


/**
 * @brief Copy-construct BFormattingConventions, deeply copying all cached formats.
 *
 * @param other  Source BFormattingConventions to copy.
 */
BFormattingConventions::BFormattingConventions(
	const BFormattingConventions& other)
	:
	fCachedNumericFormat(other.fCachedNumericFormat),
	fCachedMonetaryFormat(other.fCachedMonetaryFormat),
	fCachedUse24HourClock(other.fCachedUse24HourClock),
	fExplicitNumericFormat(other.fExplicitNumericFormat),
	fExplicitMonetaryFormat(other.fExplicitMonetaryFormat),
	fExplicitUse24HourClock(other.fExplicitUse24HourClock),
	fUseStringsFromPreferredLanguage(other.fUseStringsFromPreferredLanguage),
	fICULocale(new icu::Locale(*other.fICULocale))
{
	for (int s = 0; s < B_DATE_FORMAT_STYLE_COUNT; ++s) {
		fCachedDateFormats[s] = other.fCachedDateFormats[s];
		fExplicitDateFormats[s] = other.fExplicitDateFormats[s];

		for (int t = 0; t < B_TIME_FORMAT_STYLE_COUNT; ++t) {
			fCachedDateTimeFormats[s][t] = other.fCachedDateFormats[s][t];
			fExplicitDateTimeFormats[s][t] = other.fExplicitDateFormats[s][t];
		}
	}
	for (int s = 0; s < B_TIME_FORMAT_STYLE_COUNT; ++s) {
		fCachedTimeFormats[s] = other.fCachedTimeFormats[s];
		fExplicitTimeFormats[s] = other.fExplicitTimeFormats[s];
	}
}


/**
 * @brief Reconstruct BFormattingConventions from a BMessage archive.
 *
 * Reads the locale identifier, all explicit date and time format overrides,
 * the 24-hour clock preference, and the language-string preference.
 *
 * @param archive  BMessage produced by a previous Archive() call.
 */
BFormattingConventions::BFormattingConventions(const BMessage* archive)
	:
	fCachedUse24HourClock(CLOCK_HOURS_UNSET),
	fExplicitUse24HourClock(CLOCK_HOURS_UNSET),
	fUseStringsFromPreferredLanguage(false)
{
	BString conventionsID;
	status_t status = archive->FindString("conventions", &conventionsID);
	fICULocale = new icu::Locale(conventionsID);

	for (int s = 0; s < B_DATE_FORMAT_STYLE_COUNT && status == B_OK; ++s) {
		BString format;
		status = archive->FindString("dateFormat", s, &format);
		if (status == B_OK)
			fExplicitDateFormats[s] = format;

		status = archive->FindString("timeFormat", s, &format);
		if (status == B_OK)
			fExplicitTimeFormats[s] = format;
	}

	if (status == B_OK) {
		int8 use24HourClock;
		status = archive->FindInt8("use24HourClock", &use24HourClock);
		if (status == B_OK)
			fExplicitUse24HourClock = use24HourClock;
	}
	if (status == B_OK) {
		bool useStringsFromPreferredLanguage;
		status = archive->FindBool("useStringsFromPreferredLanguage",
			&useStringsFromPreferredLanguage);
		if (status == B_OK)
			fUseStringsFromPreferredLanguage = useStringsFromPreferredLanguage;
	}
}


/**
 * @brief Copy-assign, replacing all fields with those from \a other.
 *
 * @param other  Source BFormattingConventions.
 * @return Reference to this object.
 */
BFormattingConventions&
BFormattingConventions::operator=(const BFormattingConventions& other)
{
	if (this == &other)
		return *this;

	for (int s = 0; s < B_DATE_FORMAT_STYLE_COUNT; ++s) {
		fCachedDateFormats[s] = other.fCachedDateFormats[s];
		fExplicitDateFormats[s] = other.fExplicitDateFormats[s];
		for (int t = 0; t < B_TIME_FORMAT_STYLE_COUNT; ++t) {
			fCachedDateTimeFormats[s][t] = other.fCachedDateTimeFormats[s][t];
			fExplicitDateTimeFormats[s][t]
				= other.fExplicitDateTimeFormats[s][t];
		}
	}
	for (int s = 0; s < B_TIME_FORMAT_STYLE_COUNT; ++s) {
		fCachedTimeFormats[s] = other.fCachedTimeFormats[s];
		fExplicitTimeFormats[s] = other.fExplicitTimeFormats[s];
	}
	fCachedNumericFormat = other.fCachedNumericFormat;
	fCachedMonetaryFormat = other.fCachedMonetaryFormat;
	fCachedUse24HourClock = other.fCachedUse24HourClock;

	fExplicitNumericFormat = other.fExplicitNumericFormat;
	fExplicitMonetaryFormat = other.fExplicitMonetaryFormat;
	fExplicitUse24HourClock = other.fExplicitUse24HourClock;

	fUseStringsFromPreferredLanguage = other.fUseStringsFromPreferredLanguage;

	*fICULocale = *other.fICULocale;

	return *this;
}


/**
 * @brief Destroy the BFormattingConventions and free the ICU locale.
 */
BFormattingConventions::~BFormattingConventions()
{
	delete fICULocale;
}


/**
 * @brief Compare two BFormattingConventions for equality.
 *
 * @param other  The other object to compare against.
 * @return true if all explicit formats, flags, and locale are identical.
 */
bool
BFormattingConventions::operator==(const BFormattingConventions& other) const
{
	if (this == &other)
		return true;

	for (int s = 0; s < B_DATE_FORMAT_STYLE_COUNT; ++s) {
		if (fExplicitDateFormats[s] != other.fExplicitDateFormats[s])
			return false;
	}
	for (int s = 0; s < B_TIME_FORMAT_STYLE_COUNT; ++s) {
		if (fExplicitTimeFormats[s] != other.fExplicitTimeFormats[s])
			return false;
	}

	return fExplicitNumericFormat == other.fExplicitNumericFormat
		&& fExplicitMonetaryFormat == other.fExplicitMonetaryFormat
		&& fExplicitUse24HourClock == other.fExplicitUse24HourClock
		&& fUseStringsFromPreferredLanguage
			== other.fUseStringsFromPreferredLanguage
		&& *fICULocale == *other.fICULocale;
}


/**
 * @brief Compare two BFormattingConventions for inequality.
 *
 * @param other  The other object to compare against.
 * @return true if any field differs.
 */
bool
BFormattingConventions::operator!=(const BFormattingConventions& other) const
{
	return !(*this == other);
}


/**
 * @brief Return the ICU locale identifier string (e.g. "en_US").
 *
 * @return The locale name as a C string; valid for the lifetime of this object.
 */
const char*
BFormattingConventions::ID() const
{
	return fICULocale->getName();
}


/**
 * @brief Return the language portion of the locale ID (e.g. "en").
 *
 * @return Language subtag string; valid for the lifetime of this object.
 */
const char*
BFormattingConventions::LanguageCode() const
{
	return fICULocale->getLanguage();
}


/**
 * @brief Return the country/region portion of the locale ID, or NULL if absent.
 *
 * @return Country subtag (e.g. "US"), or NULL if none is set.
 */
const char*
BFormattingConventions::CountryCode() const
{
	const char* country = fICULocale->getCountry();
	if (country == NULL || country[0] == '\0')
		return NULL;

	return country;
}


/**
 * @brief Indicate whether these conventions are country-specific.
 *
 * @return true if a country code is present in the locale ID.
 */
bool
BFormattingConventions::AreCountrySpecific() const
{
	return CountryCode() != NULL;
}


/**
 * @brief Get the locale's native display name, title-cased.
 *
 * @param name  Output BString that receives the native name.
 * @return B_OK always.
 */
status_t
BFormattingConventions::GetNativeName(BString& name) const
{
	UnicodeString string;
	fICULocale->getDisplayName(*fICULocale, string);
	string.toTitle(NULL, *fICULocale);

	name.Truncate(0);
	BStringByteSink converter(&name);
	string.toUTF8(converter);

	return B_OK;
}


/**
 * @brief Get the locale's display name in the given language.
 *
 * If \a displayLanguage is NULL the system default language is used.
 *
 * @param name             Output BString for the localized name.
 * @param displayLanguage  Language to use, or NULL for the default.
 * @return B_OK always.
 */
status_t
BFormattingConventions::GetName(BString& name,
	const BLanguage* displayLanguage) const
{
	BString displayLanguageID;
	if (displayLanguage == NULL) {
		BLanguage defaultLanguage;
		BLocale::Default()->GetLanguage(&defaultLanguage);
		displayLanguageID = defaultLanguage.Code();
	} else {
		displayLanguageID = displayLanguage->Code();
	}

	UnicodeString uString;
	fICULocale->getDisplayName(Locale(displayLanguageID.String()), uString);
	name.Truncate(0);
	BStringByteSink stringConverter(&name);
	uString.toUTF8(stringConverter);

	return B_OK;
}


/**
 * @brief Return the measurement system (metric or US) for this locale.
 *
 * @return B_US for US customary units, B_METRIC for SI/metric.
 */
BMeasurementKind
BFormattingConventions::MeasurementKind() const
{
	UErrorCode error = U_ZERO_ERROR;
	switch (ulocdata_getMeasurementSystem(ID(), &error)) {
		case UMS_US:
			return B_US;
		case UMS_SI:
		default:
			return B_METRIC;
	}
}


/**
 * @brief Retrieve the date format pattern for the given style level.
 *
 * Returns the explicit override if one was set, otherwise returns the cached
 * ICU pattern (fetching and caching it from ICU on first access).
 *
 * @param style      Date style level.
 * @param outFormat  Output BString that receives the ICU pattern string.
 * @return B_OK on success, B_BAD_VALUE for an out-of-range style, B_NO_MEMORY
 *         if the ICU formatter could not be allocated.
 */
status_t
BFormattingConventions::GetDateFormat(BDateFormatStyle style,
	BString& outFormat) const
{
	if (style < 0 || style >= B_DATE_FORMAT_STYLE_COUNT)
		return B_BAD_VALUE;

	outFormat = fExplicitDateFormats[style].Length()
		? fExplicitDateFormats[style]
		: fCachedDateFormats[style];

	if (outFormat.Length() > 0)
		return B_OK;

	ObjectDeleter<DateFormat> dateFormatter(
		DateFormat::createDateInstance((DateFormat::EStyle)style, *fICULocale));
	if (!dateFormatter.IsSet())
		return B_NO_MEMORY;

	SimpleDateFormat* dateFormatterImpl
		= static_cast<SimpleDateFormat*>(dateFormatter.Get());

	UnicodeString icuString;
	dateFormatterImpl->toPattern(icuString);
	BStringByteSink stringConverter(&outFormat);
	icuString.toUTF8(stringConverter);

	fCachedDateFormats[style] = outFormat;

	return B_OK;
}


/**
 * @brief Retrieve the time format pattern for the given style level.
 *
 * Applies the configured 12/24-hour clock coercion and abbreviated timezone
 * substitution after fetching the ICU pattern.
 *
 * @param style      Time style level.
 * @param outFormat  Output BString that receives the ICU pattern string.
 * @return B_OK on success, B_BAD_VALUE for an out-of-range style, B_NO_MEMORY
 *         if the ICU formatter could not be allocated.
 */
status_t
BFormattingConventions::GetTimeFormat(BTimeFormatStyle style,
	BString& outFormat) const
{
	if (style < 0 || style >= B_TIME_FORMAT_STYLE_COUNT)
		return B_BAD_VALUE;

	outFormat = fExplicitTimeFormats[style].Length()
		? fExplicitTimeFormats[style]
		: fCachedTimeFormats[style];

	if (outFormat.Length() > 0)
		return B_OK;

	ObjectDeleter<DateFormat> timeFormatter(
		DateFormat::createTimeInstance((DateFormat::EStyle)style, *fICULocale));
	if (!timeFormatter.IsSet())
		return B_NO_MEMORY;

	SimpleDateFormat* timeFormatterImpl
		= static_cast<SimpleDateFormat*>(timeFormatter.Get());

	UnicodeString icuString;
	timeFormatterImpl->toPattern(icuString);
	BStringByteSink stringConverter(&outFormat);
	icuString.toUTF8(stringConverter);

	CoerceFormatForClock(outFormat);

	if (style != B_FULL_TIME_FORMAT) {
		// use abbreviated timezone in short timezone format
		CoerceFormatToAbbreviatedTimezone(outFormat);
	}

	fCachedTimeFormats[style] = outFormat;

	return B_OK;
}


/**
 * @brief Retrieve the combined date-time format pattern for the given styles.
 *
 * @param dateStyle  Date style level.
 * @param timeStyle  Time style level.
 * @param outFormat  Output BString that receives the ICU pattern string.
 * @return B_OK on success, B_BAD_VALUE for out-of-range style, B_NO_MEMORY
 *         if the ICU formatter could not be allocated.
 */
status_t
BFormattingConventions::GetDateTimeFormat(BDateFormatStyle dateStyle,
	BTimeFormatStyle timeStyle, BString& outFormat) const
{
	if (dateStyle < 0 || dateStyle >= B_DATE_FORMAT_STYLE_COUNT)
		return B_BAD_VALUE;

	if (timeStyle < 0 || timeStyle >= B_TIME_FORMAT_STYLE_COUNT)
		return B_BAD_VALUE;

	outFormat = fExplicitDateTimeFormats[dateStyle][timeStyle].Length()
		? fExplicitDateTimeFormats[dateStyle][timeStyle]
		: fCachedDateTimeFormats[dateStyle][timeStyle];

	if (outFormat.Length() > 0)
		return B_OK;

	ObjectDeleter<DateFormat> dateFormatter(
		DateFormat::createDateTimeInstance((DateFormat::EStyle)dateStyle,
			(DateFormat::EStyle)timeStyle, *fICULocale));
	if (!dateFormatter.IsSet())
		return B_NO_MEMORY;

	SimpleDateFormat* dateFormatterImpl
		= static_cast<SimpleDateFormat*>(dateFormatter.Get());

	UnicodeString icuString;
	dateFormatterImpl->toPattern(icuString);
	BStringByteSink stringConverter(&outFormat);
	icuString.toUTF8(stringConverter);

	CoerceFormatForClock(outFormat);

	if (dateStyle != B_FULL_DATE_FORMAT) {
		// use abbreviated timezone in short timezone format
		CoerceFormatToAbbreviatedTimezone(outFormat);
	}

	fCachedDateTimeFormats[dateStyle][timeStyle] = outFormat;

	return B_OK;
}


/**
 * @brief Retrieve the numeric format pattern (not yet implemented).
 *
 * @param outFormat  Output BString (not populated).
 * @return B_UNSUPPORTED always.
 */
status_t
BFormattingConventions::GetNumericFormat(BString& outFormat) const
{
	// TODO!
	return B_UNSUPPORTED;
}


/**
 * @brief Retrieve the monetary format pattern (not yet implemented).
 *
 * @param outFormat  Output BString (not populated).
 * @return B_UNSUPPORTED always.
 */
status_t
BFormattingConventions::GetMonetaryFormat(BString& outFormat) const
{
	// TODO!
	return B_UNSUPPORTED;
}


/**
 * @brief Store an explicit date pattern override for the given style.
 *
 * @param style   Date style level to override.
 * @param format  ICU SimpleDateFormat pattern string.
 */
void
BFormattingConventions::SetExplicitDateFormat(BDateFormatStyle style,
	const BString& format)
{
	fExplicitDateFormats[style] = format;
}


/**
 * @brief Store an explicit time pattern override for the given style.
 *
 * @param style   Time style level to override.
 * @param format  ICU SimpleDateFormat pattern string.
 */
void
BFormattingConventions::SetExplicitTimeFormat(BTimeFormatStyle style,
	const BString& format)
{
	fExplicitTimeFormats[style] = format;
}


/**
 * @brief Store an explicit combined date-time pattern override for the given styles.
 *
 * @param dateStyle  Date style level.
 * @param timeStyle  Time style level.
 * @param format     ICU SimpleDateFormat pattern string.
 */
void
BFormattingConventions::SetExplicitDateTimeFormat(BDateFormatStyle dateStyle,
	BTimeFormatStyle timeStyle, const BString& format)
{
	fExplicitDateTimeFormats[dateStyle][timeStyle] = format;
}


/**
 * @brief Store an explicit numeric format pattern override.
 *
 * @param format  ICU DecimalFormat pattern string.
 */
void
BFormattingConventions::SetExplicitNumericFormat(const BString& format)
{
	fExplicitNumericFormat = format;
}


/**
 * @brief Store an explicit monetary format pattern override.
 *
 * @param format  ICU DecimalFormat pattern string for monetary values.
 */
void
BFormattingConventions::SetExplicitMonetaryFormat(const BString& format)
{
	fExplicitMonetaryFormat = format;
}


/**
 * @brief Return whether name strings should come from the preferred language.
 *
 * When true, month and weekday names are taken from the user's preferred
 * language even if the formatting conventions belong to a different locale.
 *
 * @return true if strings come from the preferred language, false otherwise.
 */
bool
BFormattingConventions::UseStringsFromPreferredLanguage() const
{
	return fUseStringsFromPreferredLanguage;
}


/**
 * @brief Set whether name strings should come from the preferred language.
 *
 * @param value  true to use preferred-language strings, false for locale strings.
 */
void
BFormattingConventions::SetUseStringsFromPreferredLanguage(bool value)
{
	fUseStringsFromPreferredLanguage = value;
}


/**
 * @brief Return whether the locale uses a 24-hour clock.
 *
 * Checks the explicit override first; if unset, inspects the medium time
 * format for an AM/PM marker to determine the locale default.
 *
 * @return true if a 24-hour clock is active, false for a 12-hour clock.
 */
bool
BFormattingConventions::Use24HourClock() const
{
	int8 use24HourClock = fExplicitUse24HourClock != CLOCK_HOURS_UNSET
		?  fExplicitUse24HourClock : fCachedUse24HourClock;

	if (use24HourClock == CLOCK_HOURS_UNSET) {
		BString format;
		GetTimeFormat(B_MEDIUM_TIME_FORMAT, format);
		fCachedUse24HourClock
			= FormatUsesAmPm(format) ? CLOCK_HOURS_12 : CLOCK_HOURS_24;
		return fCachedUse24HourClock == CLOCK_HOURS_24;
	}

	return fExplicitUse24HourClock == CLOCK_HOURS_24;
}


/**
 * @brief Explicitly set the 24-hour clock preference, invalidating cached formats.
 *
 * @param value  true for a 24-hour clock, false for 12-hour.
 */
void
BFormattingConventions::SetExplicitUse24HourClock(bool value)
{
	int8 newUse24HourClock = value ? CLOCK_HOURS_24 : CLOCK_HOURS_12;
	if (fExplicitUse24HourClock == newUse24HourClock)
		return;

	fExplicitUse24HourClock = newUse24HourClock;

	for (int s = 0; s < B_TIME_FORMAT_STYLE_COUNT; ++s)
		fCachedTimeFormats[s].Truncate(0);
}


/**
 * @brief Clear the explicit 24-hour clock preference and invalidate caches.
 *
 * After this call the locale's native clock convention is used again.
 */
void
BFormattingConventions::UnsetExplicitUse24HourClock()
{
	fExplicitUse24HourClock = CLOCK_HOURS_UNSET;

	for (int s = 0; s < B_TIME_FORMAT_STYLE_COUNT; ++s)
		fCachedTimeFormats[s].Truncate(0);
}


/**
 * @brief Flatten the explicit overrides and locale ID into a BMessage.
 *
 * @param archive  Output BMessage to receive the archived fields.
 * @param deep     Unused; present for BArchivable compatibility.
 * @return B_OK on success, or an error code if any AddXxx call fails.
 */
status_t
BFormattingConventions::Archive(BMessage* archive, bool deep) const
{
	status_t status = archive->AddString("conventions", fICULocale->getName());
	for (int s = 0; s < B_DATE_FORMAT_STYLE_COUNT && status == B_OK; ++s) {
		status = archive->AddString("dateFormat", fExplicitDateFormats[s]);
		if (status == B_OK)
			status = archive->AddString("timeFormat", fExplicitTimeFormats[s]);
	}
	if (status == B_OK)
		status = archive->AddInt8("use24HourClock", fExplicitUse24HourClock);
	if (status == B_OK) {
		status = archive->AddBool("useStringsFromPreferredLanguage",
			fUseStringsFromPreferredLanguage);
	}

	return status;
}


/**
 * @brief Apply the active clock preference to a time/date-time pattern.
 *
 * If an explicit or cached 12/24-hour preference has been set and the pattern
 * uses the wrong clock, the appropriate coercion helper is called.
 *
 * @param outFormat  ICU pattern string modified in-place.
 */
void
BFormattingConventions::CoerceFormatForClock(BString& outFormat) const
{
	int8 use24HourClock = fExplicitUse24HourClock != CLOCK_HOURS_UNSET
		? fExplicitUse24HourClock : fCachedUse24HourClock;
	if (use24HourClock != CLOCK_HOURS_UNSET) {
		// adjust to 12/24-hour clock as requested
		bool localeUses24HourClock = !FormatUsesAmPm(outFormat);
		if (localeUses24HourClock) {
			if (use24HourClock == CLOCK_HOURS_12)
				CoerceFormatTo12HourClock(outFormat);
		} else {
			if (use24HourClock == CLOCK_HOURS_24)
				CoerceFormatTo24HourClock(outFormat);
		}
	}
}
