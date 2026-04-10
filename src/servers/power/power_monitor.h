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
 *   Copyright 2013-2014, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Jerome Duval, korli@users.berlios.de.
 *       Rene Gollent, rene@gollent.com.
 */

/** @file power_monitor.h
 *  @brief Abstract base class for power-event monitors (buttons, lids, etc.). */

#ifndef _POWER_MONITOR_H
#define _POWER_MONITOR_H


#include <set>


/** @brief Abstract interface for monitoring hardware power events via file descriptors. */
class PowerMonitor {
public:
	virtual	 					~PowerMonitor() {};

	/** @brief Handle a power-related event on the given file descriptor. */
	virtual	void				HandleEvent(int fd) = 0;

	/** @brief Return the set of file descriptors this monitor watches. */
	virtual	const std::set<int>&
								FDs() const = 0;
};


#endif // _POWER_MONITOR_H
