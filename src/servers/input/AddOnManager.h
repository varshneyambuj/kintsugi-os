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
 *   Copyright 2004-2013, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Jérôme Duval
 *       Marcus Overhagen
 *       John Scipione, jscipione@gmail.com
 */

/** @file AddOnManager.h
 *  @brief Loads and tracks input server device, filter, and method add-ons. */

#ifndef ADD_ON_MANAGER_H
#define ADD_ON_MANAGER_H


#include <InputServerDevice.h>
#include <InputServerFilter.h>
#include <InputServerMethod.h>
#include <Locker.h>
#include <Looper.h>

#include <AddOnMonitor.h>
#include <AddOnMonitorHandler.h>

#include <set>

#include "PathList.h"


using namespace BPrivate;

/** @brief BLooper that watches add-on directories and (un)loads input server plugins.
 *
 * Discovers BInputServerDevice, BInputServerFilter, and BInputServerMethod
 * add-ons under the standard add-on directories, instantiates them, manages
 * device path monitoring on their behalf, and exposes a BMessage RPC surface
 * the rest of the input server uses to find and control them. */
class AddOnManager : public AddOnMonitor {
public:
								AddOnManager();
								~AddOnManager();

	/** @brief Dispatches RPC and add-on monitoring messages. */
	virtual	void 				MessageReceived(BMessage* message);

	/** @brief Restores the manager's state from the on-disk settings file. */
			void				LoadState();
	/** @brief Persists the manager's state to disk. */
			void				SaveState();

	/** @brief Begins watching @p device on behalf of an input device add-on. */
			status_t			StartMonitoringDevice(DeviceAddOn* addOn,
									const char* device);
	/** @brief Stops watching @p device on behalf of an input device add-on. */
			status_t			StopMonitoringDevice(DeviceAddOn* addOn,
									const char* device);

private:
			void				_RegisterAddOns();
			void				_UnregisterAddOns();

			status_t			_RegisterAddOn(BEntry& entry);
			status_t			_UnregisterAddOn(BEntry& entry);

			bool				_IsDevice(const char* path) const;
			bool				_IsFilter(const char* path) const;
			bool				_IsMethod(const char* path) const;

			status_t			_RegisterDevice(BInputServerDevice* device,
									const entry_ref& ref, image_id image);
			status_t			_RegisterFilter(BInputServerFilter* filter,
									const entry_ref& ref, image_id image);
			status_t			_RegisterMethod(BInputServerMethod* method,
									const entry_ref& ref, image_id image);

			status_t			_HandleFindDevices(BMessage* message,
									BMessage* reply);
			status_t			_HandleWatchDevices(BMessage* message,
									BMessage* reply);
			status_t			_HandleNotifyDevice(BMessage* message,
									BMessage* reply);
			status_t			_HandleIsDeviceRunning(BMessage* message,
									BMessage* reply);
			status_t			_HandleStartStopDevices(BMessage* message,
									BMessage* reply);
			status_t			_HandleControlDevices(BMessage* message,
									BMessage* reply);
			status_t			_HandleSystemShuttingDown(BMessage* message,
									BMessage* reply);
			status_t			_HandleMethodReplicant(BMessage* message,
									BMessage* reply);
			void				_HandleDeviceMonitor(BMessage* message);

			void				_LoadReplicant();
			void				_UnloadReplicant();
			int32				_GetReplicantAt(BMessenger target,
									int32 index) const;
			status_t			_GetReplicantName(BMessenger target,
									int32 uid, BMessage* reply) const;
			status_t			_GetReplicantView(BMessenger target, int32 uid,
									BMessage* reply) const;

			status_t			_AddDevicePath(DeviceAddOn* addOn,
									const char* path, bool& newPath);
			status_t			_RemoveDevicePath(DeviceAddOn* addOn,
									const char* path, bool& lastPath);

private:
	class MonitorHandler;
	friend class MonitorHandler;

	/** @brief Bundle of (entry_ref, image, add_on instance) for one loaded add-on. */
	template<typename T> struct add_on_info {
		add_on_info()
			:
			image(-1), add_on(NULL)
		{
		}

		~add_on_info()
		{
			delete add_on;
			if (image >= 0)
				unload_add_on(image);
		}

		entry_ref				ref;     /**< File system entry the add-on was loaded from. */
		image_id				image;   /**< Loaded image id, or -1. */
		T*						add_on;  /**< The add-on instance (owned). */
	};
	typedef struct add_on_info<BInputServerDevice> device_info;
	typedef struct add_on_info<BInputServerFilter> filter_info;
	typedef struct add_on_info<BInputServerMethod> method_info;

			BObjectList<device_info> fDeviceList;     /**< Loaded BInputServerDevice add-ons. */
			BObjectList<filter_info> fFilterList;     /**< Loaded BInputServerFilter add-ons. */
			BObjectList<method_info> fMethodList;     /**< Loaded BInputServerMethod add-ons. */

			BObjectList<DeviceAddOn> fDeviceAddOns;   /**< DeviceAddOn callbacks for path monitoring. */
			PathList			fDevicePaths;         /**< Currently monitored device paths. */

			MonitorHandler*		fHandler;             /**< Inner BHandler dispatching add-on monitor events. */
			std::set<BMessenger> fWatcherMessengerList;  /**< Clients subscribed to device-list change broadcasts. */

			bool				fSafeMode;            /**< True if the kernel reported safe-mode boot. */
};

#endif	// ADD_ON_MANAGER_H
