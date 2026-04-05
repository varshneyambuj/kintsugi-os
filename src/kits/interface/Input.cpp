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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2008 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers, mflerackers@androme.be
 */


/**
 * @file Input.cpp
 * @brief Input device management functions and the BInputDevice class
 *
 * Provides global functions for enumerating and controlling input devices
 * (keyboard, mouse, etc.) and the BInputDevice class for managing individual
 * input devices via the input server.
 *
 * @see BView
 */


#include <stdlib.h>
#include <string.h>
#include <new>

#include <Input.h>
#include <List.h>
#include <Message.h>

#include <input_globals.h>
#include <InputServerTypes.h>


/**
 * @brief Lazily-initialized BMessenger targeting the input_server application.
 *
 * Allocated on first use by _control_input_server_() and reused for all
 * subsequent input server communications.
 */
static BMessenger *sInputServer = NULL;


/**
 * @brief Finds and returns a BInputDevice with the given name.
 *
 * Sends an IS_FIND_DEVICES message to the input server requesting the device
 * named @a name. On success a new BInputDevice is allocated and populated with
 * the name and type returned by the server.
 *
 * @param name The device name to look up (e.g. "AT keyboard").
 * @return A newly allocated BInputDevice, or NULL if the device was not found
 *         or the input server could not be contacted. The caller takes
 *         ownership of the returned object.
 *
 * @see get_input_devices(), BInputDevice::Name()
 */
BInputDevice *
find_input_device(const char *name)
{
	BMessage command(IS_FIND_DEVICES);
	BMessage reply;

	command.AddString("device", name);

	status_t err = _control_input_server_(&command, &reply);

	if (err != B_OK)
		return NULL;

	BInputDevice *dev = new (std::nothrow) BInputDevice;
	if (dev == NULL)
		return NULL;

	const char *device;
	int32 type;

	reply.FindString("device", &device);
	reply.FindInt32("type", &type);

	dev->_SetNameAndType(device, (input_device_type)type);

	return dev;
}


/**
 * @brief Populates @a list with BInputDevice objects for every registered device.
 *
 * Sends an IS_FIND_DEVICES message with no filter to the input server, which
 * returns all currently registered devices. The list is cleared before being
 * populated. Each entry is a newly allocated BInputDevice; the caller is
 * responsible for deleting them.
 *
 * @param list The BList to fill with BInputDevice pointers.
 * @return B_OK on success, or an error code if the input server is unavailable.
 *
 * @see find_input_device(), watch_input_devices()
 */
status_t
get_input_devices(BList *list)
{
	list->MakeEmpty();

	BMessage command(IS_FIND_DEVICES);
	BMessage reply;

	status_t err = _control_input_server_(&command, &reply);

	if (err != B_OK)
		return err;

	const char *name;
	int32 type;
	int32 i = 0;

	while (reply.FindString("device", i, &name) == B_OK) {
		reply.FindInt32("type", i++, &type);

		BInputDevice *dev = new (std::nothrow) BInputDevice;
		if (dev != NULL) {
			dev->_SetNameAndType(name, (input_device_type)type);
			list->AddItem(dev);
		}
	}

	return err;
}


/**
 * @brief Subscribes or unsubscribes @a target from input device change notifications.
 *
 * Sends an IS_WATCH_DEVICES command to the input server. When @a start is true
 * the target messenger will begin receiving notifications when devices are
 * added or removed; when false, watching is stopped.
 *
 * @param target The BMessenger to notify of device changes.
 * @param start  true to begin watching, false to stop watching.
 * @return B_OK on success, or an error code if the request could not be sent.
 *
 * @see get_input_devices(), find_input_device()
 */
status_t
watch_input_devices(BMessenger target, bool start)
{
	BMessage command(IS_WATCH_DEVICES);
	BMessage reply;

	command.AddMessenger("target", target);
	command.AddBool("start", start);

	return _control_input_server_(&command, &reply);
}


/**
 * @brief Destroys the BInputDevice and frees its name string.
 */
BInputDevice::~BInputDevice()
{
	free(fName);
}


/**
 * @brief Returns the name of this input device.
 *
 * @return The device name string, or NULL if the device has not been
 *         initialized with a name.
 *
 * @see Type()
 */
const char *
BInputDevice::Name() const
{
	return fName;
}


/**
 * @brief Returns the type of this input device.
 *
 * @return The input_device_type (e.g. B_KEYBOARD_DEVICE, B_POINTING_DEVICE),
 *         or B_UNDEFINED_DEVICE if the type has not been set.
 *
 * @see Name()
 */
input_device_type
BInputDevice::Type() const
{
	return fType;
}


/**
 * @brief Queries the input server to determine whether this device is running.
 *
 * Sends an IS_IS_DEVICE_RUNNING message for this device's name.
 *
 * @return true if the input server confirms the device is running; false if the
 *         device name is not set or the server reports the device is stopped.
 *
 * @see Start(), Stop()
 */
bool
BInputDevice::IsRunning() const
{
	if (!fName)
		return false;

	BMessage command(IS_IS_DEVICE_RUNNING);
	BMessage reply;

	command.AddString("device", fName);

	return _control_input_server_(&command, &reply) == B_OK;
}


/**
 * @brief Requests the input server to start this device.
 *
 * @return B_OK if the device was started successfully, B_ERROR if the device
 *         name is not set, or another error code from the input server.
 *
 * @see Stop(), IsRunning()
 */
status_t
BInputDevice::Start()
{
	if (!fName)
		return B_ERROR;

	BMessage command(IS_START_DEVICE);
	BMessage reply;

	command.AddString("device", fName);

	return _control_input_server_(&command, &reply);
}


/**
 * @brief Requests the input server to stop this device.
 *
 * @return B_OK if the device was stopped successfully, B_ERROR if the device
 *         name is not set, or another error code from the input server.
 *
 * @see Start(), IsRunning()
 */
status_t
BInputDevice::Stop()
{
	if (!fName)
		return B_ERROR;

	BMessage command(IS_STOP_DEVICE);
	BMessage reply;

	command.AddString("device", fName);

	return _control_input_server_(&command, &reply);
}


/**
 * @brief Sends a control message to this specific input device.
 *
 * Delivers @a code and @a message to the input server for the named device.
 * On return, @a message is emptied and then repopulated with the reply data
 * from the server.
 *
 * @param code    A device-specific control code.
 * @param message A BMessage carrying additional control parameters; on return
 *                it contains the server's reply data.
 * @return B_OK on success, B_ERROR if the device name is not set, or an error
 *         code from the input server.
 *
 * @see Start(), Stop()
 */
status_t
BInputDevice::Control(uint32 code, BMessage *message)
{
	if (!fName)
		return B_ERROR;

	BMessage command(IS_CONTROL_DEVICES);
	BMessage reply;

	command.AddString("device", fName);
	command.AddInt32("code", code);
	command.AddMessage("message", message);

	message->MakeEmpty();

	status_t err = _control_input_server_(&command, &reply);

	if (err == B_OK)
		reply.FindMessage("message", message);

	return err;
}


/**
 * @brief Requests the input server to start all devices of the given type.
 *
 * @param type The input_device_type to start (e.g. B_KEYBOARD_DEVICE).
 * @return B_OK on success, or an error code from the input server.
 *
 * @see Stop(input_device_type), Control(input_device_type, uint32, BMessage*)
 */
status_t
BInputDevice::Start(input_device_type type)
{
	BMessage command(IS_START_DEVICE);
	BMessage reply;

	command.AddInt32("type", type);

	return _control_input_server_(&command, &reply);
}


/**
 * @brief Requests the input server to stop all devices of the given type.
 *
 * @param type The input_device_type to stop (e.g. B_POINTING_DEVICE).
 * @return B_OK on success, or an error code from the input server.
 *
 * @see Start(input_device_type), Control(input_device_type, uint32, BMessage*)
 */
status_t
BInputDevice::Stop(input_device_type type)
{
	BMessage command(IS_STOP_DEVICE);
	BMessage reply;

	command.AddInt32("type", type);

	return _control_input_server_(&command, &reply);
}


/**
 * @brief Sends a control message to all devices of the given type.
 *
 * Delivers @a code and @a message to the input server for every device
 * matching @a type. On return, @a message is emptied and repopulated with
 * the aggregated reply data from the server.
 *
 * @param type    The input_device_type to target.
 * @param code    A device-specific control code.
 * @param message A BMessage with control parameters; receives reply data on return.
 * @return B_OK on success, or an error code from the input server.
 *
 * @see Start(input_device_type), Stop(input_device_type)
 */
status_t
BInputDevice::Control(input_device_type type, uint32 code, BMessage *message)
{
	BMessage command(IS_CONTROL_DEVICES);
	BMessage reply;

	command.AddInt32("type", type);
	command.AddInt32("code", code);
	command.AddMessage("message", message);

	message->MakeEmpty();

	status_t err = _control_input_server_(&command, &reply);

	if (err == B_OK)
		reply.FindMessage("message", message);

	return err;
}


/**
 * @brief Default constructor; creates an uninitialized BInputDevice.
 *
 * The device name is NULL and the type is B_UNDEFINED_DEVICE until
 * _SetNameAndType() is called by the global factory functions.
 *
 * @see find_input_device(), get_input_devices()
 */
BInputDevice::BInputDevice()
	:
	fName(NULL),
	fType(B_UNDEFINED_DEVICE)
{
}


/**
 * @brief Sets the device name and type; used by factory functions after creation.
 *
 * Frees the old name (if any) with free(), then stores a strdup() copy of
 * @a name and assigns @a type.
 *
 * @param name The device name string to copy, or NULL to clear the name.
 * @param type The input_device_type to assign.
 *
 * @see find_input_device(), get_input_devices()
 */
void
BInputDevice::_SetNameAndType(const char *name, input_device_type type)
{
	if (fName) {
		free(fName);
		fName = NULL;
	}

	if (name)
		fName = strdup(name);

	fType = type;
}


/**
 * @brief Sends a command message to the input server and waits for its reply.
 *
 * Lazily allocates and caches a BMessenger targeting the input server
 * ("application/x-vnd.Be-input_server"). Re-establishes the messenger if it
 * becomes invalid. The send and receive timeouts are both 5 seconds.
 *
 * @param command The command BMessage to send to the input server.
 * @param reply   The BMessage that will receive the server's response.
 * @return B_OK if the round-trip succeeded and the server returned B_OK in
 *         its "status" field, B_NO_MEMORY if the messenger could not be
 *         allocated, B_ERROR if the reply lacks a "status" field, or another
 *         error code from the messaging layer.
 *
 * @see find_input_device(), get_input_devices(), watch_input_devices()
 */
status_t
_control_input_server_(BMessage *command, BMessage *reply)
{
	if (!sInputServer) {
		sInputServer = new (std::nothrow) BMessenger;
		if (!sInputServer)
			return B_NO_MEMORY;
	}

	if (!sInputServer->IsValid())
		*sInputServer = BMessenger("application/x-vnd.Be-input_server", -1, NULL);

	status_t err = sInputServer->SendMessage(command, reply, 5000000LL, 5000000LL);

	if (err != B_OK)
		return err;

	if (reply->FindInt32("status", &err) != B_OK)
		return B_ERROR;

	return err;
}
