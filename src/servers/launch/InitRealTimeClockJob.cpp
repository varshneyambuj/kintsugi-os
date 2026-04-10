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


InitRealTimeClockJob::InitRealTimeClockJob()
	:
	BJob("init real time clock")
{
}


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
