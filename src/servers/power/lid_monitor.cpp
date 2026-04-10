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


LidMonitor::LidMonitor()
{
	int fd = open("/dev/power/acpi_lid/0", O_RDONLY);
	if (fd > 0)
		fFDs.insert(fd);
}


LidMonitor::~LidMonitor()
{
	for (std::set<int>::iterator it = fFDs.begin(); it != fFDs.end(); ++it)
		close(*it);
}


void
LidMonitor::HandleEvent(int fd)
{
	uint8 status;
	if (read(fd, &status, 1) != 1)
		return;

	if (status == 1)
		printf("lid status 1\n");
}
