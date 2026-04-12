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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the OpenTracker License.
 */


/**
 * @file MountMenu.cpp
 * @brief Dynamic context menu for mounting and unmounting volumes.
 *
 * MountMenu is a BMenu subclass that rebuilds its item list each time it is
 * opened, enumerating disk-device partitions via BDiskDeviceList and (when
 * SHOW_NETWORK_VOLUMES is defined) shared volumes from BVolumeRoster.  Each
 * item sends a kMountVolume or kUnmountVolume message.
 *
 * @see BDiskDeviceList, BVolumeRoster
 */


#include "MountMenu.h"

#include <Catalog.h>
#include <ControlLook.h>
#include <Debug.h>
#include <Locale.h>
#include <MenuItem.h>
#include <Mime.h>
#include <InterfaceDefs.h>
#include <VolumeRoster.h>
#include <Volume.h>

#include <fs_info.h>

#include "Commands.h"
#include "IconMenuItem.h"
#include "Tracker.h"
#include "Bitmaps.h"

#include <DiskDevice.h>
#include <DiskDeviceList.h>

#define SHOW_NETWORK_VOLUMES

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MountMenu"


class AddMenuItemVisitor : public BDiskDeviceVisitor {
public:
	AddMenuItemVisitor(BMenu* menu);
	virtual ~AddMenuItemVisitor();

	virtual bool Visit(BDiskDevice* device);
	virtual bool Visit(BPartition* partition, int32 level);

private:
	BMenu* fMenu;
};


//	#pragma mark - AddMenuItemVisitor


/**
 * @brief Construct an AddMenuItemVisitor that appends items to @p menu.
 *
 * @param menu  The BMenu to which partition items will be added.
 */
AddMenuItemVisitor::AddMenuItemVisitor(BMenu* menu)
	:
	fMenu(menu)
{
}


/**
 * @brief Destroy the AddMenuItemVisitor.
 */
AddMenuItemVisitor::~AddMenuItemVisitor()
{
}


/**
 * @brief Visit a BDiskDevice by forwarding to the partition-level Visit().
 *
 * @param device  The disk device to visit.
 * @return False to continue traversal.
 */
bool
AddMenuItemVisitor::Visit(BDiskDevice* device)
{
	return Visit(device, 0);
}


/**
 * @brief Add a menu item for a mountable/unmountable partition.
 *
 * Skips partitions that do not contain a file system.  Builds the item label
 * from the partition's content name, name, or a size/type string.  Attaches
 * a kMountVolume or kUnmountVolume message and marks currently-mounted
 * partitions; disables the boot-volume item.
 *
 * @param partition  The partition to inspect.
 * @param level      Nesting depth from BDiskDeviceVisitor (unused).
 * @return False to continue traversal.
 */
bool
AddMenuItemVisitor::Visit(BPartition* partition, int32 level)
{
	if (!partition->ContainsFileSystem())
		return false;

	// get name (and eventually the type)
	BString name = partition->ContentName();
	if (name.Length() == 0) {
		name = partition->Name();
		if (name.Length() == 0) {
			const char* type = partition->ContentType();
			if (type == NULL)
				return false;

			uint32 divisor = 1UL << 30;
			char unit = 'G';
			if (partition->Size() < divisor) {
				divisor = 1UL << 20;
				unit = 'M';
			}

			name.SetToFormat("(%.1f %cB %s)",
				1.0 * partition->Size() / divisor, unit, type);
		}
	}

	// get icon
	BBitmap* icon = new BBitmap(BRect(BPoint(0, 0), be_control_look->ComposeIconSize(B_MINI_ICON)),
		B_RGBA32);
	if (partition->GetIcon(icon, B_MINI_ICON) != B_OK) {
		delete icon;
		icon = NULL;
	}

	BMessage* message = new BMessage(partition->IsMounted() ?
		kUnmountVolume : kMountVolume);
	message->AddInt32("id", partition->ID());

	// TODO: for now, until we actually have disk device icons
	BMenuItem* item;
	if (icon != NULL)
		item = new IconMenuItem(name.String(), message, icon);
	else
		item = new BMenuItem(name.String(), message);
	if (partition->IsMounted()) {
		item->SetMarked(true);

		BVolume volume;
		if (partition->GetVolume(&volume) == B_OK) {
			BVolume bootVolume;
			BVolumeRoster().GetBootVolume(&bootVolume);
			if (volume == bootVolume)
				item->SetEnabled(false);
		}
	}

	fMenu->AddItem(item);
	return false;
}


//	#pragma mark - MountMenu


/**
 * @brief Construct a MountMenu with the given label.
 *
 * @param name  The menu label string.
 */
MountMenu::MountMenu(const char* name)
	: BMenu(name)
{
}


/**
 * @brief Rebuild the menu's item list from the current set of disk devices.
 *
 * Removes all existing items, then uses AddMenuItemVisitor to enumerate
 * partitions and (when enabled) shared network volumes.  Always returns
 * false to signal that the dynamic build is complete.
 *
 * @return False — item building is finished in a single call.
 */
bool
MountMenu::AddDynamicItem(add_state)
{
	// remove old items
	for (;;) {
		BMenuItem* item = RemoveItem((int32)0);
		if (item == NULL)
			break;
		delete item;
	}

	BDiskDeviceList devices;
	status_t status = devices.Fetch();
	if (status == B_OK) {
		AddMenuItemVisitor visitor(this);
		devices.VisitEachPartition(&visitor);
	}

#ifdef SHOW_NETWORK_VOLUMES
	// iterate the volume roster and look for volumes with the
	// 'shared' attributes -- these same volumes will not be returned
	// by the autoMounter because they do not show up in the /dev tree
	BVolumeRoster volumeRoster;
	BVolume volume;
	while (volumeRoster.GetNextVolume(&volume) == B_OK) {
		if (volume.IsShared()) {
			BBitmap* icon = new BBitmap(BRect(0, 0, 15, 15), B_CMAP8);
			fs_info info;
			if (fs_stat_dev(volume.Device(), &info) != B_OK) {
				PRINT(("Cannot get mount menu item icon; bad device ID\n"));
				delete icon;
				continue;
			}
			// Use the shared icon instead of the device icon
			if (get_device_icon(info.device_name, icon, B_MINI_ICON)
					!= B_OK) {
				GetTrackerResources()->GetIconResource(R_ShareIcon,
					B_MINI_ICON, icon);
			}

			BMessage* message = new BMessage(kUnmountVolume);
			message->AddInt32("device_id", volume.Device());
			char volumeName[B_FILE_NAME_LENGTH];
			volume.GetName(volumeName);

			BMenuItem* item = new IconMenuItem(volumeName, message, icon);
			item->SetMarked(true);
			AddItem(item);
		}
	}
#endif	// SHOW_NETWORK_VOLUMES

	AddSeparatorItem();

	BMenuItem* mountAll = new BMenuItem(B_TRANSLATE("Mount all"),
		new BMessage(kMountAllNow));
	AddItem(mountAll);
	BMenuItem* mountSettings = new BMenuItem(
		B_TRANSLATE("Settings" B_UTF8_ELLIPSIS),
		new BMessage(kRunAutomounterSettings));
	AddItem(mountSettings);

	SetTargetForItems(be_app);

	return false;
}
