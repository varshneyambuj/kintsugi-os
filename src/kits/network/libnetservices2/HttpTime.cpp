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
 *   Copyright 2010-2022 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christophe Huriaux, c.huriaux@gmail.com
 *       Adrien Destugues, pulkomandy@gmail.com
 *       Niels Sascha Reedijk, niels.reedijk@gmail.com
 */


/**
 * @file HttpTime.cpp
 * @brief Implementation of BHttpTime, an HTTP date/time parser and formatter.
 *
 * BHttpTime parses and formats the three HTTP timestamp formats defined by
 * RFC 2616 section 3.3: RFC 1123/822, RFC 850, and asctime.  It also handles
 * common real-world variants such as missing timezone indicators and four-digit
 * years in RFC 850 dates.
 *
 * @see BDateTime, BHttpFields
 */


#include <HttpTime.h>

#include <list>
#include <new>

#include <cstdio>

using namespace BPrivate::Network;


// The formats used should be, in order of preference (according to RFC2616,
// section 3.3):
// RFC1123 / RFC822: "Sun, 06 Nov 1994 08:49:37 GMT"
// RFC850          : "Sunday, 06-Nov-94 08:49:37 GMT" (obsoleted by RFC 1036)
// asctime         : "Sun Nov 6 08:49:37 1994"
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
static const std::list<std::pair<BHttpTimeFormat, const char*>> kDateFormats = {
	// RFC822
	{BHttpTimeFormat::RFC1123, "%a, %d %b %Y %H:%M:%S GMT"}, // canonical
	{BHttpTimeFormat::RFC1123, "%a, %d %b %Y %H:%M:%S"}, // without timezone
	// Standard RFC850
	{BHttpTimeFormat::RFC850, "%A, %d-%b-%y %H:%M:%S GMT"}, // canonical
	{BHttpTimeFormat::RFC850, "%A, %d-%b-%y %H:%M:%S"}, // without timezone
	// RFC 850 with 4 digit year
	{BHttpTimeFormat::RFC850, "%a, %d-%b-%Y %H:%M:%S"}, // without timezone
	{BHttpTimeFormat::RFC850, "%a, %d-%b-%Y %H:%M:%S GMT"}, // with 4-digit year
	{BHttpTimeFormat::RFC850, "%a, %d-%b-%Y %H:%M:%S UTC"}, // "UTC" timezone
	// asctime
	{BHttpTimeFormat::AscTime, "%a %b %e %H:%M:%S %Y"},
};


// #pragma mark BHttpTime::InvalidInput


/**
 * @brief Construct an InvalidInput exception for a bad HTTP timestamp string.
 *
 * @param origin  Null-terminated origin identifier string.
 * @param input   The timestamp string that could not be parsed.
 */
BHttpTime::InvalidInput::InvalidInput(const char* origin, BString input)
	:
	BError(origin),
	input(std::move(input))
{
}


/**
 * @brief Return a description of why the timestamp is invalid.
 *
 * @return "A HTTP timestamp cannot be empty" if input is empty;
 *         "The HTTP timestamp string does not match the expected format" otherwise.
 */
const char*
BHttpTime::InvalidInput::Message() const noexcept
{
	if (input.IsEmpty())
		return "A HTTP timestamp cannot be empty";
	else
		return "The HTTP timestamp string does not match the expected format";
}


/**
 * @brief Build a debug message including the offending timestamp string.
 *
 * @return BString with the base debug message and the invalid input appended.
 */
BString
BHttpTime::InvalidInput::DebugMessage() const
{
	BString output = BError::DebugMessage();
	if (!input.IsEmpty())
		output << ":\t " << input << "\n";
	return output;
}


// #pragma mark BHttpTime


/**
 * @brief Construct a BHttpTime initialised to the current GMT date/time.
 */
BHttpTime::BHttpTime() noexcept
	:
	fDate(BDateTime::CurrentDateTime(B_GMT_TIME)),
	fDateFormat(BHttpTimeFormat::RFC1123)
{
}


/**
 * @brief Construct a BHttpTime from an existing BDateTime.
 *
 * @param date  A valid BDateTime; throws InvalidInput if the date is invalid.
 */
BHttpTime::BHttpTime(BDateTime date)
	:
	fDate(date),
	fDateFormat(BHttpTimeFormat::RFC1123)
{
	if (!fDate.IsValid())
		throw InvalidInput(__PRETTY_FUNCTION__, "Invalid BDateTime object");
}


/**
 * @brief Construct a BHttpTime by parsing an HTTP date string.
 *
 * @param dateString  HTTP-formatted date string to parse.
 */
BHttpTime::BHttpTime(const BString& dateString)
	:
	fDate(0),
	fDateFormat(BHttpTimeFormat::RFC1123)
{
	_Parse(dateString);
}


// #pragma mark Date modification


/**
 * @brief Replace the stored date/time by parsing a new HTTP date string.
 *
 * @param string  HTTP-formatted date string to parse.
 */
void
BHttpTime::SetTo(const BString& string)
{
	_Parse(string);
}


/**
 * @brief Replace the stored date/time with a BDateTime.
 *
 * @param date  A valid BDateTime; throws InvalidInput if invalid.
 */
void
BHttpTime::SetTo(BDateTime date)
{
	if (!date.IsValid())
		throw InvalidInput(__PRETTY_FUNCTION__, "Invalid BDateTime object");

	fDate = date;
	fDateFormat = BHttpTimeFormat::RFC1123;
}


// #pragma mark Date Access


/**
 * @brief Return the stored BDateTime.
 *
 * @return The parsed or assigned BDateTime value.
 */
BDateTime
BHttpTime::DateTime() const noexcept
{
	return fDate;
}


/**
 * @brief Return the format in which the stored date was originally parsed.
 *
 * @return BHttpTimeFormat enum value indicating RFC1123, RFC850, or AscTime.
 */
BHttpTimeFormat
BHttpTime::DateTimeFormat() const noexcept
{
	return fDateFormat;
}


/**
 * @brief Format the stored date/time as an HTTP timestamp string.
 *
 * Uses the first matching format string for \a outputFormat found in kDateFormats.
 *
 * @param outputFormat  The desired output format (RFC1123, RFC850, or AscTime).
 * @return BString containing the formatted timestamp, or an empty string on failure.
 */
BString
BHttpTime::ToString(BHttpTimeFormat outputFormat) const
{
	BString expirationFinal;
	struct tm expirationTm = {};
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

	for (auto& [format, formatString]: kDateFormats) {
		if (format != outputFormat)
			continue;

		static const uint16 kTimetToStringMaxLength = 128;
		char expirationString[kTimetToStringMaxLength + 1];
		size_t strLength;

		strLength
			= strftime(expirationString, kTimetToStringMaxLength, formatString, &expirationTm);

		expirationFinal.SetTo(expirationString, strLength);
		break;
	}

	return expirationFinal;
}


/**
 * @brief Parse an HTTP date string and store the result in fDate and fDateFormat.
 *
 * Iterates over all known format strings in kDateFormats until one fully
 * consumes the input string.
 *
 * @param dateString  The HTTP date string to parse.
 */
void
BHttpTime::_Parse(const BString& dateString)
{
	if (dateString.Length() < 4)
		throw InvalidInput(__PRETTY_FUNCTION__, dateString);

	struct tm expireTime = {};

	bool found = false;
	for (auto& [format, formatString]: kDateFormats) {
		const char* result = strptime(dateString.String(), formatString, &expireTime);

		if (result == dateString.String() + dateString.Length()) {
			fDateFormat = format;
			found = true;
			break;
		}
	}

	// Did we identify some valid format?
	if (!found)
		throw InvalidInput(__PRETTY_FUNCTION__, dateString);

	// Now convert the struct tm from strptime into a BDateTime.
	BTime time(expireTime.tm_hour, expireTime.tm_min, expireTime.tm_sec);
	BDate date(expireTime.tm_year + 1900, expireTime.tm_mon + 1, expireTime.tm_mday);
	fDate = BDateTime(date, time);
}


// #pragma mark Convenience Functions


/**
 * @brief Parse an HTTP date string and return the corresponding BDateTime.
 *
 * @param string  HTTP-formatted date string to parse.
 * @return BDateTime representing the parsed point in time.
 */
BDateTime
BPrivate::Network::parse_http_time(const BString& string)
{
	BHttpTime httpTime(string);
	return httpTime.DateTime();
}


/**
 * @brief Format a BDateTime as an HTTP timestamp string.
 *
 * @param timestamp     The date/time to format.
 * @param outputFormat  Desired output format (RFC1123, RFC850, or AscTime).
 * @return BString containing the formatted timestamp.
 */
BString
BPrivate::Network::format_http_time(BDateTime timestamp, BHttpTimeFormat outputFormat)
{
	BHttpTime httpTime(timestamp);
	return httpTime.ToString(outputFormat);
}
