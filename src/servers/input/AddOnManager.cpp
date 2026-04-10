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
 *   Copyright 2004-2013 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Jérôme Duval
 *       Marcus Overhagen
 *       John Scipione, jscipione@gmail.com
 */

/** @file AddOnManager.cpp
 *  @brief Implementation of the input server add-on manager (devices, filters, methods). */


#include "AddOnManager.h"

#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <Autolock.h>
#include <Deskbar.h>
#include <Directory.h>
#include <Entry.h>
#include <image.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include <PathMonitor.h>

#include "InputServer.h"
#include "InputServerTypes.h"
#include "MethodReplicant.h"


#undef TRACE
//#define TRACE_ADD_ON_MONITOR
#ifdef TRACE_ADD_ON_MONITOR
#	define TRACE(x...) debug_printf(x)
#	define ERROR(x...) debug_printf(x)
#else
#	define TRACE(x...) ;
// TODO: probably better to the syslog
#	define ERROR(x...) debug_printf(x)
#endif



/**
 * @brief Internal handler that bridges AddOnMonitorHandler callbacks to AddOnManager.
 */
class AddOnManager::MonitorHandler : public AddOnMonitorHandler {
public:
	/**
	 * @brief Constructs the monitor handler with a back-pointer to its owning manager.
	 *
	 * @param manager The AddOnManager to notify when add-ons are enabled/disabled.
	 */
	MonitorHandler(AddOnManager* manager)
	{
		fManager = manager;
	}

	/**
	 * @brief Called when an add-on file is detected or enabled; registers it with the manager.
	 *
	 * @param entryInfo Filesystem information describing the newly enabled add-on.
	 */
	virtual void AddOnEnabled(const add_on_entry_info* entryInfo)
	{
		CALLED();
		entry_ref ref;
		make_entry_ref(entryInfo->dir_nref.device, entryInfo->dir_nref.node,
			entryInfo->name, &ref);
		BEntry entry(&ref, false);

		fManager->_RegisterAddOn(entry);
	}

	/**
	 * @brief Called when an add-on file is removed or disabled; unregisters it from the manager.
	 *
	 * @param entryInfo Filesystem information describing the disabled add-on.
	 */
	virtual void AddOnDisabled(const add_on_entry_info* entryInfo)
	{
		CALLED();
		entry_ref ref;
		make_entry_ref(entryInfo->dir_nref.device, entryInfo->dir_nref.node,
			entryInfo->name, &ref);
		BEntry entry(&ref, false);

		fManager->_UnregisterAddOn(entry);
	}

private:
	AddOnManager* fManager;
};


//	#pragma mark -


/**
 * @brief Loads and instantiates an input server add-on from a shared library image.
 *
 * Looks up the "instantiate_input_<type>" symbol in the loaded image, calls
 * it to create a new add-on instance, and runs InitCheck() on the result.
 *
 * @tparam T        The add-on base class (e.g. BInputServerDevice).
 * @param image     The image_id of the loaded shared library.
 * @param path      The filesystem path of the add-on (used for error messages).
 * @param type      The add-on type suffix ("device", "filter", or "method").
 * @return A newly created add-on instance, or NULL on any failure.
 */
template<class T> T*
instantiate_add_on(image_id image, const char* path, const char* type)
{
	T* (*instantiateFunction)();

	BString functionName = "instantiate_input_";
	functionName += type;

	if (get_image_symbol(image, functionName.String(), B_SYMBOL_TYPE_TEXT,
			(void**)&instantiateFunction) < B_OK) {
		ERROR("AddOnManager::_RegisterAddOn(): can't find %s() in \"%s\"\n",
			functionName.String(), path);
		return NULL;
	}

	T* addOn = (*instantiateFunction)();
	if (addOn == NULL) {
		ERROR("AddOnManager::_RegisterAddOn(): %s() in \"%s\" returned "
			"NULL\n", functionName.String(), path);
		return NULL;
	}

	status_t status = addOn->InitCheck();
	if (status != B_OK) {
		ERROR("AddOnManager::_RegisterAddOn(): InitCheck() in \"%s\" "
			"returned %s\n", path, strerror(status));
		delete addOn;
		return NULL;
	}

	return addOn;
}


//	#pragma mark - AddOnManager


/**
 * @brief Constructs the add-on manager and opens the system log.
 */
AddOnManager::AddOnManager()
	:
	AddOnMonitor(),
	fHandler(new(std::nothrow) MonitorHandler(this))
{
	openlog("input_server", LOG_PERROR, LOG_USER);
	SetHandler(fHandler);
}


/**
 * @brief Destructor; frees the monitor handler.
 */
AddOnManager::~AddOnManager()
{
	delete fHandler;
}


/**
 * @brief Dispatches incoming messages to the appropriate device/filter/method handler.
 *
 * Handles find, watch, notify, start/stop, control, system shutdown, and
 * method replicant registration messages.  Device path-monitor notifications
 * are handled separately.
 *
 * @param message The received message.
 */
void
AddOnManager::MessageReceived(BMessage* message)
{
	CALLED();

	BMessage reply;
	status_t status;

	TRACE("%s what: %.4s\n", __PRETTY_FUNCTION__, (char*)&message->what);

	switch (message->what) {
		case IS_FIND_DEVICES:
			status = _HandleFindDevices(message, &reply);
			break;
		case IS_WATCH_DEVICES:
			status = _HandleWatchDevices(message, &reply);
			break;
		case IS_NOTIFY_DEVICE:
			status = _HandleNotifyDevice(message, &reply);
			break;
		case IS_IS_DEVICE_RUNNING:
			status = _HandleIsDeviceRunning(message, &reply);
			break;
		case IS_START_DEVICE:
			status = _HandleStartStopDevices(message, &reply);
			break;
		case IS_STOP_DEVICE:
			status = _HandleStartStopDevices(message, &reply);
			break;
		case IS_CONTROL_DEVICES:
			status = _HandleControlDevices(message, &reply);
			break;
		case SYSTEM_SHUTTING_DOWN:
			status = _HandleSystemShuttingDown(message, &reply);
			break;
		case IS_METHOD_REGISTER:
			status = _HandleMethodReplicant(message, &reply);
			break;

		case B_PATH_MONITOR:
			_HandleDeviceMonitor(message);
			return;

		default:
			AddOnMonitor::MessageReceived(message);
			return;
	}

	reply.AddInt32("status", status);
	message->SendReply(&reply);
}


/**
 * @brief Scans add-on directories and registers all discovered add-ons.
 */
void
AddOnManager::LoadState()
{
	_RegisterAddOns();
}


/**
 * @brief Unregisters all add-ons, preparing for a clean shutdown.
 */
void
AddOnManager::SaveState()
{
	CALLED();
	_UnregisterAddOns();
}


/**
 * @brief Starts monitoring a device path for node changes on behalf of an add-on.
 *
 * Prepends "/dev/" if the path is not absolute.  Sets up BPathMonitor watching
 * the first time a path is registered.
 *
 * @param addOn  The device add-on requesting monitoring.
 * @param device The device path to watch.
 * @return B_OK on success, or an error code on failure.
 */
status_t
AddOnManager::StartMonitoringDevice(DeviceAddOn* addOn, const char* device)
{
	CALLED();

	BString path;
	if (device[0] != '/')
		path = "/dev/";
	path += device;

	TRACE("AddOnMonitor::StartMonitoringDevice(%s)\n", path.String());

	bool newPath;
	status_t status = _AddDevicePath(addOn, path.String(), newPath);
	if (status == B_OK && newPath) {
		status = BPathMonitor::StartWatching(path.String(),
			B_WATCH_FILES_ONLY | B_WATCH_RECURSIVELY, this);
		if (status != B_OK) {
			bool lastPath;
			_RemoveDevicePath(addOn, path.String(), lastPath);
		}
	}

	return status;
}


/**
 * @brief Stops monitoring a device path for the given add-on.
 *
 * If this was the last add-on watching the path, BPathMonitor is stopped as well.
 *
 * @param addOn  The device add-on that no longer needs monitoring.
 * @param device The device path to stop watching.
 * @return B_OK on success, or an error code on failure.
 */
status_t
AddOnManager::StopMonitoringDevice(DeviceAddOn* addOn, const char *device)
{
	CALLED();

	BString path;
	if (device[0] != '/')
		path = "/dev/";
	path += device;

	TRACE("AddOnMonitor::StopMonitoringDevice(%s)\n", path.String());

	bool lastPath;
	status_t status = _RemoveDevicePath(addOn, path.String(), lastPath);
	if (status == B_OK && lastPath)
		BPathMonitor::StopWatching(path.String(), this);

	return status;
}


// #pragma mark -


/**
 * @brief Adds monitor directories for devices, filters, and methods add-ons.
 */
void
AddOnManager::_RegisterAddOns()
{
	CALLED();
	BAutolock locker(this);

	fHandler->AddAddOnDirectories("input_server/devices");
	fHandler->AddAddOnDirectories("input_server/filters");
	fHandler->AddAddOnDirectories("input_server/methods");
}


/**
 * @brief Stops all devices and removes all device, filter, and method add-on info entries.
 */
void
AddOnManager::_UnregisterAddOns()
{
	BAutolock locker(this);

	// We have to stop manually the add-ons because the monitor doesn't
	// disable them on exit

	while (device_info* info = fDeviceList.RemoveItemAt(0)) {
		gInputServer->StartStopDevices(*info->add_on, false);
		delete info;
	}

	// TODO: what about the filters/methods lists in the input_server?

	while (filter_info* info = fFilterList.RemoveItemAt(0)) {
		delete info;
	}

	while (method_info* info = fMethodList.RemoveItemAt(0)) {
		delete info;
	}
}


/**
 * @brief Returns whether the given path refers to a device add-on.
 *
 * @param path The filesystem path to test.
 * @return @c true if the path contains "input_server/devices".
 */
bool
AddOnManager::_IsDevice(const char* path) const
{
	return strstr(path, "input_server/devices") != 0;
}


/**
 * @brief Returns whether the given path refers to a filter add-on.
 *
 * @param path The filesystem path to test.
 * @return @c true if the path contains "input_server/filters".
 */
bool
AddOnManager::_IsFilter(const char* path) const
{
	return strstr(path, "input_server/filters") != 0;
}


/**
 * @brief Returns whether the given path refers to a method add-on.
 *
 * @param path The filesystem path to test.
 * @return @c true if the path contains "input_server/methods".
 */
bool
AddOnManager::_IsMethod(const char* path) const
{
	return strstr(path, "input_server/methods") != 0;
}


/**
 * @brief Loads a single add-on image and registers it as a device, filter, or method.
 *
 * Determines the add-on type from the path, instantiates the appropriate
 * class, and delegates to the type-specific registration method.
 *
 * @param entry The filesystem entry for the add-on shared library.
 * @return B_OK on success, or an error code on failure (image is unloaded on error).
 */
status_t
AddOnManager::_RegisterAddOn(BEntry& entry)
{
	BPath path(&entry);

	entry_ref ref;
	status_t status = entry.GetRef(&ref);
	if (status < B_OK)
		return status;

	TRACE("AddOnManager::RegisterAddOn(): trying to load \"%s\"\n",
		path.Path());

	image_id image = load_add_on(path.Path());
	if (image < B_OK) {
		ERROR("load addon %s failed\n", path.Path());
		return image;
	}

	status = B_ERROR;

	if (_IsDevice(path.Path())) {
		BInputServerDevice* device = instantiate_add_on<BInputServerDevice>(
			image, path.Path(), "device");
		if (device != NULL)
			status = _RegisterDevice(device, ref, image);
	} else if (_IsFilter(path.Path())) {
		BInputServerFilter* filter = instantiate_add_on<BInputServerFilter>(
			image, path.Path(), "filter");
		if (filter != NULL)
			status = _RegisterFilter(filter, ref, image);
	} else if (_IsMethod(path.Path())) {
		BInputServerMethod* method = instantiate_add_on<BInputServerMethod>(
			image, path.Path(), "method");
		if (method != NULL)
			status = _RegisterMethod(method, ref, image);
	} else {
		ERROR("AddOnManager::_RegisterAddOn(): addon type not found for "
			"\"%s\" \n", path.Path());
	}

	if (status != B_OK)
		unload_add_on(image);

	return status;
}


/**
 * @brief Unregisters and unloads a previously registered add-on.
 *
 * Stops the device, removes the filter from the global filter list, or removes
 * the method from the global method list and updates the Deskbar replicant.
 *
 * @param entry The filesystem entry for the add-on to remove.
 * @return B_OK.
 */
status_t
AddOnManager::_UnregisterAddOn(BEntry& entry)
{
	BPath path(&entry);

	entry_ref ref;
	status_t status = entry.GetRef(&ref);
	if (status < B_OK)
		return status;

	TRACE("AddOnManager::UnregisterAddOn(): trying to unload \"%s\"\n",
		path.Path());

	BAutolock _(this);

	if (_IsDevice(path.Path())) {
		for (int32 i = fDeviceList.CountItems(); i-- > 0;) {
			device_info* info = fDeviceList.ItemAt(i);
			if (!strcmp(info->ref.name, ref.name)) {
				gInputServer->StartStopDevices(*info->add_on, false);
				delete fDeviceList.RemoveItemAt(i);
				break;
			}
		}
	} else if (_IsFilter(path.Path())) {
		for (int32 i = fFilterList.CountItems(); i-- > 0;) {
			filter_info* info = fFilterList.ItemAt(i);
			if (!strcmp(info->ref.name, ref.name)) {
				BAutolock locker(InputServer::gInputFilterListLocker);
				InputServer::gInputFilterList.RemoveItem(info->add_on);
				delete fFilterList.RemoveItemAt(i);
				break;
			}
		}
	} else if (_IsMethod(path.Path())) {
		BInputServerMethod* method = NULL;

		for (int32 i = fMethodList.CountItems(); i-- > 0;) {
			method_info* info = fMethodList.ItemAt(i);
			if (!strcmp(info->ref.name, ref.name)) {
				BAutolock locker(InputServer::gInputMethodListLocker);
				InputServer::gInputMethodList.RemoveItem(info->add_on);
				method = info->add_on;
					// this will only be used as a cookie, and not referenced
					// anymore
				delete fMethodList.RemoveItemAt(i);
				break;
			}
		}

		if (fMethodList.CountItems() <= 0) {
			// we remove the method replicant
			BDeskbar().RemoveItem(REPLICANT_CTL_NAME);
			gInputServer->SetMethodReplicant(NULL);
		} else if (method != NULL) {
			BMessage msg(IS_REMOVE_METHOD);
			msg.AddInt32("cookie", method->fOwner->Cookie());
			if (gInputServer->MethodReplicant())
				gInputServer->MethodReplicant()->SendMessage(&msg);
		}
	}

	return B_OK;
}


/**
 * @brief Registers a device add-on, taking ownership of it regardless of success.
 *
 * Checks for duplicate registrations by name.  On success the device_info
 * is added to fDeviceList.
 *
 * @param device      The instantiated device add-on (ownership taken).
 * @param ref         The entry_ref identifying the add-on file.
 * @param addOnImage  The loaded add-on image ID.
 * @return B_OK on success, B_NAME_IN_USE if already registered, or B_NO_MEMORY.
 */
status_t
AddOnManager::_RegisterDevice(BInputServerDevice* device, const entry_ref& ref,
	image_id addOnImage)
{
	BAutolock locker(this);

	for (int32 i = fDeviceList.CountItems(); i-- > 0;) {
		device_info* info = fDeviceList.ItemAt(i);
		if (!strcmp(info->ref.name, ref.name)) {
			// we already know this device
			delete device;
			return B_NAME_IN_USE;
		}
	}

	TRACE("AddOnManager::RegisterDevice, name %s\n", ref.name);

	device_info* info = new(std::nothrow) device_info;
	if (info == NULL) {
		delete device;
		return B_NO_MEMORY;
	}

	info->ref = ref;
	info->add_on = device;

	if (!fDeviceList.AddItem(info)) {
		delete info;
		return B_NO_MEMORY;
	}

	info->image = addOnImage;

	return B_OK;
}


/**
 * @brief Registers a filter add-on, taking ownership of it regardless of success.
 *
 * Checks for duplicates, adds the filter to fFilterList and the global
 * InputServer::gInputFilterList.
 *
 * @param filter      The instantiated filter add-on (ownership taken).
 * @param ref         The entry_ref identifying the add-on file.
 * @param addOnImage  The loaded add-on image ID.
 * @return B_OK on success, B_NAME_IN_USE if already registered, or B_NO_MEMORY.
 */
status_t
AddOnManager::_RegisterFilter(BInputServerFilter* filter, const entry_ref& ref,
	image_id addOnImage)
{
	BAutolock _(this);

	for (int32 i = fFilterList.CountItems(); i-- > 0;) {
		filter_info* info = fFilterList.ItemAt(i);
		if (strcmp(info->ref.name, ref.name) == 0) {
			// we already know this ref
			delete filter;
			return B_NAME_IN_USE;
		}
	}

	TRACE("%s, name %s\n", __PRETTY_FUNCTION__, ref.name);

	filter_info* info = new(std::nothrow) filter_info;
	if (info == NULL) {
		delete filter;
		return B_NO_MEMORY;
	}

	info->ref = ref;
	info->add_on = filter;

	if (!fFilterList.AddItem(info)) {
		delete info;
		return B_NO_MEMORY;
	}

	BAutolock locker(InputServer::gInputFilterListLocker);
	if (!InputServer::gInputFilterList.AddItem(filter)) {
		fFilterList.RemoveItem(info, false);
		delete info;
		return B_NO_MEMORY;
	}

	info->image = addOnImage;

	return B_OK;
}


/**
 * @brief Registers a method add-on, taking ownership of it regardless of success.
 *
 * Checks for duplicates, adds the method to fMethodList and the global
 * InputServer::gInputMethodList.  Loads the Deskbar method replicant if
 * this is the first method being registered.
 *
 * @param method      The instantiated method add-on (ownership taken).
 * @param ref         The entry_ref identifying the add-on file.
 * @param addOnImage  The loaded add-on image ID.
 * @return B_OK on success, B_NAME_IN_USE if already registered, or B_NO_MEMORY.
 */
status_t
AddOnManager::_RegisterMethod(BInputServerMethod* method, const entry_ref& ref,
	image_id addOnImage)
{
	BAutolock _(this);

	for (int32 i = fMethodList.CountItems(); i-- > 0;) {
		method_info* info = fMethodList.ItemAt(i);
		if (!strcmp(info->ref.name, ref.name)) {
			// we already know this ref
			delete method;
			return B_NAME_IN_USE;
		}
	}

	TRACE("%s, name %s\n", __PRETTY_FUNCTION__, ref.name);

	method_info* info = new(std::nothrow) method_info;
	if (info == NULL) {
		delete method;
		return B_NO_MEMORY;
	}

	info->ref = ref;
	info->add_on = method;

	if (!fMethodList.AddItem(info)) {
		delete info;
		return B_NO_MEMORY;
	}

	BAutolock locker(InputServer::gInputMethodListLocker);
	if (!InputServer::gInputMethodList.AddItem(method)) {
		fMethodList.RemoveItem(info);
		delete info;
		return B_NO_MEMORY;
	}

	info->image = addOnImage;

	if (gInputServer->MethodReplicant() == NULL) {
		_LoadReplicant();

		if (gInputServer->MethodReplicant()) {
			_BMethodAddOn_ *addon = InputServer::gKeymapMethod.fOwner;
			addon->AddMethod();
		}
	}

	if (gInputServer->MethodReplicant() != NULL) {
		_BMethodAddOn_ *addon = method->fOwner;
		addon->AddMethod();
	}

	return B_OK;
}


// #pragma mark -


/**
 * @brief Removes the input method replicant from the Deskbar.
 */
void
AddOnManager::_UnloadReplicant()
{
	BDeskbar().RemoveItem(REPLICANT_CTL_NAME);
}


/**
 * @brief Installs the input method replicant in the Deskbar and locates its messenger.
 *
 * Adds the replicant via BDeskbar, then queries the Deskbar shelf to find
 * the replicant's messenger and stores it in the global InputServer.
 */
void
AddOnManager::_LoadReplicant()
{
	CALLED();
	app_info info;
	be_app->GetAppInfo(&info);

	status_t err = BDeskbar().AddItem(&info.ref);
	if (err != B_OK)
		ERROR("Deskbar refuses to add method replicant: %s\n", strerror(err));

	BMessage request(B_GET_PROPERTY);
	BMessenger to;
	BMessenger status;

	request.AddSpecifier("Messenger");
	request.AddSpecifier("Shelf");

	// In the Deskbar the Shelf is in the View "Status" in Window "Deskbar"
	request.AddSpecifier("View", "Status");
	request.AddSpecifier("Window", "Deskbar");
	to = BMessenger("application/x-vnd.Be-TSKB", -1);

	BMessage reply;

	if (to.SendMessage(&request, &reply) == B_OK
		&& reply.FindMessenger("result", &status) == B_OK) {
		// enum replicant in Status view
		int32 index = 0;
		int32 uid;
		while ((uid = _GetReplicantAt(status, index++)) >= B_OK) {
			BMessage replicantInfo;
			if (_GetReplicantName(status, uid, &replicantInfo) != B_OK)
				continue;

			const char *name;
			if (replicantInfo.FindString("result", &name) == B_OK
				&& !strcmp(name, REPLICANT_CTL_NAME)) {
				BMessage replicant;
				if (_GetReplicantView(status, uid, &replicant) == B_OK) {
					BMessenger result;
					if (replicant.FindMessenger("result", &result) == B_OK) {
						gInputServer->SetMethodReplicant(new BMessenger(result));
					}
				}
			}
		}
	}

	if (!gInputServer->MethodReplicant()) {
		ERROR("LoadReplicant(): Method replicant not found!\n");
	}
}


/**
 * @brief Retrieves the unique ID of the replicant at the given index in the target shelf.
 *
 * @param target Messenger addressing the Deskbar shelf.
 * @param index  Zero-based index of the replicant.
 * @return The unique replicant ID on success, or a negative error code.
 */
int32
AddOnManager::_GetReplicantAt(BMessenger target, int32 index) const
{
	// So here we want to get the Unique ID of the replicant at the given index
	// in the target Shelf.

	BMessage request(B_GET_PROPERTY);// We're getting the ID property
	BMessage reply;
	status_t err;

	request.AddSpecifier("ID");// want the ID
	request.AddSpecifier("Replicant", index);// of the index'th replicant

	if ((err = target.SendMessage(&request, &reply)) != B_OK)
		return err;

	int32 uid;
	if ((err = reply.FindInt32("result", &uid)) != B_OK)
		return err;

	return uid;
}


/**
 * @brief Queries the Deskbar shelf for the name of the replicant with the given UID.
 *
 * @param target Messenger addressing the Deskbar shelf.
 * @param uid    The unique replicant ID to query.
 * @param reply  Output message containing the "result" string on success.
 * @return B_OK on success, or an error code.
 */
status_t
AddOnManager::_GetReplicantName(BMessenger target, int32 uid,
	BMessage* reply) const
{
	// We send a message to the target shelf, asking it for the Name of the
	// replicant with the given unique id.

	BMessage request(B_GET_PROPERTY);
	BMessage uid_specifier(B_ID_SPECIFIER);// specifying via ID
	status_t err;
	status_t e;

	request.AddSpecifier("Name");// ask for the Name of the replicant

	// IDs are specified using code like the following 3 lines:
	uid_specifier.AddInt32("id", uid);
	uid_specifier.AddString("property", "Replicant");
	request.AddSpecifier(&uid_specifier);

	if ((err = target.SendMessage(&request, reply)) != B_OK)
		return err;

	if (((err = reply->FindInt32("error", &e)) != B_OK) || (e != B_OK))
		return err ? err : e;

	return B_OK;
}


/**
 * @brief Queries the Deskbar shelf for the view messenger of the replicant with the given UID.
 *
 * @param target Messenger addressing the Deskbar shelf.
 * @param uid    The unique replicant ID to query.
 * @param reply  Output message containing the "result" messenger on success.
 * @return B_OK on success, or an error code.
 */
status_t
AddOnManager::_GetReplicantView(BMessenger target, int32 uid,
	BMessage* reply) const
{
	// We send a message to the target shelf, asking it for the Name of the
	// replicant with the given unique id.

	BMessage request(B_GET_PROPERTY);
	BMessage uid_specifier(B_ID_SPECIFIER);
		// specifying via ID
	status_t err;
	status_t e;

	request.AddSpecifier("View");
		// ask for the Name of the replicant

	// IDs are specified using code like the following 3 lines:
	uid_specifier.AddInt32("id", uid);
	uid_specifier.AddString("property", "Replicant");
	request.AddSpecifier(&uid_specifier);

	if ((err = target.SendMessage(&request, reply)) != B_OK)
		return err;

	if (((err = reply->FindInt32("error", &e)) != B_OK) || (e != B_OK))
		return err ? err : e;

	return B_OK;
}


/**
 * @brief Handles IS_START_DEVICE / IS_STOP_DEVICE messages by starting or stopping devices.
 *
 * Exactly one of "device" (by name) or "type" (by type) must be present in the message.
 *
 * @param message The request message.
 * @param reply   The reply message (status will be added by the caller).
 * @return B_OK on success, or B_ERROR if the request is malformed.
 */
status_t
AddOnManager::_HandleStartStopDevices(BMessage* message, BMessage* reply)
{
	const char *name = NULL;
	int32 type = 0;
	if (!((message->FindInt32("type", &type) != B_OK)
			^ (message->FindString("device", &name) != B_OK)))
		return B_ERROR;

	return gInputServer->StartStopDevices(name, (input_device_type)type,
		message->what == IS_START_DEVICE);
}


/**
 * @brief Handles IS_FIND_DEVICES by returning matching device information.
 *
 * If a specific "device" name is provided, returns its type; otherwise
 * returns information about all registered devices.
 *
 * @param message The request message.
 * @param reply   The reply message to populate with device info.
 * @return B_OK on success, or B_NAME_NOT_FOUND if a named device is not registered.
 */
status_t
AddOnManager::_HandleFindDevices(BMessage* message, BMessage* reply)
{
	CALLED();
	const char *name = NULL;
	input_device_type type;
	if (message->FindString("device", &name) == B_OK) {
		if (gInputServer->GetDeviceInfo(name, &type) != B_OK)
			return B_NAME_NOT_FOUND;
		reply->AddString("device", name);
		reply->AddInt32("type", type);
	} else {
		gInputServer->GetDeviceInfos(reply);
	}
	return B_OK;
}


/**
 * @brief Handles IS_WATCH_DEVICES by adding or removing a device change watcher.
 *
 * @param message The request containing "target" (BMessenger) and "start" (bool).
 * @param reply   The reply message.
 * @return B_OK on success, or B_ERROR / B_BAD_VALUE on invalid requests.
 */
status_t
AddOnManager::_HandleWatchDevices(BMessage* message, BMessage* reply)
{
	BMessenger watcherMessenger;
	if (message->FindMessenger("target", &watcherMessenger) != B_OK)
		return B_ERROR;

	bool startWatching;
	if (message->FindBool("start", &startWatching) != B_OK)
		return B_ERROR;

	if (fWatcherMessengerList.find(watcherMessenger)
		== fWatcherMessengerList.end()) {
		if (startWatching)
			fWatcherMessengerList.insert(watcherMessenger);
		else
			return B_BAD_VALUE;
	} else {
		if (!startWatching)
			fWatcherMessengerList.erase(watcherMessenger);
	}

	return B_OK;
}


/**
 * @brief Broadcasts a device-change notification to all registered watchers.
 *
 * The message must contain "added" or "started" booleans, a "name" string,
 * and a "type" int32.  Invalid or disconnected watchers are pruned.
 *
 * @param message The notification message from the input server.
 * @param reply   The reply message.
 * @return B_OK on success, or B_BAD_VALUE if required fields are missing.
 */
status_t
AddOnManager::_HandleNotifyDevice(BMessage* message, BMessage* reply)
{
	if (!message->HasBool("added") && !message->HasBool("started"))
		return B_BAD_VALUE;

	syslog(LOG_NOTICE, "Notify of added/removed/started/stopped device");

	BMessage changeMessage(B_INPUT_DEVICES_CHANGED);

	bool deviceAdded;
	if (message->FindBool("added", &deviceAdded) == B_OK) {
		if (deviceAdded)
			changeMessage.AddInt32("be:opcode", B_INPUT_DEVICE_ADDED);
		else
			changeMessage.AddInt32("be:opcode", B_INPUT_DEVICE_REMOVED);
	}

	bool deviceStarted;
	if (message->FindBool("started", &deviceStarted) == B_OK) {
		if (deviceStarted)
			changeMessage.AddInt32("be:opcode", B_INPUT_DEVICE_STARTED);
		else
			changeMessage.AddInt32("be:opcode", B_INPUT_DEVICE_STOPPED);
	}

	BString deviceName;
	if (message->FindString("name", &deviceName) != B_OK)
		return B_BAD_VALUE;

	changeMessage.AddString("be:device_name", deviceName);

	input_device_type deviceType = B_UNDEFINED_DEVICE;
	if (message->FindInt32("type", deviceType) != B_OK)
		return B_BAD_VALUE;

	changeMessage.AddInt32("be:device_type", deviceType);

	std::set<BMessenger>::iterator it = fWatcherMessengerList.begin();
	while (it != fWatcherMessengerList.end()) {
		const BMessenger& currentMessenger = *it;

		status_t result = currentMessenger.SendMessage(&changeMessage);

		if (result != B_OK && !currentMessenger.IsValid())
			fWatcherMessengerList.erase(it++);
		else
			it++;
	}

	return B_OK;
}


/**
 * @brief Handles IS_IS_DEVICE_RUNNING by checking whether a named device is active.
 *
 * @param message The request containing a "device" string.
 * @param reply   The reply message.
 * @return B_OK if the device is running, B_ERROR if stopped, or
 *         B_NAME_NOT_FOUND if unknown.
 */
status_t
AddOnManager::_HandleIsDeviceRunning(BMessage* message, BMessage* reply)
{
	const char* name;
	bool running;
	if (message->FindString("device", &name) != B_OK
		|| gInputServer->GetDeviceInfo(name, NULL, &running) != B_OK)
		return B_NAME_NOT_FOUND;

	return running ? B_OK : B_ERROR;
}


/**
 * @brief Handles IS_CONTROL_DEVICES by forwarding a control code to matching devices.
 *
 * Exactly one of "device" (by name) or "type" (by type) must be present.
 *
 * @param message The request containing "code", and optionally "message".
 * @param reply   The reply message.
 * @return B_OK on success, or B_BAD_VALUE if required fields are missing.
 */
status_t
AddOnManager::_HandleControlDevices(BMessage* message, BMessage* reply)
{
	CALLED();
	const char *name = NULL;
	int32 type = 0;
	if (!((message->FindInt32("type", &type) != B_OK)
			^ (message->FindString("device", &name) != B_OK)))
		return B_BAD_VALUE;

	uint32 code = 0;
	BMessage controlMessage;
	bool hasMessage = true;
	if (message->FindInt32("code", (int32*)&code) != B_OK)
		return B_BAD_VALUE;
	if (message->FindMessage("message", &controlMessage) != B_OK)
		hasMessage = false;

	return gInputServer->ControlDevices(name, (input_device_type)type,
		code, hasMessage ? &controlMessage : NULL);
}


/**
 * @brief Notifies all registered device add-ons that the system is shutting down.
 *
 * @param message The shutdown notification message.
 * @param reply   The reply message.
 * @return B_OK.
 */
status_t
AddOnManager::_HandleSystemShuttingDown(BMessage* message, BMessage* reply)
{
	CALLED();

	for (int32 i = 0; i < fDeviceList.CountItems(); i++) {
		device_info* info = fDeviceList.ItemAt(i);
		info->add_on->SystemShuttingDown();
	}

	return B_OK;
}


/**
 * @brief Handles IS_METHOD_REGISTER by (re-)loading the method replicant and populating it.
 *
 * If no methods are registered, removes the replicant.  Otherwise loads it
 * and sends IS_ADD_METHOD for the keymap method and every registered input method.
 *
 * @param message The registration request message.
 * @param reply   The reply message.
 * @return B_OK.
 */
status_t
AddOnManager::_HandleMethodReplicant(BMessage* message, BMessage* reply)
{
	CALLED();

	if (InputServer::gInputMethodList.CountItems() == 0) {
		_UnloadReplicant();
		return B_OK;
	}

	_LoadReplicant();

	BAutolock lock(InputServer::gInputMethodListLocker);

	if (gInputServer->MethodReplicant()) {
		_BMethodAddOn_* addon = InputServer::gKeymapMethod.fOwner;
		addon->AddMethod();

		for (int32 i = 0; i < InputServer::gInputMethodList.CountItems(); i++) {
			BInputServerMethod* method
				= (BInputServerMethod*)InputServer::gInputMethodList.ItemAt(i);

			_BMethodAddOn_* addon = method->fOwner;
			addon->AddMethod();
		}
	}

	return B_OK;
}


/**
 * @brief Processes B_PATH_MONITOR notifications and forwards them to watching device add-ons.
 *
 * Handles B_ENTRY_CREATED and B_ENTRY_REMOVED opcodes by forwarding the
 * notification as a B_NODE_MONITOR control message to each add-on that is
 * watching the affected path.
 *
 * @param message The path monitor notification message.
 */
void
AddOnManager::_HandleDeviceMonitor(BMessage* message)
{
	int32 opcode;
	if (message->FindInt32("opcode", &opcode) != B_OK)
		return;

	switch (opcode) {
		case B_ENTRY_CREATED:
		case B_ENTRY_REMOVED:
		{
			const char* path;
			const char* watchedPath;
			if (message->FindString("watched_path", &watchedPath) != B_OK
				|| message->FindString("path", &path) != B_OK) {
#if DEBUG
				char string[1024];
				sprintf(string, "message does not contain all fields - "
					"watched_path: %d, path: %d\n",
					message->HasString("watched_path"),
					message->HasString("path"));
				debugger(string);
#endif
				return;
			}

			// Notify all watching devices

			for (int32 i = 0; i < fDeviceAddOns.CountItems(); i++) {
				DeviceAddOn* addOn = fDeviceAddOns.ItemAt(i);
				if (!addOn->HasPath(watchedPath))
					continue;

				addOn->Device()->Control(NULL, NULL, B_NODE_MONITOR, message);
			}

			break;
		}
	}
}


/**
 * @brief Adds a device path to both the global path list and the add-on's monitored set.
 *
 * @param addOn   The device add-on requesting the path.
 * @param path    The device path to add.
 * @param newPath Output: set to @c true if this is the first registration of the path globally.
 * @return B_OK on success, or B_NO_MEMORY on allocation failure.
 */
status_t
AddOnManager::_AddDevicePath(DeviceAddOn* addOn, const char* path,
	bool& newPath)
{
	newPath = !fDevicePaths.HasPath(path);

	status_t status = fDevicePaths.AddPath(path);
	if (status == B_OK) {
		status = addOn->AddPath(path);
		if (status == B_OK) {
			if (!fDeviceAddOns.HasItem(addOn)
				&& !fDeviceAddOns.AddItem(addOn)) {
				addOn->RemovePath(path);
				status = B_NO_MEMORY;
			}
		} else
			fDevicePaths.RemovePath(path);
	}

	return status;
}


/**
 * @brief Removes a device path from both the global path list and the add-on's monitored set.
 *
 * If the add-on has no remaining monitored paths, it is removed from the
 * active device add-on list.
 *
 * @param addOn    The device add-on releasing the path.
 * @param path     The device path to remove.
 * @param lastPath Output: set to @c true if no add-on is watching this path anymore.
 * @return B_OK on success, or B_ENTRY_NOT_FOUND if the path is unknown.
 */
status_t
AddOnManager::_RemoveDevicePath(DeviceAddOn* addOn, const char* path,
	bool& lastPath)
{
	if (!fDevicePaths.HasPath(path) || !addOn->HasPath(path))
		return B_ENTRY_NOT_FOUND;

	fDevicePaths.RemovePath(path);

	lastPath = !fDevicePaths.HasPath(path);

	addOn->RemovePath(path);
	if (addOn->CountPaths() == 0)
		fDeviceAddOns.RemoveItem(addOn);

	return B_OK;
}
