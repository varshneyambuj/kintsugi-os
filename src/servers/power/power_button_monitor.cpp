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
 *   Copyright 2005-2014, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Rene Gollent
 *       Nathan Whitehorn
 */

/** @file power_button_monitor.cpp
 *  @brief Monitors hardware power button presses and initiates system shutdown. */


#include "power_button_monitor.h"

#include <string.h>

#include <Directory.h>
#include <Messenger.h>
#include <Roster.h>
#include <String.h>

#include <RosterPrivate.h>


static const char* kBasePath = "/dev/power/button";


PowerButtonMonitor::PowerButtonMonitor()
	:
	fFDs()
{
	BDirectory dir;
	if (dir.SetTo(kBasePath) != B_OK)
		return;

	entry_ref ref;
	while (dir.GetNextRef(&ref) == B_OK) {
		if (strncmp(ref.name, "power", 5) == 0) {
			BString path;
			path.SetToFormat("%s/%s", kBasePath, ref.name);
			int fd = open(path.String(), O_RDONLY);
			if (fd > 0)
				fFDs.insert(fd);
		}
	}
}


PowerButtonMonitor::~PowerButtonMonitor()
{
	for (std::set<int>::iterator it = fFDs.begin(); it != fFDs.end(); ++it)
		close(*it);
}


void
PowerButtonMonitor::HandleEvent(int fd)
{
	uint8 button_pressed;
	if (read(fd, &button_pressed, 1) != 1)
		return;

	if (button_pressed) {
		BRoster roster;
		BRoster::Private rosterPrivate(roster);

		rosterPrivate.ShutDown(false, false, false);
	}
}
