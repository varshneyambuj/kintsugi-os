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
 *   Copyright 2004-2018, Haiku, Inc. All rights reserved.
 *   Copyright 2003-2004, Ingo Weinhold, bonefish@cs.tu-berlin.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file KDiskDeviceManager.cpp
 * @brief Central registry and manager for all disk devices and partition tables.
 *
 * KDiskDeviceManager maintains the set of known KDiskDevice objects, scans
 * for new devices, loads the appropriate partitioning system and file system
 * add-ons, and provides the query interface used by devfs and user-space
 * disk management tools. Acts as the entry point for all disk device
 * discovery and partition modification operations.
 *
 * @see KDiskDevice.cpp, KPartition.cpp, KDiskSystem.cpp
 */

#include "KDiskDevice.h"
#include "KDiskDeviceManager.h"
#include "KDiskDeviceUtils.h"
#include "KDiskSystem.h"
#include "KFileDiskDevice.h"
#include "KFileSystem.h"
#include "KPartition.h"
#include "KPartitioningSystem.h"
#include "KPartitionVisitor.h"
#include "KPath.h"

#include <VectorMap.h>
#include <VectorSet.h>

#include <DiskDeviceRoster.h>
#include <KernelExport.h>
#include <NodeMonitor.h>

#include <boot_device.h>
#include <kmodule.h>
#include <node_monitor.h>
#include <Notifications.h>
#include <util/kernel_cpp.h>
#include <vfs.h>

#include <dirent.h>
#include <errno.h>
#include <module.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


//#define TRACE_KDISK_DEVICE_MANAGER
#ifdef TRACE_KDISK_DEVICE_MANAGER
#	define TRACE		TRACE_ALWAYS
#else
#	define TRACE(x...)	do { } while (false)
#endif
#define TRACE_ALWAYS(x...)	dprintf("disk_device_manager: " x)
#define TRACE_ERROR(x...)	dprintf("disk_device_manager: error: " x)


// directories for partitioning and file system modules
static const char* kPartitioningSystemPrefix = "partitioning_systems";
static const char* kFileSystemPrefix = "file_systems";


// singleton instance
KDiskDeviceManager* KDiskDeviceManager::sDefaultManager = NULL;


struct device_event {
	int32					opcode;
	const char*				name;
	dev_t					device;
	ino_t					directory;
	ino_t					node;
};


struct GetPartitionID {
	inline partition_id operator()(const KPartition* partition) const
	{
		return partition->ID();
	}
};


struct GetDiskSystemID {
	inline disk_system_id operator()(const KDiskSystem* system) const
	{
		return system->ID();
	}
};


struct KDiskDeviceManager::PartitionMap : VectorMap<partition_id, KPartition*,
	VectorMapEntryStrategy::ImplicitKey<partition_id, KPartition*,
		GetPartitionID> > {
};


struct KDiskDeviceManager::DeviceMap : VectorMap<partition_id, KDiskDevice*,
	VectorMapEntryStrategy::ImplicitKey<partition_id, KDiskDevice*,
		GetPartitionID> > {
};


struct KDiskDeviceManager::DiskSystemMap : VectorMap<disk_system_id,
	KDiskSystem*,
	VectorMapEntryStrategy::ImplicitKey<disk_system_id, KDiskSystem*,
		GetDiskSystemID> > {
};


struct KDiskDeviceManager::PartitionSet : VectorSet<KPartition*> {
};


class KDiskDeviceManager::DiskSystemWatcher : public NotificationListener {
public:
	DiskSystemWatcher(KDiskDeviceManager* manager)
		:
		fManager(manager)
	{
	}

	virtual ~DiskSystemWatcher()
	{
	}

	virtual void EventOccurred(NotificationService& service,
		const KMessage* event)
	{
		if (event->GetInt32("opcode", -1) != B_ENTRY_REMOVED)
			fManager->RescanDiskSystems();
	}

private:
	KDiskDeviceManager* fManager;
};


class KDiskDeviceManager::DeviceWatcher : public NotificationListener {
public:
	DeviceWatcher()
	{
	}

	virtual ~DeviceWatcher()
	{
	}

	virtual void EventOccurred(NotificationService& service,
		const KMessage* event)
	{
		int32 opcode = event->GetInt32("opcode", -1);
		switch (opcode) {
			case B_ENTRY_CREATED:
			case B_ENTRY_REMOVED:
			{
				device_event* deviceEvent = new(std::nothrow) device_event;
				if (deviceEvent == NULL)
					break;

				const char* name = event->GetString("name", NULL);
				if (name != NULL)
					deviceEvent->name = strdup(name);
				else
					deviceEvent->name = NULL;

				deviceEvent->opcode = opcode;
				deviceEvent->device = event->GetInt32("device", -1);
				deviceEvent->directory = event->GetInt64("directory", -1);
				deviceEvent->node = event->GetInt64("node", -1);

				struct stat stat;
				if (vfs_stat_node_ref(deviceEvent->device,  deviceEvent->node,
						&stat) != 0) {
					delete deviceEvent;
					break;
				}
				if (S_ISDIR(stat.st_mode)) {
					if (opcode == B_ENTRY_CREATED) {
						add_node_listener(deviceEvent->device,
							deviceEvent->node, B_WATCH_DIRECTORY, *this);
					} else {
						remove_node_listener(deviceEvent->device,
							deviceEvent->node, *this);
					}
					delete deviceEvent;
					break;
				}

				// TODO: a real in-kernel DPC mechanism would be preferred...
				thread_id thread = spawn_kernel_thread(_HandleDeviceEvent,
					"device event", B_NORMAL_PRIORITY, deviceEvent);
				if (thread < 0)
					delete deviceEvent;
				else
					resume_thread(thread);
				break;
			}

			default:
				break;
		}
	}

	static status_t _HandleDeviceEvent(void* _event)
	{
		device_event* event = (device_event*)_event;

		if (strcmp(event->name, "raw") == 0) {
			// a new raw device was added/removed
			KPath path;
			if (path.InitCheck() != B_OK
				|| vfs_entry_ref_to_path(event->device, event->directory,
					event->name, true, path.LockBuffer(),
					path.BufferSize()) != B_OK) {
				delete event;
				return B_ERROR;
			}

			path.UnlockBuffer();
			if (event->opcode == B_ENTRY_CREATED)
				KDiskDeviceManager::Default()->CreateDevice(path.Path());
			else
				KDiskDeviceManager::Default()->DeleteDevice(path.Path());
		}

		delete event;
		return B_OK;
	}
};


class KDiskDeviceManager::DiskNotifications
	: public DefaultUserNotificationService {
public:
	DiskNotifications()
		: DefaultUserNotificationService("disk devices")
	{
	}

	virtual ~DiskNotifications()
	{
	}
};


//	#pragma mark -


/**
 * @brief Construct the KDiskDeviceManager and perform initial disk system scan.
 *
 * Initialises the internal device, partition, disk-system and obsolete-partition
 * containers, registers the notification service, rescans all known disk
 * systems, and spawns the background media-checker daemon thread.
 *
 * @note Not thread-safe with respect to CreateDefault()/DeleteDefault().
 *       Call only once, via CreateDefault().
 */
KDiskDeviceManager::KDiskDeviceManager()
	:
	fDevices(new(nothrow) DeviceMap),
	fPartitions(new(nothrow) PartitionMap),
	fDiskSystems(new(nothrow) DiskSystemMap),
	fObsoletePartitions(new(nothrow) PartitionSet),
	fMediaChecker(-1),
	fTerminating(false),
	fDiskSystemWatcher(NULL),
	fDeviceWatcher(new(nothrow) DeviceWatcher()),
	fNotifications(new(nothrow) DiskNotifications)
{
	recursive_lock_init(&fLock, "disk device manager");

	if (InitCheck() != B_OK)
		return;

	fNotifications->Register();

	RescanDiskSystems();

	fMediaChecker = spawn_kernel_thread(_CheckMediaStatusDaemon,
		"media checker", B_NORMAL_PRIORITY, this);
	if (fMediaChecker >= 0)
		resume_thread(fMediaChecker);

	TRACE("number of disk systems: %" B_PRId32 "\n", CountDiskSystems());
	// TODO: Watch the disk systems and the relevant directories.
}


/**
 * @brief Destroy the KDiskDeviceManager and release all held resources.
 *
 * Signals the media-checker daemon to stop, removes all node monitors,
 * unregisters and deletes every known device, warns about any lingering
 * partitions, unloads all disk systems, and unregisters the notification
 * service before freeing the internal containers.
 *
 * @note Not thread-safe; call only via DeleteDefault().
 */
KDiskDeviceManager::~KDiskDeviceManager()
{
	fTerminating = true;

	status_t result;
	wait_for_thread(fMediaChecker, &result);

	// stop all node monitoring
	_AddRemoveMonitoring("/dev/disk", false);
	delete fDeviceWatcher;

	// remove all devices
	for (int32 cookie = 0; KDiskDevice* device = NextDevice(&cookie);) {
		PartitionRegistrar _(device);
		_RemoveDevice(device);
	}

	// some sanity checks
	if (fPartitions->Count() > 0) {
		TRACE_ALWAYS("WARNING: There are still %" B_PRId32 " unremoved partitions!\n",
			fPartitions->Count());
		for (PartitionMap::Iterator it = fPartitions->Begin();
				it != fPartitions->End(); ++it) {
			TRACE("         partition: %" B_PRId32 "\n", it->Value()->ID());
		}
	}
	if (fObsoletePartitions->Count() > 0) {
		TRACE_ALWAYS("WARNING: There are still %" B_PRId32 " obsolete partitions!\n",
				fObsoletePartitions->Count());
		for (PartitionSet::Iterator it = fObsoletePartitions->Begin();
				it != fObsoletePartitions->End(); ++it) {
			TRACE("         partition: %" B_PRId32 "\n", (*it)->ID());
		}
	}
	// remove all disk systems
	for (int32 cookie = 0; KDiskSystem* diskSystem = NextDiskSystem(&cookie);) {
		fDiskSystems->Remove(diskSystem->ID());
		if (diskSystem->IsLoaded()) {
			TRACE_ALWAYS("WARNING: Disk system `%s' (%" B_PRId32 ") is still loaded!\n",
				diskSystem->Name(), diskSystem->ID());
		} else
			delete diskSystem;
	}

	fNotifications->Unregister();

	// delete the containers
	delete fPartitions;
	delete fDevices;
	delete fDiskSystems;
	delete fObsoletePartitions;
}


/**
 * @brief Check whether the manager was constructed successfully.
 *
 * Verifies that all internal containers were allocated without error.
 *
 * @retval B_OK           All containers are valid and the manager is usable.
 * @retval B_NO_MEMORY    One or more containers failed to allocate.
 */
status_t
KDiskDeviceManager::InitCheck() const
{
	if (fPartitions == NULL || fDevices == NULL || fDiskSystems == NULL
		|| fObsoletePartitions == NULL || fNotifications == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


/*!	This creates the system's default DiskDeviceManager.
	The creation is not thread-safe, and shouldn't be done more than once.
*/
/**
 * @brief Create the process-wide singleton KDiskDeviceManager instance.
 *
 * Allocates and initialises sDefaultManager if it does not yet exist.
 * This function is not thread-safe and must be called exactly once during
 * kernel initialisation.
 *
 * @retval B_OK        The singleton was created (or already existed).
 * @retval B_NO_MEMORY Allocation of the manager object failed.
 * @retval other       InitCheck() of the new instance returned an error.
 */
status_t
KDiskDeviceManager::CreateDefault()
{
	if (sDefaultManager != NULL)
		return B_OK;

	sDefaultManager = new(nothrow) KDiskDeviceManager;
	if (sDefaultManager == NULL)
		return B_NO_MEMORY;

	return sDefaultManager->InitCheck();
}


/*!	This deletes the default DiskDeviceManager. The deletion is not
	thread-safe either, you should make sure that it's called only once.
*/
/**
 * @brief Destroy the process-wide singleton KDiskDeviceManager instance.
 *
 * Deletes sDefaultManager and resets the pointer to NULL.  Not thread-safe;
 * call only once during kernel shutdown after all disk I/O has ceased.
 */
void
KDiskDeviceManager::DeleteDefault()
{
	delete sDefaultManager;
	sDefaultManager = NULL;
}


/**
 * @brief Return a pointer to the singleton KDiskDeviceManager instance.
 *
 * @return Pointer to the default manager, or NULL if CreateDefault() has not
 *         been called yet.
 */
KDiskDeviceManager*
KDiskDeviceManager::Default()
{
	return sDefaultManager;
}


/**
 * @brief Acquire the manager's recursive write lock.
 *
 * Wraps recursive_lock_lock().  The same thread may call Lock() multiple
 * times; each call must be balanced by a corresponding Unlock().
 *
 * @retval true  The lock was acquired successfully.
 * @retval false The lock could not be acquired (system error).
 */
bool
KDiskDeviceManager::Lock()
{
	return recursive_lock_lock(&fLock) == B_OK;
}


/**
 * @brief Release one level of the manager's recursive write lock.
 *
 * Must be called once for every successful call to Lock() made by the
 * current thread.
 */
void
KDiskDeviceManager::Unlock()
{
	recursive_lock_unlock(&fLock);
}


/**
 * @brief Return the user-space notification service for disk device events.
 *
 * @return Reference to the DiskNotifications service used to broadcast
 *         B_DEVICE_UPDATE messages to registered listeners.
 */
DefaultUserNotificationService&
KDiskDeviceManager::Notifications()
{
	return *fNotifications;
}


/**
 * @brief Broadcast a disk-device event to all registered notification listeners.
 *
 * @param event     The KMessage describing the event (must contain at minimum
 *                  the "event" and "id" fields).
 * @param eventMask Bitmask of B_DEVICE_REQUEST_* flags that filters which
 *                  listeners receive the notification.
 */
void
KDiskDeviceManager::Notify(const KMessage& event, uint32 eventMask)
{
	fNotifications->Notify(event, eventMask);
}


/**
 * @brief Find a registered KDiskDevice by its device-node path.
 *
 * Iterates over all known devices and returns the first one whose path
 * matches @p path exactly.  The manager must be locked by the caller.
 *
 * @param path  Absolute path to the raw device node (e.g. "/dev/disk/ata/0/master/raw").
 * @return      Pointer to the matching KDiskDevice, or NULL if not found.
 */
KDiskDevice*
KDiskDeviceManager::FindDevice(const char* path)
{
	for (int32 cookie = 0; KDiskDevice* device = NextDevice(&cookie); ) {
		if (device->Path() && !strcmp(path, device->Path()))
			return device;
	}
	return NULL;
}


/**
 * @brief Find a registered KDiskDevice by partition or device ID.
 *
 * Looks up the partition with the given @p id and returns its owning device.
 * If @p deviceOnly is true the returned device's own ID must equal @p id,
 * i.e. the id must refer to the device partition itself, not a child.
 * The manager must be locked by the caller.
 *
 * @param id         Partition or device ID to look up.
 * @param deviceOnly If true, only succeed when @p id equals the device ID.
 * @return           Pointer to the owning KDiskDevice, or NULL if not found.
 */
KDiskDevice*
KDiskDeviceManager::FindDevice(partition_id id, bool deviceOnly)
{
	if (KPartition* partition = FindPartition(id)) {
		KDiskDevice* device = partition->Device();
		if (!deviceOnly || id == device->ID())
			return device;
	}
	return NULL;
}


/**
 * @brief Find a registered KPartition by its device-node path.
 *
 * Iterates every known partition and compares its resolved path against
 * @p path.  The manager must be locked by the caller.
 *
 * @param path  Absolute path to the partition node.
 * @return      Pointer to the matching KPartition, or NULL if not found.
 *
 * @note This is an O(n) linear scan; optimisation may be needed for systems
 *       with very large partition counts.
 */
KPartition*
KDiskDeviceManager::FindPartition(const char* path)
{
	// TODO: Optimize!
	KPath partitionPath;
	if (partitionPath.InitCheck() != B_OK)
		return NULL;

	for (PartitionMap::Iterator iterator = fPartitions->Begin();
			iterator != fPartitions->End(); ++iterator) {
		KPartition* partition = iterator->Value();
		if (partition->GetPath(&partitionPath) == B_OK
			&& partitionPath == path) {
			return partition;
		}
	}

	return NULL;
}


/**
 * @brief Find a registered KPartition by its numeric partition ID.
 *
 * Performs a direct map lookup.  The manager must be locked by the caller.
 *
 * @param id  The partition_id to look up.
 * @return    Pointer to the matching KPartition, or NULL if not found.
 */
KPartition*
KDiskDeviceManager::FindPartition(partition_id id)
{
	PartitionMap::Iterator iterator = fPartitions->Find(id);
	if (iterator != fPartitions->End())
		return iterator->Value();

	return NULL;
}


/**
 * @brief Find a registered KFileDiskDevice by the path of its backing file.
 *
 * Iterates all known devices, dynamic-casts each to KFileDiskDevice, and
 * compares the file path.  The manager must be locked by the caller.
 *
 * @param filePath  Absolute path of the image file backing the device.
 * @return          Pointer to the matching KFileDiskDevice, or NULL if not found.
 */
KFileDiskDevice*
KDiskDeviceManager::FindFileDevice(const char* filePath)
{
	for (int32 cookie = 0; KDiskDevice* device = NextDevice(&cookie); ) {
		KFileDiskDevice* fileDevice = dynamic_cast<KFileDiskDevice*>(device);
		if (fileDevice && fileDevice->FilePath()
			&& !strcmp(filePath, fileDevice->FilePath())) {
			return fileDevice;
		}
	}
	return NULL;
}


/**
 * @brief Register and return a KDiskDevice for the given device path,
 *        creating the device entry if it does not yet exist.
 *
 * Looks up the device at @p path.  If not found and the path looks like a
 * raw disk node under /dev/disk, CreateDevice() is called once before
 * retrying.  On success the device's reference count is incremented.
 *
 * @param path  Absolute path to the raw device node.
 * @return      Registered KDiskDevice pointer, or NULL on failure.
 *
 * @note The caller is responsible for calling Unregister() on the returned
 *       device when done.
 */
KDiskDevice*
KDiskDeviceManager::RegisterDevice(const char* path)
{
	if (ManagerLocker locker = this) {
		for (int32 i = 0; i < 2; i++) {
			if (KDiskDevice* device = FindDevice(path)) {
				device->Register();
				return device;
			}

			// if the device is not known yet, create it and try again
			const char* leaf = strrchr(path, '/');
			if (i == 0 && !strncmp(path, "/dev/disk", 9)
				&& !strcmp(leaf + 1, "raw") && CreateDevice(path) < B_OK)
				break;
		}
	}
	return NULL;
}


/**
 * @brief Register and return a KDiskDevice by partition or device ID.
 *
 * Locates the device that owns the partition identified by @p id.  If
 * @p deviceOnly is true the id must refer to the device itself.  On success
 * the device's reference count is incremented.
 *
 * @param id         The partition_id (or device id) to look up.
 * @param deviceOnly If true, only return the device when @p id is the
 *                   device's own partition id.
 * @return           Registered KDiskDevice pointer, or NULL if not found.
 *
 * @note The caller is responsible for calling Unregister() on the returned
 *       device when done.
 */
KDiskDevice*
KDiskDeviceManager::RegisterDevice(partition_id id, bool deviceOnly)
{
	if (ManagerLocker locker = this) {
		if (KDiskDevice* device = FindDevice(id, deviceOnly)) {
			device->Register();
			return device;
		}
	}
	return NULL;
}


/**
 * @brief Register and return the next KDiskDevice in iteration order.
 *
 * Advances the iteration cookie @p cookie and registers the device at the
 * new position, incrementing its reference count.
 *
 * @param[in,out] cookie  Iteration cookie; initialise to 0 before the first call.
 * @return                Registered KDiskDevice pointer, or NULL when the
 *                        iteration is exhausted.
 *
 * @note The caller is responsible for calling Unregister() on each returned
 *       device.
 */
KDiskDevice*
KDiskDeviceManager::RegisterNextDevice(int32* cookie)
{
	if (!cookie)
		return NULL;

	if (ManagerLocker locker = this) {
		if (KDiskDevice* device = NextDevice(cookie)) {
			device->Register();
			return device;
		}
	}
	return NULL;
}


/**
 * @brief Register and return a KPartition by device-node path, creating the
 *        parent device if necessary.
 *
 * Mirrors the behaviour of RegisterDevice(const char*) but operates at the
 * partition level.  On success the partition's reference count is incremented.
 *
 * @param path  Absolute path to the partition node.
 * @return      Registered KPartition pointer, or NULL on failure.
 *
 * @note The caller is responsible for calling Unregister() on the returned
 *       partition when done.
 */
KPartition*
KDiskDeviceManager::RegisterPartition(const char* path)
{
	if (ManagerLocker locker = this) {
		for (int32 i = 0; i < 2; i++) {
			if (KPartition* partition = FindPartition(path)) {
				partition->Register();
				return partition;
			}

			// if the device is not known yet, create it and try again
			const char* leaf = strrchr(path, '/');
			if (i == 0 && !strncmp(path, "/dev/disk", 9)
				&& !strcmp(leaf + 1, "raw") && CreateDevice(path) < B_OK)
				break;
		}
	}
	return NULL;
}


/**
 * @brief Register and return a KPartition by its numeric partition ID.
 *
 * On success the partition's reference count is incremented.
 *
 * @param id  The partition_id to look up.
 * @return    Registered KPartition pointer, or NULL if not found.
 *
 * @note The caller is responsible for calling Unregister() on the returned
 *       partition when done.
 */
KPartition*
KDiskDeviceManager::RegisterPartition(partition_id id)
{
	if (ManagerLocker locker = this) {
		if (KPartition* partition = FindPartition(id)) {
			partition->Register();
			return partition;
		}
	}
	return NULL;
}


/**
 * @brief Register and return a KFileDiskDevice by the path of its backing file.
 *
 * On success the device's reference count is incremented.
 *
 * @param filePath  Absolute path of the image file backing the virtual device.
 * @return          Registered KFileDiskDevice pointer, or NULL if not found.
 *
 * @note The caller is responsible for calling Unregister() on the returned
 *       device when done.
 */
KFileDiskDevice*
KDiskDeviceManager::RegisterFileDevice(const char* filePath)
{
	if (ManagerLocker locker = this) {
		if (KFileDiskDevice* device = FindFileDevice(filePath)) {
			device->Register();
			return device;
		}
	}
	return NULL;
}


/**
 * @brief Register a device by ID and acquire a read lock on it.
 *
 * Calls RegisterDevice() and then attempts to read-lock the returned device.
 * On failure the device is unregistered before returning NULL.
 *
 * @param id         The partition_id (or device id) to look up.
 * @param deviceOnly If true, only match when @p id is the device's own id.
 * @return           Read-locked, registered KDiskDevice pointer, or NULL on
 *                   failure.
 *
 * @note The caller must call device->ReadUnlock() and device->Unregister()
 *       when done.
 */
KDiskDevice*
KDiskDeviceManager::ReadLockDevice(partition_id id, bool deviceOnly)
{
	// register device
	KDiskDevice* device = RegisterDevice(id, deviceOnly);
	if (!device)
		return NULL;
	// lock device
	if (device->ReadLock())
		return device;
	device->Unregister();
	return NULL;
}


/**
 * @brief Register a device by ID and acquire a write lock on it.
 *
 * Calls RegisterDevice() and then attempts to write-lock the returned device.
 * On failure the device is unregistered before returning NULL.
 *
 * @param id         The partition_id (or device id) to look up.
 * @param deviceOnly If true, only match when @p id is the device's own id.
 * @return           Write-locked, registered KDiskDevice pointer, or NULL on
 *                   failure.
 *
 * @note The caller must call device->WriteUnlock() and device->Unregister()
 *       when done.
 */
KDiskDevice*
KDiskDeviceManager::WriteLockDevice(partition_id id, bool deviceOnly)
{
	// register device
	KDiskDevice* device = RegisterDevice(id, deviceOnly);
	if (!device)
		return NULL;
	// lock device
	if (device->WriteLock())
		return device;
	device->Unregister();
	return NULL;
}


/**
 * @brief Register a partition by ID and acquire a read lock on its device.
 *
 * Registers the partition, then registers and read-locks the owning device.
 * After locking, verifies that the partition still belongs to the same device
 * to guard against races.  All resources are cleaned up on any failure.
 *
 * @param id  The partition_id to look up.
 * @return    Registered KPartition pointer with its device read-locked, or
 *            NULL on failure.
 *
 * @note The caller must call device->ReadUnlock(), device->Unregister(), and
 *       partition->Unregister() when done.
 */
KPartition*
KDiskDeviceManager::ReadLockPartition(partition_id id)
{
	// register partition
	KPartition* partition = RegisterPartition(id);
	if (!partition)
		return NULL;
	// get and register the device
	KDiskDevice* device = NULL;
	if (ManagerLocker locker = this) {
		device = partition->Device();
		if (device)
			device->Register();
	}
	// lock the device
	if (device && device->ReadLock()) {
		// final check, if the partition still belongs to the device
		if (partition->Device() == device)
			return partition;
		device->ReadUnlock();
	}
	// cleanup on failure
	if (device)
		device->Unregister();
	partition->Unregister();
	return NULL;
}


/**
 * @brief Register a partition by ID and acquire a write lock on its device.
 *
 * Registers the partition, then registers and write-locks the owning device.
 * After locking, verifies that the partition still belongs to the same device
 * to guard against races.  All resources are cleaned up on any failure.
 *
 * @param id  The partition_id to look up.
 * @return    Registered KPartition pointer with its device write-locked, or
 *            NULL on failure.
 *
 * @note The caller must call device->WriteUnlock(), device->Unregister(), and
 *       partition->Unregister() when done.
 */
KPartition*
KDiskDeviceManager::WriteLockPartition(partition_id id)
{
	// register partition
	KPartition* partition = RegisterPartition(id);
	if (!partition)
		return NULL;
	// get and register the device
	KDiskDevice* device = NULL;
	if (ManagerLocker locker = this) {
		device = partition->Device();
		if (device)
			device->Register();
	}
	// lock the device
	if (device && device->WriteLock()) {
		// final check, if the partition still belongs to the device
		if (partition->Device() == device)
			return partition;
		device->WriteUnlock();
	}
	// cleanup on failure
	if (device)
		device->Unregister();
	partition->Unregister();
	return NULL;
}


/**
 * @brief Scan a single partition synchronously for disk systems and child
 *        partitions.
 *
 * Write-locks the partition's device and then delegates to _ScanPartition().
 *
 * @param partition  The KPartition to scan; must not be NULL.
 * @retval B_OK      Scan completed successfully.
 * @retval B_ERROR   Could not acquire the device write lock or manager lock.
 *
 * @note Locking the DDM while scanning is not ideal; see the in-code TODO for
 *       a planned improvement.
 */
status_t
KDiskDeviceManager::ScanPartition(KPartition* partition)
{
// TODO: This won't do. Locking the DDM while scanning the partition is not a
// good idea. Even locking the device doesn't feel right. Marking the partition
// busy and passing the disk system a temporary clone of the partition_data
// should work as well.
	if (DeviceWriteLocker deviceLocker = partition->Device()) {
		if (ManagerLocker locker = this)
			return _ScanPartition(partition, false);
	}

	return B_ERROR;
}


/**
 * @brief Create and register a KDiskDevice for the given device path.
 *
 * If a device at @p path is already known its ID is returned immediately and
 * @p newlyCreated is set to false.  Otherwise a new KDiskDevice is allocated,
 * initialised, added to the manager, synchronously scanned for partitions, and
 * a B_DEVICE_ADDED notification is sent.
 *
 * @param path          Absolute path to the raw device node.
 * @param[out] newlyCreated  Set to true when a new device was created, false
 *                           when an existing device was found.  May be NULL.
 * @return              The partition_id of the device (>= 0) on success,
 *                      or a negative error code on failure.
 *
 * @retval B_BAD_VALUE  @p path was NULL.
 * @retval B_NO_MEMORY  Device allocation or insertion failed.
 * @retval B_ERROR      Could not acquire the manager lock.
 */
partition_id
KDiskDeviceManager::CreateDevice(const char* path, bool* newlyCreated)
{
	if (!path)
		return B_BAD_VALUE;

	status_t error = B_ERROR;
	if (ManagerLocker locker = this) {
		KDiskDevice* device = FindDevice(path);
		if (device != NULL) {
			// we already know this device
			if (newlyCreated)
				*newlyCreated = false;

			return device->ID();
		}

		// create a KDiskDevice for it
		device = new(nothrow) KDiskDevice;
		if (!device)
			return B_NO_MEMORY;

		// initialize and add the device
		error = device->SetTo(path);

		// Note: Here we are allowed to lock a device although already having
		// the manager locked, since it is not yet added to the manager.
		DeviceWriteLocker deviceLocker(device);
		if (error == B_OK && !deviceLocker.IsLocked())
			error = B_ERROR;
		if (error == B_OK && !_AddDevice(device))
			error = B_NO_MEMORY;

		// cleanup on error
		if (error != B_OK) {
			deviceLocker.Unlock();
			delete device;
			return error;
		}

		// scan for partitions
		_ScanPartition(device, false);
		device->UnmarkBusy(true);

		_NotifyDeviceEvent(device, B_DEVICE_ADDED,
			B_DEVICE_REQUEST_DEVICE_LIST);

		if (newlyCreated)
			*newlyCreated = true;

		return device->ID();
	}

	return error;
}


/**
 * @brief Remove and destroy the KDiskDevice registered at the given path.
 *
 * Finds the device by @p path, write-locks it, removes it from the manager,
 * and sends a B_DEVICE_REMOVED notification.
 *
 * @param path  Absolute path to the raw device node.
 * @retval B_OK             The device was removed successfully.
 * @retval B_ENTRY_NOT_FOUND No device is registered at @p path.
 * @retval B_ERROR          Could not acquire the device write lock.
 */
status_t
KDiskDeviceManager::DeleteDevice(const char* path)
{
	KDiskDevice* device = FindDevice(path);
	if (device == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar _(device, false);
	if (DeviceWriteLocker locker = device) {
		if (_RemoveDevice(device))
			return B_OK;
	}

	return B_ERROR;
}


/**
 * @brief Create and register a file-backed virtual disk device.
 *
 * Normalises @p filePath, checks whether a KFileDiskDevice for that file
 * already exists (returning its ID without creating a duplicate), then
 * allocates, initialises, and scans a new KFileDiskDevice.  On success a
 * B_DEVICE_ADDED notification is sent.
 *
 * @param filePath       Absolute path to the disk image file.
 * @param[out] newlyCreated  Set to true when a new device was created, false
 *                           when an existing device was found.  May be NULL.
 * @return               The partition_id of the device (>= 0) on success,
 *                       or a negative error code on failure.
 *
 * @retval B_BAD_VALUE   @p filePath was NULL.
 * @retval B_NO_MEMORY   Device allocation or insertion failed.
 * @retval B_ERROR       Could not acquire the manager lock.
 */
partition_id
KDiskDeviceManager::CreateFileDevice(const char* filePath, bool* newlyCreated)
{
	if (!filePath)
		return B_BAD_VALUE;

	// normalize the file path
	KPath normalizedFilePath;
	status_t error = normalizedFilePath.SetTo(filePath, KPath::NORMALIZE);
	if (error != B_OK)
		return error;
	filePath = normalizedFilePath.Path();

	KFileDiskDevice* device = NULL;
	if (ManagerLocker locker = this) {
		// check, if the device does already exist
		if ((device = FindFileDevice(filePath))) {
			if (newlyCreated)
				*newlyCreated = false;

			return device->ID();
		}

		// allocate a KFileDiskDevice
		device = new(nothrow) KFileDiskDevice;
		if (!device)
			return B_NO_MEMORY;

		// initialize and add the device
		error = device->SetTo(filePath);

		// Note: Here we are allowed to lock a device although already having
		// the manager locked, since it is not yet added to the manager.
		DeviceWriteLocker deviceLocker(device);
		if (error == B_OK && !deviceLocker.IsLocked())
			error = B_ERROR;
		if (error == B_OK && !_AddDevice(device))
			error = B_NO_MEMORY;

		// scan device
		if (error == B_OK) {
			_ScanPartition(device, false);
			device->UnmarkBusy(true);

			_NotifyDeviceEvent(device, B_DEVICE_ADDED,
				B_DEVICE_REQUEST_DEVICE_LIST);

			if (newlyCreated)
				*newlyCreated = true;

			return device->ID();
		}

		// cleanup on failure
		deviceLocker.Unlock();
		delete device;
	} else
		error = B_ERROR;
	return error;
}


/**
 * @brief Remove and destroy the file-backed virtual device at the given path.
 *
 * Finds the KFileDiskDevice by @p filePath, registers it, write-locks it,
 * and removes it from the manager.
 *
 * @param filePath  Absolute path to the disk image file.
 * @retval B_OK     The device was removed successfully.
 * @retval B_ERROR  Device not found, lock not acquired, or removal failed.
 */
status_t
KDiskDeviceManager::DeleteFileDevice(const char* filePath)
{
	if (KFileDiskDevice* device = RegisterFileDevice(filePath)) {
		PartitionRegistrar _(device, true);
		if (DeviceWriteLocker locker = device) {
			if (_RemoveDevice(device))
				return B_OK;
		}
	}
	return B_ERROR;
}


/**
 * @brief Remove and destroy the file-backed virtual device with the given ID.
 *
 * Registers the device with @p id, confirms it is a KFileDiskDevice whose own
 * partition id matches @p id, write-locks it, and removes it.
 *
 * @param id  The partition_id of the file-backed device to remove.
 * @retval B_OK             Removal succeeded.
 * @retval B_ENTRY_NOT_FOUND The device is not a KFileDiskDevice or the id
 *                          belongs to a child partition, not the device itself.
 * @retval B_ERROR          Lock not acquired or removal failed.
 */
status_t
KDiskDeviceManager::DeleteFileDevice(partition_id id)
{
	if (KDiskDevice* device = RegisterDevice(id)) {
		PartitionRegistrar _(device, true);
		if (!dynamic_cast<KFileDiskDevice*>(device) || id != device->ID())
			return B_ENTRY_NOT_FOUND;
		if (DeviceWriteLocker locker = device) {
			if (_RemoveDevice(device))
				return B_OK;
		}
	}
	return B_ERROR;
}


/**
 * @brief Return the number of KDiskDevice objects currently registered.
 *
 * The manager must be locked by the caller.
 *
 * @return The count of registered devices.
 */
int32
KDiskDeviceManager::CountDevices()
{
	return fDevices->Count();
}


/**
 * @brief Return the next KDiskDevice in the iteration sequence.
 *
 * Finds the device whose ID is greater than or equal to @p *cookie and
 * advances the cookie past it.  The manager must be locked by the caller.
 *
 * @param[in,out] cookie  Iteration state; initialise to 0 before the first call.
 * @return                Pointer to the next KDiskDevice, or NULL when exhausted.
 */
KDiskDevice*
KDiskDeviceManager::NextDevice(int32* cookie)
{
	if (!cookie)
		return NULL;

	DeviceMap::Iterator it = fDevices->FindClose(*cookie, false);
	if (it != fDevices->End()) {
		KDiskDevice* device = it->Value();
		*cookie = device->ID() + 1;
		return device;
	}
	return NULL;
}


/**
 * @brief Record that a new KPartition has been added to the manager's registry.
 *
 * Inserts @p partition into the global partition map keyed by its ID.
 * Called by KDiskDevice when a new partition object is constructed.
 *
 * @param partition  The newly created KPartition; must not be NULL.
 * @retval true   Insertion succeeded.
 * @retval false  @p partition was NULL or the map insertion failed.
 */
bool
KDiskDeviceManager::PartitionAdded(KPartition* partition)
{
	return partition && fPartitions->Put(partition->ID(), partition) == B_OK;
}


/**
 * @brief Mark a KPartition as removed and move it to the obsolete set.
 *
 * Prepares @p partition for removal, removes it from the active partition map,
 * inserts it into fObsoletePartitions, and marks it obsolete.  The partition
 * is not deleted here; call DeletePartition() once all references are released.
 *
 * @param partition  The KPartition to remove; must not be NULL.
 * @retval true   The partition was successfully moved to the obsolete set.
 * @retval false  @p partition was NULL, PrepareForRemoval() failed, or the
 *                map removal failed.
 */
bool
KDiskDeviceManager::PartitionRemoved(KPartition* partition)
{
	if (partition && partition->PrepareForRemoval()
		&& fPartitions->Remove(partition->ID())) {
		// TODO: If adding the partition to the obsolete list fails (due to lack
		// of memory), we can't do anything about it. We will leak memory then.
		fObsoletePartitions->Insert(partition);
		partition->MarkObsolete();
		return true;
	}
	return false;
}


/**
 * @brief Delete an obsolete KPartition once all references to it are gone.
 *
 * Verifies that @p partition is obsolete, has no outstanding references, and
 * passes PrepareForDeletion(), then removes it from fObsoletePartitions and
 * frees it.
 *
 * @param partition  The KPartition to delete; must not be NULL.
 * @retval true   The partition was deleted.
 * @retval false  Preconditions were not met; the partition was not deleted.
 */
bool
KDiskDeviceManager::DeletePartition(KPartition* partition)
{
	if (partition && partition->IsObsolete()
		&& partition->CountReferences() == 0
		&& partition->PrepareForDeletion()
		&& fObsoletePartitions->Remove(partition)) {
		delete partition;
		return true;
	}
	return false;
}


/**
 * @brief Find a registered KDiskSystem by name or pretty name.
 *
 * Iterates all known disk systems comparing their name (or pretty name when
 * @p byPrettyName is true) against @p name.  The manager must be locked.
 *
 * @param name          The module name or pretty name to search for.
 * @param byPrettyName  If true, compare against KDiskSystem::PrettyName()
 *                      instead of KDiskSystem::Name().
 * @return              Pointer to the matching KDiskSystem, or NULL if not found.
 */
KDiskSystem*
KDiskDeviceManager::FindDiskSystem(const char* name, bool byPrettyName)
{
	for (int32 cookie = 0; KDiskSystem* diskSystem = NextDiskSystem(&cookie);) {
		if (byPrettyName) {
			if (strcmp(name, diskSystem->PrettyName()) == 0)
				return diskSystem;
		} else {
			if (strcmp(name, diskSystem->Name()) == 0)
				return diskSystem;
		}
	}
	return NULL;
}


/**
 * @brief Find a registered KDiskSystem by its numeric disk-system ID.
 *
 * Performs a direct map lookup.  The manager must be locked by the caller.
 *
 * @param id  The disk_system_id to look up.
 * @return    Pointer to the matching KDiskSystem, or NULL if not found.
 */
KDiskSystem*
KDiskDeviceManager::FindDiskSystem(disk_system_id id)
{
	DiskSystemMap::Iterator it = fDiskSystems->Find(id);
	if (it != fDiskSystems->End())
		return it->Value();
	return NULL;
}


/**
 * @brief Return the number of KDiskSystem objects currently registered.
 *
 * The manager must be locked by the caller.
 *
 * @return The count of registered disk systems.
 */
int32
KDiskDeviceManager::CountDiskSystems()
{
	return fDiskSystems->Count();
}


/**
 * @brief Return the next KDiskSystem in the iteration sequence.
 *
 * Finds the disk system whose ID is greater than or equal to @p *cookie and
 * advances the cookie past it.  The manager must be locked by the caller.
 *
 * @param[in,out] cookie  Iteration state; initialise to 0 before the first call.
 * @return                Pointer to the next KDiskSystem, or NULL when exhausted.
 */
KDiskSystem*
KDiskDeviceManager::NextDiskSystem(int32* cookie)
{
	if (!cookie)
		return NULL;

	DiskSystemMap::Iterator it = fDiskSystems->FindClose(*cookie, false);
	if (it != fDiskSystems->End()) {
		KDiskSystem* diskSystem = it->Value();
		*cookie = diskSystem->ID() + 1;
		return diskSystem;
	}
	return NULL;
}


/**
 * @brief Find a disk system by name and increment its load count.
 *
 * Looks up the disk system and calls Load() on it.  Returns NULL if the
 * system is not found or Load() fails.  The manager is locked internally.
 *
 * @param name          Module or pretty name of the disk system.
 * @param byPrettyName  If true, match against the pretty name.
 * @return              Loaded KDiskSystem pointer, or NULL on failure.
 *
 * @note The caller must call Unload() on the returned system when done.
 */
KDiskSystem*
KDiskDeviceManager::LoadDiskSystem(const char* name, bool byPrettyName)
{
	KDiskSystem* diskSystem = NULL;
	if (ManagerLocker locker = this) {
		diskSystem = FindDiskSystem(name, byPrettyName);
		if (diskSystem && diskSystem->Load() != B_OK)
			diskSystem = NULL;
	}
	return diskSystem;
}


/**
 * @brief Find a disk system by numeric ID and increment its load count.
 *
 * Looks up the disk system and calls Load() on it.  Returns NULL if the
 * system is not found or Load() fails.  The manager is locked internally.
 *
 * @param id  The disk_system_id to load.
 * @return    Loaded KDiskSystem pointer, or NULL on failure.
 *
 * @note The caller must call Unload() on the returned system when done.
 */
KDiskSystem*
KDiskDeviceManager::LoadDiskSystem(disk_system_id id)
{
	KDiskSystem* diskSystem = NULL;
	if (ManagerLocker locker = this) {
		diskSystem = FindDiskSystem(id);
		if (diskSystem && diskSystem->Load() != B_OK)
			diskSystem = NULL;
	}
	return diskSystem;
}


/**
 * @brief Iterate to the next disk system and increment its load count.
 *
 * Advances @p cookie and loads the disk system at the new position.
 * Returns NULL if the iteration is exhausted or Load() fails.
 *
 * @param[in,out] cookie  Iteration state; initialise to 0 before the first call.
 * @return                Loaded KDiskSystem pointer, or NULL when exhausted or
 *                        on load failure.
 *
 * @note The caller must call Unload() on each returned system when done.
 */
KDiskSystem*
KDiskDeviceManager::LoadNextDiskSystem(int32* cookie)
{
	if (!cookie)
		return NULL;

	if (ManagerLocker locker = this) {
		if (KDiskSystem* diskSystem = NextDiskSystem(cookie)) {
			if (diskSystem->Load() == B_OK) {
				*cookie = diskSystem->ID() + 1;
				return diskSystem;
			}
		}
	}
	return NULL;
}


/**
 * @brief Perform the boot-time device and partition scan.
 *
 * First scans the /dev/disk tree to discover all raw device nodes and register
 * the corresponding KDiskDevice objects.  Then iterates over every registered
 * device, write-locks it, and calls _ScanPartition() to identify partition
 * tables and file systems.  Scanning continues even if individual devices fail
 * so that a single bad device does not block the rest.
 *
 * @retval B_OK     All devices were scanned (individual partition errors are
 *                  recorded but do not prevent B_OK from being returned if at
 *                  least one device succeeded).
 * @retval B_ERROR  The initial /dev/disk scan or a critical locking step failed.
 * @retval other    The status code of the last partition scan error encountered.
 */
status_t
KDiskDeviceManager::InitialDeviceScan()
{
	// scan for devices
	if (ManagerLocker locker = this) {
		status_t error = _Scan("/dev/disk");
		if (error != B_OK)
			return error;
	}

	// scan the devices for partitions
	int32 cookie = 0;
	status_t status = B_OK;
	while (KDiskDevice* device = RegisterNextDevice(&cookie)) {
		PartitionRegistrar _(device, true);
		if (DeviceWriteLocker deviceLocker = device) {
			if (ManagerLocker locker = this) {
				status_t error = _ScanPartition(device, false);
				device->UnmarkBusy(true);
				if (error != B_OK)
					status = error;
				// Even if we could not scan this partition, we want to try
				// and scan the rest. Just because one partition is invalid
				// or unscannable does not mean the ones after it are.
			} else
				return B_ERROR;
		} else
			return B_ERROR;
	}
	return status;
}


/**
 * @brief Start device monitoring and trigger a second device scan.
 *
 * Calls InitialDeviceScan() to ensure devfs is fully populated, installs
 * the DiskSystemWatcher for module events on the file-system and
 * partitioning-system prefixes, and enables node monitoring on the entire
 * /dev/disk hierarchy via _AddRemoveMonitoring().
 *
 * @retval B_OK   Monitoring was started successfully.
 * @retval other  Return value of _AddRemoveMonitoring() on failure.
 */
status_t
KDiskDeviceManager::StartMonitoring()
{
	// do another scan, this will populate the devfs directories
	InitialDeviceScan();

	// start monitoring the disk systems
	fDiskSystemWatcher = new(std::nothrow) DiskSystemWatcher(this);
	if (fDiskSystemWatcher != NULL) {
		start_watching_modules(kFileSystemPrefix, *fDiskSystemWatcher);
		start_watching_modules(kPartitioningSystemPrefix,
			*fDiskSystemWatcher);
	}

	// start monitoring all dirs under /dev/disk
	return _AddRemoveMonitoring("/dev/disk", true);
}


/**
 * @brief Enumerate and register all disk systems of one type (file systems or
 *        partitioning systems) that are not yet known to the manager.
 *
 * Opens the module list for the appropriate prefix, iterates every module
 * name, skips those already registered, creates the corresponding KDiskSystem
 * subclass, and records newly added systems in @p addedSystems.
 *
 * @param[out] addedSystems  Map populated with every newly added KDiskSystem.
 * @param fileSystems        If true, enumerate file-system modules; otherwise
 *                           enumerate partitioning-system modules.
 * @retval B_OK        Enumeration completed (some systems may have failed to
 *                     initialise individually).
 * @retval B_NO_MEMORY Could not open the module list.
 */
status_t
KDiskDeviceManager::_RescanDiskSystems(DiskSystemMap& addedSystems,
	bool fileSystems)
{
	void* cookie = open_module_list(fileSystems
		? kFileSystemPrefix : kPartitioningSystemPrefix);
	if (cookie == NULL)
		return B_NO_MEMORY;

	while (true) {
		KPath name;
		if (name.InitCheck() != B_OK)
			break;
		size_t nameLength = name.BufferSize();
		if (read_next_module_name(cookie, name.LockBuffer(),
				&nameLength) != B_OK) {
			break;
		}
		name.UnlockBuffer();

		if (FindDiskSystem(name.Path()))
			continue;

		if (fileSystems) {
			TRACE("file system: %s\n", name.Path());
			_AddFileSystem(name.Path());
		} else {
			TRACE("partitioning system: %s\n", name.Path());
			_AddPartitioningSystem(name.Path());
		}

		if (KDiskSystem* system = FindDiskSystem(name.Path()))
			addedSystems.Put(system->ID(), system);
	}

	close_module_list(cookie);
	return B_OK;
}


/*!	Rescan the existing disk systems. This is called after the boot device
	has become available.
*/
/**
 * @brief Rescan all partitioning and file-system disk systems and re-scan
 *        existing devices with any newly discovered systems.
 *
 * Called after the boot device becomes available so that add-ons loaded from
 * the boot volume are picked up.  Locks the manager, rescans both module
 * prefixes, then iterates all registered devices and runs _ScanPartition()
 * restricted to the newly added systems.
 *
 * @retval B_OK    Rescan completed (individual errors are accumulated but
 *                 B_OK is returned unless a critical lock step fails).
 * @retval B_ERROR A required lock could not be acquired.
 * @retval other   Status of the last failing _ScanPartition() call.
 */
status_t
KDiskDeviceManager::RescanDiskSystems()
{
	DiskSystemMap addedSystems;

	Lock();

	// rescan for partitioning and file systems
	_RescanDiskSystems(addedSystems, false);
	_RescanDiskSystems(addedSystems, true);

	Unlock();

	// rescan existing devices with the new disk systems
	int32 cookie = 0;
	status_t status = B_OK;
	while (KDiskDevice* device = RegisterNextDevice(&cookie)) {
		PartitionRegistrar _(device, true);
		if (DeviceWriteLocker deviceLocker = device) {
			if (ManagerLocker locker = this) {
				status_t error = _ScanPartition(device, false, &addedSystems);
				device->UnmarkBusy(true);
				if (error != B_OK)
					status = error;
				// See comment in InitialDeviceScan().
			} else
				return B_ERROR;
		} else
			return B_ERROR;
	}

	return status;
}


/**
 * @brief Allocate and register a new KPartitioningSystem add-on by module name.
 *
 * @param name  Module name of the partitioning system (e.g. "partitioning_systems/intel").
 * @retval B_OK        The partitioning system was created and added.
 * @retval B_BAD_VALUE @p name was NULL.
 * @retval B_NO_MEMORY Allocation of the KPartitioningSystem failed.
 * @retval other       _AddDiskSystem() initialisation error.
 */
status_t
KDiskDeviceManager::_AddPartitioningSystem(const char* name)
{
	if (!name)
		return B_BAD_VALUE;

	KDiskSystem* diskSystem = new(nothrow) KPartitioningSystem(name);
	if (!diskSystem)
		return B_NO_MEMORY;
	return _AddDiskSystem(diskSystem);
}


/**
 * @brief Allocate and register a new KFileSystem add-on by module name.
 *
 * @param name  Module name of the file system (e.g. "file_systems/bfs").
 * @retval B_OK        The file system was created and added.
 * @retval B_BAD_VALUE @p name was NULL.
 * @retval B_NO_MEMORY Allocation of the KFileSystem failed.
 * @retval other       _AddDiskSystem() initialisation error.
 */
status_t
KDiskDeviceManager::_AddFileSystem(const char* name)
{
	if (!name)
		return B_BAD_VALUE;

	KDiskSystem* diskSystem = new(nothrow) KFileSystem(name);
	if (!diskSystem)
		return B_NO_MEMORY;

	return _AddDiskSystem(diskSystem);
}


/**
 * @brief Initialise and insert a KDiskSystem into the disk-system registry.
 *
 * Calls Init() on @p diskSystem and, if successful, inserts it into
 * fDiskSystems.  Deletes the object on any error.
 *
 * @param diskSystem  Heap-allocated KDiskSystem to register; ownership is
 *                    transferred on success, or the object is deleted on failure.
 * @retval B_OK        The disk system was initialised and registered.
 * @retval B_BAD_VALUE @p diskSystem was NULL.
 * @retval other       Init() or map insertion error; @p diskSystem was deleted.
 */
status_t
KDiskDeviceManager::_AddDiskSystem(KDiskSystem* diskSystem)
{
	if (!diskSystem)
		return B_BAD_VALUE;
	TRACE("KDiskDeviceManager::_AddDiskSystem(%s)\n", diskSystem->Name());
	status_t error = diskSystem->Init();
	if (error != B_OK) {
		TRACE("  initialization failed: %s\n", strerror(error));
	}
	if (error == B_OK)
		error = fDiskSystems->Put(diskSystem->ID(), diskSystem);
	if (error != B_OK)
		delete diskSystem;
	TRACE("KDiskDeviceManager::_AddDiskSystem() done: %s\n", strerror(error));
	return error;
}


/**
 * @brief Add a KDiskDevice to the manager's device and partition maps.
 *
 * Calls PartitionAdded() for the device (which registers it as a partition)
 * and then inserts it into fDevices.  Rolls back the PartitionAdded() call
 * if the device map insertion fails.
 *
 * @param device  The KDiskDevice to add; must not be NULL.
 * @retval true   The device was added successfully.
 * @retval false  @p device was NULL or a map insertion failed.
 */
bool
KDiskDeviceManager::_AddDevice(KDiskDevice* device)
{
	if (!device || !PartitionAdded(device))
		return false;
	if (fDevices->Put(device->ID(), device) == B_OK)
		return true;
	PartitionRemoved(device);
	return false;
}


/**
 * @brief Remove a KDiskDevice from the manager and send a removal notification.
 *
 * Removes the device from fDevices, calls PartitionRemoved() to move it to the
 * obsolete set, and sends a B_DEVICE_REMOVED event via _NotifyDeviceEvent().
 *
 * @param device  The KDiskDevice to remove; must not be NULL.
 * @retval true   The device was removed and the notification sent.
 * @retval false  @p device was NULL or removal from the maps failed.
 */
bool
KDiskDeviceManager::_RemoveDevice(KDiskDevice* device)
{
	if (device != NULL && fDevices->Remove(device->ID())
		&& PartitionRemoved(device)) {
		_NotifyDeviceEvent(device, B_DEVICE_REMOVED,
			B_DEVICE_REQUEST_DEVICE_LIST);
		return true;
	}

	return false;
}


#if 0
/*!
	The device must be write locked, the manager must be locked.
*/
/**
 * @brief Recompute the busy/descendant-busy flags for all partitions on a device.
 *
 * Clears all busy flags, then re-derives them from the set of in-progress or
 * scheduled jobs in every job queue associated with @p device.  Finally
 * propagates busy flags downward (parent busy implies child busy) and upward
 * (child busy implies parent descendant-busy).
 *
 * @param device  The KDiskDevice whose partition tree should be updated;
 *                must be write-locked and must not be NULL.
 * @retval B_OK        Update completed.
 * @retval B_BAD_VALUE @p device was NULL.
 *
 * @note This function is currently disabled (#if 0) pending reimplementation
 *       of the job-queue subsystem.
 */
status_t
KDiskDeviceManager::_UpdateBusyPartitions(KDiskDevice *device)
{
	if (!device)
		return B_BAD_VALUE;
	// mark all partitions un-busy
	struct UnmarkBusyVisitor : KPartitionVisitor {
		virtual bool VisitPre(KPartition *partition)
		{
			partition->ClearFlags(B_PARTITION_BUSY
								  | B_PARTITION_DESCENDANT_BUSY);
			return false;
		}
	} visitor;
	device->VisitEachDescendant(&visitor);
	// Iterate through all job queues and all jobs scheduled or in
	// progress and mark their scope busy.
	for (int32 cookie = 0;
		 KDiskDeviceJobQueue *jobQueue = NextJobQueue(&cookie); ) {
		if (jobQueue->Device() != device)
			continue;
		for (int32 i = jobQueue->ActiveJobIndex();
			 KDiskDeviceJob *job = jobQueue->JobAt(i); i++) {
			if (job->Status() != B_DISK_DEVICE_JOB_IN_PROGRESS
				&& job->Status() != B_DISK_DEVICE_JOB_SCHEDULED) {
				continue;
			}
			KPartition *partition = FindPartition(job->ScopeID());
			if (!partition || partition->Device() != device)
				continue;
			partition->AddFlags(B_PARTITION_BUSY);
		}
	}
	// mark all anscestors of busy partitions descendant busy and all
	// descendants busy
	struct MarkBusyVisitor : KPartitionVisitor {
		virtual bool VisitPre(KPartition *partition)
		{
			// parent busy => child busy
			if (partition->Parent() && partition->Parent()->IsBusy())
				partition->AddFlags(B_PARTITION_BUSY);
			return false;
		}

		virtual bool VisitPost(KPartition *partition)
		{
			// child [descendant] busy => parent descendant busy
			if ((partition->IsBusy() || partition->IsDescendantBusy())
				&& partition->Parent()) {
				partition->Parent()->AddFlags(B_PARTITION_DESCENDANT_BUSY);
			}
			return false;
		}
	} visitor2;
	device->VisitEachDescendant(&visitor2);
	return B_OK;
}
#endif


/**
 * @brief Recursively scan a path for raw device nodes and register them.
 *
 * If @p path is a directory, iterates its entries recursively.  If it is a
 * regular file whose name ends in "/raw", creates a KDiskDevice for it (unless
 * one already exists).
 *
 * @param path  Absolute path to scan; may be a directory or a device node.
 * @retval B_OK           At least one device was found and registered.
 * @retval B_ENTRY_NOT_FOUND No device node was found under @p path.
 * @retval B_NO_MEMORY    KDiskDevice allocation or map insertion failed.
 * @retval B_ERROR        The path is a non-raw file.
 * @retval other          errno from lstat/opendir, or device initialisation error.
 */
status_t
KDiskDeviceManager::_Scan(const char* path)
{
	TRACE("KDiskDeviceManager::_Scan(%s)\n", path);
	status_t error = B_ENTRY_NOT_FOUND;
	struct stat st;
	if (lstat(path, &st) < 0) {
		return errno;
	}
	if (S_ISDIR(st.st_mode)) {
		// a directory: iterate through its contents
		DIR* dir = opendir(path);
		if (!dir)
			return errno;
		while (dirent* entry = readdir(dir)) {
			// skip "." and ".."
			if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
				continue;
			KPath entryPath;
			if (entryPath.SetPath(path) != B_OK
				|| entryPath.Append(entry->d_name) != B_OK) {
				continue;
			}
			if (_Scan(entryPath.Path()) == B_OK)
				error = B_OK;
		}
		closedir(dir);
	} else {
		// not a directory
		// check, if it is named "raw"
		int32 len = strlen(path);
		int32 leafLen = strlen("/raw");
		if (len <= leafLen || strcmp(path + len - leafLen, "/raw"))
			return B_ERROR;
		if (FindDevice(path) != NULL) {
			// we already know this device
			return B_OK;
		}

		TRACE("  found device: %s\n", path);
		// create a KDiskDevice for it
		KDiskDevice* device = new(nothrow) KDiskDevice;
		if (!device)
			return B_NO_MEMORY;

		// init the KDiskDevice
		error = device->SetTo(path);
		// add the device
		if (error == B_OK && !_AddDevice(device))
			error = B_NO_MEMORY;
		// cleanup on error
		if (error != B_OK)
			delete device;
	}
	return error;
}


/*!
	The device must be write locked, the manager must be locked.
*/
/**
 * @brief Dispatch a synchronous or asynchronous partition scan for @p partition.
 *
 * If @p async is false (the only currently supported mode), delegates directly
 * to the two-argument _ScanPartition() overload.  Asynchronous scanning via
 * job queues is stubbed out with #if 0.
 *
 * @param partition      The KPartition to scan; device must be write-locked and
 *                       manager must be locked.
 * @param async          Reserved; must be false (async path is disabled).
 * @param restrictScan   If non-NULL, only disk systems present in this map are
 *                       tried during identification.  NULL means try all.
 * @retval B_OK          Scan completed successfully.
 * @retval B_BAD_VALUE   @p partition was NULL.
 */
status_t
KDiskDeviceManager::_ScanPartition(KPartition* partition, bool async,
	DiskSystemMap* restrictScan)
{
// TODO: There's no reason why the manager needs to be locked anymore.
	if (!partition)
		return B_BAD_VALUE;

// TODO: Reimplement asynchronous scanning, if we really need it.
#if 0
	if (async) {
		// create a new job queue for the device
		KDiskDeviceJobQueue *jobQueue = new(nothrow) KDiskDeviceJobQueue;
		if (!jobQueue)
			return B_NO_MEMORY;
		jobQueue->SetDevice(partition->Device());

		// create a job for scanning the device and add it to the job queue
		KDiskDeviceJob *job = fJobFactory->CreateScanPartitionJob(partition->ID());
		if (!job) {
			delete jobQueue;
			return B_NO_MEMORY;
		}

		if (!jobQueue->AddJob(job)) {
			delete jobQueue;
			delete job;
			return B_NO_MEMORY;
		}

		// add the job queue
		status_t error = AddJobQueue(jobQueue);
		if (error != B_OK)
			delete jobQueue;

		return error;
	}
#endif

	// scan synchronously

	return _ScanPartition(partition, restrictScan);
}


/**
 * @brief Identify the best-matching disk system for @p partition and scan it.
 *
 * Publishes the partition device node if not yet published, then iterates
 * @p restrictScan (or fDiskSystems if NULL) calling Identify() on each system.
 * The system returning the highest priority wins and its Scan() is called.  If
 * children already exist only they are recursively scanned, not the parent.
 * Partitions with invalid geometry (negative offset, zero block size, or
 * non-positive size) are rejected with B_BAD_DATA.
 *
 * @param partition    The KPartition to identify and scan; its device must be
 *                     write-locked and the manager must be locked.
 * @param restrictScan If non-NULL, limit identification to systems in this map.
 * @retval B_OK        Scan succeeded (or partition already has children).
 * @retval B_BAD_VALUE @p partition was NULL.
 * @retval B_BAD_DATA  Partition geometry is invalid.
 * @retval other       Error returned by PublishDevice() or Scan().
 */
status_t
KDiskDeviceManager::_ScanPartition(KPartition* partition,
	DiskSystemMap* restrictScan)
{
	// the partition's device must be write-locked
	if (partition == NULL)
		return B_BAD_VALUE;
	if (!partition->Device()->HasMedia() || partition->IsMounted())
		return B_OK;

	if (partition->CountChildren() > 0) {
		// Since this partition has already children, we don't scan it
		// again, but only its children.
		for (int32 i = 0; KPartition* child = partition->ChildAt(i); i++) {
			_ScanPartition(child, restrictScan);
		}
		return B_OK;
	}

	KPath partitionPath;
	partition->GetPath(&partitionPath);

	// This happens with some copy protected CDs or eventually other issues.
	// Just ignore the partition...
	if (partition->Offset() < 0 || partition->BlockSize() == 0
		|| partition->Size() <= 0) {
		TRACE_ALWAYS("Partition %s has invalid parameters, ignoring it.\n",
			partitionPath.Path());
		return B_BAD_DATA;
	}

	TRACE("KDiskDeviceManager::_ScanPartition(%s)\n", partitionPath.Path());

	// publish the partition
	status_t error = B_OK;
	if (!partition->IsPublished()) {
		error = partition->PublishDevice();
		if (error != B_OK)
			return error;
	}

	DiskSystemMap* diskSystems = restrictScan;
	if (diskSystems == NULL)
		diskSystems = fDiskSystems;

	// find the disk system that returns the best priority for this partition
	float bestPriority = partition->DiskSystemPriority();
	KDiskSystem* bestDiskSystem = NULL;
	void* bestCookie = NULL;
	for (DiskSystemMap::Iterator iterator = diskSystems->Begin();
			iterator != diskSystems->End(); iterator++) {
		KDiskSystem* diskSystem = iterator->Value();
		if (diskSystem->Load() != B_OK)
			continue;

		TRACE("  trying: %s\n", diskSystem->Name());

		void* cookie = NULL;
		float priority = diskSystem->Identify(partition, &cookie);

		TRACE("  returned: %g\n", priority);

		if (priority >= 0 && priority > bestPriority) {
			// new best disk system
			if (bestDiskSystem != NULL) {
				bestDiskSystem->FreeIdentifyCookie(partition, bestCookie);
				bestDiskSystem->Unload();
			}
			bestPriority = priority;
			bestDiskSystem = diskSystem;
			bestCookie = cookie;
		} else {
			// disk system doesn't identify the partition or worse than our
			// current favorite
			if (priority >= 0)
				diskSystem->FreeIdentifyCookie(partition, cookie);
			diskSystem->Unload();
		}
	}

	// now, if we have found a disk system, let it scan the partition
	if (bestDiskSystem != NULL) {
		TRACE("  scanning with: %s\n", bestDiskSystem->Name());
		error = bestDiskSystem->Scan(partition, bestCookie);
		bestDiskSystem->FreeIdentifyCookie(partition, bestCookie);
		if (error == B_OK) {
			partition->SetDiskSystem(bestDiskSystem, bestPriority);
			for (int32 i = 0; KPartition* child = partition->ChildAt(i); i++)
				_ScanPartition(child, restrictScan);
		} else {
			// TODO: Handle the error.
			TRACE_ERROR("scanning failed: %s\n", strerror(error));
		}

		// now we can safely unload the disk system -- it has been loaded by
		// the partition(s) and thus will not really be unloaded
		bestDiskSystem->Unload();
	} else {
		// contents not recognized
		// nothing to be done -- partitions are created as unrecognized
	}

	return error;
}


/**
 * @brief Recursively add or remove a node monitor for a directory tree.
 *
 * Stats @p path; if it is a directory, adds or removes a B_WATCH_DIRECTORY
 * listener on it (using fDeviceWatcher) and recurses into every subdirectory.
 * Non-directory entries and the "." / ".." pseudo-entries are skipped.
 *
 * @param path  Absolute path to the root of the tree to monitor.
 * @param add   If true, install node monitors; if false, remove them.
 * @retval B_OK             Monitoring was updated for at least one directory.
 * @retval B_ENTRY_NOT_FOUND @p path is not a directory.
 * @retval other            errno from lstat/opendir, or add_node_listener /
 *                          remove_node_listener error.
 */
status_t
KDiskDeviceManager::_AddRemoveMonitoring(const char* path, bool add)
{
	struct stat st;
	if (lstat(path, &st) < 0)
		return errno;

	status_t error = B_ENTRY_NOT_FOUND;
	if (S_ISDIR(st.st_mode)) {
		if (add) {
			error = add_node_listener(st.st_dev, st.st_ino, B_WATCH_DIRECTORY,
				*fDeviceWatcher);
		} else {
			error = remove_node_listener(st.st_dev, st.st_ino,
				*fDeviceWatcher);
		}
		if (error != B_OK)
			return error;

		DIR* dir = opendir(path);
		if (!dir)
			return errno;

		while (dirent* entry = readdir(dir)) {
			// skip "." and ".."
			if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
				continue;

			KPath entryPath;
			if (entryPath.SetPath(path) != B_OK
				|| entryPath.Append(entry->d_name) != B_OK) {
				continue;
			}

			if (_AddRemoveMonitoring(entryPath.Path(), add) == B_OK)
				error = B_OK;
		}
		closedir(dir);
	}

	return error;
}


/**
 * @brief Background thread body: poll all registered devices for media changes.
 *
 * Runs until fTerminating is set.  Each iteration walks every registered
 * device, checks for media insertion/removal and media-changed events, and
 * for each change uninitialises the media, optionally re-scans partitions
 * (on media change), and sends the appropriate B_DEVICE_MEDIA_CHANGED
 * notification.  Sleeps one second between iterations.
 *
 * @retval 0  Always returns 0 when fTerminating becomes true.
 */
status_t
KDiskDeviceManager::_CheckMediaStatus()
{
	while (!fTerminating) {
		int32 cookie = 0;
		while (KDiskDevice* device = RegisterNextDevice(&cookie)) {
			PartitionRegistrar _(device, true);
			DeviceWriteLocker locker(device);

			if (device->IsBusy(true))
				continue;

			bool hadMedia = device->HasMedia();
			bool changedMedia = device->MediaChanged();
			device->UpdateMediaStatusIfNeeded();

			// Detect if there was any status change since last check.
			if ((!device->MediaChanged() && (device->HasMedia() || !hadMedia))
				|| !(hadMedia != device->HasMedia()
					|| changedMedia != device->MediaChanged()))
				continue;

			device->MarkBusy(true);
			device->UninitializeMedia();

			if (device->MediaChanged()) {
				dprintf("Media changed from %s\n", device->Path());
				device->UpdateGeometry();
				_ScanPartition(device, false);
				_NotifyDeviceEvent(device, B_DEVICE_MEDIA_CHANGED,
					B_DEVICE_REQUEST_DEVICE);
			} else if (!device->HasMedia() && hadMedia) {
				dprintf("Media removed from %s\n", device->Path());
			}

			device->UnmarkBusy(true);
		}

		snooze(1000000);
	}

	return 0;
}


/**
 * @brief Kernel thread entry point for the media-status polling daemon.
 *
 * Casts @p self to KDiskDeviceManager* and forwards to _CheckMediaStatus().
 *
 * @param self  Pointer to the KDiskDeviceManager instance (passed as void*).
 * @return      Return value of _CheckMediaStatus().
 */
status_t
KDiskDeviceManager::_CheckMediaStatusDaemon(void* self)
{
	return ((KDiskDeviceManager*)self)->_CheckMediaStatus();
}


/**
 * @brief Send a B_DEVICE_UPDATE notification message for @p device.
 *
 * Builds a KMessage containing the event opcode, device ID, and device path,
 * then forwards it to fNotifications with the given event mask.
 *
 * @param device  The KDiskDevice that triggered the event; must not be NULL.
 * @param event   One of the B_DEVICE_ADDED / B_DEVICE_REMOVED /
 *                B_DEVICE_MEDIA_CHANGED constants.
 * @param mask    Bitmask of B_DEVICE_REQUEST_* flags passed to Notify().
 */
void
KDiskDeviceManager::_NotifyDeviceEvent(KDiskDevice* device, int32 event,
	uint32 mask)
{
	char messageBuffer[512];
	KMessage message;
	message.SetTo(messageBuffer, sizeof(messageBuffer), B_DEVICE_UPDATE);
	message.AddInt32("event", event);
	message.AddInt32("id", device->ID());
	message.AddString("device", device->Path());

	fNotifications->Notify(message, mask);
}
