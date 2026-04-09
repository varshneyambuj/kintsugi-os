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
 *   Copyright 2007-2008, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Lotz <mmlr@mlotz.ch>
 */


/**
 * @file USBRoster.cpp
 * @brief USB device change notification and enumeration for the Device Kit
 *
 * Implements BUSBRoster and its private helper classes (WatchedEntry and
 * RosterLooper). BUSBRoster monitors /dev/bus/usb via the node monitor and
 * calls DeviceAdded() / DeviceRemoved() on its subclass as devices appear
 * and disappear. WatchedEntry tracks individual directory entries and their
 * corresponding BUSBDevice objects.
 *
 * @see USBDevice.cpp
 */


#include <USBKit.h>
#include <Directory.h>
#include <Entry.h>
#include <Looper.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <stdio.h>
#include <string.h>
#include <new>


/**
 * @brief Watches a single directory entry (file or directory) in the USB tree.
 *
 * If the entry is a directory, recursively creates WatchedEntry children for
 * its contents and registers a node monitor on it. If the entry is a device
 * file (not named "raw"), creates a BUSBDevice and calls DeviceAdded().
 */
class WatchedEntry {
public:
							WatchedEntry(BUSBRoster *roster,
								BMessenger *messenger, entry_ref *ref);
							~WatchedEntry();

		bool				EntryCreated(entry_ref *ref);
		bool				EntryRemoved(ino_t node);

private:
		BUSBRoster *		fRoster;
		BMessenger *		fMessenger;

		node_ref			fNode;
		bool				fIsDirectory;
		BUSBDevice *		fDevice;

		WatchedEntry *		fEntries;
		WatchedEntry *		fLink;
};


/**
 * @brief BLooper that owns the USB tree root WatchedEntry and dispatches
 *     node monitor messages to it.
 */
class RosterLooper : public BLooper {
public:
							RosterLooper(BUSBRoster *roster);

		void				Stop();

virtual	void				MessageReceived(BMessage *message);

private:
		BUSBRoster *		fRoster;
		WatchedEntry *		fRoot;
		BMessenger *		fMessenger;
};


/**
 * @brief Constructs a WatchedEntry for the given entry_ref.
 *
 * If \a ref points to a directory, starts node monitoring on it and
 * recursively constructs WatchedEntry children. If it points to a regular
 * file (and is not the "raw" pseudo-entry), creates a BUSBDevice and calls
 * BUSBRoster::DeviceAdded().
 *
 * @param roster The BUSBRoster to notify on device arrivals.
 * @param messenger Messenger targeting the RosterLooper for node monitor msgs.
 * @param ref The filesystem entry to watch.
 */
WatchedEntry::WatchedEntry(BUSBRoster *roster, BMessenger *messenger,
	entry_ref *ref)
	:	fRoster(roster),
		fMessenger(messenger),
		fIsDirectory(false),
		fDevice(NULL),
		fEntries(NULL),
		fLink(NULL)
{
	BEntry entry(ref);
	entry.GetNodeRef(&fNode);

	BDirectory directory;
	if (entry.IsDirectory() && directory.SetTo(ref) >= B_OK) {
		fIsDirectory = true;

		while(directory.GetNextEntry(&entry) >= B_OK) {
			if (entry.GetRef(ref) < B_OK)
				continue;

			WatchedEntry *child = new(std::nothrow) WatchedEntry(fRoster,
				fMessenger, ref);
			if (child == NULL)
				continue;

			child->fLink = fEntries;
			fEntries = child;
		}

		watch_node(&fNode, B_WATCH_DIRECTORY, *fMessenger);
	} else {
		// filter pseudoentry that only handles ioctls
		if (strncmp(ref->name, "raw", 3) == 0)
			return;

		BPath path;
		entry.GetPath(&path);
		fDevice = new(std::nothrow) BUSBDevice(path.Path());
		if (fDevice != NULL) {
			if (fRoster->DeviceAdded(fDevice) != B_OK) {
				delete fDevice;
				fDevice = NULL;
			}
		}
	}
}


/**
 * @brief Destroys the WatchedEntry.
 *
 * For directories, stops node monitoring and recursively destroys all child
 * WatchedEntry objects. For device entries, calls BUSBRoster::DeviceRemoved()
 * and deletes the BUSBDevice.
 */
WatchedEntry::~WatchedEntry()
{
	if (fIsDirectory) {
		watch_node(&fNode, B_STOP_WATCHING, *fMessenger);

		WatchedEntry *child = fEntries;
		while (child) {
			WatchedEntry *next = child->fLink;
			delete child;
			child = next;
		}
	}

	if (fDevice) {
		fRoster->DeviceRemoved(fDevice);
		delete fDevice;
	}
}


/**
 * @brief Handles a B_ENTRY_CREATED node monitor event.
 *
 * Walks the directory tree to find the parent entry matching \a ref's
 * directory inode, then creates a new WatchedEntry child for the new entry.
 *
 * @param ref The entry_ref of the newly created entry.
 * @return \c true if the entry was handled by this WatchedEntry or a child.
 */
bool
WatchedEntry::EntryCreated(entry_ref *ref)
{
	if (!fIsDirectory)
		return false;

	if (ref->directory != fNode.node) {
		WatchedEntry *child = fEntries;
		while (child) {
			if (child->EntryCreated(ref))
				return true;
			child = child->fLink;
		}

		return false;
	}

	WatchedEntry *child = new(std::nothrow) WatchedEntry(fRoster, fMessenger,
		ref);
	if (child == NULL)
		return false;

	child->fLink = fEntries;
	fEntries = child;
	return true;
}


/**
 * @brief Handles a B_ENTRY_REMOVED node monitor event.
 *
 * Searches this entry's children for the one matching \a node and removes it,
 * delegating to child directories as needed.
 *
 * @param node The inode number of the removed entry.
 * @return \c true if the entry was found and removed.
 */
bool
WatchedEntry::EntryRemoved(ino_t node)
{
	if (!fIsDirectory)
		return false;

	WatchedEntry *child = fEntries;
	WatchedEntry *lastChild = NULL;
	while (child) {
		if (child->fNode.node == node) {
			if (lastChild)
				lastChild->fLink = child->fLink;
			else
				fEntries = child->fLink;

			delete child;
			return true;
		}

		if (child->EntryRemoved(node))
			return true;

		lastChild = child;
		child = child->fLink;
	}

	return false;
}


/**
 * @brief Constructs the RosterLooper and begins watching /dev/bus/usb.
 *
 * Runs the looper, creates a BMessenger targeting it, then creates the root
 * WatchedEntry for /dev/bus/usb to enumerate all currently connected devices.
 *
 * @param roster The BUSBRoster to notify on device arrivals and removals.
 */
RosterLooper::RosterLooper(BUSBRoster *roster)
	:	BLooper("BUSBRoster looper"),
		fRoster(roster),
		fRoot(NULL),
		fMessenger(NULL)
{
	BEntry entry("/dev/bus/usb");
	if (!entry.Exists()) {
		fprintf(stderr, "USBKit: usb_raw not published\n");
		return;
	}

	Run();
	fMessenger = new(std::nothrow) BMessenger(this);
	if (fMessenger == NULL)
		return;

	if (Lock()) {
		entry_ref ref;
		entry.GetRef(&ref);
		fRoot = new(std::nothrow) WatchedEntry(fRoster, fMessenger, &ref);
		Unlock();
	}
}


/**
 * @brief Stops the looper, destroying the root WatchedEntry before quitting.
 *
 * Must be called with the looper unlocked; acquires the lock internally.
 */
void
RosterLooper::Stop()
{
	Lock();
	delete fRoot;
	Quit();
}


/**
 * @brief Dispatches node monitor messages to the appropriate WatchedEntry.
 *
 * Handles B_ENTRY_CREATED (routes to WatchedEntry::EntryCreated()) and
 * B_ENTRY_REMOVED (routes to WatchedEntry::EntryRemoved()) opcodes.
 *
 * @param message The node monitor BMessage received by the looper.
 */
void
RosterLooper::MessageReceived(BMessage *message)
{
	int32 opcode;
	if (message->FindInt32("opcode", &opcode) < B_OK)
		return;

	switch (opcode) {
		case B_ENTRY_CREATED: {
			dev_t device;
			ino_t directory;
			const char *name;
			if (message->FindInt32("device", &device) < B_OK
				|| message->FindInt64("directory", &directory) < B_OK
				|| message->FindString("name", &name) < B_OK)
				break;

			entry_ref ref(device, directory, name);
			fRoot->EntryCreated(&ref);
			break;
		}

		case B_ENTRY_REMOVED: {
			ino_t node;
			if (message->FindInt64("node", &node) < B_OK)
				break;

			fRoot->EntryRemoved(node);
			break;
		}
	}
}


/** @brief Constructs a BUSBRoster in the stopped state. */
BUSBRoster::BUSBRoster()
	:	fLooper(NULL)
{
}


/**
 * @brief Destroys the BUSBRoster, stopping the monitoring looper if running.
 */
BUSBRoster::~BUSBRoster()
{
	Stop();
}


/**
 * @brief Starts monitoring /dev/bus/usb for device arrivals and removals.
 *
 * Creates the RosterLooper, which immediately enumerates all currently
 * connected USB devices. Has no effect if already started.
 */
void
BUSBRoster::Start()
{
	if (fLooper)
		return;

	fLooper = new(std::nothrow) RosterLooper(this);
}


/**
 * @brief Stops USB device monitoring and destroys the looper.
 *
 * Has no effect if the roster is not currently running.
 */
void
BUSBRoster::Stop()
{
	if (!fLooper)
		return;

	((RosterLooper *)fLooper)->Stop();
	fLooper = NULL;
}


//	#pragma mark - FBC protection


void BUSBRoster::_ReservedUSBRoster1() {};
void BUSBRoster::_ReservedUSBRoster2() {};
void BUSBRoster::_ReservedUSBRoster3() {};
void BUSBRoster::_ReservedUSBRoster4() {};
void BUSBRoster::_ReservedUSBRoster5() {};
