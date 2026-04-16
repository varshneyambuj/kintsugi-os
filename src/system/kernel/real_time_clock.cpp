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
 *   Copyright 2004-2009, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Copyright 2003, Jeff Ward, jeff@r2d2.stcloudstate.edu. All rights reserved.
 *
 *   Distributed under the terms of the MIT License.
 */

/** @file real_time_clock.cpp
 *  @brief Wall-clock time management bridging the hardware RTC and user-visible time.
 *
 * Reads the hardware clock through the architecture layer, exposes the
 * current wall-clock time to user space via the commpage, and notifies the
 * timer and user-timer subsystems whenever the system clock is updated. */


#include <KernelExport.h>

#include <arch/real_time_clock.h>
#include <commpage.h>
#ifdef _COMPAT_MODE
#	include <commpage_compat.h>
#endif
#include <real_time_clock.h>
#include <real_time_data.h>
#include <syscalls.h>
#include <thread.h>

#include <stdlib.h>

//#define TRACE_TIME
#ifdef TRACE_TIME
#	define TRACE(x) dprintf x
#else
#	define TRACE(x)
#endif


#define RTC_SECONDS_DAY 86400
#define RTC_EPOCH_JULIAN_DAY 2440588
	// January 1st, 1970

static struct real_time_data *sRealTimeData;
#ifdef _COMPAT_MODE
static struct real_time_data *sRealTimeDataCompat;
#endif
static bool sIsGMT = false;
static bigtime_t sTimezoneOffset = 0;
static char sTimezoneName[B_FILE_NAME_LENGTH] = "GMT";


/**
 * @brief Notify the kernel timer and user-timer layers of a wall-clock change.
 */
static void
real_time_clock_changed()
{
	timer_real_time_clock_changed();
	user_timer_real_time_clock_changed();
}


/**
 * @brief Write the current wall-clock time into the hardware RTC.
 *
 * Honours sIsGMT: when the hardware clock is running in local time, the
 * current timezone offset is added before writing.
 */
static void
rtc_system_to_hw(void)
{
	uint64 seconds;

	seconds = (arch_rtc_get_system_time_offset(sRealTimeData) + system_time()
		+ (sIsGMT ? 0 : sTimezoneOffset)) / 1000000;

	arch_rtc_set_hw_time(seconds);
}


/**
 * @brief Read the hardware RTC and update the kernel wall-clock offset.
 *
 * Adds the timezone offset when the RTC runs in local time.
 */
static void
rtc_hw_to_system(void)
{
	uint64 current_time;

	current_time = arch_rtc_get_hw_time();
	set_real_time_clock(current_time + (sIsGMT ? 0 : sTimezoneOffset));
}


/**
 * @brief Return the wall-clock offset applied to system_time() at boot.
 * @return Microseconds between system_time() == 0 and the Unix epoch.
 */
bigtime_t
rtc_boot_time(void)
{
	return arch_rtc_get_system_time_offset(sRealTimeData);
}


/**
 * @brief Debugger command: print RTC state or set the wall-clock time.
 *
 * With no arguments prints system_time(), the current offset, and the
 * derived Unix time. With one numeric argument, passes it to
 * set_real_time_clock().
 *
 * @param argc Argument count.
 * @param argv Argument vector; argv[1] if present is seconds-since-epoch.
 * @return 0 on success.
 */
static int
rtc_debug(int argc, char **argv)
{
	if (argc < 2) {
		// If no arguments were given, output all useful data.
		uint32 currentTime;
		bigtime_t systemTimeOffset
			= arch_rtc_get_system_time_offset(sRealTimeData);

		currentTime = (systemTimeOffset + system_time()) / 1000000;
		dprintf("system_time:  %" B_PRId64 "\n", system_time());
		dprintf("system_time_offset:    %" B_PRId64 "\n", systemTimeOffset);
		dprintf("current_time: %" B_PRIu32 "\n", currentTime);
	} else {
		// If there was an argument, reset the system and hw time.
		set_real_time_clock(strtoul(argv[1], NULL, 10));
	}

	return 0;
}


/**
 * @brief Initialise real-time data, publish it to user space, and sync from HW.
 *
 * Allocates the commpage real_time_data entry (plus the compat-mode copy
 * when built with _COMPAT_MODE), invokes the arch layer, then seeds the
 * kernel clock from the hardware RTC.
 *
 * @param args Boot-time kernel arguments.
 * @return B_OK on success.
 */
status_t
rtc_init(kernel_args *args)
{
	sRealTimeData = (struct real_time_data*)allocate_commpage_entry(
		COMMPAGE_ENTRY_REAL_TIME_DATA, sizeof(struct real_time_data));
	arch_rtc_init(args, sRealTimeData);

#ifdef _COMPAT_MODE
	sRealTimeDataCompat = (struct real_time_data*)
		allocate_commpage_compat_entry(COMMPAGE_ENTRY_REAL_TIME_DATA,
		sizeof(struct real_time_data));
	arch_rtc_init(args, sRealTimeDataCompat);
#endif

	rtc_hw_to_system();

	add_debugger_command("rtc", &rtc_debug, "Set and test the real-time clock");
	return B_OK;
}


//	#pragma mark - public kernel API


/**
 * @brief Set the wall-clock time from a microsecond Unix timestamp.
 *
 * Updates the system_time() offset, writes the hardware RTC, and notifies
 * timer subsystems.
 *
 * @param currentTime Microseconds since the Unix epoch.
 */
void
set_real_time_clock_usecs(bigtime_t currentTime)
{
	arch_rtc_set_system_time_offset(sRealTimeData, currentTime
		- system_time());
#ifdef _COMPAT_MODE
	arch_rtc_set_system_time_offset(sRealTimeDataCompat, currentTime
		- system_time());
#endif
	rtc_system_to_hw();
	real_time_clock_changed();
}


/**
 * @brief Set the wall-clock time from a whole-seconds Unix timestamp.
 * @param currentTime Seconds since the Unix epoch.
 */
void
set_real_time_clock(unsigned long currentTime)
{
	set_real_time_clock_usecs((bigtime_t)currentTime * 1000000);
}


/**
 * @brief Return the current wall-clock time in whole seconds.
 * @return Seconds since the Unix epoch.
 */
unsigned long
real_time_clock(void)
{
	return (arch_rtc_get_system_time_offset(sRealTimeData) + system_time())
		/ 1000000;
}


/**
 * @brief Return the current wall-clock time in microseconds.
 * @return Microseconds since the Unix epoch.
 */
bigtime_t
real_time_clock_usecs(void)
{
	return arch_rtc_get_system_time_offset(sRealTimeData) + system_time();
}


/**
 * @brief Return the configured local-timezone offset in seconds.
 * @return Seconds east of UTC.
 */
uint32
get_timezone_offset(void)
{
	return (time_t)(sTimezoneOffset / 1000000LL);
}


// #pragma mark -


/**
 * @brief Convert a broken-down @c tm to seconds since the Unix epoch.
 *
 * Uses the Fliegel / van Flandern (1968) Julian-day formula. Note that
 * @c tm_year is relative to RTC_EPOCH_BASE_YEAR (1970), not 1900.
 *
 * @param tm Calendar time in UTC.
 * @return Seconds since 1970-01-01 00:00:00 UTC.
 */
uint64
rtc_tm_to_secs(const struct tm *tm)
{
	uint32 days;
	int year, month;

	month = tm->tm_mon + 1;
	year = tm->tm_year + RTC_EPOCH_BASE_YEAR;

	// Reference: Fliegel, H. F. and van Flandern, T. C. (1968).
	// Communications of the ACM, Vol. 11, No. 10 (October, 1968).
	days = tm->tm_mday - 32075 - RTC_EPOCH_JULIAN_DAY
		+ 1461 * (year + 4800 + (month - 14) / 12) / 4
		+ 367 * (month - 2 - 12 * ((month - 14) / 12)) / 12
		- 3 * ((year + 4900 + (month - 14) / 12) / 100) / 4;

	return (uint64)days * RTC_SECONDS_DAY + tm->tm_hour * 3600 + tm->tm_min * 60
		+ tm->tm_sec;
}


/**
 * @brief Convert seconds-since-epoch into a broken-down calendar time.
 *
 * Inverse of rtc_tm_to_secs(); @c tm_year is relative to
 * RTC_EPOCH_BASE_YEAR (1970).
 *
 * @param seconds Seconds since 1970-01-01 00:00:00 UTC.
 * @param t       Destination calendar time.
 */
void
rtc_secs_to_tm(uint64 seconds, struct tm *t)
{
	uint32 year, month, day, l, n;

	// Reference: Fliegel, H. F. and van Flandern, T. C. (1968).
	// Communications of the ACM, Vol. 11, No. 10 (October, 1968).
	l = seconds / 86400 + 68569 + RTC_EPOCH_JULIAN_DAY;
	n = 4 * l / 146097;
	l = l - (146097 * n + 3) / 4;
	year = 4000 * (l + 1) / 1461001;
	l = l - 1461 * year / 4 + 31;
	month = 80 * l / 2447;
	day = l - 2447 * month / 80;
	l = month / 11;
	month = month + 2 - 12 * l;
	year = 100 * (n - 49) + year + l;

	t->tm_mday = day;
	t->tm_mon = month - 1;
	t->tm_year = year - RTC_EPOCH_BASE_YEAR;

	seconds = seconds % RTC_SECONDS_DAY;
	t->tm_hour = seconds / 3600;

	seconds = seconds % 3600;
	t->tm_min = seconds / 60;
	t->tm_sec = seconds % 60;
}


//	#pragma mark - syscalls


/**
 * @brief Syscall entry point for system_time().
 * @return Microseconds since boot.
 */
bigtime_t
_user_system_time(void)
{
	syscall_64_bit_return_value();

	return system_time();
}


/**
 * @brief Syscall entry point for set_real_time_clock(); requires root.
 * @param time Microseconds since the Unix epoch.
 * @return B_OK on success, B_NOT_ALLOWED for non-root callers.
 */
status_t
_user_set_real_time_clock(bigtime_t time)
{
	if (geteuid() != 0)
		return B_NOT_ALLOWED;

	set_real_time_clock_usecs(time);
	return B_OK;
}


/**
 * @brief Syscall entry point: update the kernel's timezone; requires root.
 *
 * Adjusts the RTC offset so that wall-clock readings remain continuous when
 * the hardware clock is running in local time.
 *
 * @param timezoneOffset New offset east of UTC, in seconds.
 * @param name           Userspace buffer holding the timezone name, or NULL.
 * @param nameLength     Length of @a name in bytes.
 * @return B_OK on success, B_NOT_ALLOWED for non-root callers, B_BAD_ADDRESS
 *         if @a name is invalid.
 */
status_t
_user_set_timezone(int32 timezoneOffset, const char *name, size_t nameLength)
{
	bigtime_t offset = (bigtime_t)timezoneOffset * 1000000LL;

	if (geteuid() != 0)
		return B_NOT_ALLOWED;

	TRACE(("old system_time_offset %lld old %lld new %lld gmt %d\n",
		arch_rtc_get_system_time_offset(sRealTimeData), sTimezoneOffset,
		offset, sIsGMT));

	if (name != NULL && nameLength > 0) {
		if (!IS_USER_ADDRESS(name)
			|| user_strlcpy(sTimezoneName, name, sizeof(sTimezoneName)) < 0)
			return B_BAD_ADDRESS;
	}

	// We only need to update our time offset if the hardware clock
	// does not run in the local timezone.
	// Since this is shared data, we need to update it atomically.
	if (!sIsGMT) {
		arch_rtc_set_system_time_offset(sRealTimeData,
			arch_rtc_get_system_time_offset(sRealTimeData) + sTimezoneOffset
				- offset);
#ifdef _COMPAT_MODE
		arch_rtc_set_system_time_offset(sRealTimeDataCompat,
			arch_rtc_get_system_time_offset(sRealTimeDataCompat)
				+ sTimezoneOffset - offset);
#endif
		real_time_clock_changed();
	}

	sTimezoneOffset = offset;

	TRACE(("new system_time_offset %lld\n",
		arch_rtc_get_system_time_offset(sRealTimeData)));

	return B_OK;
}


/**
 * @brief Syscall entry point: return the current timezone offset and name.
 *
 * Either output pointer may be NULL.
 *
 * @param _timezoneOffset Userspace out-pointer for the offset in seconds.
 * @param userName        Userspace buffer to receive the timezone name.
 * @param nameLength      Size of @a userName in bytes.
 * @return B_OK on success, B_BAD_ADDRESS on invalid userspace pointers.
 */
status_t
_user_get_timezone(int32 *_timezoneOffset, char *userName, size_t nameLength)
{
	int32 offset = (int32)(sTimezoneOffset / 1000000LL);

	if (_timezoneOffset != NULL
		&& (!IS_USER_ADDRESS(_timezoneOffset)
			|| user_memcpy(_timezoneOffset, &offset, sizeof(offset)) < B_OK))
		return B_BAD_ADDRESS;

	if (userName != NULL
		&& (!IS_USER_ADDRESS(userName)
			|| user_strlcpy(userName, sTimezoneName, nameLength) < 0))
		return B_BAD_ADDRESS;

	return B_OK;
}


/**
 * @brief Syscall entry point: declare whether the RTC runs in GMT or local.
 *
 * Requires root. When the flag changes, the system-time offset is adjusted
 * by the current timezone offset so wall-clock readings remain continuous.
 *
 * @param isGMT true if the hardware RTC holds UTC, false for local time.
 * @return B_OK on success, B_NOT_ALLOWED for non-root callers.
 */
status_t
_user_set_real_time_clock_is_gmt(bool isGMT)
{
	// store previous value
	bool wasGMT = sIsGMT;
	if (geteuid() != 0)
		return B_NOT_ALLOWED;

	sIsGMT = isGMT;

	if (wasGMT != sIsGMT) {
		arch_rtc_set_system_time_offset(sRealTimeData,
			arch_rtc_get_system_time_offset(sRealTimeData)
				+ (sIsGMT ? 1 : -1) * sTimezoneOffset);
#ifdef _COMPAT_MODE
		arch_rtc_set_system_time_offset(sRealTimeDataCompat,
			arch_rtc_get_system_time_offset(sRealTimeDataCompat)
				+ (sIsGMT ? 1 : -1) * sTimezoneOffset);
#endif
		real_time_clock_changed();
	}

	return B_OK;
}


/**
 * @brief Syscall entry point: return whether the RTC is running in GMT.
 * @param _userIsGMT Userspace out-pointer for the boolean flag.
 * @return B_OK on success, B_BAD_VALUE if @a _userIsGMT is NULL,
 *         B_BAD_ADDRESS on invalid userspace pointer.
 */
status_t
_user_get_real_time_clock_is_gmt(bool *_userIsGMT)
{
	if (_userIsGMT == NULL)
		return B_BAD_VALUE;

	if (_userIsGMT != NULL
		&& (!IS_USER_ADDRESS(_userIsGMT)
			|| user_memcpy(_userIsGMT, &sIsGMT, sizeof(bool)) != B_OK))
		return B_BAD_ADDRESS;

	return B_OK;
}

