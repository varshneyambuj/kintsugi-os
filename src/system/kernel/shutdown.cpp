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
 *   Copyright 2009, Olivier Coursière. All rights reserved.
 *   Copyright 2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file shutdown.cpp
 *  @brief System shutdown / reboot entry points. */


#include <kernel.h>
#include <syscalls.h>


/** @brief Tears down user teams, syncs the file system, and asks the arch layer to halt or reboot.
 *  @param reboot If true, reboot the machine; if false, power off.
 *  @return Should not return on success. */
status_t
system_shutdown(bool reboot)
{
	int32 cookie = 0;
	team_info info;
	
	gKernelShutdown = true;
	
	// Now shutdown all system services!
	// TODO: Once we are sure we can shutdown the system on all hardware
	// checking reboot may not be necessary anymore.
	if (reboot) {
		while (get_next_team_info(&cookie, &info) == B_OK) {
			if (info.team == B_SYSTEM_TEAM)
				continue;
			kill_team(info.team);
		}
	}
	
	sync();

	return arch_cpu_shutdown(reboot);
}


//	#pragma mark -


/** @brief Syscall entry point for shutdown(); requires the caller to be root. */
status_t
_user_shutdown(bool reboot)
{
	if (geteuid() != 0)
		return B_NOT_ALLOWED;
	return system_shutdown(reboot);
}

