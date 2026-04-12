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
 *   Copyright 2010-2016 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christophe Huriaux, c.huriaux@gmail.com
 *       Adrien Destugues, pulkomandy@gmail.com
 */


/**
 * @file HttpTime.cpp
 * @brief Implementation of BHttpTime for HTTP date/time string parsing and formatting.
 *
 * Parses HTTP date strings in the three formats defined by RFC 2616 section 3.3
 * (RFC 1123, RFC 1036, and asctime) plus common real-world variants. The POSIX
 * locale is used for strptime/strftime to ensure English month and day names
 * regardless of the user's system locale.
 *
 * @see BNetworkCookie, BHttpResult
 */

#include <HttpTime.h>

#include <new>

#include <cstdio>
#include <locale.h>

using namespace BPrivate::Network;


// The formats used should be, in order of preference (according to RFC2616,
// section 3.3):
// RFC1123 / RFC822: "Sun, 06 Nov 1994 08:49:37 GMT"
// RFC1036 / RFC850: "Sunday, 06-Nov-94 08:49:37 GMT"
// asctime         : "Sun Nov  6 08:49:37 1994"
//
// RFC1123 is the preferred one because it has 4 digit years.
//
// But of course in real life, all possible mixes of the formats are used.
// Believe it or not, it's even possible to find some website that gets this
// right and use one of the 3 formats above.
// Often seen variants are:
// - RFC1036 but with 4 digit year,
// - Missing or different timezone indicator
// - Invalid weekday
static const char* kDateFormats[] = {
	// RFC1123
	"%a, %d %b %Y %H:%M:%S",     // without timezone
	"%a, %d %b %Y %H:%M:%S GMT", // canonical

	// RFC1036
	"%A, %d-%b-%y %H:%M:%S",     // without timezone
	"%A, %d-%b-%y %H:%M:%S GMT", // canonical

	// RFC1036 with 4 digit year
	"%a, %d-%b-%Y %H:%M:%S",     // without timezone
	"%a, %d-%b-%Y %H:%M:%S GMT", // with 4-digit year
	"%a, %d-%b-%Y %H:%M:%S UTC", // "UTC" timezone

	// asctime
	"%a %d %b %H:%M:%S %Y"
};


static locale_t posix = newlocale(LC_ALL_MASK, "POSIX", (locale_t)0);


/**
 * @brief Default constructor — creates a BHttpTime with a zero date.
 */
BHttpTime::BHttpTime()
	:
	fDate(0),
	fDateFormat(B_HTTP_TIME_FORMAT_PREFERRED)
{
}


/**
 * @brief Construct a BHttpTime from an existing BDateTime value.
 *
 * @param date  The date/time to store for subsequent formatting.
 */
BHttpTime::BHttpTime(BDateTime date)
	:
	fDate(date),
	fDateFormat(B_HTTP_TIME_FORMAT_PREFERRED)
{
}


/**
 * @brief Construct a BHttpTime from a date string to be parsed later.
 *
 * The string is stored verbatim; call Parse() to convert it to a BDateTime.
 *
 * @param dateString  An HTTP date string in one of the RFC 2616 formats.
 */
BHttpTime::BHttpTime(const BString& dateString)
	:
	fDateString(dateString),
	fDate(0),
	fDateFormat(B_HTTP_TIME_FORMAT_PREFERRED)
{
}


// #pragma mark Date modification


/**
 * @brief Replace the stored date string with a new one.
 *
 * @param string  The new HTTP date string to store for subsequent parsing.
 */
void
BHttpTime::SetString(const BString& string)
{
	fDateString = string;
}


/**
 * @brief Replace the stored BDateTime value.
 *
 * @param date  The new date/time to store for subsequent formatting.
 */
void
BHttpTime::SetDate(BDateTime date)
{
	fDate = date;
}


// #pragma mark Date conversion


/**
 * @brief Parse the stored date string into a BDateTime value.
 *
 * Iterates through all entries in kDateFormats using strptime() under the
 * POSIX locale, stopping at the first format that consumes the entire input
 * string. Records the matching format index so that ToString() can reproduce
 * the same format by default.
 *
 * @return A BDateTime representing the parsed date, or a zero BDateTime if
 *         the string is too short or does not match any known format.
 */
BDateTime
BHttpTime::Parse()
{
	struct tm expireTime;

	if (fDateString.Length() < 4)
		return 0;

	memset(&expireTime, 0, sizeof(struct tm));

	// Save the current locale, switch to POSIX for strptime to match strings
	// in English, switch back when we're done.
	locale_t current = uselocale(posix);

	fDateFormat = B_HTTP_TIME_FORMAT_PARSED;
	unsigned int i;
	for (i = 0; i < sizeof(kDateFormats) / sizeof(const char*);
		i++) {
		const char* result = strptime(fDateString.String(), kDateFormats[i],
			&expireTime);

		// We need to parse the complete value for the "Expires" key.
		// Otherwise, we consider this to be a session cookie (or try another
		// one of the date formats).
		if (result == fDateString.String() + fDateString.Length()) {
			fDateFormat = i;
			break;
		}
	}

	uselocale(current);

	// Did we identify some valid format?
	if (fDateFormat == B_HTTP_TIME_FORMAT_PARSED)
		return 0;

	// Now convert the struct tm from strptime into a BDateTime.
	BTime time(expireTime.tm_hour, expireTime.tm_min, expireTime.tm_sec);
	BDate date(expireTime.tm_year + 1900, expireTime.tm_mon + 1,
		expireTime.tm_mday);
	BDateTime dateTime(date, time);
	return dateTime;
}


/**
 * @brief Format the stored BDateTime as an HTTP date string.
 *
 * Uses \a format to select from kDateFormats; if \a format is
 * B_HTTP_TIME_FORMAT_PARSED, the format that was detected by the last
 * Parse() call is reused. The output string is capped at 128 characters.
 *
 * @param format  Index into kDateFormats, B_HTTP_TIME_FORMAT_PREFERRED for the
 *                canonical RFC 1123 format, or B_HTTP_TIME_FORMAT_PARSED to
 *                reuse the format last detected by Parse().
 * @return A BString containing the formatted date, or an empty string if
 *         the format cannot be resolved or strftime() fails.
 */
BString
BHttpTime::ToString(int8 format)
{
	BString expirationFinal;
	struct tm expirationTm;
	expirationTm.tm_sec = fDate.Time().Second();
	expirationTm.tm_min = fDate.Time().Minute();
	expirationTm.tm_hour = fDate.Time().Hour();
	expirationTm.tm_mday = fDate.Date().Day();
	expirationTm.tm_mon = fDate.Date().Month() - 1;
	expirationTm.tm_year = fDate.Date().Year() - 1900;
	// strftime starts weekday count at 0 for Sunday,
	// while DayOfWeek starts at 1 for Monday and thus uses 7 for Sunday
	expirationTm.tm_wday = fDate.Date().DayOfWeek() % 7;
	expirationTm.tm_yday = 0;
	expirationTm.tm_isdst = 0;

	if (format == B_HTTP_TIME_FORMAT_PARSED)
		format = fDateFormat;

	if (format != B_HTTP_TIME_FORMAT_PARSED) {
		static const uint16 kTimetToStringMaxLength = 128;
		char expirationString[kTimetToStringMaxLength + 1];
		size_t strLength;

		strLength = strftime(expirationString, kTimetToStringMaxLength,
			kDateFormats[format], &expirationTm);

		expirationFinal.SetTo(expirationString, strLength);
	}
	return expirationFinal;
}
