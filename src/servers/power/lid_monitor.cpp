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
 *   Copyright 2013, Jerome Duval, korli@users.berlios.de.
 *   Copyright 2005, Nathan Whitehorn.
 *
 *   Distributed under the terms of the MIT License.
 */

/** @file lid_monitor.cpp
 *  @brief Monitors ACPI lid open/close events and handles them for power management. */


#include "lid_monitor.h"

#include <Messenger.h>
#include <Roster.h>

#include <stdio.h>

#include <RosterPrivate.h>


/**
 * @brief Constructs the lid monitor and opens the ACPI lid device.
 *
 * Attempts to open /dev/power/acpi_lid/0 for reading and, if successful,
 * inserts the file descriptor into the monitored descriptor set.
 */
LidMonitor::LidMonitor()
{
	int fd = open("/dev/power/acpi_lid/0", O_RDONLY);
	if (fd > 0)
		fFDs.insert(fd);
}


/** @brief Destroys the lid monitor and closes all monitored file descriptors. */
LidMonitor::~LidMonitor()
{
	for (std::set<int>::iterator it = fFDs.begin(); it != fFDs.end(); ++it)
		close(*it);
}


/**
 * @brief Handles a lid open/close event from the given file descriptor.
 *
 * Reads one byte of status from the descriptor. If the status byte
 * indicates the lid is open (value 1), a diagnostic message is printed.
 *
 * @param fd File descriptor that triggered the read-ready event.
 */
void
LidMonitor::HandleEvent(int fd)
{
	uint8 status;
	if (read(fd, &status, 1) != 1)
		return;

	if (status == 1)
		printf("lid status 1\n");
}
