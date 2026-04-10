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

/** @file power_button_monitor.h
 *  @brief Declaration of PowerButtonMonitor for hardware power button events. */

#ifndef _POWER_BUTTON_MONITOR_H
#define _POWER_BUTTON_MONITOR_H


#include "power_monitor.h"


/** @brief Monitors hardware power button devices and triggers system shutdown on press. */
class PowerButtonMonitor : public PowerMonitor {
public:
	/** @brief Scan /dev/power/button for power button devices and open them. */
								PowerButtonMonitor();
	virtual	 					~PowerButtonMonitor();

	/** @brief Handle a power button press event and initiate shutdown. */
	virtual void				HandleEvent(int fd);

	/** @brief Return the set of monitored file descriptors. */
	virtual const std::set<int>&
								FDs() const { return fFDs; }
private:
			std::set<int>		fFDs; /**< Set of open power button device file descriptors. */
};


#endif // _POWER_BUTTON_MONITOR_H
