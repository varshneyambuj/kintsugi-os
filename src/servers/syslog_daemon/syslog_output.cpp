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
 *   Copyright 2003-2010, Axel Doerfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file syslog_output.cpp
 *  @brief Writes syslog messages to the on-disk log file with rotation support. */


#include "syslog_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <FindDirectory.h>
#include <Path.h>
#include <driver_settings.h>


/** @brief Human-readable names for the standard syslog facility codes. */
static const char *kFacilities[] = {
	"KERN", "USER", "MAIL", "DAEMON",
	"AUTH", "SYSLOGD", "LPR", "NEWS",
	"UUCP", "CRON", "AUTHPRIV", "FTP",
	"", "", "", "",
	"LOCAL0", "LOCAL1", "LOCAL2", "LOCAL3",
	"LOCAL4", "LOCAL5", "LOCAL6", "LOCAL7",
	NULL
};
/** @brief Total number of recognized syslog facility codes. */
static const int32 kNumFacilities = 24;

/** @brief File descriptor for the currently open syslog file, or -1 if none. */
static int sLog = -1;

/** @brief Buffer holding the most recently written message for repeat detection. */
static char sLastMessage[1024];

/** @brief Thread ID of the last message's originating thread. */
static thread_id sLastThread;

/** @brief Count of consecutive identical messages suppressed as repeats. */
static int32 sRepeatCount;

/** @brief Maximum allowed syslog file size before rotation, in bytes (default 512 KB). */
static size_t sLogMaxSize = 524288;	// 512kB

/** @brief Number of rotated syslog history files to keep. */
static int32 sMaxHistory = 1;

/** @brief Whether to include human-readable timestamps in log entries. */
static bool sLogTimeStamps = false;


/*!	Creates the log file if not yet existing, or renames the old
	log file, if it's too big already.
*/
static status_t
prepare_output()
{
	bool needNew = true;
	bool tooLarge = false;

	if (sLog >= 0) {
		// check file size
		struct stat stat;
		if (fstat(sLog, &stat) == 0) {
			if (stat.st_size < (off_t)sLogMaxSize)
				needNew = false;
			else
				tooLarge = true;
		}
	}

	if (needNew) {
		// close old file; it'll be (re)moved soon
		if (sLog >= 0)
			close(sLog);

		// get path (and create it if necessary)
		BPath base;
		find_directory(B_SYSTEM_LOG_DIRECTORY, &base, true);

		BPath syslog(base);
		syslog.Append("syslog");

		// move old files if they already exist
		if (tooLarge) {
			// remove latest syslog.X and rename others with a suffix incremented by one
			// syslog.6 -> syslog.7, syslog.5 -> syslog.6, ...
			for (int32 x = sMaxHistory; x >= 0; x--) {
				BString oldlog(syslog.Path());
				// no suffix on 0, just 'syslog'
				if (x > 0)
					oldlog << "." << x;

				if (x == sMaxHistory)
					remove(oldlog.String());
				else {
					// increment our suffix
					BString rotateTo(syslog.Path());
					rotateTo << "." << (x + 1);
					rename(oldlog.String(), rotateTo.String());
				}
			}
			// ToDo: just remove old file if space on device is tight?
		}

		bool haveSyslog = sLog >= 0;

		// open file
		sLog = open(syslog.Path(), O_APPEND | O_CREAT | O_WRONLY, 0644);
		if (!haveSyslog && sLog >= 0) {
			// first time open, check file size again
			prepare_output();
		}
	}

	return sLog >= 0 ? B_OK : B_ERROR;
}


/**
 * @brief Writes a buffer to the syslog file, flushing any pending repeat count first.
 *
 * If repeated messages were suppressed, emits a "Last message repeated N times"
 * line before writing the new content.
 *
 * @param buffer  The log line to write.
 * @param length  Number of bytes to write from @a buffer.
 * @return B_OK on success, B_ERROR if the write fails.
 */
static status_t
write_to_log(const char *buffer, int32 length)
{
	if (sRepeatCount > 0) {
		char repeat[64];
		ssize_t size = snprintf(repeat, sizeof(repeat),
			"Last message repeated %" B_PRId32 " time%s\n", sRepeatCount,
			sRepeatCount > 1 ? "s" : "");
		sRepeatCount = 0;
		if (write(sLog, repeat, strlen(repeat)) < size)
			return B_ERROR;
	}

	if (write(sLog, buffer, length) < length)
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Formats and writes a syslog message to the on-disk log file.
 *
 * Constructs a per-line header containing optional timestamp, facility name,
 * process identifier, and thread ID. Each line of the message body is
 * prefixed with this header and written to the log. Consecutive identical
 * messages are suppressed and counted for later batch reporting.
 *
 * @param message The syslog message to format and write.
 */
static void
syslog_output(syslog_message &message)
{
	char header[128];
	int32 headerLength;
	int32 pos = 0;

	if (sLogTimeStamps) {
		// parse & nicely print the time stamp from the message
		struct tm when;
		localtime_r(&message.when, &when);
		pos = strftime(header, sizeof(header), "%Y-%m-%d %H:%M:%S ", &when);
	}

	// add facility
	int facility = SYSLOG_FACILITY_INDEX(message.priority);
	if (facility >= kNumFacilities)
		facility = SYSLOG_FACILITY_INDEX(LOG_USER);
	pos += snprintf(header + pos, sizeof(header) - pos, "%s",
		kFacilities[facility]);

	// add ident/thread ID
	if (message.ident[0] == '\0') {
		// ToDo: find out team name?
	} else {
		pos += snprintf(header + pos, sizeof(header) - pos, " '%s'",
			message.ident);
	}

	if ((message.options & LOG_PID) != 0) {
		pos += snprintf(header + pos, sizeof(header) - pos, "[%" B_PRId32 "]",
			message.from);
	}

	headerLength = pos + strlcpy(header + pos, ": ", sizeof(header) - pos);
	if (headerLength >= (int32)sizeof(header))
		headerLength = sizeof(header) - 1;

	// add header to every line of the message and write it to the syslog

	char buffer[SYSLOG_MESSAGE_BUFFER_SIZE];
	pos = 0;

	while (true) {
		strcpy(buffer, header);
		int32 length;

		const char *newLine = strchr(message.message + pos, '\n');
		if (newLine != NULL) {
			length = newLine - message.message + 1 - pos;
			strlcpy(buffer + headerLength, message.message + pos, length + 1);
			pos += length;
		} else {
			length = strlcpy(buffer + headerLength, message.message + pos,
				sizeof(buffer) - headerLength);
			if (length == 0)
				break;
		}

		length += headerLength;

		if (length >= (int32)sizeof(buffer))
			length = sizeof(buffer) - 1;

		if (message.from == sLastThread
			&& !strncmp(buffer + headerLength, sLastMessage,
					sizeof(sLastMessage))) {
			// we got this message already
			sRepeatCount++;
		} else {
			// dump message line

			if (prepare_output() < B_OK
				|| write_to_log(buffer, length) < B_OK) {
				// cannot write to syslog!
				break;
			}

			// save this message to suppress repeated messages
			strlcpy(sLastMessage, buffer + headerLength, sizeof(sLastMessage));
			sLastThread = message.from;
		}

		if (newLine == NULL || !newLine[1]) {
			// wrote last line of output
			break;
		}
	}
}


/**
 * @brief Initializes the file-based syslog output handler.
 *
 * Loads kernel driver settings for timestamps, max log size, and history
 * depth, then registers the syslog_output handler with the daemon.
 *
 * @param daemon The syslog daemon to register the handler with.
 */
void
init_syslog_output(SyslogDaemon *daemon)
{
	void *handle;

	// get kernel syslog settings
	handle = load_driver_settings("kernel");
	if (handle != NULL) {
		sLogTimeStamps = get_driver_boolean_parameter(handle,
			"syslog_time_stamps", false, false);
		const char *param = get_driver_parameter(handle, "syslog_max_history", "1", "1");
		sMaxHistory = strtol(param, NULL, 0);
		if (sMaxHistory < 0)
			sMaxHistory = 0;
		param = get_driver_parameter(handle, "syslog_max_size", "0", "0");
		int maxSize = strtol(param, NULL, 0);
		if (strchr(param, 'k') || strchr(param, 'K'))
			maxSize *= 1024;
		else if (strchr(param, 'm') || strchr(param, 'M'))
			maxSize *= 1048576;
		if (maxSize > 0)
			sLogMaxSize = maxSize;

		unload_driver_settings(handle);
	}

	daemon->AddHandler(syslog_output);
}
