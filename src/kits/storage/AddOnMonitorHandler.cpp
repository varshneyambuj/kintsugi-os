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
 */

/**
 * @file AddOnMonitorHandler.cpp
 * @brief Implementation of the AddOnMonitorHandler node-monitor handler.
 *
 * AddOnMonitorHandler extends NodeMonitorHandler to watch one or more
 * add-on directories and fire lifecycle callbacks (Created, Enabled,
 * Disabled, Removed) as add-on files appear, disappear, or are renamed.
 * Priority ordering across multiple directories ensures that a higher-priority
 * directory shadows a lower-priority one containing an identically named
 * add-on.
 *
 * @see AddOnMonitor
 */

#include "AddOnMonitorHandler.h"

#include <string.h>
#include <strings.h>

#include <Autolock.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <Path.h>

#include <driver_settings.h>
#include <safemode_defs.h>
#include <syscalls.h>


#ifndef ADD_ON_STABLE_SECONDS
#	define ADD_ON_STABLE_SECONDS 1
#endif


/**
 * @brief Constructs an AddOnMonitorHandler with an optional name.
 *
 * Passes the supplied name (or the default "AddOnMonitorHandler" when NULL)
 * to the NodeMonitorHandler base-class constructor.
 *
 * @param name The BHandler name for this instance, or NULL to use the default.
 */
AddOnMonitorHandler::AddOnMonitorHandler(const char* name)
	:
	NodeMonitorHandler(name != NULL ? name : "AddOnMonitorHandler")
{
}


/**
 * @brief Destructor. Stops watching all monitored directories and their entries.
 *
 * Iterates every known directory and its entries, calling watch_node() with
 * B_STOP_WATCHING so that stale node-monitor subscriptions are not left
 * behind after the handler is destroyed.
 */
AddOnMonitorHandler::~AddOnMonitorHandler()
{
	// TODO: Actually calling watch_node() here should be too late, since we
	// are likely not attached to a looper anymore, and thus consitute no valid
	// BMessenger at this time.
	DirectoryList::iterator it = fDirectories.begin();
	for (; it != fDirectories.end(); it++) {
		EntryList::iterator eiter = it->entries.begin();
		for (; eiter != it->entries.end(); eiter++)
			watch_node(&eiter->addon_nref, B_STOP_WATCHING, this);
		watch_node(&it->nref, B_STOP_WATCHING, this);
	}
}


/**
 * @brief Handles incoming messages, processing B_PULSE to drain the pending
 *        entry queue.
 *
 * When a B_PULSE message is received, _HandlePendingEntries() is called to
 * promote any sufficiently stable pending entries into active add-ons. All
 * other messages are forwarded to the inherited handler.
 *
 * @param msg The incoming BMessage to process.
 */
void
AddOnMonitorHandler::MessageReceived(BMessage* msg)
{
	if (msg->what == B_PULSE)
		_HandlePendingEntries();

	inherited::MessageReceived(msg);
}


/**
 * @brief Registers a directory node for add-on monitoring.
 *
 * Opens the directory identified by @p nref, registers a B_WATCH_DIRECTORY
 * node monitor on it, and enqueues all existing entries as pending. The
 * looper is locked for the duration of the call to prevent races with
 * concurrent node-monitor notifications or pulse messages.
 *
 * @param nref Pointer to the node_ref of the directory to watch.
 * @param sync If true, pending entries are processed immediately after
 *             the initial scan rather than waiting for the next pulse.
 * @return B_OK on success, or an error code if the directory could not be
 *         opened or the node monitor could not be started.
 */
status_t
AddOnMonitorHandler::AddDirectory(const node_ref* nref, bool sync)
{
	// Keep the looper thread locked, since this method is likely to be called
	// in a thread other than the looper thread. Otherwise we may access the
	// lists concurrently with the looper thread, when node monitor
	// notifications arrive while we are still adding initial entries from the
	// directory, or (much more likely) if the looper thread handles a pulse
	// message and wants to process pending entries while we are still adding
	// them.
	BAutolock _(Looper());

	// ignore directories added twice
	DirectoryList::iterator it = fDirectories.begin();
	for (; it != fDirectories.end(); it++) {
		if (it->nref == *nref)
			return B_OK;
	}

	BDirectory directory(nref);
	status_t status = directory.InitCheck();
	if (status != B_OK)
		return status;

	status = watch_node(nref, B_WATCH_DIRECTORY, this);
	if (status != B_OK)
		return status;

	add_on_directory_info dirInfo;
	dirInfo.nref = *nref;
	fDirectories.push_back(dirInfo);

	add_on_entry_info entryInfo;
	entryInfo.dir_nref = *nref;

	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		if (entry.GetName(entryInfo.name) != B_OK
			|| entry.GetNodeRef(&entryInfo.nref) != B_OK) {
			continue;
		}

		fPendingEntries.push_back(entryInfo);
	}

	if (sync)
		_HandlePendingEntries();

	return B_OK;
}


/**
 * @brief Registers standard system add-on directories for a given leaf path.
 *
 * Iterates the well-known add-on directory hierarchy (user non-packaged, user,
 * system non-packaged, system) appending @p leafPath to each, and calls
 * AddDirectory() for every path that resolves to an existing directory.
 * Safemode kernel options are honoured: if user add-ons are disabled the user
 * directories are skipped; if safe mode is active only B_SYSTEM_ADDONS_DIRECTORY
 * is used.
 *
 * @param leafPath The relative path under each add-on base directory to watch
 *                 (e.g. "input_server/devices").
 * @return B_OK on success, or the first error code returned by AddDirectory().
 */
status_t
AddOnMonitorHandler::AddAddOnDirectories(const char* leafPath)
{
	char parameter[32];
	size_t parameterLength = sizeof(parameter);
	uint32 start = 0;

	const directory_which addOnDirectories[] = {
		B_USER_NONPACKAGED_ADDONS_DIRECTORY,
		B_USER_ADDONS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
		B_SYSTEM_ADDONS_DIRECTORY
	};

	if (_kern_get_safemode_option(B_SAFEMODE_DISABLE_USER_ADD_ONS, parameter,
			&parameterLength) == B_OK) {
		if (!strcasecmp(parameter, "enabled") || !strcasecmp(parameter, "on")
			|| !strcasecmp(parameter, "true") || !strcasecmp(parameter, "yes")
			|| !strcasecmp(parameter, "enable") || !strcmp(parameter, "1")) {
			// skip user add on directories
			start = 2;
		}
	}

	if (_kern_get_safemode_option(B_SAFEMODE_SAFE_MODE, parameter,
			&parameterLength) == B_OK) {
		if (!strcasecmp(parameter, "enabled") || !strcasecmp(parameter, "on")
			|| !strcasecmp(parameter, "true") || !strcasecmp(parameter, "yes")
			|| !strcasecmp(parameter, "enable") || !strcmp(parameter, "1")) {
			// safe mode, only B_SYSTEM_ADDONS_DIRECTORY is used
			start = 3;
		}
	}

	for (uint32 i = start;
			i < sizeof(addOnDirectories) / sizeof(directory_which); i++) {
		BDirectory directory;
		node_ref nodeRef;
		BPath path;
		if (find_directory(addOnDirectories[i], &path) == B_OK
			&& path.Append(leafPath) == B_OK
			&& directory.SetTo(path.Path()) == B_OK
			&& directory.GetNodeRef(&nodeRef) == B_OK) {
			status_t result = this->AddDirectory(&nodeRef);
			if (result != B_OK)
				return result;
		}
	}

	return B_OK;
}


//	#pragma mark - AddOnMonitorHandler hooks


/**
 * @brief Hook called when a new add-on entry has been created.
 *
 * The default implementation does nothing. Subclasses should override this
 * method to react to newly discovered add-on files (e.g. load the add-on).
 *
 * @param entryInfo Pointer to the entry information for the created add-on.
 */
void
AddOnMonitorHandler::AddOnCreated(const add_on_entry_info* entryInfo)
{

}


/**
 * @brief Hook called when an add-on becomes the active (enabled) version.
 *
 * Fired after AddOnCreated() when the new add-on is not shadowed by a
 * higher-priority directory, or when a previously shadowing add-on is
 * removed. The default implementation does nothing.
 *
 * @param entryInfo Pointer to the entry information for the enabled add-on.
 */
void
AddOnMonitorHandler::AddOnEnabled(const add_on_entry_info* entryInfo)
{
}


/**
 * @brief Hook called when an active add-on is about to be superseded or
 *        removed.
 *
 * Always paired with a subsequent AddOnEnabled() or AddOnRemoved() call.
 * The default implementation does nothing.
 *
 * @param entryInfo Pointer to the entry information for the disabled add-on.
 */
void
AddOnMonitorHandler::AddOnDisabled(const add_on_entry_info* entryInfo)
{
}


/**
 * @brief Hook called when an add-on entry has been permanently removed.
 *
 * The default implementation does nothing. Subclasses should override this
 * method to unload and release resources associated with the add-on.
 *
 * @param entryInfo Pointer to the entry information for the removed add-on.
 */
void
AddOnMonitorHandler::AddOnRemoved(const add_on_entry_info* entryInfo)
{
}


//	#pragma mark - NodeMonitorHandler hooks


/**
 * @brief NodeMonitorHandler hook invoked when a new filesystem entry appears
 *        in a watched directory.
 *
 * Packages the notification parameters into an add_on_entry_info and appends
 * it to the pending entries queue for stability checking on the next pulse.
 *
 * @param name      The name of the newly created entry.
 * @param directory Inode number of the containing directory.
 * @param device    Device ID of the filesystem.
 * @param node      Inode number of the new entry.
 */
void
AddOnMonitorHandler::EntryCreated(const char* name, ino_t directory,
	dev_t device, ino_t node)
{
	add_on_entry_info entryInfo;
	strlcpy(entryInfo.name, name, sizeof(entryInfo.name));
	make_node_ref(device, node, &entryInfo.nref);
	make_node_ref(device, directory, &entryInfo.dir_nref);
	fPendingEntries.push_back(entryInfo);
}


/**
 * @brief NodeMonitorHandler hook invoked when a filesystem entry is removed
 *        from a watched directory.
 *
 * Purges the entry from the pending list, then from the active directory
 * list. If the entry was the enabled add-on it is disabled and removed, and
 * a lower-priority shadowed add-on with the same name (if any) is enabled.
 *
 * @param name      The name of the removed entry.
 * @param directory Inode number of the containing directory.
 * @param device    Device ID of the filesystem.
 * @param node      Inode number of the removed entry.
 */
void
AddOnMonitorHandler::EntryRemoved(const char* name, ino_t directory,
	dev_t device, ino_t node)
{
	node_ref entryNodeRef;
	make_node_ref(device, node, &entryNodeRef);

	// Search pending entries first, which can simply be discarded
	// We might have this entry in the pending list multiple times,
	// so we search entire list through, even after finding one.
	EntryList::iterator eiter = fPendingEntries.begin();
	while (eiter != fPendingEntries.end()) {
		if (eiter->nref == entryNodeRef)
			eiter = fPendingEntries.erase(eiter);
		else
			eiter++;
	}

	// Find the directory of the entry.
	DirectoryList::iterator diter = fDirectories.begin();
	if (!_FindDirectory(directory, device, diter)) {
		// If it was not found, we're done
		return;
	}

	eiter = diter->entries.begin();
	if (!_FindEntry(entryNodeRef, diter->entries, eiter)) {
		// This must be the directory, but we didn't find the entry.
		return;
	}

	add_on_entry_info info = *eiter;
	watch_node(&entryNodeRef, B_STOP_WATCHING, this);
	diter->entries.erase(eiter);

	// Start at the top again, and search until the directory we found
	// the old add-on in. If we find an add-on with the same name then
	// the old add-on was not enabled. So we deallocate the old add-on and
	// return.
	DirectoryList::iterator diter2 = fDirectories.begin();
	for (; diter2 != diter; diter2++) {
		if (_HasEntry(info.name, diter2->entries)) {
			AddOnRemoved(&info);
			return;
		}
	}

	// An active add-on was removed. We need to disable and then subsequently
	// deallocate it.
	AddOnDisabled(&info);
	AddOnRemoved(&info);

	// Continue searching for an add-on below us. If we find an add-on
	// with the same name, we must enable it.
	for (diter++; diter != fDirectories.end(); diter++) {
		eiter = diter->entries.begin();
		if (_FindEntry(info.name, diter->entries, eiter)) {
			AddOnEnabled(&*eiter);
			break;
		}
	}
}


/**
 * @brief NodeMonitorHandler hook invoked when a filesystem entry is moved or
 *        renamed within the watched directory hierarchy.
 *
 * Handles all combinations of source/destination visibility: move-out
 * (disable and possibly remove), move-in (create and possibly enable), and
 * rename-within (remove old name, add new name, adjust enabled state). When
 * the name stays the same but the containing directory changes the entry is
 * re-created via EntryRemoved() / _EntryCreated().
 *
 * @param name          New name of the entry in the destination directory.
 * @param fromName      Previous name of the entry in the source directory.
 * @param fromDirectory Inode number of the source directory.
 * @param toDirectory   Inode number of the destination directory.
 * @param device        Device ID shared by source and destination directories.
 * @param node          Inode number of the moved entry.
 * @param nodeDevice    Device ID of the moved entry's node.
 */
void
AddOnMonitorHandler::EntryMoved(const char* name, const char* fromName,
	ino_t fromDirectory, ino_t toDirectory, dev_t device, ino_t node,
	dev_t nodeDevice)
{
	node_ref toNodeRef;
	make_node_ref(device, toDirectory, &toNodeRef);

	// Search the "from" and "to" directory in the known directories
	DirectoryList::iterator fromIter = fDirectories.begin();
	bool watchingFromDirectory = _FindDirectory(fromDirectory, device,
		fromIter);

	DirectoryList::iterator toIter = fDirectories.begin();
	bool watchingToDirectory = _FindDirectory(toNodeRef, toIter);

	if (!watchingFromDirectory && !watchingToDirectory) {
		// It seems the notification was for a directory we are not
		// actually watching (at least not by intention).
		return;
	}

	add_on_entry_info info;

	node_ref entryNodeRef;
	make_node_ref(device, node, &entryNodeRef);

	if (!watchingToDirectory) {
		// moved out of our view
		EntryList::iterator eiter = fromIter->entries.begin();
		if (!_FindEntry(entryNodeRef, fromIter->entries, eiter)) {
			// we don't know anything about this entry yet.. ignore it
			return;
		}

		// save the info and remove the entry
		info = *eiter;
		watch_node(&entryNodeRef, B_STOP_WATCHING, this);
		fromIter->entries.erase(eiter);

		// Start at the top again, and search until the from directory.
		// If we find a add-on with the same name then the moved add-on
		// was not enabled.  So we are done.
		DirectoryList::iterator diter = fDirectories.begin();
		for (; diter != fromIter; diter++) {
			eiter = diter->entries.begin();
			if (_FindEntry(info.name, diter->entries, eiter))
				return;
		}

		// finally disable the add-on
		AddOnDisabled(&info);

		// Continue searching for a add-on below us.  If we find a add-on
		// with the same name, we must enable it.
		for (fromIter++; fromIter != fDirectories.end(); fromIter++) {
			eiter = fromIter->entries.begin();
			if (_FindEntry(info.name, fromIter->entries, eiter)) {
				AddOnEnabled(&*eiter);
				return;
			}
		}

		// finally destroy the addon
		AddOnRemoved(&info);

		// done
		return;
	}

	if (!watchingFromDirectory) {
		// moved into our view

		// update the info
		strlcpy(info.name, name, sizeof(info.name));
		info.nref = entryNodeRef;
		info.dir_nref = toNodeRef;

		AddOnCreated(&info);

		// Start at the top again, and search until the to directory.
		// If we find an add-on with the same name then the moved add-on
		// is not to be enabled. So we are done.
		DirectoryList::iterator diter = fDirectories.begin();
		for (; diter != toIter; diter++) {
			if (_HasEntry(info.name, diter->entries)) {
				// The new add-on is being shadowed.
				return;
			}
		}

		// The new add-on should be enabled, but first we check to see
		// if there is an add-on below us. If we find one, we disable it.
		for (diter++ ; diter != fDirectories.end(); diter++) {
			EntryList::iterator eiter = diter->entries.begin();
			if (_FindEntry(info.name, diter->entries, eiter)) {
				AddOnDisabled(&*eiter);
				break;
			}
		}

		// enable the new add-on
		AddOnEnabled(&info);

		// put the new entry into the target directory
		_AddNewEntry(toIter->entries, info);

		// done
		return;
	}

	// The add-on was renamed, or moved within our hierarchy.

	EntryList::iterator eiter = fromIter->entries.begin();
	if (_FindEntry(entryNodeRef, fromIter->entries, eiter)) {
		// save the old info and remove the entry
		info = *eiter;
	} else {
		// If an entry moved from one watched directory into another watched
		// directory, there will be two notifications, and this may be the
		// second. We have handled everything in the first. In that case the
		// entry was already removed from the fromDirectory and added in the
		// toDirectory list.
		return;
	}

	if (strcmp(info.name, name) == 0) {
		// It should be impossible for the name to stay the same, unless the
		// node moved in the watched hierarchy. Handle this case by removing
		// the entry and readding it. TODO: This can temporarily enable add-ons
		// which should in fact stay hidden (moving add-on from home to common
		// folder or vice versa, the system add-on should remain hidden).
		EntryRemoved(name, fromDirectory, device, node);
		info.dir_nref = toNodeRef;
		_EntryCreated(info);
	} else {
		// Erase the entry
		fromIter->entries.erase(eiter);

		// check to see if it was formerly enabled
		bool wasEnabled = true;
		DirectoryList::iterator oldIter = fDirectories.begin();
		for (; oldIter != fromIter; oldIter++) {
			if (_HasEntry(info.name, oldIter->entries)) {
				wasEnabled = false;
				break;
			}
		}

		// If it was enabled, disable it and enable the one under us, if it
		// exists.
		if (wasEnabled) {
			AddOnDisabled(&info);
			for (; oldIter != fDirectories.end(); oldIter++) {
				eiter = oldIter->entries.begin();
				if (_FindEntry(info.name, oldIter->entries, eiter)) {
					AddOnEnabled(&*eiter);
					break;
				}
			}
		}

		// kaboom!
		AddOnRemoved(&info);

		// set up new addon info
		strlcpy(info.name, name, sizeof(info.name));
		info.dir_nref = toNodeRef;

		// presto!
		AddOnCreated(&info);

		// check to see if we are newly enabled
		bool isEnabled = true;
		DirectoryList::iterator newIter = fDirectories.begin();
		for (; newIter != toIter; newIter++) {
			if (_HasEntry(info.name, newIter->entries)) {
				isEnabled = false;
				break;
			}
		}

		// if it is newly enabled, check under us for an enabled one, and
		// disable that first
		if (isEnabled) {
			for (; newIter != fDirectories.end(); newIter++) {
				eiter = newIter->entries.begin();
				if (_FindEntry(info.name, newIter->entries, eiter)) {
					AddOnDisabled(&*eiter);
					break;
				}
			}
			AddOnEnabled(&info);
		}
		// put the new entry into the target directory
		toIter->entries.push_back(info);
	}
}


/**
 * @brief NodeMonitorHandler hook invoked when the stat data of a watched
 *        add-on file changes.
 *
 * Locates the corresponding entry in the active directory list by node_ref
 * and triggers a full reload cycle (Disabled -> Removed -> Created -> Enabled)
 * so that callers can react to in-place updates of an add-on binary.
 *
 * @param node       Inode number of the entry whose stat data changed.
 * @param device     Device ID of the filesystem.
 * @param statFields Bitmask of B_STAT_* flags indicating which stat fields
 *                   were modified.
 */
void
AddOnMonitorHandler::StatChanged(ino_t node, dev_t device, int32 statFields)
{
	// This notification is received for the add-ons themselves.

	// TODO: Add the entry to the pending list, disable/enable it
	// when the modification time remains stable.

	node_ref entryNodeRef;
	make_node_ref(device, node, &entryNodeRef);

	DirectoryList::iterator diter = fDirectories.begin();
	for (; diter != fDirectories.end(); diter++) {
		EntryList::iterator eiter = diter->entries.begin();
		for (; eiter != diter->entries.end(); eiter++) {
			if (eiter->addon_nref == entryNodeRef) {
				// Trigger reloading of the add-on
				const add_on_entry_info* info = &*eiter;
				AddOnDisabled(info);
				AddOnRemoved(info);
				AddOnCreated(info);
				AddOnEnabled(info);
				return;
			}
		}
	}
}


// #pragma mark - private


/**
 * @brief Processes pending entries that have been stable long enough.
 *
 * Iterates fPendingEntries and, for each entry whose modification time is at
 * least ADD_ON_STABLE_SECONDS old, removes it from the pending list and
 * promotes it to an active add-on via _EntryCreated(). Entries whose parent
 * directory is no longer valid or whose name cannot be stat()'d are silently
 * discarded.
 */
void
AddOnMonitorHandler::_HandlePendingEntries()
{
	BDirectory directory;
	EntryList::iterator iter = fPendingEntries.begin();
	while (iter != fPendingEntries.end()) {
		add_on_entry_info info = *iter;

		// Initialize directory, or re-use from previous iteration, if
		// directory node_ref remained the same from the last pending entry.
		node_ref dirNodeRef;
		if (directory.GetNodeRef(&dirNodeRef) != B_OK
			|| dirNodeRef != info.dir_nref) {
			if (directory.SetTo(&info.dir_nref) != B_OK) {
				// invalid directory, discard this pending entry
				iter = fPendingEntries.erase(iter);
				continue;
			}
			dirNodeRef = info.dir_nref;
		}

		struct stat st;
		if (directory.GetStatFor(info.name, &st) != B_OK) {
			// invalid file name, discard this pending entry
			iter = fPendingEntries.erase(iter);
			continue;
		}

		// stat units are seconds, real_time_clock units are seconds
		if (real_time_clock() - st.st_mtime < ADD_ON_STABLE_SECONDS) {
			// entry not stable, skip the entry for this pulse
			iter++;
			continue;
		}

		// we are going to deal with the stable entry, so remove it
		iter = fPendingEntries.erase(iter);

		_EntryCreated(info);
	}
}


/**
 * @brief Inserts a new add-on entry into the active directory list and fires
 *        the appropriate lifecycle hooks.
 *
 * Locates the directory in fDirectories matching info.dir_nref, adds the
 * entry via _AddNewEntry(), then calls AddOnCreated(). Walks the directory
 * list to determine whether the new entry is enabled (not shadowed by a
 * higher-priority directory), disabling any lower-priority entry with the
 * same name before calling AddOnEnabled().
 *
 * @param info Reference to the add_on_entry_info describing the new entry.
 */
void
AddOnMonitorHandler::_EntryCreated(add_on_entry_info& info)
{
	// put the new entry into the directory info
	DirectoryList::iterator diter = fDirectories.begin();
	for (; diter != fDirectories.end(); diter++) {
		if (diter->nref == info.dir_nref) {
			_AddNewEntry(diter->entries, info);
			break;
		}
	}

	// report it
	AddOnCreated(&info);

	// Start at the top again, and search until the directory we put
	// the new add-on in.  If we find an add-on with the same name then
	// the new add-on should not be enabled.
	bool enabled = true;
	DirectoryList::iterator diter2 = fDirectories.begin();
	for (; diter2 != diter; diter2++) {
		if (_HasEntry(info.name, diter2->entries)) {
			enabled = false;
			break;
		}
	}
	if (!enabled)
		return;

	// The new add-on should be enabled, but first we check to see
	// if there is an add-on shadowed by the new one.  If we find one,
	// we disable it.
	for (diter++ ; diter != fDirectories.end(); diter++) {
		EntryList::iterator eiter = diter->entries.begin();
		if (_FindEntry(info.name, diter->entries, eiter)) {
			AddOnDisabled(&*eiter);
			break;
		}
	}

	// enable the new entry
	AddOnEnabled(&info);
}


/**
 * @brief Searches an EntryList for an entry matching the given node_ref.
 *
 * Advances the iterator @p it until either the entry whose nref equals
 * @p entry is found or the end of the list is reached.
 *
 * @param entry The node_ref to search for.
 * @param list  The EntryList to search within.
 * @param it    Iterator positioned at the start of the search range; advanced
 *              to the matching element on success.
 * @return true if a matching entry was found, false otherwise.
 */
bool
AddOnMonitorHandler::_FindEntry(const node_ref& entry, const EntryList& list,
	EntryList::iterator& it) const
{
	for (; EntryList::const_iterator(it) != list.end(); it++) {
		if (it->nref == entry)
			return true;
	}
	return false;
}


/**
 * @brief Searches an EntryList for an entry matching the given name.
 *
 * Advances the iterator @p it until either the entry whose name equals
 * @p name is found (strcmp) or the end of the list is reached.
 *
 * @param name The entry name to search for.
 * @param list The EntryList to search within.
 * @param it   Iterator positioned at the start of the search range; advanced
 *             to the matching element on success.
 * @return true if a matching entry was found, false otherwise.
 */
bool
AddOnMonitorHandler::_FindEntry(const char* name, const EntryList& list,
	EntryList::iterator& it) const
{
	for (; EntryList::const_iterator(it) != list.end(); it++) {
		if (strcmp(it->name, name) == 0)
			return true;
	}
	return false;
}


/**
 * @brief Returns whether an entry matching the given node_ref exists in a list.
 *
 * Convenience wrapper around _FindEntry(node_ref) that constructs a
 * temporary begin iterator.
 *
 * @param entry The node_ref to look for.
 * @param list  The EntryList to search.
 * @return true if the entry is present, false otherwise.
 */
bool
AddOnMonitorHandler::_HasEntry(const node_ref& entry, EntryList& list) const
{
	EntryList::iterator it = list.begin();
	return _FindEntry(entry, list, it);
}


/**
 * @brief Returns whether an entry matching the given name exists in a list.
 *
 * Convenience wrapper around _FindEntry(const char*) that constructs a
 * temporary begin iterator.
 *
 * @param name The entry name to look for.
 * @param list The EntryList to search.
 * @return true if the entry is present, false otherwise.
 */
bool
AddOnMonitorHandler::_HasEntry(const char* name, EntryList& list) const
{
	EntryList::iterator it = list.begin();
	return _FindEntry(name, list, it);
}


/**
 * @brief Searches fDirectories for a directory identified by inode and device.
 *
 * Constructs a node_ref from @p directory and @p device, then delegates to
 * the node_ref overload of _FindDirectory().
 *
 * @param directory Inode number of the directory to find.
 * @param device    Device ID of the filesystem.
 * @param it        Iterator advanced to the matching element on success.
 * @return true if the directory was found, false otherwise.
 */
bool
AddOnMonitorHandler::_FindDirectory(ino_t directory, dev_t device,
	DirectoryList::iterator& it) const
{
	node_ref nodeRef;
	make_node_ref(device, directory, &nodeRef);
	return _FindDirectory(nodeRef, it, fDirectories.end());
}


/**
 * @brief Searches fDirectories for a directory identified by node_ref.
 *
 * Delegates to the three-argument overload, passing fDirectories.end() as
 * the upper bound.
 *
 * @param directoryNodeRef The node_ref to search for.
 * @param it               Iterator advanced to the matching element on success.
 * @return true if the directory was found, false otherwise.
 */
bool
AddOnMonitorHandler::_FindDirectory(const node_ref& directoryNodeRef,
	DirectoryList::iterator& it) const
{
	return _FindDirectory(directoryNodeRef, it, fDirectories.end());
}


/**
 * @brief Searches a range of fDirectories for a directory identified by inode
 *        and device, up to a caller-supplied end iterator.
 *
 * Constructs a node_ref from @p directory and @p device, then delegates to
 * the node_ref / end-iterator overload.
 *
 * @param directory Inode number of the directory to find.
 * @param device    Device ID of the filesystem.
 * @param it        Iterator advanced to the matching element on success.
 * @param end       Exclusive upper bound for the search.
 * @return true if the directory was found before @p end, false otherwise.
 */
bool
AddOnMonitorHandler::_FindDirectory(ino_t directory, dev_t device,
	DirectoryList::iterator& it,
	const DirectoryList::const_iterator& end) const
{
	node_ref nodeRef;
	make_node_ref(device, directory, &nodeRef);
	return _FindDirectory(nodeRef, it, end);
}


/**
 * @brief Searches a range of fDirectories for a directory identified by
 *        node_ref, up to a caller-supplied end iterator.
 *
 * Advances @p it until a directory whose nref matches @p directoryNodeRef is
 * found or @p end is reached.
 *
 * @param directoryNodeRef The node_ref to search for.
 * @param it               Iterator advanced to the matching element on success.
 * @param end              Exclusive upper bound for the search.
 * @return true if the directory was found before @p end, false otherwise.
 */
bool
AddOnMonitorHandler::_FindDirectory(const node_ref& directoryNodeRef,
	DirectoryList::iterator& it,
	const DirectoryList::const_iterator& end) const
{
	for (; DirectoryList::const_iterator(it) != end; it++) {
		if (it->nref == directoryNodeRef)
			return true;
	}
	return false;
}


/**
 * @brief Resolves the real add-on node (following symlinks) and adds the entry
 *        to an EntryList with stat watching enabled.
 *
 * Opens the entry relative to its parent directory, resolves symlinks via
 * BEntry follow-link semantics, and starts a B_WATCH_STAT node monitor on the
 * resolved node so that in-place binary updates can be detected. The resolved
 * node_ref is stored in info.addon_nref; on failure info.nref is used as a
 * fallback.
 *
 * @param list The EntryList to append the new entry to.
 * @param info Reference to the add_on_entry_info to add; addon_nref is
 *             populated by this call.
 */
void
AddOnMonitorHandler::_AddNewEntry(EntryList& list, add_on_entry_info& info)
{
	BDirectory directory(&info.dir_nref);
	BEntry entry(&directory, info.name, true);

	node_ref addOnRef;
	if (entry.GetNodeRef(&addOnRef) == B_OK) {
		watch_node(&addOnRef, B_WATCH_STAT, this);
		info.addon_nref = addOnRef;
	} else
		info.addon_nref = info.nref;

	list.push_back(info);
}
