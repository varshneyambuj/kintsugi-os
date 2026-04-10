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
   Copyright 2007-2018, Haiku, Inc. All rights reserved.
   Distributed under the terms of the MIT License.
   
   Authors:
   		Stephan Aßmus, superstippi@gmx.de
   		Axel Dörfler, axeld@pinc-software.de
 */
/** @file AutoMounter.cpp
 *  @brief Automatic volume mounter server for disk devices. */
#include "AutoMounter.h"

#include <new>

#include <string.h>
#include <unistd.h>

#include <Alert.h>
#include <AutoLocker.h>
#include <Catalog.h>
#include <Debug.h>
#include <Directory.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <DiskDeviceList.h>
#include <DiskDeviceTypes.h>
#include <DiskSystem.h>
#include <FindDirectory.h>
#include <fs_info.h>
#include <fs_volume.h>
#include <LaunchRoster.h>
#include <Locale.h>
#include <Message.h>
#include <Node.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <PropertyInfo.h>
#include <String.h>
#include <VolumeRoster.h>

#include "MountServer.h"

#include "Utilities.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AutoMounter"


/** @brief Name of the mount server settings file. */
static const char* kMountServerSettings = "mount_server";

/** @brief Suffix appended to a device path to form the mount flags settings key. */
static const char* kMountFlagsKeyExtension = " mount flags";

/** @brief Name of the launch roster event fired after initial volumes are mounted. */
static const char* kInitialMountEvent = "initial_volumes_mounted";


class MountVisitor : public BDiskDeviceVisitor {
public:
								MountVisitor(mount_mode normalMode,
									mount_mode removableMode,
									bool initialRescan, BMessage& previous,
									partition_id deviceID);
	virtual						~MountVisitor()
									{}

	virtual	bool				Visit(BDiskDevice* device);
	virtual	bool				Visit(BPartition* partition, int32 level);

private:
			bool				_WasPreviouslyMounted(const BPath& path,
									const BPartition* partition);

private:
			mount_mode			fNormalMode;
			mount_mode			fRemovableMode;
			bool				fInitialRescan;
			BMessage&			fPrevious;
			partition_id		fOnlyOnDeviceID;
};


class MountArchivedVisitor : public BDiskDeviceVisitor {
public:
								MountArchivedVisitor(
									const BDiskDeviceList& devices,
									const BMessage& archived);
	virtual						~MountArchivedVisitor();

	virtual	bool				Visit(BDiskDevice* device);
	virtual	bool				Visit(BPartition* partition, int32 level);

private:
		enum {
			MATCHES_VOLUME_NAME	= (1 << 0),
			MATCHES_DEVICE_NAME	= (1 << 1),
			MATCHES_FS_NAME		= (1 << 2),
			MATCHES_BLOCK_SIZE	= (1 << 3),
			MATCHES_CAPACITY	= (1 << 4),
		};

			int					_Score(BPartition* partition);

private:
			const BDiskDeviceList& fDevices;
			const BMessage&		fArchived;
			int					fBestScore;
			partition_id		fBestID;
};


/**
 * @brief Returns whether the system was booted in safe mode.
 *
 * @return @c true if the SAFEMODE environment variable is set to "yes".
 */
static bool
BootedInSafeMode()
{
	const char* safeMode = getenv("SAFEMODE");
	return safeMode != NULL && strcmp(safeMode, "yes") == 0;
}


class ArchiveVisitor : public BDiskDeviceVisitor {
public:
								ArchiveVisitor(BMessage& message);
	virtual						~ArchiveVisitor();

	virtual	bool				Visit(BDiskDevice* device);
	virtual	bool				Visit(BPartition* partition, int32 level);

private:
			BMessage&			fMessage;
};


// #pragma mark - MountVisitor


/**
 * @brief Constructs a mount visitor with the specified mount policies.
 *
 * @param normalMode    Mount mode for normal (non-removable) volumes.
 * @param removableMode Mount mode for removable media.
 * @param initialRescan Whether this is the initial volume scan at startup.
 * @param previous      Message containing previously mounted volume info.
 * @param deviceID      If >= 0, restricts mounting to partitions on this device only.
 */
MountVisitor::MountVisitor(mount_mode normalMode, mount_mode removableMode,
		bool initialRescan, BMessage& previous, partition_id deviceID)
	:
	fNormalMode(normalMode),
	fRemovableMode(removableMode),
	fInitialRescan(initialRescan),
	fPrevious(previous),
	fOnlyOnDeviceID(deviceID)
{
}


/**
 * @brief Visits a disk device by delegating to the partition visitor at level 0.
 *
 * @param device The disk device to visit.
 * @return @c false to continue visiting.
 */
bool
MountVisitor::Visit(BDiskDevice* device)
{
	return Visit(device, 0);
}


/**
 * @brief Determines whether a partition should be mounted and mounts it.
 *
 * Applies the configured mount mode, checks device ID restrictions,
 * and consults previous-mount state or content type as needed.
 *
 * @param partition The partition to evaluate.
 * @param level     Nesting level in the partition tree.
 * @return @c false to continue visiting other partitions.
 */
bool
MountVisitor::Visit(BPartition* partition, int32 level)
{
	if (fOnlyOnDeviceID >= 0) {
		// only mount partitions on the given device id
		// or if the partition ID is already matched
		BPartition* device = partition;
		while (device->Parent() != NULL) {
			if (device->ID() == fOnlyOnDeviceID) {
				// we are happy
				break;
			}
			device = device->Parent();
		}
		if (device->ID() != fOnlyOnDeviceID)
			return false;
	}

	mount_mode mode = !fInitialRescan && partition->Device()->IsRemovableMedia()
		? fRemovableMode : fNormalMode;
	if (mode == kNoVolumes || partition->IsMounted()
		|| !partition->ContainsFileSystem()) {
		return false;
	}

	BPath path;
	if (partition->GetPath(&path) != B_OK)
		return false;

	if (mode == kRestorePreviousVolumes) {
		// mount all volumes that were stored in the settings file
		if (!_WasPreviouslyMounted(path, partition))
			return false;
	} else if (mode == kOnlyBFSVolumes) {
		if (partition->ContentType() == NULL
			|| strcmp(partition->ContentType(), kPartitionTypeBFS))
			return false;
	}

	uint32 mountFlags;
	if (!fInitialRescan) {
		// Ask the user about mount flags if this is not the
		// initial scan.
		if (!AutoMounter::_SuggestMountFlags(partition, &mountFlags))
			return false;
	} else {
		BString mountFlagsKey(path.Path());
		mountFlagsKey << kMountFlagsKeyExtension;
		if (fPrevious.FindInt32(mountFlagsKey.String(),
				(int32*)&mountFlags) < B_OK) {
			mountFlags = 0;
		}
	}

	if (partition->Mount(NULL, mountFlags) != B_OK) {
		// TODO: Error to syslog
	}
	return false;
}


/**
 * @brief Checks legacy config data to see if a partition was previously mounted.
 *
 * @param path      Path to the partition device.
 * @param partition The partition to check.
 * @return @c true if the partition's content name matches the stored name.
 */
bool
MountVisitor::_WasPreviouslyMounted(const BPath& path,
	const BPartition* partition)
{
	// We only check the legacy config data here; the current method
	// is implemented in ArchivedVolumeVisitor -- this can be removed
	// some day.
	BString volumeName;
	if (fPrevious.FindString(path.Path(), &volumeName) != B_OK
		|| volumeName != partition->ContentName())
		return false;

	return true;
}


// #pragma mark - MountArchivedVisitor


/**
 * @brief Constructs a visitor that scores partitions against archived volume metadata.
 *
 * @param devices  The disk device list to search.
 * @param archived A BMessage containing the archived volume properties.
 */
MountArchivedVisitor::MountArchivedVisitor(const BDiskDeviceList& devices,
		const BMessage& archived)
	:
	fDevices(devices),
	fArchived(archived),
	fBestScore(-1),
	fBestID(-1)
{
}


/**
 * @brief Destructor that mounts the best-scoring partition if it meets minimum criteria.
 */
MountArchivedVisitor::~MountArchivedVisitor()
{
	// At least these fields, plus one other besides, must match for us to auto-mount.
	const int requiredMatches = MATCHES_FS_NAME | MATCHES_CAPACITY | MATCHES_BLOCK_SIZE;
	if ((fBestScore & requiredMatches) != requiredMatches)
		return;
	if ((fBestScore & ~requiredMatches) == 0)
		return;

	uint32 mountFlags = fArchived.GetUInt32("mountFlags", 0);
	BPartition* partition = fDevices.PartitionWithID(fBestID);
	if (partition != NULL)
		partition->Mount(NULL, mountFlags);
}


/** @brief Visits a device by delegating to the partition visitor at level 0. */
bool
MountArchivedVisitor::Visit(BDiskDevice* device)
{
	return Visit(device, 0);
}


/**
 * @brief Scores an unmounted partition against the archived volume properties.
 *
 * @param partition The partition to evaluate.
 * @param level     Nesting level (unused).
 * @return @c false to continue visiting.
 */
bool
MountArchivedVisitor::Visit(BPartition* partition, int32 level)
{
	if (partition->IsMounted() || !partition->ContainsFileSystem())
		return false;

	int score = _Score(partition);
	if (score > fBestScore) {
		fBestScore = score;
		fBestID = partition->ID();
	}

	return false;
}


/**
 * @brief Computes a bitmask score for how well a partition matches archived properties.
 *
 * @param partition The partition to score.
 * @return A bitmask of MATCHES_* flags indicating which properties matched.
 */
int
MountArchivedVisitor::_Score(BPartition* partition)
{
	BPath path;
	if (partition->GetPath(&path) != B_OK)
		return 0;

	int score = 0;

	BString volumeName = fArchived.GetString("volumeName");
	if (volumeName == partition->ContentName())
		score |= MATCHES_VOLUME_NAME;

	BString deviceName = fArchived.GetString("deviceName");
	if (deviceName == path.Path())
		score |= MATCHES_DEVICE_NAME;

	BString fsName = fArchived.FindString("fsName");
	if (fsName == partition->ContentType())
		score |= MATCHES_FS_NAME;

	int64 capacity = fArchived.GetInt64("capacity", 0);
	if (capacity == partition->ContentSize())
		score |= MATCHES_CAPACITY;

	uint32 blockSize = fArchived.GetUInt32("blockSize", 0);
	if (blockSize == partition->BlockSize())
		score |= MATCHES_BLOCK_SIZE;

	return score;
}


// #pragma mark - ArchiveVisitor


/**
 * @brief Constructs an archive visitor that stores partition info in the given message.
 *
 * @param message Output message to populate with partition archive data.
 */
ArchiveVisitor::ArchiveVisitor(BMessage& message)
	:
	fMessage(message)
{
}


/** @brief Destructor (no-op). */
ArchiveVisitor::~ArchiveVisitor()
{
}


/** @brief Visits a device by delegating to the partition visitor at level 0. */
bool
ArchiveVisitor::Visit(BDiskDevice* device)
{
	return Visit(device, 0);
}


/**
 * @brief Archives a partition's metadata (block size, capacity, names, mount flags).
 *
 * @param partition The partition to archive.
 * @param level     Nesting level (unused).
 * @return @c false to continue visiting.
 */
bool
ArchiveVisitor::Visit(BPartition* partition, int32 level)
{
	if (!partition->ContainsFileSystem())
		return false;

	BPath path;
	if (partition->GetPath(&path) != B_OK)
		return false;

	BMessage info;
	info.AddUInt32("blockSize", partition->BlockSize());
	info.AddInt64("capacity", partition->ContentSize());
	info.AddString("deviceName", path.Path());
	info.AddString("volumeName", partition->ContentName());
	info.AddString("fsName", partition->ContentType());
	BVolume volume;
	partition->GetVolume(&volume);
	fs_info fsInfo;
	if (fs_stat_dev(volume.Device(), &fsInfo) == 0) {
		if ((fsInfo.flags & B_FS_IS_READONLY) != 0)
			info.AddUInt32("mountFlags", B_MOUNT_READ_ONLY);
		else
			info.AddUInt32("mountFlags", 0);
	}

	fMessage.AddMessage("info", &info);
	return false;
}


// #pragma mark -


/**
 * @brief Constructs the AutoMounter server and reads initial settings.
 *
 * Registers for device notifications and the initial mount event.
 * In safe mode, all automounting is disabled.
 */
AutoMounter::AutoMounter()
	:
	BServer(kMountServerSignature, false, NULL),
	fNormalMode(kRestorePreviousVolumes),
	fRemovableMode(kAllVolumes),
	fEjectWhenUnmounting(true)
{
	set_thread_priority(Thread(), B_LOW_PRIORITY);

	if (!BootedInSafeMode()) {
		_ReadSettings();
	} else {
		// defeat automounter in safe mode, don't even care about the settings
		fNormalMode = kNoVolumes;
		fRemovableMode = kNoVolumes;
	}

	BDiskDeviceRoster().StartWatching(this,
		B_DEVICE_REQUEST_DEVICE | B_DEVICE_REQUEST_DEVICE_LIST);
	BLaunchRoster().RegisterEvent(this, kInitialMountEvent, B_STICKY_EVENT);
}


/** @brief Unregisters events and stops device watching. */
AutoMounter::~AutoMounter()
{
	BLaunchRoster().UnregisterEvent(this, kInitialMountEvent);
	BDiskDeviceRoster().StopWatching(this);
}


/**
 * @brief Performs the initial volume scan and notifies the launch roster.
 */
void
AutoMounter::ReadyToRun()
{
	// Do initial scan
	_MountVolumes(fNormalMode, fRemovableMode, true);
	BLaunchRoster().NotifyEvent(this, kInitialMountEvent);
}


/**
 * @brief Dispatches mount/unmount requests, settings changes, and device events.
 *
 * @param message The incoming BMessage to process.
 */
void
AutoMounter::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMountVolume:
			_MountVolume(message);
			break;

		case kUnmountVolume:
			_UnmountAndEjectVolume(message);
			break;

		case kSetAutomounterParams:
		{
			bool rescanNow = false;
			message->FindBool("rescanNow", &rescanNow);

			_UpdateSettingsFromMessage(message);
			_GetSettings(&fSettings);
			_WriteSettings();

			if (rescanNow)
				_MountVolumes(fNormalMode, fRemovableMode);
			break;
		}

		case kGetAutomounterParams:
		{
			BMessage reply;
			_GetSettings(&reply);
			message->SendReply(&reply);
			break;
		}

		case kMountAllNow:
			_MountVolumes(kAllVolumes, kAllVolumes);
			break;

		case B_DEVICE_UPDATE:
			int32 event;
			if (message->FindInt32("event", &event) != B_OK
				|| (event != B_DEVICE_MEDIA_CHANGED
					&& event != B_DEVICE_ADDED))
				break;

			partition_id deviceID;
			if (message->FindInt32("id", &deviceID) != B_OK)
				break;

			_MountVolumes(kNoVolumes, fRemovableMode, false, deviceID);
			break;

#if 0
		case B_NODE_MONITOR:
		{
			int32 opcode;
			if (message->FindInt32("opcode", &opcode) != B_OK)
				break;

			switch (opcode) {
				//	The name of a mount point has changed
				case B_ENTRY_MOVED: {
					WRITELOG(("*** Received Mount Point Renamed Notification"));

					const char *newName;
					if (message->FindString("name", &newName) != B_OK) {
						WRITELOG(("ERROR: Couldn't find name field in update "
							"message"));
						PRINT_OBJECT(*message);
						break ;
					}

					//
					// When the node monitor reports a move, it gives the
					// parent device and inode that moved.  The problem is
					// that  the inode is the inode of root *in* the filesystem,
					// which is generally always the same number for every
					// filesystem of a type.
					//
					// What we'd really like is the device that the moved
					// volume is mounted on.  Find this by using the
					// *new* name and directory, and then stat()ing that to
					// find the device.
					//
					dev_t parentDevice;
					if (message->FindInt32("device", &parentDevice) != B_OK) {
						WRITELOG(("ERROR: Couldn't find 'device' field in "
							"update message"));
						PRINT_OBJECT(*message);
						break;
					}

					ino_t toDirectory;
					if (message->FindInt64("to directory", &toDirectory)
						!= B_OK) {
						WRITELOG(("ERROR: Couldn't find 'to directory' field "
							"in update message"));
						PRINT_OBJECT(*message);
						break;
					}

					entry_ref root_entry(parentDevice, toDirectory, newName);

					BNode entryNode(&root_entry);
					if (entryNode.InitCheck() != B_OK) {
						WRITELOG(("ERROR: Couldn't create mount point entry "
							"node: %s/n", strerror(entryNode.InitCheck())));
						break;
					}

					node_ref mountPointNode;
					if (entryNode.GetNodeRef(&mountPointNode) != B_OK) {
						WRITELOG(("ERROR: Couldn't get node ref for new mount "
							"point"));
						break;
					}

					WRITELOG(("Attempt to rename device %li to %s",
						mountPointNode.device, newName));

					Partition *partition = FindPartition(mountPointNode.device);
					if (partition != NULL) {
						WRITELOG(("Found device, changing name."));

						BVolume mountVolume(partition->VolumeDeviceID());
						BDirectory mountDir;
						mountVolume.GetRootDirectory(&mountDir);
						BPath dirPath(&mountDir, 0);

						partition->SetMountedAt(dirPath.Path());
						partition->SetVolumeName(newName);
						break;
					} else {
						WRITELOG(("ERROR: Device %li does not appear to be "
							"present", mountPointNode.device));
					}
				}
			}
			break;
		}
#endif

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


/**
 * @brief Saves settings on quit unless in safe mode.
 *
 * @return @c true to allow the application to quit.
 */
bool
AutoMounter::QuitRequested()
{
	if (!BootedInSafeMode()) {
		// Don't write out settings in safe mode - this would overwrite the
		// normal, non-safe mode settings.
		_WriteSettings();
	}

	return true;
}


// #pragma mark - private methods


/**
 * @brief Scans disk devices and mounts partitions according to the given modes.
 *
 * @param normal        Mount mode for non-removable volumes.
 * @param removable     Mount mode for removable media.
 * @param initialRescan Whether this is the initial startup scan (default false).
 * @param deviceID      If >= 0, restricts mounting to this device (default -1).
 */
void
AutoMounter::_MountVolumes(mount_mode normal, mount_mode removable,
	bool initialRescan, partition_id deviceID)
{
	if (normal == kNoVolumes && removable == kNoVolumes)
		return;

	BDiskDeviceList devices;
	status_t status = devices.Fetch();
	if (status != B_OK)
		return;

	if (normal == kRestorePreviousVolumes) {
		BMessage archived;
		for (int32 index = 0;
				fSettings.FindMessage("info", index, &archived) == B_OK;
				index++) {
			MountArchivedVisitor visitor(devices, archived);
			devices.VisitEachPartition(&visitor);
		}
	}

	MountVisitor visitor(normal, removable, initialRescan, fSettings, deviceID);
	devices.VisitEachPartition(&visitor);
}


/**
 * @brief Mounts a specific partition identified by ID from a BMessage.
 *
 * Shows an error alert if mounting fails and a GUI context is available.
 *
 * @param message The message containing an "id" int32 field.
 */
void
AutoMounter::_MountVolume(const BMessage* message)
{
	int32 id;
	if (message->FindInt32("id", &id) != B_OK)
		return;

	BDiskDeviceRoster roster;
	BPartition *partition;
	BDiskDevice device;
	if (roster.GetPartitionWithID(id, &device, &partition) != B_OK)
		return;

	uint32 mountFlags;
	if (!_SuggestMountFlags(partition, &mountFlags))
		return;

	status_t status = partition->Mount(NULL, mountFlags);
	if (status < B_OK && InitGUIContext() == B_OK) {
		char text[512];
		snprintf(text, sizeof(text),
			B_TRANSLATE("Error mounting volume:\n\n%s"), strerror(status));
		BAlert* alert = new BAlert(B_TRANSLATE("Mount error"), text,
			B_TRANSLATE("OK"));
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);
	}
}


/**
 * @brief Asks the user whether to force-unmount a volume after a failed unmount.
 *
 * @param name  The volume name to display in the alert.
 * @param error The error code from the failed unmount attempt.
 * @return @c true if the user chose to force-unmount.
 */
bool
AutoMounter::_SuggestForceUnmount(const char* name, status_t error)
{
	if (InitGUIContext() != B_OK)
		return false;

	char text[1024];
	snprintf(text, sizeof(text),
		B_TRANSLATE("Could not unmount disk \"%s\":\n\t%s\n\n"
			"Should unmounting be forced?\n\n"
			"Note: If an application is currently writing to the volume, "
			"unmounting it now might result in loss of data.\n"),
		name, strerror(error));

	BAlert* alert = new BAlert(B_TRANSLATE("Force unmount"), text,
		B_TRANSLATE("Cancel"), B_TRANSLATE("Force unmount"), NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	int32 choice = alert->Go();

	return choice == 1;
}


/**
 * @brief Displays an error alert for a failed unmount operation.
 *
 * @param name  The volume name.
 * @param error The error code from the failed unmount.
 */
void
AutoMounter::_ReportUnmountError(const char* name, status_t error)
{
	if (InitGUIContext() != B_OK)
		return;

	char text[512];
	snprintf(text, sizeof(text), B_TRANSLATE("Could not unmount disk "
		"\"%s\":\n\t%s"), name, strerror(error));

	BAlert* alert = new BAlert(B_TRANSLATE("Unmount error"), text,
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
}


/**
 * @brief Unmounts and optionally ejects a volume.
 *
 * If the initial unmount fails, offers force-unmount. After successful
 * unmount, ejects the device if no other mounted partitions remain and
 * removes the mount point directory if it resides on rootfs.
 *
 * @param partition  The partition to unmount, or NULL to unmount by path.
 * @param mountPoint The mount point path.
 * @param name       Human-readable volume name for error messages.
 */
void
AutoMounter::_UnmountAndEjectVolume(BPartition* partition, BPath& mountPoint,
	const char* name)
{
	BDiskDevice deviceStorage;
	BDiskDevice* device;
	if (partition == NULL) {
		// Try to retrieve partition
		BDiskDeviceRoster().FindPartitionByMountPoint(mountPoint.Path(),
			&deviceStorage, &partition);
			device = &deviceStorage;
	} else {
		device = partition->Device();
	}

	status_t status;
	if (partition != NULL)
		status = partition->Unmount();
	else
		status = fs_unmount_volume(mountPoint.Path(), 0);

	if (status != B_OK) {
		if (!_SuggestForceUnmount(name, status))
			return;

		if (partition != NULL)
			status = partition->Unmount(B_FORCE_UNMOUNT);
		else
			status = fs_unmount_volume(mountPoint.Path(), B_FORCE_UNMOUNT);
	}

	if (status != B_OK) {
		_ReportUnmountError(name, status);
		return;
	}

	if (fEjectWhenUnmounting && partition != NULL) {
		// eject device if it doesn't have any mounted partitions left
		class IsMountedVisitor : public BDiskDeviceVisitor {
		public:
			IsMountedVisitor()
				:
				fHasMounted(false)
			{
			}

			virtual bool Visit(BDiskDevice* device)
			{
				return Visit(device, 0);
			}

			virtual bool Visit(BPartition* partition, int32 level)
			{
				if (partition->IsMounted()) {
					fHasMounted = true;
					return true;
				}

				return false;
			}

			bool HasMountedPartitions() const
			{
				return fHasMounted;
			}

		private:
			bool	fHasMounted;
		} visitor;

		device->VisitEachDescendant(&visitor);

		if (!visitor.HasMountedPartitions())
			device->Eject();
	}

	// remove the directory if it's a directory in rootfs
	if (dev_for_path(mountPoint.Path()) == dev_for_path("/"))
		rmdir(mountPoint.Path());
}


/**
 * @brief Unmounts and ejects a volume identified by partition ID or device ID in a message.
 *
 * @param message The message containing "id" (partition_id) or "device_id" (dev_t).
 */
void
AutoMounter::_UnmountAndEjectVolume(BMessage* message)
{
	int32 id;
	if (message->FindInt32("id", &id) == B_OK) {
		BDiskDeviceRoster roster;
		BPartition *partition;
		BDiskDevice device;
		if (roster.GetPartitionWithID(id, &device, &partition) != B_OK)
			return;

		BPath path;
		if (partition->GetMountPoint(&path) == B_OK)
			_UnmountAndEjectVolume(partition, path, partition->ContentName());
	} else {
		// see if we got a dev_t

		dev_t device;
		if (message->FindInt32("device_id", &device) != B_OK)
			return;

		BVolume volume(device);
		status_t status = volume.InitCheck();

		char name[B_FILE_NAME_LENGTH];
		if (status == B_OK)
			status = volume.GetName(name);
		if (status < B_OK)
			snprintf(name, sizeof(name), "device:%" B_PRIdDEV, device);

		BPath path;
		if (status == B_OK) {
			BDirectory mountPoint;
			status = volume.GetRootDirectory(&mountPoint);
			if (status == B_OK)
				status = path.SetTo(&mountPoint, ".");
		}

		if (status == B_OK)
			_UnmountAndEjectVolume(NULL, path, name);
	}
}


/**
 * @brief Decomposes a mount_mode enum into individual boolean flags.
 *
 * @param mode    The mount mode to decompose.
 * @param all     Output: set if mode is kAllVolumes.
 * @param bfs     Output: set if mode is kOnlyBFSVolumes.
 * @param restore Output: set if mode is kRestorePreviousVolumes.
 */
void
AutoMounter::_FromMode(mount_mode mode, bool& all, bool& bfs, bool& restore)
{
	all = bfs = restore = false;

	switch (mode) {
		case kAllVolumes:
			all = true;
			break;
		case kOnlyBFSVolumes:
			bfs = true;
			break;
		case kRestorePreviousVolumes:
			restore = true;
			break;

		default:
			break;
	}
}


/**
 * @brief Converts individual boolean flags into a mount_mode enum value.
 *
 * @param all     If true, returns kAllVolumes.
 * @param bfs     If true, returns kOnlyBFSVolumes.
 * @param restore If true, returns kRestorePreviousVolumes.
 * @return The corresponding mount_mode, or kNoVolumes if all flags are false.
 */
mount_mode
AutoMounter::_ToMode(bool all, bool bfs, bool restore)
{
	if (all)
		return kAllVolumes;
	if (bfs)
		return kOnlyBFSVolumes;
	if (restore)
		return kRestorePreviousVolumes;

	return kNoVolumes;
}


/**
 * @brief Reads automounter settings from the user settings file.
 *
 * Opens or creates the prefs file, unflattens the stored BMessage, and
 * updates the mount modes and eject preference from it.
 */
void
AutoMounter::_ReadSettings()
{
	BPath directoryPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &directoryPath, true)
		!= B_OK) {
		return;
	}

	BPath path(directoryPath);
	path.Append(kMountServerSettings);
	fPrefsFile.SetTo(path.Path(), O_RDWR);

	if (fPrefsFile.InitCheck() != B_OK) {
		// no prefs file yet, create a new one

		BDirectory dir(directoryPath.Path());
		dir.CreateFile(kMountServerSettings, &fPrefsFile);
		return;
	}

	ssize_t settingsSize = (ssize_t)fPrefsFile.Seek(0, SEEK_END);
	if (settingsSize == 0)
		return;

	ASSERT(settingsSize != 0);
	char *buffer = new(std::nothrow) char[settingsSize];
	if (buffer == NULL) {
		PRINT(("error writing automounter settings, out of memory\n"));
		return;
	}

	fPrefsFile.Seek(0, 0);
	if (fPrefsFile.Read(buffer, (size_t)settingsSize) != settingsSize) {
		PRINT(("error reading automounter settings\n"));
		delete [] buffer;
		return;
	}

	BMessage message('stng');
	status_t result = message.Unflatten(buffer);
	if (result != B_OK) {
		PRINT(("error %s unflattening automounter settings, size %" B_PRIdSSIZE "\n",
			strerror(result), settingsSize));
		delete [] buffer;
		return;
	}

	delete [] buffer;

	// update flags and modes from the message
	_UpdateSettingsFromMessage(&message);
	// copy the previously mounted partitions
	fSettings = message;
}


/**
 * @brief Persists the current automounter settings to the user settings file.
 */
void
AutoMounter::_WriteSettings()
{
	if (fPrefsFile.InitCheck() != B_OK)
		return;

	BMessage message('stng');
	_GetSettings(&message);

	ssize_t settingsSize = message.FlattenedSize();

	char* buffer = new(std::nothrow) char[settingsSize];
	if (buffer == NULL) {
		PRINT(("error writing automounter settings, out of memory\n"));
		return;
	}

	status_t result = message.Flatten(buffer, settingsSize);

	fPrefsFile.Seek(0, SEEK_SET);
	fPrefsFile.SetSize(0);

	result = fPrefsFile.Write(buffer, (size_t)settingsSize);
	if (result != settingsSize)
		PRINT(("error writing automounter settings, %s\n", strerror(result)));

	delete [] buffer;
}


/**
 * @brief Updates mount modes and eject preference from a settings BMessage.
 *
 * @param message The message containing automounter parameter fields.
 */
void
AutoMounter::_UpdateSettingsFromMessage(BMessage* message)
{
	// auto mounter settings

	bool all, bfs, restore;
	if (message->FindBool("autoMountAll", &all) != B_OK)
		all = true;
	if (message->FindBool("autoMountAllBFS", &bfs) != B_OK)
		bfs = false;

	fRemovableMode = _ToMode(all, bfs, false);

	// initial mount settings

	if (message->FindBool("initialMountAll", &all) != B_OK)
		all = false;
	if (message->FindBool("initialMountAllBFS", &bfs) != B_OK)
		bfs = false;
	if (message->FindBool("initialMountRestore", &restore) != B_OK)
		restore = true;

	fNormalMode = _ToMode(all, bfs, restore);

	// eject settings
	bool eject;
	if (message->FindBool("ejectWhenUnmounting", &eject) == B_OK)
		fEjectWhenUnmounting = eject;
}


/**
 * @brief Populates a BMessage with the current settings and mounted partition archive.
 *
 * @param message The output message to fill with current settings.
 */
void
AutoMounter::_GetSettings(BMessage *message)
{
	message->MakeEmpty();

	bool all, bfs, restore;

	_FromMode(fNormalMode, all, bfs, restore);
	message->AddBool("initialMountAll", all);
	message->AddBool("initialMountAllBFS", bfs);
	message->AddBool("initialMountRestore", restore);

	_FromMode(fRemovableMode, all, bfs, restore);
	message->AddBool("autoMountAll", all);
	message->AddBool("autoMountAllBFS", bfs);

	message->AddBool("ejectWhenUnmounting", fEjectWhenUnmounting);

	// Save mounted volumes so we can optionally mount them on next
	// startup
	ArchiveVisitor visitor(*message);
	BDiskDeviceRoster().VisitEachMountedPartition(&visitor);
}


/**
 * @brief Determines the mount flags for a partition, possibly asking the user.
 *
 * For non-BFS partitions on writable media, shows a dialog suggesting
 * read-only mounting for data safety.
 *
 * @param partition The partition about to be mounted.
 * @param _flags    Output: the mount flags to use.
 * @return @c true if mounting should proceed, @c false if the user cancelled.
 */
/*static*/ bool
AutoMounter::_SuggestMountFlags(const BPartition* partition, uint32* _flags)
{
	uint32 mountFlags = 0;

	bool askReadOnly = true;

	if (partition->ContentType() != NULL
		&& strcmp(partition->ContentType(), kPartitionTypeBFS) == 0) {
		askReadOnly = false;
	}

	BDiskSystem diskSystem;
	status_t status = partition->GetDiskSystem(&diskSystem);
	if (status == B_OK && !diskSystem.SupportsWriting())
		askReadOnly = false;

	if (partition->IsReadOnly())
		askReadOnly = false;

	if (askReadOnly && ((BServer*)be_app)->InitGUIContext() != B_OK) {
		// Mount read-only, just to be safe.
		mountFlags |= B_MOUNT_READ_ONLY;
		askReadOnly = false;
	}

	if (askReadOnly) {
		// Suggest to the user to mount read-only until Haiku is more mature.
		BString string;
		string.SetToFormat(B_TRANSLATE("Mounting volume '%s'\n\n"),
			partition->ContentName().String());

		// TODO: Use distro name instead of "Haiku"...
		string << B_TRANSLATE("The file system on this volume is not the "
			"Be file system. It is recommended to mount it in read-only "
			"mode, to prevent unintentional data loss because of bugs "
			"in Haiku.");

		BAlert* alert = new BAlert(B_TRANSLATE("Mount warning"),
			string.String(), B_TRANSLATE("Mount read/write"),
			B_TRANSLATE("Cancel"), B_TRANSLATE("Mount read-only"),
			B_WIDTH_FROM_WIDEST, B_WARNING_ALERT);
		alert->SetShortcut(1, B_ESCAPE);
		int32 choice = alert->Go();
		switch (choice) {
			case 0:
				break;
			case 1:
				return false;
			case 2:
				mountFlags |= B_MOUNT_READ_ONLY;
				break;
		}
	}

	*_flags = mountFlags;
	return true;
}


// #pragma mark -


/**
 * @brief Entry point for the mount server process.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on normal exit.
 */
int
main(int argc, char* argv[])
{
	AutoMounter app;

	app.Run();
	return 0;
}


