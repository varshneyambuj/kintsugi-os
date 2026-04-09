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
 *   Copyright 2007-2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       Julun, host.haiku@gmx.de
 */


/**
 * @file DateTime.cpp
 * @brief Implementation of the BTime, BDate, and BDateTime classes
 *        (BPrivate namespace).
 *
 * BTime represents a time-of-day with microsecond resolution.  It stores the
 * number of microseconds elapsed since midnight and supports arithmetic
 * (AddHours, AddMinutes, …), comparison operators, and BMessage
 * archiving.  An uninitialized BTime has fMicroseconds == -1 and
 * IsValid() returns false.
 *
 * BDate represents a calendar date (year, month, day) using a hybrid
 * Julian/Gregorian calendar.  The Gregorian calendar begins on 15 October
 * 1582; dates between 5 and 14 October 1582 are invalid.  Dates before
 * 1 January 4713 BC are also considered invalid.  BDate provides Julian
 * Day Number conversions, weekday/week-number calculation, and BMessage
 * archiving.
 *
 * BDateTime combines a BDate and a BTime into a single value, provides
 * round-trip conversion to/from POSIX time_t (seconds since 1 January 1970),
 * and comparison operators.
 *
 * All three classes live in the BPrivate namespace.
 *
 * @see BTime, BDate, BDateTime
 */


#include "DateTime.h"


#include <time.h>
#include <sys/time.h>

#include <DateFormat.h>
#include <Locale.h>
#include <LocaleRoster.h>
#include <Message.h>


namespace BPrivate {

const int32			kSecondsPerMinute			= 60;

const int32			kHoursPerDay				= 24;
const int32			kMinutesPerDay				= 1440;
const int32			kSecondsPerDay				= 86400;
const int32			kMillisecondsPerDay			= 86400000;

const bigtime_t		kMicrosecondsPerSecond		= 1000000LL;
const bigtime_t		kMicrosecondsPerMinute		= 60000000LL;
const bigtime_t		kMicrosecondsPerHour		= 3600000000LL;
const bigtime_t		kMicrosecondsPerDay			= 86400000000LL;


/**
 * @brief Construct a default BTime object.
 *
 * All component accessors (Hour(), Minute(), Second(), etc.) will return 0,
 * but IsValid() will return false until a valid time is set.
 */
BTime::BTime()
	:
	fMicroseconds(-1)
{
}


/**
 * @brief Construct a BTime as a copy of \a other.
 * @param other The BTime to copy.
 */
BTime::BTime(const BTime& other)
	:
	fMicroseconds(other.fMicroseconds)
{
}


/**
 * @brief Construct a BTime with explicit hour, minute, second, and microsecond.
 *
 * @param hour        Must be in [0, 23].
 * @param minute      Must be in [0, 59].
 * @param second      Must be in [0, 59].
 * @param microsecond Must be in [0, 999999].
 *
 * If the specified time is invalid the object is left uninitialised and
 * IsValid() returns false.
 */
BTime::BTime(int32 hour, int32 minute, int32 second, int32 microsecond)
	:
	fMicroseconds(-1)
{
	_SetTime(hour, minute, second, microsecond);
}


/**
 * @brief Construct a BTime by unarchiving from a BMessage.
 * @param archive The BMessage to read from; may be NULL (leaves object invalid).
 */
BTime::BTime(const BMessage* archive)
	:
	fMicroseconds(-1)
{
	if (archive == NULL)
		return;
	archive->FindInt64("microseconds", &fMicroseconds);
}


/** @brief Destroy the BTime object. */
BTime::~BTime()
{
}


/**
 * @brief Archive the BTime object into a BMessage.
 * @param into Destination BMessage; must not be NULL.
 * @return B_OK on success, B_BAD_VALUE if \a into is NULL, or another
 *         error code if adding the field fails.
 */
status_t
BTime::Archive(BMessage* into) const
{
	if (into == NULL)
		return B_BAD_VALUE;
	return into->AddInt64("microseconds", fMicroseconds);
}


/**
 * @brief Return whether this BTime represents a valid time-of-day value.
 *
 * A time is valid when fMicroseconds is in [0, kMicrosecondsPerDay).
 * For example BTime(23, 59, 59, 999999) is valid while BTime(24, 0, 0) is not.
 *
 * @return True if the time is valid, false otherwise.
 */
bool
BTime::IsValid() const
{
	return fMicroseconds > -1 && fMicroseconds < kMicrosecondsPerDay;
}


/**
 * @brief Static convenience overload — return whether \a time is valid.
 * @param time The BTime to test.
 * @return True if \a time is valid.
 */
/*static*/ bool
BTime::IsValid(const BTime& time)
{
	return time.IsValid();
}


/**
 * @brief Static convenience overload — return whether the given components
 *        constitute a valid time.
 * @param hour        Hour component to test.
 * @param minute      Minute component to test.
 * @param second      Second component to test.
 * @param microsecond Microsecond component to test.
 * @return True if the resulting BTime would be valid.
 */
/*static*/ bool
BTime::IsValid(int32 hour, int32 minute, int32 second, int32 microsecond)
{
	return BTime(hour, minute, second, microsecond).IsValid();
}


/**
 * @brief Return the current wall-clock time.
 * @param type B_LOCAL_TIME to use the local timezone, B_GMT_TIME for UTC.
 * @return A BTime set to the current time.  Returns an invalid BTime if
 *         the system clock cannot be read.
 */
BTime
BTime::CurrentTime(time_type type)
{
	struct timeval tv;
	if (gettimeofday(&tv, NULL) != 0) {
		// gettimeofday failed?
		time(&tv.tv_sec);
	}

	struct tm result;
	struct tm* timeinfo;
	if (type == B_GMT_TIME)
		timeinfo = gmtime_r(&tv.tv_sec, &result);
	else
		timeinfo = localtime_r(&tv.tv_sec, &result);

	if (timeinfo == NULL)
		return BTime();

	int32 sec = timeinfo->tm_sec;
	return BTime(timeinfo->tm_hour, timeinfo->tm_min, (sec > 59) ? 59 : sec,
		tv.tv_usec);
}


/**
 * @brief Return a copy of this BTime.
 * @return A copy of this object.
 */
BTime
BTime::Time() const
{
	return *this;
}


/**
 * @brief Set this BTime to the value of \a time.
 * @param time The BTime to copy.
 * @return True if the resulting time is valid, false otherwise.
 */
bool
BTime::SetTime(const BTime& time)
{
	fMicroseconds = time.fMicroseconds;
	return IsValid();
}


/**
 * @brief Set the time from explicit components.
 *
 * @param hour        Must be in [0, 23].
 * @param minute      Must be in [0, 59].
 * @param second      Must be in [0, 59].
 * @param microsecond Must be in [0, 999999].
 * @return True if the time is valid; the object is left unchanged and false
 *         is returned if the specified time is invalid.
 */
bool
BTime::SetTime(int32 hour, int32 minute, int32 second, int32 microsecond)
{
	return _SetTime(hour, minute, second, microsecond);
}


/**
 * @brief Add \a hours to the current time, wrapping at midnight.
 * @param hours Number of hours to add; may be negative.
 * @return Reference to this BTime.
 */
BTime&
BTime::AddHours(int32 hours)
{
	return _AddMicroseconds(bigtime_t(hours % kHoursPerDay)
		* kMicrosecondsPerHour);
}


/**
 * @brief Add \a minutes to the current time, wrapping at midnight.
 * @param minutes Number of minutes to add; may be negative.
 * @return Reference to this BTime.
 */
BTime&
BTime::AddMinutes(int32 minutes)
{
	return _AddMicroseconds(bigtime_t(minutes % kMinutesPerDay)
		* kMicrosecondsPerMinute);
}


/**
 * @brief Add \a seconds to the current time, wrapping at midnight.
 * @param seconds Number of seconds to add; may be negative.
 * @return Reference to this BTime.
 */
BTime&
BTime::AddSeconds(int32 seconds)
{
	return _AddMicroseconds(bigtime_t(seconds % kSecondsPerDay)
		* kMicrosecondsPerSecond);
}


/**
 * @brief Add \a milliseconds to the current time, wrapping at midnight.
 * @param milliseconds Number of milliseconds to add; may be negative.
 * @return Reference to this BTime.
 */
BTime&
BTime::AddMilliseconds(int32 milliseconds)
{
	return _AddMicroseconds(bigtime_t(milliseconds % kMillisecondsPerDay)
		* 1000);
}


/**
 * @brief Add \a microseconds to the current time, wrapping at midnight.
 * @param microseconds Number of microseconds to add; may be negative.
 * @return Reference to this BTime.
 */
BTime&
BTime::AddMicroseconds(int32 microseconds)
{
	return _AddMicroseconds(microseconds);
}


/**
 * @brief Return the hour component of the time (0–23).
 * @return Hour in [0, 23], or 0 if the time is invalid.
 */
int32
BTime::Hour() const
{
	return int32(_Microseconds() / kMicrosecondsPerHour);
}


/**
 * @brief Return the minute component of the time (0–59).
 * @return Minute in [0, 59], or 0 if the time is invalid.
 */
int32
BTime::Minute() const
{
	return int32(((_Microseconds() % kMicrosecondsPerHour))
		/ kMicrosecondsPerMinute);
}


/**
 * @brief Return the second component of the time (0–59).
 * @return Second in [0, 59], or 0 if the time is invalid.
 */
int32
BTime::Second() const
{
	return int32(_Microseconds() / kMicrosecondsPerSecond) % kSecondsPerMinute;
}


/**
 * @brief Return the millisecond component of the time (0–999).
 * @return Milliseconds in [0, 999], derived from the microsecond field.
 */
int32
BTime::Millisecond() const
{

	return Microsecond() / 1000;
}


/**
 * @brief Return the microsecond component of the time (0–999999).
 * @return Microseconds in [0, 999999].
 */
int32
BTime::Microsecond() const
{
	return int32(_Microseconds() % kMicrosecondsPerSecond);
}


bigtime_t
BTime::_Microseconds() const
{
	return fMicroseconds == -1 ? 0 : fMicroseconds;
}


/**
 * @brief Compute the signed difference between \a time and this time.
 *
 * If \a time is earlier the return value is negative.
 *
 * @param time The other BTime to compare against.
 * @param type The unit for the result: B_HOURS_DIFF, B_MINUTES_DIFF,
 *             B_SECONDS_DIFF, B_MILLISECONDS_DIFF, or B_MICROSECONDS_DIFF.
 * @return The difference expressed in the requested unit; range is
 *         [-86400000000, 86400000000] microseconds scaled to the chosen unit.
 */
bigtime_t
BTime::Difference(const BTime& time, diff_type type) const
{
	bigtime_t diff = time._Microseconds() - _Microseconds();
	switch (type) {
		case B_HOURS_DIFF:
			diff /= kMicrosecondsPerHour;
			break;
		case B_MINUTES_DIFF:
			diff /= kMicrosecondsPerMinute;
			break;
		case B_SECONDS_DIFF:
			diff /= kMicrosecondsPerSecond;
			break;
		case B_MILLISECONDS_DIFF:
			diff /= 1000;
			break;
		case B_MICROSECONDS_DIFF:
		default:
			break;
	}
	return diff;
}


/** @brief Return true if this time differs from \a time. */
bool
BTime::operator!=(const BTime& time) const
{
	return fMicroseconds != time.fMicroseconds;
}


/** @brief Return true if this time equals \a time. */
bool
BTime::operator==(const BTime& time) const
{
	return fMicroseconds == time.fMicroseconds;
}


/** @brief Return true if this time is earlier than \a time. */
bool
BTime::operator<(const BTime& time) const
{
	return fMicroseconds < time.fMicroseconds;
}


/** @brief Return true if this time is earlier than or equal to \a time. */
bool
BTime::operator<=(const BTime& time) const
{
	return fMicroseconds <= time.fMicroseconds;
}


/** @brief Return true if this time is later than \a time. */
bool
BTime::operator>(const BTime& time) const
{
	return fMicroseconds > time.fMicroseconds;
}


/** @brief Return true if this time is later than or equal to \a time. */
bool
BTime::operator>=(const BTime& time) const
{
	return fMicroseconds >= time.fMicroseconds;
}


BTime&
BTime::_AddMicroseconds(bigtime_t microseconds)
{
	bigtime_t count = 0;
	if (microseconds < 0) {
		count = ((kMicrosecondsPerDay - microseconds) / kMicrosecondsPerDay) *
			kMicrosecondsPerDay;
	}
	fMicroseconds = (_Microseconds() + microseconds + count) % kMicrosecondsPerDay;
	return *this;
}


bool
BTime::_SetTime(bigtime_t hour, bigtime_t minute, bigtime_t second,
	bigtime_t microsecond)
{
	fMicroseconds = hour * kMicrosecondsPerHour +
					minute * kMicrosecondsPerMinute +
					second * kMicrosecondsPerSecond +
					microsecond;

	bool isValid = IsValid();
	if (!isValid)
		fMicroseconds = -1;

	return isValid;
}


//	#pragma mark - BDate


/**
 * @brief Construct a default (invalid) BDate.
 *
 * IsValid() returns false until the date is set to a valid calendar value.
 */
BDate::BDate()
	:
	fDay(-1),
	fYear(0),
	fMonth(-1)
{
}


/**
 * @brief Construct a BDate as a copy of \a other.
 * @param other The BDate to copy.
 */
BDate::BDate(const BDate& other)
	:
	fDay(other.fDay),
	fYear(other.fYear),
	fMonth(other.fMonth)
{
}


/**
 * @brief Construct a BDate with explicit year, month, and day.
 *
 * @param year  Calendar year (no year 0; dates before 4713 BC are invalid).
 * @param month Month in [1, 12].
 * @param day   Day in [1, days-in-month].
 *
 * Dates between 5 October 1582 and 14 October 1582 (the Julian/Gregorian
 * gap) are considered invalid.  IsValid() returns false if any component is
 * out of range.
 */
BDate::BDate(int32 year, int32 month, int32 day)
{
	_SetDate(year, month, day);
}


/**
 * @brief Construct a BDate from a POSIX time_t value.
 * @param time Seconds since the POSIX epoch (1 January 1970 00:00:00 UTC).
 * @param type B_LOCAL_TIME to interpret \a time in the local timezone,
 *             B_GMT_TIME for UTC.
 */
BDate::BDate(time_t time, time_type type)
{
	struct tm result;
	struct tm* timeinfo;

	if (type == B_GMT_TIME)
		timeinfo = gmtime_r(&time, &result);
	else
		timeinfo = localtime_r(&time, &result);

	if (timeinfo != NULL) {
		_SetDate(timeinfo->tm_year + 1900, timeinfo->tm_mon + 1,
			timeinfo->tm_mday);
	}
}


/**
 * @brief Construct a BDate by unarchiving from a BMessage.
 * @param archive BMessage created by BDate::Archive(); may be NULL.
 */
BDate::BDate(const BMessage* archive)
	:
	fDay(-1),
	fYear(0),
	fMonth(-1)
{
	if (archive == NULL)
		return;
	archive->FindInt32("day", &fDay);
	archive->FindInt32("year", &fYear);
	archive->FindInt32("month", &fMonth);
}


/** @brief Destroy the BDate object. */
BDate::~BDate()
{
}


/**
 * @brief Archive the BDate object into a BMessage.
 * @param into Destination BMessage; must not be NULL.
 * @return B_OK on success, B_BAD_VALUE if \a into is NULL, or another
 *         error code if adding a field fails.
 */
status_t
BDate::Archive(BMessage* into) const
{
	if (into == NULL)
		return B_BAD_VALUE;
	status_t ret = into->AddInt32("day", fDay);
	if (ret == B_OK)
		ret = into->AddInt32("year", fYear);
	if (ret == B_OK)
		ret = into->AddInt32("month", fMonth);
	return ret;
}


/**
 * @brief Return whether this BDate represents a valid calendar date.
 *
 * Dates before 1 January 4713 BC, year 0, and the Julian/Gregorian gap
 * (5–14 October 1582) are all considered invalid.
 *
 * @return True if the date is valid.
 */
bool
BDate::IsValid() const
{
	return IsValid(fYear, fMonth, fDay);
}


/**
 * @brief Static convenience overload — return whether \a date is valid.
 * @param date The BDate to test.
 * @return True if \a date is valid.
 */
/*static*/ bool
BDate::IsValid(const BDate& date)
{
	return IsValid(date.fYear, date.fMonth, date.fDay);
}


/**
 * @brief Static convenience overload — return whether the given components
 *        form a valid calendar date.
 * @param year  Calendar year to test.
 * @param month Month to test (1–12).
 * @param day   Day to test (1–days-in-month).
 * @return True if the resulting BDate would be valid.
 */
/*static*/ bool
BDate::IsValid(int32 year, int32 month, int32 day)
{
	// no year 0 in Julian and nothing before 1.1.4713 BC
	if (year == 0 || year < -4713)
		return false;

	if (month < 1 || month > 12)
		return false;

	if (day < 1 || day > _DaysInMonth(year, month))
		return false;

	// 'missing' days between switch julian - gregorian
	if (year == 1582 && month == 10 && day > 4 && day < 15)
		return false;

	return true;
}


/**
 * @brief Return the current calendar date.
 * @param type B_LOCAL_TIME for the local timezone, B_GMT_TIME for UTC.
 * @return A BDate representing today's date.
 */
BDate
BDate::CurrentDate(time_type type)
{
	return BDate(time(NULL), type);
}


/**
 * @brief Return a copy of this BDate.
 * @return A copy of this object.
 */
BDate
BDate::Date() const
{
	return *this;
}


/**
 * @brief Set this date to \a date.
 * @param date The BDate to copy.
 * @return True if \a date is valid.
 */
bool
BDate::SetDate(const BDate& date)
{
	return _SetDate(date.fYear, date.fMonth, date.fDay);
}


/**
 * @brief Set the date from explicit components.
 * @param year  Calendar year.
 * @param month Month (1–12).
 * @param day   Day (1–days-in-month).
 * @return True if the resulting date is valid; the object is left unchanged
 *         and false is returned if the components are invalid.
 */
bool
BDate::SetDate(int32 year, int32 month, int32 day)
{
	return _SetDate(year, month, day);
}


/**
 * @brief Retrieve the year, month, and day components of the date.
 *
 * Any of the output pointers may be NULL.  If the date is invalid,
 * \a month and \a day are set to -1 and \a year is set to 0.
 *
 * @param year  Receives the year, or NULL to skip.
 * @param month Receives the month (1–12), or NULL to skip.
 * @param day   Receives the day (1–31), or NULL to skip.
 */
void
BDate::GetDate(int32* year, int32* month, int32* day) const
{
	if (year)
		*year = fYear;

	if (month)
		*month = fMonth;

	if (day)
		*day = fDay;
}


/**
 * @brief Add \a days to the current date.
 *
 * If \a days is negative the date moves earlier.  Has no effect if the
 * current date is invalid.
 *
 * @param days Number of days to add.
 */
void
BDate::AddDays(int32 days)
{
	if (IsValid())
		*this = JulianDayToDate(DateToJulianDay() + days);
}


/**
 * @brief Add \a years to the current date.
 *
 * If the resulting day/month combination does not exist in the target year
 * (e.g. 29 February in a non-leap year), the day is clamped to the last
 * valid day of that month.  Has no effect if the date is invalid.
 *
 * @param years Number of years to add; may be negative.
 */
void
BDate::AddYears(int32 years)
{
	if (IsValid()) {
		const int32 tmp = fYear;
		fYear += years;

		if ((tmp > 0 && fYear <= 0) || (tmp < 0 && fYear >= 0))
			fYear += (years > 0) ? +1 : -1;

		fDay = min_c(fDay, _DaysInMonth(fYear, fMonth));
	}
}


/**
 * @brief Add \a months to the current date.
 *
 * The day is clamped to the last valid day of the resulting month if
 * necessary.  Has no effect if the date is invalid.
 *
 * @param months Number of months to add; may be negative.
 */
void
BDate::AddMonths(int32 months)
{
	if (IsValid()) {
		const int32 tmp = fYear;
		fYear += months / 12;
		fMonth +=  months % 12;

		if (fMonth > 12) {
			fYear++;
			fMonth -= 12;
		} else if (fMonth < 1) {
			fYear--;
			fMonth += 12;
		}

		if ((tmp > 0 && fYear <= 0) || (tmp < 0 && fYear >= 0))
			fYear += (months > 0) ? +1 : -1;

		// 'missing' days between switch julian - gregorian
		if (fYear == 1582 && fMonth == 10 && fDay > 4 && fDay < 15)
			fDay = (months > 0) ? 15 : 4;

		fDay = min_c(fDay, _DaysInMonth(fYear, fMonth));
	}
}


/**
 * @brief Return the day component of the date (1–31).
 * @return Day in [1, 31], or -1 if the date is invalid.
 */
int32
BDate::Day() const
{
	return fDay;
}


/**
 * @brief Return the year component of the date.
 * @return Calendar year, or 0 if the date is invalid.
 */
int32
BDate::Year() const
{
	return fYear;
}


/**
 * @brief Return the month component of the date (1–12).
 * @return Month in [1, 12], or -1 if the date is invalid.
 */
int32
BDate::Month() const
{
	return fMonth;
}


/**
 * @brief Return the signed difference in days between \a date and this date.
 *
 * A positive result means \a date is later than this date.  If either date is
 * invalid the result is undefined.
 *
 * @param date The other date to compare against.
 * @return Number of days from this date to \a date (positive = later).
 */
int32
BDate::Difference(const BDate& date) const
{
	return date.DateToJulianDay() - DateToJulianDay();
}


void
BDate::SetDay(int32 day)
{
	fDay = day;
}


void
BDate::SetMonth(int32 month)
{
	fMonth = month;
}


void
BDate::SetYear(int32 year)
{
	fYear = year;
}


/**
 * @brief Return the ISO week number of the year for this date.
 *
 * Only works within the Gregorian calendar; dates before 15 October 1582
 * return B_ERROR.  Uses the algorithm from "Frequently Asked Questions about
 * Calendars", Version 2.8, Claus Tøndering, 15 December 2005.
 *
 * @return ISO week number (1–53), or B_ERROR for invalid or pre-Gregorian dates.
 */
int32
BDate::WeekNumber() const
{
	/*
		This algorithm is taken from:
		Frequently Asked Questions about Calendars
		Version 2.8 Claus Tøndering 15 December 2005

		Note: it will work only within the Gregorian Calendar
	*/

	if (!IsValid() || fYear < 1582
		|| (fYear == 1582 && fMonth < 10)
		|| (fYear == 1582 && fMonth == 10 && fDay < 15))
		return int32(B_ERROR);

	int32 a;
	int32 b;
	int32 s;
	int32 e;
	int32 f;

	if (fMonth > 0 && fMonth < 3) {
		a = fYear - 1;
		b = (a / 4) - (a / 100) + (a / 400);
		int32 c = ((a - 1) / 4) - ((a - 1) / 100) + ((a -1) / 400);
		s = b - c;
		e = 0;
		f = fDay - 1 + 31 * (fMonth - 1);
	} else if (fMonth >= 3 && fMonth <= 12) {
		a = fYear;
		b = (a / 4) - (a / 100) + (a / 400);
		int32 c = ((a - 1) / 4) - ((a - 1) / 100) + ((a -1) / 400);
		s = b - c;
		e = s + 1;
		f = fDay + ((153 * (fMonth - 3) + 2) / 5) + 58 + s;
	} else
		return int32(B_ERROR);

	int32 g = (a + b) % 7;
	int32 d = (f + g - e) % 7;
	int32 n = f + 3 - d;

	int32 weekNumber;
	if (n < 0)
		weekNumber = 53 - (g -s) / 5;
	else if (n > 364 + s)
		weekNumber = 1;
	else
		weekNumber = n / 7 + 1;

	return weekNumber;
}


/**
 * @brief Return the day of the week (1 = Monday, 7 = Sunday).
 * @return Day-of-week in [1, 7], or B_ERROR if the date is invalid.
 */
int32
BDate::DayOfWeek() const
{
	// http://en.wikipedia.org/wiki/Julian_day#Calculation
	return IsValid() ? (DateToJulianDay() % 7) + 1 : int32(B_ERROR);
}


/**
 * @brief Return the ordinal day of the year (1–366).
 * @return Day-of-year in [1, 365] (or 366 for leap years), or B_ERROR if
 *         the date is invalid.
 */
int32
BDate::DayOfYear() const
{
	if (!IsValid())
		return int32(B_ERROR);

	return DateToJulianDay() - _DateToJulianDay(fYear, 1, 1) + 1;
}


/**
 * @brief Return true if the year of this date is a leap year.
 * @note Result is undefined for years before 4713 BC.
 * @return True for a leap year.
 */
bool
BDate::IsLeapYear() const
{
	return IsLeapYear(fYear);
}


/**
 * @brief Static overload — return whether \a year is a leap year.
 * @param year Calendar year to test.
 * @note Result is undefined for years before 4713 BC.
 * @return True if \a year is a leap year.
 */
/*static*/ bool
BDate::IsLeapYear(int32 year)
{
	if (year < 1582) {
		if (year < 0)
			year++;
		return (year % 4) == 0;
	}
	return (year % 400 == 0) || (year % 4 == 0 && year % 100 != 0);
}


/**
 * @brief Return the number of days in the year of this date.
 * @return 365 or 366 for a valid date, or B_ERROR if the date is invalid.
 */
int32
BDate::DaysInYear() const
{
	if (!IsValid())
		return int32(B_ERROR);

	return IsLeapYear(fYear) ? 366 : 365;
}


/**
 * @brief Return the number of days in the month of this date (28–31).
 * @return Days in the month for a valid date, or B_ERROR if the date is invalid.
 */
int32
BDate::DaysInMonth() const
{
	if (!IsValid())
		return int32(B_ERROR);

	return _DaysInMonth(fYear, fMonth);
}


/**
 * @brief Return the abbreviated weekday name for this date's day-of-week.
 * @return Abbreviated day name (e.g. "Mon"), or an empty string if invalid.
 */
BString
BDate::ShortDayName() const
{
	return ShortDayName(DayOfWeek());
}


/**
 * @brief Return the abbreviated weekday name for \a day.
 * @param day Day of week in [1, 7] where 1 = Monday, 7 = Sunday.
 * @return Abbreviated day name (e.g. "Mon"), or an empty string for an
 *         out-of-range value.
 */
/*static*/ BString
BDate::ShortDayName(int32 day)
{
	if (day < 1 || day > 7)
		return BString();

	tm tm_struct;
	memset(&tm_struct, 0, sizeof(tm));
	tm_struct.tm_wday = day == 7 ? 0 : day;

	char buffer[256];
	strftime(buffer, sizeof(buffer), "%a", &tm_struct);

	return BString(buffer);
}


/**
 * @brief Return the abbreviated month name for this date's month.
 * @return Abbreviated month name (e.g. "Jan"), or an empty string if invalid.
 */
BString
BDate::ShortMonthName() const
{
	return ShortMonthName(Month());
}


/**
 * @brief Return the abbreviated month name for \a month.
 * @param month Month number in [1, 12].
 * @return Abbreviated month name (e.g. "Jan"), or an empty string for an
 *         out-of-range value.
 */
/*static*/ BString
BDate::ShortMonthName(int32 month)
{
	if (month < 1 || month > 12)
		return BString();

	tm tm_struct;
	memset(&tm_struct, 0, sizeof(tm));
	tm_struct.tm_mon = month - 1;

	char buffer[256];
	strftime(buffer, sizeof(buffer), "%b", &tm_struct);

	return BString(buffer);
}


/**
 * @brief Return the full weekday name for this date's day-of-week.
 * @return Full day name (e.g. "Monday") in the default locale.
 */
BString
BDate::LongDayName() const
{
	return LongDayName(DayOfWeek());
}


/**
 * @brief Return the full weekday name for \a day in the default locale.
 * @param day Day of week in [1, 7] where 1 = Monday, 7 = Sunday.
 * @return Full day name (e.g. "Monday"), or an empty string for an
 *         out-of-range value or locale error.
 */
/*static*/ BString
BDate::LongDayName(int32 day)
{
	if (day < 1 || day > 7)
		return BString();

	const BLocale* locale = BLocaleRoster::Default()->GetDefaultLocale();
	BDateFormat format(locale);
	BString out;
	if (format.GetDayName(day, out, B_FULL_DATE_FORMAT) != B_OK)
		return BString();

	return out;
}


/**
 * @brief Return the full month name for this date's month.
 * @return Full month name (e.g. "January") in the default locale.
 */
BString
BDate::LongMonthName() const
{
	return LongMonthName(Month());
}


/**
 * @brief Return the full month name for \a month in the default locale.
 * @param month Month number in [1, 12].
 * @return Full month name (e.g. "January"), or an empty string for an
 *         out-of-range value or locale error.
 */
/*static*/ BString
BDate::LongMonthName(int32 month)
{
	if (month < 1 || month > 12)
		return BString();

	const BLocale* locale = BLocaleRoster::Default()->GetDefaultLocale();
	BDateFormat format(locale);
	BString out;
	if (format.GetMonthName(month, out, B_LONG_DATE_FORMAT) != B_OK)
		return BString();

	return out;
}


/**
 * @brief Convert this date to a Julian Day Number.
 * @return Julian Day Number, or B_ERROR if the date is invalid.
 */
int32
BDate::DateToJulianDay() const
{
	return _DateToJulianDay(fYear, fMonth, fDay);
}


/**
 * @brief Convert a Julian Day Number to a BDate.
 *
 * Because of the Julian-to-Gregorian calendar switch, 4 October 1582 is
 * immediately followed by 15 October 1582.
 *
 * @param julianDay Julian Day Number to convert.
 * @return The corresponding BDate, or an invalid BDate if \a julianDay is
 *         negative.
 */
/*static*/ BDate
BDate::JulianDayToDate(int32 julianDay)
{
	BDate date;
	const int32 kGregorianCalendarStart = 2299161;
	if (julianDay >= kGregorianCalendarStart) {
		// http://en.wikipedia.org/wiki/Julian_day#Gregorian_calendar_from_Julian_day_number
		int32 j = julianDay + 32044;
		int32 dg = j % 146097;
		int32 c = (dg / 36524 + 1) * 3 / 4;
		int32 dc = dg - c * 36524;
		int32 db = dc % 1461;
		int32 a = (db / 365 + 1) * 3 / 4;
		int32 da = db - a * 365;
		int32 m = (da * 5 + 308) / 153 - 2;
		date.fYear = ((j / 146097) * 400 + c * 100 + (dc / 1461) * 4 + a)
			- 4800 + (m + 2) / 12;
		date.fMonth = (m + 2) % 12 + 1;
		date.fDay = int32((da - (m + 4) * 153 / 5 + 122) + 1.5);
	} else if (julianDay >= 0) {
		// http://en.wikipedia.org/wiki/Julian_day#Calculation
		julianDay += 32082;
		int32 d = (4 * julianDay + 3) / 1461;
		int32 e = julianDay - (1461 * d) / 4;
		int32 m = ((5 * e) + 2) / 153;
		date.fDay = e - (153 * m + 2) / 5 + 1;
		date.fMonth = m + 3 - 12 * (m / 10);
		int32 year = d - 4800 + (m / 10);
		if (year <= 0)
			year--;
		date.fYear = year;
	}
	return date;
}


/** @brief Return true if this date differs from \a date. */
bool
BDate::operator!=(const BDate& date) const
{
	return DateToJulianDay() != date.DateToJulianDay();
}


/** @brief Return true if this date equals \a date. */
bool
BDate::operator==(const BDate& date) const
{
	return DateToJulianDay() == date.DateToJulianDay();
}


/** @brief Return true if this date is earlier than \a date. */
bool
BDate::operator<(const BDate& date) const
{
	return DateToJulianDay() < date.DateToJulianDay();
}


/** @brief Return true if this date is earlier than or equal to \a date. */
bool
BDate::operator<=(const BDate& date) const
{
	return DateToJulianDay() <= date.DateToJulianDay();
}


/** @brief Return true if this date is later than \a date. */
bool
BDate::operator>(const BDate& date) const
{
	return DateToJulianDay() > date.DateToJulianDay();
}


/** @brief Return true if this date is later than or equal to \a date. */
bool
BDate::operator>=(const BDate& date) const
{
	return DateToJulianDay() >= date.DateToJulianDay();
}


bool
BDate::_SetDate(int32 year, int32 month, int32 day)
{
	fDay = -1;
	fYear = 0;
	fMonth = -1;

	bool valid = IsValid(year, month, day);
	if (valid) {
		fDay = day;
		fYear = year;
		fMonth = month;
	}

	return valid;
}


int32
BDate::_DaysInMonth(int32 year, int32 month)
{
	if (month == 2 && IsLeapYear(year))
		return 29;

	const int32 daysInMonth[12] =
		{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	return daysInMonth[month -1];
}


int32
BDate::_DateToJulianDay(int32 _year, int32 month, int32 day)
{
	if (IsValid(_year, month, day)) {
		int32 year = _year;
		if (year < 0) year++;

		int32 a = (14 - month) / 12;
		int32 y = year + 4800 - a;
		int32 m = month + (12 * a) - 3;

		// http://en.wikipedia.org/wiki/Julian_day#Calculation
		if (year > 1582
			|| (year == 1582 && month > 10)
			|| (year == 1582 && month == 10 && day >= 15)) {
			return day + (((153 * m) + 2) / 5) + (365 * y) + (y / 4) -
				(y / 100) + (y / 400) - 32045;
		} else if (year < 1582
			|| (year == 1582 && month < 10)
			|| (year == 1582 && month == 10 && day <= 4)) {
			return day + (((153 * m) + 2) / 5) + (365 * y) + (y / 4) - 32083;
		}
	}

	// http://en.wikipedia.org/wiki/Gregorian_calendar:
	//		The last day of the Julian calendar was Thursday October 4, 1582
	//		and this was followed by the first day of the Gregorian calendar,
	//		Friday October 15, 1582 (the cycle of weekdays was not affected).
	return int32(B_ERROR);
}


//	#pragma mark - BDateTime


/**
 * @brief Construct a default (invalid) BDateTime.
 *
 * IsValid() returns false until both the date and time components are set
 * to valid values.
 */
BDateTime::BDateTime()
	: fDate(),
	  fTime()
{
}


/**
 * @brief Construct a BDateTime from a BDate and a BTime.
 *
 * IsValid() depends on both \a date and \a time being valid.
 *
 * @param date Calendar date component.
 * @param time Time-of-day component.
 */
BDateTime::BDateTime(const BDate& date, const BTime& time)
	: fDate(date),
	  fTime(time)
{
}


/**
 * @brief Construct a BDateTime by unarchiving from a BMessage.
 * @param archive BMessage created by BDateTime::Archive(); may be NULL.
 */
BDateTime::BDateTime(const BMessage* archive)
	: fDate(archive),
	  fTime(archive)
{
}


/** @brief Destroy the BDateTime object. */
BDateTime::~BDateTime()
{
}


/**
 * @brief Archive the BDateTime object into a BMessage.
 *
 * Stores both the date and time components in \a into.
 *
 * @param into Destination BMessage; must not be NULL.
 * @return B_OK on success, B_BAD_VALUE if \a into is NULL, or another
 *         error code if adding a field fails.
 */
status_t
BDateTime::Archive(BMessage* into) const
{
	status_t ret = fDate.Archive(into);
	if (ret == B_OK)
		ret = fTime.Archive(into);
	return ret;
}


/**
 * @brief Return whether both the date and time components are valid.
 * @return True if both the BDate and BTime components are valid.
 */
bool
BDateTime::IsValid() const
{
	return fDate.IsValid() && fTime.IsValid();
}


/**
 * @brief Return the current date and time.
 * @param type B_LOCAL_TIME for the local timezone, B_GMT_TIME for UTC.
 * @return A BDateTime set to the current date and time.
 */
BDateTime
BDateTime::CurrentDateTime(time_type type)
{
	return BDateTime(BDate::CurrentDate(type), BTime::CurrentTime(type));
}


/**
 * @brief Set both the date and time components simultaneously.
 * @param date New date component.
 * @param time New time component.
 */
void
BDateTime::SetDateTime(const BDate& date, const BTime& time)
{
	fDate = date;
	fTime = time;
}


/**
 * @brief Return a mutable reference to the date component.
 * @return Reference to the internal BDate.
 */
BDate&
BDateTime::Date()
{
	return fDate;
}


/**
 * @brief Return a const reference to the date component.
 * @return Const reference to the internal BDate.
 */
const BDate&
BDateTime::Date() const
{
	return fDate;
}


/**
 * @brief Set the date component.
 * @param date New date value.
 */
void
BDateTime::SetDate(const BDate& date)
{
	fDate = date;
}


/**
 * @brief Return a mutable reference to the time component.
 * @return Reference to the internal BTime.
 */
BTime&
BDateTime::Time()
{
	return fTime;
}


/**
 * @brief Return a const reference to the time component.
 * @return Const reference to the internal BTime.
 */
const BTime&
BDateTime::Time() const
{
	return fTime;
}


/**
 * @brief Set the time component.
 * @param time New time value.
 */
void
BDateTime::SetTime(const BTime& time)
{
	fTime = time;
}


/**
 * @brief Convert this BDateTime to a POSIX time_t (seconds since the epoch).
 *
 * @return Seconds since 1 January 1970 00:00:00 in local time, or -1 if the
 *         date is before the epoch or the conversion fails.
 */
time_t
BDateTime::Time_t() const
{
	BDate date(1970, 1, 1);
	if (date.Difference(fDate) < 0)
		return -1;

	tm tm_struct;

	tm_struct.tm_hour = fTime.Hour();
	tm_struct.tm_min = fTime.Minute();
	tm_struct.tm_sec = fTime.Second();

	tm_struct.tm_year = fDate.Year() - 1900;
	tm_struct.tm_mon = fDate.Month() - 1;
	tm_struct.tm_mday = fDate.Day();

	// set less 0 as we won't use it
	tm_struct.tm_isdst = -1;

	// return secs_since_jan1_1970 or -1 on error
	return mktime(&tm_struct);
}


/**
 * @brief Set this BDateTime from a POSIX time_t value.
 * @param seconds Seconds since 1 January 1970 00:00:00 (local time).
 */
void
BDateTime::SetTime_t(time_t seconds)
{
	time_t timePart = seconds % kSecondsPerDay;
	if (timePart < 0) {
		timePart += kSecondsPerDay;
		seconds -= kSecondsPerDay;
	}

	BTime time;
	time.AddSeconds(timePart);
	fTime.SetTime(time);

	BDate date(1970, 1, 1);
	date.AddDays(seconds / kSecondsPerDay);
	fDate.SetDate(date);
}


/** @brief Return true if this datetime differs from \a dateTime. */
bool
BDateTime::operator!=(const BDateTime& dateTime) const
{
	return fTime != dateTime.fTime && fDate != dateTime.fDate;
}


/** @brief Return true if this datetime equals \a dateTime. */
bool
BDateTime::operator==(const BDateTime& dateTime) const
{
	return fTime == dateTime.fTime && fDate == dateTime.fDate;
}


/** @brief Return true if this datetime is earlier than \a dateTime. */
bool
BDateTime::operator<(const BDateTime& dateTime) const
{
	if (fDate < dateTime.fDate)
		return true;
	if (fDate == dateTime.fDate)
		return fTime < dateTime.fTime;
	return false;
}


/** @brief Return true if this datetime is earlier than or equal to \a dateTime. */
bool
BDateTime::operator<=(const BDateTime& dateTime) const
{
	if (fDate < dateTime.fDate)
		return true;
	if (fDate == dateTime.fDate)
		return fTime <= dateTime.fTime;
	return false;
}


/** @brief Return true if this datetime is later than \a dateTime. */
bool
BDateTime::operator>(const BDateTime& dateTime) const
{
	if (fDate > dateTime.fDate)
		return true;
	if (fDate == dateTime.fDate)
		return fTime > dateTime.fTime;
	return false;
}


/** @brief Return true if this datetime is later than or equal to \a dateTime. */
bool
BDateTime::operator>=(const BDateTime& dateTime) const
{
	if (fDate > dateTime.fDate)
		return true;
	if (fDate == dateTime.fDate)
		return fTime >= dateTime.fTime;
	return false;
}

}	/* namespace BPrivate */
