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
 *   Copyright 2005-2013, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Nathan Whitehorn
 */

/** @file lid_monitor.h
 *  @brief Declaration of the LidMonitor class for ACPI lid events. */

#ifndef _LID_MONITOR_H
#define _LID_MONITOR_H


#include "power_monitor.h"


/** @brief Monitors laptop lid open/close events via the ACPI lid device. */
class LidMonitor : public PowerMonitor {
public:
	/** @brief Open the ACPI lid device for monitoring. */
								LidMonitor();
	virtual	 					~LidMonitor();

	/** @brief Handle a lid state change event on the given file descriptor. */
	virtual	void				HandleEvent(int fd);

	/** @brief Return the set of file descriptors being monitored. */
	virtual	const std::set<int>&
								FDs() const { return fFDs; }
private:
			std::set<int>		fFDs; /**< Set of open ACPI lid device file descriptors. */
};


#endif // _LID_MONITOR_H
