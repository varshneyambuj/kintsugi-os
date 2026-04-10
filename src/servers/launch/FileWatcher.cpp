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
 *   Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2025, Jérôme Duval, jerome.duval@gmail.com.
 *   Distributed under the terms of the MIT License.
 */

/** @file FileWatcher.cpp
 *  @brief Implements file creation monitoring and listener notification for launch events. */


#include "FileWatcher.h"

#include <Application.h>
#include <Autolock.h>
#include <Path.h>

#include <PathMonitor.h>

#include <stdio.h>


class Path;


/** @brief Global lock protecting the shared FileWatcher singleton and listener list. */
static BLocker sLocker("file watcher");
/** @brief Singleton FileWatcher instance; created on first Register() call. */
static FileWatcher* sWatcher;


/** @brief Destructor for the FileListener interface. */
FileListener::~FileListener()
{
}


// #pragma mark -


/**
 * @brief Constructs the file watcher and begins monitoring mount events.
 *
 * Registers itself as a BHandler with the application and starts node
 * monitoring for B_WATCH_MOUNT events.
 */
FileWatcher::FileWatcher()
	:
	BHandler("file watcher")
{
	if (be_app->Lock()) {
		be_app->AddHandler(this);

		watch_node(NULL, B_WATCH_MOUNT, this);
		be_app->Unlock();
	}
}


/**
 * @brief Destroys the file watcher and stops all monitoring.
 *
 * Stops node watching and removes itself from the application's handler list.
 */
FileWatcher::~FileWatcher()
{
	if (be_app->Lock()) {
		stop_watching(this);

		be_app->RemoveHandler(this);
		be_app->Unlock();
	}
}


/**
 * @brief Adds a listener to be notified when watched files are created.
 *
 * @param listener The FileListener to add.
 */
void
FileWatcher::AddListener(FileListener* listener)
{
	BAutolock lock(sLocker);
	fListeners.AddItem(listener);
}


/**
 * @brief Removes a previously added file listener.
 *
 * @param listener The FileListener to remove.
 */
void
FileWatcher::RemoveListener(FileListener* listener)
{
	BAutolock lock(sLocker);
	fListeners.RemoveItem(listener);
}


/**
 * @brief Returns the number of currently registered file listeners.
 *
 * @return The listener count.
 */
int32
FileWatcher::CountListeners() const
{
	BAutolock lock(sLocker);
	return fListeners.CountItems();
}


/**
 * @brief Handles incoming path-monitor messages for file creation events.
 *
 * Dispatches B_ENTRY_CREATED opcodes to all registered FileListener
 * instances, passing the watched path to FileCreated().
 *
 * @param message The BMessage received from the path monitor.
 */
void
FileWatcher::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_PATH_MONITOR:
		{
			int32 opcode = message->GetInt32("opcode", -1);
			if (opcode == B_ENTRY_CREATED) {
				const char* path;
				const char* watchedPath;
				if (message->FindString("watched_path", &watchedPath) != B_OK
					|| message->FindString("path", &path) != B_OK) {
					break;
				}

				BAutolock lock(sLocker);
				for (int32 i = 0; i < fListeners.CountItems(); i++)
					fListeners.ItemAt(i)->FileCreated(watchedPath);
			}
			break;
		}
	}
}


/**
 * @brief Registers a file listener for creation events at the given path.
 *
 * Creates the singleton FileWatcher if needed, starts path monitoring on
 * @a path for files only, and adds the listener to the notification list.
 *
 * @param listener The FileListener to register.
 * @param path     The filesystem path to monitor for new file entries.
 * @return B_OK on success, or an error code if path monitoring could not start.
 */
/*static*/ status_t
FileWatcher::Register(FileListener* listener, BPath& path)
{
	BAutolock lock(sLocker);
	if (sWatcher == NULL)
		sWatcher = new FileWatcher();

	status_t status = BPathMonitor::StartWatching(path.Path(),
			B_WATCH_FILES_ONLY, sWatcher);
	if (status != B_OK)
		return status;
	sWatcher->AddListener(listener);
	return B_OK;
}


/**
 * @brief Unregisters a file listener, destroying the singleton when the last listener is removed.
 *
 * Stops path monitoring for @a path and removes the listener. If no
 * listeners remain, the singleton FileWatcher is deleted.
 *
 * @param listener The FileListener to unregister.
 * @param path     The filesystem path to stop monitoring.
 */
/*static*/ void
FileWatcher::Unregister(FileListener* listener, BPath& path)
{
	BAutolock lock(sLocker);
	BPathMonitor::StopWatching(path.Path(), sWatcher);
	sWatcher->RemoveListener(listener);

	if (sWatcher->CountListeners() == 0)
		delete sWatcher;
}
