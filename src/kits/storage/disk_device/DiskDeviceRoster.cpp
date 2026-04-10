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
 *   Copyright 2003-2009, Haiku, Inc. All Rights Reserved.
 *   Authors: Ingo Weinhold, bonefish@cs.tu-berlin.de
 *            Axel Dörfler, axeld@pinc-software.de
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file DiskDeviceRoster.cpp
 * @brief Implementation of the BDiskDeviceRoster class.
 *
 * BDiskDeviceRoster provides an interface for iterating through all disk
 * devices and disk systems known to the system, for looking up devices and
 * partitions by ID or path, and for subscribing to disk device change
 * notifications. It also supports registration and unregistration of
 * file-backed virtual disk devices.
 *
 * @see BDiskDevice
 * @see BDiskDeviceList
 */

#include <DiskDeviceRoster.h>

#include <new>

#include <Directory.h>
#include <DiskDevice.h>
#include <DiskDevicePrivate.h>
#include <DiskSystem.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Partition.h>
#include <Path.h>
#include <Volume.h>

#include <MessengerPrivate.h>

#include <syscalls.h>
#include <ddm_userland_interface_defs.h>


/**
 * @brief Interface for iterating through disk devices and listening for changes.
 */

/** @brief find_directory constants of the add-on dirs to be searched. */
static const directory_which kAddOnDirs[] = {
	B_USER_NONPACKAGED_ADDONS_DIRECTORY,
	B_USER_ADDONS_DIRECTORY,
	B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
	B_SYSTEM_ADDONS_DIRECTORY,
};
/** @brief Size of the kAddOnDirs array. */
static const int32 kAddOnDirCount
	= sizeof(kAddOnDirs) / sizeof(directory_which);


/**
 * @brief Creates a BDiskDeviceRoster object ready for use.
 */
BDiskDeviceRoster::BDiskDeviceRoster()
	: fDeviceCookie(0),
	  fDiskSystemCookie(0),
	  fJobCookie(0)
//	  fPartitionAddOnDir(NULL),
//	  fFSAddOnDir(NULL),
//	  fPartitionAddOnDirIndex(0),
//	  fFSAddOnDirIndex(0)
{
}


/**
 * @brief Frees all resources associated with the object.
 */
BDiskDeviceRoster::~BDiskDeviceRoster()
{
//	if (fPartitionAddOnDir)
//		delete fPartitionAddOnDir;
//	if (fFSAddOnDir)
//		delete fFSAddOnDir;
}


/**
 * @brief Returns the next BDiskDevice in the iteration sequence.
 *
 * @param device Pointer to a pre-allocated BDiskDevice to be initialized to
 *        represent the next device.
 * @return \c B_OK on success, \c B_ENTRY_NOT_FOUND if the end of the device
 *         list has been reached, or another error code on failure.
 */
status_t
BDiskDeviceRoster::GetNextDevice(BDiskDevice* device)
{
	if (!device)
		return B_BAD_VALUE;

	size_t neededSize = 0;
	partition_id id = _kern_get_next_disk_device_id(&fDeviceCookie,
		&neededSize);
	if (id < 0)
		return id;

	return device->_SetTo(id, true, neededSize);
}


/**
 * @brief Rewinds the device list iterator to the beginning.
 *
 * @return \c B_OK always.
 */
status_t
BDiskDeviceRoster::RewindDevices()
{
	fDeviceCookie = 0;
	return B_OK;
}


/**
 * @brief Returns the next BDiskSystem in the iteration sequence.
 *
 * @param system Pointer to a pre-allocated BDiskSystem to be initialized to
 *        the next disk system.
 * @return \c B_OK on success, \c B_ENTRY_NOT_FOUND at the end, or an error code.
 */
status_t
BDiskDeviceRoster::GetNextDiskSystem(BDiskSystem* system)
{
	if (!system)
		return B_BAD_VALUE;
	user_disk_system_info info;
	status_t error = _kern_get_next_disk_system_info(&fDiskSystemCookie,
		&info);
	if (error == B_OK)
		error = system->_SetTo(&info);
	return error;
}


/**
 * @brief Rewinds the disk system list iterator to the beginning.
 *
 * @return \c B_OK always.
 */
status_t
BDiskDeviceRoster::RewindDiskSystems()
{
	fDiskSystemCookie = 0;
	return B_OK;
}


/**
 * @brief Finds a disk system by name (short name, internal name, or pretty name).
 *
 * @param system Pointer to a pre-allocated BDiskSystem to be initialized to
 *        the found disk system.
 * @param name The name to search for; matched against short, internal, and
 *        pretty names.
 * @return \c B_OK if found, \c B_ENTRY_NOT_FOUND if no match exists, or
 *         an error code on failure.
 */
status_t
BDiskDeviceRoster::GetDiskSystem(BDiskSystem* system, const char* name)
{
	if (!system)
		return B_BAD_VALUE;

	int32 cookie = 0;
	user_disk_system_info info;
	while (_kern_get_next_disk_system_info(&cookie, &info) == B_OK) {
		if (!strcmp(name, info.name)
			|| !strcmp(name, info.short_name)
			|| !strcmp(name, info.pretty_name))
			return system->_SetTo(&info);
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Registers a file as a virtual disk device.
 *
 * @param filename Path to the file to register as a disk device.
 * @return The partition_id of the new virtual device on success, or a
 *         negative error code on failure.
 */
partition_id
BDiskDeviceRoster::RegisterFileDevice(const char* filename)
{
	if (!filename)
		return B_BAD_VALUE;
	return _kern_register_file_device(filename);
}


/**
 * @brief Unregisters a file-backed virtual disk device by filename.
 *
 * @param filename Path to the file that was registered as a disk device.
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BDiskDeviceRoster::UnregisterFileDevice(const char* filename)
{
	if (!filename)
		return B_BAD_VALUE;
	return _kern_unregister_file_device(-1, filename);
}


/**
 * @brief Unregisters a file-backed virtual disk device by partition ID.
 *
 * @param device The partition_id of the virtual device to unregister.
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BDiskDeviceRoster::UnregisterFileDevice(partition_id device)
{
	if (device < 0)
		return B_BAD_VALUE;
	return _kern_unregister_file_device(device, NULL);
}


/**
 * @brief Iterates through all devices, invoking the visitor for each one.
 *
 * The visitor's Visit(BDiskDevice*) is invoked for each device. If it returns
 * \c true, the iteration stops early and this method returns \c true. If
 * \a device is supplied, it is set to the device that terminated the iteration.
 *
 * @param visitor The visitor to invoke for each device.
 * @param device Optional pre-allocated BDiskDevice to hold the terminating device.
 * @return \c true if the iteration was terminated early, \c false otherwise.
 */
bool
BDiskDeviceRoster::VisitEachDevice(BDiskDeviceVisitor* visitor,
	BDiskDevice* device)
{
	bool terminatedEarly = false;
	if (visitor) {
		int32 oldCookie = fDeviceCookie;
		fDeviceCookie = 0;
		BDiskDevice deviceOnStack;
		BDiskDevice* useDevice = device ? device : &deviceOnStack;
		while (!terminatedEarly && GetNextDevice(useDevice) == B_OK)
			terminatedEarly = visitor->Visit(useDevice);
		fDeviceCookie = oldCookie;
		if (!terminatedEarly)
			useDevice->Unset();
	}
	return terminatedEarly;
}


/**
 * @brief Pre-order traverses all devices and their partition trees using a visitor.
 *
 * Visit(BDiskDevice*) is called for each device and Visit(BPartition*, int32)
 * for each non-device partition. Terminates early when a Visit() call returns
 * \c true. If supplied, \a device and \a partition are set to the terminating
 * objects.
 *
 * @param visitor The visitor to invoke for each device and partition.
 * @param device Optional pre-allocated BDiskDevice to hold the terminating device.
 * @param partition Optional pointer to a BPartition* to hold the terminating partition.
 * @return \c true if the iteration was terminated early, \c false otherwise.
 */
bool
BDiskDeviceRoster::VisitEachPartition(BDiskDeviceVisitor* visitor,
	BDiskDevice* device, BPartition** partition)
{
	bool terminatedEarly = false;
	if (visitor) {
		int32 oldCookie = fDeviceCookie;
		fDeviceCookie = 0;
		BDiskDevice deviceOnStack;
		BDiskDevice* useDevice = device ? device : &deviceOnStack;
		BPartition* foundPartition = NULL;
		while (GetNextDevice(useDevice) == B_OK) {
			foundPartition = useDevice->VisitEachDescendant(visitor);
			if (foundPartition) {
				terminatedEarly = true;
				break;
			}
		}
		fDeviceCookie = oldCookie;
		if (!terminatedEarly)
			useDevice->Unset();
		else if (device && partition)
			*partition = foundPartition;
	}
	return terminatedEarly;
}


/**
 * @brief Iterates through all mounted partitions across all devices.
 *
 * Only partitions for which IsMounted() returns \c true are visited.
 *
 * @param visitor The visitor to invoke for each mounted partition.
 * @param device Optional pre-allocated BDiskDevice to hold the terminating device.
 * @param partition Optional pointer to a BPartition* to hold the terminating partition.
 * @return \c true if the iteration was terminated early, \c false otherwise.
 */
bool
BDiskDeviceRoster::VisitEachMountedPartition(BDiskDeviceVisitor* visitor,
	BDiskDevice* device, BPartition** partition)
{
	bool terminatedEarly = false;
	if (visitor) {
		struct MountedPartitionFilter : public PartitionFilter {
			virtual bool Filter(BPartition *partition, int32)
				{ return partition->IsMounted(); }
		} filter;
		PartitionFilterVisitor filterVisitor(visitor, &filter);
		terminatedEarly
			= VisitEachPartition(&filterVisitor, device, partition);
	}
	return terminatedEarly;
}


/**
 * @brief Iterates through all mountable partitions across all devices.
 *
 * Only partitions for which ContainsFileSystem() returns \c true are visited.
 *
 * @param visitor The visitor to invoke for each mountable partition.
 * @param device Optional pre-allocated BDiskDevice to hold the terminating device.
 * @param partition Optional pointer to a BPartition* to hold the terminating partition.
 * @return \c true if the iteration was terminated early, \c false otherwise.
 */
bool
BDiskDeviceRoster::VisitEachMountablePartition(BDiskDeviceVisitor* visitor,
	BDiskDevice* device, BPartition** partition)
{
	bool terminatedEarly = false;
	if (visitor) {
		struct MountablePartitionFilter : public PartitionFilter {
			virtual bool Filter(BPartition *partition, int32)
				{ return partition->ContainsFileSystem(); }
		} filter;
		PartitionFilterVisitor filterVisitor(visitor, &filter);
		terminatedEarly
			= VisitEachPartition(&filterVisitor, device, partition);
	}
	return terminatedEarly;
}


/**
 * @brief Finds the partition that corresponds to a given BVolume.
 *
 * @param volume The mounted volume whose backing partition is sought.
 * @param device Pre-allocated BDiskDevice to be initialized to the device
 *        containing the found partition.
 * @param _partition Set to the BPartition corresponding to \a volume.
 * @return \c B_OK if found, \c B_ENTRY_NOT_FOUND otherwise.
 */
status_t
BDiskDeviceRoster::FindPartitionByVolume(const BVolume& volume,
	BDiskDevice* device, BPartition** _partition)
{
	class FindPartitionVisitor : public BDiskDeviceVisitor {
	public:
		FindPartitionVisitor(dev_t volume)
			:
			fVolume(volume)
		{
		}

		virtual bool Visit(BDiskDevice* device)
		{
			return Visit(device, 0);
		}

		virtual bool Visit(BPartition* partition, int32 level)
		{
			BVolume volume;
			return partition->GetVolume(&volume) == B_OK
				&& volume.Device() == fVolume;
		}

	private:
		dev_t	fVolume;
	} visitor(volume.Device());

	if (VisitEachMountedPartition(&visitor, device, _partition))
		return B_OK;

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Finds the partition that is mounted at a given mount point path.
 *
 * @param mountPoint The file system path of the mount point to search for.
 * @param device Pre-allocated BDiskDevice to be initialized to the containing device.
 * @param _partition Set to the BPartition mounted at \a mountPoint.
 * @return \c B_OK if found, \c B_ENTRY_NOT_FOUND otherwise.
 */
status_t
BDiskDeviceRoster::FindPartitionByMountPoint(const char* mountPoint,
	BDiskDevice* device, BPartition** _partition)
{
	BVolume volume(dev_for_path(mountPoint));
	if (volume.InitCheck() == B_OK
		&& FindPartitionByVolume(volume, device, _partition) == B_OK)
		return B_OK;

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Retrieves the BDiskDevice identified by a given device ID.
 *
 * @param id The ID of the device to retrieve.
 * @param device Pointer to a pre-allocated BDiskDevice to be initialized.
 * @return \c B_OK on success, \c B_ENTRY_NOT_FOUND if no such device exists,
 *         or another error code.
 */
status_t
BDiskDeviceRoster::GetDeviceWithID(int32 id, BDiskDevice* device) const
{
	if (!device)
		return B_BAD_VALUE;
	return device->_SetTo(id, true, 0);
}


/**
 * @brief Retrieves the BPartition and its containing BDiskDevice by partition ID.
 *
 * @param id The ID of the partition to retrieve.
 * @param device Pre-allocated BDiskDevice to be initialized to the device
 *        that contains the partition.
 * @param partition Set to a pointer to the BPartition with ID \a id.
 * @return \c B_OK on success, \c B_ENTRY_NOT_FOUND if not found, or another
 *         error code.
 */
status_t
BDiskDeviceRoster::GetPartitionWithID(int32 id, BDiskDevice* device,
	BPartition** partition) const
{
	if (!device || !partition)
		return B_BAD_VALUE;

	// retrieve the device data
	status_t error = device->_SetTo(id, false, 0);
	if (error != B_OK)
		return error;

	// find the partition object
	*partition = device->FindDescendant(id);
	if (!*partition)	// should never happen!
		return B_ENTRY_NOT_FOUND;

	return B_OK;
}


/**
 * @brief Finds the disk device that owns the given file or device path.
 *
 * @param filename The path to a file or device node to locate.
 * @param device Pre-allocated BDiskDevice to be initialized to the result.
 * @return \c B_OK on success, or an error code if the device cannot be found.
 */
status_t
BDiskDeviceRoster::GetDeviceForPath(const char* filename, BDiskDevice* device)
{
	if (!filename || !device)
		return B_BAD_VALUE;

	// get the device ID
	size_t neededSize = 0;
	partition_id id = _kern_find_disk_device(filename, &neededSize);
	if (id < 0)
		return id;

	// retrieve the device data
	return device->_SetTo(id, true, neededSize);
}


/**
 * @brief Finds the partition and its containing device for a given path.
 *
 * @param filename The path to a file or device node to locate.
 * @param device Pre-allocated BDiskDevice to be initialized to the containing device.
 * @param partition Set to the BPartition that covers \a filename.
 * @return \c B_OK on success, or an error code if the partition cannot be found.
 */
status_t
BDiskDeviceRoster::GetPartitionForPath(const char* filename,
	BDiskDevice* device, BPartition** partition)
{
	if (!filename || !device || !partition)
		return B_BAD_VALUE;

	// get the partition ID
	size_t neededSize = 0;
	partition_id id = _kern_find_partition(filename, &neededSize);
	if (id < 0)
		return id;

	// retrieve the device data
	status_t error = device->_SetTo(id, false, neededSize);
	if (error != B_OK)
		return error;

	// find the partition object
	*partition = device->FindDescendant(id);
	if (!*partition)	// should never happen!
		return B_ENTRY_NOT_FOUND;
	return B_OK;
}


/**
 * @brief Finds the file-backed virtual disk device that contains the given path.
 *
 * @param filename The path to look up within file-backed disk devices.
 * @param device Pre-allocated BDiskDevice to be initialized to the result.
 * @return \c B_OK on success, or an error code if no matching device is found.
 */
status_t
BDiskDeviceRoster::GetFileDeviceForPath(const char* filename,
	BDiskDevice* device)
{
	if (!filename || !device)
		return B_BAD_VALUE;

	// get the device ID
	size_t neededSize = 0;
	partition_id id = _kern_find_file_disk_device(filename, &neededSize);
	if (id < 0)
		return id;

	// retrieve the device data
	return device->_SetTo(id, true, neededSize);
}


/**
 * @brief Registers a messenger to receive disk device event notifications.
 *
 * If \a target is already watching, its event mask is replaced with
 * \a eventMask.
 *
 * @param target A BMessenger identifying the target to notify.
 * @param eventMask A bitmask specifying which events to watch for.
 * @return \c B_OK on success, \c B_BAD_VALUE if \a eventMask is 0, or
 *         another error code on failure.
 */
status_t
BDiskDeviceRoster::StartWatching(BMessenger target, uint32 eventMask)
{
	if (eventMask == 0)
		return B_BAD_VALUE;

	BMessenger::Private messengerPrivate(target);
	port_id port = messengerPrivate.Port();
	int32 token = messengerPrivate.Token();

	return _kern_start_watching_disks(eventMask, port, token);
}


/**
 * @brief Removes a messenger from the disk device event notification list.
 *
 * @param target A BMessenger identifying the target to stop notifying.
 * @return \c B_OK on success, or an error code on failure.
 */
status_t
BDiskDeviceRoster::StopWatching(BMessenger target)
{
	BMessenger::Private messengerPrivate(target);
	port_id port = messengerPrivate.Port();
	int32 token = messengerPrivate.Token();

	return _kern_stop_watching_disks(port, token);
}

#if 0

/**
 * @brief Returns the next partitioning system capable of partitioning.
 *
 * The returned \a shortName can be passed to BSession::Partition().
 *
 * @param shortName Pre-allocated buffer of at least B_FILE_NAME_LENGTH bytes
 *        to receive the short name of the partitioning system.
 * @param longName Pre-allocated buffer of at least B_FILE_NAME_LENGTH bytes
 *        to receive the long name. May be \c NULL.
 * @return \c B_OK on success, \c B_BAD_VALUE if \a shortName is \c NULL,
 *         \c B_ENTRY_NOT_FOUND at the end of the list, or another error code.
 */
status_t
BDiskDeviceRoster::GetNextPartitioningSystem(char *shortName, char *longName)
{
	status_t error = (shortName ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		// search until an add-on has been found or the end of all directories
		// has been reached
		bool found = false;
		do {
			// get the next add-on in the current dir
			AddOnImage image;
			error = _GetNextAddOn(fPartitionAddOnDir, &image);
			if (error == B_OK) {
				// add-on loaded: get the function that creates an add-on
				// object
				BDiskScannerPartitionAddOn *(*create_add_on)();
				if (get_image_symbol(image.ID(), "create_ds_partition_add_on",
									 B_SYMBOL_TYPE_TEXT,
									 (void**)&create_add_on) == B_OK) {
					// create the add-on object and copy the requested data
					if (BDiskScannerPartitionAddOn *addOn
						= (*create_add_on)()) {
						const char *addOnShortName = addOn->ShortName();
						const char *addOnLongName = addOn->LongName();
						if (addOnShortName && addOnLongName) {
							strcpy(shortName, addOnShortName);
							if (longName)
								strcpy(longName, addOnLongName);
							found = true;
						}
						delete addOn;
					}
				}
			} else if (error == B_ENTRY_NOT_FOUND) {
				// end of the current directory has been reached, try next dir
				error = _GetNextAddOnDir(&fPartitionAddOnDir,
										 &fPartitionAddOnDirIndex,
										 "partition");
			}
		} while (error == B_OK && !found);
	}
	return error;
}


/**
 * @brief Returns the next file system capable of initializing partitions.
 *
 * The returned \a shortName can be passed to BPartition::Initialize().
 *
 * @param shortName Pre-allocated buffer of at least B_FILE_NAME_LENGTH bytes
 *        to receive the short name of the file system.
 * @param longName Pre-allocated buffer of at least B_FILE_NAME_LENGTH bytes
 *        to receive the long name. May be \c NULL.
 * @return \c B_OK on success, \c B_BAD_VALUE if \a shortName is \c NULL,
 *         \c B_ENTRY_NOT_FOUND at the end of the list, or another error code.
 */
status_t
BDiskDeviceRoster::GetNextFileSystem(char *shortName, char *longName)
{
	status_t error = (shortName ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		// search until an add-on has been found or the end of all directories
		// has been reached
		bool found = false;
		do {
			// get the next add-on in the current dir
			AddOnImage image;
			error = _GetNextAddOn(fFSAddOnDir, &image);
			if (error == B_OK) {
				// add-on loaded: get the function that creates an add-on
				// object
				BDiskScannerFSAddOn *(*create_add_on)();
				if (get_image_symbol(image.ID(), "create_ds_fs_add_on",
									 B_SYMBOL_TYPE_TEXT,
									 (void**)&create_add_on) == B_OK) {
					// create the add-on object and copy the requested data
					if (BDiskScannerFSAddOn *addOn = (*create_add_on)()) {
						const char *addOnShortName = addOn->ShortName();
						const char *addOnLongName = addOn->LongName();
						if (addOnShortName && addOnLongName) {
							strcpy(shortName, addOnShortName);
							if (longName)
								strcpy(longName, addOnLongName);
							found = true;
						}
						delete addOn;
					}
				}
			} else if (error == B_ENTRY_NOT_FOUND) {
				// end of the current directory has been reached, try next dir
				error = _GetNextAddOnDir(&fFSAddOnDir, &fFSAddOnDirIndex,
										 "fs");
			}
		} while (error == B_OK && !found);
	}
	return error;
}


/**
 * @brief Rewinds the partitioning system list iterator.
 *
 * @return \c B_OK always.
 */
status_t
BDiskDeviceRoster::RewindPartitiningSystems()
{
	if (fPartitionAddOnDir) {
		delete fPartitionAddOnDir;
		fPartitionAddOnDir = NULL;
	}
	fPartitionAddOnDirIndex = 0;
	return B_OK;
}


/**
 * @brief Rewinds the file system list iterator.
 *
 * @return \c B_OK always.
 */
status_t
BDiskDeviceRoster::RewindFileSystems()
{
	if (fFSAddOnDir) {
		delete fFSAddOnDir;
		fFSAddOnDir = NULL;
	}
	fFSAddOnDirIndex = 0;
	return B_OK;
}


/**
 * @brief Retrieves a BDiskDevice for a given device, session, or partition ID.
 *
 * @param fieldName "device_id", "session_id", or "partition_id" indicating
 *        the type of ID being provided.
 * @param id The ID value to look up.
 * @param device Pre-allocated BDiskDevice to be initialized to the result.
 * @return \c B_OK on success, \c B_ENTRY_NOT_FOUND if not found, or an error code.
 */
status_t
BDiskDeviceRoster::_GetObjectWithID(const char *fieldName, int32 id,
	BDiskDevice *device) const
{
	status_t error = (device ? B_OK : B_BAD_VALUE);
	// compose request message
	BMessage request(B_REG_GET_DISK_DEVICE);
	if (error == B_OK)
		error = request.AddInt32(fieldName, id);
	// send request
	BMessage reply;
	if (error == B_OK)
		error = fManager.SendMessage(&request, &reply);
	// analyze reply
	if (error == B_OK) {
		// result
		status_t result = B_OK;
		error = reply.FindInt32("result", &result);
		if (error == B_OK)
			error = result;
		// device
		BMessage archive;
		if (error == B_OK)
			error = reply.FindMessage("device", &archive);
		if (error == B_OK)
			error = device->_Unarchive(&archive);
	}
	return error;
}


/**
 * @brief Finds and loads the next add-on from a named subdirectory.
 *
 * Searches across all add-on directories (user and system) for the given
 * subdirectory name, loading the next available add-on image.
 *
 * @param directory Pointer to the current BDirectory* (updated as dirs are exhausted).
 * @param index Pointer to the current index into kAddOnDirs.
 * @param subdir The subdirectory name within "disk_scanner" to search.
 * @param image Pointer to an AddOnImage to be loaded with the found add-on.
 * @return \c B_OK on success, \c B_ENTRY_NOT_FOUND at end, or an error code.
 */
status_t
BDiskDeviceRoster::_GetNextAddOn(BDirectory **directory, int32 *index,
	const char *subdir, AddOnImage *image)
{
	status_t error = (directory && index && subdir && image
					  ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		// search until an add-on has been found or the end of all directories
		// has been reached
		bool found = false;
		do {
			// get the next add-on in the current dir
			error = _GetNextAddOn(*directory, image);
			if (error == B_OK) {
				found = true;
			} else if (error == B_ENTRY_NOT_FOUND) {
				// end of the current directory has been reached, try next dir
				error = _GetNextAddOnDir(directory, index, subdir);
			}
		} while (error == B_OK && !found);
	}
	return error;
}


/**
 * @brief Loads the next add-on image from an open directory.
 *
 * Iterates through directory entries until a loadable add-on image is found.
 *
 * @param directory The directory to iterate, or \c NULL (returns B_ENTRY_NOT_FOUND).
 * @param image Pointer to an AddOnImage to be loaded.
 * @return \c B_OK on success, \c B_ENTRY_NOT_FOUND at end, or an error code.
 */
status_t
BDiskDeviceRoster::_GetNextAddOn(BDirectory *directory, AddOnImage *image)
{
	status_t error = (directory ? B_OK : B_ENTRY_NOT_FOUND);
	if (error == B_OK) {
		// iterate through the entry list and try to load the entries
		bool found = false;
		while (error == B_OK && !found) {
			BEntry entry;
			error = directory->GetNextEntry(&entry);
			BPath path;
			if (error == B_OK && entry.GetPath(&path) == B_OK)
				found = (image->Load(path.Path()) == B_OK);
		}
	}
	return error;
}


/**
 * @brief Advances the add-on directory path to the next base add-on directory.
 *
 * @param path Pointer to a BPath to be set to the next add-on directory.
 * @param index Pointer to the index into kAddOnDirs indicating the next entry.
 * @param subdir The subdirectory name within "disk_scanner".
 * @return \c B_OK on success, \c B_ENTRY_NOT_FOUND when all directories are
 *         exhausted, or an error code.
 */
status_t
BDiskDeviceRoster::_GetNextAddOnDir(BPath *path, int32 *index,
	const char *subdir)
{
	status_t error = (*index < kAddOnDirCount ? B_OK : B_ENTRY_NOT_FOUND);
	// get the add-on dir path
	if (error == B_OK) {
		error = find_directory(kAddOnDirs[*index], path);
		(*index)++;
	}
	// construct the subdirectory path
	if (error == B_OK) {
		error = path->Append("disk_scanner");
		if (error == B_OK)
			error = path->Append(subdir);
	}
if (error == B_OK)
printf("  next add-on dir: `%s'\n", path->Path());
	return error;
}


/**
 * @brief Advances the add-on directory object to the next base add-on directory.
 *
 * Creates a BDirectory for the next add-on subdirectory path, cycling through
 * all base directories in kAddOnDirs.
 *
 * @param directory Pointer to a BDirectory* to be updated to the next directory.
 * @param index Pointer to the index into kAddOnDirs.
 * @param subdir The subdirectory name within "disk_scanner".
 * @return \c B_OK on success, \c B_ENTRY_NOT_FOUND when exhausted, or an error code.
 */
status_t
BDiskDeviceRoster::_GetNextAddOnDir(BDirectory **directory, int32 *index,
	const char *subdir)
{
	BPath path;
	status_t error = _GetNextAddOnDir(&path, index, subdir);
	// create a BDirectory object, if there is none yet.
	if (error == B_OK && !*directory) {
		*directory = new BDirectory;
		if (!*directory)
			error = B_NO_MEMORY;
	}
	// init the directory
	if (error == B_OK)
		error = (*directory)->SetTo(path.Path());
	// cleanup on error
	if (error != B_OK && *directory) {
		delete *directory;
		*directory = NULL;
	}
	return error;
}


/**
 * @brief Searches all add-on directories for a partition add-on with the given name.
 *
 * Loads the image and creates the add-on object if found.
 *
 * @param partitioningSystem The short name of the partitioning system to find.
 * @param image Pointer to an AddOnImage to be loaded with the found add-on.
 * @param _addOn Set to the created BDiskScannerPartitionAddOn object if found.
 * @return \c B_OK on success, or an error code if the add-on cannot be found.
 */
status_t
BDiskDeviceRoster::_LoadPartitionAddOn(const char *partitioningSystem,
	AddOnImage *image, BDiskScannerPartitionAddOn **_addOn)
{
	status_t error = partitioningSystem && image && _addOn
		? B_OK : B_BAD_VALUE;

	// load the image
	bool found = false;
	BPath path;
	BDirectory *directory = NULL;
	int32 index = 0;
	while (error == B_OK && !found) {
		error = _GetNextAddOn(&directory, &index, "partition", image);
		if (error == B_OK) {
			// add-on loaded: get the function that creates an add-on
			// object
			BDiskScannerPartitionAddOn *(*create_add_on)();
			if (get_image_symbol(image->ID(), "create_ds_partition_add_on",
								 B_SYMBOL_TYPE_TEXT,
								 (void**)&create_add_on) == B_OK) {
				// create the add-on object and copy the requested data
				if (BDiskScannerPartitionAddOn *addOn = (*create_add_on)()) {
					if (!strcmp(addOn->ShortName(), partitioningSystem)) {
						*_addOn = addOn;
						found = true;
					} else
						delete addOn;
				}
			}
		}
	}
	// cleanup
	if (directory)
		delete directory;
	if (error != B_OK && image)
		image->Unload();
	return error;
}

#endif	// 0
