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
 *   Distributed under the terms of the Be Sample Code License.
 */


/**
 * @file TrashWatcher.cpp
 * @brief BTrashWatcher monitors Trash directories and updates the Trash icon.
 *
 * BTrashWatcher is a BLooper that uses the node monitor to watch every
 * Trash directory across all mounted persistent volumes.  When items are
 * added to or removed from Trash it updates the icon on the boot volume's
 * Trash directory node to reflect the current full/empty state.
 *
 * @see BLooper, TTracker, FSGetTrashDir
 */


#include "TrashWatcher.h"

#include <string.h>

#include <Debug.h>
#include <Directory.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include "Attributes.h"
#include "Bitmaps.h"
#include "FSUtils.h"
#include "Tracker.h"


//	 #pragma mark - BTrashWatcher


/**
 * @brief Construct a BTrashWatcher and begin watching all Trash directories.
 *
 * Creates any missing Trash directories, registers node-monitor watches
 * on each one, checks the initial full/empty state, and starts watching
 * for volume mounts.
 */
BTrashWatcher::BTrashWatcher()
	:
	BLooper("TrashWatcher", B_LOW_PRIORITY),
	fTrashNodeList(20)
{
	FSCreateTrashDirs();
	WatchTrashDirs();
	fTrashFull = CheckTrashDirs();
	UpdateTrashIcon();

	// watch volumes
	TTracker::WatchNode(0, B_WATCH_MOUNT, this);
}


/**
 * @brief Destructor; stops all node-monitor watches.
 */
BTrashWatcher::~BTrashWatcher()
{
	stop_watching(this);
}


/**
 * @brief Test whether a node_ref belongs to a watched Trash directory.
 *
 * @param testNode  Node reference to look up in the internal list.
 * @return true if \a testNode matches any registered Trash directory.
 */
bool
BTrashWatcher::IsTrashNode(const node_ref* testNode) const
{
	int32 count = fTrashNodeList.CountItems();
	for (int32 index = 0; index < count; index++) {
		node_ref* nref = fTrashNodeList.ItemAt(index);
		if (nref->node == testNode->node && nref->device == testNode->device)
			return true;
	}

	return false;
}


/**
 * @brief Handle node-monitor and volume-mount/unmount messages.
 *
 * Responds to B_ENTRY_CREATED, B_ENTRY_REMOVED, B_ENTRY_MOVED,
 * B_DEVICE_MOUNTED, and B_DEVICE_UNMOUNTED opcodes to keep the Trash
 * icon state in sync.
 *
 * @param message  Incoming BMessage from the node monitor or volume roster.
 */
void
BTrashWatcher::MessageReceived(BMessage* message)
{
	if (message->what != B_NODE_MONITOR) {
		_inherited::MessageReceived(message);
		return;
	}

	switch (message->GetInt32("opcode", 0)) {
		case B_ENTRY_CREATED:
			if (!fTrashFull) {
				fTrashFull = true;
				UpdateTrashIcon();
			}
			break;

		case B_ENTRY_MOVED:
		{
			// allow code to fall through if move is from/to trash
			// but do nothing for moves in the same directory
			ino_t toDir;
			ino_t fromDir;
			message->FindInt64("from directory", &fromDir);
			message->FindInt64("to directory", &toDir);
			if (fromDir == toDir)
				break;
		}
		// fall-through
		case B_DEVICE_UNMOUNTED:
		case B_ENTRY_REMOVED:
		{
			bool full = CheckTrashDirs();
			if (fTrashFull != full) {
				fTrashFull = full;
				UpdateTrashIcon();
			}
			break;
		}
		// We should handle DEVICE_UNMOUNTED here too to remove trash

		case B_DEVICE_MOUNTED:
		{
			dev_t device;
			BDirectory trashDir;
			if (message->FindInt32("new device", &device) == B_OK
				&& FSGetTrashDir(&trashDir, device) == B_OK) {
				node_ref trashNode;
				trashDir.GetNodeRef(&trashNode);
				TTracker::WatchNode(&trashNode, B_WATCH_DIRECTORY, this);
				fTrashNodeList.AddItem(new node_ref(trashNode));

				// Check if the new volume has anything trashed.
				if (CheckTrashDirs() && !fTrashFull) {
					fTrashFull = true;
					UpdateTrashIcon();
				}
			}
			break;
		}
	}
}


/**
 * @brief Write the appropriate Trash icon attributes to the boot-volume Trash directory.
 *
 * Loads either the full-trash or empty-trash icon from Tracker resources and
 * writes it as a vector icon (or large/mini bitmap fallback) onto the Trash
 * directory node, causing the desktop to refresh the icon immediately.
 */
void
BTrashWatcher::UpdateTrashIcon()
{
	// only update Trash icon attributes on boot volume
	BPath path;
	if (find_directory(B_TRASH_DIRECTORY, &path) != B_OK)
		return;

	BDirectory trashDir(path.Path());

	// pull out the icons for the current trash state from resources
	// and apply them onto the trash directory node
	int32 id = fTrashFull ? R_TrashFullIcon : R_TrashIcon;
	size_t size = 0;
	const void* data = GetTrackerResources()->LoadResource(B_VECTOR_ICON_TYPE, id, &size);

	if (data != NULL && size > 0) {
		// write vector icon attribute
		trashDir.WriteAttr(kAttrIcon, B_VECTOR_ICON_TYPE, 0, data, size);
	} else {
		// write large and mini icon attributes
		data = GetTrackerResources()->LoadResource('ICON', id, &size);
		if (data != NULL && size > 0)
			trashDir.WriteAttr(kAttrLargeIcon, 'ICON', 0, data, size);

		data = GetTrackerResources()->LoadResource('MICN', id, &size);
		if (data != NULL && size > 0)
			trashDir.WriteAttr(kAttrMiniIcon, 'MICN', 0, data, size);
	}
}


/**
 * @brief Register node-monitor watches on every writable volume's Trash directory.
 *
 * Iterates all mounted, persistent, non-read-only volumes and installs a
 * B_WATCH_DIRECTORY watch on each Trash directory found.
 */
void
BTrashWatcher::WatchTrashDirs()
{
	BVolumeRoster volRoster;
	volRoster.Rewind();
	BVolume volume;
	while (volRoster.GetNextVolume(&volume) == B_OK) {
		if (volume.IsReadOnly() || !volume.IsPersistent() || volume.Capacity() == 0)
			continue;

		BDirectory trashDir;
		if (FSGetTrashDir(&trashDir, volume.Device()) == B_OK) {
			node_ref trashNode;
			trashDir.GetNodeRef(&trashNode);
			watch_node(&trashNode, B_WATCH_DIRECTORY, this);
			fTrashNodeList.AddItem(new node_ref(trashNode));
		}
	}
}


/**
 * @brief Scan all Trash directories and return whether any contain items.
 *
 * @return true if at least one Trash directory on any volume is non-empty.
 */
bool
BTrashWatcher::CheckTrashDirs()
{
	BVolumeRoster volRoster;
	volRoster.Rewind();
	BVolume	volume;
	while (volRoster.GetNextVolume(&volume) == B_OK) {
		if (volume.IsReadOnly() || !volume.IsPersistent() || volume.Capacity() == 0)
			continue;

		BDirectory trashDir;
		if (FSGetTrashDir(&trashDir, volume.Device()) == B_OK) {
			trashDir.Rewind();
			// GetNextRef() is a bit faster than GetNextEntry()
			entry_ref ref;
			if (trashDir.GetNextRef(&ref) == B_OK)
				return true;
		}
	}

	return false;
}
