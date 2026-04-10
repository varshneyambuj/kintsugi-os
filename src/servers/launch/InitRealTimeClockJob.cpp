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
 *   Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2004, Jérôme Duval, jerome.duval@free.fr.
 *   Copyright 2010, 2012, Oliver Tappe <zooey@hirschkaefer.de>
 *   Distributed under the terms of the MIT License.
 */

/** @file InitRealTimeClockJob.cpp
 *  @brief Implements the boot job that initializes the real-time clock and time zone offset. */


#include "InitRealTimeClockJob.h"

#include <stdio.h>

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <TimeZone.h>

#include <syscalls.h>


using BSupportKit::BJob;


/** @brief Constructs the real-time clock initialization job. */
InitRealTimeClockJob::InitRealTimeClockJob()
	:
	BJob("init real time clock")
{
}


/**
 * @brief Reads RTC and timezone settings from the user settings directory and applies them.
 *
 * Locates the user settings directory, then configures whether the RTC stores
 * GMT or local time and sets the system timezone offset.
 *
 * @return B_OK on success, or an error code if the settings directory cannot be found.
 */
status_t
InitRealTimeClockJob::Execute()
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status == B_OK) {
		_SetRealTimeClockIsGMT(path);
		_SetTimeZoneOffset(path);
	}
	return status;
}


/**
 * @brief Reads the RTC_time_settings file and tells the kernel whether the RTC uses GMT.
 *
 * Opens the "RTC_time_settings" file under @a path, reads its contents,
 * and calls _kern_set_real_time_clock_is_gmt() with the result. If the file
 * contains "local" the RTC is considered to store local time; otherwise GMT.
 *
 * @param path Base path to the user settings directory (will be modified).
 */
void
InitRealTimeClockJob::_SetRealTimeClockIsGMT(BPath path) const
{
	path.Append("RTC_time_settings");
	BFile file;
	status_t status = file.SetTo(path.Path(), B_READ_ONLY);
	if (status != B_OK) {
		fprintf(stderr, "Can't open RTC settings file\n");
		return;
	}

	char buffer[10];
	ssize_t bytesRead = file.Read(buffer, sizeof(buffer));
	if (bytesRead < 0) {
		fprintf(stderr, "Unable to read RTC settings file\n");
		return;
	}
	bool isGMT = strncmp(buffer, "local", 5) != 0;

	_kern_set_real_time_clock_is_gmt(isGMT);
	printf("RTC stores %s time.\n", isGMT ? "GMT" : "local" );
}


/**
 * @brief Reads the Time settings file and configures the kernel timezone offset.
 *
 * Opens the "Time settings" file under @a path, unflattens the BMessage
 * inside, extracts the "timezone" string, and calls _kern_set_timezone()
 * with the computed GMT offset.
 *
 * @param path Base path to the user settings directory (will be modified).
 */
void
InitRealTimeClockJob::_SetTimeZoneOffset(BPath path) const
{
	path.Append("Time settings");
	BFile file;
	status_t status = file.SetTo(path.Path(), B_READ_ONLY);
	if (status != B_OK) {
		fprintf(stderr, "Can't open Time settings file\n");
		return;
	}

	BMessage settings;
	status = settings.Unflatten(&file);
	if (status != B_OK) {
		fprintf(stderr, "Unable to parse Time settings file\n");
		return;
	}
	BString timeZoneID;
	if (settings.FindString("timezone", &timeZoneID) != B_OK) {
		fprintf(stderr, "No timezone found\n");
		return;
	}
	int32 timeZoneOffset = BTimeZone(timeZoneID.String()).OffsetFromGMT();

	_kern_set_timezone(timeZoneOffset, timeZoneID.String(),
		timeZoneID.Length());
	printf("timezone is %s, offset is %" B_PRId32 " seconds from GMT.\n",
		timeZoneID.String(), timeZoneOffset);
}
