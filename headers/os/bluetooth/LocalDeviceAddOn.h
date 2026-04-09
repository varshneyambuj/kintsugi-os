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
 *   (no original copyright block present)
 */

/** @file LocalDeviceAddOn.h
 *  @brief Add-on plug-in interface for extending LocalDevice behaviour with custom actions. */

#ifndef LOCAL_DEVICE_ADDON_H
#define LOCAL_DEVICE_ADDON_H

#include <bluetooth/LocalDevice.h>
#include <bluetooth/RemoteDevice.h>

/** @brief Abstract plug-in interface that add-on shared libraries must implement to extend
 *         LocalDevice functionality; exported via INSTANTIATE_LOCAL_DEVICE_ADDON. */
class LocalDeviceAddOn {

public:

	/** @brief Returns the human-readable name of this add-on.
	 *  @return Null-terminated string containing the add-on name. */
	virtual const char* GetName()=0;

	/** @brief Verifies that this add-on is usable with the given local device.
	 *  @param lDevice Pointer to the LocalDevice to check against.
	 *  @return B_OK if the add-on can operate, or an error code otherwise. */
	virtual status_t	InitCheck(LocalDevice* lDevice)=0;

	/** @brief Returns a short description of the action this add-on performs on a local device.
	 *  @return Null-terminated description string. */
	virtual const char* GetActionDescription()=0;

	/** @brief Executes this add-on's action on the given local device.
	 *  @param lDevice Pointer to the LocalDevice to act on.
	 *  @return B_OK on success, or an error code on failure. */
	virtual status_t	TakeAction(LocalDevice* lDevice)=0;

	/** @brief Returns a short description of the action this add-on performs on a remote device.
	 *  @return Null-terminated description string. */
	virtual const char* GetActionOnRemoteDescription()=0;

	/** @brief Executes this add-on's action involving both a local and a remote device.
	 *  @param lDevice Pointer to the LocalDevice.
	 *  @param rDevice Pointer to the target RemoteDevice.
	 *  @return B_OK on success, or an error code on failure. */
	virtual status_t 	TakeActionOnRemote(LocalDevice* lDevice, RemoteDevice* rDevice)=0;

	/** @brief Returns a short description of the device properties that this add-on overrides.
	 *  @return Null-terminated description string. */
	virtual const char* GetOverridenPropertiesDescription()=0;

	/** @brief Returns a BMessage containing the properties overridden by this add-on.
	 *  @param lDevice   Pointer to the LocalDevice whose properties may be overridden.
	 *  @param property  Name of the specific property being queried, or NULL for all.
	 *  @return Pointer to a BMessage with the overridden property values; caller takes ownership. */
	virtual BMessage*	OverridenProperties(LocalDevice* lDevice, const char* property)=0;

};

/** @brief Macro that defines the required factory function for a LocalDeviceAddOn subclass.
 *  @param addon The concrete LocalDeviceAddOn subclass to instantiate. */
#define INSTANTIATE_LOCAL_DEVICE_ADDON(addon) LocalDeviceAddOn* InstantiateLocalDeviceAddOn(){return new addon();}

/** @brief Macro that declares the DLL-export signature for the add-on factory function. */
#define EXPORT_LOCAL_DEVICE_ADDON extern "C" __declspec(dllexport) LocalDeviceAddOn* InstantiateLocalDeviceAddOn();



#endif
