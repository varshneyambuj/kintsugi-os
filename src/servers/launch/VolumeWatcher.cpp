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
 *   Distributed under the terms of the MIT License.
 */

/** @file VolumeWatcher.cpp
 *  @brief Implements volume mount/unmount monitoring and listener notification for launch events. */


#include "VolumeWatcher.h"

#include <Application.h>
#include <Autolock.h>
#include <NodeMonitor.h>


/** @brief Global lock protecting the shared VolumeWatcher singleton and listener list. */
static BLocker sLocker("volume watcher");
/** @brief Singleton VolumeWatcher instance; created on first Register() call. */
static VolumeWatcher* sWatcher;


/** @brief Destructor for the VolumeListener interface. */
VolumeListener::~VolumeListener()
{
}


// #pragma mark -


/**
 * @brief Constructs the volume watcher and begins monitoring mount events.
 *
 * Registers itself as a BHandler with the application and starts node
 * monitoring for B_WATCH_MOUNT events.
 */
VolumeWatcher::VolumeWatcher()
	:
	BHandler("volume watcher")
{
	if (be_app->Lock()) {
		be_app->AddHandler(this);

		watch_node(NULL, B_WATCH_MOUNT, this);
		be_app->Unlock();
	}
}


/**
 * @brief Destroys the volume watcher and stops monitoring mount events.
 *
 * Stops node watching and removes itself from the application's handler list.
 */
VolumeWatcher::~VolumeWatcher()
{
	if (be_app->Lock()) {
		stop_watching(this);

		be_app->RemoveHandler(this);
		be_app->Unlock();
	}
}


/**
 * @brief Adds a listener to be notified of volume mount/unmount events.
 *
 * @param listener The VolumeListener to add.
 */
void
VolumeWatcher::AddListener(VolumeListener* listener)
{
	BAutolock lock(sLocker);
	fListeners.AddItem(listener);
}


/**
 * @brief Removes a previously added volume listener.
 *
 * @param listener The VolumeListener to remove.
 */
void
VolumeWatcher::RemoveListener(VolumeListener* listener)
{
	BAutolock lock(sLocker);
	fListeners.RemoveItem(listener);
}


/**
 * @brief Returns the number of currently registered volume listeners.
 *
 * @return The listener count.
 */
int32
VolumeWatcher::CountListeners() const
{
	BAutolock lock(sLocker);
	return fListeners.CountItems();
}


/**
 * @brief Handles incoming node-monitor messages for mount/unmount events.
 *
 * Dispatches B_DEVICE_MOUNTED and B_DEVICE_UNMOUNTED opcodes to all
 * registered VolumeListener instances.
 *
 * @param message The BMessage received from the node monitor.
 */
void
VolumeWatcher::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NODE_MONITOR:
		{
			int32 opcode = message->GetInt32("opcode", -1);
			if (opcode == B_DEVICE_MOUNTED) {
				dev_t device;
				if (message->FindInt32("new device", &device) == B_OK) {
					BAutolock lock(sLocker);
					for (int32 i = 0; i < fListeners.CountItems(); i++) {
						fListeners.ItemAt(i)->VolumeMounted(device);
					}
				}
			} else if (opcode == B_DEVICE_UNMOUNTED) {
				dev_t device;
				if (message->FindInt32("device", &device) == B_OK) {
					BAutolock lock(sLocker);
					for (int32 i = 0; i < fListeners.CountItems(); i++) {
						fListeners.ItemAt(i)->VolumeUnmounted(device);
					}
				}
			}
			break;
		}
	}
}


/**
 * @brief Registers a volume listener, creating the singleton watcher if needed.
 *
 * @param listener The VolumeListener to register.
 */
/*static*/ void
VolumeWatcher::Register(VolumeListener* listener)
{
	BAutolock lock(sLocker);
	if (sWatcher == NULL)
		sWatcher = new VolumeWatcher();

	sWatcher->AddListener(listener);
}


/**
 * @brief Unregisters a volume listener, destroying the singleton when the last listener is removed.
 *
 * @param listener The VolumeListener to unregister.
 */
/*static*/ void
VolumeWatcher::Unregister(VolumeListener* listener)
{
	BAutolock lock(sLocker);
	sWatcher->RemoveListener(listener);

	if (sWatcher->CountListeners() == 0)
		delete sWatcher;
}
