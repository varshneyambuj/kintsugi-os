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
 *   Copyright 2004, the Haiku project. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Author : Jérôme Duval
 *   Original authors: Marcus Overhagen, Axel Dörfler
 */

/** @file DeviceManager.h
 *  @brief BLooper that watches /dev for Bluetooth controller hot-plug events. */

#ifndef _DEVICE_MANAGER_H
#define _DEVICE_MANAGER_H

// Manager for devices monitoring
#include <Handler.h>
#include <Node.h>
#include <Looper.h>
#include <Locker.h>


/** @brief BLooper that monitors the device tree for Bluetooth controllers.
 *
 * Watches the directories that hold Bluetooth transport device nodes,
 * notifying the rest of the server when a controller appears or disappears,
 * and persists/restores its monitor list across daemon restarts. */
class DeviceManager : public BLooper {
	public:
		DeviceManager();
		~DeviceManager();

		/** @brief Loads the persisted set of monitored device paths from disk. */
		void		LoadState();
		/** @brief Persists the current set of monitored device paths to disk. */
		void		SaveState();

		/** @brief Begins watching @p device for transport-driver creation events. */
		status_t StartMonitoringDevice(const char* device);
		/** @brief Stops watching @p device. */
		status_t StopMonitoringDevice(const char* device);

		/** @brief Handles node-monitor and lifecycle messages. */
		void MessageReceived(BMessage *msg);

	private:
		status_t AddDirectory(node_ref* nref);
		status_t RemoveDirectory(node_ref* nref);
		status_t AddDevice(entry_ref* nref);

		BLocker fLock;  /**< Serialises access to the monitored set. */
};

#endif // _DEVICE_MANAGER_H
