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
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file NodeMonitorHandler.cpp
 * @brief Implementation of the NodeMonitorHandler BHandler subclass.
 *
 * NodeMonitorHandler receives B_NODE_MONITOR messages from the node monitoring
 * subsystem, unpacks the opcode and message fields, and dispatches them to
 * typed virtual hook methods (EntryCreated, EntryRemoved, EntryMoved,
 * StatChanged, AttrChanged, DeviceMounted, DeviceUnmounted). Subclasses
 * override only the hooks they care about.
 *
 * @see AddOnMonitorHandler
 */

#include "NodeMonitorHandler.h"

/*
 * static util functions for the super lazy
 */


/**
 * @brief Populates an entry_ref from a device ID, directory inode, and name.
 *
 * Sets the device and directory fields of @p ref and calls set_name() to
 * copy the entry name string.
 *
 * @param device    Device ID for the entry.
 * @param directory Inode number of the containing directory.
 * @param name      Null-terminated name of the entry.
 * @param ref       Pointer to the entry_ref to populate.
 * @return B_OK on success, or an error code if set_name() fails.
 */
/* static */ status_t
NodeMonitorHandler::make_entry_ref(dev_t device, ino_t directory,
                                   const char * name,
                                   entry_ref * ref)
{
	ref->device = device;
	ref->directory = directory;
	return ref->set_name(name);
}


/**
 * @brief Populates a node_ref from a device ID and inode number.
 *
 * @param device Device ID for the node.
 * @param node   Inode number of the node.
 * @param ref    Pointer to the node_ref to populate.
 */
/* static */ void
NodeMonitorHandler::make_node_ref(dev_t device, ino_t node, node_ref * ref)
{
	ref->device = device;
	ref->node = node;
}


/*
 * public functions: constructor, destructor, MessageReceived
 */

/**
 * @brief Constructs a NodeMonitorHandler with the given BHandler name.
 *
 * @param name The name passed to the BHandler base-class constructor.
 */
NodeMonitorHandler::NodeMonitorHandler(const char * name)
	: BHandler(name)
{
	// nothing to do
}


/**
 * @brief Destructor. No resources to release.
 */
NodeMonitorHandler::~NodeMonitorHandler()
{
	// nothing to do
}


/**
 * @brief Dispatches incoming B_NODE_MONITOR messages to the appropriate hook.
 *
 * Extracts the "opcode" field and routes the message to one of the private
 * Handle*() methods, each of which unpacks the remaining fields and calls the
 * corresponding public virtual hook. If the message is not a B_NODE_MONITOR
 * message, or if the opcode is not recognised, the base-class implementation
 * is called instead.
 *
 * @param msg The incoming BMessage to process.
 */
/* virtual */ void
NodeMonitorHandler::MessageReceived(BMessage * msg)
{
	status_t status = B_MESSAGE_NOT_UNDERSTOOD;
	if (msg->what == B_NODE_MONITOR) {
		int32 opcode;
		if (msg->FindInt32("opcode", &opcode) == B_OK) {
			switch (opcode) {
			case B_ENTRY_CREATED:
				status = HandleEntryCreated(msg);
				break;
			case B_ENTRY_REMOVED:
				status = HandleEntryRemoved(msg);
				break;
			case B_ENTRY_MOVED:
				status = HandleEntryMoved(msg);
				break;
			case B_STAT_CHANGED:
				status = HandleStatChanged(msg);
				break;
			case B_ATTR_CHANGED:
				status = HandleAttrChanged(msg);
				break;
			case B_DEVICE_MOUNTED:
				status = HandleDeviceMounted(msg);
				break;
			case B_DEVICE_UNMOUNTED:
				status = HandleDeviceUnmounted(msg);
				break;
			default:
				break;
			}
		}
	}
	if (status == B_MESSAGE_NOT_UNDERSTOOD) {
		inherited::MessageReceived(msg);
	}
}

/*
 * default virtual functions do nothing
 */

/**
 * @brief Hook called when a new entry has been created in a watched directory.
 *
 * The default implementation ignores the notification. Subclasses should
 * override this method to react to new entries.
 *
 * @param name      The name of the new entry.
 * @param directory Inode number of the containing directory.
 * @param device    Device ID of the filesystem.
 * @param node      Inode number of the new entry.
 */
/* virtual */ void
NodeMonitorHandler::EntryCreated(const char *name, ino_t directory,
	dev_t device, ino_t node)
{
	// ignore
}


/**
 * @brief Hook called when an entry has been removed from a watched directory.
 *
 * The default implementation ignores the notification. Subclasses should
 * override this method to react to removed entries.
 *
 * @param name      The name of the removed entry.
 * @param directory Inode number of the containing directory.
 * @param device    Device ID of the filesystem.
 * @param node      Inode number of the removed entry.
 */
/* virtual */ void
NodeMonitorHandler::EntryRemoved(const char *name, ino_t directory,
	dev_t device, ino_t node)
{
	// ignore
}


/**
 * @brief Hook called when an entry has been moved or renamed.
 *
 * The default implementation ignores the notification. Subclasses should
 * override this method to react to moved or renamed entries.
 *
 * @param name          New name of the entry.
 * @param fromName      Previous name of the entry.
 * @param fromDirectory Inode number of the source directory.
 * @param toDirectory   Inode number of the destination directory.
 * @param device        Device ID shared by both directories.
 * @param node          Inode number of the moved entry.
 * @param nodeDevice    Device ID of the moved entry's node.
 */
/* virtual */ void
NodeMonitorHandler::EntryMoved(const char *name, const char *fromName,
	ino_t fromDirectory, ino_t toDirectory, dev_t device,ino_t node,
	dev_t nodeDevice)
{
	// ignore
}


/**
 * @brief Hook called when the stat data of a watched node has changed.
 *
 * The default implementation ignores the notification. Subclasses should
 * override this method to react to stat changes (e.g. size, modification
 * time).
 *
 * @param node       Inode number of the node whose stat data changed.
 * @param device     Device ID of the filesystem.
 * @param statFields Bitmask of B_STAT_* flags indicating which fields changed.
 */
/* virtual */ void
NodeMonitorHandler::StatChanged(ino_t node, dev_t device, int32 statFields)
{
	// ignore
}


/**
 * @brief Hook called when an attribute of a watched node has changed.
 *
 * The default implementation ignores the notification. Subclasses should
 * override this method to react to attribute additions, changes, or removals.
 *
 * @param node   Inode number of the node whose attribute changed.
 * @param device Device ID of the filesystem.
 */
/* virtual */ void
NodeMonitorHandler::AttrChanged(ino_t node, dev_t device)
{
	// ignore
}


/**
 * @brief Hook called when a new volume has been mounted.
 *
 * The default implementation ignores the notification. Subclasses should
 * override this method to react to newly mounted volumes.
 *
 * @param new_device Device ID of the newly mounted volume.
 * @param device     Device ID of the directory that serves as the mount point.
 * @param directory  Inode number of the mount-point directory.
 */
/* virtual */ void
NodeMonitorHandler::DeviceMounted(dev_t new_device, dev_t device,
	ino_t directory)
{
	// ignore
}


/**
 * @brief Hook called when a volume has been unmounted.
 *
 * The default implementation ignores the notification. Subclasses should
 * override this method to react to unmounted volumes.
 *
 * @param new_device Device ID of the volume that was unmounted.
 */
/* virtual */ void
NodeMonitorHandler::DeviceUnmounted(dev_t new_device)
{
	// ignore
}


/*
 * private functions to rip apart the corresponding BMessage
 */

/**
 * @brief Unpacks a B_ENTRY_CREATED node-monitor message and calls EntryCreated().
 *
 * Extracts "name", "directory", "device", and "node" fields from @p msg. If
 * any field is missing B_MESSAGE_NOT_UNDERSTOOD is returned.
 *
 * @param msg The raw B_NODE_MONITOR BMessage with opcode B_ENTRY_CREATED.
 * @return B_OK on success, B_MESSAGE_NOT_UNDERSTOOD if required fields are
 *         absent.
 */
status_t
NodeMonitorHandler::HandleEntryCreated(BMessage * msg)
{
	const char *name;
	ino_t directory;
	dev_t device;
	ino_t node;
	if ((msg->FindString("name", &name) != B_OK) ||
        (msg->FindInt64("directory", &directory) != B_OK) ||
		(msg->FindInt32("device", &device) != B_OK) ||
		(msg->FindInt64("node", &node) != B_OK)) {
		return B_MESSAGE_NOT_UNDERSTOOD;
	}
	EntryCreated(name, directory, device, node);
	return B_OK;
}


/**
 * @brief Unpacks a B_ENTRY_REMOVED node-monitor message and calls EntryRemoved().
 *
 * Extracts "name", "directory", "device", and "node" fields from @p msg. If
 * any field is missing B_MESSAGE_NOT_UNDERSTOOD is returned.
 *
 * @param msg The raw B_NODE_MONITOR BMessage with opcode B_ENTRY_REMOVED.
 * @return B_OK on success, B_MESSAGE_NOT_UNDERSTOOD if required fields are
 *         absent.
 */
status_t
NodeMonitorHandler::HandleEntryRemoved(BMessage * msg)
{
	const char *name;
	ino_t directory;
	dev_t device;
	ino_t node;
	if ((msg->FindString("name", &name) != B_OK) ||
		(msg->FindInt64("directory", &directory) != B_OK) ||
		(msg->FindInt32("device", &device) != B_OK) ||
		(msg->FindInt64("node", &node) != B_OK)) {
		return B_MESSAGE_NOT_UNDERSTOOD;
	}
	EntryRemoved(name, directory, device, node);
	return B_OK;
}


/**
 * @brief Unpacks a B_ENTRY_MOVED node-monitor message and calls EntryMoved().
 *
 * Extracts "name", "from name", "from directory", "to directory", "device",
 * "node device", and "node" fields from @p msg. If any field is missing
 * B_MESSAGE_NOT_UNDERSTOOD is returned.
 *
 * @param msg The raw B_NODE_MONITOR BMessage with opcode B_ENTRY_MOVED.
 * @return B_OK on success, B_MESSAGE_NOT_UNDERSTOOD if required fields are
 *         absent.
 */
status_t
NodeMonitorHandler::HandleEntryMoved(BMessage * msg)
{
	const char *name;
	const char *fromName;
	ino_t fromDirectory;
	ino_t toDirectory;
	dev_t device;
	ino_t node;
	dev_t deviceNode;
	if ((msg->FindString("name", &name) != B_OK) ||
		(msg->FindString("from name", &fromName) != B_OK) ||
		(msg->FindInt64("from directory", &fromDirectory) != B_OK) ||
		(msg->FindInt64("to directory", &toDirectory) != B_OK) ||
		(msg->FindInt32("device", &device) != B_OK) ||
		(msg->FindInt32("node device", &deviceNode) != B_OK) ||
		(msg->FindInt64("node", &node) != B_OK)) {
		return B_MESSAGE_NOT_UNDERSTOOD;
	}
	EntryMoved(name, fromName, fromDirectory, toDirectory, device, node,
		deviceNode);
	return B_OK;
}


/**
 * @brief Unpacks a B_STAT_CHANGED node-monitor message and calls StatChanged().
 *
 * Extracts "node", "device", and "fields" from @p msg. If any field is
 * missing B_MESSAGE_NOT_UNDERSTOOD is returned.
 *
 * @param msg The raw B_NODE_MONITOR BMessage with opcode B_STAT_CHANGED.
 * @return B_OK on success, B_MESSAGE_NOT_UNDERSTOOD if required fields are
 *         absent.
 */
status_t
NodeMonitorHandler::HandleStatChanged(BMessage * msg)
{
	ino_t node;
	dev_t device;
	int32 statFields;
	if ((msg->FindInt64("node", &node) != B_OK) ||
		(msg->FindInt32("device", &device) != B_OK) ||
		(msg->FindInt32("fields", &statFields) != B_OK)) {
		return B_MESSAGE_NOT_UNDERSTOOD;
	}
	StatChanged(node, device, statFields);
	return B_OK;
}


/**
 * @brief Unpacks a B_ATTR_CHANGED node-monitor message and calls AttrChanged().
 *
 * Extracts "node" and "device" from @p msg. If any field is missing
 * B_MESSAGE_NOT_UNDERSTOOD is returned.
 *
 * @param msg The raw B_NODE_MONITOR BMessage with opcode B_ATTR_CHANGED.
 * @return B_OK on success, B_MESSAGE_NOT_UNDERSTOOD if required fields are
 *         absent.
 */
status_t
NodeMonitorHandler::HandleAttrChanged(BMessage * msg)
{
	ino_t node;
	dev_t device;
	if ((msg->FindInt64("node", &node) != B_OK) ||
		(msg->FindInt32("device", &device) != B_OK)) {
		return B_MESSAGE_NOT_UNDERSTOOD;
	}
	AttrChanged(node, device);
	return B_OK;
}


/**
 * @brief Unpacks a B_DEVICE_MOUNTED node-monitor message and calls
 *        DeviceMounted().
 *
 * Extracts "new device", "device", and "directory" from @p msg. If any field
 * is missing B_MESSAGE_NOT_UNDERSTOOD is returned.
 *
 * @param msg The raw B_NODE_MONITOR BMessage with opcode B_DEVICE_MOUNTED.
 * @return B_OK on success, B_MESSAGE_NOT_UNDERSTOOD if required fields are
 *         absent.
 */
status_t
NodeMonitorHandler::HandleDeviceMounted(BMessage * msg)
{
	dev_t new_device;
	dev_t device;
	ino_t directory;
	if ((msg->FindInt32("new device", &new_device) != B_OK) ||
		(msg->FindInt32("device", &device) != B_OK) ||
		(msg->FindInt64("directory", &directory) != B_OK)) {
		return B_MESSAGE_NOT_UNDERSTOOD;
	}
	DeviceMounted(new_device, device, directory);
	return B_OK;
}


/**
 * @brief Unpacks a B_DEVICE_UNMOUNTED node-monitor message and calls
 *        DeviceUnmounted().
 *
 * Extracts "new device" from @p msg. If the field is missing
 * B_MESSAGE_NOT_UNDERSTOOD is returned.
 *
 * @param msg The raw B_NODE_MONITOR BMessage with opcode B_DEVICE_UNMOUNTED.
 * @return B_OK on success, B_MESSAGE_NOT_UNDERSTOOD if the required field is
 *         absent.
 */
status_t
NodeMonitorHandler::HandleDeviceUnmounted(BMessage * msg)
{
	dev_t new_device;
	if (msg->FindInt32("new device", &new_device) != B_OK) {
		return B_MESSAGE_NOT_UNDERSTOOD;
	}
	DeviceUnmounted(new_device);
	return B_OK;
}
