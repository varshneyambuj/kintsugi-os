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
 *   Copyright 2002-2008, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file InputServerDevice.cpp
 *  @brief BInputServerDevice kit-side implementation and DeviceAddOn helpers. */


#include <InputServerDevice.h>

#include <new>

#include <Directory.h>
#include <Path.h>

#include "InputServer.h"


/**
 * @brief Constructs a DeviceAddOn wrapper around the given server device.
 *
 * @param device The BInputServerDevice that this add-on wraps.
 */
DeviceAddOn::DeviceAddOn(BInputServerDevice *device)
	:
	fDevice(device)
{
}


/**
 * @brief Destructor; stops monitoring all paths still registered with this add-on.
 */
DeviceAddOn::~DeviceAddOn()
{
	while (const char* path = fMonitoredPaths.PathAt(0)) {
		gInputServer->AddOnManager()->StopMonitoringDevice(this, path);
	}
}


/**
 * @brief Checks whether the given device path is being monitored by this add-on.
 *
 * @param path The device path to test.
 * @return @c true if the path is in the monitored list, @c false otherwise.
 */
bool
DeviceAddOn::HasPath(const char* path) const
{
	return fMonitoredPaths.HasPath(path);
}


/**
 * @brief Adds a device path to this add-on's monitored path list.
 *
 * @param path The device path to add.
 * @return B_OK on success, or an error code from PathList::AddPath().
 */
status_t
DeviceAddOn::AddPath(const char* path)
{
	return fMonitoredPaths.AddPath(path);
}


/**
 * @brief Removes a device path from this add-on's monitored path list.
 *
 * @param path The device path to remove.
 * @return B_OK on success, or B_ENTRY_NOT_FOUND if the path is not monitored.
 */
status_t
DeviceAddOn::RemovePath(const char* path)
{
	return fMonitoredPaths.RemovePath(path);
}


/**
 * @brief Returns the number of device paths currently monitored by this add-on.
 *
 * @return The path count.
 */
int32
DeviceAddOn::CountPaths() const
{
	return fMonitoredPaths.CountPaths();
}


//	#pragma mark -


/**
 * @brief Constructs a BInputServerDevice and creates its internal DeviceAddOn.
 */
BInputServerDevice::BInputServerDevice()
{
	fOwner = new(std::nothrow) DeviceAddOn(this);
}


/**
 * @brief Destructor; unregisters all devices and deletes the internal DeviceAddOn.
 */
BInputServerDevice::~BInputServerDevice()
{
	CALLED();

	gInputServer->UnregisterDevices(*this);
	delete fOwner;
}


/**
 * @brief Returns whether the device was initialized successfully.
 *
 * @return B_OK if the internal DeviceAddOn was allocated, or B_NO_MEMORY otherwise.
 */
status_t
BInputServerDevice::InitCheck()
{
	if (fOwner == NULL)
		return B_NO_MEMORY;
	return B_OK;
}


/**
 * @brief Called when the system is shutting down.
 *
 * The default implementation does nothing. Subclasses may override to
 * perform cleanup.
 *
 * @return B_OK.
 */
status_t
BInputServerDevice::SystemShuttingDown()
{
	return B_OK;
}


/**
 * @brief Starts producing events for the named device.
 *
 * The default implementation does nothing. Subclasses must override this.
 *
 * @param device Name of the device to start.
 * @param cookie Opaque handle previously associated with this device.
 * @return B_OK.
 */
status_t
BInputServerDevice::Start(const char* device, void* cookie)
{
	return B_OK;
}


/**
 * @brief Stops producing events for the named device.
 *
 * The default implementation does nothing. Subclasses must override this.
 *
 * @param device Name of the device to stop.
 * @param cookie Opaque handle previously associated with this device.
 * @return B_OK.
 */
status_t
BInputServerDevice::Stop(const char* device, void* cookie)
{
	return B_OK;
}


/**
 * @brief Sends a control message to the named device.
 *
 * The default implementation does nothing. Subclasses may override to handle
 * device-specific control codes.
 *
 * @param device  Name of the target device.
 * @param cookie  Opaque handle previously associated with this device.
 * @param code    Control code identifying the operation.
 * @param message Optional message carrying additional parameters.
 * @return B_OK.
 */
status_t
BInputServerDevice::Control(const char* device, void* cookie,
	uint32 code, BMessage* message)
{
	return B_OK;
}


/**
 * @brief Registers an array of input device references with the input server.
 *
 * @param devices NULL-terminated array of input_device_ref pointers to register.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BInputServerDevice::RegisterDevices(input_device_ref** devices)
{
	CALLED();
	return gInputServer->RegisterDevices(*this, devices);
}


/**
 * @brief Unregisters an array of input device references from the input server.
 *
 * @param devices NULL-terminated array of input_device_ref pointers to unregister,
 *                or NULL to unregister all devices owned by this object.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BInputServerDevice::UnregisterDevices(input_device_ref** devices)
{
    CALLED();
    // TODO: is this function supposed to remove devices that do not belong to this object?
    //	(at least that's what the previous implementation allowed for)
	return gInputServer->UnregisterDevices(*this, devices);
}


/**
 * @brief Enqueues an input event message into the input server's device event queue.
 *
 * @param message The event message to enqueue. Ownership transfers on success.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BInputServerDevice::EnqueueMessage(BMessage* message)
{
	return gInputServer->EnqueueDeviceMessage(message);
}


/**
 * @brief Begins monitoring a device path for node changes.
 *
 * The add-on manager will watch the given path (under /dev/ if not absolute)
 * and forward node monitor notifications to this device.
 *
 * @param device The device path to monitor.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BInputServerDevice::StartMonitoringDevice(const char* device)
{
	CALLED();
	PRINT(("StartMonitoringDevice %s\n", device));

	return gInputServer->AddOnManager()->StartMonitoringDevice(fOwner, device);
}


/**
 * @brief Stops monitoring a previously watched device path.
 *
 * @param device The device path to stop monitoring.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BInputServerDevice::StopMonitoringDevice(const char* device)
{
	return gInputServer->AddOnManager()->StopMonitoringDevice(fOwner, device);
}


/**
 * @brief Recursively scans a directory for device entries and sends B_INPUT_DEVICE_ADDED
 *        control messages for each file found.
 *
 * Directories are traversed recursively; regular files trigger a
 * Control() call with B_INPUT_DEVICE_ADDED.
 *
 * @param path The directory path to scan.
 * @return B_OK on success, or an error code if the directory cannot be opened.
 */
status_t
BInputServerDevice::AddDevices(const char* path)
{
	BDirectory directory;
	status_t status = directory.SetTo(path);
	if (status != B_OK)
		return status;

	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		BPath entryPath(&entry);

		if (entry.IsDirectory()) {
			AddDevices(entryPath.Path());
		} else {
			BMessage added(B_NODE_MONITOR);
			added.AddInt32("opcode", B_ENTRY_CREATED);
			added.AddString("path", entryPath.Path());

			Control(NULL, NULL, B_INPUT_DEVICE_ADDED, &added);
		}
	}

	return B_OK;
}


/** @brief Reserved for future binary compatibility. */
void BInputServerDevice::_ReservedInputServerDevice1() {}
/** @brief Reserved for future binary compatibility. */
void BInputServerDevice::_ReservedInputServerDevice2() {}
/** @brief Reserved for future binary compatibility. */
void BInputServerDevice::_ReservedInputServerDevice3() {}
/** @brief Reserved for future binary compatibility. */
void BInputServerDevice::_ReservedInputServerDevice4() {}
