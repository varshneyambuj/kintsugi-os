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
 *   Copyright (c) 2002 Haiku Project
 *   Author: Michael Pfeiffer
 *   Licensed under the MIT License.
 */


/**
 * @file FolderWatcher.cpp
 * @brief Node-monitor wrapper that observes a directory for entry and attribute changes.
 *
 * FolderWatcher registers itself with the kernel node monitor to watch a given
 * BDirectory for file creation, removal, moves, and optional attribute changes.
 * It dispatches decoded notifications to a FolderListener, shielding callers
 * from the low-level B_NODE_MONITOR message format.
 *
 * @see Jobs.cpp, FolderListener
 */


#include "FolderWatcher.h"

#include <stdio.h>

// BeOS
#include <kernel/fs_attr.h>
#include <Node.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>


// Implementation of FolderWatcher

/**
 * @brief Constructs a FolderWatcher and begins monitoring the supplied directory.
 *
 * Adds this handler to \a looper so that node-monitor messages can be received,
 * then starts B_WATCH_DIRECTORY monitoring on \a folder. If \a watchAttrChanges
 * is true, B_WATCH_ATTR monitoring is also started for every file currently
 * present in the directory.
 *
 * @param looper             The BLooper that will receive node-monitor messages
 *                           on behalf of this handler.
 * @param folder             The directory to watch.
 * @param watchAttrChanges   If true, attribute-change notifications are also
 *                           enabled for all current and future entries.
 */
FolderWatcher::FolderWatcher(BLooper* looper, const BDirectory& folder, bool watchAttrChanges)
	: fFolder(folder)
	, fListener(NULL)
	, fWatchAttrChanges(watchAttrChanges)
{
		// add this handler to the application for node monitoring
	if (looper->Lock()) {
		looper->AddHandler(this);
		looper->Unlock();
	}

		// start attribute change watching for existing files
	if (watchAttrChanges) {
		BEntry entry;
		node_ref node;
		while (fFolder.GetNextEntry(&entry) == B_OK && entry.GetNodeRef(&node) == B_OK) {
			StartAttrWatching(&node);
		}
	}

		// start watching the spooler directory
	node_ref ref;
	fFolder.GetNodeRef(&ref);
	watch_node(&ref, B_WATCH_DIRECTORY, this);
}

/**
 * @brief Stops all node monitoring and removes this handler from its looper.
 *
 * Cancels B_WATCH_DIRECTORY monitoring for the folder and calls stop_watching()
 * to cancel any remaining per-node registrations before removing this handler
 * from its owning looper.
 */
FolderWatcher::~FolderWatcher() {
		// stop node watching for spooler directory
	node_ref ref;
	fFolder.GetNodeRef(&ref);
	watch_node(&ref, B_STOP_WATCHING, this);
		// stop sending notifications to this handler
	stop_watching(this);

	if (LockLooper()) {
		BLooper* looper = Looper();
			// and remove it
		looper->RemoveHandler(this);
		looper->Unlock();
	}
}

/**
 * @brief Sets the listener that receives decoded folder-change callbacks.
 *
 * @param listener  Pointer to the FolderListener implementation to notify.
 *                  Pass NULL to disable notifications.
 */
void FolderWatcher::SetListener(FolderListener* listener) {
	fListener = listener;
}

/**
 * @brief Constructs an entry_ref from a B_NODE_MONITOR message.
 *
 * Reads "device", the field named \a dirName (for the directory inode), and
 * "name" from \a msg and assembles them into \a entry.
 *
 * @param msg      The B_NODE_MONITOR BMessage to parse.
 * @param dirName  Name of the message field that holds the directory inode
 *                 (e.g. "directory" or "to directory").
 * @param entry    Receives the constructed entry_ref on success.
 * @return true if all required fields were found; false otherwise.
 */
bool FolderWatcher::BuildEntryRef(BMessage* msg, const char* dirName, entry_ref* entry) {
	const char* name;
	if (msg->FindInt32("device", &entry->device) == B_OK &&
		msg->FindInt64(dirName, &entry->directory) == B_OK &&
		msg->FindString("name", &name) == B_OK) {
		entry->set_name(name);
		return true;
	}
	return false;
}

/**
 * @brief Constructs a node_ref from a B_NODE_MONITOR message.
 *
 * Reads "device" and "node" fields from \a msg and stores them in \a node.
 *
 * @param msg   The B_NODE_MONITOR BMessage to parse.
 * @param node  Receives the constructed node_ref on success.
 * @return true if both fields were found; false otherwise.
 */
bool FolderWatcher::BuildNodeRef(BMessage* msg, node_ref* node) {
	return (msg->FindInt32("device", &node->device) == B_OK &&
		msg->FindInt64("node", &node->node) == B_OK);
}

/**
 * @brief Handles a B_ENTRY_CREATED or equivalent node-monitor event.
 *
 * Decodes the entry_ref and node_ref from \a msg using \a dirName as the
 * directory field name, optionally starts attribute watching on the new node,
 * and then notifies the listener via FolderListener::EntryCreated().
 *
 * @param msg      The incoming B_NODE_MONITOR BMessage.
 * @param dirName  Field name for the directory inode ("directory" or "to directory").
 */
void FolderWatcher::HandleCreatedEntry(BMessage* msg, const char* dirName) {
	node_ref node;
	entry_ref entry;
	if (BuildEntryRef(msg, dirName, &entry) &&
		BuildNodeRef(msg, &node)) {
		if (fWatchAttrChanges) StartAttrWatching(&node);
		fListener->EntryCreated(&node, &entry);
	}
}

/**
 * @brief Handles a B_ENTRY_REMOVED node-monitor event.
 *
 * Decodes the node_ref from \a msg, optionally stops attribute watching on
 * the removed node, and notifies the listener via FolderListener::EntryRemoved().
 *
 * @param msg  The incoming B_NODE_MONITOR BMessage.
 */
void FolderWatcher::HandleRemovedEntry(BMessage* msg) {
	node_ref node;
	if (BuildNodeRef(msg, &node)) {
		if (fWatchAttrChanges) StopAttrWatching(&node);
		fListener->EntryRemoved(&node);
	}
}

/**
 * @brief Handles a B_ATTR_CHANGED node-monitor event.
 *
 * Decodes the node_ref from \a msg and notifies the listener via
 * FolderListener::AttributeChanged().
 *
 * @param msg  The incoming B_NODE_MONITOR BMessage.
 */
void FolderWatcher::HandleChangedAttr(BMessage* msg) {
	node_ref node;
	if (BuildNodeRef(msg, &node)) {
		fListener->AttributeChanged(&node);
	}
}

/**
 * @brief Dispatches incoming messages, routing B_NODE_MONITOR events to handlers.
 *
 * Intercepts B_NODE_MONITOR messages and dispatches them to the appropriate
 * Handle* method based on the "opcode" field. B_ENTRY_MOVED is interpreted as
 * either a creation or a removal depending on whether the destination directory
 * matches the watched folder. All other messages are forwarded to the inherited
 * MessageReceived().
 *
 * @param msg  The incoming BMessage to process.
 */
void FolderWatcher::MessageReceived(BMessage* msg) {
	int32 opcode;
	node_ref folder;
	ino_t dir;

	if (msg->what == B_NODE_MONITOR) {
		if (fListener == NULL || msg->FindInt32("opcode", &opcode) != B_OK) return;

		switch (opcode) {
			case B_ENTRY_CREATED:
				HandleCreatedEntry(msg, "directory");
				break;
			case B_ENTRY_REMOVED:
				HandleRemovedEntry(msg);
				break;
			case B_ENTRY_MOVED:
				fFolder.GetNodeRef(&folder);
				if (msg->FindInt64("to directory", &dir) == B_OK && folder.node == dir) {
					// entry moved into this folder
					HandleCreatedEntry(msg, "to directory");
				} else if (msg->FindInt64("from directory", &dir) == B_OK && folder.node == dir) {
					// entry removed from this folder
					HandleRemovedEntry(msg);
				}
				break;
			case B_ATTR_CHANGED:
				HandleChangedAttr(msg);
				break;
			default: // nothing to do
				break;
		}
	} else {
		inherited::MessageReceived(msg);
	}
}

/**
 * @brief Begins attribute-change monitoring for a specific node.
 *
 * @param node  The node_ref identifying the file to watch.
 * @return B_OK on success, or an error code if watch_node() fails.
 */
status_t FolderWatcher::StartAttrWatching(node_ref* node) {
	return watch_node(node, B_WATCH_ATTR, this);
}

/**
 * @brief Stops attribute-change monitoring for a specific node.
 *
 * @param node  The node_ref identifying the file to stop watching.
 * @return B_OK on success, or an error code if watch_node() fails.
 */
status_t FolderWatcher::StopAttrWatching(node_ref* node) {
	return watch_node(node, B_STOP_WATCHING, this);
}
