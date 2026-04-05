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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2003-2006, Haiku Inc.
 *   Authors: Ingo Weinhold, bonefish@users.sf.net
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file DiskDeviceList.cpp
 * @brief Implementation of the BDiskDeviceList class.
 *
 * BDiskDeviceList maintains a live list of BDiskDevice objects, subscribing
 * to the disk device notification service when attached to a BLooper so that
 * the list stays up to date automatically. Virtual hook methods are called
 * whenever device or partition events occur, allowing subclasses to react to
 * changes such as mounts, unmounts, device additions, and removals.
 *
 * @see BDiskDevice
 * @see BDiskDeviceRoster
 */

#include <DiskDeviceList.h>

#include <AutoLocker.h>
#include <DiskDevice.h>
#include <DiskDevicePrivate.h>
#include <DiskDeviceRoster.h>
#include <Locker.h>
#include <Looper.h>
#include <Partition.h>

#include <new>
using namespace std;

/**
 * @brief Creates an empty BDiskDeviceList object.
 *
 * @param useOwnLocker If \c true, the list allocates its own BLocker for
 *        thread safety; otherwise, it relies on the BLooper it is attached to.
 */
BDiskDeviceList::BDiskDeviceList(bool useOwnLocker)
	: fLocker(NULL),
	  fDevices(20),
	  fSubscribed(false)
{
	if (useOwnLocker)
		fLocker = new(nothrow) BLocker("BDiskDeviceList_fLocker");
}

/**
 * @brief Frees all resources associated with the object.
 */
BDiskDeviceList::~BDiskDeviceList()
{
	delete fLocker;
}

/**
 * @brief Handles incoming notification messages to keep the list current.
 *
 * Dispatches B_DEVICE_UPDATE messages to the appropriate private handler
 * methods, then forwards all other messages to the base class.
 *
 * @param message The message to handle.
 */
void
BDiskDeviceList::MessageReceived(BMessage *message)
{
	AutoLocker<BDiskDeviceList> _(this);
	switch (message->what) {
		case B_DEVICE_UPDATE:
		{
			uint32 event;
			if (message->FindInt32("event", (int32*)&event) == B_OK) {
				switch (event) {
					case B_DEVICE_MOUNT_POINT_MOVED:
						_MountPointMoved(message);
						break;
					case B_DEVICE_PARTITION_MOUNTED:
						_PartitionMounted(message);
						break;
					case B_DEVICE_PARTITION_UNMOUNTED:
						_PartitionUnmounted(message);
						break;
					case B_DEVICE_PARTITION_INITIALIZED:
						_PartitionInitialized(message);
						break;
					case B_DEVICE_PARTITION_RESIZED:
						_PartitionResized(message);
						break;
					case B_DEVICE_PARTITION_MOVED:
						_PartitionMoved(message);
						break;
					case B_DEVICE_PARTITION_CREATED:
						_PartitionCreated(message);
						break;
					case B_DEVICE_PARTITION_DELETED:
						_PartitionDeleted(message);
						break;
					case B_DEVICE_PARTITION_DEFRAGMENTED:
						_PartitionDefragmented(message);
						break;
					case B_DEVICE_PARTITION_REPAIRED:
						_PartitionRepaired(message);
						break;
					case B_DEVICE_MEDIA_CHANGED:
						_MediaChanged(message);
						break;
					case B_DEVICE_ADDED:
						_DeviceAdded(message);
						break;
					case B_DEVICE_REMOVED:
						_DeviceRemoved(message);
						break;
				}
			}
		}
		default:
			BHandler::MessageReceived(message);
	}
}

/**
 * @brief Unsubscribes from notification services when being detached from a looper.
 *
 * @param handler The new next handler, or \c NULL when being detached.
 */
void
BDiskDeviceList::SetNextHandler(BHandler *handler)
{
	if (!handler) {
		AutoLocker<BDiskDeviceList> _(this);
		if (fSubscribed)
			_StopWatching();
	}
	BHandler::SetNextHandler(handler);
}

/**
 * @brief Empties the list and refills it according to the current system state.
 *
 * Also subscribes to notification services if the list is attached to a
 * looper, so that the list remains up to date. On error, the list is Unset().
 * The object does not need to be locked when this method is invoked.
 *
 * @return \c B_OK if everything went fine, another error code otherwise.
 */
status_t
BDiskDeviceList::Fetch()
{
	Unset();
	AutoLocker<BDiskDeviceList> _(this);
	// register for notifications
	status_t error = B_OK;
	if (Looper())
		error = _StartWatching();
	// get the devices
	BDiskDeviceRoster roster;
	while (error == B_OK) {
		if (BDiskDevice *device = new(nothrow) BDiskDevice) {
			status_t status = roster.GetNextDevice(device);
			if (status == B_OK)
				fDevices.AddItem(device);
			else if (status == B_ENTRY_NOT_FOUND)
				break;
			else
				error = status;
		} else
			error = B_NO_MEMORY;
	}
	// cleanup on error
	if (error != B_OK)
		Unset();
	return error;
}

/**
 * @brief Empties the list and unsubscribes from all notification services.
 *
 * The object does not need to be locked when this method is invoked.
 */
void
BDiskDeviceList::Unset()
{
	AutoLocker<BDiskDeviceList> _(this);
	// unsubscribe from notification services
	_StopWatching();
	// empty the list
	fDevices.MakeEmpty();
}

/**
 * @brief Locks the list for exclusive access.
 *
 * Uses the own BLocker if one was created at construction time, otherwise
 * calls LockLooper().
 *
 * @return \c true if the list was successfully locked, \c false otherwise.
 */
bool
BDiskDeviceList::Lock()
{
	if (fLocker)
		return fLocker->Lock();
	return LockLooper();
}

/**
 * @brief Unlocks the list.
 *
 * Uses the own BLocker if one was created at construction time, otherwise
 * calls UnlockLooper().
 */
void
BDiskDeviceList::Unlock()
{
	if (fLocker)
		return fLocker->Unlock();
	return UnlockLooper();
}

/**
 * @brief Returns the number of devices currently in the list.
 *
 * The list must be locked.
 *
 * @return The number of devices in the list.
 */
int32
BDiskDeviceList::CountDevices() const
{
	return fDevices.CountItems();
}

/**
 * @brief Retrieves a device by its list index.
 *
 * The list must be locked.
 *
 * @param index The zero-based index of the device to retrieve.
 * @return The device at \a index, or \c NULL if the index is out of range.
 */
BDiskDevice *
BDiskDeviceList::DeviceAt(int32 index) const
{
	return fDevices.ItemAt(index);
}

/**
 * @brief Iterates through all devices in the list using a visitor.
 *
 * Invokes visitor->Visit(BDiskDevice*) for each device. If Visit() returns
 * \c true, the iteration is terminated and the respective device is returned.
 * The list must be locked.
 *
 * @param visitor The visitor to invoke for each device.
 * @return The device at which the iteration was terminated early, or \c NULL.
 */
BDiskDevice *
BDiskDeviceList::VisitEachDevice(BDiskDeviceVisitor *visitor)
{
	if (visitor) {
		for (int32 i = 0; BDiskDevice *device = DeviceAt(i); i++) {
			if (visitor->Visit(device))
				return device;
		}
	}
	return NULL;
}

/**
 * @brief Iterates through all partitions of all devices using a visitor.
 *
 * Invokes visitor->Visit(BPartition*, int32) for each partition. If Visit()
 * returns \c true, the iteration is terminated and the respective partition
 * is returned. The list must be locked.
 *
 * @param visitor The visitor to invoke for each partition.
 * @return The partition at which the iteration was terminated early, or \c NULL.
 */
BPartition *
BDiskDeviceList::VisitEachPartition(BDiskDeviceVisitor *visitor)
{
	if (visitor) {
		for (int32 i = 0; BDiskDevice *device = DeviceAt(i); i++) {
			if (BPartition *partition = device->VisitEachDescendant(visitor))
				return partition;
		}
	}
	return NULL;
}

/**
 * @brief Iterates through all mounted partitions of all devices using a visitor.
 *
 * Only partitions for which IsMounted() returns \c true are visited. The list
 * must be locked.
 *
 * @param visitor The visitor to invoke for each mounted partition.
 * @return The partition at which the iteration was terminated early, or \c NULL.
 */
BPartition *
BDiskDeviceList::VisitEachMountedPartition(BDiskDeviceVisitor *visitor)
{
	BPartition *partition = NULL;
	if (visitor) {
		struct MountedPartitionFilter : public PartitionFilter {
			virtual ~MountedPartitionFilter() {};
			virtual bool Filter(BPartition *partition, int32 level)
				{ return partition->IsMounted(); }
		} filter;
		PartitionFilterVisitor filterVisitor(visitor, &filter);
		partition = VisitEachPartition(&filterVisitor);
	}
	return partition;
}

/**
 * @brief Iterates through all mountable partitions of all devices using a visitor.
 *
 * Only partitions for which ContainsFileSystem() returns \c true are visited.
 * The list must be locked.
 *
 * @param visitor The visitor to invoke for each mountable partition.
 * @return The partition at which the iteration was terminated early, or \c NULL.
 */
BPartition *
BDiskDeviceList::VisitEachMountablePartition(BDiskDeviceVisitor *visitor)
{
	BPartition *partition = NULL;
	if (visitor) {
		struct MountablePartitionFilter : public PartitionFilter {
			virtual ~MountablePartitionFilter() {};
			virtual bool Filter(BPartition *partition, int32 level)
				{ return partition->ContainsFileSystem(); }
		} filter;
		PartitionFilterVisitor filterVisitor(visitor, &filter);
		partition = VisitEachPartition(&filterVisitor);
	}
	return partition;
}

/**
 * @brief Retrieves the device with the given ID from the list.
 *
 * The list must be locked.
 *
 * @param id The ID of the device to find.
 * @return The device with ID \a id, or \c NULL if not found.
 */
BDiskDevice *
BDiskDeviceList::DeviceWithID(int32 id) const
{
	IDFinderVisitor visitor(id);
	return const_cast<BDiskDeviceList*>(this)->VisitEachDevice(&visitor);
}

/**
 * @brief Retrieves the partition with the given ID from the list.
 *
 * The list must be locked.
 *
 * @param id The ID of the partition to find.
 * @return The partition with ID \a id, or \c NULL if not found.
 */
BPartition *
BDiskDeviceList::PartitionWithID(int32 id) const
{
	IDFinderVisitor visitor(id);
	return const_cast<BDiskDeviceList*>(this)->VisitEachPartition(&visitor);
}

/**
 * @brief Invoked when the mount point of a partition has been moved.
 *
 * The list is locked when this method is invoked.
 *
 * @param partition The concerned partition.
 */
void
BDiskDeviceList::MountPointMoved(BPartition *partition)
{
	PartitionChanged(partition, B_DEVICE_MOUNT_POINT_MOVED);
}

/**
 * @brief Invoked when a partition has been mounted.
 *
 * The list is locked when this method is invoked.
 *
 * @param partition The concerned partition.
 */
void
BDiskDeviceList::PartitionMounted(BPartition *partition)
{
	PartitionChanged(partition, B_DEVICE_PARTITION_MOUNTED);
}

/**
 * @brief Invoked when a partition has been unmounted.
 *
 * The list is locked when this method is invoked.
 *
 * @param partition The concerned partition.
 */
void
BDiskDeviceList::PartitionUnmounted(BPartition *partition)
{
	PartitionChanged(partition, B_DEVICE_PARTITION_UNMOUNTED);
}

/**
 * @brief Invoked when a partition has been initialized with a disk system.
 *
 * The list is locked when this method is invoked.
 *
 * @param partition The concerned partition.
 */
void
BDiskDeviceList::PartitionInitialized(BPartition *partition)
{
	PartitionChanged(partition, B_DEVICE_PARTITION_INITIALIZED);
}

/**
 * @brief Invoked when a partition has been resized.
 *
 * The list is locked when this method is invoked.
 *
 * @param partition The concerned partition.
 */
void
BDiskDeviceList::PartitionResized(BPartition *partition)
{
	PartitionChanged(partition, B_DEVICE_PARTITION_RESIZED);
}

/**
 * @brief Invoked when a partition has been moved.
 *
 * The list is locked when this method is invoked.
 *
 * @param partition The concerned partition.
 */
void
BDiskDeviceList::PartitionMoved(BPartition *partition)
{
	PartitionChanged(partition, B_DEVICE_PARTITION_MOVED);
}

/**
 * @brief Invoked when a new partition has been created.
 *
 * The list is locked when this method is invoked. The base implementation
 * does nothing; override in a subclass to react to partition creation.
 *
 * @param partition The newly created partition.
 */
void
BDiskDeviceList::PartitionCreated(BPartition *partition)
{
}

/**
 * @brief Invoked when a partition has been deleted.
 *
 * Called twice per deletion event. On the first call, \a partition points to
 * a still-valid BPartition object (before the device is updated). On the
 * second call, the device has been updated and \a partition is \c NULL.
 * The list is locked when this method is invoked.
 *
 * @param partition The concerned partition, or \c NULL on the second invocation.
 * @param partitionID The ID of the deleted partition.
 */
void
BDiskDeviceList::PartitionDeleted(BPartition *partition,
	partition_id partitionID)
{
}

/**
 * @brief Invoked when a partition has been defragmented.
 *
 * The list is locked when this method is invoked.
 *
 * @param partition The concerned partition.
 */
void
BDiskDeviceList::PartitionDefragmented(BPartition *partition)
{
	PartitionChanged(partition, B_DEVICE_PARTITION_DEFRAGMENTED);
}

/**
 * @brief Invoked when a partition has been repaired.
 *
 * The list is locked when this method is invoked.
 *
 * @param partition The concerned partition.
 */
void
BDiskDeviceList::PartitionRepaired(BPartition *partition)
{
	PartitionChanged(partition, B_DEVICE_PARTITION_REPAIRED);
}

/**
 * @brief Catch-all hook invoked by partition event hooks (except Created and Deleted).
 *
 * Subclasses that only care that something changed on a partition can override
 * this method instead of the more specific hooks.
 *
 * @param partition The concerned partition.
 * @param event The specific event code that occurred.
 */
void
BDiskDeviceList::PartitionChanged(BPartition *partition, uint32 event)
{
}

/**
 * @brief Invoked when the media of a device has changed.
 *
 * The list is locked when this method is invoked.
 *
 * @param device The concerned device.
 */
void
BDiskDeviceList::MediaChanged(BDiskDevice *device)
{
}

/**
 * @brief Invoked when a new device has been added to the system.
 *
 * The list is locked when this method is invoked.
 *
 * @param device The newly added device.
 */
void
BDiskDeviceList::DeviceAdded(BDiskDevice *device)
{
}

/**
 * @brief Invoked when a device has been removed from the system.
 *
 * The supplied object has already been removed from the list and will be
 * deleted after this hook returns. The list is locked when invoked.
 *
 * @param device The removed device.
 */
void
BDiskDeviceList::DeviceRemoved(BDiskDevice *device)
{
}

/**
 * @brief Starts watching for disk device notifications via the roster.
 *
 * The object must be locked (if possible) when this method is invoked.
 *
 * @return \c B_OK if watching started successfully, another error code otherwise.
 */
status_t
BDiskDeviceList::_StartWatching()
{
	if (!Looper() || fSubscribed)
		return B_BAD_VALUE;

	status_t error = BDiskDeviceRoster().StartWatching(BMessenger(this));
	fSubscribed = (error == B_OK);
	return error;
}

/**
 * @brief Stops watching for disk device notifications.
 *
 * The object must be locked (if possible) when this method is invoked.
 */
void
BDiskDeviceList::_StopWatching()
{
	if (fSubscribed) {
		BDiskDeviceRoster().StopWatching(BMessenger(this));
		fSubscribed = false;
	}
}

/**
 * @brief Handles a "mount point moved" notification message.
 *
 * @param message The notification message containing device and partition IDs.
 */
void
BDiskDeviceList::_MountPointMoved(BMessage *message)
{
	if (_UpdateDevice(message) != NULL) {
		if (BPartition *partition = _FindPartition(message))
			MountPointMoved(partition);
	}
}

/**
 * @brief Handles a "partition mounted" notification message.
 *
 * @param message The notification message containing device and partition IDs.
 */
void
BDiskDeviceList::_PartitionMounted(BMessage *message)
{
	if (_UpdateDevice(message) != NULL) {
		if (BPartition *partition = _FindPartition(message))
			PartitionMounted(partition);
	}
}

/**
 * @brief Handles a "partition unmounted" notification message.
 *
 * @param message The notification message containing device and partition IDs.
 */
void
BDiskDeviceList::_PartitionUnmounted(BMessage *message)
{
	if (_UpdateDevice(message) != NULL) {
		if (BPartition *partition = _FindPartition(message))
			PartitionUnmounted(partition);
	}
}

/**
 * @brief Handles a "partition initialized" notification message.
 *
 * @param message The notification message containing device and partition IDs.
 */
void
BDiskDeviceList::_PartitionInitialized(BMessage *message)
{
	if (_UpdateDevice(message) != NULL) {
		if (BPartition *partition = _FindPartition(message))
			PartitionInitialized(partition);
	}
}

/**
 * @brief Handles a "partition resized" notification message.
 *
 * @param message The notification message containing device and partition IDs.
 */
void
BDiskDeviceList::_PartitionResized(BMessage *message)
{
	if (_UpdateDevice(message) != NULL) {
		if (BPartition *partition = _FindPartition(message))
			PartitionResized(partition);
	}
}

/**
 * @brief Handles a "partition moved" notification message.
 *
 * @param message The notification message containing device and partition IDs.
 */
void
BDiskDeviceList::_PartitionMoved(BMessage *message)
{
	if (_UpdateDevice(message) != NULL) {
		if (BPartition *partition = _FindPartition(message))
			PartitionMoved(partition);
	}
}

/**
 * @brief Handles a "partition created" notification message.
 *
 * @param message The notification message containing device and partition IDs.
 */
void
BDiskDeviceList::_PartitionCreated(BMessage *message)
{
	if (_UpdateDevice(message) != NULL) {
		if (BPartition *partition = _FindPartition(message))
			PartitionCreated(partition);
	}
}

/**
 * @brief Handles a "partition deleted" notification message.
 *
 * Invokes PartitionDeleted() twice: once before and once after updating the
 * device, mirroring the two-phase deletion protocol.
 *
 * @param message The notification message containing device and partition IDs.
 */
void
BDiskDeviceList::_PartitionDeleted(BMessage *message)
{
	if (BPartition *partition = _FindPartition(message)) {
		partition_id id = partition->ID();
		PartitionDeleted(partition, id);
		if (_UpdateDevice(message))
			PartitionDeleted(NULL, id);
	}
}

/**
 * @brief Handles a "partition defragmented" notification message.
 *
 * @param message The notification message containing device and partition IDs.
 */
void
BDiskDeviceList::_PartitionDefragmented(BMessage *message)
{
	if (_UpdateDevice(message) != NULL) {
		if (BPartition *partition = _FindPartition(message))
			PartitionDefragmented(partition);
	}
}

/**
 * @brief Handles a "partition repaired" notification message.
 *
 * @param message The notification message containing device and partition IDs.
 */
void
BDiskDeviceList::_PartitionRepaired(BMessage *message)
{
	if (_UpdateDevice(message) != NULL) {
		if (BPartition *partition = _FindPartition(message))
			PartitionRepaired(partition);
	}
}

/**
 * @brief Handles a "media changed" notification message.
 *
 * @param message The notification message containing the device ID.
 */
void
BDiskDeviceList::_MediaChanged(BMessage *message)
{
	if (BDiskDevice *device = _UpdateDevice(message))
		MediaChanged(device);
}

/**
 * @brief Handles a "device added" notification message.
 *
 * Fetches the new device from the roster and adds it to the list.
 *
 * @param message The notification message containing the new device ID.
 */
void
BDiskDeviceList::_DeviceAdded(BMessage *message)
{
	int32 id;
	if (message->FindInt32("device_id", &id) == B_OK && !DeviceWithID(id)) {
		BDiskDevice *device = new(nothrow) BDiskDevice;
		if (BDiskDeviceRoster().GetDeviceWithID(id, device) == B_OK) {
			fDevices.AddItem(device);
			DeviceAdded(device);
		} else
			delete device;
	}
}

/**
 * @brief Handles a "device removed" notification message.
 *
 * Removes the device from the list and calls DeviceRemoved(), then deletes it.
 *
 * @param message The notification message containing the removed device ID.
 */
void
BDiskDeviceList::_DeviceRemoved(BMessage *message)
{
	if (BDiskDevice *device = _FindDevice(message)) {
		fDevices.RemoveItem(device, false);
		DeviceRemoved(device);
		delete device;
	}
}

/**
 * @brief Returns the device identified by the "device_id" field in a message.
 *
 * @param message The notification message containing a "device_id" field.
 * @return The matching device, or \c NULL if not found.
 */
BDiskDevice *
BDiskDeviceList::_FindDevice(BMessage *message)
{
	BDiskDevice *device = NULL;
	int32 id;
	if (message->FindInt32("device_id", &id) == B_OK)
		device = DeviceWithID(id);
	return device;
}

/**
 * @brief Returns the partition identified by the "partition_id" field in a message.
 *
 * @param message The notification message containing a "partition_id" field.
 * @return The matching partition, or \c NULL if not found.
 */
BPartition *
BDiskDeviceList::_FindPartition(BMessage *message)
{
	BPartition *partition = NULL;
	int32 id;
	if (message->FindInt32("partition_id", &id) == B_OK)
		partition = PartitionWithID(id);
	return partition;
}

/**
 * @brief Finds and updates the device identified by a notification message.
 *
 * If the device's Update() call fails, it is removed from the list.
 *
 * @param message The notification message containing a "device_id" field.
 * @return The updated device, or \c NULL if the device was not found or
 *         could not be updated.
 */
BDiskDevice *
BDiskDeviceList::_UpdateDevice(BMessage *message)
{
	BDiskDevice *device = _FindDevice(message);
	if (device) {
		if (device->Update() != B_OK) {
			fDevices.RemoveItem(device);
			device = NULL;
		}
	}
	return device;
}
