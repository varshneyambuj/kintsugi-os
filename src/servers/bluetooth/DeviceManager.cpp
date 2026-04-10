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
 */

/** @file DeviceManager.cpp
 *  @brief Implementation of the /dev watcher that discovers Bluetooth controllers at runtime. */

#include <Application.h>
#include <Autolock.h>
#include <String.h>

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <NodeMonitor.h>


#include <image.h>
#include <stdio.h>
#include <string.h>

#include "DeviceManager.h"
#include "LocalDeviceImpl.h"

#include "Debug.h"
#include "BluetoothServer.h"

#include <bluetoothserver_p.h>


/**
 * @brief Handle node-monitor notifications from the /dev filesystem.
 *
 * Dispatches B_ENTRY_CREATED and B_ENTRY_MOVED events by either
 * recursively monitoring a new subdirectory or registering a new device
 * file with the Bluetooth server.  B_ENTRY_REMOVED events are logged
 * but not yet fully handled.
 *
 * @param msg The incoming BMessage, expected to carry B_NODE_MONITOR data.
 */
void
DeviceManager::MessageReceived(BMessage* msg)
{
	if (msg->what == B_NODE_MONITOR) {
		int32 opcode;
		if (msg->FindInt32("opcode", &opcode) == B_OK) {
			switch (opcode)	{
				case B_ENTRY_CREATED:
				case B_ENTRY_MOVED:
				{
					entry_ref ref;
					const char *name;
					BDirectory dir;

					TRACE_BT("Something new in the bus ... ");

					if ((msg->FindInt32("device", &ref.device)!=B_OK)
						|| (msg->FindInt64("directory", &ref.directory)!=B_OK)
						|| (msg->FindString("name",	&name) != B_OK))
						return;

					TRACE_BT("DeviceManager: -> %s\n", name);

					ref.set_name(name);

					// Check if	the	entry is a File	or a directory
					if (dir.SetTo(&ref) == B_OK) {
						printf("%s: Entry %s is taken as a dir\n", __FUNCTION__, name);
					    node_ref nref;
					    dir.GetNodeRef(&nref);
						AddDirectory(&nref);

					} else {
						printf("%s: Entry %s is taken as a file\n", __FUNCTION__, name);
                        AddDevice(&ref);
					}
				}
				break;
				case B_ENTRY_REMOVED:
				{
					TRACE_BT("Something removed from the bus ...\n");

				}
				break;
				case B_STAT_CHANGED:
				case B_ATTR_CHANGED:
				case B_DEVICE_MOUNTED:
				case B_DEVICE_UNMOUNTED:
				default:
					BLooper::MessageReceived(msg);
				break;
			}
		}
	}
}


/**
 * @brief Start monitoring a directory for new Bluetooth device entries.
 *
 * Installs a B_WATCH_DIRECTORY node monitor on \a nref and enumerates any
 * entries already present, calling AddDevice() for each one.
 *
 * @param nref The node_ref of the directory to monitor.
 * @return B_OK on success, or an error from watch_node() / directory init.
 */
status_t
DeviceManager::AddDirectory(node_ref *nref)
{
	BDirectory directory(nref);
	status_t status	= directory.InitCheck();
	if (status != B_OK)	{
		TRACE_BT("AddDirectory::Initcheck Failed\n");
		return status;
	}

	status = watch_node(nref, B_WATCH_DIRECTORY, this);
	if (status != B_OK)	{
		TRACE_BT("AddDirectory::watch_node	Failed\n");
		return status;
	}

//	BPath path(*nref);
//	BString	str(path.Path());
//
//	TRACE_BT("DeviceManager: Exploring entries in %s\n", str.String());

	entry_ref ref;
	status_t error;
	while ((error =	directory.GetNextRef(&ref))	== B_OK) {
		// its suposed to be devices ...
		AddDevice(&ref);
	}

	TRACE_BT("DeviceManager: Finished exploring entries(%s)\n", strerror(error));

	return (error == B_OK || error == B_ENTRY_NOT_FOUND)?B_OK:error;
}


/**
 * @brief Stop monitoring a directory and synthesize removal events for its entries.
 *
 * Unregisters the node monitor for \a nref, then iterates each remaining
 * entry and posts a synthetic B_ENTRY_REMOVED message.
 *
 * @param nref The node_ref of the directory to stop watching.
 * @return B_OK on success, or an error from the directory or watch_node().
 */
status_t
DeviceManager::RemoveDirectory(node_ref* nref)
{
	BDirectory directory(nref);
	status_t status	= directory.InitCheck();
	if (status != B_OK)
		return status;

	status = watch_node(nref, B_STOP_WATCHING, this);
	if (status != B_OK)
		return status;

	BEntry entry;
	while (directory.GetNextEntry(&entry, true)	== B_OK) {
		entry_ref ref;
		entry.GetRef(&ref);
		BMessage msg(B_NODE_MONITOR);
		msg.AddInt32("opcode", B_ENTRY_REMOVED);
		msg.AddInt32("device", nref->device);
		msg.AddInt64("directory", nref->node);
		msg.AddString("name", ref.name);
		//addon->fDevice->Control(NULL,	NULL, msg.what,	&msg);
	}

	return B_OK;
}


/**
 * @brief Notify the Bluetooth server that a new device node appeared.
 *
 * Constructs a BT_MSG_ADD_DEVICE message containing the device path and
 * sends it asynchronously to the application.
 *
 * @param ref The entry_ref pointing to the new device file under /dev.
 * @return B_OK if the message was sent, or an error from SendMessage().
 */
status_t
DeviceManager::AddDevice(entry_ref* ref)
{
	BPath path(ref);
	BString* str = new BString(path.Path());

	BMessage* msg =	new	BMessage(BT_MSG_ADD_DEVICE);
	msg->AddInt32("opcode",	B_ENTRY_CREATED);
	msg->AddInt32("device",	ref->device);
	msg->AddInt64("directory", ref->directory);

	msg->AddString("name", *str	);

	TRACE_BT("DeviceManager: Device %s registered\n", path.Path());
	return be_app_messenger.SendMessage(msg);
}


/** @brief Construct the device manager and initialize its internal lock. */
DeviceManager::DeviceManager() :
	fLock("device manager")
{

}


/** @brief Destroy the device manager. */
DeviceManager::~DeviceManager()
{

}


/**
 * @brief Restore saved device-monitoring state and start the looper.
 *
 * Acquires the internal lock, calls Run() to start the BLooper message
 * loop, then unlocks.
 */
void
DeviceManager::LoadState()
{
	if (!Lock())
		return;
	Run();
	Unlock();
}


/**
 * @brief Persist the current device-monitoring state to disk.
 *
 * Currently a no-op placeholder.
 */
void
DeviceManager::SaveState()
{

}


/**
 * @brief Begin watching a /dev subdirectory tree for Bluetooth controllers.
 *
 * Constructs the path /dev/\a device, creating the directory if it does not
 * exist, installs a B_WATCH_DIRECTORY node monitor on it, and recursively
 * monitors any subdirectories already present.
 *
 * @param device Relative path under /dev to monitor (e.g. "bluetooth/h2").
 * @return B_OK on success, or an error from the filesystem or watch_node().
 */
status_t
DeviceManager::StartMonitoringDevice(const char	*device)
{

	status_t err;
	node_ref nref;
	BDirectory directory;
	BPath path("/dev");

	/* Build the path */
	if ((err = path.Append(device))	!= B_OK) {
		printf("DeviceManager::StartMonitoringDevice BPath::Append() error %s: %s\n", path.Path(), strerror(err));
		return err;
	}

	/* Check the path */
	if ((err = directory.SetTo(path.Path())) !=	B_OK) {
		/* Entry not there ... */
		if (err	!= B_ENTRY_NOT_FOUND) {	// something else we cannot	handle
			printf("DeviceManager::StartMonitoringDevice SetTo error %s: %s\n",	path.Path(), strerror(err));
			return err;
		}
		/* Create it */
		if ((err = create_directory(path.Path(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) != B_OK
			|| (err	= directory.SetTo(path.Path()))	!= B_OK) {
			printf("DeviceManager::StartMonitoringDevice CreateDirectory error %s: %s\n", path.Path(), strerror(err));
			return err;
		}
	}

	// get noderef
	if ((err = directory.GetNodeRef(&nref))	!= B_OK) {
		printf("DeviceManager::StartMonitoringDevice GetNodeRef	error %s: %s\n", path.Path(), strerror(err));
		return err;
	}

	// start monitoring	the	root
	status_t error = watch_node(&nref, B_WATCH_DIRECTORY, this);
	if (error != B_OK)
		return error;

	TRACE_BT("DeviceManager: %s	path being monitored\n", path.Path());

	// We are monitoring the root we may have already directories inside
	// to be monitored
	entry_ref driverRef;
	while ((error =	directory.GetNextRef(&driverRef)) == B_OK) {

		// its suposed to be directories that needs	to be monitored...
		BNode driverNode(&driverRef);
		node_ref driverNRef;
		driverNode.GetNodeRef(&driverNRef);
		AddDirectory(&driverNRef);
	}

    TRACE_BT("DeviceManager: Finished exploring entries(%s)\n", strerror(error));

#if	0
	HCIDelegate	*tmphd = NULL;
	int32 i	= 0;

	// TODO!! ask the server if	this needs to be monitored

	while ((tmphd =	(HCIDelegate *)fDelegatesList.ItemAt(i++)) !=NULL) {

		/* Find	out	the	reference*/
		node_ref *dnref	= (node_ref	*)tmphd->fMonitoredRefs	;
		if (*dnref == nref)	{
			printf("StartMonitoringDevice already monitored\n");
			alreadyMonitored = true;
			break;
		}

	}
#endif

	return B_OK;
}


/**
 * @brief Stop watching a /dev subdirectory for Bluetooth controllers.
 *
 * Resolves /dev/\a device to a node_ref.  The actual unwatching and
 * cleanup logic is currently commented out.
 *
 * @param device Relative path under /dev to stop monitoring.
 * @return B_OK on success, or an error from path resolution.
 */
status_t
DeviceManager::StopMonitoringDevice(const char *device)
{
	status_t err;
	node_ref nref;
	BDirectory directory;
	BPath path("/dev");
	if (((err =	path.Append(device)) !=	B_OK)
		|| ((err = directory.SetTo(path.Path())) !=	B_OK)
		|| ((err = directory.GetNodeRef(&nref))	!= B_OK))
		return err;

	// test	if still monitored
/*
	bool stillMonitored	= false;
	int32 i	= 0;
	while ((tmpaddon = (_BDeviceAddOn_ *)fDeviceAddons.ItemAt(i++))	!=NULL)	{
		if (addon == tmpaddon)
			continue;

		int32 j=0;
		node_ref *dnref	= NULL;
		while ((dnref =	(node_ref *)tmpaddon->fMonitoredRefs.ItemAt(j++)) != NULL) {
			if (*dnref == nref)	{
				stillMonitored = true;
				break;
			}
		}
		if (stillMonitored)
			break;
	}

	// remove from list
	node_ref *dnref	= NULL;
	int32 j=0;
	while ((dnref =	(node_ref *)addon->fMonitoredRefs.ItemAt(j)) !=	NULL) {
		if (*dnref == nref)	{
			addon->fMonitoredRefs.RemoveItem(j);
			delete dnref;
			break;
		}
		j++;
	}

	// stop	monitoring if needed
	if (!stillMonitored) {
		if ((err = RemoveDirectory(&nref, addon)) != B_OK)
			return err;
	}
*/
	return B_OK;
}
